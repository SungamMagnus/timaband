// Offscreen render of the real editor, fed with synthetic drum audio. Dev tool:
// checks the layout without a host. Writes panel.png (the mockup's default state),
// panel-solo.png (mid soloed, high muted) and panel-bypass.png (low band bypassed)
// into the directory given as argv[1].
//
// Built with TIMABAND_THEME_SUNGAM=1 (panel_shot_sungam) it renders the Sungam recolour
// instead: plugin-sungam.png (the default state) and plugin-sungam-states.png (mid
// soloed, high muted and the low band bypassed, all at once).
#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <vector>

#include "PluginEditor.h"
#include "PluginProcessor.h"

#ifndef TIMABAND_DOCS_DIR
 #define TIMABAND_DOCS_DIR "."
#endif

#ifndef TIMABAND_THEME_SUNGAM
 #define TIMABAND_THEME_SUNGAM 0
#endif

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 512;

void setValue (TimabandProcessor& p, const juce::String& id, float value)
{
    if (auto* param = p.apvts.getParameter (id))
        param->setValueNotifyingHost (param->convertTo0to1 (value));
}

struct Audio
{
    std::vector<float> left, right;
};

// A kick, snare and hats on the mockup's 16th-note grid at 120 BPM, peaking at -8 dBFS.
Audio makeDrums (double seconds)
{
    const auto total = (size_t) (seconds * sampleRate);
    Audio out { std::vector<float> (total, 0.0f), std::vector<float> (total, 0.0f) };

    const auto step = 0.125;
    const auto twoPi = juce::MathConstants<double>::twoPi;

    for (size_t channel = 0; channel < 2; ++channel)
    {
        auto& buf = channel == 0 ? out.left : out.right;
        juce::Random rng (11 + (juce::int64) channel * 97);

        const auto noise = [&rng] { return rng.nextFloat() * 2.0f - 1.0f; };

        const auto kick = [&] (double start, float vel)
        {
            auto phase = 0.0;

            for (size_t i = (size_t) (start * sampleRate); i < total; ++i)
            {
                const auto t = (double) i / sampleRate - start;

                if (t > 0.5)
                    break;

                phase += twoPi * (48.0 + 120.0 * std::exp (-t / 0.028)) / sampleRate;
                const auto body = std::sin (phase) * std::exp (-t / 0.13);
                const auto click = t < 0.003 ? (double) noise() * 0.35 : 0.0;
                buf[i] += vel * (float) (0.95 * body + click);
            }
        };

        const auto snare = [&] (double start, float vel)
        {
            auto previous = 0.0f;

            for (size_t i = (size_t) (start * sampleRate); i < total; ++i)
            {
                const auto t = (double) i / sampleRate - start;

                if (t > 0.5)
                    break;

                const auto n = noise();
                const auto hp = n - previous;           // crude high-pass
                previous = n;

                const auto tone = std::sin (twoPi * 190.0 * t) * std::exp (-t / 0.05);
                buf[i] += vel * (float) (0.32 * hp * std::exp (-t / 0.11) + 0.5 * tone);
            }
        };

        const auto hat = [&] (double start, float vel, bool open)
        {
            auto n1 = 0.0f, n2 = 0.0f;

            for (size_t i = (size_t) (start * sampleRate); i < total; ++i)
            {
                const auto t = (double) i / sampleRate - start;

                if (t > (open ? 0.9 : 0.25))
                    break;

                const auto n0 = noise();
                const auto hp = n0 - 2.0f * n1 + n2;    // crude second-order high-pass
                n2 = n1;
                n1 = n0;

                buf[i] += vel * (float) (0.22 * hp * std::exp (-t / (open ? 0.2 : 0.03)));
            }
        };

        for (int s = 0; (double) s * step < seconds; ++s)
        {
            const auto n = s % 16, bar = s / 16;
            const auto t = (double) s * step;

            if (n == 0)  kick (t, 1.0f);
            if (n == 8)  kick (t, 0.95f);
            if (n == 10) kick (t, 0.8f);
            if (n == 4)  snare (t, 0.95f);
            if (n == 12) snare (t, 1.0f);

            if (n % 2 == 0)
            {
                const auto open = bar % 2 == 1 && n == 14;
                hat (t, open ? 0.85f : (n % 4 == 2 ? 0.65f : 0.5f), open);
            }
        }
    }

    auto peak = 0.0f;

    for (size_t i = 0; i < total; ++i)
        peak = juce::jmax (peak, std::abs (out.left[i]), std::abs (out.right[i]));

    const auto gain = juce::Decibels::decibelsToGain (-8.0f) / juce::jmax (peak, 1.0e-6f);

    for (size_t i = 0; i < total; ++i)
    {
        out.left[i] *= gain;
        out.right[i] *= gain;
    }

    return out;
}

