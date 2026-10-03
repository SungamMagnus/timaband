#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace timaband::ui
{

// Draws the mockup's controls: pill toggles, the power/M/S circles, the big joined
// buttons, the curve buttons, the bipolar sliders, the gain faders and the
// draggable gain readouts.
class TimabandLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    TimabandLookAndFeel();

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

    int getSliderThumbRadius (juce::Slider&) override;
    juce::Slider::SliderLayout getSliderLayout (juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TimabandLookAndFeel)
};

} // namespace timaband::ui
