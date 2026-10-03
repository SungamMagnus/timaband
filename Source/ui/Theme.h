#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Parameters.h"
#include "../dsp/Shaper.h"

// Colours, fonts and the design-space geometry, copied from the :root tokens and
// layout values of docs/ui-mockup.html. The editor lays everything out at the
// fixed design size and scales it with a transform, so these are in design pixels.
//
// The default palette is the dark one. Building with TIMABAND_THEME_SUNGAM=1 swaps in the
// Sungam design system (flat paper and ink; colour only ever means a signal) with the
// same names and the same layout, so only colours change.
#ifndef TIMABAND_THEME_SUNGAM
 #define TIMABAND_THEME_SUNGAM 0
#endif

namespace timaband::ui
{

namespace colours
{
using juce::Colour;

#if TIMABAND_THEME_SUNGAM

const Colour paper  { 0xfff0ece2 };
const Colour ink    { 0xff1a1a17 };
const Colour ink62  { 0xff6b6a64 };
const Colour ink55  { 0xff7a7972 };
const Colour ink45  { 0xff908e87 };
const Colour ink38  { 0xff9f9c95 };
const Colour ink28  { 0xffb4b1a9 };
const Colour ink08  { 0xffdfdbd2 };
const Colour ink13  { 0x211a1a17 };     // alpha
const Colour ink18  { 0x2e1a1a17 };     // alpha

const Colour page    = paper;
const Colour chrome  = paper;
const Colour panel   = paper;
const Colour graph   = paper;
const Colour strip   = paper;
const Colour line    = ink13;
const Colour gridDot = ink18;
const Colour ctrl    = ink08;
const Colour ctrlHi  = ink13;
const Colour text    = ink;
const Colour muted   = ink62;
const Colour dim     = ink38;

const Colour low      { 0xffed8159 };
const Colour mid      { 0xff52b0a4 };
const Colour high     { 0xff4f7ea8 };
const Colour lowSoft  { 0xffeeb69d };
const Colour midSoft  { 0xffa1cec3 };
const Colour highSoft { 0xffa0b5c5 };

const Colour accent    = ink.withAlpha (0.55f);     // focus ring
const Colour outline   = ink28;
const Colour outlineHi = ink55;
const Colour white     = paper;
const Colour spec      = ink45;

const Colour meterBg   = ink08;
const Colour meterLo   = ink55;     // flat, no gradient
const Colour meterHi   = ink55;
const Colour menuBorder = ink28;

#else

const Colour page     { 0xff0c0e11 };
const Colour chrome   { 0xff1a1e23 };
const Colour panel    { 0xff14171b };
const Colour graph    { 0xff101317 };
const Colour strip    { 0xff0e1114 };
const Colour line     { 0xff262c33 };
const Colour gridDot  { 0xff2c333b };
const Colour ctrl     { 0xff262c33 };
const Colour ctrlHi   { 0xff323a43 };
const Colour text     { 0xffe7eaee };
const Colour muted    { 0xff8a939d };
const Colour dim      { 0xff5b636c };

const Colour low      { 0xff2fb3ea };
const Colour mid      { 0xfff3a83b };
const Colour high     { 0xff3ed3b2 };
const Colour lowSoft  { 0xff8cc5e0 };
const Colour midSoft  { 0xffe3bf88 };
const Colour highSoft { 0xff8fd3c4 };

const Colour accent    { 0xff9fb4c4 };
const Colour outline   { 0xff8f98a2 };
const Colour outlineHi { 0xffcfd5db };
const Colour white     { 0xffffffff };
const Colour ink       { 0xff0c0e11 };
const Colour spec      { 0xff9aa3ad };

const Colour meterBg   { 0xff0b0d10 };
const Colour meterLo   { 0xff9fb4c4 };
const Colour meterHi   { 0xffdfe7ee };
const Colour menuBorder { 0xff444d58 };

#endif

inline Colour band (int b) noexcept
{
    const Colour all[] = { low, mid, high };
    return all[juce::jlimit (0, 2, b)];
}

inline Colour bandSoft (int b) noexcept
{
    const Colour all[] = { lowSoft, midSoft, highSoft };
    return all[juce::jlimit (0, 2, b)];
}

// Roles whose colour does not simply follow the base names above, because what is
// dark on a dark panel is light on paper. The code asks for the role, so both palettes
// draw the same controls.
#if TIMABAND_THEME_SUNGAM

const Colour onText         = paper;                // text on an ink fill
const Colour onBandText     = paper;                // text or glyph on a band-coloured or ink fill
const Colour onFill         = ink;                  // a selected segment, a pressed-in Bypass or Delta
const Colour pillOnFill     = ink;
const Colour pillOnOutline  = ink;
const Colour pillDotOn      = paper;
const Colour ringIdle       = ink28;                // the M, S and power rings
const Colour ringHover      = ink55;
const Colour trackFill      = ink08;                // slider and gain meter grooves
const Colour hoverFill      = ink13;
const Colour thumbBody      = ink;
const Colour thumbDot       = paper;
const Colour faderGroove    = ink13;
const Colour faderGrooveHi  = ink13;
const Colour faderZero      = ink13;
const Colour faderZeroHi    = ink13;
const Colour faderHandle    = paper;
const Colour faderHandleHi  = ink08;
const Colour faderEdge      = ink55;
const Colour faderEdgeHi    = ink;
const Colour faderMark      = ink;
const Colour menuBg         = paper;                // tooltips and menus
const Colour editText       = ink;
const Colour handleLine     = ink;
const Colour handleDragFill = ink;
const Colour handleHover    = ink13;
const Colour hatch          = ink.withAlpha (0.10f);
const Colour gridLine       = ink08;                // the transfer graphs
const Colour specLine       = ink45;
const Colour specFill       = ink;
const Colour peakLine       = ink;
const Colour bypassed       = ink38;                // a bypassed band's accent

const Colour tabOutline     = ink28;
const Colour tabIdle        = paper;
const Colour tabIdleHi      = ink08;
const Colour tabGlyph       = ink62;
const Colour tabGlyphOn     = paper;
const Colour tabBypassed    = ink38;

#else

const Colour onText         = white;
const Colour onBandText     = ink;
const Colour onFill         = ctrlHi;
const Colour pillOnFill     = white.withAlpha (0.05f);
const Colour pillOnOutline  = outlineHi;
const Colour pillDotOn      = accent;
const Colour ringIdle       = muted;
const Colour ringHover      = text;
const Colour trackFill      = ctrlHi;
const Colour hoverFill      = ctrl;
const Colour thumbBody      = white;
const Colour thumbDot       = ink;
const Colour faderGroove    = ctrl.withAlpha (0.5f);
const Colour faderGrooveHi  = ctrl.withAlpha (0.9f);
const Colour faderZero      = muted.withAlpha (0.5f);
const Colour faderZeroHi    = muted.withAlpha (0.9f);
const Colour faderHandle    = ctrl;
const Colour faderHandleHi  = ctrlHi;
const Colour faderEdge      = outline;
const Colour faderEdgeHi    = outlineHi;
const Colour faderMark      = low;
const Colour menuBg         = ctrlHi;
const Colour editText       = white;
const Colour handleLine     = text.withAlpha (0.9f);
const Colour handleDragFill = white;
const Colour handleHover    = white.withAlpha (0.18f);
const Colour hatch          = white.withAlpha (0.06f);
const Colour gridLine       = white.withAlpha (0.045f);
const Colour specLine       = spec;
const Colour specFill       = spec;
const Colour peakLine       = white.withAlpha (0.9f);
const Colour bypassed       = muted;

// The curve buttons look like Max's live.tab: grey keys, dark outline and glyph.
const Colour tabOutline     { 0xff2b2b2b };
const Colour tabIdle        { 0xff9a9a9a };
const Colour tabIdleHi      { 0xffb0b0b0 };
const Colour tabGlyph       { 0xff1c1c1c };
const Colour tabGlyphOn     { 0xff1c1c1c };
const Colour tabBypassed    { 0xffc8c8c8 };

#endif
} // namespace colours

// What else differs between the palettes besides colour values.
namespace look
{
#if TIMABAND_THEME_SUNGAM
constexpr bool flat = true;                     // no shadows, glows or gradients
constexpr float columnTint = 0.04f;             // the selected column's wash
constexpr float spectrumLineAlpha = 1.0f;       // an unselected band's spectrum
constexpr float spectrumFillAlpha = 0.08f;
constexpr float selectedFillAlpha = 0.14f;      // the selected band's spectrum fill
#else
constexpr bool flat = false;
constexpr float columnTint = 0.035f;
constexpr float spectrumLineAlpha = 0.7f;
constexpr float spectrumFillAlpha = 0.07f;
constexpr float selectedFillAlpha = 0.18f;
#endif
} // namespace look

namespace layout
{
constexpr int designW = 1120, designH = 640;
constexpr double minScale = 0.6, maxScale = 1.5;

constexpr int headerH = 56;
constexpr int leftW = 920, rightW = 200;
constexpr int toolbarH = 44;

// Crossover display: a 46 px strip for the handles above a 164 px graph.
constexpr int graphW = 920, stripH = 46, graphH = 164, displayH = stripH + graphH;

constexpr int linkRowH = 38;

// Slider geometry (the mockup's --tl and --thumb).
constexpr int trackLen = 120, thumbSize = 22;
constexpr float dbRange = 15.0f;

// Meter geometry (the mockup's METER_TOP / METER_H / METER_FLOOR).
constexpr float meterTop = 8.0f, meterH = 334.0f, meterFloorDb = 60.0f;
} // namespace layout

namespace theme
{
// The CSS sizes are em sizes in px, so the fonts are specified by point height.
// Bold stands in for the mockup's 600 weight; 400 and 500 are both regular.
inline juce::Font font (float px, bool bold = false, float letterSpacingPx = 0.0f)
{
    auto f = juce::Font (juce::FontOptions().withPointHeight (px))
                 .withStyle (bold ? juce::Font::bold : juce::Font::plain);

    if (letterSpacingPx != 0.0f)
        f = f.withExtraKerningFactor (letterSpacingPx / px);

    return f;
}

inline juce::String utf8 (const char* s)
{
    return juce::String (juce::CharPointer_UTF8 (s));
}

inline juce::String appName()    { return utf8 ("T\xc3\xadmaband"); }
inline juce::String enDash()     { return utf8 ("\xe2\x80\x93"); }
inline juce::String minusSign()  { return utf8 ("\xe2\x88\x92"); }
inline juce::String deltaSign()  { return utf8 ("\xce\x94"); }

// Slider readout: "+4.5", "-2.0" (with a true minus sign), "0.0".
inline juce::String formatDb (float db)
{
    if (std::abs (db) < 0.05f)
        return "0.0";

    return (db > 0.0f ? juce::String ("+") : minusSign()) + juce::String (std::abs (db), 1);
}

// Meter readout: one decimal, "-Inf" at the floor.
inline juce::String formatMeter (float db)
{
    if (db <= -59.5f)
        return minusSign() + "Inf";

    return (db < 0.0f ? minusSign() : juce::String()) + juce::String (std::abs (db), 1);
}

// Band range text. Unlike the host-facing hzReadout, whole hertz below 1 kHz.
inline juce::String formatHz (float hz)
{
    if (hz >= 19999.5f)
        return "20 kHz";

    if (hz >= 1000.0f)
        return juce::String (hz / 1000.0f, 2) + " kHz";

    return juce::String (juce::roundToInt (hz)) + " Hz";
}

// 0..1 in, 0..1 out, exactly what the DSP applies.
inline float curve (int curveIndex, float x) noexcept
{
    return applyCurve ((Curve) juce::jlimit (0, 2, curveIndex), x);
}

// A polyline of the curve for the glyphs and mini graphs.
inline juce::Path curvePath (int curveIndex, juce::Rectangle<float> area, int segments)
{
    juce::Path p;

    for (int k = 0; k <= segments; ++k)
    {
        const auto x = (float) k / (float) segments;
        const juce::Point<float> pt (area.getX() + x * area.getWidth(),
                                     area.getBottom() - curve (curveIndex, x) * area.getHeight());
        if (k == 0) p.startNewSubPath (pt); else p.lineTo (pt);
    }

    return p;
}

// The glyph on a curve button: the curve across the box, stroked on its centre line.
inline void drawCurveGlyph (juce::Graphics& g, int curveIndex, juce::Rectangle<float> box, juce::Colour colour)
{
    g.setColour (colour);
    g.strokePath (curvePath (curveIndex, box, 20),
                  juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
} // namespace theme

} // namespace timaband::ui
