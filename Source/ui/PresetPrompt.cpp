#include "PresetPrompt.h"

#include "Theme.h"

namespace timaband::ui
{

namespace
{
constexpr int pad = 14, promptW = 300, buttonH = 28, buttonW = 76;

void styleLabel (juce::Label& label, juce::Colour colour, float size)
{
    label.setFont (theme::font (size));
    label.setColour (juce::Label::textColourId, colour);
    label.setMinimumHorizontalScale (1.0f);
    label.setInterceptsMouseClicks (false, false);
}

void styleButtons (juce::TextButton& cancel, juce::TextButton& action)
{
    cancel.setColour (juce::TextButton::buttonColourId, colours::ctrl);
    cancel.setColour (juce::TextButton::textColourOffId, colours::text);

    action.setColour (juce::TextButton::buttonColourId, colours::onFill);
    action.setColour (juce::TextButton::textColourOffId, colours::onText);
}

void dismissCallOut (juce::Component& c)
{
    if (auto* box = c.findParentComponentOfClass<juce::CallOutBox>())
        box->dismiss();
}
} // namespace

/* ── NamePrompt ──────────────────────────────────────────────────────────── */

NamePrompt::NamePrompt (const juce::String& title, const juce::String& initialText)
{
    setTitle (title);
    setSize (promptW, pad + 16 + 6 + 30 + 6 + 16 + 10 + buttonH + pad);

    titleLabel.setText (title, juce::dontSendNotification);
    styleLabel (titleLabel, colours::muted, 13.0f);
    messageLabel.setText ({}, juce::dontSendNotification);
    styleLabel (messageLabel, colours::text, 12.0f);

    editor.setFont (theme::font (15.0f));
    editor.setTitle ("Preset name");
    editor.setText (initialText, juce::dontSendNotification);
    editor.setSelectAllWhenFocused (true);
    editor.setInputRestrictions (80);
    editor.setIndents (8, 6);
    editor.setColour (juce::TextEditor::backgroundColourId, colours::panel);
    editor.setColour (juce::TextEditor::textColourId, colours::editText);
    editor.setColour (juce::TextEditor::outlineColourId, colours::outline);
    editor.setColour (juce::TextEditor::focusedOutlineColourId, colours::outlineHi);
    editor.setColour (juce::TextEditor::highlightColourId, colours::accent.withAlpha (0.35f));
    editor.setColour (juce::TextEditor::highlightedTextColourId, colours::editText);
    editor.setColour (juce::CaretComponent::caretColourId, colours::editText);
    editor.onReturnKey = [this] { submit(); };
    editor.onEscapeKey = [this] { dismiss(); };

    styleButtons (cancelButton, saveButton);
    cancelButton.onClick = [this] { dismiss(); };
    saveButton.onClick   = [this] { submit(); };

    addAndMakeVisible (titleLabel);
    addAndMakeVisible (editor);
    addAndMakeVisible (messageLabel);
    addAndMakeVisible (cancelButton);
    addAndMakeVisible (saveButton);
}

void NamePrompt::focusEditor()
{
    editor.grabKeyboardFocus();
}

void NamePrompt::setMessage (const juce::String& text)
{
    messageLabel.setText (text, juce::dontSendNotification);
}

void NamePrompt::resized()
{
    auto area = getLocalBounds().reduced (pad);

    titleLabel.setBounds (area.removeFromTop (16));
    area.removeFromTop (6);
    editor.setBounds (area.removeFromTop (30));
    area.removeFromTop (6);
    messageLabel.setBounds (area.removeFromTop (16));
    area.removeFromTop (10);

    auto row = area.removeFromTop (buttonH);
    saveButton.setBounds (row.removeFromRight (buttonW));
    row.removeFromRight (8);
    cancelButton.setBounds (row.removeFromRight (buttonW));
}

void NamePrompt::submit()
{
    const auto error = onSubmit != nullptr ? onSubmit (editor.getText()) : juce::String();

    if (error.isEmpty())
        dismiss();
    else
        setMessage (error);
}

void NamePrompt::dismiss()
{
    dismissCallOut (*this);
}

/* ── ConfirmPrompt ───────────────────────────────────────────────────────── */

ConfirmPrompt::ConfirmPrompt (const juce::String& message, const juce::String& confirmText)
    : confirmButton (confirmText)
{
    setTitle ("Confirm");
    setSize (promptW, pad + 36 + 12 + buttonH + pad);

    messageLabel.setText (message, juce::dontSendNotification);
    messageLabel.setJustificationType (juce::Justification::topLeft);
    styleLabel (messageLabel, colours::text, 13.0f);

    styleButtons (cancelButton, confirmButton);
    cancelButton.onClick  = [this] { dismiss(); };
    confirmButton.onClick = [this]
    {
        if (onConfirm != nullptr)
            onConfirm();

        dismiss();
    };

    addAndMakeVisible (messageLabel);
    addAndMakeVisible (cancelButton);
    addAndMakeVisible (confirmButton);
}

void ConfirmPrompt::resized()
{
    auto area = getLocalBounds().reduced (pad);

    messageLabel.setBounds (area.removeFromTop (36));
    area.removeFromTop (12);

    auto row = area.removeFromTop (buttonH);
    confirmButton.setBounds (row.removeFromRight (buttonW));
    row.removeFromRight (8);
    cancelButton.setBounds (row.removeFromRight (buttonW));
}

void ConfirmPrompt::dismiss()
{
    dismissCallOut (*this);
}

} // namespace timaband::ui
