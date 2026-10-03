// Headless checks of the editor's behaviour: parameter gestures, link bands, the
// crossover handles, M/S audibility, state persistence, and the spectrum maths.
// Mouse events are delivered straight to the components, so no window is needed.
#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <map>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ui/Spectrum.h"

namespace
{
int failures = 0, checks = 0;

void check (bool ok, const juce::String& what)
{
    ++checks;

    if (! ok)
        ++failures;

    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
}

bool near (double a, double b, double tol = 1.0e-3) { return std::abs (a - b) <= tol; }

juce::Component* findByTitle (juce::Component& root, const juce::String& title)
{
    for (auto* child : root.getChildren())
    {
        if (child->getTitle() == title)
            return child;

        if (auto* found = findByTitle (*child, title))
            return found;
    }

    return nullptr;
}

juce::MouseEvent makeEvent (juce::Component& c, juce::Point<float> position, juce::Point<float> downPosition,
                            int clicks = 1, bool dragged = false, juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier)
{
    const auto now = juce::Time::getCurrentTime();
    return { juce::Desktop::getInstance().getMainMouseSource(), position, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
             &c, &c, now, downPosition, now, clicks, dragged };
}

void drag (juce::Component& c, juce::Point<float> from, juce::Point<float> to)
{
    c.mouseDown (makeEvent (c, from, from));
    c.mouseDrag (makeEvent (c, to, from, 1, true));
    c.mouseUp (makeEvent (c, to, from, 1, true));
}

// triggerClick() posts a message; let it arrive.
void click (juce::Component* c)
{
    static_cast<juce::Button*> (c)->triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
}

// Counts gesture begin/end per parameter.
struct GestureLog final : juce::AudioProcessorParameter::Listener
{
    std::map<juce::String, int> begins, ends;

    void parameterValueChanged (int, float) override {}

