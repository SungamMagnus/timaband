#include "CrossoverDisplay.h"

#include <cmath>

namespace timaband::ui
{

namespace
{
constexpr float axisMinHz = 20.0f, axisMaxHz = 20000.0f;
constexpr float minRatio = 1.26f;                            // 1/3 octave, as the DSP enforces
constexpr float handleMinHz = 40.0f, handleMaxHz = 16000.0f;
constexpr float handleHalfWidth = 7.0f;
constexpr float pillTop = 5.0f, pillHeight = 36.0f, lineTop = 41.0f;
constexpr float specMinDb = -84.0f, specMaxDb = 6.0f;

struct GridLine { float hz; const char* text; };
constexpr GridLine gridLines[] = { { 60.0f, "60" }, { 100.0f, "100" }, { 300.0f, "300" }, { 600.0f, "600" },
                                   { 1000.0f, "1k" }, { 3000.0f, "3k" }, { 6000.0f, "6k" }, { 10000.0f, "10k" } };

float clampSafe (float v, float lo, float hi) noexcept
{
    return juce::jmax (lo, juce::jmin (hi, v));
}
} // namespace

/* ── Handle ──────────────────────────────────────────────────────────────── */

class CrossoverDisplay::Handle final : public FocusVisible<juce::Component>
{
public:
    Handle (CrossoverDisplay& owner, int handleIndex)
        : display (owner),
          index (handleIndex),
          attachment (*owner.processor.apvts.getParameter (handleIndex == 0 ? pid::xover1 : pid::xover2),
                      [this] (float value) { setHz (value); })
    {
        setTitle (handleIndex == 0 ? "Low and mid crossover frequency" : "Mid and high crossover frequency");
        setWantsKeyboardFocus (true);
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    }

    // Separate from the constructor: the display reacts to the first value.
    void start() { attachment.sendInitialUpdate(); }

    float getHz() const noexcept { return hz; }

    bool isTipVisible() const { return isMouseOver() || dragging || isFocusRingVisible(); }

    // Where the line sits, in display coordinates.
    float getLineX() const noexcept { return freqToX (hz); }

    juce::Rectangle<float> getFocusRingBounds() const override
    {
        return { centreX() - handleHalfWidth, pillTop, handleHalfWidth * 2.0f, pillHeight };
    }

    float getFocusRingRadius() const override { return handleHalfWidth; }

    void updateBounds()
    {
        const auto left = (int) std::floor (getLineX() - handleHalfWidth);
        setBounds (left, 0, (int) (handleHalfWidth * 2.0f), layout::displayH);
        repaint();
    }

    bool hitTest (int x, int y) override
    {
        const auto cx = centreX();

        if (std::abs ((float) x - cx) <= 5.0f)
            return true;

        return juce::Rectangle<float> (cx - handleHalfWidth, pillTop, handleHalfWidth * 2.0f, pillHeight)
                   .contains ((float) x, (float) y);
    }

    void paint (juce::Graphics& g) override
    {
        const auto cx = centreX();

        g.setColour (colours::text.withAlpha (0.9f));
        g.fillRect (cx - 0.75f, lineTop, 1.5f, (float) getHeight() - lineTop);

        const juce::Rectangle<float> pill (cx - handleHalfWidth, pillTop, handleHalfWidth * 2.0f, pillHeight);

        if (dragging || isMouseOver())
        {
            g.setColour (dragging ? colours::white : colours::white.withAlpha (0.18f));
            g.fillRoundedRectangle (pill, handleHalfWidth);
        }

        g.setColour (dragging ? colours::white : colours::text);
        g.drawRoundedRectangle (pill.reduced (1.0f), handleHalfWidth - 1.0f, 2.0f);
    }

    void mouseEnter (const juce::MouseEvent&) override { hoverChanged(); }
    void mouseExit (const juce::MouseEvent&) override  { hoverChanged(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.mods.isLeftButtonDown())
            return;

        dragging = true;
        attachment.beginGesture();
        grabOffset = e.getEventRelativeTo (&display).position.x - getLineX();
        hoverChanged();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging)
            return;

        const auto x = e.getEventRelativeTo (&display).position.x - grabOffset;
        move (display.constrainCrossover (index, xToFreq (x)), false);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (! dragging)
            return;

        dragging = false;
        attachment.endGesture();
        hoverChanged();
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        const auto& param = *display.processor.apvts.getParameter (index == 0 ? pid::xover1 : pid::xover2);
        move (display.constrainCrossover (index, param.convertFrom0to1 (param.getDefaultValue())), true);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        const auto shift = key.getModifiers().isShiftDown();
        const auto step = shift ? 1.0 / 6.0 : 1.0 / 24.0;      // octaves

