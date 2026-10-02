#pragma once

#include <array>
#include <functional>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../Parameters.h"

namespace timaband::ui
{

// While "Link bands" is on, moving one band's Attack (or Sustain) slider moves the
// same control on the other two bands by the same dB amount. The other two
// parameters get their own begin/end gestures, so the host sees three touches.
class LinkController final : private juce::Slider::Listener
{
public:
    using Sliders = std::array<std::array<juce::Slider*, 2>, numBands>;     // [band][0 attack, 1 sustain]

    LinkController (juce::AudioProcessorValueTreeState& state, Sliders s, std::function<bool()> linkedQuery)
        : apvts (state), sliders (s), isLinked (std::move (linkedQuery))
    {
        for (auto& band : sliders)
            for (auto* slider : band)
                slider->addListener (this);
    }

    ~LinkController() override
    {
        for (auto& band : sliders)
            for (auto* slider : band)
                slider->removeListener (this);
    }

private:
    struct Position
    {
        int band = -1, control = -1;
        bool operator== (const Position& o) const noexcept { return band == o.band && control == o.control; }
        bool operator!= (const Position& o) const noexcept { return ! (*this == o); }
    };

    Position find (const juce::Slider* s) const noexcept
    {
        for (int b = 0; b < numBands; ++b)
            for (int c = 0; c < 2; ++c)
                if (sliders[(size_t) b][(size_t) c] == s)
                    return { b, c };

        return {};
    }

    juce::RangedAudioParameter* paramFor (int band, int control) const
    {
        return apvts.getParameter (control == 0 ? pid::attack[band] : pid::sustain[band]);
    }

    template <typename Fn>
    void forEachOther (Fn&& fn) const
    {
        for (int b = 0; b < numBands; ++b)
            if (b != active.band)
                fn (*paramFor (b, active.control));
    }

    void sliderDragStarted (juce::Slider* s) override
    {
        active = find (s);

        if (active.band < 0)
            return;

        lastValue = s->getValue();
        linked = isLinked();

        if (linked)
            forEachOther ([] (juce::RangedAudioParameter& p) { p.beginChangeGesture(); });
    }

    void sliderValueChanged (juce::Slider* s) override
    {
        if (! linked || active.band < 0 || find (s) != active)
            return;

        const auto delta = (float) (s->getValue() - lastValue);
        lastValue = s->getValue();

        if (delta == 0.0f)
            return;

        forEachOther ([delta] (juce::RangedAudioParameter& p)
        {
            const auto range = p.getNormalisableRange();
            const auto next = juce::jlimit (range.start, range.end, p.convertFrom0to1 (p.getValue()) + delta);
            p.setValueNotifyingHost (p.convertTo0to1 (next));
        });
    }

    void sliderDragEnded (juce::Slider* s) override
    {
        if (active.band < 0 || find (s) != active)
            return;

        if (linked)
            forEachOther ([] (juce::RangedAudioParameter& p) { p.endChangeGesture(); });

        active = {};
        linked = false;
    }

    juce::AudioProcessorValueTreeState& apvts;
    Sliders sliders;
    std::function<bool()> isLinked;

    Position active;
    bool linked = false;
    double lastValue = 0.0;
};

} // namespace timaband::ui
