#include "PluginProcessor.h"

#include "PluginEditor.h"

using namespace timaband;

static_assert (numBands == Engine::numBands, "parameter bands must match the engine's");

namespace
{
std::atomic<float>* raw (juce::AudioProcessorValueTreeState& s, const juce::String& id)
{
    auto* p = s.getRawParameterValue (id);
    jassert (p != nullptr);
    return p;
}

bool isOn (const std::atomic<float>* p) noexcept
{
    return p->load (std::memory_order_relaxed) > 0.5f;
}

float get (const std::atomic<float>* p) noexcept
{
    return p->load (std::memory_order_relaxed);
}
} // namespace

TimabandProcessor::TimabandProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "state", createLayout()),    // no UndoManager: see UndoRecorder
      undoRecorder (*this, apvts, undoManager),
      presets (apvts, undoRecorder)
{
    xover1 = raw (apvts, pid::xover1);
    xover2 = raw (apvts, pid::xover2);

    for (int b = 0; b < numBands; ++b)
    {
        auto& band = bands[(size_t) b];
        band.attack  = raw (apvts, pid::attack[b]);
        band.sustain = raw (apvts, pid::sustain[b]);
        band.curve   = raw (apvts, pid::curve[b]);
        band.mute    = raw (apvts, pid::mute[b]);
        band.solo    = raw (apvts, pid::solo[b]);
        band.bypass  = raw (apvts, pid::bandBypass[b]);
    }

    inputGain  = raw (apvts, pid::inputGain);
    outputGain = raw (apvts, pid::outputGain);
    lookahead  = raw (apvts, pid::lookahead);
    stereoLink = raw (apvts, pid::stereoLink);
    delta      = raw (apvts, pid::delta);
    bypass     = raw (apvts, pid::bypass);

    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (pid::bypass));
    jassert (bypassParam != nullptr);

    applyUiStateDefaults();

    // A preset loaded from the editor changes what the host's program menu should show.
    presets.onLoaded = [this]
    {
        if (! hostIsLoadingProgram)
            updateHostDisplay (ChangeDetails().withProgramChanged (true));
    };
}

bool TimabandProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    return in == out && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
}

void TimabandProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    setLatencySamples (Engine::latencyFor (sampleRate, isOn (lookahead)));
}

Settings TimabandProcessor::gather() const
{
    Settings s;

    s.xover1 = get (xover1);
    s.xover2 = get (xover2);

    for (size_t b = 0; b < (size_t) numBands; ++b)
    {
        const auto& p = bands[b];
        auto& band = s.bands[b];

        band.attackDb  = get (p.attack);
        band.sustainDb = get (p.sustain);
        band.curve     = (Curve) juce::jlimit (0, 2, juce::roundToInt (get (p.curve)));
        band.mute      = isOn (p.mute);
        band.solo      = isOn (p.solo);
        band.bypass    = isOn (p.bypass);
    }

    s.inputGainDb  = get (inputGain);
    s.outputGainDb = get (outputGain);
    s.lookahead    = isOn (lookahead);
    s.stereoLink   = isOn (stereoLink);
    s.delta        = isOn (delta);
    s.bypass       = isOn (bypass);

    return s;
}

void TimabandProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, buffer.getNumSamples());

    const auto numIn  = getTotalNumInputChannels();
    const auto numOut = getTotalNumOutputChannels();

    inputMeter.measure (buffer, numIn);
    pushMono (preFifo, buffer, numIn);

    engine.process (buffer, gather());

    outputMeter.measure (buffer, numOut);
    pushMono (postFifo, buffer, numOut);

    // The engine's latency follows the lookahead switch, so the host needs telling when it moves.
    if (engine.getLatencySamples() != getLatencySamples())
        setLatencySamples (engine.getLatencySamples());
}

void TimabandProcessor::pushMono (SampleFifo& fifo, const juce::AudioBuffer<float>& buffer, int numChannels) noexcept
{
    const auto chans = juce::jmin (numChannels, buffer.getNumChannels());
    if (chans == 0)
        return;

    const auto scale = 1.0f / (float) chans;
    std::array<float, 256> mono;

    for (int start = 0; start < buffer.getNumSamples(); start += (int) mono.size())
    {
        const auto n = juce::jmin ((int) mono.size(), buffer.getNumSamples() - start);
        std::fill (mono.begin(), mono.begin() + n, 0.0f);

        for (int c = 0; c < chans; ++c)
        {
            const auto* data = buffer.getReadPointer (c, start);
            for (int i = 0; i < n; ++i)
                mono[(size_t) i] += data[i];
        }

        for (int i = 0; i < n; ++i)
            mono[(size_t) i] *= scale;

        fifo.push (mono.data(), n);
    }
}

juce::AudioProcessorEditor* TimabandProcessor::createEditor()
{
    return new TimabandEditor (*this);
}

void TimabandProcessor::applyUiStateDefaults()
{
    const auto ensure = [this] (const juce::Identifier& id, const juce::var& value)
    {
        if (! apvts.state.hasProperty (id))
            apvts.state.setProperty (id, value, nullptr);
    };

    ensure (uiState::selectedBand, uiState::defaultSelectedBand);
    ensure (uiState::analyzerPost, uiState::defaultAnalyzerPost);
    ensure (uiState::linkBands,    uiState::defaultLinkBands);

    // A hand-edited or corrupt preset must not leave the editor pointing at a band that doesn't exist.
    const auto band = (int) apvts.state.getProperty (uiState::selectedBand);
    if (band < 0 || band >= numBands)
        apvts.state.setProperty (uiState::selectedBand, uiState::defaultSelectedBand, nullptr);
}

int TimabandProcessor::getNumPrograms()
{
    return (int) factoryPresets().size();
}

int TimabandProcessor::getCurrentProgram()
{
    return presets.getProgramIndex();
}

const juce::String TimabandProcessor::getProgramName (int index)
{
    const auto& list = factoryPresets();
    return index >= 0 && index < (int) list.size() ? list[(size_t) index].name : juce::String();
}

void TimabandProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= getNumPrograms())
        return;

    // Hosts may call this from any thread; the load is parameter gestures and tree edits, so it
    // belongs on the message thread. The index is visible straight away.
    presets.setProgramIndex (index);

    const auto load = [this, index]
    {
        const juce::ScopedValueSetter<bool> quiet (hostIsLoadingProgram, true);
        presets.loadFactory (index);
    };

    if (juce::MessageManager::existsAndIsCurrentThread())
        load();
    else
        juce::MessageManager::callAsync ([weak = juce::WeakReference<TimabandProcessor> (this), load]
        {
            if (weak != nullptr)
                load();
        });
}

void TimabandProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void TimabandProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            applyUiStateDefaults();

            // A session starts with an empty history. The new tree may have been set from any thread.
            const auto restore = [this]
            {
                presets.syncWithState();
                undoRecorder.clear();
            };

            if (juce::MessageManager::existsAndIsCurrentThread())
                restore();
            else
                juce::MessageManager::callAsync ([weak = juce::WeakReference<TimabandProcessor> (this), restore]
                {
                    if (weak != nullptr)
                        restore();
                });
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TimabandProcessor();
}
