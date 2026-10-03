#include "Presets.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "UndoRecorder.h"
#include "dsp/Shaper.h"

namespace timaband
{

namespace presetState
{
const juce::Identifier presetName ("presetName");
const juce::Identifier presetIsFactory ("presetIsFactory");

Id read (const juce::ValueTree& state)
{
    Id id;
    id.name = state.getProperty (presetName, juce::String (defaultName)).toString();
    id.factory = (bool) state.getProperty (presetIsFactory, true);
    return id;
}

void write (juce::ValueTree state, const Id& id)
{
    state.setProperty (presetName, id.name, nullptr);
    state.setProperty (presetIsFactory, id.factory, nullptr);
}
}

/* ── Factory presets ─────────────────────────────────────────────────────── */

namespace
{
constexpr int curveExp = (int) Curve::exponential, curveLin = (int) Curve::linear, curveLog = (int) Curve::logarithmic;

struct BandSetting
{
    float attack, sustain;
    int curve;
};

FactoryPreset make (const char* name, float xover1, float xover2, const std::array<BandSetting, numBands>& bands)
{
    FactoryPreset p { juce::String (name), {} };

    p.values.push_back ({ pid::xover1, xover1 });
    p.values.push_back ({ pid::xover2, xover2 });

    for (int b = 0; b < numBands; ++b)
    {
        const auto& band = bands[(size_t) b];

        p.values.push_back ({ pid::attack[b],  band.attack });
        p.values.push_back ({ pid::sustain[b], band.sustain });
        p.values.push_back ({ pid::curve[b],   (float) band.curve });
        p.values.push_back ({ pid::mute[b],       0.0f });
        p.values.push_back ({ pid::solo[b],       0.0f });
        p.values.push_back ({ pid::bandBypass[b], 0.0f });
    }

    p.values.push_back ({ pid::lookahead,  1.0f });
    p.values.push_back ({ pid::stereoLink, 1.0f });
    return p;
}
} // namespace

const std::vector<FactoryPreset>& factoryPresets()
{
    // Built on first use: the parameter IDs are globals in another translation unit.
    static const std::vector<FactoryPreset> presets = []
    {
        std::vector<FactoryPreset> list;

        // The first four are the editor mockup's presets.
        list.push_back (make ("Default",       180.0f, 2400.0f, { { { 4.5f, -2.0f, curveExp }, { 6.0f, -3.5f, curveLin }, { -1.5f, 2.5f, curveLog } } }));
        list.push_back (make ("Init",          180.0f, 2400.0f, { { { 0.0f, 0.0f, curveLin }, { 0.0f, 0.0f, curveLin }, { 0.0f, 0.0f, curveLin } } }));
        list.push_back (make ("Punchy Kit",    120.0f, 3200.0f, { { { 6.0f, -4.0f, curveExp }, { 8.0f, -2.0f, curveLin }, { 3.0f, 1.0f, curveLog } } }));
        list.push_back (make ("Soft Attack",   200.0f, 2000.0f, { { { -4.0f, 3.0f, curveLin }, { -6.0f, 4.0f, curveLog }, { -3.0f, 1.0f, curveLin } } }));
        list.push_back (make ("Tight Low End",  90.0f, 1800.0f, { { { 9.0f, -9.0f, curveExp }, { 2.0f, -2.0f, curveLin }, { 0.0f, 0.0f, curveLog } } }));

        // The exponential curve only reacts to the biggest hits, which suits a snare crack.
        list.push_back (make ("Snappy Snare",  160.0f, 2800.0f, { { { 1.0f, -2.0f, curveLin }, { 4.5f, -2.5f, curveLin }, { 8.0f, -1.5f, curveExp } } }));

        // Sustain only: a room's tail lives in the mids and highs.
        list.push_back (make ("Room Tamer",    200.0f, 2500.0f, { { { 0.0f, 0.0f, curveLin }, { 0.0f, -5.0f, curveLin }, { 1.0f, -7.0f, curveLog } } }));

        // A touch of both everywhere; the logarithmic curve lets small detector values through.
        list.push_back (make ("Drum Bus Glue", 180.0f, 2400.0f, { { { 2.0f, 3.0f, curveLog }, { 2.0f, 3.0f, curveLog }, { 2.0f, 3.0f, curveLog } } }));

        return list;
    }();

    return presets;
}

const std::vector<RecalledParameter>& recalledParameters()
{
    static const std::vector<RecalledParameter> list = []
    {
        std::vector<RecalledParameter> l;

        l.push_back ({ pid::xover1, 0.5f });
        l.push_back ({ pid::xover2, 0.5f });

        for (int b = 0; b < numBands; ++b)
        {
            l.push_back ({ pid::attack[b],  0.05f });
            l.push_back ({ pid::sustain[b], 0.05f });
            l.push_back ({ pid::curve[b],      0.5f });
            l.push_back ({ pid::mute[b],       0.5f });
            l.push_back ({ pid::solo[b],       0.5f });
            l.push_back ({ pid::bandBypass[b], 0.5f });
        }

        l.push_back ({ pid::lookahead,  0.5f });
        l.push_back ({ pid::stereoLink, 0.5f });
        return l;
    }();

    return list;
}

/* ── PresetManager ───────────────────────────────────────────────────────── */

namespace
{
juce::File& folderOverride()
{
    static juce::File folder;
    return folder;
}

float realValue (const juce::RangedAudioParameter& p)
{
    return p.convertFrom0to1 (p.getValue());
}
} // namespace

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& state, UndoRecorder& undoRecorder)
    : apvts (state), recorder (undoRecorder)
{
    syncWithState();
    rescan();
}

