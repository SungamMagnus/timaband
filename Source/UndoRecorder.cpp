#include "UndoRecorder.h"

#include <algorithm>
#include <cmath>

namespace timaband
{

/* ── Action ──────────────────────────────────────────────────────────────── */

// One undo step: the parameters that changed, and the preset name if that changed too. The
// change has already happened when the step is recorded, so the first perform() is a no-op.
class UndoRecorder::Action final : public juce::UndoableAction
{
public:
    Action (UndoRecorder& r, std::vector<Change> c, presetState::Id before, presetState::Id after, bool alreadyApplied)
        : owner (r), changes (std::move (c)), presetBefore (std::move (before)), presetAfter (std::move (after)),
          skipFirstPerform (alreadyApplied)
    {
    }

    bool perform() override
    {
        if (skipFirstPerform)
        {
            skipFirstPerform = false;
            return true;
        }

        apply (true);
        return true;
    }

    bool undo() override
    {
        apply (false);
        return true;
    }

    // Wheel and key bursts: the same parameters again, so the step just reaches further.
    juce::UndoableAction* createCoalescedAction (juce::UndoableAction* next) override
    {
        auto* n = dynamic_cast<Action*> (next);

        if (n == nullptr || n->changes.size() != changes.size())
            return nullptr;

        auto merged = changes;

        for (size_t i = 0; i < merged.size(); ++i)
        {
            if (n->changes[i].index != merged[i].index)
                return nullptr;

            merged[i].after = n->changes[i].after;
        }

        return new Action (owner, std::move (merged), presetBefore, n->presetAfter, false);
    }

private:
    void apply (bool useAfter)
    {
        // The host gets each change as a touch of its own, like anything else the editor does.
        for (const auto& c : changes)
        {
            auto* p = owner.params[(size_t) c.index];

            p->beginChangeGesture();
            p->setValueNotifyingHost (useAfter ? c.after : c.before);
            p->endChangeGesture();
        }

        // A step that did not change the preset name (a tweak) leaves the name alone, so an
        // undo after Save As does not rename the preset back.
        if (presetBefore != presetAfter)
            presetState::write (owner.apvts.state, useAfter ? presetAfter : presetBefore);
    }

    UndoRecorder& owner;
    std::vector<Change> changes;
    presetState::Id presetBefore, presetAfter;
    bool skipFirstPerform;
};

/* ── UndoRecorder ────────────────────────────────────────────────────────── */

UndoRecorder::UndoRecorder (juce::AudioProcessor& p, juce::AudioProcessorValueTreeState& state, juce::UndoManager& um)
    : processor (p), apvts (state), undoManager (um)
{
    for (auto* param : processor.getParameters())
    {
        params.push_back (param);
        param->addListener (this);
    }

    openGestures.assign (params.size(), 0);
}

UndoRecorder::~UndoRecorder()
{
    for (auto* param : params)
        param->removeListener (this);
}

UndoRecorder::Scope::Scope (UndoRecorder& r)
    : owner (r)
{
    ++owner.scopeDepth;
    owner.open();
}

UndoRecorder::Scope::~Scope()
{
    --owner.scopeDepth;
    owner.closeIfDone();
}

void UndoRecorder::clear()
{
    undoManager.clearUndoHistory();

    std::fill (openGestures.begin(), openGestures.end(), 0);
    openTotal = 0;
    stepOpen = false;
    touched.clear();
    lastWasBurst = false;
    lastIndices.clear();
}

void UndoRecorder::open()
{
    if (stepOpen || undoManager.isPerformingUndoRedo() || ! juce::MessageManager::existsAndIsCurrentThread())
        return;

    stepOpen = true;
    stepIsBurst = Coalesce::depth > 0;
    presetBefore = presetState::read (apvts.state);
    touched.clear();
}

void UndoRecorder::parameterGestureChanged (int index, bool starting)
{
    if (undoManager.isPerformingUndoRedo() || ! juce::MessageManager::existsAndIsCurrentThread()
          || index < 0 || index >= (int) params.size())
        return;

    auto& count = openGestures[(size_t) index];

    if (starting)
    {
        open();

        const auto known = std::any_of (touched.begin(), touched.end(), [index] (const auto& t) { return t.first == index; });

        if (! known)
            touched.emplace_back (index, params[(size_t) index]->getValue());

        ++count;
        ++openTotal;
    }
    else if (count > 0)
    {
        --count;
        --openTotal;
        closeIfDone();
    }
}

void UndoRecorder::closeIfDone()
{
    if (stepOpen && scopeDepth == 0 && openTotal == 0)
        close();
}

void UndoRecorder::close()
{
    stepOpen = false;

    std::vector<Change> changes;

    for (const auto& [index, before] : touched)
    {
        const auto after = params[(size_t) index]->getValue();

        if (std::abs (after - before) > 1.0e-6f)
            changes.push_back ({ index, before, after });
    }

    touched.clear();

    const auto presetAfter = presetState::read (apvts.state);

    if (changes.empty() && presetAfter == presetBefore)
        return;

    std::sort (changes.begin(), changes.end(), [] (const Change& a, const Change& b) { return a.index < b.index; });

    std::vector<int> indices;

    for (const auto& c : changes)
        indices.push_back (c.index);

    // Joining the previous step is just not starting a new transaction: the UndoManager then
    // asks that step's action to coalesce. It never does after an undo or a redo.
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto join = stepIsBurst && lastWasBurst && indices == lastIndices && now - lastEndMs < coalesceMs;

    if (! join)
        undoManager.beginNewTransaction();

    undoManager.perform (new Action (*this, std::move (changes), presetBefore, presetAfter, true));

    lastWasBurst = stepIsBurst;
    lastEndMs = now;
    lastIndices = std::move (indices);
}

} // namespace timaband
