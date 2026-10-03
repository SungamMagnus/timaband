#pragma once

#include <atomic>
#include <functional>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"

namespace timaband
{

class UndoRecorder;

// Which preset the state is based on. Stored in the state tree, so it round-trips with the
// host's session (and with undo).
namespace presetState
{
extern const juce::Identifier presetName;         // string
extern const juce::Identifier presetIsFactory;    // bool

constexpr const char* defaultName = "Init";       // what a fresh instance (all parameters at their defaults) is

struct Id
{
    juce::String name = defaultName;
    bool factory = true;

    bool operator== (const Id& o) const noexcept { return name == o.name && factory == o.factory; }
    bool operator!= (const Id& o) const noexcept { return ! (*this == o); }
};

Id read (const juce::ValueTree& state);
void write (juce::ValueTree state, const Id&);      // never undoable: the undo history restores it itself
}

struct PresetValue
{
    juce::String id;
    float value;            // real units: dB, Hz, a curve index, or 0 / 1
};

struct FactoryPreset
{
    juce::String name;
    std::vector<PresetValue> values;
};

// The compiled-in presets, in menu order.
const std::vector<FactoryPreset>& factoryPresets();

// What a preset recalls: every band parameter, both crossovers, lookahead and stereo link.
// The global bypass, delta and the two gains are monitoring and level settings, and stay put.
struct RecalledParameter
{
    juce::String id;
    float tolerance;        // how far a value may drift before the preset counts as modified
};

const std::vector<RecalledParameter>& recalledParameters();

/**
 * The preset list (factory, then the files in the user folder), loading and saving, and
 * the "modified" test. Message thread only, except that the program index is atomic.
 *
 * A preset is applied as one undo step: every changed parameter gets its own begin/end
 * gesture inside an UndoRecorder::Scope.
 */
class PresetManager
{
public:
    struct Entry
    {
        juce::String name;
        bool factory = false;
    };

    PresetManager (juce::AudioProcessorValueTreeState&, UndoRecorder&);

    static constexpr const char* fileExtension = ".timabandpreset";

    // ~/Library/Audio/Presets/Sungam/Timaband, unless a test has pointed it elsewhere.
    static juce::File getUserFolder();
    static void setUserFolder (const juce::File& folderOverride);      // File() restores the default

    // Strips path separators and characters that are not allowed in a file name.
    static juce::String sanitiseName (const juce::String&);

    // Re-reads the user folder. Called when the menu opens and before stepping.
    void rescan();

    // The factory presets, then the user presets sorted by name.
    const std::vector<Entry>& getEntries() const noexcept   { return entries; }
    int getNumFactory() const noexcept                      { return (int) factoryPresets().size(); }

    juce::String getName() const;
    bool isFactory() const;
    int getCurrentIndex() const;            // into getEntries(), or -1
    bool isDirty() const;
    bool canDelete() const;                 // a loaded user preset whose file is still there
    juce::File getFileFor (const juce::String& name) const;

    bool load (int entryIndex);
    bool loadFactory (int index);
    bool loadUser (const juce::String& name);
    void step (int direction);              // wraps around

    enum class SaveResult { saved, invalidName, factoryName, failed };

    SaveResult save();                      // overwrites the loaded user preset
    SaveResult saveAs (const juce::String& name);
    bool remove();                          // deletes the loaded user preset's file

    // Fills in the preset properties a fresh or older state lacks, and follows a state that was set from outside.
    void syncWithState();

    // The index the host sees: the loaded factory preset, or the last one that was.
    int getProgramIndex() const noexcept    { return programIndex.load(); }
    void setProgramIndex (int index) noexcept { programIndex.store (index); }

    // Called after any preset has been loaded.
    std::function<void()> onLoaded;

private:
    struct Reference
    {
        juce::String name;
        bool factory = false;
        bool valid = false;
        std::vector<float> values;      // in recalledParameters() order
    };

    std::vector<float> valuesFor (const std::vector<PresetValue>&) const;
    bool readFile (const juce::File&, std::vector<float>& values) const;
    void apply (const std::vector<float>& values, const juce::String& name, bool factory);
    void setReference (const juce::String& name, bool factory, bool valid, std::vector<float> values);
    const Reference& getReference() const;

    juce::AudioProcessorValueTreeState& apvts;
    UndoRecorder& recorder;

    std::vector<Entry> entries;
    std::atomic<int> programIndex { 0 };
    mutable Reference reference;
    mutable bool referenceCurrent = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetManager)
};

} // namespace timaband
