#pragma once

#include <array>
#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "Presets.h"
#include "UndoRecorder.h"
#include "dsp/Analysis.h"
#include "dsp/Engine.h"

class TimabandProcessor final : public juce::AudioProcessor
{
public:
    TimabandProcessor();
    ~TimabandProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // JucePlugin_Name is UTF-8 ("Tímaband"); String (const char*) asserts on non-ASCII.
    const juce::String getName() const override { return juce::String (juce::CharPointer_UTF8 (JucePlugin_Name)); }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    // The factory presets are the host's programs. Loading one is exactly a preset load in the editor.
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // For the editor's meters and analyzer.
    timaband::Engine& getEngine() noexcept             { return engine; }
    const timaband::Engine& getEngine() const noexcept { return engine; }

    // Mono mix of the input (pre) or output (post), for the spectrum analyzer.
    timaband::SampleFifo& getAnalyzerFifo (bool post) noexcept { return post ? postFifo : preFifo; }
    timaband::LevelMeter& getInputMeter() noexcept             { return inputMeter; }
    timaband::LevelMeter& getOutputMeter() noexcept            { return outputMeter; }

    // The undo history of the editor's gestures; see UndoRecorder for what goes in it.
    juce::UndoManager& getUndoManager() noexcept        { return undoManager; }
    timaband::UndoRecorder& getUndoRecorder() noexcept  { return undoRecorder; }
    timaband::PresetManager& getPresets() noexcept      { return presets; }

    juce::AudioProcessorValueTreeState apvts;

private:
    struct BandParams
    {
        std::atomic<float>* attack  = nullptr;
        std::atomic<float>* sustain = nullptr;
        std::atomic<float>* curve   = nullptr;
        std::atomic<float>* mute    = nullptr;
        std::atomic<float>* solo    = nullptr;
        std::atomic<float>* bypass  = nullptr;
    };

    timaband::Settings gather() const;

    static void pushMono (timaband::SampleFifo&, const juce::AudioBuffer<float>&, int numChannels) noexcept;

    // Fills in whichever UI properties the state tree lacks (a fresh instance or an older preset).
    void applyUiStateDefaults();

    timaband::Engine engine;
    timaband::SampleFifo preFifo, postFifo;
    timaband::LevelMeter inputMeter, outputMeter;

    std::atomic<float>* xover1 = nullptr;
    std::atomic<float>* xover2 = nullptr;
    std::array<BandParams, timaband::numBands> bands {};
    std::atomic<float>* inputGain  = nullptr;
    std::atomic<float>* outputGain = nullptr;
    std::atomic<float>* lookahead  = nullptr;
    std::atomic<float>* stereoLink = nullptr;
    std::atomic<float>* delta      = nullptr;
    std::atomic<float>* bypass     = nullptr;
    juce::AudioParameterBool* bypassParam = nullptr;

    // Declared after apvts: the recorder listens to the parameters it created.
    juce::UndoManager undoManager;
    timaband::UndoRecorder undoRecorder;
    timaband::PresetManager presets;
    bool hostIsLoadingProgram = false;

    JUCE_DECLARE_WEAK_REFERENCEABLE (TimabandProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TimabandProcessor)
};
