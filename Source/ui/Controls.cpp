#include "Controls.h"

namespace timaband::ui
{

/* ── StyledToggle ────────────────────────────────────────────────────────── */

StyledToggle::StyledToggle (Style s, const juce::String& text, int bandIndex)
    : FocusVisible<juce::ToggleButton> (text), style (s), band (bandIndex)
{
    setTitle (text);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);

    if (style == Style::textLink)
        setClickingTogglesState (false);
}

juce::Font StyledToggle::getFont (Style style)
{
    switch (style)
    {
        case Style::pill:     return theme::font (13.0f);
        case Style::bypass:
        case Style::mute:
        case Style::solo:     return theme::font (12.0f, true);
        case Style::bigLeft:
        case Style::bigRight: return theme::font (15.0f);
        case Style::segment:  return theme::font (12.0f);
        case Style::textLink: return theme::font (13.0f);
    }

    return theme::font (13.0f);
}

int StyledToggle::getIdealWidth() const
{
    const auto textWidth = (int) std::ceil (juce::GlyphArrangement::getStringWidth (getFont (style), getButtonText()));

    if (style == Style::pill)
        return 2 + 28 + 6 + 8 + textWidth;     // border, padding, dot, gap

    if (style == Style::segment)
        return 28 + textWidth;

    if (style == Style::textLink)
        return 16 + textWidth;

    return getWidth();
}

bool StyledToggle::hitTest (int x, int y)
{
    if (style == Style::bypass || style == Style::mute || style == Style::solo)
        return getLocalBounds().toFloat().getCentre().getDistanceFrom ({ (float) x, (float) y }) <= (float) getWidth() * 0.5f;

    return juce::ToggleButton::hitTest (x, y);
}

float StyledToggle::getFocusRingRadius() const
{
    switch (style)
    {
        case Style::pill:
        case Style::bypass:
        case Style::mute:
        case Style::solo:     return (float) getHeight() * 0.5f;
        case Style::bigLeft:
        case Style::bigRight: return 22.0f;
        case Style::segment:  return (float) getHeight() * 0.5f;
        case Style::textLink: return 4.0f;
    }

    return 4.0f;
}

/* ── SegmentedControl ────────────────────────────────────────────────────── */

SegmentedControl::SegmentedControl (const juce::StringArray& labels)
{
    for (int i = 0; i < labels.size(); ++i)
    {
        auto* seg = segments.add (new StyledToggle (StyledToggle::Style::segment, labels[i]));
        seg->setRadioGroupId (1);
        seg->onClick = [this, i] { select (i, true); };
        addAndMakeVisible (seg);
    }

    if (! segments.isEmpty())
        segments[0]->setToggleState (true, juce::dontSendNotification);
}

int SegmentedControl::getIdealWidth() const
{
    auto w = 2 + 4;     // outline and padding

    for (auto* seg : segments)
        w += seg->getIdealWidth();

    return w;
}

void SegmentedControl::setSelected (int index)
{
    select (index, false);
}

void SegmentedControl::select (int index, bool notify)
{
    index = juce::jlimit (0, segments.size() - 1, index);
    segments[index]->setToggleState (true, juce::dontSendNotification);

    if (index == selected)
        return;

    selected = index;

    if (notify && onChange != nullptr)
        onChange (index);
}

void SegmentedControl::paint (juce::Graphics& g)
{
    g.setColour (colours::outline);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), (float) getHeight() * 0.5f, 1.0f);
}

void SegmentedControl::resized()
{
    auto inner = getLocalBounds().reduced (3);

    for (auto* seg : segments)
        seg->setBounds (inner.removeFromLeft (seg->getIdealWidth()));
}

bool SegmentedControl::keyPressed (const juce::KeyPress& key)
{
    int step = 0;

    if (key == juce::KeyPress::leftKey || key == juce::KeyPress::upKey)        step = -1;
    else if (key == juce::KeyPress::rightKey || key == juce::KeyPress::downKey) step = 1;
    else return false;

    const auto next = juce::jlimit (0, segments.size() - 1, selected + step);
    select (next, true);
    segments[next]->grabKeyboardFocus();
    return true;
}

/* ── Sliders ─────────────────────────────────────────────────────────────── */

bool applySliderKey (juce::Slider& slider, const juce::KeyPress& key, double step, double fineStep, double pageStep)
{
    const auto delta = key.getModifiers().isShiftDown() ? fineStep : step;
    auto next = slider.getValue();

    if (key.isKeyCode (juce::KeyPress::upKey) || key.isKeyCode (juce::KeyPress::rightKey))     next += delta;
    else if (key.isKeyCode (juce::KeyPress::downKey) || key.isKeyCode (juce::KeyPress::leftKey)) next -= delta;
    else if (key.isKeyCode (juce::KeyPress::pageUpKey))   next += pageStep;
    else if (key.isKeyCode (juce::KeyPress::pageDownKey)) next -= pageStep;
    else if (key.isKeyCode (juce::KeyPress::homeKey))     next = slider.getMinimum();
    else if (key.isKeyCode (juce::KeyPress::endKey))      next = slider.getMaximum();
    else return false;

    juce::Slider::ScopedDragNotification drag (slider);
    slider.setValue (next, juce::sendNotificationSync);
    return true;
}