// Runs `numBlocks` blocks starting at block `first`, ticking the editor about every 32 ms of audio.
void play (TimabandProcessor& proc, TimabandEditor& editor, const Audio& audio, size_t first, size_t numBlocks)
{
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;

    const auto blocksPerTick = 3;

    for (size_t b = 0; b < numBlocks; ++b)
    {
        const auto offset = (first + b) * (size_t) blockSize;

        for (int i = 0; i < blockSize; ++i)
        {
            const auto idx = juce::jmin (offset + (size_t) i, audio.left.size() - 1);
            buffer.setSample (0, i, audio.left[idx]);
            buffer.setSample (1, i, audio.right[idx]);
        }

        proc.processBlock (buffer, midi);

        if ((b + 1) % (size_t) blocksPerTick == 0)
            editor.tick ((float) (blocksPerTick * blockSize / sampleRate));
    }
}

void save (TimabandEditor& editor, const juce::File& file)
{
    const auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 2.0f);

    juce::FileOutputStream stream (file);
    stream.setPosition (0);
    stream.truncate();
    juce::PNGImageFormat().writeImageToStream (image, stream);
    std::printf ("%s\n", file.getFullPathName().toRawUTF8());
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::String (TIMABAND_DOCS_DIR));
    outDir.createDirectory();

    TimabandProcessor proc;
    proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
    proc.prepareToPlay (sampleRate, blockSize);

    // The mockup's default state.
    const float attack[]  = { 4.5f, 6.0f, -1.5f };
    const float sustain[] = { -2.0f, -3.5f, 2.5f };
    const int curve[]     = { 0, 1, 2 };      // exponential, linear, logarithmic

    for (int b = 0; b < timaband::numBands; ++b)
    {
        setValue (proc, timaband::pid::attack[b], attack[b]);
        setValue (proc, timaband::pid::sustain[b], sustain[b]);
        setValue (proc, timaband::pid::curve[b], (float) curve[b]);
    }

    setValue (proc, timaband::pid::xover1, 180.0f);
    setValue (proc, timaband::pid::xover2, 2400.0f);
    proc.apvts.state.setProperty (timaband::uiState::selectedBand, 1, nullptr);

    // Open the editor before any audio runs: it drains the analyzer FIFOs on opening.
    std::unique_ptr<TimabandEditor> editor (static_cast<TimabandEditor*> (proc.createEditor()));
    editor->setSize (timaband::ui::layout::designW, timaband::ui::layout::designH);

    // Two seconds of one bar, then 48 ms into the next downbeat (kick and hat), where the
    // detectors, the gain meters and the spectrum all have something to show.
    const auto audio = makeDrums (3.0);
    const size_t blocks = 192;      // 2.048 s
    play (proc, *editor, audio, 0, blocks);
   #if TIMABAND_THEME_SUNGAM
    save (*editor, outDir.getChildFile ("plugin-sungam.png"));

    // Every state at once: mid soloed (so low and high dim), high muted, low bypassed.
    setValue (proc, timaband::pid::solo[1], 1.0f);
    setValue (proc, timaband::pid::mute[2], 1.0f);
    setValue (proc, timaband::pid::bandBypass[0], 1.0f);
    play (proc, *editor, audio, blocks, 6);
    save (*editor, outDir.getChildFile ("plugin-sungam-states.png"));
   #else
    save (*editor, outDir.getChildFile ("panel.png"));

    // Mid soloed, high muted: the dimmed columns and hatched regions.
    setValue (proc, timaband::pid::solo[1], 1.0f);
    setValue (proc, timaband::pid::mute[2], 1.0f);
    play (proc, *editor, audio, blocks, 6);
    save (*editor, outDir.getChildFile ("panel-solo.png"));

    // Back to everything audible, with the low band bypassed: its column goes grey and faint,
    // its region is not hatched (audio still passes), and its power button is out.
    setValue (proc, timaband::pid::solo[1], 0.0f);
    setValue (proc, timaband::pid::mute[2], 0.0f);
    setValue (proc, timaband::pid::bandBypass[0], 1.0f);
    play (proc, *editor, audio, blocks + 6, 6);
    save (*editor, outDir.getChildFile ("panel-bypass.png"));
   #endif

    return 0;
}