        if (key.isKeyCode (juce::KeyPress::rightKey) || key.isKeyCode (juce::KeyPress::upKey))
            move (display.constrainCrossover (index, hz * (float) std::pow (2.0, step)), true);
        else if (key.isKeyCode (juce::KeyPress::leftKey) || key.isKeyCode (juce::KeyPress::downKey))
            move (display.constrainCrossover (index, hz * (float) std::pow (2.0, -step)), true);
        else if (key.isKeyCode (juce::KeyPress::homeKey))
            move (display.constrainCrossover (index, 0.0f), true);
        else if (key.isKeyCode (juce::KeyPress::endKey))
            move (display.constrainCrossover (index, 1.0e9f), true);
        else
            return false;

        return true;
    }

private:
    // The line's x inside this component: the bounds are snapped to whole pixels.
    float centreX() const noexcept { return getLineX() - (float) Component::getX(); }

    void hoverChanged()
    {
        repaint();
        display.repaint (0, 0, layout::graphW, layout::stripH);
    }

    void setHz (float value)
    {
        if (std::abs (value - hz) < 1.0e-4f)
            return;

        hz = value;
        setDescription (hzReadout (hz));
        updateBounds();
        display.crossoversMoved();
    }

    void move (float value, bool complete)
    {
        if (std::abs (value - hz) < 1.0e-4f)
            return;

        // The attachment ignores echoes of its own writes, so the UI follows here.
        hz = value;
        setDescription (hzReadout (hz));
        updateBounds();

        if (complete)
            attachment.setValueAsCompleteGesture (value);
        else
            attachment.setValueAsPartOfGesture (value);

        display.crossoversMoved();
    }

    CrossoverDisplay& display;
    int index;
    float hz = index == 0 ? 180.0f : 2400.0f;
    bool dragging = false;
    float grabOffset = 0.0f;
    juce::ParameterAttachment attachment;
};

/* ── CrossoverDisplay ────────────────────────────────────────────────────── */

float CrossoverDisplay::freqToX (float hz) noexcept
{
    return (float) layout::graphW * std::log (hz / axisMinHz) / std::log (axisMaxHz / axisMinHz);
}

float CrossoverDisplay::xToFreq (float x) noexcept
{
    return axisMinHz * std::exp (x / (float) layout::graphW * std::log (axisMaxHz / axisMinHz));
}

CrossoverDisplay::CrossoverDisplay (TimabandProcessor& p)
    : processor (p)
{
    setOpaque (true);
    setTitle ("Crossover display");

    analyzerPost = (bool) p.apvts.state.getProperty (uiState::analyzerPost, uiState::defaultAnalyzerPost);

    // Whatever the FIFOs piled up while no editor was open is stale.
    analyzer.setSampleRate (p.getSampleRate());
    analyzer.discard (p.getAnalyzerFifo (false));
    analyzer.discard (p.getAnalyzerFifo (true));

    for (size_t i = 0; i < handles.size(); ++i)
    {
        handles[i] = std::make_unique<Handle> (*this, (int) i);
        addAndMakeVisible (*handles[i]);
    }

    for (int b = 0; b < numBands; ++b)
    {
        const juce::String name (bandNames[b]);
        auto& bypass = bypassButtons[(size_t) b];
        auto& mute = muteButtons[(size_t) b];
        auto& solo = soloButtons[(size_t) b];

        bypass = std::make_unique<StyledToggle> (StyledToggle::Style::bypass, "", b);
        mute = std::make_unique<StyledToggle> (StyledToggle::Style::mute, "M", b);
        solo = std::make_unique<StyledToggle> (StyledToggle::Style::solo, "S", b);
        bypass->setTitle (name + " band bypass");
        mute->setTitle ("Mute " + name.toLowerCase() + " band");
        solo->setTitle ("Solo " + name.toLowerCase() + " band");
        addAndMakeVisible (*bypass);
        addAndMakeVisible (*mute);
        addAndMakeVisible (*solo);

        bypassAttachments[(size_t) b] = std::make_unique<juce::ButtonParameterAttachment> (*p.apvts.getParameter (pid::bandBypass[b]), *bypass);
        muteAttachments[(size_t) b] = std::make_unique<juce::ButtonParameterAttachment> (*p.apvts.getParameter (pid::mute[b]), *mute);
        soloAttachments[(size_t) b] = std::make_unique<juce::ButtonParameterAttachment> (*p.apvts.getParameter (pid::solo[b]), *solo);
    }

    // Hover must follow the mouse over the buttons and handles too.
    addMouseListener (this, true);

    ready = true;

    for (auto& h : handles)
        h->start();

    crossoversMoved();
}

