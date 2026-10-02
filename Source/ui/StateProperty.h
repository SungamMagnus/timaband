#pragma once

#include <functional>

#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

namespace timaband::ui
{

// Follows one property of the state tree (the UI-only state in uiState::*).
// The callback always runs on the message thread, even when the host loads state
// from another thread.
class StateProperty final : private juce::ValueTree::Listener,
                            private juce::AsyncUpdater
{
public:
    StateProperty (juce::ValueTree stateTree, const juce::Identifier& propertyId, std::function<void()> onChangeCallback)
        : tree (std::move (stateTree)), id (propertyId), onChange (std::move (onChangeCallback))
    {
        tree.addListener (this);
    }

    ~StateProperty() override
    {
        cancelPendingUpdate();
        tree.removeListener (this);
    }

    juce::var get() const { return tree.getProperty (id); }
    void set (const juce::var& v) { tree.setProperty (id, v, nullptr); }

private:
    void valueTreePropertyChanged (juce::ValueTree& changed, const juce::Identifier& property) override
    {
        if (property != id || changed != tree)
            return;

        if (juce::MessageManager::existsAndIsCurrentThread())
        {
            cancelPendingUpdate();
            if (onChange) onChange();
        }
        else
        {
            triggerAsyncUpdate();
        }
    }

    void handleAsyncUpdate() override { if (onChange) onChange(); }

    juce::ValueTree tree;
    juce::Identifier id;
    std::function<void()> onChange;
};

} // namespace timaband::ui
