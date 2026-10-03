#include "MeterPanel.h"

#include <cmath>

namespace timaband::ui
{

namespace
{
constexpr float barW = 8.0f;
constexpr std::array<float, 4> barX { 26.0f, 36.0f, 156.0f, 166.0f };
constexpr int metersTop = 104;

constexpr float barFallDbPerSecond = 40.0f;
constexpr float peakHoldSeconds = 0.9f, peakFallDbPerSecond = 14.0f;
constexpr float readoutPeakHoldSeconds = 1.5f, rmsSeconds = 0.3f;

struct ScaleMark { float db; const char* text; };

// Labels share one dB-to-y mapping with the bars.
float dbToY (float db) noexcept
{
    return layout::meterTop + (juce::jlimit (0.0f, layout::meterFloorDb, -db) / layout::meterFloorDb) * layout::meterH;
}

juce::Rectangle<float> barRect (size_t i) noexcept
{
    return { barX[i], (float) metersTop + layout::meterTop, barW, layout::meterH };
}

void drawTriangle (juce::Graphics& g, juce::Rectangle<float> box, bool pointsDown)
{
    juce::Path p;

    if (pointsDown)
        p.addTriangle (box.getX() + 0.5f, box.getY() + 1.0f, box.getRight() - 0.5f, box.getY() + 1.0f, box.getCentreX(), box.getBottom() - 1.0f);
    else
        p.addTriangle (box.getX() + 0.5f, box.getBottom() - 1.0f, box.getRight() - 0.5f, box.getBottom() - 1.0f, box.getCentreX(), box.getY() + 1.0f);

    g.fillPath (p);
}
} // namespace

MeterPanel::MeterPanel (TimabandProcessor& p)
    : processor (p)
{
    setOpaque (true);

    inputFader.setTitle ("Input gain");
    outputFader.setTitle ("Output gain");
    inputGain.setTitle ("Input gain value");
    outputGain.setTitle ("Output gain value");
    bypassButton.setTitle ("Bypass");
    deltaButton.setTitle ("Delta");

    // Each gain has two controls on the same parameter: the fader beside the bars and the number below.
    inputFaderAttachment  = std::make_unique<juce::SliderParameterAttachment> (*p.apvts.getParameter (pid::inputGain), inputFader);
    outputFaderAttachment = std::make_unique<juce::SliderParameterAttachment> (*p.apvts.getParameter (pid::outputGain), outputFader);
    inputAttachment  = std::make_unique<juce::SliderParameterAttachment> (*p.apvts.getParameter (pid::inputGain), inputGain);
    outputAttachment = std::make_unique<juce::SliderParameterAttachment> (*p.apvts.getParameter (pid::outputGain), outputGain);
    inputGain.useSignedText();
    outputGain.useSignedText();
    bypassAttachment = std::make_unique<juce::ButtonParameterAttachment> (*p.apvts.getParameter (pid::bypass), bypassButton);
    deltaAttachment  = std::make_unique<juce::ButtonParameterAttachment> (*p.apvts.getParameter (pid::delta), deltaButton);

    addAndMakeVisible (inputFader);
    addAndMakeVisible (outputFader);
    addAndMakeVisible (inputGain);
    addAndMakeVisible (outputGain);
    addAndMakeVisible (bypassButton);
    addAndMakeVisible (deltaButton);

    // Whatever the meters collected before the editor opened is old news.
    processor.getInputMeter().collect();
    processor.getOutputMeter().collect();

    readoutText.fill (theme::formatMeter (-120.0f));
}

MeterPanel::~MeterPanel() = default;

void MeterPanel::resized()
{
    // The faders travel the height of the bars, their centres from +24 dB at the top to -24 dB at the bottom.
    const auto faderTop = metersTop + (int) layout::meterTop - FaderSlider::handleH / 2;
    const auto faderH = (int) layout::meterH + FaderSlider::handleH;
    inputFader.setBounds (7, faderTop, 18, faderH);
    outputFader.setBounds (175, faderTop, 18, faderH);

    inputGain.setBounds (1, 457, 52, 24);
    outputGain.setBounds (147, 457, 52, 24);

    // Two joined buttons, 1 px apart, 20 px above the bottom edge.
    const auto y = getHeight() - 20 - 44;
    bypassButton.setBounds (12, y, 88, 44);
    deltaButton.setBounds (101, y, 87, 44);
}

void MeterPanel::stepBar (Bar& bar, float db, float dt) const noexcept
{
    db = juce::jmax (db, -layout::meterFloorDb);
    bar.level = juce::jmax (db, bar.level - barFallDbPerSecond * dt);

    if (db >= bar.peak)
    {
        bar.peak = db;
        bar.holdLeft = peakHoldSeconds;
    }
    else if ((bar.holdLeft -= dt) <= 0.0f)
    {
        bar.peak = juce::jmax (db, bar.peak - peakFallDbPerSecond * dt);
    }
}

void MeterPanel::tick (float dt)
{
    const std::array<LevelMeter::Reading, 2> collected { processor.getInputMeter().collect(),
                                                         processor.getOutputMeter().collect() };

    const auto rmsCoeff = 1.0f - std::exp (-dt / rmsSeconds);

    for (size_t side = 0; side < 2; ++side)
    {
        for (size_t c = 0; c < (size_t) LevelMeter::numChannels; ++c)
        {
            const auto i = side * (size_t) LevelMeter::numChannels + c;
            const auto peakDb = juce::Decibels::gainToDecibels (collected[side].peak[c], -120.0f);

            stepBar (bars[i], peakDb, dt);

            auto& r = readings[i];

            if (peakDb >= r.peakDb)
            {
                r.peakDb = peakDb;
                r.peakHoldLeft = readoutPeakHoldSeconds;
            }
            else if ((r.peakHoldLeft -= dt) <= 0.0f)
            {
                r.peakDb = peakDb;
                r.peakHoldLeft = readoutPeakHoldSeconds;
            }

            r.meanSquare += (collected[side].meanSquare[c] - r.meanSquare) * rmsCoeff;
        }
    }

    repaint (juce::Rectangle<int> (26, metersTop + (int) layout::meterTop, 18, (int) layout::meterH));
    repaint (juce::Rectangle<int> (156, metersTop + (int) layout::meterTop, 18, (int) layout::meterH));

    readoutTimer += dt;

    if (readoutTimer >= 0.1f)
    {
        readoutTimer = 0.0f;
        updateReadouts();
    }
}

void MeterPanel::updateReadouts()
{
    std::array<juce::String, 2 * numChannels> next;

    // Layout: [in peak L, in peak R, out peak L, out peak R, in RMS L, in RMS R, out RMS L, out RMS R]
    for (size_t i = 0; i < (size_t) numChannels; ++i)
    {
        next[i] = theme::formatMeter (readings[i].peakDb);
        next[i + (size_t) numChannels] = theme::formatMeter (10.0f * std::log10 (juce::jmax (readings[i].meanSquare, 1.0e-12f)));
    }

    if (next == readoutText)
        return;

    readoutText = next;
    repaint (0, 52, getWidth(), 48);
}

void MeterPanel::paintBar (juce::Graphics& g, juce::Rectangle<float> r, const Bar& bar) const
{
    juce::Path shape;
    shape.addRoundedRectangle (r, 2.0f);

    g.setColour (colours::meterBg);
    g.fillPath (shape);

    g.saveState();
    g.reduceClipRegion (shape);

    const auto level = juce::jlimit (0.0f, 1.0f, (bar.level + layout::meterFloorDb) / layout::meterFloorDb);
    const auto fillTop = r.getY() + (1.0f - level) * r.getHeight();

    g.setGradientFill (juce::ColourGradient (colours::meterHi, r.getX(), r.getY(),
                                             colours::meterLo, r.getX(), r.getBottom(), false));
    g.fillRect (r.getX(), fillTop, r.getWidth(), r.getBottom() - fillTop);

    const auto peak = juce::jlimit (0.0f, 1.0f, (bar.peak + layout::meterFloorDb) / layout::meterFloorDb);
    g.setColour (colours::peakLine);
    g.fillRect (r.getX(), r.getY() + (1.0f - peak) * (r.getHeight() - 1.0f), r.getWidth(), 1.0f);

    g.restoreState();
}

void MeterPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    g.setColour (colours::line);
    g.fillRect (0, 0, 1, getHeight());

