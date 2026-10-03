#pragma once

#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Presets.h"

namespace timaband
{

/**
 * Turns the editor's parameter gestures into undo steps on a juce::UndoManager.
 *
 * A step starts at the first beginChangeGesture() (or Scope) and ends when the last open
 * gesture ends, so a slider drag, a click, a Link-bands drag that holds three parameters,
 * and a preset load that holds twenty are each one step. At the end the parameters that
 * were touched are compared with how they were when they were first touched; a step with
 * no net change is dropped.
 *
 * Only gestures are recorded. Host automation calls setValue() without one, so it never
 * touches the history; and a parameter that moves while someone else's gesture is open is
 * not in that gesture's touched set. That is also why the AudioProcessorValueTreeState is
 * not given the UndoManager: it records on its own timer, with no idea where a change came
 * from, and would fill the stack during playback and clear the redo list.
 *
 * Message thread only. An action applies its values with begin/end gestures of its own, so
 * the host sees an undo like any other edit; the recorder ignores those.
 */
class UndoRecorder final : private juce::AudioProcessorParameter::Listener
{
public:
    UndoRecorder (juce::AudioProcessor&, juce::AudioProcessorValueTreeState&, juce::UndoManager&);
    ~UndoRecorder() override;

    // Keeps one step open while several gestures follow each other.
    class Scope final
    {
    public:
        explicit Scope (UndoRecorder&);
        ~Scope();

    private:
        UndoRecorder& owner;
    };

    // Marks wheel and key input. A step that starts inside it joins the one before when that
    // touched the same parameters and ended less than coalesceMs ago.
    class Coalesce final
    {
    public:
        Coalesce() noexcept  { ++depth; }
        ~Coalesce()          { --depth; }

    private:
        friend class UndoRecorder;
        static inline int depth = 0;
    };

    static constexpr double coalesceMs = 800.0;

    // True when no gesture or scope is open, which is when undo and redo are safe.
    bool isIdle() const noexcept     { return scopeDepth == 0 && openTotal == 0; }

    // Clears the history and forgets any open step (for a session's state arriving from the host).
    void clear();

private:
    struct Change
    {
        int index;
        float before, after;
    };

    class Action;

    void parameterValueChanged (int, float) override {}
    void parameterGestureChanged (int index, bool starting) override;

    void open();
    void closeIfDone();
    void close();

    juce::AudioProcessor& processor;
    juce::AudioProcessorValueTreeState& apvts;
    juce::UndoManager& undoManager;

    std::vector<juce::AudioProcessorParameter*> params;
    std::vector<int> openGestures;
    int openTotal = 0, scopeDepth = 0;

    bool stepOpen = false, stepIsBurst = false;
    std::vector<std::pair<int, float>> touched;         // parameter index, value when first touched
    presetState::Id presetBefore;

    bool lastWasBurst = false;
    double lastEndMs = 0.0;
    std::vector<int> lastIndices;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UndoRecorder)
};

} // namespace timaband
