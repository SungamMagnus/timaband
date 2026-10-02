#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace timaband
{

constexpr int numBands = 3;     // low, mid, high

extern const char* const bandNames[numBands];
extern const char* const curveNames[3];

namespace pid
{
extern const juce::String xover1;   // low/mid split, Hz
extern const juce::String xover2;   // mid/high split, Hz

// Indexed by band: 0 low, 1 mid, 2 high.
extern const juce::String attack[numBands];
extern const juce::String sustain[numBands];
extern const juce::String curve[numBands];
extern const juce::String mute[numBands];
extern const juce::String solo[numBands];
extern const juce::String bandBypass[numBands];   // band plays unshaped (unlike mute)

extern const juce::String inputGain;
extern const juce::String outputGain;
extern const juce::String lookahead;
extern const juce::String stereoLink;
extern const juce::String delta;
extern const juce::String bypass;
}

// UI-only properties on the state tree. They are saved with the preset but are
// not parameters, so the host cannot automate them.
namespace uiState
{
extern const juce::Identifier selectedBand;     // int 0..2, default 1
extern const juce::Identifier analyzerPost;     // bool, default true
extern const juce::Identifier linkBands;        // bool, default false
extern const juce::Identifier editorScale;      // double, window size relative to the 1120 x 640 design, 0.6..1.5

constexpr int  defaultSelectedBand = 1;
constexpr bool defaultAnalyzerPost = true;
constexpr bool defaultLinkBands    = false;
constexpr double defaultEditorScale = 1.0;
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

// Readouts shared by the host's parameter text and the editor.
juce::String hzReadout (float hz);
juce::String dbReadout (float db);

} // namespace timaband
