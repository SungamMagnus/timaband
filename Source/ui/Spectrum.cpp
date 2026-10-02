#include "Spectrum.h"

#include <cmath>

namespace timaband::ui
{

namespace
{
constexpr float silenceDb = -120.0f;

// Catmull-Rom through four equally spaced samples, evaluated between y1 and y2.
float cubic (float y0, float y1, float y2, float y3, float t) noexcept
{
    const auto a = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
    const auto b = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const auto c = -0.5f * y0 + 0.5f * y2;
    return ((a * t + b) * t + c) * t + y1;
}
} // namespace

SpectrumAnalyzer::SpectrumAnalyzer()
{
    // Periodic Hann. The sum of the window is what turns a bin magnitude into the
    // amplitude of the sine that produced it, so a full-scale sine reads 0 dBFS.
    auto sum = 0.0;

    for (int i = 0; i < fftSize; ++i)
    {
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) fftSize);
        sum += (double) window[(size_t) i];
    }

    windowNorm = (float) (2.0 / sum);
    reset();
}

void SpectrumAnalyzer::setSampleRate (double newRate) noexcept
{
    if (newRate > 0.0)
        sampleRate = newRate;
}

void SpectrumAnalyzer::reset()
{
    ring.fill (0.0f);
    bins.fill (silenceDb);
    points.fill (silenceDb);
    writePos = 0;
    idleSeconds = 0.0f;
}

void SpectrumAnalyzer::discard (SampleFifo& fifo)
{
    while (fifo.pull (scratch.data(), (int) scratch.size()) > 0) {}
    reset();
}

double SpectrumAnalyzer::pointFrequency (int index) noexcept
{
    return minHz * std::pow (maxHz / minHz, (double) index / (double) (numPoints - 1));
}

void SpectrumAnalyzer::push (const float* samples, int num) noexcept
{
    // Only the newest fftSize samples matter.
    if (num > fftSize)
    {
        samples += num - fftSize;
        num = fftSize;
    }

    for (int i = 0; i < num; ++i)
    {
        ring[(size_t) writePos] = samples[i];
        writePos = (writePos + 1) & (fftSize - 1);
    }
}

bool SpectrumAnalyzer::update (SampleFifo& fifo, float dtSeconds)
{
    auto received = 0;

    for (;;)
    {
        const auto n = fifo.pull (scratch.data(), (int) scratch.size());

        if (n == 0)
            break;

        push (scratch.data(), n);
        received += n;
    }

    // With the transport stopped nothing arrives. Large host buffers also leave the
    // odd tick empty, so only treat a sustained gap as silence and let it decay.
    idleSeconds = received > 0 ? 0.0f : idleSeconds + dtSeconds;

    if (idleSeconds > 0.25f)
    {
        const auto silent = juce::jmin (fftSize, (int) (sampleRate * (double) dtSeconds));
        std::fill (scratch.begin(), scratch.end(), 0.0f);

        for (auto left = silent; left > 0; left -= (int) scratch.size())
            push (scratch.data(), juce::jmin (left, (int) scratch.size()));
    }

    const auto before = points;
    analyse (dtSeconds);
    resample();

    auto moved = 0.0f;

    for (size_t i = 0; i < points.size(); ++i)
        moved = juce::jmax (moved, std::abs (points[i] - before[i]));

    return moved > 0.02f;
}

void SpectrumAnalyzer::analyse (float dtSeconds)
{
    // Unroll the ring, oldest sample first, through the window.
    for (int i = 0; i < fftSize; ++i)
        fftData[(size_t) i] = ring[(size_t) ((writePos + i) & (fftSize - 1))] * window[(size_t) i];

    std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
    fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

    const auto binHz = sampleRate / (double) fftSize;
    const auto rise = 1.0f - std::exp (-dtSeconds / riseSeconds);
    const auto fall = fallDbPerSecond * dtSeconds;

    bins[0] = silenceDb;

    for (size_t k = 1; k < bins.size(); ++k)
    {
        const auto octaves = (float) std::log2 ((double) k * binHz / (double) tiltPivotHz);
        const auto target = juce::Decibels::gainToDecibels (fftData[k] * windowNorm, silenceDb)
                              + tiltDbPerOctave * octaves;

        auto& level = bins[k];
        level = target > level ? level + (target - level) * rise
                               : juce::jmax (target, level - fall);
    }
}

void SpectrumAnalyzer::resample()
{
    const auto binHz = sampleRate / (double) fftSize;
    const auto lastBin = (int) bins.size() - 1;
    const auto halfStep = std::pow (maxHz / minHz, 0.5 / (double) (numPoints - 1));

    const auto at = [this, lastBin] (int k) { return bins[(size_t) juce::jlimit (1, lastBin, k)]; };

    for (int i = 0; i < numPoints; ++i)
    {
        const auto f = pointFrequency (i);
        const auto position = f / binHz;
        const auto lo = f / halfStep / binHz, hi = f * halfStep / binHz;

        if (position > (double) lastBin)
        {
            points[(size_t) i] = silenceDb;
        }
        else if (hi - lo < 1.0)
        {
            // Finer than the bins: interpolate between them.
            const auto k = (int) std::floor (position);
            points[(size_t) i] = cubic (at (k - 1), at (k), at (k + 1), at (k + 2), (float) (position - (double) k));
        }
        else
        {
            // Several bins per point: show the strongest, so peaks aren't averaged away.
            auto peak = silenceDb;

            for (int k = (int) std::floor (lo + 0.5); k <= (int) std::floor (hi + 0.5); ++k)
                peak = juce::jmax (peak, at (k));

            points[(size_t) i] = peak;
        }
    }
}

} // namespace timaband::ui
