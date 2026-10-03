// Headless checks of the editor's behaviour: parameter gestures, link bands, the
// crossover handles, M/S audibility, state persistence, presets, undo and redo, and the
// spectrum maths. Built twice, for the light palette and (TIMABAND_THEME_DARK) the dark one.
// Mouse events are delivered straight to the components, so no window is needed.
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <map>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ui/PresetPill.h"
#include "ui/Spectrum.h"

namespace
{
int failures = 0, checks = 0;

void check (bool ok, const juce::String& what)
{
    ++checks;

    if (! ok)
        ++failures;

    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
}

bool near (double a, double b, double tol = 1.0e-3) { return std::abs (a - b) <= tol; }

juce::Component* findByTitle (juce::Component& root, const juce::String& title)
{
    for (auto* child : root.getChildren())
    {
        if (child->getTitle() == title)
            return child;

        if (auto* found = findByTitle (*child, title))
            return found;
    }

    return nullptr;
}

juce::MouseEvent makeEvent (juce::Component& c, juce::Point<float> position, juce::Point<float> downPosition,
                            int clicks = 1, bool dragged = false, juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier)
{
    const auto now = juce::Time::getCurrentTime();
    return { juce::Desktop::getInstance().getMainMouseSource(), position, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
             &c, &c, now, downPosition, now, clicks, dragged };
}

void drag (juce::Component& c, juce::Point<float> from, juce::Point<float> to)
{
    c.mouseDown (makeEvent (c, from, from));
    c.mouseDrag (makeEvent (c, to, from, 1, true));
    c.mouseUp (makeEvent (c, to, from, 1, true));
}

// triggerClick() posts a message; let it arrive.
void click (juce::Component* c)
{
    static_cast<juce::Button*> (c)->triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
}

// Counts gesture begin/end per parameter.
struct GestureLog final : juce::AudioProcessorParameter::Listener
{
    std::map<juce::String, int> begins, ends;

    void parameterValueChanged (int, float) override {}

