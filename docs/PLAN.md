# Tímaband — build plan

Three-band transient designer (VST3 / AU / Standalone, macOS universal binary).
UI reference: [`ui-mockup.html`](ui-mockup.html) (open in a browser; it is interactive).

## Identity

| Field | Value |
|---|---|
| Display name (`PLUGIN_NAME`) | `Tímaband` — falls back to `Timaband` if the accent breaks validation |
| Target / bundle file (`PRODUCT_NAME`) | `Timaband` (ASCII) |
| Company | `Sungam`, manufacturer code `Sngm` |
| Plugin code | `Tmbd` |
| Bundle ID | `com.sungam.timaband` |
| JUCE | 9.0.1 at `/Applications/JUCE` (same pattern as `~/code/whoomp`) |

## Signal flow

```
in ─► input gain ─► LR4 3-band split ─┬─ low  ─► shaper ─► mute/solo ─┐
                                      ├─ mid  ─► shaper ─► mute/solo ─┼─► sum ─► output gain ─► bypass/delta ─► out
                                      └─ high ─► shaper ─► mute/solo ─┘
```

- **Crossover**: Linkwitz-Riley 4th order (`juce::dsp::LinkwitzRileyFilter`).
  low = LP(f1) → AP(f2); mid = HP(f1) → LP(f2); high = HP(f1) → HP(f2).
  The low-band allpass makes the sum magnitude-flat. Crossover frequencies are
  smoothed and coefficients updated every 32-sample sub-block. The DSP enforces
  `f2 ≥ f1 · 1.26` (1/3 octave) regardless of what the host automates.
- **Shaper (per band)**, detector stereo-linked (max |L|,|R|) when `stereoLink` is on:
  - Attack detector: peak envelopes with equal release, fast vs slow attack.
    `t = clamp((dB(fast) − dB(slow)) / 12, 0, 1)`.
  - Sustain detector: peak envelopes with equal attack, fast vs slow release.
    `s = clamp((dB(slowRel) − dB(fastRel)) / 12, 0, 1)`.
  - Time constants scale per band (low ×2, mid ×1, high ×0.6) so low-frequency
    cycles don't ripple the envelopes.
  - Curve `f` applied to both: Exponential `(e^{4x}−1)/(e^4−1)`, Linear `x`,
    Logarithmic `ln(1+20x)/ln(21)`.
  - `gain_dB = attack · f(t) + sustain · f(s)`, clamped ±24 dB, one-pole smoothed.
- **Lookahead** (3 ms when on): the band audio is delayed, the detector is not.
  Latency is reported with `setLatencySamples`.
- **Mute / solo**: 5 ms gain ramps. A band is audible if `anySolo ? solo : !mute`.
- **Band bypass**: scales that band's shaping gain toward 0 dB over 10 ms. The detector keeps
  running, and a fully bypassed band is bit-identical to the same band at zero shaping.
- **Delta**: outputs `Σ (g_b − 1) · band_b` — what the shaper adds or removes.
  It nulls at zero settings because it subtracts the summed crossover output, not the raw input.
- **Bypass**: exposed as the host bypass parameter. Crossfades (10 ms) to the input,
  delayed by the current latency so the timing stays constant.

## Parameters (`juce::ParameterID{id, 1}`)

| ID | Name | Range | Default | Notes |
|---|---|---|---|---|
| `xover1` | Low/Mid Crossover | 40 – 16 000 Hz, log | 180 | |
| `xover2` | Mid/High Crossover | 40 – 16 000 Hz, log | 2400 | |
| `{band}Attack` | {Band} Attack | −15 – +15 dB, step 0.1 | 0 | band ∈ `low`, `mid`, `high` |
| `{band}Sustain` | {Band} Sustain | −15 – +15 dB, step 0.1 | 0 | |
| `{band}Curve` | {Band} Curve | Exponential, Linear, Logarithmic | Linear | choice |
| `{band}Mute` | {Band} Mute | bool | off | |
| `{band}Solo` | {Band} Solo | bool | off | |
| `{band}Bypass` | {Band} Bypass | bool | off | band plays unshaped (10 ms crossfade); unlike mute |
| `inputGain` | Input Gain | −24 – +24 dB, step 0.1 | 0 | fader on the IN meters |
| `outputGain` | Output Gain | −24 – +24 dB, step 0.1 | 0 | fader on the OUT meters |
| `lookahead` | Lookahead | bool | on | changes latency |
| `stereoLink` | Stereo Link | bool | on | |
| `delta` | Delta | bool | off | |
| `bypass` | Bypass | bool | off | returned by `getBypassParameter()` |

UI-only state (stored in the state tree, not automatable): selected band,
analyzer Pre/Post, Link bands.

## Files

```
CMakeLists.txt
Source/
  PluginProcessor.h/.cpp   APVTS, bus layouts (mono and stereo), state, reads params → Engine::Settings
  PluginEditor.h/.cpp      Phase 1–2: generic editor. Phase 3: the mockup UI
  Parameters.h/.cpp        IDs and createLayout()
  dsp/Engine.h/.cpp        crossover + 3 shapers + mute/solo + lookahead + delta/bypass (no plugin wrapper deps)
  dsp/Shaper.h             envelope followers, curves, gain computer
tools/
  dsp_check.cpp            offline tests of Engine (-DTIMABAND_TOOLS=ON)
docs/
  PLAN.md, ui-mockup.html
```

## Phases

1. **Scaffold** — CMake, parameters, pass-through processor, generic editor, builds and validates. *(Sonnet)*
2. **DSP** — `Engine` + `Shaper`, wired into the processor; `dsp_check` covers flat band sum,
   null at zero settings, delta null, latency, mute/solo routing, silence/denormals,
   extreme params, 44.1–192 kHz, block sizes 1–4096. *(Opus writes DSP, Sonnet writes checks)*
3. **UI** — LookAndFeel and components ported from the mockup at 1120×640, scalable. *(Sonnet)*
4. **Analyzer and meters** — lock-free FIFO → FFT spectrum, I/O peak/RMS, per-band gain meters. *(Sonnet)*
5. **Polish** — presets, pluginval at strictness 10, auval, listening tests in Live and REAPER. *(Opus review)*
