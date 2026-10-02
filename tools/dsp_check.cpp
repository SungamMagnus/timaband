// Offline checks for timaband::Engine, run without a host or a plug-in wrapper.
// Each check prints PASS or FAIL with what it measured; the exit code is
// non-zero if any failed. Build with -DTIMABAND_TOOLS=ON.
//
// Levels: "-6 dBFS" and "0 dBFS" are peak amplitudes (0.5 and 1.0). Noise is
// uniform white noise with that peak.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <juce_dsp/juce_dsp.h>

#include "dsp/Engine.h"

using namespace timaband;

namespace
{
using Signal     = std::vector<float>;
using Channels   = std::vector<Signal>;
using SettingsFn = std::function<Settings (int blockStart)>;

int failures = 0;
int checksRun = 0;

#if defined (__GNUC__) || defined (__clang__)
 #define DSP_CHECK_PRINTF(fmtIndex, argIndex) __attribute__ ((format (printf, fmtIndex, argIndex)))
#else
 #define DSP_CHECK_PRINTF(fmtIndex, argIndex)
#endif

DSP_CHECK_PRINTF (1, 2) std::string fmt (const char* format, ...)
{
    char buffer[1024];
    va_list args;
    va_start (args, format);
    std::vsnprintf (buffer, sizeof (buffer), format, args);
    va_end (args);
    return buffer;
}

void verdict (bool pass, const std::string& label, const std::string& detail)
{
    ++checksRun;
    if (! pass)
        ++failures;

    std::printf ("%s  %s: %s\n", pass ? "PASS" : "FAIL", label.c_str(), detail.c_str());
}

void section (const char* title) { std::printf ("\n== %s ==\n", title); }

double toDb (double linear) noexcept { return 20.0 * std::log10 (std::max (linear, 1.0e-30)); }

// Linear amplitude as dBFS text; exact silence prints as -inf rather than a floor value.
std::string dbfs (double linear)
{
    return linear < 1.0e-30 ? std::string ("-inf dBFS") : fmt ("%.1f dBFS", toDb (linear));
}

/* ── Signals ─────────────────────────────────────────────────────────────── */

Signal whiteNoise (int n, float peak, unsigned seed)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> dist (-peak, peak);

    Signal x ((size_t) n);
    for (auto& s : x)
        s = dist (rng);
    return x;
}

Signal sineWave (int n, double sampleRate, double freq, float amp)
{
    Signal x ((size_t) n);
    for (int i = 0; i < n; ++i)
        x[(size_t) i] = amp * (float) std::sin (juce::MathConstants<double>::twoPi * freq * i / sampleRate);
    return x;
}

Signal constant (int n, float value) { return Signal ((size_t) n, value); }

Signal impulse (int n)
{
    Signal x ((size_t) n, 0.0f);
    x[0] = 1.0f;
    return x;
}

// Short noise click plus a 180 Hz sine decaying with tau = 120 ms, normalised so
// the biggest sample sits at -6 dBFS.
Signal drumHits (int total, double sampleRate, const std::vector<int>& onsets)
{
    Signal x ((size_t) total, 0.0f);
    std::mt19937 rng (7);
    std::uniform_real_distribution<float> noise (-1.0f, 1.0f);

    const auto clickLength = juce::roundToInt (sampleRate * 0.001);

    for (const auto onset : onsets)
    {
        for (int i = 0; onset + i < total; ++i)
        {
            const auto t = i / sampleRate;
            auto v = (float) (std::sin (juce::MathConstants<double>::twoPi * 180.0 * t) * std::exp (-t / 0.120));

            if (i < clickLength)
                v += noise (rng);

            x[(size_t) (onset + i)] += v;
        }
    }

    float peak = 0.0f;
    for (const auto s : x)
        peak = std::max (peak, std::abs (s));

    for (auto& s : x)
        s *= 0.5f / peak;

    return x;
}

Channels dual (const Signal& s) { return { s, s }; }

/* ── Measurement ─────────────────────────────────────────────────────────── */

double rmsOf (const Signal& x, int start, int end)
{
    double sum = 0.0;
    for (int i = start; i < end; ++i)
        sum += (double) x[(size_t) i] * x[(size_t) i];

    return std::sqrt (sum / std::max (1, end - start));
}

