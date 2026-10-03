#include "LookAndFeel.h"

#include "Controls.h"
#include "Theme.h"

namespace timaband::ui
{

namespace
{
// A rounded rectangle with a choice of which corners are rounded.
juce::Path roundedPath (juce::Rectangle<float> r, float radius, bool left, bool right)
{
    juce::Path p;
    p.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), radius, radius,
                           left, right, left, right);
    return p;
}

void drawPill (juce::Graphics& g, StyledToggle& b, bool highlighted)
{
    const auto bounds = b.getLocalBounds().toFloat();
    const auto on = b.getToggleState();

    if (on)
    {
        g.setColour (colours::pillOnFill);
        g.fillRoundedRectangle (bounds.reduced (0.5f), bounds.getHeight() * 0.5f);
    }

    g.setColour (on ? colours::pillOnOutline : colours::outline);
    g.drawRoundedRectangle (bounds.reduced (0.5f), bounds.getHeight() * 0.5f, 1.0f);

    const juce::Point<float> dot (1.0f + 14.0f + 3.0f, bounds.getCentreY());

    if (on && ! look::flat)     // box-shadow: 0 0 6px accent
    {
        g.setGradientFill (juce::ColourGradient (colours::accent.withAlpha (0.55f), dot,
                                                 colours::accent.withAlpha (0.0f), dot.translated (9.0f, 0.0f), true));
        g.fillEllipse (juce::Rectangle<float> (18.0f, 18.0f).withCentre (dot));
    }

    g.setColour (on ? colours::pillDotOn : colours::dim);
    g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (dot));

    g.setColour (on ? colours::onText : (highlighted ? colours::text : colours::muted));
    g.setFont (StyledToggle::getFont (b.getStyle()));
    g.drawText (b.getButtonText(), juce::Rectangle<float> (29.0f, 0.0f, bounds.getWidth() - 29.0f, bounds.getHeight()),
                juce::Justification::centredLeft, false);
}

void drawMuteSolo (juce::Graphics& g, StyledToggle& b, bool highlighted)
{
    const auto bounds = b.getLocalBounds().toFloat();
    const auto on = b.getToggleState();
    const auto isMute = b.getStyle() == StyledToggle::Style::mute;

    const auto fill = isMute ? colours::band (b.getBand()) : colours::text;
    const auto edge = on ? fill : (highlighted ? colours::ringHover : colours::ringIdle);

    if (on)
    {
        g.setColour (fill);
        g.fillEllipse (bounds);
    }

    g.setColour (edge);
    g.drawEllipse (bounds.reduced (0.75f), 1.5f);

    g.setColour (on ? colours::onBandText : (highlighted ? colours::text : colours::muted));
    g.setFont (StyledToggle::getFont (b.getStyle()));
    g.drawText (b.getButtonText(), bounds, juce::Justification::centred, false);
}

