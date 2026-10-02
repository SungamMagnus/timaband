#pragma once

#include <algorithm>
#include <cmath>

namespace timaband
{

enum class Curve { exponential = 0, linear = 1, logarithmic = 2 };

// Maps a detector value in [0, 1] to how strongly the gain responds.
// Exponential reacts only to the strongest peaks; logarithmic reacts to soft ones too.
inline float applyCurve (Curve curve, float x) noexcept
{
    x = std::clamp (x, 0.0f, 1.0f);

    switch (curve)
    {
        case Curve::exponential: return std::expm1 (4.0f * x) / std::expm1 (4.0f);
        case Curve::logarithmic: return std::log1p (20.0f * x) / std::log (21.0f);
        case Curve::linear:      break;
    }

    return x;
}

// One-pole peak follower with separate attack and release times.
class EnvelopeFollower
{
public:
    void setTimes (double sampleRate, float attackMs, float releaseMs) noexcept
    {
        attackCoeff  = coefficient (sampleRate, attackMs);
        releaseCoeff = coefficient (sampleRate, releaseMs);
    }

    void reset() noexcept { env = 0.0f; }

    float process (float level) noexcept
    {
        const auto coeff = level > env ? attackCoeff : releaseCoeff;
        env = level + coeff * (env - level);
        return env;
    }

    static float coefficient (double sampleRate, float ms) noexcept
    {
        return ms > 0.0f ? (float) std::exp (-1.0 / (0.001 * (double) ms * sampleRate)) : 0.0f;
    }

private:
    float attackCoeff = 0.0f, releaseCoeff = 0.0f, env = 0.0f;
};

// Transient shaper gain computer for one detector channel of one band.
//
// Attack: two followers share a release but differ in attack time. Right after an
// onset the fast one is well above the slow one, and that gap (in dB) is the
// transient amount.
//
// Sustain: two followers share an attack but differ in release time. During a
// decay the slow one stays above the fast one, and that gap is the sustain amount.
//
// timeScale stretches every time constant, so the low band (where one cycle can be
// 25 ms) uses longer windows than the high band.
class Shaper
{
public:
    static constexpr float rangeDb   = 12.0f;  // gap that counts as a full-strength detection
    static constexpr float maxGainDb = 24.0f;

    void prepare (double sampleRate, float timeScale) noexcept
    {
        const auto ms = [timeScale] (float t) { return t * timeScale; };

        attackFast .setTimes (sampleRate, ms (0.2f), ms (60.0f));
        attackSlow .setTimes (sampleRate, ms (20.0f), ms (60.0f));
        sustainFast.setTimes (sampleRate, ms (1.0f), ms (40.0f));
        sustainSlow.setTimes (sampleRate, ms (1.0f), ms (400.0f));

        // Sustain changes slowly by nature; smoothing it removes the ripple a
        // fast-release follower picks up on bass notes.
        sustainSmoothCoeff = EnvelopeFollower::coefficient (sampleRate, ms (8.0f));
        gainSmoothCoeff    = EnvelopeFollower::coefficient (sampleRate, ms (0.5f));

        reset();
    }

    void reset() noexcept
    {
        attackFast.reset();
        attackSlow.reset();
        sustainFast.reset();
        sustainSlow.reset();
        transient = sustain = gainDb = 0.0f;
    }

    // level: rectified detector input (|x|, or max |x| across linked channels).
    // Returns the gain to apply, in dB.
    float process (float level, float attackDb, float sustainDb, Curve curve) noexcept
    {
        const auto aFast = attackFast.process (level);
        const auto aSlow = attackSlow.process (level);
        const auto sFast = sustainFast.process (level);
        const auto sSlow = sustainSlow.process (level);

        // Fade detection out between -48 and -60 dBFS so noise floors don't trigger it.
        const auto gate = std::clamp ((sSlow - gateFloor) / (gateCeiling - gateFloor), 0.0f, 1.0f);

        transient = gate * std::clamp (gapDb (aFast, aSlow) / rangeDb, 0.0f, 1.0f);

        const auto sustainTarget = gate * std::clamp (gapDb (sSlow, sFast) / rangeDb, 0.0f, 1.0f);
        sustain = sustainTarget + sustainSmoothCoeff * (sustain - sustainTarget);

        const auto target = std::clamp (attackDb  * applyCurve (curve, transient)
                                      + sustainDb * applyCurve (curve, sustain),
                                        -maxGainDb, maxGainDb);

        gainDb = target + gainSmoothCoeff * (gainDb - target);
        return gainDb;
    }

    float getTransient() const noexcept { return transient; }
    float getSustain() const noexcept   { return sustain; }

private:
    static constexpr float gateFloor   = 0.001f;    // -60 dBFS
    static constexpr float gateCeiling = 0.00398f;  // -48 dBFS
    static constexpr float epsilon     = 1.0e-6f;

    static float gapDb (float above, float below) noexcept
    {
        return 20.0f * std::log10 ((above + epsilon) / (below + epsilon));
    }

    EnvelopeFollower attackFast, attackSlow, sustainFast, sustainSlow;
    float sustainSmoothCoeff = 0.0f, gainSmoothCoeff = 0.0f;
    float transient = 0.0f, sustain = 0.0f, gainDb = 0.0f;
};

} // namespace timaband