double peakOf (const Signal& x, int start, int end)
{
    double peak = 0.0;
    for (int i = start; i < end; ++i)
        peak = std::max (peak, (double) std::abs (x[(size_t) i]));

    return peak;
}

// Amplitude of one sinusoid in x[start, start + count), from a single DFT bin.
// Exact when the window holds a whole number of cycles.
double toneAmplitude (const Signal& x, int start, int count, double freq, double sampleRate)
{
    const auto w = juce::MathConstants<double>::twoPi * freq / sampleRate;
    double re = 0.0, im = 0.0;

    for (int i = 0; i < count; ++i)
    {
        re += x[(size_t) (start + i)] * std::cos (w * i);
        im += x[(size_t) (start + i)] * std::sin (w * i);
    }

    return 2.0 * std::hypot (re, im) / count;
}

/* ── Rendering ───────────────────────────────────────────────────────────── */

struct Rendered
{
    Channels out;
    int latency = 0;
};

// Runs the engine over the input in blocks of `block`, asking for the settings at
// the start of each block. The output is processed in place, like a host would.
Rendered render (double sampleRate, int block, const Channels& in, const SettingsFn& settingsAt)
{
    Rendered r;
    r.out = in;

    const auto numChannels = (int) in.size();
    const auto total = (int) in[0].size();

    Engine engine;
    engine.prepare (sampleRate, block, numChannels);

    for (int pos = 0; pos < total; pos += block)
    {
        const auto n = std::min (block, total - pos);

        std::array<float*, Engine::maxChannels> pointers {};
        for (int c = 0; c < numChannels; ++c)
            pointers[(size_t) c] = r.out[(size_t) c].data() + pos;

        juce::AudioBuffer<float> view (pointers.data(), numChannels, n);
        engine.process (view, settingsAt (pos));
    }

    r.latency = engine.getLatencySamples();
    return r;
}

Rendered render (double sampleRate, int block, const Channels& in, const Settings& settings)
{
    return render (sampleRate, block, in, [settings] (int) { return settings; });
}

Settings zeroSettings (bool lookahead = false)
{
    Settings s;
    s.lookahead = lookahead;
    return s;
}

/* ── a. Flat sum ─────────────────────────────────────────────────────────── */

void checkFlatSum()
{
    section ("a. Flat sum (zero settings, lookahead off, 48 kHz, impulse)");

    constexpr int order = 16;
    constexpr int size = 1 << order;
    constexpr double sampleRate = 48000.0;

    const std::array<std::array<float, 2>, 4> pairs {{ { 180.0f, 2400.0f },
                                                       { 40.0f, 51.0f },
                                                       { 1000.0f, 16000.0f },
                                                       { 8000.0f, 8000.0f * 1.26f } }};

    juce::dsp::FFT fft (order);

    for (const auto& pair : pairs)
    {
        auto s = zeroSettings();
        s.xover1 = pair[0];
        s.xover2 = pair[1];

        const auto r = render (sampleRate, 512, { impulse (size) }, s);

        std::vector<float> data ((size_t) size * 2, 0.0f);
        std::copy (r.out[0].begin(), r.out[0].end(), data.begin());
        fft.performRealOnlyForwardTransform (data.data(), true);

        auto maxDev = -1.0e9, minDev = 1.0e9, maxAt = 0.0, minAt = 0.0;

        for (int k = 0; k <= size / 2; ++k)
        {
            const auto freq = k * sampleRate / size;
            if (freq < 20.0 || freq > 20000.0)
                continue;

            const auto dev = toDb (std::hypot ((double) data[(size_t) (2 * k)], (double) data[(size_t) (2 * k + 1)]));

            if (dev > maxDev) { maxDev = dev; maxAt = freq; }
            if (dev < minDev) { minDev = dev; minAt = freq; }
        }

        verdict (maxDev <= 0.1 && minDev >= -0.1,
                 fmt ("flat sum, crossovers (%.1f, %.1f) Hz", pair[0], pair[1]),
                 fmt ("magnitude %+.5f dB (at %.1f Hz) to %+.5f dB (at %.1f Hz) over 20 Hz-20 kHz, limit +/-0.1 dB",
                      maxDev, maxAt, minDev, minAt));
    }
}

