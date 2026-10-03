#include "PluginEditor.h"

using namespace timaband;
using namespace timaband::ui;

namespace
{
constexpr int toolbarPillY = 8;     // pills sit 26 px high inside the 43 px toolbar
constexpr int linkRowY = layout::toolbarH + layout::displayH;
constexpr int bandsY = linkRowY + layout::linkRowH + 1;     // below the divider

// The header logo: three band-coloured waveform strokes on a graph-coloured tile.
void drawLogo (juce::Graphics& g)
{
    g.setColour (colours::graph);
    g.fillRect (0, 0, 55, 55);
    g.setColour (colours::line);
    g.fillRect (55, 0, 1, 56);

    const auto stroke = [&g] (juce::Colour colour, std::initializer_list<std::pair<float, float>> points)
    {
        juce::Path p;
        auto first = true;

        for (auto [x, y] : points)
        {
            if (first) p.startNewSubPath (x, y); else p.lineTo (x, y);
            first = false;
        }

        g.setColour (colour);
        g.strokePath (p, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };

    stroke (colours::high, { { 12, 17 }, { 24, 17 }, { 27, 11 }, { 30, 22 }, { 33, 17 }, { 44, 17 } });
    stroke (colours::mid,  { { 12, 28 }, { 21, 28 }, { 25, 20 }, { 29, 36 }, { 33, 28 }, { 44, 28 } });
    stroke (colours::low,  { { 12, 39 }, { 18, 39 }, { 21, 33 }, { 24, 44 }, { 27, 39 }, { 44, 39 } });
}
} // namespace

/* ── Content and left area ───────────────────────────────────────────────── */

void TimabandEditor::Content::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);

    g.setColour (colours::chrome);
    g.fillRect (0, 0, layout::designW, layout::headerH);
    g.setColour (colours::line);
    g.fillRect (0, layout::headerH - 1, layout::designW, 1);

    drawLogo (g);

    // Name and tagline, 18 px after the logo; 26 + 4 + 9 px tall, centred in the 55 px header.
    const auto nameFont = theme::font (26.0f, false, 1.04f);
    const auto subFont = theme::font (9.0f, false, 2.7f);

    g.setColour (colours::text);
    g.setFont (nameFont);
    g.drawText (theme::appName(), juce::Rectangle<float> (74.0f, 8.0f, 300.0f, 26.0f), juce::Justification::centredLeft, false);

    g.setColour (colours::muted);
    g.setFont (subFont);
    g.drawText ("MULTIBAND TRANSIENT DESIGNER", juce::Rectangle<float> (74.0f, 38.0f, 300.0f, 9.0f), juce::Justification::centredLeft, false);
}

void TimabandEditor::Content::paintOverChildren (juce::Graphics& g)
{
    g.setColour (colours::line);
    g.drawRect (getLocalBounds(), 1);
}

void TimabandEditor::LeftArea::paint (juce::Graphics& g)
{
    g.setColour (colours::line);
    g.fillRect (0, layout::toolbarH - 1, layout::leftW, 1);                 // under the toolbar
    g.fillRect (40, linkRowY + layout::linkRowH, layout::leftW - 80, 1);    // under the link row
}

/* ── Construction ────────────────────────────────────────────────────────── */