// A band's power button, lit in the band colour while the band is processing. The
// toggle is on when the band is bypassed.
void drawPower (juce::Graphics& g, StyledToggle& b, bool highlighted)
{
    const auto bounds = b.getLocalBounds().toFloat();
    const auto lit = ! b.getToggleState();
    const auto colour = lit ? colours::band (b.getBand()) : (highlighted ? colours::muted : colours::dim);

    if (lit)
    {
        g.setColour (colour.withAlpha (highlighted ? 0.24f : 0.14f));
        g.fillEllipse (bounds);
    }

    g.setColour (colour);
    g.drawEllipse (bounds.reduced (0.75f), 1.5f);

    // The glyph: an arc open at the top, and a stroke through the gap.
    const auto centre = bounds.getCentre();
    juce::Path glyph;
    glyph.addCentredArc (centre.x, centre.y + 0.5f, 6.5f, 6.5f, 0.0f, 0.7f,
                         juce::MathConstants<float>::twoPi - 0.7f, true);
    glyph.startNewSubPath (centre.x, centre.y - 7.5f);
    glyph.lineTo (centre.x, centre.y - 1.0f);

    g.strokePath (glyph, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void drawBig (juce::Graphics& g, StyledToggle& b, bool highlighted)
{
    const auto bounds = b.getLocalBounds().toFloat();
    const auto on = b.getToggleState();
    const auto left = b.getStyle() == StyledToggle::Style::bigLeft;
    const auto path = roundedPath (bounds, 22.0f, left, ! left);

    g.setColour (on ? colours::onFill : colours::ctrl);
    g.fillPath (path);

    if (on && ! look::flat)     // inset 1px accent ring
    {
        g.saveState();
        g.reduceClipRegion (path);
        g.setColour (colours::accent.withAlpha (0.55f));
        g.strokePath (path, juce::PathStrokeType (2.0f));
        g.restoreState();
    }

    g.setColour (on ? colours::onText : (highlighted ? colours::text : colours::muted));
    g.setFont (StyledToggle::getFont (b.getStyle()));
    g.drawText (b.getButtonText(), bounds, juce::Justification::centred, false);
}

void drawSegment (juce::Graphics& g, StyledToggle& b, bool highlighted)
{
    const auto bounds = b.getLocalBounds().toFloat();
    const auto on = b.getToggleState();

    if (on)
    {
        g.setColour (colours::onFill);
        g.fillRoundedRectangle (bounds, bounds.getHeight() * 0.5f);
    }

    g.setColour (on ? colours::onText : (highlighted ? colours::text : colours::muted));
    g.setFont (StyledToggle::getFont (b.getStyle()));
    g.drawText (b.getButtonText(), bounds, juce::Justification::centred, false);
}

void drawTextLink (juce::Graphics& g, StyledToggle& b, bool highlighted)
{
    g.setColour (highlighted ? colours::text : colours::muted);
    g.setFont (StyledToggle::getFont (b.getStyle()));
    g.drawText (b.getButtonText(), b.getLocalBounds().toFloat(), juce::Justification::centred, false);
}

// A live.tab key: a flat key with an outline, filled with the accent while selected.
void drawCurveButton (juce::Graphics& g, CurveButton& b, bool highlighted, bool down)
{
    const auto on = b.getToggleState();
    auto fill = on ? b.findColour (juce::ToggleButton::tickColourId) : colours::tabIdle;

    if (down)
        fill = fill.darker (0.1f);
    else if (highlighted)
        fill = on ? fill.brighter (0.1f) : colours::tabIdleHi;

    const auto bounds = b.getLocalBounds().toFloat();

    // The outline is centred on this rectangle's edge, which keeps all of it inside the button.
    const auto edge = bounds.reduced (0.75f);

    g.setColour (fill);
    g.fillRoundedRectangle (edge, 2.0f);
    g.setColour (colours::tabOutline);
    g.drawRoundedRectangle (edge, 2.0f, 1.5f);

    theme::drawCurveGlyph (g, b.getCurve(), juce::Rectangle<float> (26.0f, 14.0f).withCentre (bounds.getCentre()),
                           on ? colours::tabGlyphOn : colours::tabGlyph);
}
} // namespace

TimabandLookAndFeel::TimabandLookAndFeel()
{
    // The only popup left is the right-click menu of the gain readout's text editor.
    setColour (juce::PopupMenu::backgroundColourId, colours::menuBg);
    setColour (juce::PopupMenu::textColourId, colours::text);

    setColour (juce::TooltipWindow::backgroundColourId, colours::menuBg);
    setColour (juce::TooltipWindow::textColourId, colours::text);
    setColour (juce::TooltipWindow::outlineColourId, colours::menuBorder);

    setColour (juce::Slider::thumbColourId, colours::thumbBody);
    setColour (juce::Label::textColourId, colours::text);
}

/* ── Buttons ─────────────────────────────────────────────────────────────── */

void TimabandLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool down)
{
    if (auto* curve = dynamic_cast<CurveButton*> (&button))
    {
        drawCurveButton (g, *curve, highlighted, down);
        return;
    }

    auto* b = dynamic_cast<StyledToggle*> (&button);

    if (b == nullptr)
    {
        LookAndFeel_V4::drawToggleButton (g, button, highlighted, down);
        return;
    }

    switch (b->getStyle())
    {
        case StyledToggle::Style::pill:     drawPill (g, *b, highlighted); break;
        case StyledToggle::Style::bypass:   drawPower (g, *b, highlighted); break;
        case StyledToggle::Style::mute:
        case StyledToggle::Style::solo:     drawMuteSolo (g, *b, highlighted); break;
        case StyledToggle::Style::bigLeft:
        case StyledToggle::Style::bigRight: drawBig (g, *b, highlighted); break;
        case StyledToggle::Style::segment:  drawSegment (g, *b, highlighted); break;
        case StyledToggle::Style::textLink: drawTextLink (g, *b, highlighted); break;
    }
}

/* ── Sliders ─────────────────────────────────────────────────────────────── */

int TimabandLookAndFeel::getSliderThumbRadius (juce::Slider& slider)
{
    if (dynamic_cast<BipolarSlider*> (&slider) != nullptr)
        return layout::thumbSize / 2;

    if (dynamic_cast<FaderSlider*> (&slider) != nullptr)
        return FaderSlider::handleH / 2;

    return LookAndFeel_V4::getSliderThumbRadius (slider);
}

juce::Slider::SliderLayout TimabandLookAndFeel::getSliderLayout (juce::Slider& slider)
{
    if (dynamic_cast<GainBox*> (&slider) == nullptr)
        return LookAndFeel_V4::getSliderLayout (slider);

    // The value box is the whole control.
    juce::Slider::SliderLayout result;
    result.sliderBounds = result.textBoxBounds = slider.getLocalBounds();
    return result;
}