    void parameterGestureChanged (int index, bool starting) override
    {
        const auto id = juce::String (index);
        (starting ? begins : ends)[id]++;
    }
};

float real (TimabandProcessor& p, const juce::String& id)
{
    auto* param = p.apvts.getParameter (id);
    return param->convertFrom0to1 (param->getValue());
}

void set (TimabandProcessor& p, const juce::String& id, float value)
{
    auto* param = p.apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

void spectrumChecks()
{
    using timaband::ui::SpectrumAnalyzer;

    const auto sr = 48000.0;
    const auto binHz = sr / (double) SpectrumAnalyzer::fftSize;

    const auto peakOf = [&] (double hz, float amplitude)
    {
        SpectrumAnalyzer analyzer;
        timaband::SampleFifo fifo;
        analyzer.setSampleRate (sr);

        // Four ticks of 1/30 s of a continuous sine, enough to settle the rise smoothing.
        const auto perTick = (size_t) (sr / 30.0);
        std::vector<float> samples (perTick);
        size_t n = 0;

        for (int tick = 0; tick < 12; ++tick)
        {
            for (auto& x : samples)
                x = amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * hz * (double) n++ / sr);

            fifo.push (samples.data(), (int) samples.size());
            analyzer.update (fifo, 1.0f / 30.0f);
        }

        const auto& pts = analyzer.getPoints();
        const auto it = std::max_element (pts.begin(), pts.end());
        return std::pair<float, double> (*it, SpectrumAnalyzer::pointFrequency ((int) (it - pts.begin())));
    };

    // A full-scale sine centred on a bin reads 0 dBFS (no tilt at 1 kHz).
    const auto onePk = peakOf (85.0 * binHz, 1.0f);
    check (near (onePk.first, 0.0, 0.6), "full-scale sine at ~1 kHz reads 0 dBFS (got " + juce::String (onePk.first, 2) + ")");
    check (std::abs (onePk.second - 85.0 * binHz) < 40.0, "its peak sits at the right frequency (" + juce::String (onePk.second, 0) + " Hz)");

    const auto half = peakOf (85.0 * binHz, 0.5f);
    check (near (half.first, -6.02, 0.6), "half-scale sine reads -6 dBFS (got " + juce::String (half.first, 2) + ")");

    // The tilt: +4.5 dB per octave about 1 kHz.
    const auto hf = peakOf (853.0 * binHz, 1.0f);     // ~10 kHz
    const auto expected = 4.5 * std::log2 (853.0 * binHz / 1000.0);
    check (near (hf.first, expected, 1.5), "tilt at ~10 kHz is +" + juce::String (expected, 1) + " dB (got " + juce::String (hf.first, 2) + ")");

    // Silence decays at a fixed rate: 100 ms later, 4 dB down.
    SpectrumAnalyzer analyzer;
    timaband::SampleFifo fifo;
    analyzer.setSampleRate (sr);
    std::vector<float> loud (8192);
    for (size_t i = 0; i < loud.size(); ++i)
        loud[i] = (float) std::sin (juce::MathConstants<double>::twoPi * 85.0 * binHz * (double) i / sr);
    fifo.push (loud.data(), (int) loud.size());
    analyzer.update (fifo, 1.0f / 30.0f);
    analyzer.update (fifo, 1.0f / 30.0f);
    const auto before = *std::max_element (analyzer.getPoints().begin(), analyzer.getPoints().end());

    std::vector<float> silence (8192, 0.0f);
    fifo.push (silence.data(), (int) silence.size());
    analyzer.update (fifo, 0.1f);
    const auto after = *std::max_element (analyzer.getPoints().begin(), analyzer.getPoints().end());
    check (near (before - after, 4.0, 0.5), "bins fall at 40 dB/s (fell " + juce::String (before - after, 2) + " dB in 100 ms)");
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    spectrumChecks();

    TimabandProcessor proc;
    proc.setRateAndBufferSizeDetails (48000.0, 512);
    proc.prepareToPlay (48000.0, 512);

    GestureLog log;
    for (auto* param : proc.getParameters())
        param->addListener (&log);

    const auto indexOf = [&proc] (const juce::String& id)
    {
        return juce::String (dynamic_cast<juce::AudioProcessorParameterWithID*> (proc.apvts.getParameter (id))->getParameterIndex());
    };

    std::unique_ptr<TimabandEditor> editor (static_cast<TimabandEditor*> (proc.createEditor()));
    editor->setSize (1120, 640);
    editor->tick (1.0f / 30.0f);

    // Size and persistence.
    editor->setSize (672, 384);
    check (near ((double) proc.apvts.state.getProperty (timaband::uiState::editorScale), 0.6, 1.0e-3), "scale 0.6 is stored in the state tree");
    editor->setSize (1680, 960);
    check (near ((double) proc.apvts.state.getProperty (timaband::uiState::editorScale), 1.5, 1.0e-3), "scale 1.5 is stored in the state tree");
    editor->setSize (1120, 640);

    {
        // A second editor opens at the remembered scale.
        proc.apvts.state.setProperty (timaband::uiState::editorScale, 1.25, nullptr);
        std::unique_ptr<TimabandEditor> second (static_cast<TimabandEditor*> (proc.createEditor()));
        check (second->getWidth() == 1400 && second->getHeight() == 800, "a new editor opens at the stored scale (1400 x 800, got " + juce::String (second->getWidth()) + " x " + juce::String (second->getHeight()) + ")");
        proc.apvts.state.setProperty (timaband::uiState::editorScale, 1.0, nullptr);
    }

    auto* attack = static_cast<timaband::ui::BipolarSlider*> (findByTitle (*editor, "Mid band attack"));
    check (attack != nullptr, "found the mid attack slider");

    if (attack == nullptr)
        return 1;

    // Slider: a drag from the thumb is relative, 120 px is the full 30 dB, and it is one gesture.
    set (proc, timaband::pid::attack[1], 0.0f);
    const auto thumb = attack->getThumbBounds().getCentre();
    log.begins.clear(); log.ends.clear();
    drag (*attack, thumb, thumb.translated (0.0f, -12.0f));
    check (near (real (proc, timaband::pid::attack[1]), 3.0, 0.11), "dragging the thumb up 12 px adds 3 dB (got " + juce::String (real (proc, timaband::pid::attack[1]), 2) + ")");
    check (log.begins[indexOf (timaband::pid::attack[1])] == 1 && log.ends[indexOf (timaband::pid::attack[1])] == 1, "the drag is one begin/end gesture");

    // A press on the track jumps there.
    const auto top = juce::Point<float> ((float) attack->getWidth() * 0.5f, 11.0f);
    attack->mouseDown (makeEvent (*attack, top, top));
    attack->mouseUp (makeEvent (*attack, top, top));
    check (near (real (proc, timaband::pid::attack[1]), 15.0, 0.11), "a click at the top of the track jumps to +15 dB");

    // Double-click resets.
    attack->mouseDoubleClick (makeEvent (*attack, thumb, thumb, 2));
    check (near (real (proc, timaband::pid::attack[1]), 0.0, 1.0e-4), "double-click returns the slider to 0 dB");

    // Keys.
    attack->keyPressed (juce::KeyPress (juce::KeyPress::upKey));
    check (near (real (proc, timaband::pid::attack[1]), 0.5, 1.0e-3), "Up arrow adds 0.5 dB");
    attack->keyPressed (juce::KeyPress (juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier, 0));
    check (near (real (proc, timaband::pid::attack[1]), 0.4, 1.0e-3), "Shift+Down subtracts 0.1 dB");
    attack->keyPressed (juce::KeyPress (juce::KeyPress::endKey));
    check (near (real (proc, timaband::pid::attack[1]), 15.0, 1.0e-3), "End goes to +15 dB");
    attack->keyPressed (juce::KeyPress (juce::KeyPress::homeKey));
    check (near (real (proc, timaband::pid::attack[1]), -15.0, 1.0e-3), "Home goes to -15 dB");

    // Reset button: all six to zero, each its own gesture.
    for (int b = 0; b < 3; ++b)
    {
        set (proc, timaband::pid::attack[b], 3.0f + (float) b);
        set (proc, timaband::pid::sustain[b], -4.0f - (float) b);
    }

    log.begins.clear(); log.ends.clear();
    click (findByTitle (*editor, "Reset attack and sustain"));
    auto allZero = true, allGestures = true;

    for (int b = 0; b < 3; ++b)
        for (const auto& id : { timaband::pid::attack[b], timaband::pid::sustain[b] })
        {
            allZero = allZero && near (real (proc, id), 0.0, 1.0e-4);
            allGestures = allGestures && log.begins[indexOf (id)] == 1 && log.ends[indexOf (id)] == 1;
        }

    check (allZero, "Reset sets all six attack/sustain values to 0");
    check (allGestures, "Reset wraps each parameter in a begin/end gesture");

    // Link bands.
    auto* linkButton = findByTitle (*editor, "Link bands");
    set (proc, timaband::pid::attack[0], 2.0f);
    set (proc, timaband::pid::attack[1], 0.0f);
    set (proc, timaband::pid::attack[2], 14.0f);
    click (linkButton);
    check ((bool) proc.apvts.state.getProperty (timaband::uiState::linkBands), "Link bands is stored in the state tree");

    log.begins.clear(); log.ends.clear();
    const auto thumb2 = attack->getThumbBounds().getCentre();
    drag (*attack, thumb2, thumb2.translated (0.0f, -20.0f));      // +5 dB on mid
    check (near (real (proc, timaband::pid::attack[1]), 5.0, 0.11), "linked: the dragged band moves by 5 dB");
    check (near (real (proc, timaband::pid::attack[0]), 7.0, 0.11), "linked: low follows by the same 5 dB");
    check (near (real (proc, timaband::pid::attack[2]), 15.0, 0.11), "linked: high follows and clamps at +15 dB");
    check (log.begins[indexOf (timaband::pid::attack[0])] == 1 && log.ends[indexOf (timaband::pid::attack[0])] == 1
             && log.begins[indexOf (timaband::pid::attack[2])] == 1 && log.ends[indexOf (timaband::pid::attack[2])] == 1
             && log.begins[indexOf (timaband::pid::attack[1])] == 1 && log.ends[indexOf (timaband::pid::attack[1])] == 1,
           "linked: all three bands get one gesture");

    set (proc, timaband::pid::sustain[0], 0.0f);
    check (near (real (proc, timaband::pid::sustain[1]), 0.0, 1.0e-4), "linked: moving host-side does not drag the others");
    click (linkButton);
    check (! (bool) proc.apvts.state.getProperty (timaband::uiState::linkBands), "Link bands turns off again");

    drag (*attack, attack->getThumbBounds().getCentre(), attack->getThumbBounds().getCentre().translated (0.0f, 20.0f));
    check (near (real (proc, timaband::pid::attack[0]), 7.0, 0.11), "unlinked: other bands stay put");

    // Crossover handles.
    auto* h1 = findByTitle (*editor, "Low and mid crossover frequency");
    auto* h2 = findByTitle (*editor, "Mid and high crossover frequency");
    check (h1 != nullptr && h2 != nullptr, "found both crossover handles");
    set (proc, timaband::pid::xover1, 180.0f);
    set (proc, timaband::pid::xover2, 2400.0f);

    const auto centre = [] (juce::Component& c) { return juce::Point<float> (c.getWidth() * 0.5f, 20.0f); };
    log.begins.clear(); log.ends.clear();
    {
        // Drag handle 1 to the right by one octave's worth of pixels.
        const auto octavePx = 920.0f * std::log (2.0f) / std::log (1000.0f);
        drag (*h1, centre (*h1), centre (*h1).translated (octavePx, 0.0f));
        check (near (real (proc, timaband::pid::xover1), 360.0, 12.0), "dragging a handle one octave of pixels doubles the frequency (got " + juce::String (real (proc, timaband::pid::xover1), 1) + ")");
        check (log.begins[indexOf (timaband::pid::xover1)] == 1 && log.ends[indexOf (timaband::pid::xover1)] == 1, "handle drag is one gesture");

        // And far past the other handle: pinned a third of an octave short of it.
        drag (*h1, centre (*h1), centre (*h1).translated (800.0f, 0.0f));
        check (near (real (proc, timaband::pid::xover1), 2400.0 / 1.26, 2.0), "handle 1 stops 1/3 octave below handle 2 (got " + juce::String (real (proc, timaband::pid::xover1), 1) + ")");
    }

    h1->mouseDoubleClick (makeEvent (*h1, centre (*h1), centre (*h1), 2));
    check (near (real (proc, timaband::pid::xover1), 180.0, 0.5), "double-click resets handle 1 to 180 Hz");

    h2->keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
    check (near (real (proc, timaband::pid::xover2), 2400.0 * std::pow (2.0, 1.0 / 24.0), 2.0), "Right arrow moves a handle 1/24 octave");
    h2->keyPressed (juce::KeyPress (juce::KeyPress::leftKey, juce::ModifierKeys::shiftModifier, 0));
    check (near (real (proc, timaband::pid::xover2), 2400.0 * std::pow (2.0, 1.0 / 24.0 - 1.0 / 6.0), 2.0), "Shift+Left moves it 1/6 octave back");

    h1->keyPressed (juce::KeyPress (juce::KeyPress::homeKey));
    check (near (real (proc, timaband::pid::xover1), 40.0, 0.1), "Home sends handle 1 to 40 Hz");
    h2->keyPressed (juce::KeyPress (juce::KeyPress::endKey));
    check (near (real (proc, timaband::pid::xover2), 16000.0, 1.0), "End sends handle 2 to 16 kHz");
    set (proc, timaband::pid::xover1, 180.0f);
    set (proc, timaband::pid::xover2, 2400.0f);

    // Mute and solo.
    auto* bypassButton = findByTitle (*editor, "Bypass");
    auto* muteHigh = findByTitle (*editor, "Mute high band");
    auto* soloMid = findByTitle (*editor, "Solo mid band");
    click (muteHigh);
    check (real (proc, timaband::pid::mute[2]) > 0.5f, "the M button drives the mute parameter");
    click (soloMid);
    check (real (proc, timaband::pid::solo[1]) > 0.5f, "the S button drives the solo parameter");

    // Audibility shows as the columns' alpha: mid is soloed, so low and high dim.
    check (near (findByTitle (*editor, "Low band")->getAlpha(), 0.35, 0.01)
             && near (findByTitle (*editor, "Mid band")->getAlpha(), 1.0, 0.01)
             && near (findByTitle (*editor, "High band")->getAlpha(), 0.35, 0.01),
           "with mid soloed the low and high columns are drawn at 35 % alpha");

    click (soloMid);
    check (near (findByTitle (*editor, "Low band")->getAlpha(), 1.0, 0.01)
             && near (findByTitle (*editor, "High band")->getAlpha(), 0.35, 0.01),
           "with the solo off only the muted high column stays dimmed");
    click (muteHigh);

    // Bypass dims the left area.
    click (bypassButton);
    check (real (proc, timaband::pid::bypass) > 0.5f, "the Bypass button drives the bypass parameter");
    check (near (findByTitle (*editor, "Controls")->getAlpha(), 0.7, 0.01), "bypass draws the left area at 70 % alpha");

    // Curve buttons: one radio group per band, bound to the curve parameter.
    {
        using timaband::ui::CurveSelector;
        namespace colours = timaband::ui::colours;

        auto* group = static_cast<CurveSelector*> (findByTitle (*editor, "Mid band detector curve"));
        const auto button = [&editor] (const juce::String& band, int index)
        {
            return static_cast<juce::Button*> (findByTitle (*editor, band + " curve: " + timaband::curveNames[index]));
        };

        // The index of the lit button, or -1 unless exactly one is lit.
        const auto lit = [&button] (const juce::String& band)
        {
            auto found = -1, count = 0;

            for (int i = 0; i < 3; ++i)
                if (button (band, i)->getToggleState())
                {
                    found = i;
                    ++count;
                }

            return count == 1 ? found : -1;
        };

        const auto& id = timaband::pid::curve[1];
        const auto curveIndex = indexOf (id);
        const auto oneGesture = [&] { return log.begins[curveIndex] == 1 && log.ends[curveIndex] == 1; };
        const auto noGesture = [&] { return log.begins[curveIndex] == 0 && log.ends[curveIndex] == 0; };

        auto allFound = group != nullptr;

        for (const auto* band : timaband::bandNames)
            for (int i = 0; i < 3; ++i)
                allFound = allFound && button (band, i) != nullptr
                             && button (band, i)->getTooltip() == timaband::curveNames[i];

        check (allFound, "each band has Exponential, Linear and Logarithmic buttons, titled and with a tooltip");

        if (! allFound)
            return 1;

        check (group->getWantsKeyboardFocus() && ! button ("Mid", 0)->getWantsKeyboardFocus(),
               "the curve group is one Tab stop");
        check (near (group->getFocusRingBounds().getWidth(), 3 * 44 + 2 * 4, 0.5) && near (group->getFocusRingBounds().getHeight(), 30, 0.5),
               "its focus ring goes round the three buttons");

        // The parameter changing under the editor lights the matching button.
        auto followsParameter = true;

        for (int v : { 0, 2, 1, 0 })
        {
            set (proc, id, (float) v);
            followsParameter = followsParameter && lit ("Mid") == v && group->getSelected() == v;
        }

        check (followsParameter, "a host change of the curve parameter lights the matching button");

        // Clicking a button sets the parameter in one gesture, and leaves the other bands alone.
        set (proc, timaband::pid::curve[0], 1.0f);
        set (proc, timaband::pid::curve[2], 1.0f);
        log.begins.clear(); log.ends.clear();
        click (button ("Mid", 2));
        check (near (real (proc, id), 2.0, 1.0e-3) && lit ("Mid") == 2, "clicking Logarithmic sets the curve parameter and lights that button");
        check (oneGesture(), "a click is one begin/end gesture");
        check (near (real (proc, timaband::pid::curve[0]), 1.0, 1.0e-3) && near (real (proc, timaband::pid::curve[2]), 1.0, 1.0e-3)
                 && lit ("Low") == 1 && lit ("High") == 1,
               "the other bands' curves and buttons are untouched");

        log.begins.clear(); log.ends.clear();
        click (button ("Mid", 2));
        check (noGesture() && lit ("Mid") == 2, "clicking the lit button changes nothing");

        click (button ("Mid", 0));
        check (near (real (proc, id), 0.0, 1.0e-3) && lit ("Mid") == 0, "clicking Exponential sets the curve parameter and lights that button");

        // Keys move the selection, one gesture per change, and stop at the ends.
        set (proc, id, 1.0f);
        log.begins.clear(); log.ends.clear();
        auto handled = group->keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
        check (handled && near (real (proc, id), 2.0, 1.0e-3) && lit ("Mid") == 2, "Right moves the selection to Logarithmic");
        check (oneGesture(), "a key press is one begin/end gesture");

        log.begins.clear(); log.ends.clear();
        handled = group->keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
        check (handled && noGesture() && near (real (proc, id), 2.0, 1.0e-3) && lit ("Mid") == 2, "Right at the last button changes nothing");

        handled = group->keyPressed (juce::KeyPress (juce::KeyPress::leftKey));
        check (handled && near (real (proc, id), 1.0, 1.0e-3) && lit ("Mid") == 1, "Left moves the selection back to Linear");
        handled = group->keyPressed (juce::KeyPress (juce::KeyPress::upKey));
        check (handled && near (real (proc, id), 0.0, 1.0e-3) && lit ("Mid") == 0, "Up moves it back to Exponential");

        log.begins.clear(); log.ends.clear();
        handled = group->keyPressed (juce::KeyPress (juce::KeyPress::leftKey));
        check (handled && noGesture() && lit ("Mid") == 0, "Left at the first button changes nothing");

        handled = group->keyPressed (juce::KeyPress (juce::KeyPress::downKey));
        check (handled && near (real (proc, id), 1.0, 1.0e-3) && lit ("Mid") == 1, "Down moves it forward to Linear");
        check (! group->keyPressed (juce::KeyPress ('a')) && near (real (proc, id), 1.0, 1.0e-3), "other keys are left alone");

        // Looks: grey when idle, the band colour when selected, a light grey once the band is bypassed.
        // A triggered click leaves its button drawn as pressed until it has painted once
        // and a timer has run, so let that happen first.
        group->createComponentSnapshot (group->getLocalBounds());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);

        const auto fillAt = [&] (int index)
        {
            const auto image = group->createComponentSnapshot (group->getLocalBounds(), true, 1.0f);
            return image.getPixelAt (index * (44 + 4) + 5, 4 + 5);
        };

        set (proc, id, 2.0f);
        check (fillAt (2) == colours::mid && fillAt (0) == colours::tabIdle && fillAt (1) == colours::tabIdle,
               "the selected button is filled with the band colour, the others are grey");
        set (proc, id, 1.0f);
        check (fillAt (1) == colours::mid && fillAt (2) == colours::tabIdle, "its fill follows the selection");

        set (proc, timaband::pid::bandBypass[1], 1.0f);
        check (fillAt (1) == colours::tabBypassed && fillAt (0) == colours::tabIdle, "a bypassed band's selected button turns light grey");
        set (proc, timaband::pid::bandBypass[1], 0.0f);
        check (fillAt (1) == colours::mid, "and goes back to the band colour");
    }