    // Header: IN, the I/O badge, OUT.
    g.fillRect (0, layout::toolbarH - 1, getWidth(), 1);

    const auto headFont = theme::font (12.0f, false, 1.44f);
    const auto inW = std::ceil (juce::GlyphArrangement::getStringWidth (headFont, "IN"));
    const auto outW = std::ceil (juce::GlyphArrangement::getStringWidth (headFont, "OUT"));
    const auto headY = 21.5f;

    g.setColour (colours::muted);
    g.setFont (headFont);
    g.drawText ("IN", juce::Rectangle<float> (12.0f, headY - 8.0f, inW, 16.0f), juce::Justification::centredLeft, false);
    drawTriangle (g, juce::Rectangle<float> (12.0f + inW + 6.0f, headY - 4.0f, 9.0f, 8.0f), true);

    drawTriangle (g, juce::Rectangle<float> ((float) getWidth() - 12.0f - 9.0f, headY - 4.0f, 9.0f, 8.0f), false);
    g.drawText ("OUT", juce::Rectangle<float> ((float) getWidth() - 12.0f - 9.0f - 6.0f - outW, headY - 8.0f, outW, 16.0f),
                juce::Justification::centredLeft, false);

    const auto badge = juce::Rectangle<float> (28.0f, 28.0f).withCentre ({ (float) getWidth() * 0.5f, headY });
    g.setColour (colours::outline);
    g.drawEllipse (badge.reduced (0.5f), 1.0f);
    g.setColour (colours::muted);
    g.setFont (theme::font (9.0f, true, 0.36f));
    g.drawText ("I/O", badge, juce::Justification::centred, false);

