#include "Widgets.h"
#include "../Tooltips.h"

namespace pv::ui
{
juce::String shortValue (const juce::String& text)
{
    auto t = text.trim();
    for (auto suffix : { " st", " %", " dB", " dBTP", " ms", " c", " s" })
        if (t.endsWith (suffix)) return t.dropLastCharacters ((int) strlen (suffix));
    return t;
}

juce::String tip (const juce::String& id) { return tooltipFor (id); }

// ============================================================================
Knob::Knob (APVTS& s, const juce::String& id, const juce::String& l, Style st, juce::Colour accent)
    : state (s), style (st)
{
    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setMouseDragSensitivity (style == macro ? 300 : 220);
    slider.setDoubleClickReturnValue (false, 0.0);
    slider.onValueChange = [this] { valueChanged(); };
    slider.onDoubleClick = [this] { showValueEditor(); };
    setAccent (accent);
    addAndMakeVisible (slider);
    bind (id, l);
}

void Knob::setShown (float pos)
{
    shownPos = pos;
    slider.getProperties().set ("animPos", pos);
    slider.getProperties().set ("flash", flash);
}

// v3.1: los cambios que no hace el ratón (preset, A/B, deshacer, host) barren suavemente
// hasta el valor nuevo con un destello; al arrastrar, el knob sigue al ratón sin retardo.
void Knob::valueChanged()
{
    const float target = (float) slider.valueToProportionOfLength (slider.getValue());
    const auto now = juce::Time::getMillisecondCounter();
    const bool direct = shownPos < 0.0f || motionMode() == 0 || slider.isMouseButtonDown() || editor != nullptr || ! isShowing();
    if (direct)
    {
        animT = 1.0f;
        setShown (target);
        stopTimer();
    }
    else if (std::abs (target - shownPos) > 1.0e-4f)
    {
        if (now - lastChange > 300 && std::abs (target - shownPos) > 0.04f) flash = 1.0f;
        fromPos = shownPos;
        toPos = target;
        animT = 0.0f;
        startTimerHz (60);
    }
    lastChange = now;
    repaint();
}

void Knob::timerCallback()
{
    animT = juce::jmin (1.0f, animT + 1.0f / (60.0f * 0.3f));
    const float e = 1.0f - std::pow (1.0f - animT, 3.0f);
    flash *= 0.93f;
    setShown (fromPos + (toPos - fromPos) * e);
    slider.repaint();
    if (animT >= 1.0f && flash < 0.02f)
    {
        flash = 0.0f;
        setShown (toPos);
        stopTimer();
    }
}

void Knob::setAccent (juce::Colour c)
{
    slider.getProperties().set ("accent", c.toString());
    slider.repaint();
}

void Knob::bind (const juce::String& id, const juce::String& l)
{
    attachment.reset();
    paramId = id;
    label = l;
    if (auto* prm = state.getParameter (id))
    {
        slider.defaultValue = (double) state.getParameterRange (id).convertFrom0to1 (prm->getDefaultValue());
        const bool macroParam = id.startsWith ("m") && id.length() <= 8 && id != "monoCheck";
        slider.getProperties().set ("centred", macroParam);
        slider.setTooltip (tip (id) + "\n(Doble clic: escribir valor  /  Ctrl/Alt+clic: reset)");
        attachment = std::make_unique<APVTS::SliderAttachment> (state, id, slider);
    }
    animT = 1.0f;
    setShown ((float) slider.valueToProportionOfLength (slider.getValue()));
    repaint();
}

void Knob::showValueEditor()
{
    editor = std::make_unique<juce::TextEditor>();
    editor->setJustification (juce::Justification::centred);
    editor->setFont (font (15.0f, true));
    editor->setText (slider.getTextFromValue (slider.getValue()), false);
    editor->selectAll();
    editor->setBounds (getLocalBounds().removeFromBottom (style == macro ? 26 : 20).withSizeKeepingCentre (juce::jmin (getWidth() - 4, 96), 22));
    auto commit = [this]
    {
        if (editor == nullptr) return;
        const auto txt = editor->getText().retainCharacters ("0123456789.-+");
        if (txt.isNotEmpty())
            slider.setValue (slider.getValueFromText (txt), juce::sendNotificationSync);
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Knob> (this)] { if (safe) safe->editor.reset(); });
    };
    editor->onReturnKey = commit;
    editor->onFocusLost = commit;
    editor->onEscapeKey = [this] { juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Knob> (this)] { if (safe) safe->editor.reset(); }); };
    addAndMakeVisible (*editor);
    editor->grabKeyboardFocus();
}

