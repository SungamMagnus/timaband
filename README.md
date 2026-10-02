# Tímaband

A three-band transient designer. VST3 / AU / standalone for macOS.

The signal is split into low, mid and high with Linkwitz-Riley 4th-order
crossovers. Each band has its own attack and sustain control, with a choice of
response curve, plus mute and solo. A short lookahead lets the shaping land on
the transient itself, and a delta mode lets you hear only what the shaper is
adding or removing.

The build plan, signal flow and full parameter table are in
[`docs/PLAN.md`](docs/PLAN.md). The editor is a port of
[`docs/ui-mockup.html`](docs/ui-mockup.html): a 1120 x 640 design scaled by a
single transform (0.6x to 1.5x), with a live spectrum analyzer, level meters and
per-band gain meters. Presets and undo come in a later phase.

## Build

Needs [JUCE](https://juce.com) 9 and CMake 3.22 or later.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j8
```

`JUCE_DIR` defaults to `/Applications/JUCE`; pass `-DJUCE_DIR=/path/to/JUCE` to
use another checkout. The build copies the VST3 and AU into your user plug-in
folders:

```
VST3  ->  ~/Library/Audio/Plug-Ins/VST3/Timaband.vst3
AU    ->  ~/Library/Audio/Plug-Ins/Components/Timaband.component
```

The binary is universal (`arm64;x86_64`). Check it with `lipo -archs`, not with
`auval`.

### DSP checks

`tools/dsp_check.cpp` drives `timaband::Engine` offline, with no plug-in wrapper:
flat band sum, latency and bypass, delta null, mute/solo routing, shaping,
curves, extreme settings across sample rates and block sizes, and independent
stereo channels. It prints PASS or FAIL for each check and exits non-zero if any
fail.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DTIMABAND_TOOLS=ON
cmake --build build --config Release --target dsp_check -j8

./build/dsp_check_artefacts/Release/dsp_check
```

### Editor renders and checks

`panel_shot` renders the real editor, fed with synthetic drum audio, to
`docs/panel.png` (the mockup's default state), `docs/panel-solo.png` (mid soloed,
high muted) and `docs/panel-bypass.png` (low band bypassed). `ui_check` drives the
editor's controls headlessly and checks the parameter gestures, link bands, the
crossover handles, the gain faders and the spectrum maths.

```sh
cmake --build build --config Release --target panel_shot ui_check -j8

./build/panel_shot_artefacts/Release/panel_shot
./build/ui_check_artefacts/Release/ui_check
```

## Validate

```sh
auval -v aufx Tmbd Sngm
/Applications/pluginval.app/Contents/MacOS/pluginval --strictness-level 5 --validate ~/Library/Audio/Plug-Ins/VST3/Timaband.vst3
/Applications/pluginval.app/Contents/MacOS/pluginval --strictness-level 5 --validate ~/Library/Audio/Plug-Ins/Components/Timaband.component
```

If `auval` cannot find the component after a fresh install, run
`killall -9 AudioComponentRegistrar` and try again.
