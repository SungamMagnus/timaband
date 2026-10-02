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
        g.setColour (colours::white.withAlpha (0.05f));
        g.fillRoundedRectangle (bounds.reduced (0.5f), bounds.getHeight() * 0.5f);
    }

    g.setColour (on ? colours::outlineHi : colours::outline);
    g.drawRoundedRectangle (bounds.reduced (0.5f), bounds.getHeight() * 0.5f, 1.0f);

    const juce::Point<float> dot (1.0f + 14.0f + 3.0f, bounds.getCentreY());

    if (on)     // box-shadow: 0 0 6px accent
    {
        g.setGradientFill (juce::ColourGradient (colours::accent.withAlpha (0.55f), dot,
                                                 colours::accent.withAlpha (0.0f), dot.translated (9.0f, 0.0f), true));
        g.fillEllipse (juce::Rectangle<float> (18.0f, 18.0f).withCentre (dot));
    }

    g.setColour (on ? colours::accent : colours::dim);
    g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (dot));

    g.setColour (on ? colours::white : (highlighted ? colours::text : colours::muted));
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
    const auto edge = on ? fill : (highlighted ? colours::text : colours::muted);

    if (on)
    {
        g.setColour (fill);
        g.fillEllipse (bounds);
    }

    g.setColour (edge);
    g.drawEllipse (bounds.reduced (0.75f), 1.5f);

    g.setColour (on ? colours::ink : (highlighted ? colours::text : colours::muted));
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

    g.setColour (on ? colours::ctrlHi : colours::ctrl);
    g.fillPath (path);

    if (on)     // inset 1px accent ring
    {
        g.saveState();
        g.reduceClipRegion (path);
        g.setColour (colours::accent.withAlpha (0.55f));
        g.strokePath (path, juce::PathStrokeType (2.0f));
        g.restoreState();
    }

    g.setColour (on ? colours::white : (highlighted ? colours::text : colours::muted));
    g.setFont (StyledToggle::getFont (b.getStyle()));
    g.drawText (b.getButtonText(), bounds, juce::Justification::centred, false);
}

void drawSegment (juce::Graphics& g, StyledToggle& b, bool highlighted)
{
    const auto bounds = b.getLocalBounds().toFloat();
    const auto on = b.getToggleState();

    if (on)
    {
        g.setColour (colours::ctrlHi);
        g.fillRoundedRectangle (bounds, bounds.getHeight() * 0.5f);
    }

    g.setColour (on ? colours::white : (highlighted ? colours::text : colours::muted));
    g.setFont (StyledToggle::getFont (b.getStyle()));
    g.drawText (b.getButtonText(), bounds, juce::Justification::centred, false);
}

void drawTextLink (juce::Graphics& g, StyledToggle& b, bool highlighted)
{
    g.setColour (highlighted ? colours::text : colours::muted);
    g.setFont (StyledToggle::getFont (b.getStyle()));
    g.drawText (b.getButtonText(), b.getLocalBounds().toFloat(), juce::Justification::centred, false);
}
} // namespace

TimabandLookAndFeel::TimabandLookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId, colours::ctrlHi);
    setColour (juce::PopupMenu::textColourId, colours::text);
    setColour (juce::ComboBox::arrowColourId, colours::muted);
    setColour (juce::Slider::thumbColourId, colours::white);
    setColour (juce::Label::textColourId, colours::text);
}

/* ── Buttons ─────────────────────────────────────────────────────────────── */

void TimabandLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool down)
{
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

/* ── Curve dropdown ──────────────────────────────────────────────────────── */

void TimabandLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                        int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float> ((float) width, (float) height);

    g.setColour (box.isMouseOver (true) || box.isPopupActive() ? colours::ctrlHi : colours::ctrl);
    g.fillRoundedRectangle (bounds, 3.0f);

    const auto index = box.getSelectedItemIndex();

    if (index >= 0)
        theme::drawCurveGlyph (g, index, juce::Rectangle<float> (11.0f, (bounds.getHeight() - 14.0f) * 0.5f, 24.0f, 14.0f),
                               box.findColour (juce::ComboBox::textColourId));

    juce::Path chevron;
    const auto origin = juce::Point<float> (bounds.getWidth() - 10.0f - 12.0f, (bounds.getHeight() - 12.0f) * 0.5f);
    chevron.startNewSubPath (origin.translated (2.5f, 4.5f));
    chevron.lineTo (origin.translated (6.0f, 8.0f));
    chevron.lineTo (origin.translated (9.5f, 4.5f));

    g.setColour (colours::muted);
    g.strokePath (chevron, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font TimabandLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return theme::font (14.0f);
}

void TimabandLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    // After the glyph (11 + 24 + 8) and before the chevron (12 + 10), plus its gap.
    label.setBounds (43, 0, box.getWidth() - 43 - 8 - 12 - 10, box.getHeight());
    label.setFont (getComboBoxFont (box));
}