/* ── b. Latency and bypass ───────────────────────────────────────────────── */

void checkBypassDelay (double sampleRate, bool lookahead)
{
    const auto total = juce::roundToInt (sampleRate * 1.0);
    const auto switchAt = juce::roundToInt (sampleRate * 0.2);
    const auto settle = juce::roundToInt (sampleRate * 0.06);   // 10 ms crossfade plus a block

    const Channels in { whiteNoise (total, 0.5f, 11), whiteNoise (total, 0.5f, 12) };

    // Starts processing and bypasses mid-stream, so the crossfade is part of what's tested.
    const auto r = render (sampleRate, 512, in, [=] (int pos)
    {
        auto s = zeroSettings (lookahead);
        s.bypass = pos >= switchAt;
        return s;
    });

    const auto expected = Engine::latencyFor (sampleRate, lookahead);

    long mismatches = 0, compared = 0;
    double worst = 0.0;
    int firstBad = -1;

    for (size_t c = 0; c < in.size(); ++c)
    {
        for (int n = switchAt + settle; n < total; ++n)
        {
            const auto a = r.out[c][(size_t) n];
            const auto b = in[c][(size_t) (n - expected)];
            ++compared;

            if (! juce::exactlyEqual (a, b))
            {
                ++mismatches;
                worst = std::max (worst, (double) std::abs (a - b));
                if (firstBad < 0) firstBad = n;
            }
        }
    }

    const auto label = fmt ("bypass, lookahead %s, %.1f kHz", lookahead ? "on" : "off", sampleRate / 1000.0);

    verdict (r.latency == expected && mismatches == 0, label,
             fmt ("engine latency %d (expected %d); out[n] == in[n - %d] bit-exact: %ld of %ld samples differ, max |err| %.3g (%s)%s",
                  r.latency, expected, expected, mismatches, compared, worst, dbfs (worst).c_str(),
                  firstBad >= 0 ? fmt (", first at sample %d", firstBad).c_str() : ""));
}

void checkLatencyAndBypass()
{
    section ("b. Latency and bypass");

    for (const auto sampleRate : { 48000.0, 44100.0, 96000.0 })
    {
        checkBypassDelay (sampleRate, true);
        checkBypassDelay (sampleRate, false);
    }
}

/* ── c. Delta null ───────────────────────────────────────────────────────── */

void checkDeltaNull()
{
    section ("c. Delta null (zero settings, delta on, white noise -6 dBFS, 48 kHz)");

    constexpr double sampleRate = 48000.0;
    const auto total = (int) sampleRate;
    const auto settle = juce::roundToInt (sampleRate * 0.05);

    const Channels in { whiteNoise (total, 0.5f, 21), whiteNoise (total, 0.5f, 22) };

    for (const auto lookahead : { true, false })
    {
        for (const auto link : { true, false })
        {
            auto s = zeroSettings (lookahead);
            s.delta = true;
            s.stereoLink = link;

            const auto r = render (sampleRate, 512, in, s);
            const auto rms = std::max (rmsOf (r.out[0], settle, total), rmsOf (r.out[1], settle, total));

            verdict (toDb (rms) < -100.0,
                     fmt ("delta null, lookahead %s, stereoLink %s", lookahead ? "on" : "off", link ? "on" : "off"),
                     fmt ("output RMS %s (limit < -100 dBFS)", dbfs (rms).c_str()));
        }
    }
}

/* ── d. Mute and solo ────────────────────────────────────────────────────── */

// Sum of sines through the engine; the caller measures each with toneAmplitude().
Rendered renderTones (const std::vector<double>& freqs, const Settings& s)
{
    constexpr double sampleRate = 48000.0;
    constexpr int total = 4800 + 24000;

    Signal sum ((size_t) total, 0.0f);
    for (const auto f : freqs)
    {
        const auto tone = sineWave (total, sampleRate, f, 0.5f);
        for (size_t i = 0; i < sum.size(); ++i)
            sum[i] += tone[i];
    }

    return render (sampleRate, 512, dual (sum), s);
}

