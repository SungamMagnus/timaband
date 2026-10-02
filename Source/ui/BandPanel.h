#pragma once

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../PluginProcessor.h"
#include "Controls.h"

namespace timaband::ui
{

// One band's column: header with the live range, the curve dropdown with its
// transfer graph, the Attack and Sustain sliders with readouts, and the gain meter.
class BandPanel final : public juce::Component
{
public:
    BandPanel (TimabandProcessor&, int bandIndex);
    ~BandPanel() override;

    int getBand() const noexcept { return band; }
    BipolarSlider& getAttackSlider() noexcept  { return attack; }
    BipolarSlider& getSustainSlider() noexcept { return sustain; }

    void setSelected (bool);
    void setAudible (bool);

    // A bypassed band still passes audio, just unshaped: its column goes grey and faint,
    // like the greyed-out band in Ozone, and stays fully usable.
    void setBypassed (bool);
    void setRange (float lowHz, float highHz);

    // Smooths the gain meter and moves the dot on the transfer graph.
    void tick (float dtSeconds);

    std::function<void (int)> onSelectBand;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void focusOfChildComponentChanged (FocusChangeType) override;

private:
    // The little transfer graph: the band's curve with a dot riding on it.
    class TransferGraph final : public juce::Component
    {
    public:
        void setCurve (int curveIndex);
        void setTransient (float value);

        void paint (juce::Graphics&) override;

    private:
        juce::Rectangle<float> getPlotArea() const;

        int curve = 1;
        float transient = 0.0f;
        juce::Colour colour;
        friend class BandPanel;
    };

    // Bipolar bar showing the gain the shaper is applying right now.
    class GainMeter final : public juce::Component
    {
    public:
        void setGainDb (float db);

        void paint (juce::Graphics&) override;

    private:
        float gainDb = 0.0f;
        juce::Colour colour, softColour;
        friend class BandPanel;
    };

    // The big number under a slider: "+4.5" with a small "dB".
    class Readout final : public juce::Component
    {
    public:
        void setValue (float db);

        void paint (juce::Graphics&) override;

    private:
        float value = 0.0f;
    };

    juce::Colour getAccent() const noexcept { return bypassed ? colours::muted : colours::band (band); }
    void updateAppearance();

    TimabandProcessor& processor;
    const int band;

    CurveBox curveBox;
    TransferGraph transfer;
    BipolarSlider attack, sustain;
    Readout attackReadout, sustainReadout;
    GainMeter gainMeter;

    juce::String rangeText;
    float shownGainDb = 0.0f, shownDot = 0.0f;
    bool selected = false, audible = true, bypassed = false;
    std::atomic<float>* bypassValue = nullptr;

    std::unique_ptr<juce::ComboBoxParameterAttachment> curveAttachment;
    std::unique_ptr<juce::SliderParameterAttachment> attackAttachment, sustainAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandPanel)
};

} // namespace timaband::ui