juce::File PresetManager::getUserFolder()
{
    if (folderOverride().getFullPathName().isNotEmpty())
        return folderOverride();

    return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Audio/Presets/Sungam/Timaband");
}

void PresetManager::setUserFolder (const juce::File& folder)
{
    folderOverride() = folder;
}

juce::String PresetManager::sanitiseName (const juce::String& raw)
{
    juce::String out;

    for (auto c : raw)
        if (c >= 32 && juce::String ("/\\:*?\"<>|").indexOfChar (c) < 0)
            out += c;

    // No hidden files, and nothing a file system would trim off the end.
    out = out.trim().trimCharactersAtStart (".").trim();
    return out.substring (0, 80).trimEnd();
}

juce::File PresetManager::getFileFor (const juce::String& name) const
{
    return getUserFolder().getChildFile (name + fileExtension);
}

void PresetManager::rescan()
{
    entries.clear();

    for (const auto& preset : factoryPresets())
        entries.push_back ({ preset.name, true });

    juce::Array<juce::File> files;
    getUserFolder().findChildFiles (files, juce::File::findFiles, false, juce::String ("*") + fileExtension);

    std::vector<juce::String> names;

    for (const auto& file : files)
        names.push_back (file.getFileNameWithoutExtension());

    std::sort (names.begin(), names.end(), [] (const juce::String& a, const juce::String& b) { return a.compareNatural (b) < 0; });

    for (const auto& name : names)
        entries.push_back ({ name, false });

    referenceCurrent = false;
}

juce::String PresetManager::getName() const    { return presetState::read (apvts.state).name; }
bool PresetManager::isFactory() const          { return presetState::read (apvts.state).factory; }

int PresetManager::getCurrentIndex() const
{
    const auto id = presetState::read (apvts.state);

    for (size_t i = 0; i < entries.size(); ++i)
        if (entries[i].factory == id.factory && (id.factory ? entries[i].name == id.name : entries[i].name.equalsIgnoreCase (id.name)))
            return (int) i;

    return -1;
}

bool PresetManager::canDelete() const
{
    const auto id = presetState::read (apvts.state);
    return ! id.factory && getFileFor (id.name).existsAsFile();
}

/* ── Values ──────────────────────────────────────────────────────────────── */

std::vector<float> PresetManager::valuesFor (const std::vector<PresetValue>& list) const
{
    std::vector<float> out;

    for (const auto& r : recalledParameters())
    {
        const auto it = std::find_if (list.begin(), list.end(), [&r] (const PresetValue& v) { return v.id == r.id; });
        const auto* p = apvts.getParameter (r.id);

        // A preset that does not mention a parameter gets that parameter's default.
        out.push_back (it != list.end() ? it->value : (p != nullptr ? p->convertFrom0to1 (p->getDefaultValue()) : 0.0f));
    }

    return out;
}

bool PresetManager::readFile (const juce::File& file, std::vector<float>& values) const
{
    const auto xml = juce::XmlDocument::parse (file);

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType().toString()))
        return false;

    std::vector<PresetValue> list;

    for (const auto* child : xml->getChildWithTagNameIterator ("PARAM"))
        list.push_back ({ child->getStringAttribute ("id"), (float) child->getDoubleAttribute ("value") });

    values = valuesFor (list);
    return true;
}

void PresetManager::setReference (const juce::String& name, bool factory, bool valid, std::vector<float> values)
{
    reference = { name, factory, valid, std::move (values) };
    referenceCurrent = true;
}

const PresetManager::Reference& PresetManager::getReference() const
{
    const auto id = presetState::read (apvts.state);

    if (referenceCurrent && reference.name == id.name && reference.factory == id.factory)
        return reference;

    reference = { id.name, id.factory, false, {} };
    referenceCurrent = true;

    if (id.factory)
    {
        for (const auto& preset : factoryPresets())
            if (preset.name == id.name)
            {
                reference.values = valuesFor (preset.values);
                reference.valid = true;
            }
    }
    else
    {
        reference.valid = readFile (getFileFor (id.name), reference.values);
    }

    return reference;
}