void checkMuteSolo()
{
    section ("d. Mute and solo (sines at -6 dBFS, measured 100 ms after start)");

    constexpr double sampleRate = 48000.0;
    constexpr int settle = 4800, window = 24000;

    const auto gainOf = [&] (const Rendered& r, double f)
    {
        return toDb (toneAmplitude (r.out[0], settle, window, f, sampleRate) / 0.5);
    };

    {
        auto s = zeroSettings();
        s.bands[0].solo = true;

        const auto low  = gainOf (renderTones ({ 50.0 }, s), 50.0);
        const auto high = gainOf (renderTones ({ 5000.0 }, s), 5000.0);

        verdict (std::abs (low) <= 0.5, "solo low, 50 Hz passes", fmt ("gain %+.3f dB, limit +/-0.5 dB", low));
        verdict (high <= -40.0, "solo low, 5 kHz is attenuated", fmt ("gain %+.1f dB, limit <= -40 dB", high));
    }

    {
        auto s = zeroSettings();
        s.bands[1].mute = true;

        const auto mid = gainOf (renderTones ({ 800.0 }, s), 800.0);
        verdict (mid <= -30.0, "mute mid, 800 Hz is attenuated", fmt ("gain %+.1f dB, limit <= -30 dB", mid));
    }

    {
        auto s = zeroSettings();
        for (auto& band : s.bands)
            band.mute = true;

        const Channels in { whiteNoise (settle + window, 0.5f, 31), whiteNoise (settle + window, 0.5f, 32) };
        const auto r = render (sampleRate, 512, in, s);
        const auto peak = std::max (peakOf (r.out[0], settle, settle + window), peakOf (r.out[1], settle, settle + window));

        verdict (toDb (peak) < -120.0, "mute all bands", fmt ("output peak %s (limit < -120 dBFS)", dbfs (peak).c_str()));
    }

    // Each band's sine sits well inside it (low 50 Hz, mid 800 Hz, high 8 kHz, so the
    // crossover slopes cost under 0.1 dB); the pass band is +/-0.5 dB like the solo test.
    const std::array<double, 3> bandTone { 50.0, 800.0, 8000.0 };
    const std::array<const char*, 3> bandName { "low", "mid", "high" };

    for (size_t a = 0; a < 3; ++a)
    {
        for (size_t b = a + 1; b < 3; ++b)
        {
            auto s = zeroSettings();
            s.bands[a].solo = true;
            s.bands[b].solo = true;

            const auto r = renderTones ({ bandTone[a], bandTone[b] }, s);
            const auto ga = gainOf (r, bandTone[a]);
            const auto gb = gainOf (r, bandTone[b]);

            verdict (std::abs (ga) <= 0.5 && std::abs (gb) <= 0.5,
                     fmt ("solo %s + %s, both pass", bandName[a], bandName[b]),
                     fmt ("%.0f Hz gain %+.3f dB, %.0f Hz gain %+.3f dB, limit +/-0.5 dB",
                          bandTone[a], ga, bandTone[b], gb));
        }
    }
}

/* ── e. Shaping works ────────────────────────────────────────────────────── */

