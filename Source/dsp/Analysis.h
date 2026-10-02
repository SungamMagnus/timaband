#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <vector>

namespace timaband
{

// Single-producer, single-consumer sample queue: the audio thread pushes, the
// editor pulls. If the editor falls behind (or is closed), new samples are
// dropped; the audio thread never waits.
class SampleFifo
{
public:
    explicit SampleFifo (int capacity = 1 << 15)
        : fifo (capacity), buffer ((size_t) capacity, 0.0f) {}

    void push (const float* samples, int numSamples) noexcept
    {
        const auto scope = fifo.write (numSamples);
        copyIn (samples, scope.startIndex1, scope.blockSize1);
        copyIn (samples + scope.blockSize1, scope.startIndex2, scope.blockSize2);
    }

    // Returns how many samples were copied into dest.
    int pull (float* dest, int maxSamples) noexcept
    {
        const auto scope = fifo.read (juce::jmin (maxSamples, fifo.getNumReady()));
        copyOut (dest, scope.startIndex1, scope.blockSize1);
        copyOut (dest + scope.blockSize1, scope.startIndex2, scope.blockSize2);
        return scope.blockSize1 + scope.blockSize2;
    }

private:
    void copyIn (const float* src, int start, int num) noexcept
    {
        if (num > 0)
            std::copy (src, src + num, buffer.begin() + start);
    }

    void copyOut (float* dest, int start, int num) const noexcept
    {
        if (num > 0)
            std::copy (buffer.begin() + start, buffer.begin() + start + num, dest);
    }

    juce::AbstractFifo fifo;
    std::vector<float> buffer;
};

// Peak and mean square per channel, accumulated by the audio thread and
// collected (and cleared) by the editor, so no peak between two UI frames is lost.
class LevelMeter
{
public:
    static constexpr int numChannels = 2;

    struct Reading
    {
        std::array<float, numChannels> peak {}, meanSquare {};
    };

    // A mono buffer feeds both meter channels.
    void measure (const juce::AudioBuffer<float>& buffer, int numBufferChannels) noexcept
    {
        const auto n = buffer.getNumSamples();
        if (n == 0 || numBufferChannels == 0)
            return;

        for (int c = 0; c < numChannels; ++c)
        {
            const auto* data = buffer.getReadPointer (juce::jmin (c, numBufferChannels - 1));
            auto peak = 0.0f, sumSquares = 0.0f;

            for (int i = 0; i < n; ++i)
            {
                peak = juce::jmax (peak, std::abs (data[i]));
                sumSquares += data[i] * data[i];
            }

            auto& ch = channels[(size_t) c];
            auto previous = ch.peak.load (std::memory_order_relaxed);
            while (peak > previous && ! ch.peak.compare_exchange_weak (previous, peak, std::memory_order_relaxed)) {}

            // atomic<float>::fetch_add is C++20; this project builds as C++17.
            auto sum = ch.sumSquares.load (std::memory_order_relaxed);
            while (! ch.sumSquares.compare_exchange_weak (sum, sum + sumSquares, std::memory_order_relaxed)) {}

            ch.count.fetch_add (n, std::memory_order_relaxed);
        }
    }

    // Editor side: everything measured since the previous call.
    Reading collect() noexcept
    {
        Reading r;

        for (size_t c = 0; c < (size_t) numChannels; ++c)
        {
            auto& ch = channels[c];
            r.peak[c] = ch.peak.exchange (0.0f, std::memory_order_relaxed);

            const auto sum   = ch.sumSquares.exchange (0.0f, std::memory_order_relaxed);
            const auto count = ch.count.exchange (0, std::memory_order_relaxed);
            r.meanSquare[c]  = count > 0 ? sum / (float) count : 0.0f;
        }

        return r;
    }

private:
    struct Channel
    {
        std::atomic<float> peak { 0.0f }, sumSquares { 0.0f };
        std::atomic<int> count { 0 };
    };

    std::array<Channel, numChannels> channels;
};

} // namespace timaband