TimabandEditor::TimabandEditor (TimabandProcessor& p)
    : AudioProcessorEditor (&p),
      proc (p),
      lookaheadButton (StyledToggle::Style::pill, "Lookahead"),
      stereoLinkButton (StyledToggle::Style::pill, "Stereo link"),
      linkBandsButton (StyledToggle::Style::pill, "Link bands"),
      resetButton (StyledToggle::Style::textLink, "Reset"),
      analyzerControl (juce::StringArray { "Pre", "Post" }),
      display (p),
      meters (p)
{
    setTitle (theme::appName());
    setOpaque (true);

    content.setLookAndFeel (&lookAndFeel);
    content.setOpaque (true);
    tooltipWindow.setLookAndFeel (&lookAndFeel);        // a sibling of content, not inside it
    addAndMakeVisible (content);

    leftArea.setTitle ("Controls");
    content.addAndMakeVisible (leftArea);
    content.addAndMakeVisible (meters);

    // Toolbar.
    leftArea.addAndMakeVisible (lookaheadButton);
    leftArea.addAndMakeVisible (analyzerLabel);
    leftArea.addAndMakeVisible (analyzerControl);
    leftArea.addAndMakeVisible (resetButton);

    analyzerLabel.setText ("Analyzer:", juce::dontSendNotification);
    analyzerLabel.setFont (theme::font (12.0f));
    analyzerLabel.setColour (juce::Label::textColourId, colours::muted);
    analyzerLabel.setJustificationType (juce::Justification::centredRight);
    analyzerLabel.setInterceptsMouseClicks (false, false);
    analyzerControl.setTitle ("Analyzer source");

    resetButton.setTitle ("Reset attack and sustain");
    resetButton.setDescription ("Set all Attack and Sustain values to 0");
    resetButton.onClick = [this] { resetShaping(); };

    lookaheadAttachment = std::make_unique<juce::ButtonParameterAttachment> (*p.apvts.getParameter (pid::lookahead), lookaheadButton);

    // Crossover display and link row.
    leftArea.addAndMakeVisible (display);
    leftArea.addAndMakeVisible (stereoLinkButton);
    leftArea.addAndMakeVisible (linkBandsButton);

    stereoLinkAttachment = std::make_unique<juce::ButtonParameterAttachment> (*p.apvts.getParameter (pid::stereoLink), stereoLinkButton);

    // Band columns.
    for (int b = 0; b < numBands; ++b)
    {
        bands[(size_t) b] = std::make_unique<BandPanel> (p, b);
        bands[(size_t) b]->onSelectBand = [this] (int band) { selectBand (band); };
        leftArea.addAndMakeVisible (*bands[(size_t) b]);
    }

    display.onSelectBand = [this] (int band) { selectBand (band); };
    display.onCrossoversChanged = [this] { updateRanges(); };

    LinkController::Sliders sliders {};

    for (size_t b = 0; b < (size_t) numBands; ++b)
    {
        sliders[b][0] = &bands[b]->getAttackSlider();
        sliders[b][1] = &bands[b]->getSustainSlider();
    }

    linkController = std::make_unique<LinkController> (p.apvts, sliders, [this]
    {
        return (bool) proc.apvts.state.getProperty (uiState::linkBands, uiState::defaultLinkBands);
    });

    // Host-driven state that changes how the editor looks.
    bypassWatcher = std::make_unique<juce::ParameterAttachment> (*p.apvts.getParameter (pid::bypass), [this] (float v)
    {
        leftArea.setAlpha (v > 0.5f ? 0.7f : 1.0f);
    });

    for (size_t b = 0; b < (size_t) numBands; ++b)
    {
        muteWatchers[b] = std::make_unique<juce::ParameterAttachment> (*p.apvts.getParameter (pid::mute[b]), [this, b] (float v)
        {
            muted[b] = v > 0.5f;
            updateAudibility();
        });
        soloWatchers[b] = std::make_unique<juce::ParameterAttachment> (*p.apvts.getParameter (pid::solo[b]), [this, b] (float v)
        {
            soloed[b] = v > 0.5f;
            updateAudibility();
        });
        bandBypassWatchers[b] = std::make_unique<juce::ParameterAttachment> (*p.apvts.getParameter (pid::bandBypass[b]), [this, b] (float v)
        {
            bands[b]->setBypassed (v > 0.5f);
        });
    }

    // UI-only state.
    analyzerControl.onChange = [this] (int index)
    {
        analyzerProperty->set (index == 1);
    };

    linkBandsButton.onClick = [this] { linkBandsProperty->set (linkBandsButton.getToggleState()); };

    selectedBandProperty = std::make_unique<StateProperty> (p.apvts.state, uiState::selectedBand, [this] { applySelection(); });
    analyzerProperty     = std::make_unique<StateProperty> (p.apvts.state, uiState::analyzerPost, [this] { applyAnalyzerMode(); });
    linkBandsProperty    = std::make_unique<StateProperty> (p.apvts.state, uiState::linkBands, [this] { applyLinkBands(); });

    content.addAndMakeVisible (focusRing);

    layoutContent();

    bypassWatcher->sendInitialUpdate();

    for (size_t b = 0; b < (size_t) numBands; ++b)
    {
        muteWatchers[b]->sendInitialUpdate();
        soloWatchers[b]->sendInitialUpdate();
        bandBypassWatchers[b]->sendInitialUpdate();
    }

    applySelection();
    applyAnalyzerMode();
    applyLinkBands();
    updateRanges();

    // Resizable at a fixed aspect, 0.6x to 1.5x of the design size; the last size is remembered.
    // Read it first: setResizeLimits() constrains the (still empty) editor to the minimum size,
    // and resized() records whatever size it ends up with.
    const auto scale = juce::jlimit (layout::minScale, layout::maxScale,
                                     (double) p.apvts.state.getProperty (uiState::editorScale, uiState::defaultEditorScale));

    setResizable (true, true);
    setResizeLimits ((int) std::lround (layout::designW * layout::minScale), (int) std::lround (layout::designH * layout::minScale),
                     (int) std::lround (layout::designW * layout::maxScale), (int) std::lround (layout::designH * layout::maxScale));
    getConstrainer()->setFixedAspectRatio ((double) layout::designW / (double) layout::designH);
    setSize ((int) std::lround (layout::designW * scale), (int) std::lround (layout::designH * scale));

    lastTickMs = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (30);
}

TimabandEditor::~TimabandEditor()
{
    stopTimer();
    tooltipWindow.setLookAndFeel (nullptr);
    content.setLookAndFeel (nullptr);
}

