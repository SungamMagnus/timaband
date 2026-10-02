#include "BandPanel.h"

#include <cmath>

namespace timaband::ui
{

namespace
{
constexpr float headerTop = 10.0f;
constexpr int rowTop = 32;                  // dropdown and transfer graph
constexpr int slidersTop = 92;
constexpr int sliderH = layout::trackLen + layout::thumbSize;

// Baseline that centres a font's ascent-to-descent box on a line, like drawText does.
float baselineIn (const juce::Font& font, juce::Rectangle<float> r)
{
    return r.getCentreY() + (font.getAscent() - font.getDescent()) * 0.5f;
}
} // namespace

/* ── TransferGraph ───────────────────────────────────────────────────────── */

void BandPanel::TransferGraph::setCurve (int curveIndex)
{
    if (curveIndex == curve)
        return;

    curve = curveIndex;
    repaint();
}

void BandPanel::TransferGraph::setTransient (float value)
{
    value = juce::jlimit (0.0f, 1.0f, value);

    if (std::abs (value - transient) < 0.002f)
        return;

    transient = value;
    repaint();
}

juce::Rectangle<float> BandPanel::TransferGraph::getPlotArea() const
{
    // The component is padded so the dot's halo can spill past the border, as in the mockup.
    return getLocalBounds().toFloat().reduced (6.0f);
}

void BandPanel::TransferGraph::paint (juce::Graphics& g)
{
    const auto box = getPlotArea();
    const auto inner = box.reduced (1.0f);

    g.setColour (colours::graph);
    g.fillRect (box);

    // Grid in thirds: a hairline at the top and left edge of each third.
    g.setColour (colours::white.withAlpha (0.045f));

    for (int k = 0; k < 3; ++k)
    {
        g.fillRect (inner.getX(), std::round (inner.getY() + inner.getHeight() * (float) k / 3.0f), inner.getWidth(), 1.0f);
        g.fillRect (std::round (inner.getX() + inner.getWidth() * (float) k / 3.0f), inner.getY(), 1.0f, inner.getHeight());
    }

    g.setColour (colours::line);
    g.drawRect (box, 1.0f);

    g.setColour (colour);
    g.strokePath (theme::curvePath (curve, inner, 28),
                  juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const juce::Point<float> dot (inner.getX() + transient * inner.getWidth(),
                                  inner.getBottom() - theme::curve (curve, transient) * inner.getHeight());

    g.setColour (colour.withAlpha (0.28f));
    g.fillEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre (dot));
    g.setColour (colour);
    g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (dot));
}

/* ── GainMeter ───────────────────────────────────────────────────────────── */

void BandPanel::GainMeter::setGainDb (float db)
{
    db = juce::jlimit (-layout::dbRange, layout::dbRange, db);

    if (std::abs (db - gainDb) < 0.005f)
        return;

    gainDb = db;
    repaint();
}

void BandPanel::GainMeter::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto track = bounds.withSizeKeepingCentre (6.0f, bounds.getHeight());
    const auto centre = track.getCentreY();
    const auto half = track.getHeight() * 0.5f;

    juce::Path trackPath;
    trackPath.addRoundedRectangle (track, 3.0f);

    g.setColour (colours::ctrlHi);
    g.fillPath (trackPath);

    g.saveState();
    g.reduceClipRegion (trackPath);

    if (gainDb > 0.0f)
    {
        g.setColour (colour);
        g.fillRect (track.getX(), centre - half * gainDb / layout::dbRange, track.getWidth(), half * gainDb / layout::dbRange);
    }
    else if (gainDb < 0.0f)
    {
        g.setColour (softColour);
        g.fillRect (track.getX(), centre, track.getWidth(), half * -gainDb / layout::dbRange);
    }

    g.restoreState();

    g.setColour (colours::muted);
    g.fillRect (bounds.getX(), centre - 0.5f, bounds.getWidth(), 1.0f);
}

/* ── Readout ─────────────────────────────────────────────────────────────── */

void BandPanel::Readout::setValue (float db)
{
    if (juce::exactlyEqual (db, value))
        return;

    value = db;
    repaint();
}

void BandPanel::Readout::paint (juce::Graphics& g)
{
    const auto big = theme::font (30.0f), small = theme::font (13.0f);
    const auto number = theme::formatDb (value);
    const auto numberW = std::ceil (juce::GlyphArrangement::getStringWidth (big, number));
    const auto unitW = std::ceil (juce::GlyphArrangement::getStringWidth (small, "dB"));
    const auto left = ((float) getWidth() - (numberW + 3.0f + unitW)) * 0.5f;
    const auto baseline = baselineIn (big, getLocalBounds().toFloat());

    g.setColour (colours::text);
    g.setFont (big);
    g.drawSingleLineText (number, (int) std::lround (left), (int) std::lround (baseline));

    g.setColour (colours::muted);
    g.setFont (small);
    g.drawSingleLineText ("dB", (int) std::lround (left + numberW + 3.0f), (int) std::lround (baseline));
}