void checkShaping()
{
    section ("e. Shaping works (drum hits every 500 ms, all bands equal, Linear, lookahead on)");

    constexpr double sampleRate = 48000.0;
    constexpr int numHits = 6;
    const auto total = juce::roundToInt (sampleRate * 3.5);

    std::vector<int> onsets;
    for (int k = 0; k < numHits; ++k)
        onsets.push_back (juce::roundToInt (sampleRate * (0.25 + 0.5 * k)));

    const auto input = dual (drumHits (total, sampleRate, onsets));
    const auto ms = [&] (double t) { return juce::roundToInt (sampleRate * t * 0.001); };

    const auto baseline = render (sampleRate, 512, input, zeroSettings (true));
    const auto latency = baseline.latency;

    struct Case { const char* name; float attack, sustain; bool measureAttack; double minDeltaDb; };

    const std::array<Case, 4> cases {{ { "attack +12 dB",  12.0f,   0.0f, true,   3.0 },
                                       { "attack -12 dB", -12.0f,   0.0f, true,  -3.0 },
                                       { "sustain -12 dB",  0.0f, -12.0f, false, -3.0 },
                                       { "sustain +12 dB",  0.0f,  12.0f, false,  3.0 } }};

    for (const auto& c : cases)
    {
        auto s = zeroSettings (true);
        for (auto& band : s.bands)
        {
            band.attackDb = c.attack;
            band.sustainDb = c.sustain;
            band.curve = Curve::linear;
        }

        const auto r = render (sampleRate, 512, input, s);

        std::vector<double> deltas;
        for (const auto onset : onsets)
        {
            // The output is the input delayed by the latency, so line the windows up on that.
            const auto from = onset + latency + (c.measureAttack ? 0 : ms (150.0));
            const auto to   = onset + latency + (c.measureAttack ? ms (5.0) : ms (400.0));

            const auto shaped = c.measureAttack ? peakOf (r.out[0], from, to) : rmsOf (r.out[0], from, to);
            const auto base   = c.measureAttack ? peakOf (baseline.out[0], from, to) : rmsOf (baseline.out[0], from, to);
            deltas.push_back (toDb (shaped) - toDb (base));
        }

        double mean = 0.0;
        for (const auto d : deltas)
            mean += d / (double) deltas.size();

        std::string perHit;
        for (const auto d : deltas)
            perHit += fmt (" %+.2f", d);

        const auto pass = c.minDeltaDb > 0.0 ? mean >= c.minDeltaDb : mean <= c.minDeltaDb;

        verdict (pass, c.name,
                 fmt ("%s delta %+.2f dB mean over %d hits (need %s %.0f dB); per hit:%s",
                      c.measureAttack ? "0-5 ms onset peak" : "150-400 ms RMS", mean, numHits,
                      c.minDeltaDb > 0.0 ? ">=" : "<=", c.minDeltaDb, perHit.c_str()));
    }
}

/* ── f. Curves ───────────────────────────────────────────────────────────── */

void checkCurves()
{
    section ("f. Curves");

    const std::array<Curve, 3> curves { Curve::exponential, Curve::linear, Curve::logarithmic };
    const std::array<const char*, 3> names { "exponential", "linear", "logarithmic" };

    for (size_t i = 0; i < curves.size(); ++i)
    {
        const auto at0 = applyCurve (curves[i], 0.0f);
        const auto at1 = applyCurve (curves[i], 1.0f);

        auto monotonic = true;
        auto previous = applyCurve (curves[i], 0.0f);
        for (int step = 1; step <= 1000; ++step)
        {
            const auto v = applyCurve (curves[i], (float) step / 1000.0f);
            if (v < previous)
                monotonic = false;
            previous = v;
        }

        verdict (std::abs (at0) < 1.0e-6f && std::abs (at1 - 1.0f) < 1.0e-6f && monotonic,
                 fmt ("curve %s", names[i]),
                 fmt ("f(0) = %.8f, f(1) = %.8f, monotonic over 1000 steps: %s", at0, at1, monotonic ? "yes" : "no"));
    }

    const auto e = applyCurve (Curve::exponential, 0.5f);
    const auto l = applyCurve (Curve::linear, 0.5f);
    const auto g = applyCurve (Curve::logarithmic, 0.5f);

    verdict (e < l && l < g, "curve order at x = 0.5",
             fmt ("exponential %.4f < linear %.4f < logarithmic %.4f", e, l, g));
}

/* ── g. Robustness ───────────────────────────────────────────────────────── */

struct Scenario
{
    std::string name;
    int channels;
    std::function<SettingsFn (double sampleRate)> make;
    float gainDb = 0.0f;   // the most input + output gain the scenario can reach
};

Settings bandsAt (float attack, float sustain, Curve curve)
{
    auto s = zeroSettings (true);
    for (auto& band : s.bands)
    {
        band.attackDb = attack;
        band.sustainDb = sustain;
        band.curve = curve;
    }
    return s;
}

std::function<SettingsFn (double)> fixed (const Settings& s)
{
    return [s] (double) { return SettingsFn ([s] (int) { return s; }); };
}