    // UI-only state.
    auto* segmentPre = findByTitle (*editor, "Pre");
    auto* segmentPost = findByTitle (*editor, "Post");
    const auto segmentsOn = [&]
    {
        return (int) static_cast<juce::Button*> (segmentPre)->getToggleState() + (int) static_cast<juce::Button*> (segmentPost)->getToggleState();
    };

    click (segmentPre);
    check (! (bool) proc.apvts.state.getProperty (timaband::uiState::analyzerPost), "Pre selects the pre-fader analyzer");
    check (segmentsOn() == 1 && static_cast<juce::Button*> (segmentPre)->getToggleState(), "after clicking Pre only Pre is lit");

    click (segmentPost);
    check ((bool) proc.apvts.state.getProperty (timaband::uiState::analyzerPost), "Post selects the post-fader analyzer");
    check (segmentsOn() == 1 && static_cast<juce::Button*> (segmentPost)->getToggleState(), "after clicking Post only Post is lit");

    click (segmentPre);
    click (segmentPre);
    check (! (bool) proc.apvts.state.getProperty (timaband::uiState::analyzerPost) && segmentsOn() == 1, "clicking the lit segment again changes nothing");

    // Selecting a band by clicking in its column.
    // (The column listens to its children's presses as a mouse listener; here it gets one directly.)
    auto* highColumn = findByTitle (*editor, "High band");
    highColumn->mouseDown (makeEvent (*highColumn, { 22.0f, 40.0f }, { 22.0f, 40.0f }));
    check ((int) proc.apvts.state.getProperty (timaband::uiState::selectedBand) == 2, "pressing inside the high column selects the high band");