CrossoverDisplay::~CrossoverDisplay() = default;

float CrossoverDisplay::constrainCrossover (int index, float hz) const noexcept
{
    return index == 0 ? clampSafe (hz, handleMinHz, crossoverHz[1] / minRatio)
                      : clampSafe (hz, crossoverHz[0] * minRatio, handleMaxHz);
}

std::array<float, numBands + 1> CrossoverDisplay::regionEdges() const noexcept
{
    return { 0.0f, freqToX (crossoverHz[0]), freqToX (crossoverHz[1]), (float) layout::graphW };
}

int CrossoverDisplay::regionAt (float x) const noexcept
{
    const auto edges = regionEdges();
    return x < edges[1] ? 0 : (x < edges[2] ? 1 : 2);
}

void CrossoverDisplay::crossoversMoved()
{
    if (! ready)
        return;

    for (size_t i = 0; i < handles.size(); ++i)
        crossoverHz[i] = handles[i]->getHz();

    resized();
    repaint();

    if (onCrossoversChanged != nullptr)
        onCrossoversChanged();
}

void CrossoverDisplay::resized()
{
    if (! ready)
        return;

    for (auto& h : handles)
        h->updateBounds();

    const auto edges = regionEdges();

    for (size_t b = 0; b < (size_t) numBands; ++b)
    {
        // Three 28 px circles with 10 px gaps, centred in the region: power, M, S.
        const auto centre = (int) std::lround ((edges[b] + edges[b + 1]) * 0.5f);
        bypassButtons[b]->setBounds (centre - 52, 76, 28, 28);
        muteButtons[b]->setBounds (centre - 14, 76, 28, 28);
        soloButtons[b]->setBounds (centre + 24, 76, 28, 28);
    }
}

void CrossoverDisplay::tick (float dtSeconds)
{
    analyzer.setSampleRate (processor.getSampleRate());

    if (analyzer.update (processor.getAnalyzerFifo (analyzerPost), dtSeconds))
        repaint (graphArea());
}

void CrossoverDisplay::setSelectedBand (int band)
{
    if (band == selectedBand)
        return;

    selectedBand = band;
    repaint();
}

void CrossoverDisplay::setAnalyzerPost (bool post)
{
    if (post == analyzerPost)
        return;

    analyzerPost = post;
    analyzer.discard (processor.getAnalyzerFifo (post));
    repaint (graphArea());
}

void CrossoverDisplay::setAudible (const std::array<bool, numBands>& flags)
{
    if (flags == audible)
        return;

    audible = flags;
    repaint();
}

/* ── Mouse ───────────────────────────────────────────────────────────────── */

void CrossoverDisplay::updateHover()
{
    const auto next = isMouseOver (true) ? regionAt ((float) getMouseXYRelative().x) : -1;

    if (next == hoverRegion)
        return;

    hoverRegion = next;
    repaint (graphArea());
}

void CrossoverDisplay::mouseMove (const juce::MouseEvent&)  { updateHover(); }
void CrossoverDisplay::mouseEnter (const juce::MouseEvent&) { updateHover(); }
void CrossoverDisplay::mouseExit (const juce::MouseEvent&)  { updateHover(); }

void CrossoverDisplay::mouseDown (const juce::MouseEvent& e)
{
    // Child events arrive here too (hover tracking); only a press on the display itself selects.
    if (e.eventComponent != this || ! e.mods.isLeftButtonDown())
        return;

    if (onSelectBand != nullptr)
        onSelectBand (regionAt (e.position.x));
}

/* ── Painting ────────────────────────────────────────────────────────────── */

void CrossoverDisplay::paintStrip (juce::Graphics& g) const
{
    g.setColour (colours::strip);
    g.fillRect (0, 0, layout::graphW, layout::stripH);

    // radial-gradient dots, 12 px apart, offset by 6
    g.setColour (colours::gridDot);

    for (float y = 6.0f; y < (float) layout::stripH; y += 12.0f)
        for (float x = 6.0f; x < (float) layout::graphW; x += 12.0f)
            g.fillEllipse (x - 1.0f, y - 1.0f, 2.0f, 2.0f);
}