void Knob::resized()
{
    auto r = getLocalBounds();
    switch (style)
    {
        case macro:  r.removeFromBottom (44); break;
        case medium: r.removeFromBottom (34); break;
        case small:  r.removeFromBottom (30); break;
        case tiny:   r.removeFromBottom (0);  break;
        case bare:   break;
    }
    const int d = juce::jmin (r.getWidth(), r.getHeight());
    slider.setBounds (r.withSizeKeepingCentre (d, d));
}

void Knob::paint (juce::Graphics& g)
{
    if (style == tiny || style == bare) return;
    auto r = getLocalBounds().toFloat();
    const float a = isEnabled() ? 1.0f : 0.4f;
    auto text = slider.getTextFromValue (slider.getValue());
    if (showShortValue) text = shortValue (text);
    if (style == macro)
    {
        auto valueArea = r.removeFromBottom (22.0f);
        auto labelArea = r.removeFromBottom (20.0f);
        g.setFont (font (16.0f, true));
        g.setColour (Pal::text.withAlpha (0.92f * a));
        drawSpaced (g, label.toUpperCase(), labelArea, 0.8f, juce::Justification::centred);
        g.setFont (font (20.0f, true));
        g.setColour (Pal::lilac.withAlpha ((slider.isMouseOverOrDragging() ? 1.0f : 0.92f) * a));
        g.drawText (text, valueArea, juce::Justification::centred);
        return;
    }
    const float lh = style == medium ? 16.0f : 14.0f;
    auto valueArea = r.removeFromBottom (lh);
    auto labelArea = r.removeFromBottom (lh + 1.0f);
    g.setFont (font (style == medium ? 13.5f : 12.5f, true));
    g.setColour (Pal::text.withAlpha (0.8f * a));
    drawSpaced (g, label.toUpperCase(), labelArea, 0.6f, juce::Justification::centred);
    g.setFont (font (style == medium ? 14.0f : 13.0f, true));
    g.setColour (Pal::lilac.withAlpha ((slider.isMouseOverOrDragging() ? 1.0f : 0.8f) * a));
    g.drawText (text, valueArea, juce::Justification::centredTop);
}

// ============================================================================
MiniSlider::MiniSlider (APVTS& s, const juce::String& id, const juce::String& l, juce::Colour accent) : label (l)
{
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.getProperties().set ("accent", accent.toString());
    slider.onValueChange = [this] { repaint(); };
    slider.setTooltip (tip (id));
    if (auto* prm = s.getParameter (id))
        slider.defaultValue = (double) s.getParameterRange (id).convertFrom0to1 (prm->getDefaultValue());
    addAndMakeVisible (slider);
    attachment = std::make_unique<APVTS::SliderAttachment> (s, id, slider);
}

void MiniSlider::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (getHeight() / 2);
    slider.setBounds (r.reduced (2, 0));
}

void MiniSlider::paint (juce::Graphics& g)
{
    auto top = getLocalBounds().toFloat().removeFromTop ((float) getHeight() * 0.5f + 2.0f);
    g.setFont (font (13.0f, true));
    g.setColour (Pal::text.withAlpha (isEnabled() ? 0.75f : 0.3f));
    g.drawText (label.toUpperCase(), top.reduced (2.0f, 0.0f), juce::Justification::centredLeft);
    g.setColour (Pal::lilac.withAlpha (isEnabled() ? 0.85f : 0.3f));
    g.drawText (shortValue (slider.getTextFromValue (slider.getValue())), top.reduced (2.0f, 0.0f), juce::Justification::centredRight);
}

// ============================================================================
Fader::Fader (APVTS& s, const juce::String& id, juce::Colour accent)
{
    slider.setSliderStyle (juce::Slider::LinearVertical);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.getProperties().set ("accent", accent.toString());
    slider.setTooltip (tip (id));
    if (auto* prm = s.getParameter (id))
        slider.defaultValue = (double) s.getParameterRange (id).convertFrom0to1 (prm->getDefaultValue());
    addAndMakeVisible (slider);
    attachment = std::make_unique<APVTS::SliderAttachment> (s, id, slider);
}

void Fader::resized()
{
    auto r = getLocalBounds();
    r.removeFromRight (10);
    slider.setBounds (r);
}