    void parameterGestureChanged (int index, bool starting) override
    {
        const auto id = juce::String (index);
        (starting ? begins : ends)[id]++;
    }
};

float real (TimabandProcessor& p, const juce::String& id)
{
    auto* param = p.apvts.getParameter (id);
    return param->convertFrom0to1 (param->getValue());
}

void set (TimabandProcessor& p, const juce::String& id, float value)
{
    auto* param = p.apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}


int steps (TimabandProcessor& p)
{
    return p.getUndoManager().getUndoDescriptions().size();
}

std::vector<juce::String> menuTexts (juce::PopupMenu& menu)
{
    std::vector<juce::String> out;
    juce::PopupMenu::MenuItemIterator it (menu);

    while (it.next())
        out.push_back (it.getItem().isSeparator ? juce::String ("-") : it.getItem().text);

    return out;
}

bool has (const std::vector<juce::String>& list, const juce::String& text)
{
    return std::find (list.begin(), list.end(), text) != list.end();
}

// Every recalled parameter holds the value `preset` lists for it.
bool matches (TimabandProcessor& p, const timaband::FactoryPreset& preset)
{
    for (const auto& v : preset.values)
        if (! near (real (p, v.id), v.value, 0.011))
            return false;

    return true;
}

bool recalledEqual (TimabandProcessor& a, TimabandProcessor& b)
{
    for (const auto& r : timaband::recalledParameters())
        if (! near (real (a, r.id), real (b, r.id), 1.0e-3))
            return false;

    return true;
}

// A change as the editor makes it: inside a gesture.
void touch (TimabandProcessor& p, const juce::String& id, float value)
{
    auto* param = p.apvts.getParameter (id);
    param->beginChangeGesture();
    param->setValueNotifyingHost (param->convertTo0to1 (value));
    param->endChangeGesture();
}

void presetChecks (TimabandProcessor& proc, TimabandEditor& editor, const juce::File& folder)
{
    using namespace timaband;
    auto& presets = proc.getPresets();
    const auto& factory = factoryPresets();
    const auto pill = static_cast<ui::PresetPill*> (findByTitle (editor, "Preset"));

    // Header controls.
    check (findByTitle (editor, "Choose preset") != nullptr && findByTitle (editor, "Previous preset") != nullptr
             && findByTitle (editor, "Next preset") != nullptr && pill != nullptr,
           "the header has the preset pill with its list, previous and next controls, titled");
    check (findByTitle (editor, "Undo") != nullptr && findByTitle (editor, "Redo") != nullptr, "the header has Undo and Redo buttons, titled");
    check (pill != nullptr && pill->getWidth() == 420 && pill->getHeight() == 36, "the pill is 420 x 36");

    {
        // Right of the title block, left of the history buttons.
        const auto undoX = editor.getLocalArea (findByTitle (editor, "Undo"), juce::Rectangle<int> (0, 0, 1, 1)).getX();
        const auto pillBox = editor.getLocalArea (pill, pill->getLocalBounds());
        check (pillBox.getX() > 300 && pillBox.getRight() < undoX, "the pill sits between the title block and the undo button");
    }

    // The factory list.
    check (factory.size() == 8, "there are 8 factory presets (got " + juce::String ((int) factory.size()) + ")");

    auto allInRange = true;

    for (const auto& preset : factory)
    {
        check (preset.values.size() == recalledParameters().size(), preset.name + " lists a value for every recalled parameter");

        for (const auto& v : preset.values)
        {
            const auto& range = proc.apvts.getParameter (v.id)->getNormalisableRange();
            allInRange = allInRange && v.value >= range.start && v.value <= range.end;
        }
    }

    check (allInRange, "every factory value is inside its parameter's range");

    presets.rescan();
    auto menu = editor.createPresetMenu();
    auto texts = menuTexts (menu);
    auto listsFactory = texts.size() >= factory.size();

    for (size_t i = 0; i < factory.size() && listsFactory; ++i)
        listsFactory = texts[i] == factory[i].name;

    check (listsFactory, "the preset menu lists the factory presets in order");
    check (has (texts, "Save") && has (texts, "Save As...") && has (texts, "Show Presets Folder"), "the menu has Save, Save As... and Show Presets Folder");
    check (! has (texts, "Delete"), "no Delete while a factory preset is loaded");
    check (std::count (texts.begin(), texts.end(), juce::String ("-")) == 1, "one separator: no user presets yet");

    // Loading recalls the band parameters and crossovers, and leaves the monitoring and level settings alone.
    set (proc, pid::bypass, 1.0f);
    set (proc, pid::delta, 1.0f);
    set (proc, pid::inputGain, 3.5f);
    set (proc, pid::outputGain, -2.5f);
    set (proc, pid::mute[0], 1.0f);
    set (proc, pid::solo[2], 1.0f);
    set (proc, pid::bandBypass[1], 1.0f);
    set (proc, pid::lookahead, 0.0f);
    set (proc, pid::stereoLink, 0.0f);

    check (presets.loadFactory (2), "loading Punchy Kit works");
    check (matches (proc, factory[2]), "Punchy Kit sets every recalled parameter (bands, curves, mute, solo, bypass, crossovers, lookahead, link)");
    check (real (proc, pid::bypass) > 0.5f && real (proc, pid::delta) > 0.5f && near (real (proc, pid::inputGain), 3.5, 1.0e-3)
             && near (real (proc, pid::outputGain), -2.5, 1.0e-3),
           "global bypass, delta and both gains are left alone");
    check (presets.getName() == "Punchy Kit" && presets.isFactory(), "the state remembers the preset name");
    set (proc, pid::bypass, 0.0f);
    set (proc, pid::delta, 0.0f);
    set (proc, pid::inputGain, 0.0f);
    set (proc, pid::outputGain, 0.0f);

    // Gestures: one per changed parameter.
    {
        GestureLog log;

        for (auto* param : proc.getParameters())
            param->addListener (&log);

        presets.loadFactory (3);

        auto balanced = true, monitoringQuiet = true;

        for (const auto& [id, n] : log.begins)
            balanced = balanced && n == 1 && log.ends[id] == 1;

        for (const auto& id : { pid::bypass, pid::delta, pid::inputGain, pid::outputGain })
        {
            const auto index = juce::String (dynamic_cast<juce::AudioProcessorParameterWithID*> (proc.apvts.getParameter (id))->getParameterIndex());
            monitoringQuiet = monitoringQuiet && log.begins.count (index) == 0;
        }

        for (auto* param : proc.getParameters())
            param->removeListener (&log);

        check (! log.begins.empty() && balanced && log.begins == log.ends, "a preset load wraps each parameter it moves in one begin/end gesture");
        check (monitoringQuiet, "a preset load does not touch the global parameters, not even with a gesture");
    }

    // Previous and next wrap around.
    presets.loadFactory (0);
    click (findByTitle (editor, "Previous preset"));
    check (presets.getName() == factory.back().name, "Previous from the first preset goes to the last (" + presets.getName() + ")");
    click (findByTitle (editor, "Next preset"));
    check (presets.getName() == factory.front().name, "Next from the last preset wraps to the first (" + presets.getName() + ")");
    click (findByTitle (editor, "Next preset"));
    check (presets.getName() == factory[1].name && matches (proc, factory[1]), "Next loads the following preset's values");

    // Modified marker.
    presets.loadFactory (2);
    editor.tick (0.03f);
    check (pill->getPresetName() == "Punchy Kit" && ! pill->isModified(), "the pill shows the loaded preset, not modified");
    touch (proc, pid::attack[1], 1.0f);
    editor.tick (0.03f);
    check (pill->isModified() && ! presets.getName().endsWith ("*"), "a change shows the modified marker");
    presets.loadFactory (2);
    editor.tick (0.03f);
    check (! pill->isModified(), "loading the preset again clears it");
    set (proc, pid::xover1, 120.3f);
    check (! presets.isDirty(), "a crossover within 0.5 Hz does not count as modified");
    set (proc, pid::xover1, 121.5f);
    check (presets.isDirty(), "a crossover 1.5 Hz away does");
    set (proc, pid::xover1, 120.0f);
    set (proc, pid::inputGain, 6.0f);
    check (! presets.isDirty(), "a changed gain does not count: gains are not part of a preset");
    set (proc, pid::inputGain, 0.0f);
    set (proc, pid::bandBypass[2], 1.0f);
    check (presets.isDirty(), "a band bypass does");
    set (proc, pid::bandBypass[2], 0.0f);

    // User presets: save, list, load, overwrite, delete.
    check (! folder.exists() || folder.getNumberOfChildFiles (juce::File::findFiles) == 0, "the test's preset folder starts empty");
    presets.loadFactory (4);
    set (proc, pid::attack[2], 4.0f);
    check (presets.saveAs ("My: Test/Preset?") == PresetManager::SaveResult::saved, "Save As writes a user preset");
    check (presets.getName() == "My TestPreset" && ! presets.isFactory(), "the name is sanitised (no path separators or invalid characters)");
    check (folder.getChildFile ("My TestPreset.timabandpreset").existsAsFile(), "the file is <name>.timabandpreset in the (test) folder");
    check (! presets.isDirty() && presets.canDelete(), "a just-saved preset is not modified, and can be deleted");

    {
        const auto xml = juce::XmlDocument::parse (folder.getChildFile ("My TestPreset.timabandpreset"));
        auto params = 0, globals = 0;

        if (xml != nullptr)
            for (const auto* child : xml->getChildWithTagNameIterator ("PARAM"))
            {
                ++params;
                const auto id = child->getStringAttribute ("id");
                globals += (id == pid::bypass || id == pid::delta || id == pid::inputGain || id == pid::outputGain) ? 1 : 0;
            }

        check (xml != nullptr && xml->hasTagName (proc.apvts.state.getType().toString()) && params == (int) recalledParameters().size() && globals == 0,
               "the file is the state's XML with the global parameters left out (" + juce::String (params) + " PARAMs)");
    }

    check (presets.saveAs ("init") == PresetManager::SaveResult::factoryName, "Save As refuses a factory preset's name (any case)");
    check (presets.saveAs ("  /: ") == PresetManager::SaveResult::invalidName, "Save As refuses a name with nothing left after sanitising");
    check (presets.getName() == "My TestPreset", "a refused save leaves the preset name alone");
    check (PresetManager::sanitiseName ("..\\hidden/../x") == "hidden..x", "sanitising strips separators and leading dots (got " + PresetManager::sanitiseName ("..\\hidden/../x") + ")");

    // Another file dropped into the folder shows up when the menu opens.
    folder.getChildFile ("My TestPreset.timabandpreset").copyFileTo (folder.getChildFile ("Dropped In.timabandpreset"));
    menu = editor.createPresetMenu();
    texts = menuTexts (menu);
    check (has (texts, "My TestPreset") && has (texts, "Dropped In"), "the menu lists the user presets, including files added from outside");
    check (std::count (texts.begin(), texts.end(), juce::String ("-")) == 2 && has (texts, "Delete"), "a separator before the user presets, and Delete for a loaded user preset");

    presets.loadFactory (1);
    set (proc, pid::attack[2], -3.0f);
    check (presets.loadUser ("My TestPreset"), "loading the user preset works");
    check (near (real (proc, pid::attack[2]), 4.0, 0.011) && matches (proc, factory[4]) == false && presets.getName() == "My TestPreset" && ! presets.isFactory() && ! presets.isDirty(),
           "its values come back, with its name");

    // Save overwrites the loaded user preset.
    set (proc, pid::attack[2], 7.0f);
    check (presets.isDirty(), "an edited user preset is modified");
    check (presets.save() == PresetManager::SaveResult::saved && ! presets.isDirty(), "Save overwrites the loaded user preset");
    presets.loadFactory (1);
    presets.loadUser ("My TestPreset");
    check (near (real (proc, pid::attack[2]), 7.0, 0.011), "the overwritten file holds the new value");

    // Prev/next walk through factory then user presets.
    presets.rescan();
    presets.loadFactory ((int) factory.size() - 1);
    presets.step (1);
    check (! presets.isFactory(), "Next from the last factory preset goes to the first user preset (" + presets.getName() + ")");
    presets.step (-1);
    check (presets.isFactory() && presets.getName() == factory.back().name, "and Previous goes back");

    // The name round-trips through the session state.
    presets.loadUser ("My TestPreset");
    juce::MemoryBlock block;
    proc.getStateInformation (block);
    {
        TimabandProcessor other;
        other.setStateInformation (block.getData(), (int) block.getSize());
        check (other.getPresets().getName() == "My TestPreset" && ! other.getPresets().isFactory() && ! other.getPresets().isDirty(),
               "the preset name and its user flag survive getStateInformation / setStateInformation");
        check (recalledEqual (proc, other), "and so do the parameters");
    }

    presets.loadFactory (6);
    proc.getStateInformation (block);
    {
        TimabandProcessor other;
        other.setStateInformation (block.getData(), (int) block.getSize());
        check (other.getPresets().getName() == "Room Tamer" && other.getPresets().isFactory() && other.getCurrentProgram() == 6,
               "a factory preset name survives too, and sets the program index");
    }

    // Delete.
    presets.loadUser ("My TestPreset");
    check (presets.remove() && ! folder.getChildFile ("My TestPreset.timabandpreset").existsAsFile(), "Delete removes the file");
    check (presets.getName() == "Init" && presets.isFactory(), "and the preset falls back to Init");
    presets.loadUser ("Dropped In");
    presets.remove();
    check (! presets.loadUser ("Dropped In") && ! presets.remove(), "a missing user preset does not load, and there is nothing to delete");

    // Programs.
    check (proc.getNumPrograms() == (int) factory.size(), "the host sees the factory presets as programs");

    auto programNames = true;

    for (int i = 0; i < proc.getNumPrograms(); ++i)
        programNames = programNames && proc.getProgramName (i) == factory[(size_t) i].name;

    check (programNames && proc.getProgramName (99).isEmpty(), "getProgramName follows the factory list");

    set (proc, pid::inputGain, 2.0f);
    proc.setCurrentProgram (4);
    check (proc.getCurrentProgram() == 4 && presets.getName() == "Tight Low End" && matches (proc, factory[4]), "setCurrentProgram loads the preset like the UI does");
    check (near (real (proc, pid::inputGain), 2.0, 1.0e-3), "and leaves the input gain alone");
    set (proc, pid::inputGain, 0.0f);
    proc.setCurrentProgram (-1);
    proc.setCurrentProgram (100);
    check (proc.getCurrentProgram() == 4, "an invalid program index is ignored");
    presets.step (1);
    check (proc.getCurrentProgram() == 5, "the program index follows a preset loaded in the editor");
}

void undoChecks (TimabandProcessor& proc, TimabandEditor& editor)
{
    using namespace timaband;
    auto& um = proc.getUndoManager();
    auto& presets = proc.getPresets();
    const auto& factory = factoryPresets();

    const auto reset = [&]
    {
        proc.getUndoRecorder().clear();
        editor.tick (0.03f);
    };

    auto* attack = static_cast<ui::BipolarSlider*> (findByTitle (editor, "Mid band attack"));
    auto* undoButton = findByTitle (editor, "Undo");
    auto* redoButton = findByTitle (editor, "Redo");

    presets.loadFactory (1);
    set (proc, pid::attack[1], 0.0f);
    reset();
    check (! um.canUndo() && ! undoButton->isEnabled() && ! redoButton->isEnabled(), "with nothing to undo, Undo and Redo are disabled");

    // Host automation: changes without a gesture never reach the history.
    for (int i = 0; i < 20; ++i)
    {
        set (proc, pid::attack[1], (float) i * 0.5f);
        set (proc, pid::xover1, 150.0f + (float) i);
        set (proc, pid::mute[0], (float) (i % 2));
    }

    editor.tick (0.03f);
    check (! um.canUndo() && steps (proc) == 0 && ! undoButton->isEnabled(), "automation without a gesture creates no undo steps, and Undo stays dark");
    set (proc, pid::mute[0], 0.0f);
    set (proc, pid::xover1, 180.0f);
    set (proc, pid::attack[1], 0.0f);

    // A slider drag.
    const auto thumb = attack->getThumbBounds().getCentre();
    drag (*attack, thumb, thumb.translated (0.0f, -12.0f));
    editor.tick (0.03f);
    check (steps (proc) == 1 && undoButton->isEnabled() && ! redoButton->isEnabled(), "a slider drag is one undo step, and lights Undo");
    const auto dragged = real (proc, pid::attack[1]);
    check (near (dragged, 3.0, 0.11), "(the drag moved it to +3 dB)");

    // Automation while there is something to redo leaves the redo list alone.
    check (um.undo(), "undo works");
    check (near (real (proc, pid::attack[1]), 0.0, 1.0e-3), "undo restores the slider's parameter");
    check (near (attack->getValue(), 0.0, 1.0e-3), "and the slider follows");
    editor.tick (0.03f);
    check (! undoButton->isEnabled() && redoButton->isEnabled(), "Undo goes dark and Redo lights");

    set (proc, pid::attack[0], 5.0f);
    set (proc, pid::attack[0], 6.0f);
    check (um.canRedo() && ! um.canUndo(), "host automation after an undo does not clear the redo list or add a step");
    set (proc, pid::attack[0], 0.0f);

    check (um.redo() && near (real (proc, pid::attack[1]), dragged, 1.0e-3), "redo reapplies the drag");

    // A new action clears the redo list.
    um.undo();
    touch (proc, pid::attack[2], 2.0f);
    check (! um.canRedo() && steps (proc) == 1, "a new edit clears the redo list");
    touch (proc, pid::attack[2], 0.0f);
    reset();

    // A preset load is one step, name and dirty marker included.
    presets.loadFactory (0);
    reset();
    touch (proc, pid::attack[0], 1.0f);                  // modified Default
    check (steps (proc) == 1, "a single gesture on a slider is one step");
    presets.loadFactory (2);
    check (steps (proc) == 2, "a preset load is exactly one step");
    check (matches (proc, factory[2]) && presets.getName() == "Punchy Kit", "(Punchy Kit is loaded)");
    um.undo();
    editor.tick (0.03f);
    check (presets.getName() == "Default" && presets.isFactory() && near (real (proc, pid::attack[0]), 1.0, 1.0e-3)
             && near (real (proc, pid::sustain[0]), -2.0, 1.0e-3) && near (real (proc, pid::xover1), 180.0, 0.6),
           "undoing a preset load restores every parameter and the preset name");
    check (static_cast<ui::PresetPill*> (findByTitle (editor, "Preset"))->getPresetName() == "Default" && static_cast<ui::PresetPill*> (findByTitle (editor, "Preset"))->isModified(),
           "and the pill shows Default with its modified marker (the tweak is still there)");
    um.undo();
    editor.tick (0.03f);
    check (near (real (proc, pid::attack[0]), 4.5, 1.0e-3) && ! static_cast<ui::PresetPill*> (findByTitle (editor, "Preset"))->isModified(),
           "undoing the tweak clears the marker");
    um.redo();
    um.redo();
    editor.tick (0.03f);
    check (presets.getName() == "Punchy Kit" && matches (proc, factory[2]) && ! presets.isDirty(), "redo brings the preset, its name and a clean state back");

    // A preset load that changes nothing but the name is still a step.
    presets.loadFactory (2);
    reset();
    presets.loadFactory (1);
    set (proc, pid::attack[0], 0.0f);
    reset();
    presets.loadFactory (1);
    check (steps (proc) == 0, "loading the preset that is already loaded changes nothing and records nothing");

    // Reset button.
    for (int b = 0; b < 3; ++b)
    {
        set (proc, pid::attack[b], 3.0f + (float) b);
        set (proc, pid::sustain[b], -4.0f - (float) b);
    }

    reset();
    click (findByTitle (editor, "Reset attack and sustain"));
    check (steps (proc) == 1, "the Reset button is one undo step (got " + juce::String (steps (proc)) + ")");
    um.undo();
    check (near (real (proc, pid::attack[2]), 5.0, 1.0e-3) && near (real (proc, pid::sustain[0]), -4.0, 1.0e-3) && near (real (proc, pid::sustain[2]), -6.0, 1.0e-3)
             && near (real (proc, pid::attack[0]), 3.0, 1.0e-3),
           "one undo brings all six values back");
    for (int b = 0; b < 3; ++b)
    {
        set (proc, pid::attack[b], 0.0f);
        set (proc, pid::sustain[b], 0.0f);
    }

    // Link bands: three parameters, one step. Turning Link on is UI state and not a step.
    reset();
    set (proc, pid::attack[0], 2.0f);
    set (proc, pid::attack[1], 0.0f);
    set (proc, pid::attack[2], 4.0f);
    auto* linkButton = findByTitle (editor, "Link bands");
    click (linkButton);
    check (steps (proc) == 0, "turning Link bands on is not an undo step");
    {
        const auto t = attack->getThumbBounds().getCentre();
        drag (*attack, t, t.translated (0.0f, -20.0f));
    }
    check (near (real (proc, pid::attack[0]), 7.0, 0.11) && near (real (proc, pid::attack[2]), 9.0, 0.11), "(the linked drag moved all three)");
    check (steps (proc) == 1, "a Link bands drag that moves three parameters is one step (got " + juce::String (steps (proc)) + ")");
    um.undo();
    check (near (real (proc, pid::attack[0]), 2.0, 1.0e-3) && near (real (proc, pid::attack[1]), 0.0, 1.0e-3) && near (real (proc, pid::attack[2]), 4.0, 1.0e-3),
           "and one undo puts all three back");
    click (linkButton);
    set (proc, pid::attack[0], 0.0f);
    set (proc, pid::attack[2], 0.0f);

    // Wheel and key bursts coalesce.
    reset();
    juce::MouseWheelDetails wheel;
    wheel.deltaX = 0.0f;
    wheel.deltaY = 0.1f;
    wheel.isReversed = false;
    wheel.isSmooth = false;
    wheel.isInertial = false;
    const auto grab = attack->getThumbBounds().getCentre();
    const auto spin = [&]
    {
        juce::Thread::sleep (4);       // the slider ignores two wheel events with the same time stamp
        attack->mouseWheelMove (makeEvent (*attack, grab, grab, 0, false, juce::ModifierKeys()), wheel);
    };

    spin();
    spin();
    spin();
    check (real (proc, pid::attack[1]) > 0.5f && steps (proc) == 1, "three wheel ticks in a row are one step (got " + juce::String (steps (proc)) + ")");
    juce::Thread::sleep (850);
    spin();
    check (steps (proc) == 2, "a wheel tick 800 ms after the last starts a new step (got " + juce::String (steps (proc)) + ")");
    um.undo();
    check (steps (proc) == 1 && real (proc, pid::attack[1]) > 0.5f, "undo takes back the last burst only");
    um.undo();
    check (near (real (proc, pid::attack[1]), 0.0, 1.0e-3), "and the next undo takes back the whole first burst");

    reset();
    attack->keyPressed (juce::KeyPress (juce::KeyPress::upKey));
    attack->keyPressed (juce::KeyPress (juce::KeyPress::upKey));
    attack->keyPressed (juce::KeyPress (juce::KeyPress::upKey));
    check (steps (proc) == 1 && near (real (proc, pid::attack[1]), 1.5, 1.0e-3), "three arrow key presses in a row are one step");
    attack->keyPressed (juce::KeyPress (juce::KeyPress::downKey));
    check (steps (proc) == 1, "and so is a fourth in the other direction");
    um.undo();
    check (near (real (proc, pid::attack[1]), 0.0, 1.0e-3), "(undone in one go)");

    // A wheel burst on one parameter and then another make two steps.
    reset();
    spin();
    {
        auto* sustain = static_cast<ui::BipolarSlider*> (findByTitle (editor, "High band sustain"));
        juce::Thread::sleep (4);
        sustain->mouseWheelMove (makeEvent (*sustain, grab, grab, 0, false, juce::ModifierKeys()), wheel);
    }
    check (steps (proc) == 2, "wheel ticks on different parameters are separate steps");
    um.undo();
    um.undo();
    set (proc, pid::sustain[2], 0.0f);

    // Drags never coalesce, however close they are.
    reset();
    drag (*attack, grab, grab.translated (0.0f, -8.0f));
    drag (*attack, attack->getThumbBounds().getCentre(), attack->getThumbBounds().getCentre().translated (0.0f, -8.0f));
    check (steps (proc) == 2, "two quick drags are two steps");
    set (proc, pid::attack[1], 0.0f);

    // Crossover handle, button, curve button and fader.
    reset();
    auto* h1 = findByTitle (editor, "Low and mid crossover frequency");
    {
        const auto c = juce::Point<float> (h1->getWidth() * 0.5f, 20.0f);
        drag (*h1, c, c.translated (60.0f, 0.0f));
    }
    check (steps (proc) == 1 && real (proc, pid::xover1) > 200.0f, "a crossover handle drag is one step");
    auto* display = static_cast<ui::CrossoverDisplay*> (findByTitle (editor, "Crossover display"));
    um.undo();
    check (near (real (proc, pid::xover1), 180.0, 0.6) && near (display->getCrossoverHz (0), 180.0, 0.6),
           "undo moves the handle back (the display follows the parameter)");
    um.redo();
    check (display->getCrossoverHz (0) > 200.0f, "and redo forward again");
    set (proc, pid::xover1, 180.0f);

    reset();
    click (findByTitle (editor, "Mute high band"));
    check (steps (proc) == 1 && real (proc, pid::mute[2]) > 0.5f, "a button click is one step");
    check (near (findByTitle (editor, "High band")->getAlpha(), 0.35, 0.01), "(the column dims)");
    um.undo();
    check (real (proc, pid::mute[2]) < 0.5f && near (findByTitle (editor, "High band")->getAlpha(), 1.0, 0.01), "undo unmutes, and the column follows");

    reset();
    click (findByTitle (editor, "Solo mid band"));
    click (findByTitle (editor, "Low band bypass"));
    click (findByTitle (editor, "Bypass"));
    check (steps (proc) == 3, "each of Solo, a band's power button and Bypass is a step of its own");
    um.undo();
    um.undo();
    um.undo();
    check (real (proc, pid::solo[1]) < 0.5f && real (proc, pid::bandBypass[0]) < 0.5f && real (proc, pid::bypass) < 0.5f
             && near (findByTitle (editor, "Controls")->getAlpha(), 1.0, 0.01) && near (findByTitle (editor, "Low band")->getAlpha(), 1.0, 0.01),
           "all three undo, and the editor follows");

    reset();
    {
        auto* curveSelector = static_cast<ui::CurveSelector*> (findByTitle (editor, "Mid band detector curve"));
        set (proc, pid::curve[1], 1.0f);
        reset();
        click (findByTitle (editor, "Mid curve: Logarithmic"));
        check (steps (proc) == 1 && curveSelector->getSelected() == 2, "a curve button click is one step");
        um.undo();
        check (near (real (proc, pid::curve[1]), 1.0, 1.0e-3) && curveSelector->getSelected() == 1
                 && static_cast<juce::Button*> (findByTitle (editor, "Mid curve: Linear"))->getToggleState(),
               "undo lights the previous curve button");
    }

    reset();
    {
        auto* fader = static_cast<ui::FaderSlider*> (findByTitle (editor, "Output gain"));
        const auto g0 = fader->getThumbBounds().getCentre();
        drag (*fader, g0, g0.translated (0.0f, -33.4f));
        check (steps (proc) == 1 && real (proc, pid::outputGain) > 3.0f, "a fader drag is one step");
        um.undo();
        check (near (real (proc, pid::outputGain), 0.0, 1.0e-3), "and undoes");
    }

    // UI-only state is not undoable.
    reset();
    click (findByTitle (editor, "Pre"));
    click (findByTitle (editor, "Post"));
    findByTitle (editor, "High band")->mouseDown (makeEvent (*findByTitle (editor, "High band"), { 22.0f, 40.0f }, { 22.0f, 40.0f }));
    editor.setSize (1344, 768);
    editor.setSize (1120, 640);
    check (steps (proc) == 0, "analyzer Pre/Post, the selected band and the editor scale are not undoable");

    // Keyboard.
    set (proc, pid::attack[1], 0.0f);
    reset();
    touch (proc, pid::attack[1], 6.0f);
    check (editor.keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)) && near (real (proc, pid::attack[1]), 0.0, 1.0e-3), "Cmd/Ctrl+Z undoes");
    check (editor.keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)) && near (real (proc, pid::attack[1]), 6.0, 1.0e-3), "Shift+Cmd/Ctrl+Z redoes");
    editor.keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0));
   #if ! JUCE_MAC
    check (editor.keyPressed (juce::KeyPress ('y', juce::ModifierKeys::commandModifier, 0)) && near (real (proc, pid::attack[1]), 6.0, 1.0e-3), "Ctrl+Y redoes");
   #endif
    check (! editor.keyPressed (juce::KeyPress ('q', juce::ModifierKeys::commandModifier, 0)), "other shortcuts are left alone");

    // The buttons.
    reset();
    touch (proc, pid::attack[1], 2.0f);
    editor.tick (0.03f);
    click (findByTitle (editor, "Undo"));
    check (near (real (proc, pid::attack[1]), 0.0, 1.0e-3) && ! undoButton->isEnabled() && redoButton->isEnabled(), "the Undo button undoes, then goes dark");
    click (findByTitle (editor, "Redo"));
    check (near (real (proc, pid::attack[1]), 2.0, 1.0e-3) && undoButton->isEnabled() && ! redoButton->isEnabled(), "the Redo button redoes");
    set (proc, pid::attack[1], 0.0f);

    // An undo while a gesture is open is ignored (it would be recorded on top).
    reset();
    {
        auto* param = proc.apvts.getParameter (pid::attack[0]);
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 (3.0f));
        check (! editor.undo(), "undo is ignored while a gesture is open");
        param->endChangeGesture();
        check (steps (proc) == 1 && near (real (proc, pid::attack[0]), 3.0, 1.0e-3), "(and the gesture still becomes a step)");
        set (proc, pid::attack[0], 0.0f);
    }

    // Setting the session state from the host clears the history.
    reset();
    touch (proc, pid::attack[1], 2.0f);
    touch (proc, pid::attack[1], 3.0f);
    um.undo();
    juce::MemoryBlock block;
    proc.getStateInformation (block);
    check (um.canUndo() || um.canRedo(), "(there is history)");
    proc.setStateInformation (block.getData(), (int) block.getSize());
    editor.tick (0.03f);
    check (! um.canUndo() && ! um.canRedo() && ! undoButton->isEnabled() && ! redoButton->isEnabled(), "setStateInformation clears the undo history");
    check (steps (proc) == 0, "(no steps are left)");
}

