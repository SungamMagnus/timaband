#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Theme.h"

namespace timaband::ui
{

// Implemented by controls that show a keyboard focus ring. The ring is drawn by
// a single overlay (FocusRing) rather than by each control, so it can sit outside
// the control's bounds without being clipped. Like :focus-visible in the mockup,
// only focus that arrived through the Tab key shows it.
class FocusRingProvider
{
public:
    virtual ~FocusRingProvider() = default;

    // In the control's own coordinates.
    virtual juce::Rectangle<float> getFocusRingBounds() const = 0;
    virtual float getFocusRingRadius() const = 0;

    bool isFocusRingVisible() const noexcept { return ringVisible; }

protected:
    bool ringVisible = false;
};

template <typename Base>
class FocusVisible : public Base, public FocusRingProvider
{
public:
    using Base::Base;

    void focusGained (juce::Component::FocusChangeType type) override
    {
        ringVisible = (type == juce::Component::focusChangedByTabKey);
        Base::focusGained (type);
        this->repaint();
    }

    void focusLost (juce::Component::FocusChangeType type) override
    {
        ringVisible = false;
        Base::focusLost (type);
        this->repaint();
    }
};

// Transparent overlay covering the whole editor content.
class FocusRing final : public juce::Component,
                        private juce::FocusChangeListener
{
public:
    FocusRing()
    {
        setInterceptsMouseClicks (false, false);
        juce::Desktop::getInstance().addFocusChangeListener (this);
    }

    ~FocusRing() override { juce::Desktop::getInstance().removeFocusChangeListener (this); }

    void paint (juce::Graphics& g) override
    {
        auto* focused = juce::Component::getCurrentlyFocusedComponent();
        auto* provider = dynamic_cast<FocusRingProvider*> (focused);

        // Only controls inside the editor: the overlay is a sibling of what it rings.
        if (provider == nullptr || ! provider->isFocusRingVisible()
              || getParentComponent() == nullptr || ! getParentComponent()->isParentOf (focused))
            return;

        const auto area = getLocalArea (focused, provider->getFocusRingBounds());
        const auto radius = provider->getFocusRingRadius();

        // outline: 2px solid accent, outline-offset: 2px
        g.setColour (colours::accent);
        g.drawRoundedRectangle (area.expanded (3.0f), radius + 3.0f, 2.0f);
    }

private:
    void globalFocusChanged (juce::Component*) override { repaint(); }
};

} // namespace timaband::ui