    // A press that starts on a curve button reaches the column the same way.
    auto* lowCurveButton = findByTitle (*editor, "Low curve: Linear");
    findByTitle (*editor, "Low band")->mouseDown (makeEvent (*lowCurveButton, { 22.0f, 15.0f }, { 22.0f, 15.0f }));
    check ((int) proc.apvts.state.getProperty (timaband::uiState::selectedBand) == 0, "pressing a curve button selects its band");

    // Per-band bypass: the power button drives the parameter and greys the column.
    click (findByTitle (*editor, "Low band bypass"));
    check (real (proc, timaband::pid::bandBypass[0]) > 0.5f, "the power button drives the band bypass parameter");
    check (near (findByTitle (*editor, "Low band")->getAlpha(), 0.45, 0.01), "a bypassed band's column is drawn at 45 % alpha");
    check (near (findByTitle (*editor, "Mid band")->getAlpha(), 1.0, 0.01), "bypassing one band leaves the others alone");
    set (proc, timaband::pid::mute[0], 1.0f);
    check (near (findByTitle (*editor, "Low band")->getAlpha(), 0.35 * 0.45, 0.01), "mute dimming stacks on top of the bypass greying");
    set (proc, timaband::pid::mute[0], 0.0f);
    set (proc, timaband::pid::bandBypass[0], 0.0f);
    check (near (findByTitle (*editor, "Low band")->getAlpha(), 1.0, 0.01), "a host change of the bypass parameter updates the column");