void Fader::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().removeFromRight (6.0f).reduced (0.0f, 5.0f);
    paintBarMeter (g, r, meter, 0.0f, Pal::lime, Pal::violet);
}

// ============================================================================
ParamChoice::ParamChoice (APVTS& s, const juce::String& id, const juce::String& l) : label (l)
{
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (s.getParameter (id)))
        box.addItemList (c->choices, 1);
    box.setTooltip (tip (id));
    addAndMakeVisible (box);
    attachment = std::make_unique<APVTS::ComboBoxAttachment> (s, id, box);
}

void ParamChoice::resized()
{
    auto r = getLocalBounds();
    if (label.isNotEmpty()) r.removeFromBottom (18);
    box.setBounds (r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), 28)));
}

void ParamChoice::paint (juce::Graphics& g)
{
    if (label.isEmpty()) return;
    g.setFont (font (13.0f, true));
    g.setColour (Pal::text.withAlpha (isEnabled() ? 0.8f : 0.35f));
    drawSpaced (g, label.toUpperCase(), getLocalBounds().toFloat().removeFromBottom (16.0f), 0.6f, juce::Justification::centred);
}

// ============================================================================
ParamToggle::ParamToggle (APVTS& s, const juce::String& id, const juce::String& text)
{
    setButtonText (text);
    setClickingTogglesState (true);
    setTooltip (tip (id));
    attachment = std::make_unique<APVTS::ButtonAttachment> (s, id, *this);
}

PowerButton::PowerButton (APVTS& s, const juce::String& id, juce::Colour accent)
{
    getProperties().set ("accent", accent.toString());
    setTooltip (tip (id).isNotEmpty() ? tip (id) : juce::String ("On / Off"));
    attachment = std::make_unique<APVTS::ButtonAttachment> (s, id, *this);
}

// ============================================================================
Segmented::Segmented (APVTS& s, const juce::String& id, juce::StringArray labels, float fontSize)
{
    auto* prm = dynamic_cast<juce::AudioParameterChoice*> (s.getParameter (id));
    jassert (prm != nullptr);
    if (labels.isEmpty() && prm != nullptr) labels = prm->choices;
    for (int i = 0; i < labels.size(); ++i)
    {
        auto* b = buttons.add (new juce::TextButton (labels[i]));
        b->setClickingTogglesState (false);
        b->getProperties().set ("fontSize", fontSize);
        b->setTooltip (tip (id));
        b->onClick = [this, i] { if (attachment) attachment->setValueAsCompleteGesture ((float) i); };
        addAndMakeVisible (b);
    }
    if (prm != nullptr)
        attachment = std::make_unique<juce::ParameterAttachment> (*prm, [this] (float v)
        {
            current = juce::roundToInt (v);
            for (int i = 0; i < buttons.size(); ++i) buttons[i]->setToggleState (i == current, juce::dontSendNotification);
        }, s.undoManager);
    if (attachment) attachment->sendInitialUpdate();
}

void Segmented::resized()
{
    if (buttons.isEmpty()) return;
    const float w = ((float) getWidth() - (float) gap * (float) (buttons.size() - 1)) / (float) buttons.size();
    for (int i = 0; i < buttons.size(); ++i)
        buttons[i]->setBounds (juce::Rectangle<float> ((w + (float) gap) * (float) i, 0.0f, w, (float) getHeight()).toNearestInt());
}

// ============================================================================
IconButton::IconButton (Icon i, const juce::String& tooltip) : juce::Button (tooltip), icon (i)
{
    setTooltip (tooltip);
}

void IconButton::paintButton (juce::Graphics& g, bool hover, bool down)
{
    auto r = getLocalBounds().toFloat();
    if (frame || hover || down)
    {
        g.setColour (Pal::violet.withAlpha (down ? 0.3f : hover ? 0.14f : 0.0f));
        g.fillRoundedRectangle (r.reduced (0.5f), 5.0f);
        if (frame)
        {
            g.setColour ((hover ? Pal::violet : Pal::border).withAlpha (0.95f));
            g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
        }
    }
    const bool on = getToggleState();
    drawIcon (g, icon, r.reduced (r.getHeight() * 0.25f), on ? onColour : Pal::text.withAlpha (isEnabled() ? (hover ? 1.0f : 0.8f) : 0.3f), on);
}

