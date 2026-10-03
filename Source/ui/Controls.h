#pragma once

#include <array>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "FocusRing.h"
#include "Theme.h"

// The control types that TimabandLookAndFeel knows how to draw. They are thin
// subclasses: behaviour is JUCE's, and the look is the look-and-feel's.
namespace timaband::ui
{

// Pill toggles, the power/M/S circles, the Bypass/Delta pair, a segment of the
// segmented control, and the plain-text Reset button.
class StyledToggle final : public FocusVisible<juce::ToggleButton>
{
public:
    enum class Style { pill, bypass, mute, solo, bigLeft, bigRight, segment, textLink };

    StyledToggle (Style, const juce::String& text, int bandIndex = 0);

    Style getStyle() const noexcept { return style; }
    int getBand() const noexcept    { return band; }

    static juce::Font getFont (Style);
    int getIdealWidth() const;

    bool hitTest (int x, int y) override;

    juce::Rectangle<float> getFocusRingBounds() const override { return getLocalBounds().toFloat(); }
    float getFocusRingRadius() const override;

private:
    Style style;
    int band;
};

// A round button with a drawn glyph: undo and redo in the header, and the previous/next
// triangles of the preset pill. Disabled it is drawn faint.
class IconButton final : public FocusVisible<juce::Button>
{
public:
    enum class Icon { undo, redo, previous, next };

    IconButton (Icon, const juce::String& title);

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    bool hitTest (int x, int y) override;

    juce::Rectangle<float> getFocusRingBounds() const override { return getLocalBounds().toFloat(); }
    float getFocusRingRadius() const override { return (float) getWidth() * 0.5f; }

private:
    Icon icon;
};

// Two or more labelled segments inside one outline; exactly one is selected.
class SegmentedControl final : public juce::Component
{
public:
    explicit SegmentedControl (const juce::StringArray& labels);

    int getSelected() const noexcept { return selected; }
    void setSelected (int index);

    int getIdealWidth() const;

    std::function<void (int)> onChange;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void select (int index, bool notify);

    juce::OwnedArray<StyledToggle> segments;
    int selected = 0;
};

// Applies the keys the sliders share: arrows, Page Up/Down, Home and End. The change
// is wrapped in a drag notification, so it gets the same begin/end gesture as a drag.
bool applySliderKey (juce::Slider&, const juce::KeyPress&, double step, double fineStep, double pageStep);

// A vertical slider with a thumb. Click on the track jumps there; a drag that
// starts on the thumb moves relative to it.
class ThumbSlider : public FocusVisible<juce::Slider>
{
public:
    explicit ThumbSlider (juce::Point<float> thumbSize);

    juce::Rectangle<float> getThumbBounds() const;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    juce::Rectangle<float> getFocusRingBounds() const override { return getThumbBounds(); }

private:
    juce::Point<float> thumbSize;
};

// Vertical, +-15 dB, filled from the centre.
class BipolarSlider final : public ThumbSlider
{
public:
    explicit BipolarSlider (int bandIndex);

    int getBand() const noexcept { return band; }

    bool keyPressed (const juce::KeyPress&) override;
    float getFocusRingRadius() const override { return layout::thumbSize * 0.5f; }

private:
    int band;
};

// The input/output gain fader: a small handle beside a meter pair, +-24 dB with
// 0 dB at the middle of the travel.
class FaderSlider final : public ThumbSlider
{
public:
    static constexpr int handleW = 12, handleH = 20;

    FaderSlider();

    bool keyPressed (const juce::KeyPress&) override;
    float getFocusRingRadius() const override { return 3.0f; }
};

// A signed one-decimal readout (input and output gain). Drag it vertically, or
// click it to type a value.
class GainBox final : public FocusVisible<juce::Slider>
{
public:
    GainBox();

    // The parameter attachment installs its own text conversion; this replaces it
    // with the readout's format ("+1.5", "0.0") and a parser that accepts it back.
    void useSignedText();

    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void lookAndFeelChanged() override;

    // The text box JUCE puts inside the slider, for typing a value.
    juce::Label* getValueLabel() const;

    juce::Rectangle<float> getFocusRingBounds() const override { return getLocalBounds().toFloat(); }
    float getFocusRingRadius() const override { return 3.0f; }
};

// One of a band's three curve buttons. The look-and-feel draws it: a grey key with
// the curve's glyph, filled with the ToggleButton::tickColourId colour while selected.
class CurveButton final : public juce::ToggleButton
{
public:
    explicit CurveButton (int curveIndex);

    int getCurve() const noexcept { return curve; }

private:
    int curve;
};

// A band's curve choice as three buttons in one radio group, in the style of Max's
// live.tab. The group is a single Tab stop; Left/Right (and Up/Down) move the selection.
class CurveSelector final : public FocusVisible<juce::Component>
{
public:
    static constexpr int buttonW = 44, buttonH = 30, buttonGap = 4;

    explicit CurveSelector (int bandIndex);

    int getSelected() const noexcept { return selected; }

    // Shows a curve without calling onChange, for changes that come from the parameter.
    void setSelected (int index);

    // The fill of the selected button.
    void setAccent (juce::Colour);

    // The user picked a curve, by mouse or key.
    std::function<void (int)> onChange;

    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    // The ring goes round the whole group rather than the selected button, so it stays put.
    juce::Rectangle<float> getFocusRingBounds() const override;
    float getFocusRingRadius() const override { return 2.0f; }

private:
    void select (int index, bool notify);

    juce::OwnedArray<CurveButton> buttons;
    int selected = 0;
};

} // namespace timaband::ui