    // The input/output faders.
    for (const auto& [title, id] : { std::pair<juce::String, juce::String> { "Input gain", timaband::pid::inputGain },
                                      std::pair<juce::String, juce::String> { "Output gain", timaband::pid::outputGain } })
    {
        auto* fader = static_cast<timaband::ui::FaderSlider*> (findByTitle (*editor, title));
        check (fader != nullptr, "found the " + title.toLowerCase() + " fader");

        if (fader == nullptr)
            continue;

        set (proc, id, 0.0f);
        check (near (fader->getThumbBounds().getCentreY(), (float) fader->getHeight() * 0.5f, 1.0), title + " fader: 0 dB sits at the middle of the travel");

        set (proc, id, 24.0f);
        check (near (fader->getThumbBounds().getCentreY(), 10.0, 1.0), title + " fader: +24 dB is the top of the travel, level with the bar");
        set (proc, id, -24.0f);
        check (near (fader->getThumbBounds().getCentreY(), (double) fader->getHeight() - 10.0, 1.0), title + " fader: -24 dB is the bottom");

        set (proc, id, 0.0f);
        const auto grab = fader->getThumbBounds().getCentre();
        log.begins.clear(); log.ends.clear();
        drag (*fader, grab, grab.translated (0.0f, -33.4f));                        // a tenth of the 334 px travel
        check (near (real (proc, id), 4.8, 0.15), title + " fader: dragging up a tenth of the travel adds 4.8 dB (got " + juce::String (real (proc, id), 2) + ")");
        check (log.begins[indexOf (id)] == 1 && log.ends[indexOf (id)] == 1, title + " fader: one begin/end gesture per drag");

        fader->mouseDoubleClick (makeEvent (*fader, grab, grab, 2));
        check (near (real (proc, id), 0.0, 1.0e-4), title + " fader: double-click resets to 0 dB");

        juce::MouseWheelDetails wheel;
        wheel.deltaY = 0.2f;
        wheel.isReversed = false;
        wheel.isSmooth = false;
        wheel.isInertial = false;
        fader->mouseWheelMove (makeEvent (*fader, grab, grab, 0, false, juce::ModifierKeys()), wheel);
        check (real (proc, id) > 0.5f, title + " fader: the mouse wheel moves it (got " + juce::String (real (proc, id), 2) + ")");
        set (proc, id, 0.0f);

        fader->keyPressed (juce::KeyPress (juce::KeyPress::upKey));
        check (near (real (proc, id), 1.0, 1.0e-3), title + " fader: Up arrow adds 1 dB");
        set (proc, id, 0.0f);
    }

