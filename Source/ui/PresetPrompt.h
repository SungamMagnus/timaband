#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace timaband::ui
{

// The two small dialogs of the preset menu, shown in a CallOutBox: callouts are not modal,
// which is not allowed in a plug-in, and they scale with the editor.

// "Save As...": a text field. onSubmit gets the typed name and returns an error to show
// (the prompt stays open), or an empty string when the name was accepted.
class NamePrompt final : public juce::Component
{
public:
    NamePrompt (const juce::String& title, const juce::String& initialText);

    std::function<juce::String (const juce::String&)> onSubmit;

    void focusEditor();
    void setMessage (const juce::String&);

    void resized() override;

private:
    void submit();
    void dismiss();

    juce::Label titleLabel, messageLabel;
    juce::TextEditor editor;
    juce::TextButton cancelButton { "Cancel" }, saveButton { "Save" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NamePrompt)
};

// "Delete": a question with a destructive button.
class ConfirmPrompt final : public juce::Component
{
public:
    ConfirmPrompt (const juce::String& message, const juce::String& confirmText);

    std::function<void()> onConfirm;

    void resized() override;

private:
    void dismiss();

    juce::Label messageLabel;
    juce::TextButton cancelButton { "Cancel" }, confirmButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConfirmPrompt)
};

} // namespace timaband::ui
