#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Controls.h"

namespace timaband::ui
{

// The header's preset control: a list glyph, the preset name (with a dot while the state
// differs from the loaded preset), and previous / next triangles.
class PresetPill final : public juce::Component
{
public:
    static constexpr int width = 420, height = 36;

    PresetPill();
    ~PresetPill() override;

    void setState (const juce::String& presetName, bool modified);

    const juce::String& getPresetName() const noexcept  { return name; }
    bool isModified() const noexcept                    { return modified; }

    std::function<void()> onOpenMenu, onPrevious, onNext;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class NameButton;

    std::unique_ptr<NameButton> nameButton;
    IconButton previousButton, nextButton;
    juce::String name;
    bool modified = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetPill)
};

} // namespace timaband::ui
