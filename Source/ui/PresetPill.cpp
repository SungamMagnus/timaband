#include "PresetPill.h"

#include <cmath>

namespace timaband::ui
{

namespace
{
constexpr int triSize = 30, triMargin = 4;
constexpr int glyphX = 14, glyphSize = 18, textX = glyphX + glyphSize + 10, textPadRight = 8;
constexpr float dotSize = 6.0f, dotGap = 8.0f;
}

class PresetPill::NameButton final : public FocusVisible<juce::Button>
{
public:
    NameButton() : FocusVisible<juce::Button> ("Choose preset")
    {
        setTitle ("Choose preset");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void setState (const juce::String& presetName, bool isModified)
    {
        name = presetName;
        modified = isModified;
        setDescription (name + (modified ? ", modified" : ""));
        repaint();
    }

    juce::Rectangle<float> getFocusRingBounds() const override { return getLocalBounds().toFloat(); }
    float getFocusRingRadius() const override                  { return (float) getHeight() * 0.5f; }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto bounds = getLocalBounds().toFloat();

        theme::drawListGlyph (g, juce::Rectangle<float> ((float) glyphSize, (float) glyphSize)
                                     .withPosition ((float) glyphX, bounds.getCentreY() - (float) glyphSize * 0.5f),
                              (highlighted || down) ? colours::text : colours::muted);

        const auto font = theme::font (15.0f);
        const auto room = bounds.getWidth() - (float) textX - (float) textPadRight - (modified ? dotSize + dotGap : 0.0f);
        const auto textW = juce::jmin (std::ceil (juce::GlyphArrangement::getStringWidth (font, name)), room);

        g.setColour (colours::text);
        g.setFont (font);
        g.drawText (name, juce::Rectangle<float> ((float) textX, 0.0f, textW, bounds.getHeight()), juce::Justification::centredLeft, true);

        if (modified)
        {
            g.setColour (colours::muted);
            g.fillEllipse (juce::Rectangle<float> (dotSize, dotSize).withCentre ({ (float) textX + textW + dotGap + dotSize * 0.5f, bounds.getCentreY() }));
        }
    }

private:
    juce::String name;
    bool modified = false;
};

PresetPill::PresetPill()
    : nameButton (std::make_unique<NameButton>()),
      previousButton (IconButton::Icon::previous, "Previous preset"),
      nextButton (IconButton::Icon::next, "Next preset")
{
    setTitle ("Preset");

    addAndMakeVisible (*nameButton);
    addAndMakeVisible (previousButton);
    addAndMakeVisible (nextButton);

    nameButton->onClick   = [this] { if (onOpenMenu != nullptr) onOpenMenu(); };
    previousButton.onClick = [this] { if (onPrevious != nullptr) onPrevious(); };
    nextButton.onClick     = [this] { if (onNext != nullptr) onNext(); };
}

PresetPill::~PresetPill() = default;

void PresetPill::setState (const juce::String& presetName, bool isModified)
{
    if (presetName == name && isModified == modified)
        return;

    name = presetName;
    modified = isModified;
    nameButton->setState (name, modified);
}

void PresetPill::resized()
{
    // The name takes what the two triangles leave; the last one sits 4 px in from the end.
    const auto triY = (getHeight() - triSize) / 2;
    const auto mainW = getWidth() - 2 * triSize - triMargin;

    nameButton->setBounds (0, 0, mainW, getHeight());
    previousButton.setBounds (mainW, triY, triSize, triSize);
    nextButton.setBounds (mainW + triSize, triY, triSize, triSize);
}

void PresetPill::paint (juce::Graphics& g)
{
    g.setColour (colours::ctrl);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), (float) getHeight() * 0.5f);
}

} // namespace timaband::ui