void CrossoverDisplay::paintRegions (juce::Graphics& g) const
{
    const auto edges = regionEdges();
    const auto top = (float) layout::stripH, height = (float) layout::graphH;

    for (int b = 0; b < numBands; ++b)
    {
        const juce::Rectangle<float> area (edges[(size_t) b], top, edges[(size_t) b + 1] - edges[(size_t) b], height);

        if (b == selectedBand)
        {
            g.setColour (colours::band (b).withAlpha (0.14f));
            g.fillRect (area);
        }
        else if (b == hoverRegion)
        {
            g.setColour (colours::band (b).withAlpha (0.06f));
            g.fillRect (area);
        }

        if (audible[(size_t) b])
            continue;

        // repeating-linear-gradient(135deg, white 6% 0 1px, transparent 1px 8px)
        juce::Path clip;
        clip.addRectangle (area);

        g.saveState();
        g.reduceClipRegion (clip);
        g.setColour (colours::white.withAlpha (0.06f));

        constexpr float period = 8.0f * 1.41421356f;

        for (float c = 0.5f * 1.41421356f; c < area.getWidth() + height; c += period)
            g.drawLine (area.getX() + c, top, area.getX() + c - height, top + height, 1.0f);

        g.restoreState();
    }
}

void CrossoverDisplay::paintSpectrum (juce::Graphics& g) const
{
    const auto w = (float) layout::graphW, h = (float) layout::graphH;

    g.saveState();
    g.setOrigin (0, layout::stripH);

    g.setColour (colours::line);

    for (const auto& line : gridLines)
        g.fillRect ((int) std::lround (freqToX (line.hz)), 0, 1, layout::graphH);

    const auto& points = analyzer.getPoints();
    juce::Path line, fill;

    for (size_t i = 0; i < points.size(); ++i)
    {
        const auto x = w * (float) i / (float) (points.size() - 1);
        const auto y = juce::jlimit (2.0f, h - 1.0f, h * (1.0f - (points[i] - specMinDb) / (specMaxDb - specMinDb)));

        if (i == 0)
        {
            line.startNewSubPath (x, y);
            fill.startNewSubPath (x, y);
        }
        else
        {
            line.lineTo (x, y);
            fill.lineTo (x, y);
        }
    }

    fill.lineTo (w, h);
    fill.lineTo (0.0f, h);
    fill.closeSubPath();

    // One pass per band region, clipped to it: the selected band's slice draws in its colour.
    const auto edges = regionEdges();

    for (int b = 0; b < numBands; ++b)
    {
        juce::Path clip;
        clip.addRectangle (edges[(size_t) b], 0.0f, edges[(size_t) b + 1] - edges[(size_t) b], h);

        const auto alpha = audible[(size_t) b] ? 1.0f : 0.35f;
        const auto selected = b == selectedBand;

        g.saveState();
        g.reduceClipRegion (clip);

        g.setColour ((selected ? colours::band (b) : colours::spec).withAlpha ((selected ? 0.18f : 0.07f) * alpha));
        g.fillPath (fill);

        g.setColour ((selected ? colours::band (b) : colours::spec).withAlpha ((selected ? 1.0f : 0.7f) * alpha));
        g.strokePath (line, juce::PathStrokeType (1.25f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.restoreState();
    }

    g.setColour (colours::muted);
    g.setFont (theme::font (12.0f));

    for (const auto& label : gridLines)
        g.drawSingleLineText (label.text, (int) std::lround (freqToX (label.hz)) + 6, layout::graphH - 6);

    g.drawSingleLineText ("Hz", layout::graphW - 6, layout::graphH - 6, juce::Justification::right);

    g.restoreState();
}

void CrossoverDisplay::paint (juce::Graphics& g)
{
    if (g.getClipBounds().intersects (juce::Rectangle<int> (0, 0, layout::graphW, layout::stripH)))
        paintStrip (g);

    g.setColour (colours::graph);
    g.fillRect (0, layout::stripH, layout::graphW, layout::graphH);
    g.setColour (colours::line);
    g.fillRect (0, layout::stripH, layout::graphW, 1);

    paintRegions (g);
    paintSpectrum (g);
}

void CrossoverDisplay::paintTip (juce::Graphics& g, const Handle& handle) const
{
    const auto font = theme::font (12.0f);
    const auto text = hzReadout (handle.getHz());
    const auto width = std::ceil (juce::GlyphArrangement::getStringWidth (font, text)) + 16.0f + 2.0f;
    const auto x = handle.getLineX();
    const auto left = x > (float) layout::graphW - 96.0f ? x - 14.0f - width : x + 14.0f;
    const juce::Rectangle<float> tip (left, 11.0f, width, 22.0f);

    g.setColour (colours::ctrlHi);
    g.fillRoundedRectangle (tip, 4.0f);
    g.setColour (colours::menuBorder);
    g.drawRoundedRectangle (tip.reduced (0.5f), 4.0f, 1.0f);

    g.setColour (colours::text);
    g.setFont (font);
    g.drawText (text, tip, juce::Justification::centred, false);
}

void CrossoverDisplay::paintOverChildren (juce::Graphics& g)
{
    for (auto& h : handles)
        if (h->isTipVisible())
            paintTip (g, *h);
}

} // namespace timaband::ui