juce::PopupMenu::Options TimabandLookAndFeel::getOptionsForComboBoxPopupMenu (juce::ComboBox& box, juce::Label& label)
{
    return LookAndFeel_V4::getOptionsForComboBoxPopupMenu (box, label)
               .withMinimumWidth (168)
               .withStandardItemHeight (32);
}

juce::Font TimabandLookAndFeel::getPopupMenuFont()
{
    return theme::font (14.0f);
}

int TimabandLookAndFeel::getPopupMenuBorderSizeWithOptions (const juce::PopupMenu::Options&)
{
    return 5;       // 1px border and 4px padding
}

void TimabandLookAndFeel::drawPopupMenuBackgroundWithOptions (juce::Graphics& g, int width, int height,
                                                              const juce::PopupMenu::Options&)
{
    g.fillAll (colours::ctrlHi);
    g.setColour (colours::menuBorder);
    g.drawRect (0, 0, width, height, 1);
}

void TimabandLookAndFeel::drawPopupMenuItemWithOptions (juce::Graphics& g, const juce::Rectangle<int>& area, bool isHighlighted,
                                                        const juce::PopupMenu::Item& item, const juce::PopupMenu::Options& options)
{
    if (item.isSeparator || item.isSectionHeader)
    {
        LookAndFeel_V4::drawPopupMenuItemWithOptions (g, area, isHighlighted, item, options);
        return;
    }

    // The dropdown's text colour is the band colour; the menu borrows it for the glyph and the ticked item.
    const auto bandColour = options.getTargetComponent() != nullptr
                                ? options.getTargetComponent()->findColour (juce::ComboBox::textColourId)
                                : colours::text;

    if (isHighlighted && item.isEnabled)
    {
        g.setColour (colours::white.withAlpha (0.08f));
        g.fillRoundedRectangle (area.toFloat(), 3.0f);
    }

    const auto curve = theme::curveIndexForName (item.text);

    if (curve >= 0)
        theme::drawCurveGlyph (g, curve, juce::Rectangle<float> (24.0f, 14.0f)
                                             .withCentre ({ (float) area.getX() + 10.0f + 12.0f, (float) area.getCentreY() }),
                               bandColour);

    g.setColour (item.isTicked ? bandColour : colours::text);
    g.setFont (getPopupMenuFont());
    g.drawText (item.text, area.withTrimmedLeft (10 + 24 + 10).withTrimmedRight (10),
                juce::Justification::centredLeft, false);
}

void TimabandLookAndFeel::getIdealPopupMenuItemSizeWithOptions (const juce::String& text, bool isSeparator,
                                                                int standardMenuItemHeight, int& idealWidth, int& idealHeight,
                                                                const juce::PopupMenu::Options& options)
{
    if (isSeparator)
    {
        LookAndFeel_V4::getIdealPopupMenuItemSizeWithOptions (text, isSeparator, standardMenuItemHeight,
                                                              idealWidth, idealHeight, options);
        return;
    }

    idealWidth = 10 + 24 + 10 + (int) std::ceil (juce::GlyphArrangement::getStringWidth (getPopupMenuFont(), text)) + 10;
    idealHeight = 32;
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
        label->setColour (juce::Label::textWhenEditingColourId, colours::white);
        label->setColour (juce::Label::backgroundWhenEditingColourId, colours::ctrlHi);
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

    g.setColour (colours::ctrlHi);
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

    juce::Path disc;
    disc.addEllipse (thumb);
    juce::DropShadow (juce::Colours::black.withAlpha (0.55f), 4, { 0, 1 }).drawForPath (g, disc);

    g.setColour (colours::white);
    g.fillEllipse (thumb);

    g.setColour (colours::ink);
    g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (thumb.getCentre()));
}

// The mockup's fader: a small rounded rectangle with a line through its middle. A
// faint groove and a mark at 0 dB show where it travels.
void drawFader (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, juce::Slider& slider)
{
    const auto cx = (float) x + (float) width * 0.5f;
    const auto centre = (float) y + (float) height * 0.5f;
    const auto active = slider.isMouseOverOrDragging() || slider.isMouseButtonDown();

    g.setColour (colours::ctrl.withAlpha (active ? 0.9f : 0.5f));
    g.fillRoundedRectangle (cx - 1.0f, (float) y, 2.0f, (float) height, 1.0f);

    g.setColour (colours::muted.withAlpha (active ? 0.9f : 0.5f));
    g.fillRect (cx - 6.0f, centre - 0.5f, 12.0f, 1.0f);

    const auto handle = juce::Rectangle<float> ((float) FaderSlider::handleW, (float) FaderSlider::handleH)
                            .withCentre ({ cx, sliderPos });

    g.setColour (active ? colours::ctrlHi : colours::ctrl);
    g.fillRoundedRectangle (handle, 3.0f);

    g.setColour (active ? colours::outlineHi : colours::outline);
    g.drawRoundedRectangle (handle.reduced (0.5f), 2.5f, 1.0f);

    g.setColour (colours::low);
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
        g.setColour (colours::ctrl);
        g.fillRoundedRectangle (juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height), 3.0f);
    }
}

} // namespace timaband::ui