// Flips random switches, crossovers and band settings every few milliseconds,
// starting from extreme band settings.
SettingsFn makeToggler (double sampleRate)
{
    struct State
    {
        Settings s = bandsAt (15.0f, 15.0f, Curve::exponential);
        std::mt19937 rng { 1234 };
        int next = 0;
    };

    auto state = std::make_shared<State>();
    const auto interval = std::max (1, juce::roundToInt (sampleRate * 0.004));

    return [state, interval] (int pos)
    {
        auto& st = *state;
        auto randomFloat = [&st] (float lo, float hi) { return std::uniform_real_distribution<float> (lo, hi) (st.rng); };

        while (pos >= st.next)
        {
            st.next += interval;

            switch (st.rng() % 12)
            {
                case 0:  st.s.lookahead  = ! st.s.lookahead;  break;
                case 1:  st.s.stereoLink = ! st.s.stereoLink; break;
                case 2:  st.s.delta      = ! st.s.delta;      break;
                case 3:  st.s.bypass     = ! st.s.bypass;     break;
                case 4:  case 5:  case 6:
                {
                    auto& flag = st.s.bands[st.rng() % 3].mute;
                    flag = ! flag;
                    break;
                }
                case 7:  case 8:  case 9:
                {
                    auto& flag = st.s.bands[st.rng() % 3].solo;
                    flag = ! flag;
                    break;
                }
                case 10:
                    st.s.xover1 = 40.0f * std::pow (400.0f, randomFloat (0.0f, 1.0f));
                    st.s.xover2 = 40.0f * std::pow (400.0f, randomFloat (0.0f, 1.0f));
                    break;
                default:
                {
                    auto& band = st.s.bands[st.rng() % 3];
                    band.attackDb  = randomFloat (-15.0f, 15.0f);
                    band.sustainDb = randomFloat (-15.0f, 15.0f);
                    band.curve     = (Curve) (st.rng() % 3);
                    st.s.inputGainDb  = randomFloat (-24.0f, 24.0f);
                    st.s.outputGainDb = randomFloat (-24.0f, 24.0f);
                    break;
                }
            }
        }

        return st.s;
    };
}

// Jumps both crossovers between the extremes of their range every 7 ms.
SettingsFn makeCrossoverJumper (double sampleRate)
{
    const auto interval = std::max (1, juce::roundToInt (sampleRate * 0.007));

    return [interval] (int pos)
    {
        static constexpr std::array<std::array<float, 2>, 4> positions {{ { 40.0f, 16000.0f }, { 16000.0f, 40.0f },
                                                                           { 40.0f, 40.0f }, { 16000.0f, 16000.0f } }};
        auto s = bandsAt (15.0f, 15.0f, Curve::linear);
        const auto& p = positions[(size_t) ((pos / interval) % 4)];
        s.xover1 = p[0];
        s.xover2 = p[1];
        return s;
    };
}

std::vector<Scenario> makeScenarios()
{
    std::vector<Scenario> list;

    const auto add = [&list] (const std::string& name, const Settings& s)
    {
        list.push_back ({ name, 2, fixed (s), s.inputGainDb + s.outputGainDb });
    };

    add ("all bands +15/+15 linear",     bandsAt (15.0f, 15.0f, Curve::linear));
    add ("all bands -15/-15 exp",        bandsAt (-15.0f, -15.0f, Curve::exponential));
    add ("all bands +15/-15 log",        bandsAt (15.0f, -15.0f, Curve::logarithmic));
    add ("all bands -15/+15 exp",        bandsAt (-15.0f, 15.0f, Curve::exponential));

    {
        auto s = zeroSettings (true);
        s.bands[0] = { 15.0f, -15.0f, Curve::exponential, false, false };
        s.bands[1] = { -15.0f, 15.0f, Curve::linear, false, false };
        s.bands[2] = { 15.0f, 15.0f, Curve::logarithmic, false, false };
        add ("bands mixed per band", s);
    }

    const std::array<std::array<float, 2>, 5> crossovers {{ { 40.0f, 40.0f }, { 16000.0f, 16000.0f },
                                                            { 40.0f, 16000.0f }, { 10000.0f, 100.0f },
                                                            { 16000.0f, 40.0f } }};
    for (const auto& x : crossovers)
    {
        auto s = bandsAt (15.0f, 15.0f, Curve::linear);
        s.xover1 = x[0];
        s.xover2 = x[1];
        add (fmt ("xover %.0f / %.0f, bands +15/+15", x[0], x[1]), s);
    }

    const auto gains = [] (float in, float out, float shape)
    {
        auto s = bandsAt (shape, shape, Curve::linear);
        s.inputGainDb = in;
        s.outputGainDb = out;
        return s;
    };

    add ("gains +24/+24, bands 0",       gains (24.0f, 24.0f, 0.0f));
    add ("gains -24/-24, bands +15",     gains (-24.0f, -24.0f, 15.0f));
    add ("gains +24/-24, bands +15",     gains (24.0f, -24.0f, 15.0f));
    add ("gains -24/+24, bands +15",     gains (-24.0f, 24.0f, 15.0f));
    add ("gains +24/+24, bands +15",     gains (24.0f, 24.0f, 15.0f));

    list.push_back ({ "crossovers jump between extremes", 2, [] (double sr) { return makeCrossoverJumper (sr); } });
    list.push_back ({ "toggles mid-stream, stereo",        2, [] (double sr) { return makeToggler (sr); }, 48.0f });
    list.push_back ({ "toggles mid-stream, mono",          1, [] (double sr) { return makeToggler (sr); }, 48.0f });

    return list;
}

