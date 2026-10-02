#pragma once

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <vector>

#include "Shaper.h"

namespace timaband
{

struct BandSettings
{
    float attackDb  = 0.0f;
    float sustainDb = 0.0f;
    Curve curve     = Curve::linear;
    bool  mute      = false;
    bool  solo      = false;
    bool  bypass    = false;   // band still plays, unshaped
};

struct Settings
{
    float xover1 = 180.0f;
    float xover2 = 2400.0f;
    std::array<BandSettings, 3> bands {};
    float inputGainDb  = 0.0f;
    float outputGainDb = 0.0f;
    bool  lookahead  = true;
    bool  stereoLink = true;
    bool  delta      = false;
    bool  bypass     = false;
};

// The whole signal path, independent of the plugin wrapper so tools can drive it.
// Call prepare() before process(); process() is real-time safe.
class Engine
{
public:
    static constexpr int    numBands    = 3;
    static constexpr int    maxChannels = 2;
    static constexpr double lookaheadMs = 3.0;
    static constexpr float  minRatio    = 1.26f;   // crossovers stay >= 1/3 octave apart
    static constexpr float  minXover    = 20.0f;
    static constexpr float  maxXover    = 20000.0f;

    static int latencyFor (double sampleRate, bool lookahead) noexcept
    {
        return lookahead ? (int) std::lround (sampleRate * lookaheadMs * 0.001) : 0;
    }

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    // Processes up to maxChannels channels in place; any extra channels are left untouched.
    void process (juce::AudioBuffer<float>& buffer, const Settings& settings) noexcept;

    int getLatencySamples() const noexcept { return latency; }

    // Metering, safe to read from any thread. Values describe the last processed block.
    float getBandGainDb (int band) const noexcept    { return meterGainDb[(size_t) band].load (std::memory_order_relaxed); }
    float getBandTransient (int band) const noexcept { return meterTransient[(size_t) band].load (std::memory_order_relaxed); }

private:
    class DelayLine
    {
    public:
        void prepare (int maxDelay) { buffer.assign ((size_t) maxDelay + 1, 0.0f); pos = 0; }
        void reset() noexcept       { std::fill (buffer.begin(), buffer.end(), 0.0f); pos = 0; }

        float process (float x, int delay) noexcept
        {
            const auto size = (int) buffer.size();
            buffer[(size_t) pos] = x;
            auto read = pos - delay;
            if (read < 0) read += size;
            if (++pos == size) pos = 0;
            return buffer[(size_t) read];
        }

    private:
        std::vector<float> buffer;
        int pos = 0;
    };

    void setTargets (const Settings& settings, bool snap) noexcept;
    void updateCrossovers (int numSamples) noexcept;

    double sampleRate = 44100.0;
    int numChannels = 2;
    int latency = 0;
    bool needsSnap = true;
    float appliedXover1 = 0.0f, appliedXover2 = 0.0f;

    juce::dsp::LinkwitzRileyFilter<float> split1, split2, lowAllpass;

    std::array<std::array<Shaper, maxChannels>, numBands>    shapers;
    std::array<std::array<DelayLine, maxChannels>, numBands> bandDelay;
    std::array<DelayLine, maxChannels>                       dryDelay;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> xover1, xover2;
    juce::SmoothedValue<float> inputGain, outputGain, bypassMix;
    std::array<juce::SmoothedValue<float>, numBands> bandGate, bandShape;

    std::array<std::atomic<float>, numBands> meterGainDb {}, meterTransient {};
};

} // namespace timaband