void spectrumChecks()
{
    using timaband::ui::SpectrumAnalyzer;

    const auto sr = 48000.0;
    const auto binHz = sr / (double) SpectrumAnalyzer::fftSize;

    const auto peakOf = [&] (double hz, float amplitude)
    {
        SpectrumAnalyzer analyzer;
        timaband::SampleFifo fifo;
        analyzer.setSampleRate (sr);

        // Four ticks of 1/30 s of a continuous sine, enough to settle the rise smoothing.
        const auto perTick = (size_t) (sr / 30.0);
        std::vector<float> samples (perTick);
        size_t n = 0;

        for (int tick = 0; tick < 12; ++tick)
        {
            for (auto& x : samples)
                x = amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * hz * (double) n++ / sr);

            fifo.push (samples.data(), (int) samples.size());
            analyzer.update (fifo, 1.0f / 30.0f);
        }

        const auto& pts = analyzer.getPoints();
        const auto it = std::max_element (pts.begin(), pts.end());
        return std::pair<float, double> (*it, SpectrumAnalyzer::pointFrequency ((int) (it - pts.begin())));
    };

    // A full-scale sine centred on a bin reads 0 dBFS (no tilt at 1 kHz).
    const auto onePk = peakOf (85.0 * binHz, 1.0f);
    check (near (onePk.first, 0.0, 0.6), "full-scale sine at ~1 kHz reads 0 dBFS (got " + juce::String (onePk.first, 2) + ")");
    check (std::abs (onePk.second - 85.0 * binHz) < 40.0, "its peak sits at the right frequency (" + juce::String (onePk.second, 0) + " Hz)");

    const auto half = peakOf (85.0 * binHz, 0.5f);
    check (near (half.first, -6.02, 0.6), "half-scale sine reads -6 dBFS (got " + juce::String (half.first, 2) + ")");

    // The tilt: +4.5 dB per octave about 1 kHz.
    const auto hf = peakOf (853.0 * binHz, 1.0f);     // ~10 kHz
    const auto expected = 4.5 * std::log2 (853.0 * binHz / 1000.0);
    check (near (hf.first, expected, 1.5), "tilt at ~10 kHz is +" + juce::String (expected, 1) + " dB (got " + juce::String (hf.first, 2) + ")");

    // Silence decays at a fixed rate: 100 ms later, 4 dB down.
    SpectrumAnalyzer analyzer;
    timaband::SampleFifo fifo;
    analyzer.setSampleRate (sr);
    std::vector<float> loud (8192);
    for (size_t i = 0; i < loud.size(); ++i)
        loud[i] = (float) std::sin (juce::MathConstants<double>::twoPi * 85.0 * binHz * (double) i / sr);
    fifo.push (loud.data(), (int) loud.size());
    analyzer.update (fifo, 1.0f / 30.0f);
    analyzer.update (fifo, 1.0f / 30.0f);
    const auto before = *std::max_element (analyzer.getPoints().begin(), analyzer.getPoints().end());

    std::vector<float> silence (8192, 0.0f);
    fifo.push (silence.data(), (int) silence.size());
    analyzer.update (fifo, 0.1f);
    const auto after = *std::max_element (analyzer.getPoints().begin(), analyzer.getPoints().end());
    check (near (before - after, 4.0, 0.5), "bins fall at 40 dB/s (fell " + juce::String (before - after, 2) + " dB in 100 ms)");
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    spectrumChecks();

    // The presets these checks save go to a temporary folder, never the real one.
    const auto presetFolder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("timaband-ui-check-presets");
    presetFolder.deleteRecursively();
    timaband::PresetManager::setUserFolder (presetFolder);

    TimabandProcessor proc;
    proc.setRateAndBufferSizeDetails (48000.0, 512);
    proc.prepareToPlay (48000.0, 512);

    GestureLog log;
    for (auto* param : proc.getParameters())
        param->addListener (&log);

    const auto indexOf = [&proc] (const juce::String& id)
    {
        return juce::String (dynamic_cast<juce::AudioProcessorParameterWithID*> (proc.apvts.getParameter (id))->getParameterIndex());
    };

    std::unique_ptr<TimabandEditor> editor (static_cast<TimabandEditor*> (proc.createEditor()));
    editor->setSize (1120, 640);
    editor->tick (1.0f / 30.0f);

    // Size and persistence.
    editor->setSize (672, 384);
    check (near ((double) proc.apvts.state.getProperty (timaband::uiState::editorScale), 0.6, 1.0e-3), "scale 0.6 is stored in the state tree");
    editor->setSize (1680, 960);
    check (near ((double) proc.apvts.state.getProperty (timaband::uiState::editorScale), 1.5, 1.0e-3), "scale 1.5 is stored in the state tree");
    editor->setSize (1120, 640);

    {
        // A second editor opens at the remembered scale.
        proc.apvts.state.setProperty (timaband::uiState::editorScale, 1.25, nullptr);
        std::unique_ptr<TimabandEditor> second (static_cast<TimabandEditor*> (proc.createEditor()));
        check (second->getWidth() == 1400 && second->getHeight() == 800, "a new editor opens at the stored scale (1400 x 800, got " + juce::String (second->getWidth()) + " x " + juce::String (second->getHeight()) + ")");
        proc.apvts.state.setProperty (timaband::uiState::editorScale, 1.0, nullptr);
    }

    auto* attack = static_cast<timaband::ui::BipolarSlider*> (findByTitle (*editor, "Mid band attack"));
    check (attack != nullptr, "found the mid attack slider");

    if (attack == nullptr)
        return 1;

    // Slider: a drag from the thumb is relative, 120 px is the full 30 dB, and it is one gesture.
    set (proc, timaband::pid::attack[1], 0.0f);
    const auto thumb = attack->getThumbBounds().getCentre();
    log.begins.clear(); log.ends.clear();
    drag (*attack, thumb, thumb.translated (0.0f, -12.0f));
    check (near (real (proc, timaband::pid::attack[1]), 3.0, 0.11), "dragging the thumb up 12 px adds 3 dB (got " + juce::String (real (proc, timaband::pid::attack[1]), 2) + ")");
    check (log.begins[indexOf (timaband::pid::attack[1])] == 1 && log.ends[indexOf (timaband::pid::attack[1])] == 1, "the drag is one begin/end gesture");

    // A press on the track jumps there.
    const auto top = juce::Point<float> ((float) attack->getWidth() * 0.5f, 11.0f);
    attack->mouseDown (makeEvent (*attack, top, top));
    attack->mouseUp (makeEvent (*attack, top, top));
    check (near (real (proc, timaband::pid::attack[1]), 15.0, 0.11), "a click at the top of the track jumps to +15 dB");

    // Double-click resets.
    attack->mouseDoubleClick (makeEvent (*attack, thumb, thumb, 2));
    check (near (real (proc, timaband::pid::attack[1]), 0.0, 1.0e-4), "double-click returns the slider to 0 dB");

    // Keys.
    attack->keyPressed (juce::KeyPress (juce::KeyPress::upKey));
    check (near (real (proc, timaband::pid::attack[1]), 0.5, 1.0e-3), "Up arrow adds 0.5 dB");
    attack->keyPressed (juce::KeyPress (juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier, 0));
    check (near (real (proc, timaband::pid::attack[1]), 0.4, 1.0e-3), "Shift+Down subtracts 0.1 dB");
    attack->keyPressed (juce::KeyPress (juce::KeyPress::endKey));
    check (near (real (proc, timaband::pid::attack[1]), 15.0, 1.0e-3), "End goes to +15 dB");
    attack->keyPressed (juce::KeyPress (juce::KeyPress::homeKey));
    check (near (real (proc, timaband::pid::attack[1]), -15.0, 1.0e-3), "Home goes to -15 dB");

    // Reset button: all six to zero, each its own gesture.
    for (int b = 0; b < 3; ++b)
    {
        set (proc, timaband::pid::attack[b], 3.0f + (float) b);
        set (proc, timaband::pid::sustain[b], -4.0f - (float) b);
    }

    log.begins.clear(); log.ends.clear();
    click (findByTitle (*editor, "Reset attack and sustain"));
    auto allZero = true, allGestures = true;

    for (int b = 0; b < 3; ++b)
        for (const auto& id : { timaband::pid::attack[b], timaband::pid::sustain[b] })
        {
            allZero = allZero && near (real (proc, id), 0.0, 1.0e-4);
            allGestures = allGestures && log.begins[indexOf (id)] == 1 && log.ends[indexOf (id)] == 1;
        }

    check (allZero, "Reset sets all six attack/sustain values to 0");
    check (allGestures, "Reset wraps each parameter in a begin/end gesture");

    // Link bands.
    auto* linkButton = findByTitle (*editor, "Link bands");
    set (proc, timaband::pid::attack[0], 2.0f);
    set (proc, timaband::pid::attack[1], 0.0f);
    set (proc, timaband::pid::attack[2], 14.0f);
    click (linkButton);
    check ((bool) proc.apvts.state.getProperty (timaband::uiState::linkBands), "Link bands is stored in the state tree");

    log.begins.clear(); log.ends.clear();
    const auto thumb2 = attack->getThumbBounds().getCentre();
    drag (*attack, thumb2, thumb2.translated (0.0f, -20.0f));      // +5 dB on mid
    check (near (real (proc, timaband::pid::attack[1]), 5.0, 0.11), "linked: the dragged band moves by 5 dB");
    check (near (real (proc, timaband::pid::attack[0]), 7.0, 0.11), "linked: low follows by the same 5 dB");
    check (near (real (proc, timaband::pid::attack[2]), 15.0, 0.11), "linked: high follows and clamps at +15 dB");
    check (log.begins[indexOf (timaband::pid::attack[0])] == 1 && log.ends[indexOf (timaband::pid::attack[0])] == 1
             && log.begins[indexOf (timaband::pid::attack[2])] == 1 && log.ends[indexOf (timaband::pid::attack[2])] == 1
             && log.begins[indexOf (timaband::pid::attack[1])] == 1 && log.ends[indexOf (timaband::pid::attack[1])] == 1,
           "linked: all three bands get one gesture");

    set (proc, timaband::pid::sustain[0], 0.0f);
    check (near (real (proc, timaband::pid::sustain[1]), 0.0, 1.0e-4), "linked: moving host-side does not drag the others");
    click (linkButton);
    check (! (bool) proc.apvts.state.getProperty (timaband::uiState::linkBands), "Link bands turns off again");

    drag (*attack, attack->getThumbBounds().getCentre(), attack->getThumbBounds().getCentre().translated (0.0f, 20.0f));
    check (near (real (proc, timaband::pid::attack[0]), 7.0, 0.11), "unlinked: other bands stay put");

    // Crossover handles.
    auto* h1 = findByTitle (*editor, "Low and mid crossover frequency");
    auto* h2 = findByTitle (*editor, "Mid and high crossover frequency");
    check (h1 != nullptr && h2 != nullptr, "found both crossover handles");
    set (proc, timaband::pid::xover1, 180.0f);
    set (proc, timaband::pid::xover2, 2400.0f);

    const auto centre = [] (juce::Component& c) { return juce::Point<float> (c.getWidth() * 0.5f, 20.0f); };
    log.begins.clear(); log.ends.clear();
    {
        // Drag handle 1 to the right by one octave's worth of pixels.
        const auto octavePx = 920.0f * std::log (2.0f) / std::log (1000.0f);
        drag (*h1, centre (*h1), centre (*h1).translated (octavePx, 0.0f));
        check (near (real (proc, timaband::pid::xover1), 360.0, 12.0), "dragging a handle one octave of pixels doubles the frequency (got " + juce::String (real (proc, timaband::pid::xover1), 1) + ")");
        check (log.begins[indexOf (timaband::pid::xover1)] == 1 && log.ends[indexOf (timaband::pid::xover1)] == 1, "handle drag is one gesture");

        // And far past the other handle: pinned a third of an octave short of it.
        drag (*h1, centre (*h1), centre (*h1).translated (800.0f, 0.0f));
        check (near (real (proc, timaband::pid::xover1), 2400.0 / 1.26, 2.0), "handle 1 stops 1/3 octave below handle 2 (got " + juce::String (real (proc, timaband::pid::xover1), 1) + ")");
    }

    h1->mouseDoubleClick (makeEvent (*h1, centre (*h1), centre (*h1), 2));
    check (near (real (proc, timaband::pid::xover1), 180.0, 0.5), "double-click resets handle 1 to 180 Hz");

    h2->keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
    check (near (real (proc, timaband::pid::xover2), 2400.0 * std::pow (2.0, 1.0 / 24.0), 2.0), "Right arrow moves a handle 1/24 octave");
    h2->keyPressed (juce::KeyPress (juce::KeyPress::leftKey, juce::ModifierKeys::shiftModifier, 0));
    check (near (real (proc, timaband::pid::xover2), 2400.0 * std::pow (2.0, 1.0 / 24.0 - 1.0 / 6.0), 2.0), "Shift+Left moves it 1/6 octave back");

    h1->keyPressed (juce::KeyPress (juce::KeyPress::homeKey));
    check (near (real (proc, timaband::pid::xover1), 40.0, 0.1), "Home sends handle 1 to 40 Hz");
    h2->keyPressed (juce::KeyPress (juce::KeyPress::endKey));
    check (near (real (proc, timaband::pid::xover2), 16000.0, 1.0), "End sends handle 2 to 16 kHz");
    set (proc, timaband::pid::xover1, 180.0f);
    set (proc, timaband::pid::xover2, 2400.0f);

    // Mute and solo.
    auto* bypassButton = findByTitle (*editor, "Bypass");
    auto* muteHigh = findByTitle (*editor, "Mute high band");
    auto* soloMid = findByTitle (*editor, "Solo mid band");
    click (muteHigh);
    check (real (proc, timaband::pid::mute[2]) > 0.5f, "the M button drives the mute parameter");
    click (soloMid);
    check (real (proc, timaband::pid::solo[1]) > 0.5f, "the S button drives the solo parameter");

    // Audibility shows as the columns' alpha: mid is soloed, so low and high dim.
    check (near (findByTitle (*editor, "Low band")->getAlpha(), 0.35, 0.01)
             && near (findByTitle (*editor, "Mid band")->getAlpha(), 1.0, 0.01)
             && near (findByTitle (*editor, "High band")->getAlpha(), 0.35, 0.01),
           "with mid soloed the low and high columns are drawn at 35 % alpha");

    click (soloMid);
    check (near (findByTitle (*editor, "Low band")->getAlpha(), 1.0, 0.01)
             && near (findByTitle (*editor, "High band")->getAlpha(), 0.35, 0.01),
           "with the solo off only the muted high column stays dimmed");
    click (muteHigh);

    // Bypass dims the left area.
    click (bypassButton);
    check (real (proc, timaband::pid::bypass) > 0.5f, "the Bypass button drives the bypass parameter");
    check (near (findByTitle (*editor, "Controls")->getAlpha(), 0.7, 0.01), "bypass draws the left area at 70 % alpha");

    // Curve buttons: one radio group per band, bound to the curve parameter.
    {
        using timaband::ui::CurveSelector;
        namespace colours = timaband::ui::colours;

        auto* group = static_cast<CurveSelector*> (findByTitle (*editor, "Mid band detector curve"));
        const auto button = [&editor] (const juce::String& band, int index)
        {
            return static_cast<juce::Button*> (findByTitle (*editor, band + " curve: " + timaband::curveNames[index]));
        };

        // The index of the lit button, or -1 unless exactly one is lit.
        const auto lit = [&button] (const juce::String& band)
        {
            auto found = -1, count = 0;

            for (int i = 0; i < 3; ++i)
                if (button (band, i)->getToggleState())
                {
                    found = i;
                    ++count;
                }

            return count == 1 ? found : -1;
        };

        const auto& id = timaband::pid::curve[1];
        const auto curveIndex = indexOf (id);
        const auto oneGesture = [&] { return log.begins[curveIndex] == 1 && log.ends[curveIndex] == 1; };
        const auto noGesture = [&] { return log.begins[curveIndex] == 0 && log.ends[curveIndex] == 0; };

        auto allFound = group != nullptr;

        for (const auto* band : timaband::bandNames)
            for (int i = 0; i < 3; ++i)
                allFound = allFound && button (band, i) != nullptr
                             && button (band, i)->getTooltip() == timaband::curveNames[i];

        check (allFound, "each band has Exponential, Linear and Logarithmic buttons, titled and with a tooltip");

        if (! allFound)
            return 1;

        check (group->getWantsKeyboardFocus() && ! button ("Mid", 0)->getWantsKeyboardFocus(),
               "the curve group is one Tab stop");
        check (near (group->getFocusRingBounds().getWidth(), 3 * 44 + 2 * 4, 0.5) && near (group->getFocusRingBounds().getHeight(), 30, 0.5),
               "its focus ring goes round the three buttons");

        // The parameter changing under the editor lights the matching button.
        auto followsParameter = true;

        for (int v : { 0, 2, 1, 0 })
        {
            set (proc, id, (float) v);
            followsParameter = followsParameter && lit ("Mid") == v && group->getSelected() == v;
        }

        check (followsParameter, "a host change of the curve parameter lights the matching button");

        // Clicking a button sets the parameter in one gesture, and leaves the other bands alone.
        set (proc, timaband::pid::curve[0], 1.0f);
        set (proc, timaband::pid::curve[2], 1.0f);
        log.begins.clear(); log.ends.clear();
        click (button ("Mid", 2));
        check (near (real (proc, id), 2.0, 1.0e-3) && lit ("Mid") == 2, "clicking Logarithmic sets the curve parameter and lights that button");
        check (oneGesture(), "a click is one begin/end gesture");
        check (near (real (proc, timaband::pid::curve[0]), 1.0, 1.0e-3) && near (real (proc, timaband::pid::curve[2]), 1.0, 1.0e-3)
                 && lit ("Low") == 1 && lit ("High") == 1,
               "the other bands' curves and buttons are untouched");

        log.begins.clear(); log.ends.clear();
        click (button ("Mid", 2));
        check (noGesture() && lit ("Mid") == 2, "clicking the lit button changes nothing");

        click (button ("Mid", 0));
        check (near (real (proc, id), 0.0, 1.0e-3) && lit ("Mid") == 0, "clicking Exponential sets the curve parameter and lights that button");

        // Keys move the selection, one gesture per change, and stop at the ends.
        set (proc, id, 1.0f);
        log.begins.clear(); log.ends.clear();
        auto handled = group->keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
        check (handled && near (real (proc, id), 2.0, 1.0e-3) && lit ("Mid") == 2, "Right moves the selection to Logarithmic");
        check (oneGesture(), "a key press is one begin/end gesture");

        log.begins.clear(); log.ends.clear();
        handled = group->keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
        check (handled && noGesture() && near (real (proc, id), 2.0, 1.0e-3) && lit ("Mid") == 2, "Right at the last button changes nothing");

        handled = group->keyPressed (juce::KeyPress (juce::KeyPress::leftKey));
        check (handled && near (real (proc, id), 1.0, 1.0e-3) && lit ("Mid") == 1, "Left moves the selection back to Linear");
        handled = group->keyPressed (juce::KeyPress (juce::KeyPress::upKey));
        check (handled && near (real (proc, id), 0.0, 1.0e-3) && lit ("Mid") == 0, "Up moves it back to Exponential");

        log.begins.clear(); log.ends.clear();
        handled = group->keyPressed (juce::KeyPress (juce::KeyPress::leftKey));
        check (handled && noGesture() && lit ("Mid") == 0, "Left at the first button changes nothing");

        handled = group->keyPressed (juce::KeyPress (juce::KeyPress::downKey));
        check (handled && near (real (proc, id), 1.0, 1.0e-3) && lit ("Mid") == 1, "Down moves it forward to Linear");
        check (! group->keyPressed (juce::KeyPress ('a')) && near (real (proc, id), 1.0, 1.0e-3), "other keys are left alone");

        // Looks: grey when idle, the band colour when selected, a light grey once the band is bypassed.
        // A triggered click leaves its button drawn as pressed until it has painted once
        // and a timer has run, so let that happen first.
        group->createComponentSnapshot (group->getLocalBounds());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);

        const auto fillAt = [&] (int index)
        {
            const auto image = group->createComponentSnapshot (group->getLocalBounds(), true, 1.0f);
            return image.getPixelAt (index * (44 + 4) + 5, 4 + 5);
        };

        set (proc, id, 2.0f);
        check (fillAt (2) == colours::mid && fillAt (0) == colours::tabIdle && fillAt (1) == colours::tabIdle,
               "the selected button is filled with the band colour, the others are grey");
        set (proc, id, 1.0f);
        check (fillAt (1) == colours::mid && fillAt (2) == colours::tabIdle, "its fill follows the selection");

        set (proc, timaband::pid::bandBypass[1], 1.0f);
        check (fillAt (1) == colours::tabBypassed && fillAt (0) == colours::tabIdle, "a bypassed band's selected button turns light grey");
        set (proc, timaband::pid::bandBypass[1], 0.0f);
        check (fillAt (1) == colours::mid, "and goes back to the band colour");
    }

    // UI-only state.
    auto* segmentPre = findByTitle (*editor, "Pre");
    auto* segmentPost = findByTitle (*editor, "Post");
    const auto segmentsOn = [&]
    {
        return (int) static_cast<juce::Button*> (segmentPre)->getToggleState() + (int) static_cast<juce::Button*> (segmentPost)->getToggleState();
    };

    click (segmentPre);
    check (! (bool) proc.apvts.state.getProperty (timaband::uiState::analyzerPost), "Pre selects the pre-fader analyzer");
    check (segmentsOn() == 1 && static_cast<juce::Button*> (segmentPre)->getToggleState(), "after clicking Pre only Pre is lit");

    click (segmentPost);
    check ((bool) proc.apvts.state.getProperty (timaband::uiState::analyzerPost), "Post selects the post-fader analyzer");
    check (segmentsOn() == 1 && static_cast<juce::Button*> (segmentPost)->getToggleState(), "after clicking Post only Post is lit");

    click (segmentPre);
    click (segmentPre);
    check (! (bool) proc.apvts.state.getProperty (timaband::uiState::analyzerPost) && segmentsOn() == 1, "clicking the lit segment again changes nothing");

    // Selecting a band by clicking in its column.
    // (The column listens to its children's presses as a mouse listener; here it gets one directly.)
    auto* highColumn = findByTitle (*editor, "High band");
    highColumn->mouseDown (makeEvent (*highColumn, { 22.0f, 40.0f }, { 22.0f, 40.0f }));
    check ((int) proc.apvts.state.getProperty (timaband::uiState::selectedBand) == 2, "pressing inside the high column selects the high band");

    // A press that starts on a curve button reaches the column the same way.
    auto* lowCurveButton = findByTitle (*editor, "Low curve: Linear");
    findByTitle (*editor, "Low band")->mouseDown (makeEvent (*lowCurveButton, { 22.0f, 15.0f }, { 22.0f, 15.0f }));
    check ((int) proc.apvts.state.getProperty (timaband::uiState::selectedBand) == 0, "pressing a curve button selects its band");

    // Per-band bypass: the power button drives the parameter and greys the column.
    click (findByTitle (*editor, "Low band bypass"));
    check (real (proc, timaband::pid::bandBypass[0]) > 0.5f, "the power button drives the band bypass parameter");
    check (near (findByTitle (*editor, "Low band")->getAlpha(), 0.45, 0.01), "a bypassed band's column is drawn at 45 % alpha");
    check (near (findByTitle (*editor, "Mid band")->getAlpha(), 1.0, 0.01), "bypassing one band leaves the others alone");
    set (proc, timaband::pid::mute[0], 1.0f);
    check (near (findByTitle (*editor, "Low band")->getAlpha(), 0.35 * 0.45, 0.01), "mute dimming stacks on top of the bypass greying");
    set (proc, timaband::pid::mute[0], 0.0f);
    set (proc, timaband::pid::bandBypass[0], 0.0f);
    check (near (findByTitle (*editor, "Low band")->getAlpha(), 1.0, 0.01), "a host change of the bypass parameter updates the column");

    // The input/output faders.
    for (const auto& [title, id] : { std::pair<juce::String, juce::String> { "Input gain", timaband::pid::inputGain },
                                      std::pair<juce::String, juce::String> { "Output gain", timaband::pid::outputGain } })
    {
        auto* fader = static_cast<timaband::ui::FaderSlider*> (findByTitle (*editor, title));
        check (fader != nullptr, "found the " + title.toLowerCase() + " fader");

        if (fader == nullptr)
            continue;

        set (proc, id, 0.0f);
        check (near (fader->getThumbBounds().getCentreY(), (float) fader->getHeight() * 0.5f, 1.0), title + " fader: 0 dB sits at the middle of the travel");

        set (proc, id, 24.0f);
        check (near (fader->getThumbBounds().getCentreY(), 10.0, 1.0), title + " fader: +24 dB is the top of the travel, level with the bar");
        set (proc, id, -24.0f);
        check (near (fader->getThumbBounds().getCentreY(), (double) fader->getHeight() - 10.0, 1.0), title + " fader: -24 dB is the bottom");

        set (proc, id, 0.0f);
        const auto grab = fader->getThumbBounds().getCentre();
        log.begins.clear(); log.ends.clear();
        drag (*fader, grab, grab.translated (0.0f, -33.4f));                        // a tenth of the 334 px travel
        check (near (real (proc, id), 4.8, 0.15), title + " fader: dragging up a tenth of the travel adds 4.8 dB (got " + juce::String (real (proc, id), 2) + ")");
        check (log.begins[indexOf (id)] == 1 && log.ends[indexOf (id)] == 1, title + " fader: one begin/end gesture per drag");

        fader->mouseDoubleClick (makeEvent (*fader, grab, grab, 2));
        check (near (real (proc, id), 0.0, 1.0e-4), title + " fader: double-click resets to 0 dB");

        juce::MouseWheelDetails wheel;
        wheel.deltaX = 0.0f;
        wheel.deltaY = 0.2f;
        wheel.isReversed = false;
        wheel.isSmooth = false;
        wheel.isInertial = false;
        fader->mouseWheelMove (makeEvent (*fader, grab, grab, 0, false, juce::ModifierKeys()), wheel);
        check (real (proc, id) > 0.5f, title + " fader: the mouse wheel moves it (got " + juce::String (real (proc, id), 2) + ")");
        set (proc, id, 0.0f);

        fader->keyPressed (juce::KeyPress (juce::KeyPress::upKey));
        check (near (real (proc, id), 1.0, 1.0e-3), title + " fader: Up arrow adds 1 dB");
        set (proc, id, 0.0f);
    }

    // The readouts under the bars follow, and take typed values.
    {
        auto* box = static_cast<timaband::ui::GainBox*> (findByTitle (*editor, "Input gain value"));
        set (proc, timaband::pid::inputGain, -2.0f);
        check (box->getValueLabel()->getText() == timaband::ui::theme::minusSign() + "2.0", "the readout shows a signed one-decimal value");
        set (proc, timaband::pid::inputGain, 0.0f);
        check (box->getValueLabel()->getText() == "0.0", "the readout shows 0.0 at zero");
        set (proc, timaband::pid::inputGain, 4.5f);
        check (box->getValueLabel()->getText() == "+4.5", "the readout shows a plus sign above zero");

        box->getValueLabel()->showEditor();
        check (box->getValueLabel()->getCurrentTextEditor() != nullptr, "clicking the readout opens a text editor");

        if (auto* ed = box->getValueLabel()->getCurrentTextEditor())
            ed->setText (timaband::ui::theme::minusSign() + "3.5", false);

        box->getValueLabel()->hideEditor (false);
        check (near (real (proc, timaband::pid::inputGain), -3.5, 1.0e-3), "typing a value sets the gain");

        const auto grab = juce::Point<float> (26.0f, 12.0f);
        set (proc, timaband::pid::inputGain, 0.0f);
        drag (*box, grab, grab.translated (0.0f, -25.0f));
        check (real (proc, timaband::pid::inputGain) > 2.0f, "dragging the readout up raises the gain (got " + juce::String (real (proc, timaband::pid::inputGain), 2) + ")");
        set (proc, timaband::pid::inputGain, 0.0f);
    }

    check (attack->getWantsKeyboardFocus(), "the band sliders can take keyboard focus");

    // Ticking with audio in flight must not misbehave.
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    for (int i = 0; i < 20; ++i)
    {
        for (int c = 0; c < 2; ++c)
            for (int s = 0; s < 512; ++s)
                buffer.setSample (c, s, 0.3f * std::sin (0.05f * (float) (s + i * 512)));

        proc.processBlock (buffer, midi);
        editor->tick (1.0f / 30.0f);
    }

    check (true, "ticking the editor with audio running");

    presetChecks (proc, *editor, presetFolder);
    undoChecks (proc, *editor);

    for (auto* param : proc.getParameters())
        param->removeListener (&log);

    editor.reset();
    presetFolder.deleteRecursively();
    std::printf ("\n%d of %d checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