    // Readouts: left pair is IN, right pair is OUT.
    constexpr float columnW = 68.0f;        // each side of the centre label
    const auto labelFont = theme::font (12.0f);
    const auto rowY = [] (int row) { return 56.0f + (float) row * 24.0f; };

    // Shrink the numbers when a pair would run into the centre label (a wider typeface
    // than the mockup's), keeping one size for all of them.
    auto valueSize = 13.0f;
    auto gap = 10.0f;

    for (int row = 0; row < 2; ++row)
    {
        for (size_t side = 0; side < 2; ++side)
        {
            const auto base = (size_t) row * (size_t) numChannels + side * 2;
            const auto wide = std::ceil (juce::GlyphArrangement::getStringWidth (theme::font (13.0f), readoutText[base]))
                              + std::ceil (juce::GlyphArrangement::getStringWidth (theme::font (13.0f), readoutText[base + 1]));

            if (wide + gap > columnW)
            {
                gap = 8.0f;
                valueSize = juce::jmin (valueSize, 13.0f * (columnW - gap) / wide);
            }
        }
    }

    const auto valueFont = theme::font (juce::jmax (valueSize, 10.0f));
    const auto width = [&valueFont] (const juce::String& s) { return std::ceil (juce::GlyphArrangement::getStringWidth (valueFont, s)); };

    for (int row = 0; row < 2; ++row)
    {
        const auto base = (size_t) row * (size_t) numChannels;
        const auto& inL = readoutText[base + 0];
        const auto& inR = readoutText[base + 1];
        const auto& outL = readoutText[base + 2];
        const auto& outR = readoutText[base + 3];
        const auto y = rowY (row);

        g.setColour (colours::text);
        g.setFont (valueFont);
        g.drawText (inL, juce::Rectangle<float> (12.0f, y, columnW, 16.0f), juce::Justification::centredLeft, false);
        g.drawText (inR, juce::Rectangle<float> (12.0f + width (inL) + gap, y, columnW, 16.0f), juce::Justification::centredLeft, false);

        const auto right = (float) getWidth() - 12.0f;
        g.drawText (outR, juce::Rectangle<float> (right - columnW, y, columnW, 16.0f), juce::Justification::centredRight, false);
        g.drawText (outL, juce::Rectangle<float> (right - width (outR) - gap - columnW, y, columnW, 16.0f), juce::Justification::centredRight, false);

        g.setColour (colours::muted);
        g.setFont (labelFont);
        g.drawText (row == 0 ? "Peak" : "RMS", juce::Rectangle<float> (80.0f, y, 40.0f, 16.0f), juce::Justification::centred, false);
    }

    // Bars, then the scale between them.
    for (size_t i = 0; i < (size_t) numChannels; ++i)
        paintBar (g, barRect (i), bars[i]);

    static const ScaleMark marks[] = { { 0.0f, "0" }, { -3.0f, "3" }, { -6.0f, "6" }, { -10.0f, "10" }, { -15.0f, "15" },
                                       { -20.0f, "20" }, { -30.0f, "30" }, { -40.0f, "40" }, { -50.0f, "50" }, { -60.0f, "Inf" } };

    g.setFont (labelFont);

    for (const auto& mark : marks)
    {
        const auto y = (float) metersTop + dbToY (mark.db);
        const auto text = mark.db < 0.0f ? theme::minusSign() + mark.text : juce::String (mark.text);

        g.setColour (colours::muted);
        g.drawText (text, juce::Rectangle<float> (44.0f, y - 7.0f, 112.0f, 14.0f), juce::Justification::centred, false);

        g.setColour (colours::dim);
        g.fillRect (47.0f, y - 0.5f, 4.0f, 1.0f);
        g.fillRect (149.0f, y - 0.5f, 4.0f, 1.0f);
    }
}

} // namespace timaband::ui
