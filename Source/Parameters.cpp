#include "Parameters.h"

#include <cmath>

#include "dsp/Shaper.h"

namespace timaband
{

const char* const bandNames[numBands] = { "Low", "Mid", "High" };
const char* const curveNames[3]       = { "Exponential", "Linear", "Logarithmic" };

// The choice index goes straight into timaband::Curve, so the order matters.
static_assert ((int) Curve::exponential == 0 && (int) Curve::linear == 1 && (int) Curve::logarithmic == 2,
               "curveNames must follow the Curve enum");

namespace pid
{
const juce::String xover1 = "xover1";
const juce::String xover2 = "xover2";

const juce::String attack[numBands]  = { "lowAttack",  "midAttack",  "highAttack" };
const juce::String sustain[numBands] = { "lowSustain", "midSustain", "highSustain" };
const juce::String curve[numBands]   = { "lowCurve",   "midCurve",   "highCurve" };
const juce::String mute[numBands]    = { "lowMute",    "midMute",    "highMute" };
const juce::String solo[numBands]    = { "lowSolo",    "midSolo",    "highSolo" };
const juce::String bandBypass[numBands] = { "lowBypass", "midBypass", "highBypass" };

const juce::String inputGain  = "inputGain";
const juce::String outputGain = "outputGain";
const juce::String lookahead  = "lookahead";
const juce::String stereoLink = "stereoLink";
const juce::String delta      = "delta";
const juce::String bypass     = "bypass";
}

namespace uiState
{
const juce::Identifier selectedBand ("selectedBand");
const juce::Identifier analyzerPost ("analyzerPost");
const juce::Identifier linkBands ("linkBands");
const juce::Identifier editorScale ("editorScale");
}

/* ── Readouts ────────────────────────────────────────────────────────────── */

juce::String hzReadout (float hz)
{
    if (hz >= 1000.0f)
        return juce::String (hz / 1000.0f, hz < 10000.0f ? 2 : 1) + " kHz";

    return (hz < 100.0f ? juce::String (hz, 1)
                        : juce::String (juce::roundToInt (hz))) + " Hz";
}

juce::String dbReadout (float db)
{
    // Anything that rounds to zero would otherwise print as "-0.0".
    if (std::abs (db) < 0.05f)
        return "0.0 dB";

    return (db > 0.0f ? "+" : "") + juce::String (db, 1) + " dB";
}

/* ── Layout ──────────────────────────────────────────────────────────────── */

namespace
{
using Range  = juce::NormalisableRange<float>;
using Attrib = juce::AudioParameterFloatAttributes;

// Equal distance on the knob is an equal pitch interval: f = start * (end / start)^norm.
Range logRange (float lo, float hi)
{
    return Range (lo, hi,
                  [] (float start, float end, float norm)  { return start * std::pow (end / start, norm); },
                  [] (float start, float end, float value) { return std::log (juce::jlimit (start, end, value) / start) / std::log (end / start); },
                  [] (float, float, float value)           { return value; });
}

float parseHz (const juce::String& text)
{
    const auto t = text.trim().toLowerCase();
    const auto value = t.getFloatValue();
    return t.contains ("k") ? value * 1000.0f : value;
}

std::unique_ptr<juce::AudioParameterFloat> makeFloat (const juce::String& id, const juce::String& name,
                                                      Range range, float def,
                                                      std::function<juce::String (float, int)> toText,
                                                      std::function<float (const juce::String&)> fromText = {})
{
    auto attributes = Attrib().withStringFromValueFunction (std::move (toText));

    if (fromText != nullptr)
        attributes = attributes.withValueFromStringFunction (std::move (fromText));

    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, range, def, attributes);
}

std::unique_ptr<juce::AudioParameterBool> makeBool (const juce::String& id, const juce::String& name, bool def)
{
    return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def);
}

const auto hz = [] (float v, int) { return hzReadout (v); };
const auto db = [] (float v, int) { return dbReadout (v); };
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (makeFloat (pid::xover1, "Low/Mid Crossover", logRange (40.0f, 16000.0f), 180.0f, hz, parseHz));
    layout.add (makeFloat (pid::xover2, "Mid/High Crossover", logRange (40.0f, 16000.0f), 2400.0f, hz, parseHz));

    // Steps of 0.1 dB: finer than anyone can hear, and the readout shows one decimal.
    const Range shapeRange (-15.0f, 15.0f, 0.1f);

    for (int b = 0; b < numBands; ++b)
    {
        const juce::String band (bandNames[b]);

        layout.add (makeFloat (pid::attack[b],  band + " Attack",  shapeRange, 0.0f, db));
        layout.add (makeFloat (pid::sustain[b], band + " Sustain", shapeRange, 0.0f, db));

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { pid::curve[b], 1 }, band + " Curve",
            juce::StringArray { curveNames[0], curveNames[1], curveNames[2] },
            (int) Curve::linear));

        layout.add (makeBool (pid::mute[b], band + " Mute", false));
        layout.add (makeBool (pid::solo[b], band + " Solo", false));
        layout.add (makeBool (pid::bandBypass[b], band + " Bypass", false));
    }

    const Range gainRange (-24.0f, 24.0f, 0.1f);
    layout.add (makeFloat (pid::inputGain,  "Input Gain",  gainRange, 0.0f, db));
    layout.add (makeFloat (pid::outputGain, "Output Gain", gainRange, 0.0f, db));

    // Lookahead trades 3 ms of latency for shaping that lands on the transient itself.
    layout.add (makeBool (pid::lookahead,  "Lookahead",   true));
    layout.add (makeBool (pid::stereoLink, "Stereo Link", true));
    layout.add (makeBool (pid::delta,      "Delta",       false));
    layout.add (makeBool (pid::bypass,     "Bypass",      false));

    return layout;
}

} // namespace timaband