/* ── BandPanel ───────────────────────────────────────────────────────────── */

BandPanel::BandPanel (TimabandProcessor& p, int bandIndex)
    : processor (p), band (bandIndex), curveBox (bandIndex), attack (bandIndex), sustain (bandIndex)
{
    const juce::String name (bandNames[bandIndex]);

    bypassValue = p.apvts.getRawParameterValue (pid::bypass);
    transfer.setInterceptsMouseClicks (false, false);
    gainMeter.setInterceptsMouseClicks (false, false);
    attackReadout.setInterceptsMouseClicks (false, false);
    sustainReadout.setInterceptsMouseClicks (false, false);

    setTitle (name + " band");
    curveBox.setTitle (name + " band detector curve");
    attack.setTitle (name + " band attack");
    sustain.setTitle (name + " band sustain");

    auto* curveParam = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter (pid::curve[bandIndex]));
    jassert (curveParam != nullptr);
    curveBox.addItemList (curveParam->choices, 1);

    attack.onValueChange  = [this] { attackReadout.setValue ((float) attack.getValue()); };
    sustain.onValueChange = [this] { sustainReadout.setValue ((float) sustain.getValue()); };
    curveBox.onChange     = [this] { transfer.setCurve (curveBox.getSelectedItemIndex()); };

    curveAttachment   = std::make_unique<juce::ComboBoxParameterAttachment> (*curveParam, curveBox);
    attackAttachment  = std::make_unique<juce::SliderParameterAttachment> (*p.apvts.getParameter (pid::attack[bandIndex]), attack);
    sustainAttachment = std::make_unique<juce::SliderParameterAttachment> (*p.apvts.getParameter (pid::sustain[bandIndex]), sustain);

    updateAppearance();
    transfer.setCurve (curveBox.getSelectedItemIndex());
    attackReadout.setValue ((float) attack.getValue());
    sustainReadout.setValue ((float) sustain.getValue());

    addAndMakeVisible (curveBox);
    addAndMakeVisible (transfer);
    addAndMakeVisible (attack);
    addAndMakeVisible (sustain);
    addAndMakeVisible (attackReadout);
    addAndMakeVisible (sustainReadout);
    addAndMakeVisible (gainMeter);

    // A press anywhere in the column, on a child or not, selects the band.
    addMouseListener (this, true);
}

BandPanel::~BandPanel() = default;

void BandPanel::setSelected (bool shouldBeSelected)
{
    if (shouldBeSelected == selected)
        return;

    selected = shouldBeSelected;
    repaint();
}

void BandPanel::setAudible (bool isAudible)
{
    audible = isAudible;
    updateAppearance();
}

void BandPanel::setBypassed (bool isBypassed)
{
    bypassed = isBypassed;
    updateAppearance();
}

void BandPanel::updateAppearance()
{
    // Mute/solo dimming and bypass greying stack.
    setAlpha ((audible ? 1.0f : 0.35f) * (bypassed ? 0.45f : 1.0f));

    const auto accent = getAccent();
    attack.setColour (juce::Slider::trackColourId, accent);
    sustain.setColour (juce::Slider::trackColourId, accent);
    curveBox.setColour (juce::ComboBox::textColourId, accent);

    transfer.colour = accent;
    transfer.repaint();

    gainMeter.colour = accent;
    gainMeter.softColour = bypassed ? colours::dim : colours::bandSoft (band);
    gainMeter.repaint();

    repaint();
}

void BandPanel::setRange (float lowHz, float highHz)
{
    const auto text = theme::formatHz (lowHz) + " " + theme::enDash() + " " + theme::formatHz (highHz);

    if (text == rangeText)
        return;

    rangeText = text;
    repaint();
}

void BandPanel::tick (float dtSeconds)
{
    const auto& engine = processor.getEngine();

    // Same smoothing as the mockup: 30 ms toward the applied gain, and zero while bypassed.
    const auto target = bypassValue->load (std::memory_order_relaxed) > 0.5f
                            ? 0.0f
                            : juce::jlimit (-layout::dbRange, layout::dbRange, engine.getBandGainDb (band));
    shownGainDb += (target - shownGainDb) * (1.0f - std::exp (-dtSeconds / 0.03f));
    gainMeter.setGainDb (shownGainDb);

    // The engine reports one value per block, so a 30 Hz look at it would miss most
    // hits. The dot jumps up to the reading and lets go over about 80 ms.
    shownDot = juce::jmax (engine.getBandTransient (band), shownDot * std::exp (-dtSeconds / 0.08f));
    transfer.setTransient (shownDot);
}