juce::Label* TimabandLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox (slider);

    if (dynamic_cast<GainBox*> (&slider) != nullptr)
    {
        label->setFont (theme::font (14.0f));
        label->setJustificationType (juce::Justification::centred);
        label->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);

        // While typing.
        label->setColour (juce::Label::textWhenEditingColourId, colours::editText);
        label->setColour (juce::Label::backgroundWhenEditingColourId, colours::menuBg);
        label->setColour (juce::Label::outlineWhenEditingColourId, colours::accent);
        label->setColour (juce::TextEditor::highlightColourId, colours::accent.withAlpha (0.35f));
    }

    return label;
}

namespace
{
void drawBipolar (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, juce::Slider& slider)
{
    const auto band = slider.findColour (juce::Slider::trackColourId);
    const auto cx = (float) x + (float) width * 0.5f;
    const auto top = (float) y, length = (float) height;
    const auto centre = top + length * 0.5f;

    g.setColour (colours::trackFill);
    g.fillRoundedRectangle (cx - 3.0f, top, 6.0f, length, 3.0f);

    const auto fillTop = juce::jmin (sliderPos, centre), fillBottom = juce::jmax (sliderPos, centre);

    if (fillBottom - fillTop > 0.01f)
    {
        g.setColour (band);
        g.fillRoundedRectangle (cx - 3.0f, fillTop, 6.0f, fillBottom - fillTop, 3.0f);
    }

    g.setColour (colours::muted);
    g.fillRect (cx - 8.0f, centre - 0.5f, 16.0f, 1.0f);

    const auto thumb = juce::Rectangle<float> ((float) layout::thumbSize, (float) layout::thumbSize)
                           .withCentre ({ cx, sliderPos });

    if (slider.isMouseOverOrDragging() || slider.isMouseButtonDown())
    {
        g.setColour (band.withAlpha (0.22f));
        g.fillEllipse (thumb.expanded (4.0f));
    }

    if (! look::flat)
    {
        juce::Path disc;
        disc.addEllipse (thumb);
        juce::DropShadow (juce::Colours::black.withAlpha (0.55f), 4, { 0, 1 }).drawForPath (g, disc);
    }

    g.setColour (colours::thumbBody);
    g.fillEllipse (thumb);

    g.setColour (colours::thumbDot);
    g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (thumb.getCentre()));
}

// The mockup's fader: a small rounded rectangle with a line through its middle. A
// faint groove and a mark at 0 dB show where it travels.
void drawFader (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, juce::Slider& slider)
{
    const auto cx = (float) x + (float) width * 0.5f;
    const auto centre = (float) y + (float) height * 0.5f;
    const auto active = slider.isMouseOverOrDragging() || slider.isMouseButtonDown();

    g.setColour (active ? colours::faderGrooveHi : colours::faderGroove);
    g.fillRoundedRectangle (cx - 1.0f, (float) y, 2.0f, (float) height, 1.0f);

    g.setColour (active ? colours::faderZeroHi : colours::faderZero);
    g.fillRect (cx - 6.0f, centre - 0.5f, 12.0f, 1.0f);

    const auto handle = juce::Rectangle<float> ((float) FaderSlider::handleW, (float) FaderSlider::handleH)
                            .withCentre ({ cx, sliderPos });

    g.setColour (active ? colours::faderHandleHi : colours::faderHandle);
    g.fillRoundedRectangle (handle, 3.0f);

    g.setColour (active ? colours::faderEdgeHi : colours::faderEdge);
    g.drawRoundedRectangle (handle.reduced (0.5f), 2.5f, 1.0f);

    g.setColour (colours::faderMark);
    g.fillRect (handle.getX() + 1.0f, handle.getCentreY() - 0.5f, handle.getWidth() - 2.0f, 1.0f);
}
} // namespace

void TimabandLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                            float sliderPos, float minSliderPos, float maxSliderPos,
                                            juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (dynamic_cast<BipolarSlider*> (&slider) != nullptr)
        drawBipolar (g, x, y, width, height, sliderPos, slider);
    else if (dynamic_cast<FaderSlider*> (&slider) != nullptr)
        drawFader (g, x, y, width, height, sliderPos, slider);
    else
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
}

void TimabandLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float,
                                            float, float, juce::Slider& slider)
{
    // The number itself is the slider's text box; this is only the hover highlight behind it.
    if (slider.isMouseOverOrDragging (true) || slider.isMouseButtonDown (true))
    {
        g.setColour (colours::hoverFill);
        g.fillRoundedRectangle (juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height), 3.0f);
    }
}

} // namespace timaband::ui