void checkRobustness()
{
    section ("g. Robustness (finite, peak within the settings' maximum gain + 12 dB)");

    const auto scenarios = makeScenarios();
    const std::array<double, 5> sampleRates { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };
    const std::array<int, 5> blockSizes { 1, 7, 64, 512, 4096 };
    const std::array<const char*, 3> inputNames { "silence", "noise 0 dBFS", "DC 0.5" };

    // A 0 dBFS input can legitimately reach input + output gain plus the shaper's
    // maximum boost; 12 dB on top allows for crossover overshoot. Anything louder
    // means the gain path is misbehaving.
    constexpr float overshootDb = 12.0f;
    const auto limitDb = [] (const Scenario& s) { return s.gainDb + Shaper::maxGainDb + overshootDb; };

    struct Worst { double peak = 0.0; std::string where; long tooLoud = 0; };
    std::map<std::string, Worst> worstPerScenario;
    Worst worstOverall;

    long runs = 0, nonFinite = 0, tooLoud = 0;
    int reported = 0;

    for (const auto sampleRate : sampleRates)
    {
        const auto total = juce::roundToInt (sampleRate * 0.3);

        const std::array<Channels, 3> inputs {{ { constant (total, 0.0f), constant (total, 0.0f) },
                                                { whiteNoise (total, 1.0f, 41), whiteNoise (total, 1.0f, 42) },
                                                { constant (total, 0.5f), constant (total, 0.5f) } }};

        for (size_t in = 0; in < inputs.size(); ++in)
        {
            for (const auto block : blockSizes)
            {
                for (const auto& scenario : scenarios)
                {
                    Channels input = inputs[in];
                    input.resize ((size_t) scenario.channels);

                    const auto r = render (sampleRate, block, input, scenario.make (sampleRate));
                    ++runs;

                    double peak = 0.0;
                    auto finite = true;
                    for (const auto& channel : r.out)
                    {
                        for (const auto s : channel)
                        {
                            if (! std::isfinite (s))
                                finite = false;
                            else
                                peak = std::max (peak, (double) std::abs (s));
                        }
                    }

                    const auto where = fmt ("%s, %.1f kHz, block %d, input %s", scenario.name.c_str(),
                                            sampleRate / 1000.0, block, inputNames[in]);

                    auto& worst = worstPerScenario[scenario.name];
                    if (peak > worst.peak) { worst.peak = peak; worst.where = where; }
                    if (peak > worstOverall.peak) { worstOverall.peak = peak; worstOverall.where = where; }

                    if (peak >= juce::Decibels::decibelsToGain ((double) limitDb (scenario)))
                    {
                        ++tooLoud;
                        ++worst.tooLoud;
                    }

                    if (! finite)
                    {
                        ++nonFinite;
                        if (reported++ < 20)
                            std::printf ("      NON-FINITE output: %s\n", where.c_str());
                    }
                }
            }
        }
    }

    std::string loudScenarios;

    for (const auto& scenario : scenarios)
    {
        const auto& w = worstPerScenario[scenario.name];
        std::printf ("      worst peak %+7.1f dBFS (limit %+.0f)  %s  [%s]%s\n", toDb (w.peak), (double) limitDb (scenario),
                     scenario.name.c_str(), w.where.c_str(),
                     w.tooLoud > 0 ? fmt ("  <-- %ld runs over the limit", w.tooLoud).c_str() : "");

        if (w.tooLoud > 0)
            loudScenarios += (loudScenarios.empty() ? "" : "; ") + scenario.name;
    }

    const auto setup = fmt ("%ld runs (%zu scenarios x %zu sample rates x %zu block sizes x 3 inputs)",
                            runs, scenarios.size(), sampleRates.size(), blockSizes.size());

    verdict (nonFinite == 0, "robustness, finite output",
             fmt ("%s; %ld with non-finite output", setup.c_str(), nonFinite));

    verdict (tooLoud == 0, "robustness, peak within each scenario's limit",
             fmt ("%ld runs over their limit; worst peak %+.1f dBFS [%s]%s%s",
                  tooLoud, toDb (worstOverall.peak), worstOverall.where.c_str(),
                  tooLoud > 0 ? "; scenarios over the limit: " : "", loudScenarios.c_str()));
}

