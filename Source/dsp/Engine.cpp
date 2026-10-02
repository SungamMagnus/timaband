#include "Engine.h"

namespace timaband
{

namespace
{
    // Low band windows are longer so one bass cycle doesn't read as a transient.
    constexpr std::array<float, Engine::numBands> bandTimeScale { 2.0f, 1.0f, 0.6f };

    // Crossover coefficients are recomputed at most this often while they glide.
    constexpr int crossoverUpdateInterval = 32;

    inline float dbToGain (float db) noexcept { return std::exp (db * 0.115129255f); }  // ln(10) / 20
}

void Engine::prepare (double newSampleRate, int maxBlockSize, int newNumChannels)
{
    sampleRate  = newSampleRate;
    numChannels = juce::jlimit (1, maxChannels, newNumChannels);

    const juce::dsp::ProcessSpec spec { sampleRate,
                                        (juce::uint32) juce::jmax (1, maxBlockSize),
                                        (juce::uint32) maxChannels };
    split1.prepare (spec);
    split2.prepare (spec);
    lowAllpass.setType (juce::dsp::LinkwitzRileyFilterType::allpass);
    lowAllpass.prepare (spec);

    const auto maxDelay = latencyFor (sampleRate, true);
    for (auto& band : bandDelay)
        for (auto& line : band)
            line.prepare (maxDelay);
    for (auto& line : dryDelay)
        line.prepare (maxDelay);

    for (size_t b = 0; b < (size_t) numBands; ++b)
        for (auto& shaper : shapers[b])
            shaper.prepare (sampleRate, bandTimeScale[b]);

    xover1.reset (sampleRate, 0.05);
    xover2.reset (sampleRate, 0.05);
    inputGain.reset (sampleRate, 0.02);
    outputGain.reset (sampleRate, 0.02);
    bypassMix.reset (sampleRate, 0.01);
    for (auto& gate : bandGate)
        gate.reset (sampleRate, 0.005);

    reset();
}

void Engine::reset()
{
    split1.reset();
    split2.reset();
    lowAllpass.reset();

    for (auto& band : shapers)
        for (auto& shaper : band)
            shaper.reset();
    for (auto& band : bandDelay)
        for (auto& line : band)
            line.reset();
    for (auto& line : dryDelay)
        line.reset();

    for (size_t b = 0; b < (size_t) numBands; ++b)
    {
        meterGainDb[b].store (0.0f, std::memory_order_relaxed);
        meterTransient[b].store (0.0f, std::memory_order_relaxed);
    }

    appliedXover1 = appliedXover2 = 0.0f;
    needsSnap = true;
}

void Engine::setTargets (const Settings& settings, bool snap) noexcept
{
    // Enforce the crossover order and spacing here too: host automation can move
    // either parameter anywhere in its range.
    const auto cap = (float) juce::jmin ((double) maxXover, sampleRate * 0.45);
    const auto f1  = juce::jlimit (minXover, cap / minRatio, settings.xover1);
    const auto f2  = juce::jlimit (f1 * minRatio, cap, settings.xover2);

    const auto anySolo = std::any_of (settings.bands.begin(), settings.bands.end(),
                                      [] (const BandSettings& b) { return b.solo; });

    const auto set = [snap] (auto& smoother, float target)
    {
        if (snap)
            smoother.setCurrentAndTargetValue (target);
        else
            smoother.setTargetValue (target);
    };

    set (xover1, f1);
    set (xover2, f2);
    set (inputGain,  dbToGain (settings.inputGainDb));
    set (outputGain, dbToGain (settings.outputGainDb));
    set (bypassMix,  settings.bypass ? 1.0f : 0.0f);

    for (size_t b = 0; b < (size_t) numBands; ++b)
    {
        const auto& band = settings.bands[b];
        set (bandGate[b], (anySolo ? band.solo : ! band.mute) ? 1.0f : 0.0f);
    }

    latency = latencyFor (sampleRate, settings.lookahead);
}

void Engine::updateCrossovers (int numSamples) noexcept
{
    // The two smoothers can be mid-glide toward different targets, so re-apply the spacing.
    const auto f1 = xover1.skip (numSamples);
    const auto f2 = juce::jmax (xover2.skip (numSamples), f1 * minRatio);

    if (! juce::exactlyEqual (f1, appliedXover1))
    {
        split1.setCutoffFrequency (f1);
        appliedXover1 = f1;
    }

    if (! juce::exactlyEqual (f2, appliedXover2))
    {
        split2.setCutoffFrequency (f2);
        lowAllpass.setCutoffFrequency (f2);
        appliedXover2 = f2;
    }
}

void Engine::process (juce::AudioBuffer<float>& buffer, const Settings& settings) noexcept
{
    setTargets (settings, needsSnap);
    needsSnap = false;

    const auto numSamples = buffer.getNumSamples();
    const auto chans = juce::jmin (buffer.getNumChannels(), numChannels);

    if (numSamples == 0 || chans == 0)
        return;

    std::array<float*, maxChannels> data {};
    for (int c = 0; c < chans; ++c)
        data[(size_t) c] = buffer.getWritePointer (c);

    const auto linked = settings.stereoLink || chans == 1;
    std::array<float, numBands> peakGainDb {}, peakTransient {};

    for (int start = 0; start < numSamples; start += crossoverUpdateInterval)
    {
        const auto end = juce::jmin (numSamples, start + crossoverUpdateInterval);
        updateCrossovers (end - start);

        for (int i = start; i < end; ++i)
        {
            const auto inGain  = inputGain.getNextValue();
            const auto outGain = outputGain.getNextValue();
            const auto bypass  = bypassMix.getNextValue();

            std::array<std::array<float, maxChannels>, numBands> bands {};
            std::array<float, maxChannels> dry {}, wet {};

            // Split: low = LP(f1) -> AP(f2), mid = HP(f1) -> LP(f2), high = HP(f1) -> HP(f2).
            for (int c = 0; c < chans; ++c)
            {
                const auto ci = (size_t) c;
                const auto x  = data[ci][i];
                dry[ci] = dryDelay[ci].process (x, latency);

                float low, rest, mid, high;
                split1.processSample (c, x * inGain, low, rest);
                split2.processSample (c, rest, mid, high);

                bands[0][ci] = lowAllpass.processSample (c, low);
                bands[1][ci] = mid;
                bands[2][ci] = high;
            }

            for (size_t b = 0; b < (size_t) numBands; ++b)
            {
                const auto& band = settings.bands[b];
                const auto  gate = bandGate[b].getNextValue();
                std::array<float, maxChannels> gainDb {};

                // The detector sees the band now; the audio it shapes is delayed by the lookahead.
                if (linked)
                {
                    auto level = 0.0f;
                    for (int c = 0; c < chans; ++c)
                        level = juce::jmax (level, std::abs (bands[b][(size_t) c]));

                    gainDb.fill (shapers[b][0].process (level, band.attackDb, band.sustainDb, band.curve));
                    peakTransient[b] = juce::jmax (peakTransient[b], shapers[b][0].getTransient());
                }
                else
                {
                    for (int c = 0; c < chans; ++c)
                    {
                        auto& shaper = shapers[b][(size_t) c];
                        gainDb[(size_t) c] = shaper.process (std::abs (bands[b][(size_t) c]),
                                                             band.attackDb, band.sustainDb, band.curve);
                        peakTransient[b] = juce::jmax (peakTransient[b], shaper.getTransient());
                    }
                }

                for (int c = 0; c < chans; ++c)
                {
                    const auto ci      = (size_t) c;
                    const auto delayed = bandDelay[b][ci].process (bands[b][ci], latency) * gate;
                    const auto shaped  = delayed * dbToGain (gainDb[ci]);

                    // Delta subtracts the unshaped band, so it nulls at zero settings
                    // despite the crossover's allpass phase.
                    wet[ci] += settings.delta ? shaped - delayed : shaped;

                    if (std::abs (gainDb[ci]) > std::abs (peakGainDb[b]))
                        peakGainDb[b] = gainDb[ci];
                }
            }

            for (int c = 0; c < chans; ++c)
            {
                const auto ci = (size_t) c;
                const auto processed = wet[ci] * outGain;

                // Fully bypassed must be bit-exact; the crossfade formula is off by an ulp at 1.0.
                data[ci][i] = bypass >= 1.0f ? dry[ci] : processed + bypass * (dry[ci] - processed);
            }
        }
    }

    for (size_t b = 0; b < (size_t) numBands; ++b)
    {
        meterGainDb[b].store (peakGainDb[b], std::memory_order_relaxed);
        meterTransient[b].store (peakTransient[b], std::memory_order_relaxed);
    }
}

} // namespace timaband
