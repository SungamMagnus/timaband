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
per-band gain meters. The look is the Sungam palette: flat paper and ink, where
colour only ever means a signal.

## Presets

The header's preset pill opens a menu of the factory presets (Default, Init, Punchy
Kit, Soft Attack, Tight Low End, Snappy Snare, Room Tamer, Drum Bus Glue), then your
own, then Save, Save As..., Delete (for a loaded user preset, with a confirmation) and
Show Presets Folder. The triangles step to the previous and next preset, wrapping
around. A dot after the name means the settings no longer match the loaded preset.

A preset recalls every band parameter (attack, sustain, curve, mute, solo, band
bypass), both crossovers, lookahead and stereo link. It does not touch the global
bypass, delta, input gain or output gain: those are monitoring and level settings, and
they stay as they are when you switch presets.

User presets are XML files in `~/Library/Audio/Presets/Sungam/Timaband/<name>.timabandpreset`,
the same format as the plug-in's state with the excluded parameters left out. The
folder is created on the first save and re-read whenever the menu opens, so files you
drop in appear. Save overwrites the loaded user preset; Save As asks for a name and
refuses the name of a factory preset. The loaded preset's name is part of the session
state. The factory presets are also the host's programs, so the host's own preset menu
works.

## Undo and redo

The two round buttons in the header (and Cmd/Ctrl+Z, Shift+Cmd/Ctrl+Z, and Ctrl+Y on
Windows) step through the editor's edits. One gesture is one step: a slider, crossover
or fader drag, a button or curve click, Reset, a preset load, and a Link bands drag
that moves three parameters are each a single step. Wheel and arrow-key bursts count as
one step while they follow each other within 800 ms. Undo and redo restore the
parameters and the preset name, so the modified dot follows.

Not undoable: UI-only state (selected band, analyzer Pre/Post, Link bands, window
size) and host automation. Only changes made inside the editor's own begin/end
gestures are recorded, so automation playback never fills the stack or lights Undo.
The history is cleared when the host sets the session state.

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
`docs/panel.png` (the Default preset), `docs/panel-solo.png` (mid soloed, high muted),
`docs/panel-bypass.png` (low band bypassed) and `docs/panel-presets.png` (the preset
menu open, with two user presets and a modified preset loaded). `ui_check` drives the
editor's controls headlessly and checks the parameter gestures, link bands, the
crossover handles, the curve buttons, the gain faders, presets (menu, loading, the
user-file round trip, programs, state) and undo/redo, and the spectrum maths. Both use
a temporary presets folder; the real one is never touched.

The default palette is the light Sungam one. `panel_shot_dark` and `ui_check_dark` build
the same tools with the original dark palette (`TIMABAND_THEME_DARK=1`); the renders go
to `docs/dark-theme/`. Every check passes in both palettes.

```sh
cmake --build build --config Release --target panel_shot ui_check panel_shot_dark ui_check_dark -j8

./build/panel_shot_artefacts/Release/panel_shot
./build/ui_check_artefacts/Release/ui_check
./build/panel_shot_dark_artefacts/Release/panel_shot_dark
./build/ui_check_dark_artefacts/Release/ui_check_dark
```

## Validate

```sh
auval -v aufx Tmbd Sngm
/Applications/pluginval.app/Contents/MacOS/pluginval --strictness-level 5 --validate ~/Library/Audio/Plug-Ins/VST3/Timaband.vst3
/Applications/pluginval.app/Contents/MacOS/pluginval --strictness-level 5 --validate ~/Library/Audio/Plug-Ins/Components/Timaband.component
```

If `auval` cannot find the component after a fresh install, run
`killall -9 AudioComponentRegistrar` and try again.
