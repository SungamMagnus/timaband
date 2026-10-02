#pragma once

#include <array>

#include <juce_dsp/juce_dsp.h>

#include "../dsp/Analysis.h"

namespace timaband::ui
{

// Turns the analyzer FIFO into a smoothed, log-spaced spectrum in dBFS.
// Message thread only: the FIFO is single-consumer.
class SpectrumAnalyzer
{
public:
    static constexpr int    fftOrder = 12;
    static constexpr int    fftSize  = 1 << fftOrder;
    static constexpr int    numPoints = 300;
    static constexpr double minHz = 20.0, maxHz = 20000.0;

    // Ozone-style tilt: +4.5 dB per octave around 1 kHz.
    static constexpr float tiltDbPerOctave = 4.5f;
    static constexpr float tiltPivotHz = 1000.0f;

    // Display smoothing: bins rise quickly and fall at a fixed rate in dB per second.
    static constexpr float riseSeconds = 0.012f;
    static constexpr float fallDbPerSecond = 40.0f;

    SpectrumAnalyzer();

    void setSampleRate (double sampleRate) noexcept;

    // Drops whatever the FIFO holds and clears the display, so stale audio is never drawn.
    void discard (SampleFifo&);
    void reset();

    // Pulls new samples, analyses the latest window and advances the smoothing by
    // dtSeconds. Returns true if any point moved enough to be worth repainting.
    bool update (SampleFifo&, float dtSeconds);

    // Level of each log-spaced point, in dBFS after the tilt.
    const std::array<float, numPoints>& getPoints() const noexcept { return points; }

    static double pointFrequency (int index) noexcept;

private:
    void push (const float* samples, int num) noexcept;
    void analyse (float dtSeconds);
    void resample();

    juce::dsp::FFT fft { fftOrder };
    std::array<float, fftSize> window {};
    std::array<float, fftSize> ring {};
    std::array<float, 2 * fftSize> fftData {};
    std::array<float, fftSize / 2 + 1> bins {};         // smoothed, dBFS with tilt
    std::array<float, numPoints> points {};
    std::array<float, 1024> scratch {};

    float windowNorm = 1.0f;
    int writePos = 0;
    double sampleRate = 48000.0;
    float idleSeconds = 0.0f;
};

} // namespace timaband::ui