void BandPanel::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isLeftButtonDown() && onSelectBand != nullptr)
        onSelectBand (band);
}

void BandPanel::focusOfChildComponentChanged (FocusChangeType)
{
    if (hasKeyboardFocus (true) && onSelectBand != nullptr)
        onSelectBand (band);
}

void BandPanel::resized()
{
    const auto border = band > 0 ? 1 : 0;
    const auto left = 14 + border, width = getWidth() - 28 - border;

    curveBox.setBounds (left, rowTop, 148, 38);

    // After the dropdown (148 + 8), the rotated "gain" label (10 + 4), then the plot.
    const juce::Rectangle<int> plot (left + 170, rowTop, width - 170, 38);
    transfer.setBounds (plot.expanded (6));

    const auto quarter = width / 4.0f;
    const auto centre = (float) left + (float) width * 0.5f;

    attack.setBounds ((int) std::lround ((float) left + quarter) - 22, slidersTop, 44, sliderH);
    sustain.setBounds ((int) std::lround ((float) left + quarter * 3.0f) - 22, slidersTop, 44, sliderH);

    gainMeter.setBounds ((int) std::lround (centre) - 7, slidersTop + layout::thumbSize / 2, 14, layout::trackLen);

    const auto readoutY = slidersTop + sliderH + 2 + 16;
    attackReadout.setBounds ((int) std::lround ((float) left + quarter) - 60, readoutY, 120, 31);
    sustainReadout.setBounds ((int) std::lround ((float) left + quarter * 3.0f) - 60, readoutY, 120, 31);
}

void BandPanel::paint (juce::Graphics& g)
{
    const auto colour = getAccent();
    const auto border = band > 0 ? 1 : 0;
    const auto left = 14 + border, width = getWidth() - 28 - border;

    if (selected)
    {
        g.setColour (colour.withAlpha (0.035f));
        g.fillRect (getLocalBounds().withTrimmedLeft (border));
        g.setColour (colour);
        g.fillRect (border, 0, getWidth() - border, 2);
    }

    if (border > 0)
    {
        g.setColour (colours::line);
        g.fillRect (0, 0, 1, getHeight());
    }

    // Header: letter-spaced band name, then the live range.
    const auto nameFont = theme::font (11.0f, true, 2.2f);
    const auto name = juce::String (bandNames[band]).toUpperCase();
    const auto nameW = std::ceil (juce::GlyphArrangement::getStringWidth (nameFont, name));
    const juce::Rectangle<float> headerRow ((float) left, headerTop, (float) width, 14.0f);

    g.setColour (colour);
    g.setFont (nameFont);
    g.drawText (name, headerRow, juce::Justification::centredLeft, false);

    g.setColour (colours::muted);
    g.setFont (theme::font (11.0f));
    g.drawText (rangeText, headerRow.withTrimmedLeft (nameW + 9.0f), juce::Justification::centredLeft, false);

    // Axis captions of the transfer graph.
    const auto caption = theme::font (9.0f, false, 0.54f);
    g.setColour (colours::dim);
    g.setFont (caption);

    const juce::Rectangle<float> plot ((float) (left + 170), (float) rowTop, (float) (width - 170), 38.0f);
    g.drawText ("detector", plot.withY (plot.getBottom() + 1.0f).withHeight (11.0f), juce::Justification::centredRight, false);

    const juce::Point<float> gainCentre ((float) (left + 156) + 5.0f, plot.getCentreY());
    g.saveState();
    g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, gainCentre.x, gainCentre.y));
    g.drawText ("gain", juce::Rectangle<float> (38.0f, 10.0f).withCentre (gainCentre), juce::Justification::centred, false);
    g.restoreState();

    // Slider labels, and the delta caption under the gain meter.
    const auto quarter = (float) width / 4.0f;
    const auto labelY = (float) (slidersTop + sliderH + 2);
    g.setColour (colours::muted);
    g.setFont (theme::font (15.0f));
    g.drawText ("Attack", juce::Rectangle<float> (120.0f, 16.0f).withCentre ({ (float) left + quarter, labelY + 8.0f }),
                juce::Justification::centred, false);
    g.drawText ("Sustain", juce::Rectangle<float> (120.0f, 16.0f).withCentre ({ (float) left + quarter * 3.0f, labelY + 8.0f }),
                juce::Justification::centred, false);

    g.setColour (colours::dim);
    g.setFont (theme::font (10.0f, false, 0.8f));
    g.drawText (theme::deltaSign(), juce::Rectangle<float> (30.0f, 12.0f)
                                        .withCentre ({ (float) left + (float) width * 0.5f, (float) (slidersTop + layout::trackLen + layout::thumbSize + 3) + 6.0f }),
                juce::Justification::centred, false);
}

} // namespace timaband::ui