    // The readouts under the bars follow, and take typed values.
    {
        auto* box = static_cast<timaband::ui::GainBox*> (findByTitle (*editor, "Input gain value"));
        set (proc, timaband::pid::inputGain, -2.0f);
        check (box->getValueLabel()->getText() == timaband::ui::theme::minusSign() + "2.0", "the readout shows a signed one-decimal value");
        set (proc, timaband::pid::inputGain, 0.0f);
        check (box->getValueLabel()->getText() == "0.0", "the readout shows 0.0 at zero");
        set (proc, timaband::pid::inputGain, 4.5f);
        check (box->getValueLabel()->getText() == "+4.5", "the readout shows a plus sign above zero");

        box->getValueLabel()->showEditor();
        check (box->getValueLabel()->getCurrentTextEditor() != nullptr, "clicking the readout opens a text editor");

        if (auto* ed = box->getValueLabel()->getCurrentTextEditor())
            ed->setText (timaband::ui::theme::minusSign() + "3.5", false);

        box->getValueLabel()->hideEditor (false);
        check (near (real (proc, timaband::pid::inputGain), -3.5, 1.0e-3), "typing a value sets the gain");

        const auto grab = juce::Point<float> (26.0f, 12.0f);
        set (proc, timaband::pid::inputGain, 0.0f);
        drag (*box, grab, grab.translated (0.0f, -25.0f));
        check (real (proc, timaband::pid::inputGain) > 2.0f, "dragging the readout up raises the gain (got " + juce::String (real (proc, timaband::pid::inputGain), 2) + ")");
        set (proc, timaband::pid::inputGain, 0.0f);
    }

    check (attack->getWantsKeyboardFocus(), "the band sliders can take keyboard focus");

    // Ticking with audio in flight must not misbehave.
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    for (int i = 0; i < 20; ++i)
    {
        for (int c = 0; c < 2; ++c)
            for (int s = 0; s < 512; ++s)
                buffer.setSample (c, s, 0.3f * std::sin (0.05f * (float) (s + i * 512)));

        proc.processBlock (buffer, midi);
        editor->tick (1.0f / 30.0f);
    }

    check (true, "ticking the editor with audio running");

    for (auto* param : proc.getParameters())
        param->removeListener (&log);

    editor.reset();
    std::printf ("\n%d of %d checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