void drawIcon (juce::Graphics& g, IconButton::Icon icon, juce::Rectangle<float> r, juce::Colour c, bool filled)
{
    using namespace juce;
    g.setColour (c);
    const float s = jmin (r.getWidth(), r.getHeight());
    auto b = r.withSizeKeepingCentre (s, s);
    const PathStrokeType st (1.5f, PathStrokeType::curved, PathStrokeType::rounded);
    Path p;
    switch (icon)
    {
        case IconButton::undo:
        case IconButton::redo:
        {
            const bool u = icon == IconButton::undo;
            p.addCentredArc (b.getCentreX(), b.getCentreY() + s * 0.1f, s * 0.36f, s * 0.32f, 0.0f,
                             u ? -MathConstants<float>::halfPi * 1.4f : -MathConstants<float>::halfPi * 0.6f,
                             u ? MathConstants<float>::halfPi * 0.6f : MathConstants<float>::halfPi * 1.4f, true);
            g.strokePath (p, st);
            const float ax = u ? b.getX() + s * 0.12f : b.getRight() - s * 0.12f, ay = b.getY() + s * 0.32f;
            Path a; a.startNewSubPath (ax + (u ? s * 0.2f : -s * 0.2f), ay - s * 0.14f); a.lineTo (ax, ay); a.lineTo (ax + (u ? s * 0.22f : -s * 0.22f), ay + s * 0.1f);
            g.strokePath (a, st);
            break;
        }
        case IconButton::heart:
        {
            const float x = b.getX(), y = b.getY();
            p.startNewSubPath (x + s * 0.5f, y + s * 0.9f);
            p.cubicTo (x - s * 0.15f, y + s * 0.45f, x + s * 0.12f, y - s * 0.05f, x + s * 0.5f, y + s * 0.28f);
            p.cubicTo (x + s * 0.88f, y - s * 0.05f, x + s * 1.15f, y + s * 0.45f, x + s * 0.5f, y + s * 0.9f);
            p.closeSubPath();
            if (filled) g.fillPath (p); else g.strokePath (p, st);
            break;
        }
        case IconButton::download:
            p.startNewSubPath (b.getCentreX(), b.getY()); p.lineTo (b.getCentreX(), b.getY() + s * 0.62f);
            p.startNewSubPath (b.getCentreX() - s * 0.25f, b.getY() + s * 0.38f); p.lineTo (b.getCentreX(), b.getY() + s * 0.64f); p.lineTo (b.getCentreX() + s * 0.25f, b.getY() + s * 0.38f);
            p.startNewSubPath (b.getX() + s * 0.05f, b.getY() + s * 0.68f); p.lineTo (b.getX() + s * 0.05f, b.getBottom()); p.lineTo (b.getRight() - s * 0.05f, b.getBottom()); p.lineTo (b.getRight() - s * 0.05f, b.getY() + s * 0.68f);
            g.strokePath (p, st);
            break;
        case IconButton::more:
            for (int i = 0; i < 3; ++i) g.fillEllipse (b.getCentreX() - 1.6f, b.getY() + s * (0.12f + 0.38f * (float) i) - 1.6f, 3.2f, 3.2f);
            break;
        case IconButton::gear:
        {
            const auto cc = b.getCentre();
            for (int i = 0; i < 8; ++i)
            {
                const float a = MathConstants<float>::twoPi * (float) i / 8.0f;
                p.addRectangle (-s * 0.07f, -s * 0.5f, s * 0.14f, s * 0.2f);
                p.applyTransform (AffineTransform::rotation (MathConstants<float>::twoPi / 8.0f));
                (void) a;
            }
            p.applyTransform (AffineTransform::translation (cc.x, cc.y));
            g.fillPath (p);
            g.drawEllipse (cc.x - s * 0.33f, cc.y - s * 0.33f, s * 0.66f, s * 0.66f, 2.2f);
            g.drawEllipse (cc.x - s * 0.12f, cc.y - s * 0.12f, s * 0.24f, s * 0.24f, 1.4f);
            break;
        }
        case IconButton::search:
            g.drawEllipse (b.getX() + s * 0.05f, b.getY() + s * 0.05f, s * 0.6f, s * 0.6f, 1.5f);
            g.drawLine (b.getX() + s * 0.57f, b.getY() + s * 0.57f, b.getRight() - s * 0.05f, b.getBottom() - s * 0.05f, 1.6f);
            break;
        case IconButton::close:
            g.drawLine (b.getX() + s * 0.15f, b.getY() + s * 0.15f, b.getRight() - s * 0.15f, b.getBottom() - s * 0.15f, 1.5f);
            g.drawLine (b.getRight() - s * 0.15f, b.getY() + s * 0.15f, b.getX() + s * 0.15f, b.getBottom() - s * 0.15f, 1.5f);
            break;
        case IconButton::plus:
            g.drawLine (b.getCentreX(), b.getY() + s * 0.1f, b.getCentreX(), b.getBottom() - s * 0.1f, 1.6f);
            g.drawLine (b.getX() + s * 0.1f, b.getCentreY(), b.getRight() - s * 0.1f, b.getCentreY(), 1.6f);
            break;
        case IconButton::importExport:
            p.startNewSubPath (b.getX() + s * 0.1f, b.getY() + s * 0.5f); p.lineTo (b.getX() + s * 0.1f, b.getBottom()); p.lineTo (b.getRight() - s * 0.1f, b.getBottom()); p.lineTo (b.getRight() - s * 0.1f, b.getY() + s * 0.5f);
            p.startNewSubPath (b.getCentreX(), b.getY() + s * 0.7f); p.lineTo (b.getCentreX(), b.getY());
            p.startNewSubPath (b.getCentreX() - s * 0.22f, b.getY() + s * 0.22f); p.lineTo (b.getCentreX(), b.getY()); p.lineTo (b.getCentreX() + s * 0.22f, b.getY() + s * 0.22f);
            g.strokePath (p, st);
            break;
        case IconButton::image:
            g.drawRoundedRectangle (b.reduced (s * 0.05f), 2.0f, 1.4f);
            p.startNewSubPath (b.getX() + s * 0.1f, b.getBottom() - s * 0.15f); p.lineTo (b.getX() + s * 0.4f, b.getY() + s * 0.5f);
            p.lineTo (b.getX() + s * 0.6f, b.getY() + s * 0.7f); p.lineTo (b.getX() + s * 0.72f, b.getY() + s * 0.55f); p.lineTo (b.getRight() - s * 0.1f, b.getBottom() - s * 0.15f);
            g.strokePath (p, st);
            g.fillEllipse (b.getX() + s * 0.62f, b.getY() + s * 0.2f, s * 0.14f, s * 0.14f);
            break;
        case IconButton::prev:
        case IconButton::next:
        {
            const bool pv = icon == IconButton::prev;
            p.startNewSubPath (b.getCentreX() + (pv ? s * 0.18f : -s * 0.18f), b.getY() + s * 0.15f);
            p.lineTo (b.getCentreX() + (pv ? -s * 0.18f : s * 0.18f), b.getCentreY());
            p.lineTo (b.getCentreX() + (pv ? s * 0.18f : -s * 0.18f), b.getBottom() - s * 0.15f);
            g.strokePath (p, st);
            break;
        }
        case IconButton::link:
            g.drawRoundedRectangle (b.getX(), b.getY() + s * 0.3f, s * 0.6f, s * 0.36f, s * 0.18f, 1.5f);
            g.drawRoundedRectangle (b.getX() + s * 0.4f, b.getY() + s * 0.34f, s * 0.6f, s * 0.36f, s * 0.18f, 1.5f);
            break;
    }
}

// ============================================================================
float meterNorm (float linear, float floorDb, float topDb)
{
    return juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (linear, floorDb) - floorDb) / (topDb - floorDb));
}

void paintBarMeter (juce::Graphics& g, juce::Rectangle<float> r, float level01, float peak01, juce::Colour c1, juce::Colour c2, bool fromTop)
{
    using namespace juce;
    g.setColour (Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (r, 1.5f);
    const float h = r.getHeight() * jlimit (0.0f, 1.0f, level01);
    auto fill = fromTop ? r.withHeight (h) : r.withTop (r.getBottom() - h);
    g.setGradientFill (ColourGradient (c1, 0, r.getBottom(), c2, 0, r.getY(), false));
    g.fillRoundedRectangle (fill, 1.5f);
    // segmentos
    g.setColour (Pal::bg.withAlpha (0.55f));
    for (float y = r.getY() + 3.0f; y < r.getBottom(); y += 3.0f)
        g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
    if (peak01 > 0.0f)
    {
        g.setColour (Colours::white.withAlpha (0.9f));
        g.fillRect (r.getX(), r.getBottom() - r.getHeight() * peak01 - 1.0f, r.getWidth(), 2.0f);
    }
}
} // namespace pv::ui