ThumbSlider::ThumbSlider (juce::Point<float> size)
    : thumbSize (size)
{
    setSliderStyle (juce::Slider::LinearVertical);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setVelocityBasedMode (false);
    setScrollWheelEnabled (true);
    setWantsKeyboardFocus (true);       // sliders don't take focus by default
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

juce::Rectangle<float> ThumbSlider::getThumbBounds() const
{
    return juce::Rectangle<float> (thumbSize.x, thumbSize.y)
               .withCentre ({ (float) getWidth() * 0.5f, (float) getPositionOfValue (getValue()) });
}

void ThumbSlider::mouseDown (const juce::MouseEvent& e)
{
    // A press on the thumb drags relative to where it sits; anywhere else jumps there.
    setSliderSnapsToMousePosition (! getThumbBounds().contains (e.position));
    juce::Slider::mouseDown (e);
}

void ThumbSlider::mouseEnter (const juce::MouseEvent& e)
{
    juce::Slider::mouseEnter (e);
    repaint();
}

void ThumbSlider::mouseExit (const juce::MouseEvent& e)
{
    juce::Slider::mouseExit (e);
    repaint();
}

BipolarSlider::BipolarSlider (int bandIndex)
    : ThumbSlider ({ (float) layout::thumbSize, (float) layout::thumbSize }), band (bandIndex)
{
    setMouseDragSensitivity (layout::trackLen);     // a full sweep takes one track length
    setColour (juce::Slider::trackColourId, colours::band (bandIndex));
}

bool BipolarSlider::keyPressed (const juce::KeyPress& key)
{
    return applySliderKey (*this, key, 0.5, 0.1, 3.0);
}

FaderSlider::FaderSlider()
    : ThumbSlider ({ (float) handleW, (float) handleH })
{
    setMouseDragSensitivity ((int) layout::meterH);
}

bool FaderSlider::keyPressed (const juce::KeyPress& key)
{
    return applySliderKey (*this, key, 1.0, 0.1, 6.0);
}

/* ── GainBox ─────────────────────────────────────────────────────────────── */

GainBox::GainBox()
{
    setSliderStyle (juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle (juce::Slider::TextBoxBelow, false, 52, 24);     // the look-and-feel stretches it over the whole box
    setScrollWheelEnabled (true);
    setWantsKeyboardFocus (true);
    setMouseDragSensitivity (250);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);

    // Plain text at rest; the look-and-feel styles the box that appears while typing.
    setColour (juce::Slider::textBoxTextColourId, colours::text);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, colours::accent.withAlpha (0.35f));
}

juce::Label* GainBox::getValueLabel() const
{
    for (auto* child : getChildren())
        if (auto* label = dynamic_cast<juce::Label*> (child))
            return label;

    return nullptr;
}

void GainBox::lookAndFeelChanged()
{
    juce::Slider::lookAndFeelChanged();

    // JUCE only lets bar-style sliders be dragged by their text box. Here the text box
    // is the whole control, so hand its mouse events to the slider ourselves.
    if (auto* label = getValueLabel())
    {
        label->addMouseListener (this, false);
        label->setMouseCursor (juce::MouseCursor::ParentCursor);
    }
}

void GainBox::useSignedText()
{
    textFromValueFunction = [] (double db) { return theme::formatDb ((float) db); };

    valueFromTextFunction = [] (const juce::String& text)
    {
        // Accept what the box shows (a true minus sign, maybe a unit) as well as plain typing.
        return text.replace (theme::minusSign(), "-").retainCharacters ("+-.0123456789").getDoubleValue();
    };

    updateText();

    // Alt-click resets. A plain click starts typing, so double-click can't also reset.
    setDoubleClickReturnValue (true, 0.0, juce::ModifierKeys::altModifier);
}

void GainBox::mouseEnter (const juce::MouseEvent& e)
{
    juce::Slider::mouseEnter (e);
    repaint();
}

void GainBox::mouseExit (const juce::MouseEvent& e)
{
    juce::Slider::mouseExit (e);
    repaint();
}

bool GainBox::keyPressed (const juce::KeyPress& key)
{
    return applySliderKey (*this, key, 0.5, 0.1, 3.0);
}

/* ── CurveBox ────────────────────────────────────────────────────────────── */

CurveBox::CurveBox (int bandIndex)
    : band (bandIndex)
{
    setColour (juce::ComboBox::textColourId, colours::band (bandIndex));
    setJustificationType (juce::Justification::centredLeft);
    setScrollWheelEnabled (false);
}

} // namespace timaband::ui
