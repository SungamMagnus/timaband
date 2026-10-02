#pragma once

#include <array>
#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../PluginProcessor.h"
#include "Controls.h"
#include "Spectrum.h"

namespace timaband::ui
{

// The strip of crossover handles above the spectrum, the three band regions
// under them, and each region's power, M and S buttons.
class CrossoverDisplay final : public juce::Component
{
public:
    explicit CrossoverDisplay (TimabandProcessor&);
    ~CrossoverDisplay() override;

    // Advances the analyzer by dtSeconds and repaints the graph if it moved.
    void tick (float dtSeconds);

    void setSelectedBand (int band);
    void setAnalyzerPost (bool post);
    void setAudible (const std::array<bool, numBands>&);

    // Crossover frequencies as last reported by the parameters.
    float getCrossoverHz (int index) const noexcept { return crossoverHz[(size_t) index]; }

    std::function<void (int)> onSelectBand;
    std::function<void()> onCrossoversChanged;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;

    static float freqToX (float hz) noexcept;
    static float xToFreq (float x) noexcept;

private:
    class Handle;
    friend class Handle;

    float constrainCrossover (int index, float hz) const noexcept;
    void crossoversMoved();
    std::array<float, numBands + 1> regionEdges() const noexcept;
    int regionAt (float x) const noexcept;
    void updateHover();
    juce::Rectangle<int> graphArea() const { return { 0, layout::stripH, layout::graphW, layout::graphH }; }

    void paintStrip (juce::Graphics&) const;
    void paintRegions (juce::Graphics&) const;
    void paintSpectrum (juce::Graphics&) const;
    void paintTip (juce::Graphics&, const Handle&) const;

    TimabandProcessor& processor;
    SpectrumAnalyzer analyzer;

    std::array<float, 2> crossoverHz { 180.0f, 2400.0f };
    std::array<bool, numBands> audible { true, true, true };
    int selectedBand = uiState::defaultSelectedBand;
    int hoverRegion = -1;
    bool analyzerPost = uiState::defaultAnalyzerPost;
    bool ready = false;

    std::array<std::unique_ptr<Handle>, 2> handles;
    std::array<std::unique_ptr<StyledToggle>, numBands> bypassButtons, muteButtons, soloButtons;
    std::array<std::unique_ptr<juce::ButtonParameterAttachment>, numBands> bypassAttachments, muteAttachments, soloAttachments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrossoverDisplay)
};

} // namespace timaband::ui