void TimabandEditor::layoutContent()
{
    content.setBounds (0, 0, layout::designW, layout::designH);
    leftArea.setBounds (0, layout::headerH, layout::leftW, layout::designH - layout::headerH);
    meters.setBounds (layout::leftW, layout::headerH, layout::rightW, layout::designH - layout::headerH);
    focusRing.setBounds (content.getLocalBounds());

    // Toolbar: Lookahead on the left, the analyzer switch centred, Reset on the right.
    lookaheadButton.setBounds (16, toolbarPillY, lookaheadButton.getIdealWidth(), 26);

    const auto labelW = 60, gap = 10;
    const auto groupW = labelW + gap + analyzerControl.getIdealWidth();
    const auto groupX = layout::leftW / 2 - groupW / 2;
    analyzerLabel.setBounds (groupX, toolbarPillY, labelW, 26);
    analyzerControl.setBounds (groupX + labelW + gap, toolbarPillY, analyzerControl.getIdealWidth(), 26);

    const auto resetW = resetButton.getIdealWidth();
    resetButton.setBounds (layout::leftW - 16 - resetW, toolbarPillY, resetW, 26);

    display.setBounds (0, layout::toolbarH, layout::graphW, layout::displayH);

    const auto linkY = linkRowY + (layout::linkRowH - 26) / 2;
    stereoLinkButton.setBounds (16, linkY, stereoLinkButton.getIdealWidth(), 26);
    linkBandsButton.setBounds (layout::leftW - 16 - linkBandsButton.getIdealWidth(), linkY, linkBandsButton.getIdealWidth(), 26);

    // Three equal columns.
    for (int b = 0; b < numBands; ++b)
    {
        const auto x0 = layout::leftW * b / numBands, x1 = layout::leftW * (b + 1) / numBands;
        bands[(size_t) b]->setBounds (x0, bandsY, x1 - x0, layout::designH - layout::headerH - bandsY);
    }
}

/* ── State ───────────────────────────────────────────────────────────────── */

void TimabandEditor::selectBand (int band)
{
    selectedBandProperty->set (juce::jlimit (0, numBands - 1, band));
}

void TimabandEditor::applySelection()
{
    const auto selected = juce::jlimit (0, numBands - 1, (int) selectedBandProperty->get());

    display.setSelectedBand (selected);

    for (int b = 0; b < numBands; ++b)
        bands[(size_t) b]->setSelected (b == selected);
}

void TimabandEditor::applyAnalyzerMode()
{
    const auto post = (bool) analyzerProperty->get();

    analyzerControl.setSelected (post ? 1 : 0);
    display.setAnalyzerPost (post);
}

void TimabandEditor::applyLinkBands()
{
    linkBandsButton.setToggleState ((bool) linkBandsProperty->get(), juce::dontSendNotification);
}

void TimabandEditor::updateAudibility()
{
    // A band is audible if anything is soloed ? it is soloed : it is not muted.
    const auto anySolo = soloed[0] || soloed[1] || soloed[2];
    std::array<bool, numBands> audible {};

    for (size_t b = 0; b < (size_t) numBands; ++b)
    {
        audible[b] = anySolo ? soloed[b] : ! muted[b];
        bands[b]->setAudible (audible[b]);
    }

    display.setAudible (audible);
}

void TimabandEditor::updateRanges()
{
    const auto f1 = display.getCrossoverHz (0), f2 = display.getCrossoverHz (1);

    bands[0]->setRange (20.0f, f1);
    bands[1]->setRange (f1, f2);
    bands[2]->setRange (f2, 20000.0f);
}

void TimabandEditor::resetShaping()
{
    for (int b = 0; b < numBands; ++b)
    {
        for (const auto& id : { pid::attack[b], pid::sustain[b] })
        {
            auto* param = proc.apvts.getParameter (id);
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 (0.0f));
            param->endChangeGesture();
        }
    }
}

/* ── Frame ───────────────────────────────────────────────────────────────── */

void TimabandEditor::tick()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto dt = juce::jlimit (0.001, 0.1, (now - lastTickMs) * 0.001);
    lastTickMs = now;

    tick ((float) dt);
}

void TimabandEditor::tick (float dtSeconds)
{
    display.tick (dtSeconds);

    for (auto& band : bands)
        band->tick (dtSeconds);

    meters.tick (dtSeconds);
}

void TimabandEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
}

void TimabandEditor::resized()
{
    const auto scale = juce::jmin ((double) getWidth() / (double) layout::designW,
                                   (double) getHeight() / (double) layout::designH);

    if (scale <= 0.0)
        return;

    content.setTransform (juce::AffineTransform::scale ((float) scale));

    auto& state = proc.apvts.state;

    if (std::abs ((double) state.getProperty (uiState::editorScale, 0.0) - scale) > 1.0e-3)
        state.setProperty (uiState::editorScale, scale, nullptr);
}