/* ── h. Unlinked stereo ──────────────────────────────────────────────────── */

double maxDifference (const Signal& a, const Signal& b)
{
    double worst = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
        worst = std::max (worst, (double) std::abs (a[i] - b[i]));
    return worst;
}

void checkUnlinkedStereo()
{
    section ("h. Unlinked stereo (hits in the left channel only)");

    constexpr double sampleRate = 48000.0;
    const auto total = juce::roundToInt (sampleRate * 3.5);

    std::vector<int> onsets;
    for (int k = 0; k < 6; ++k)
        onsets.push_back (juce::roundToInt (sampleRate * (0.25 + 0.5 * k)));

    const auto hits = drumHits (total, sampleRate, onsets);
    const Signal silence ((size_t) total, 0.0f);
    const auto tone = sineWave (total, sampleRate, 1000.0, 0.1f);   // -20 dBFS

    auto shaped = bandsAt (12.0f, 12.0f, Curve::linear);
    shaped.stereoLink = false;

    auto zero = zeroSettings (true);
    zero.stereoLink = false;

    // As specified: the right channel is silent, and must match its zero-settings output.
    {
        const auto in = Channels { hits, silence };
        const auto withShaping = render (sampleRate, 512, in, shaped);
        const auto reference = render (sampleRate, 512, in, zero);

        const auto right = maxDifference (withShaping.out[1], reference.out[1]);
        const auto left = maxDifference (withShaping.out[0], reference.out[0]);

        verdict (right <= 1.0e-4 && left > 1.0e-2, "right channel matches zero settings",
                 fmt ("max |right - right at zero settings| = %.3g (limit 1e-4); left channel differs by %.3g, so shaping was active",
                      right, left));
    }

    // Stronger: give the right channel a signal of its own. Its output must not
    // depend on what the left channel does.
    {
        const auto withHits = render (sampleRate, 512, Channels { hits, tone }, shaped);
        const auto withoutHits = render (sampleRate, 512, Channels { silence, tone }, shaped);
        const auto diff = maxDifference (withHits.out[1], withoutHits.out[1]);

        verdict (diff <= 1.0e-4, "right channel (own signal) ignores the left channel",
                 fmt ("max |right with left hits - right with left silent| = %.3g (limit 1e-4)", diff));

        // Control: with the link on, the left hits must reach the right channel, or the test above proves nothing.
        auto linked = shaped;
        linked.stereoLink = true;

        const auto linkedHits = render (sampleRate, 512, Channels { hits, tone }, linked);
        const auto linkedSilent = render (sampleRate, 512, Channels { silence, tone }, linked);
        const auto linkedDiff = maxDifference (linkedHits.out[1], linkedSilent.out[1]);

        verdict (linkedDiff > 1.0e-4, "control: stereoLink on couples the channels",
                 fmt ("max |right with left hits - right with left silent| = %.3g (must exceed 1e-4)", linkedDiff));
    }
}

} // namespace

int main()
{
    const juce::ScopedNoDenormals noDenormals;   // as in the plug-in's processBlock

    std::printf ("Timaband dsp_check\n");

    checkFlatSum();
    checkLatencyAndBypass();
    checkDeltaNull();
    checkMuteSolo();
    checkShaping();
    checkCurves();
    checkRobustness();
    checkUnlinkedStereo();

    std::printf ("\n%d of %d checks passed\n", checksRun - failures, checksRun);
    return failures == 0 ? 0 : 1;
}