bool PresetManager::isDirty() const
{
    const auto& ref = getReference();

    if (! ref.valid)
        return true;

    const auto& list = recalledParameters();

    for (size_t i = 0; i < list.size(); ++i)
        if (std::abs (apvts.getRawParameterValue (list[i].id)->load() - ref.values[i]) > list[i].tolerance)
            return true;

    return false;
}

/* ── Loading ─────────────────────────────────────────────────────────────── */

void PresetManager::apply (const std::vector<float>& values, const juce::String& name, bool factory)
{
    // One step for the whole preset, however many parameters it moves.
    UndoRecorder::Scope step (recorder);

    std::vector<std::pair<juce::RangedAudioParameter*, float>> changes;
    const auto& list = recalledParameters();

    for (size_t i = 0; i < list.size(); ++i)
    {
        auto* p = apvts.getParameter (list[i].id);
        const auto& range = p->getNormalisableRange();
        const auto target = juce::jlimit (range.start, range.end, values[i]);

        if (std::abs (realValue (*p) - target) > 1.0e-3f)
            changes.emplace_back (p, p->convertTo0to1 (target));
    }

    for (auto& c : changes)
        c.first->beginChangeGesture();

    for (auto& c : changes)
        c.first->setValueNotifyingHost (c.second);

    for (auto& c : changes)
        c.first->endChangeGesture();

    presetState::write (apvts.state, { name, factory });
    setReference (name, factory, true, values);

    if (factory)
        for (size_t i = 0; i < factoryPresets().size(); ++i)
            if (factoryPresets()[i].name == name)
                programIndex.store ((int) i);

    if (onLoaded != nullptr)
        onLoaded();
}

bool PresetManager::loadFactory (int index)
{
    const auto& presets = factoryPresets();

    if (index < 0 || index >= (int) presets.size())
        return false;

    apply (valuesFor (presets[(size_t) index].values), presets[(size_t) index].name, true);
    return true;
}

bool PresetManager::loadUser (const juce::String& name)
{
    std::vector<float> values;

    if (! readFile (getFileFor (name), values))
        return false;

    apply (values, name, false);
    return true;
}

bool PresetManager::load (int entryIndex)
{
    if (entryIndex < 0 || entryIndex >= (int) entries.size())
        return false;

    const auto entry = entries[(size_t) entryIndex];
    return entry.factory ? loadFactory (entryIndex) : loadUser (entry.name);
}

void PresetManager::step (int direction)
{
    rescan();

    const auto n = (int) entries.size();
    const auto current = getCurrentIndex();
    const auto next = current < 0 ? (direction > 0 ? 0 : n - 1)
                                  : (current + (direction > 0 ? 1 : n - 1)) % n;

    load (next);
}

/* ── Saving ──────────────────────────────────────────────────────────────── */

PresetManager::SaveResult PresetManager::saveAs (const juce::String& rawName)
{
    const auto name = sanitiseName (rawName);

    if (name.isEmpty())
        return SaveResult::invalidName;

    for (const auto& preset : factoryPresets())
        if (preset.name.equalsIgnoreCase (name))
            return SaveResult::factoryName;

    // The same XML as the plug-in's state, minus the parameters a preset does not recall.
    juce::XmlElement xml (apvts.state.getType().toString());
    std::vector<float> values;

    for (const auto& r : recalledParameters())
    {
        const auto value = apvts.getRawParameterValue (r.id)->load();

        auto* child = xml.createNewChildElement ("PARAM");
        child->setAttribute ("id", r.id);
        child->setAttribute ("value", (double) value);
        values.push_back (value);
    }

    const auto file = getFileFor (name);

    if (! file.getParentDirectory().createDirectory().wasOk() || ! file.replaceWithText (xml.toString()))
        return SaveResult::failed;

    presetState::write (apvts.state, { name, false });
    rescan();
    setReference (name, false, true, std::move (values));
    return SaveResult::saved;
}

PresetManager::SaveResult PresetManager::save()
{
    const auto id = presetState::read (apvts.state);
    return id.factory ? SaveResult::factoryName : saveAs (id.name);
}

bool PresetManager::remove()
{
    const auto id = presetState::read (apvts.state);

    if (id.factory || ! getFileFor (id.name).deleteFile())
        return false;

    // The settings stay as they are; they just no longer belong to a file.
    presetState::write (apvts.state, { presetState::defaultName, true });
    rescan();
    return true;
}

void PresetManager::syncWithState()
{
    auto& state = apvts.state;

    if (! state.hasProperty (presetState::presetName) || ! state.hasProperty (presetState::presetIsFactory))
        presetState::write (state, presetState::read (state));

    const auto id = presetState::read (state);

    if (id.factory)
        for (size_t i = 0; i < factoryPresets().size(); ++i)
            if (factoryPresets()[i].name == id.name)
                programIndex.store ((int) i);

    referenceCurrent = false;
}

} // namespace timaband
