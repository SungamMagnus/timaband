#pragma once

#include <array>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../PluginProcessor.h"
#include "Controls.h"

namespace timaband::ui
{

// The 200 px column on the right: peak and RMS readouts, the two stereo level
// bars with their shared scale, the gain faders and readouts, and the Bypass and
// Delta buttons.
class MeterPanel final : public juce::Component
{
public:
    explicit MeterPanel (TimabandProcessor&);
    ~MeterPanel() override;

    // Collects the audio thread's levels and advances the ballistics by dtSeconds.
    void tick (float dtSeconds);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    static constexpr int numChannels = 4;      // in L, in R, out L, out R

    struct Bar
    {
        float level = -60.0f;       // dB, falls at a fixed rate
        float peak = -60.0f;        // dB, the hold line
        float holdLeft = 0.0f;      // seconds
    };

    struct Reading
    {
        float peakDb = -120.0f;     // held peak for the readout
        float peakHoldLeft = 0.0f;
        float meanSquare = 0.0f;    // smoothed
    };

    void stepBar (Bar&, float db, float dt) const noexcept;
    void paintBar (juce::Graphics&, juce::Rectangle<float>, const Bar&) const;
    void updateReadouts();

    TimabandProcessor& processor;

    std::array<Bar, numChannels> bars;
    std::array<Reading, numChannels> readings;
    std::array<juce::String, 2 * numChannels> readoutText;     // peak L, R, RMS L, R for in, then out
    float readoutTimer = 1.0f;

    FaderSlider inputFader, outputFader;
    GainBox inputGain, outputGain;
    StyledToggle bypassButton { StyledToggle::Style::bigLeft, "Bypass" };
    StyledToggle deltaButton { StyledToggle::Style::bigRight, "Delta" };

    std::unique_ptr<juce::SliderParameterAttachment> inputFaderAttachment, outputFaderAttachment, inputAttachment, outputAttachment;
    std::unique_ptr<juce::ButtonParameterAttachment> bypassAttachment, deltaAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeterPanel)
};

} // namespace timaband::ui
