#pragma once

#include <array>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/BandPanel.h"
#include "ui/Controls.h"
#include "ui/CrossoverDisplay.h"
#include "ui/FocusRing.h"
#include "ui/LinkController.h"
#include "ui/LookAndFeel.h"
#include "ui/MeterPanel.h"
#include "ui/StateProperty.h"

/**
 * The editor lays everything out in a fixed 1120 x 640 design space and scales
 * that with one transform, so the layout constants in ui/Theme.h are the only
 * source of truth. Everything live (spectrum, meters, gain bars) is driven by a
 * single 30 Hz timer through tick().
 */
class TimabandEditor final : public juce::AudioProcessorEditor,
                             private juce::Timer
{
public:
    explicit TimabandEditor (TimabandProcessor&);
    ~TimabandEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // One frame of every live element. The timer calls the first form; tools that
    // run faster or slower than real time pass the time that has elapsed.
    void tick();
    void tick (float dtSeconds);

private:
    // Header, outer outline.
    class Content final : public juce::Component
    {
    public:
        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
    };

    // Toolbar and link row dividers.
    class LeftArea final : public juce::Component
    {
    public:
        void paint (juce::Graphics&) override;
    };

    void timerCallback() override { tick(); }

    void layoutContent();
    void selectBand (int band);
    void applySelection();
    void applyAnalyzerMode();
    void applyLinkBands();
    void updateAudibility();
    void updateRanges();
    void resetShaping();

    TimabandProcessor& proc;
    timaband::ui::TimabandLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this };

    Content content;
    LeftArea leftArea;
    timaband::ui::StyledToggle lookaheadButton, stereoLinkButton, linkBandsButton, resetButton;
    juce::Label analyzerLabel;
    timaband::ui::SegmentedControl analyzerControl;
    timaband::ui::CrossoverDisplay display;
    std::array<std::unique_ptr<timaband::ui::BandPanel>, timaband::numBands> bands;
    timaband::ui::MeterPanel meters;
    timaband::ui::FocusRing focusRing;

    std::unique_ptr<juce::ButtonParameterAttachment> lookaheadAttachment, stereoLinkAttachment;
    std::unique_ptr<juce::ParameterAttachment> bypassWatcher;
    std::array<std::unique_ptr<juce::ParameterAttachment>, timaband::numBands> muteWatchers, soloWatchers, bandBypassWatchers;
    std::array<bool, timaband::numBands> muted {}, soloed {};

    std::unique_ptr<timaband::ui::LinkController> linkController;
    std::unique_ptr<timaband::ui::StateProperty> selectedBandProperty, analyzerProperty, linkBandsProperty;

    double lastTickMs = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TimabandEditor)
};
