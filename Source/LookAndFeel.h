#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "BinaryData.h"

// ============================================================================
//  VOX DNA PRO v3 — tema visual "Urban Vocal Laboratory"
//  Paleta oscura azul-noche con acentos violeta / magenta / cian (mockup v3).
//  Tipografías OFL incrustadas: Rajdhani (UI) y VoxDisplay (titulares).
// ============================================================================
namespace pv
{
namespace Pal
{
    const juce::Colour bg       { 0xff05051a };
    const juce::Colour bg2      { 0xff0a0920 };
    const juce::Colour panel    { 0xff0b0b1f };
    const juce::Colour panel2   { 0xff121130 };
    const juce::Colour border   { 0xff2a2357 };
    const juce::Colour violet   { 0xff9b5cff };
    const juce::Colour lilac    { 0xffc9a7ff };
    const juce::Colour magenta  { 0xffff3df2 };
    const juce::Colour pink     { 0xffff4fa3 };
    const juce::Colour cyan     { 0xff38d6ff };
    const juce::Colour blue     { 0xff3fa9ff };
    const juce::Colour teal     { 0xff2fd3e0 };
    const juce::Colour lime     { 0xff52e07a };
    const juce::Colour orange   { 0xffff8a2a };
    const juce::Colour amber    { 0xffffb02e };
    const juce::Colour red      { 0xffff4a5a };
    const juce::Colour text     { 0xffe6e3ff };
    const juce::Colour dim      { 0xff7f7aa8 };
    const juce::Colour faint    { 0xff4a4670 };
    const juce::Colour track    { 0xff1c1a3c };

    inline juce::Colour layer (int i)
    {
        static const juce::Colour c[] { violet, pink, juce::Colour (0xffff62b5), blue, teal, orange, amber, lime };
        return c[juce::jlimit (0, 7, i)];
    }
}

// ---------------------------------------------------------------------------
// Tipografías
// ---------------------------------------------------------------------------
struct Fonts
{
    static juce::Typeface::Ptr get (int which)
    {
        static juce::Typeface::Ptr t[5];
        if (t[which] == nullptr)
        {
            switch (which)
            {
                case 0: t[0] = juce::Typeface::createSystemTypefaceFor (BinaryData::RajdhaniMedium_ttf, BinaryData::RajdhaniMedium_ttfSize); break;
                case 1: t[1] = juce::Typeface::createSystemTypefaceFor (BinaryData::RajdhaniSemiBold_ttf, BinaryData::RajdhaniSemiBold_ttfSize); break;
                case 2: t[2] = juce::Typeface::createSystemTypefaceFor (BinaryData::RajdhaniBold_ttf, BinaryData::RajdhaniBold_ttfSize); break;
                case 3: t[3] = juce::Typeface::createSystemTypefaceFor (BinaryData::VoxDisplayMedium_ttf, BinaryData::VoxDisplayMedium_ttfSize); break;
                default: t[4] = juce::Typeface::createSystemTypefaceFor (BinaryData::VoxDisplayBold_ttf, BinaryData::VoxDisplayBold_ttfSize); break;
            }
        }
        return t[which];
    }
};

// UI: Rajdhani (h en px). bold = SemiBold; heavy = Bold
inline juce::Font font (float h, bool bold = false, bool heavy = false)
{
    return juce::Font (juce::FontOptions (Fonts::get (heavy ? 2 : bold ? 1 : 0)).withHeight (h));
}
// titulares / logo
inline juce::Font display (float h, bool bold = true)
{
    return juce::Font (juce::FontOptions (Fonts::get (bold ? 4 : 3)).withHeight (h));
}

// texto con espaciado entre letras (look "HUD")
inline void drawSpaced (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> area,
                        float tracking, juce::Justification just)
{
    const auto f = g.getCurrentFont().withExtraKerningFactor (tracking / juce::jmax (1.0f, g.getCurrentFont().getHeight()));
    g.setFont (f);
    g.drawText (s, area, just, false);
}

// panel con borde violeta y brillo superior
inline void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour accent = Pal::violet, float accentAlpha = 0.35f, float radius = 7.0f)
{
    using namespace juce;
    g.setGradientFill (ColourGradient (Pal::panel2.withAlpha (0.96f), r.getX(), r.getY(), Pal::panel.withAlpha (0.96f), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, radius);
    g.setGradientFill (ColourGradient (accent.withAlpha (accentAlpha), r.getX(), r.getY(),
                                       Pal::border.withAlpha (0.9f), r.getRight(), r.getBottom(), false));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
    g.setColour (Colours::white.withAlpha (0.035f));
    g.drawLine (r.getX() + radius, r.getY() + 1.2f, r.getRight() - radius, r.getY() + 1.2f, 1.0f);
}

inline void panelTitle (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& t, juce::Colour c = Pal::text)
{
    g.setFont (font (15.5f, true));
    g.setColour (c);
    drawSpaced (g, t, r, 1.2f, juce::Justification::centredLeft);
}

// glow suave para trazos (varias pasadas)
inline void strokeGlow (juce::Graphics& g, const juce::Path& p, juce::Colour c, float width, int passes = 3)
{
    for (int i = passes; i >= 1; --i)
    {
        g.setColour (c.withAlpha (0.07f));
        g.strokePath (p, juce::PathStrokeType (width + (float) i * 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// ---------------------------------------------------------------------------
class NeonLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NeonLookAndFeel()
    {
        using namespace juce;
        setDefaultSansSerifTypeface (Fonts::get (0));
        setColour (ComboBox::backgroundColourId, Pal::panel2);
        setColour (ComboBox::textColourId, Pal::text);
        setColour (ComboBox::outlineColourId, Pal::border);
        setColour (ComboBox::arrowColourId, Pal::lilac);
        setColour (PopupMenu::backgroundColourId, Colour (0xf60b0a1e));
        setColour (PopupMenu::textColourId, Pal::text);
        setColour (PopupMenu::highlightedBackgroundColourId, Pal::violet.withAlpha (0.35f));
        setColour (PopupMenu::highlightedTextColourId, Colours::white);
        setColour (PopupMenu::headerTextColourId, Pal::lilac);
        setColour (TextButton::buttonColourId, Pal::panel2);
        setColour (TextButton::textColourOffId, Pal::text.withAlpha (0.85f));
        setColour (TextButton::textColourOnId, Colours::white);
        setColour (AlertWindow::backgroundColourId, Pal::panel);
        setColour (AlertWindow::textColourId, Pal::text);
        setColour (AlertWindow::outlineColourId, Pal::violet.withAlpha (0.6f));
        setColour (TextEditor::backgroundColourId, Pal::bg.withAlpha (0.9f));
        setColour (TextEditor::textColourId, Pal::text);
        setColour (TextEditor::outlineColourId, Pal::border);
        setColour (TextEditor::focusedOutlineColourId, Pal::violet);
        setColour (TextEditor::highlightColourId, Pal::violet.withAlpha (0.4f));
        setColour (CaretComponent::caretColourId, Pal::lilac);
        setColour (Slider::textBoxTextColourId, Pal::text);
        setColour (TooltipWindow::backgroundColourId, Colour (0xf20d0c24));
        setColour (TooltipWindow::textColourId, Pal::text);
        setColour (TooltipWindow::outlineColourId, Pal::violet.withAlpha (0.7f));
        setColour (ListBox::backgroundColourId, Colours::transparentBlack);
        setColour (ScrollBar::thumbColourId, Pal::violet.withAlpha (0.45f));
        setColour (Label::textColourId, Pal::text);
    }

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font& f) override
    {
        if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName()) return Fonts::get (f.isBold() ? 1 : 0);
        return LookAndFeel_V4::getTypefaceForFont (f);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        using namespace juce;
        const auto b = Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        const float r = jmin (b.getWidth(), b.getHeight()) * 0.5f - 3.0f;
        const float cx = b.getCentreX(), cy = b.getCentreY();
        if (s.getProperties().contains ("animPos") && ! s.isMouseButtonDown())
            pos = jlimit (0.0f, 1.0f, (float) s.getProperties()["animPos"]);   // barrido animado (v3.1)
        const float flash = s.getProperties().contains ("flash") ? (float) s.getProperties()["flash"] : 0.0f;
        const float angle = startAngle + pos * (endAngle - startAngle);
        const bool active = s.isEnabled();
        const float alpha = active ? 1.0f : 0.35f;
        const auto accent = s.getProperties().contains ("accent") ? Colour::fromString (s.getProperties()["accent"].toString()) : Pal::violet;
        const bool big = r > 30.0f;
        const float thick = big ? 5.5f : r > 18.0f ? 4.0f : 3.0f;

        const float arcR = r - thick * 0.5f - 1.0f;
        Path track;
        track.addCentredArc (cx, cy, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (Pal::track.withAlpha (alpha));
        g.strokePath (track, PathStrokeType (thick, PathStrokeType::curved, PathStrokeType::rounded));

        const bool centred = (bool) s.getProperties()["centred"];
        const bool bipolar = centred || (s.getMinimum() < 0.0 && s.getMaximum() > 0.0);
        const float originPos = centred ? 0.5f : bipolar ? (float) s.valueToProportionOfLength (0.0) : 0.0f;
        const float originAngle = startAngle + originPos * (endAngle - startAngle);

        if (std::abs (angle - originAngle) > 0.01f)
        {
            Path val;
            val.addCentredArc (cx, cy, arcR, arcR, 0.0f, jmin (originAngle, angle), jmax (originAngle, angle), true);
            for (int i = 4; i >= 1; --i)
            {
                g.setColour (accent.withAlpha (0.06f * alpha));
                g.strokePath (val, PathStrokeType (thick + (float) i * (big ? 3.5f : 2.5f), PathStrokeType::curved, PathStrokeType::rounded));
            }
            g.setGradientFill (ColourGradient (accent.withAlpha (alpha), cx - arcR, cy + arcR,
                                               Pal::magenta.interpolatedWith (accent, 0.35f).withAlpha (alpha), cx + arcR, cy - arcR, false));
            g.strokePath (val, PathStrokeType (thick, PathStrokeType::curved, PathStrokeType::rounded));
        }

        // destello tras un cambio de preset
        if (flash > 0.01f)
        {
            g.setColour (Colours::white.withAlpha (0.35f * flash * alpha));
            g.drawEllipse (cx - arcR, cy - arcR, arcR * 2.0f, arcR * 2.0f, thick + 1.0f);
            g.setColour (accent.withAlpha (0.25f * flash * alpha));
            g.drawEllipse (cx - arcR - 4.0f, cy - arcR - 4.0f, arcR * 2.0f + 8.0f, arcR * 2.0f + 8.0f, 5.0f * flash);
        }

        // cuerpo metálico oscuro
        const float kr = arcR - thick * 0.5f - (big ? 6.0f : 3.5f);
        g.setColour (Colours::black.withAlpha (0.55f * alpha));
        g.fillEllipse (cx - kr - 2.0f, cy - kr + 1.5f, (kr + 2.0f) * 2.0f, (kr + 2.0f) * 2.0f);
        g.setGradientFill (ColourGradient (Colour (0xff3a3854), cx - kr * 0.6f, cy - kr,
                                           Colour (0xff0b0b16), cx + kr * 0.5f, cy + kr, false));
        g.fillEllipse (cx - kr, cy - kr, kr * 2.0f, kr * 2.0f);
        g.setGradientFill (ColourGradient (Colour (0xff1d1c30), cx, cy - kr * 0.6f, Colour (0xff0e0d1a), cx, cy + kr * 0.8f, false));
        g.fillEllipse (cx - kr * 0.8f, cy - kr * 0.8f, kr * 1.6f, kr * 1.6f);
        g.setColour (Colours::white.withAlpha (0.10f * alpha));
        g.drawEllipse (cx - kr, cy - kr, kr * 2.0f, kr * 2.0f, 1.0f);

        // indicador
        const float p1 = kr * 0.15f, p2 = kr * 0.78f;
        const Point<float> a1 (cx + p1 * std::sin (angle), cy - p1 * std::cos (angle));
        const Point<float> a2 (cx + p2 * std::sin (angle), cy - p2 * std::cos (angle));
        g.setColour (Colours::white.withAlpha (0.92f * alpha));
        g.drawLine ({ a1, a2 }, big ? 2.4f : 1.8f);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos, float, float,
                           juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        using namespace juce;
        const auto accent = s.getProperties().contains ("accent") ? Colour::fromString (s.getProperties()["accent"].toString()) : Pal::violet;
        const float alpha = s.isEnabled() ? 1.0f : 0.35f;
        if (style == Slider::LinearHorizontal)
        {
            const float cy = (float) y + (float) h * 0.5f;
            const auto tr = Rectangle<float> ((float) x, cy - 2.0f, (float) w, 4.0f);
            g.setColour (Pal::track.withAlpha (alpha));
            g.fillRoundedRectangle (tr, 2.0f);
            const auto fill = tr.withRight (sliderPos);
            g.setGradientFill (ColourGradient (accent.withAlpha (0.6f * alpha), tr.getX(), 0, accent.withAlpha (alpha), sliderPos, 0, false));
            g.fillRoundedRectangle (fill, 2.0f);
            g.setColour (accent.withAlpha (0.25f * alpha));
            g.fillEllipse (sliderPos - 7.0f, cy - 7.0f, 14.0f, 14.0f);
            g.setColour (Colours::white.withAlpha (alpha));
            g.fillEllipse (sliderPos - 4.0f, cy - 4.0f, 8.0f, 8.0f);
        }
        else if (style == Slider::LinearVertical || style == Slider::LinearBarVertical)
        {
            const float cx = (float) x + (float) w * 0.5f;
            const auto tr = Rectangle<float> (cx - 2.5f, (float) y, 5.0f, (float) h);
            g.setColour (Pal::track.withAlpha (alpha));
            g.fillRoundedRectangle (tr, 2.5f);
            const auto fill = tr.withTop (sliderPos);
            g.setGradientFill (ColourGradient (accent.withAlpha (alpha), 0, sliderPos, accent.withAlpha (0.5f * alpha), 0, tr.getBottom(), false));
            g.fillRoundedRectangle (fill, 2.5f);
            const auto thumb = Rectangle<float> (cx - 11.0f, sliderPos - 5.0f, 22.0f, 10.0f);
            g.setColour (Colour (0xff23213d).withAlpha (alpha));
            g.fillRoundedRectangle (thumb, 3.0f);
            g.setColour (accent.withAlpha (alpha));
            g.drawRoundedRectangle (thumb, 3.0f, 1.0f);
            g.setColour (Colours::white.withAlpha (0.9f * alpha));
            g.drawHorizontalLine ((int) sliderPos, thumb.getX() + 4.0f, thumb.getRight() - 4.0f);
        }
        else LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, sliderPos, 0, 0, style, s);
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool hover, bool) override
    {
        using namespace juce;
        const auto r = b.getLocalBounds().toFloat().reduced (1.5f);
        const float d = jmin (r.getWidth(), r.getHeight());
        const auto c = r.withSizeKeepingCentre (d, d);
        const bool on = b.getToggleState();
        const auto accent = b.getProperties().contains ("accent") ? Colour::fromString (b.getProperties()["accent"].toString()) : Pal::violet;
        const Colour col = on ? accent.brighter (0.3f) : Pal::faint;
        if (on)
        {
            g.setColour (accent.withAlpha (0.22f));
            g.fillEllipse (c.expanded (1.5f));
        }
        g.setColour (col.withAlpha (hover ? 1.0f : 0.9f));
        const auto ic = c.reduced (d * 0.24f);
        Path p;
        p.addCentredArc (ic.getCentreX(), ic.getCentreY(), ic.getWidth() * 0.5f, ic.getHeight() * 0.5f, 0.0f,
                         MathConstants<float>::pi * 0.22f, MathConstants<float>::twoPi - MathConstants<float>::pi * 0.22f, true);
        p.startNewSubPath (ic.getCentreX(), ic.getY() - 1.0f);
        p.lineTo (ic.getCentreX(), ic.getCentreY());
        g.strokePath (p, PathStrokeType (1.5f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        using namespace juce;
        const auto r = Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
        const bool flat = (bool) box.getProperties()["flat"];
        if (! flat)
        {
            g.setColour (Pal::bg.withAlpha (0.75f));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour ((box.isMouseOver (true) ? Pal::violet : Pal::border).withAlpha (0.9f));
            g.drawRoundedRectangle (r, 4.0f, 1.0f);
        }
        Path arrow;
        const float ax = (float) w - 11.0f, ay = (float) h * 0.5f;
        arrow.startNewSubPath (ax - 3.5f, ay - 1.8f);
        arrow.lineTo (ax, ay + 2.0f);
        arrow.lineTo (ax + 3.5f, ay - 1.8f);
        g.setColour (Pal::lilac.withAlpha (box.isEnabled() ? 0.9f : 0.3f));
        g.strokePath (arrow, PathStrokeType (1.4f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    juce::Font getComboBoxFont (juce::ComboBox& b) override { return font (juce::jmax (12.0f, juce::jmin (15.5f, (float) b.getHeight() * 0.62f, (float) b.getWidth() * 0.15f)), true); }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (4, 0, box.getWidth() - 20, box.getHeight());
        label.setFont (getComboBoxFont (box));
        label.setJustificationType (box.getProperties()["left"] ? juce::Justification::centredLeft : juce::Justification::centred);
    }

    juce::Font getPopupMenuFont() override { return font (16.0f, true); }
    juce::Font getLabelFont (juce::Label& l) override { return font (juce::jmax (12.0f, (float) l.getHeight() * 0.6f), true); }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hover, bool down) override
    {
        using namespace juce;
        const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const float rad = b.getProperties().contains ("radius") ? (float) b.getProperties()["radius"] : 4.0f;
        const bool ghost = (bool) b.getProperties()["ghost"];
        if (b.getToggleState())
        {
            g.setGradientFill (ColourGradient (Pal::violet.withAlpha (0.45f), 0, 0, Pal::magenta.withAlpha (0.22f), r.getRight(), r.getBottom(), false));
            g.fillRoundedRectangle (r, rad);
            g.setColour (Pal::violet.brighter (0.4f).withAlpha (0.95f));
            g.drawRoundedRectangle (r, rad, 1.2f);
            return;
        }
        if (ghost && ! hover && ! down) return;
        g.setColour (down ? Pal::violet.withAlpha (0.3f) : hover ? Pal::violet.withAlpha (0.14f) : Pal::bg.withAlpha (0.55f));
        g.fillRoundedRectangle (r, rad);
        g.setColour ((hover ? Pal::violet : Pal::border).withAlpha (ghost ? 0.5f : 0.95f));
        g.drawRoundedRectangle (r, rad, 1.0f);
    }

    juce::Font getTextButtonFont (juce::TextButton& b, int h) override
    {
        const float sz = b.getProperties().contains ("fontSize") ? (float) b.getProperties()["fontSize"] : juce::jmin (15.0f, (float) h * 0.55f);
        return font (sz, true);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.setColour ((b.getToggleState() ? juce::Colours::white : Pal::text.withAlpha (0.85f)).withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
        g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (3, 1), juce::Justification::centred, 2);
    }

    juce::Rectangle<int> getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent) override
    {
        const float tw = juce::GlyphArrangement::getStringWidth (font (14.5f), text);
        const int w = juce::jmin (340, 26 + (int) tw);
        const int lines = 1 + (int) tw / 312 + text.retainCharacters ("\n").length();
        const int h = 12 + lines * 18;
        return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 18,
                                     pos.y > parent.getCentreY() ? pos.y - (h + 8) : pos.y + 8, w, h).constrainedWithin (parent);
    }

    void drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h) override
    {
        g.setColour (findColour (juce::TooltipWindow::backgroundColourId));
        g.fillRoundedRectangle (0, 0, (float) w, (float) h, 6.0f);
        g.setColour (findColour (juce::TooltipWindow::outlineColourId));
        g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1, (float) h - 1, 6.0f, 1.0f);
        g.setColour (Pal::text);
        g.setFont (font (14.5f));
        g.drawFittedText (text, 10, 4, w - 20, h - 8, juce::Justification::centredLeft, 8);
    }

    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
        g.setColour (Pal::violet.withAlpha (0.45f));
        g.drawRect (0, 0, w, h);
    }

    void drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbPos, int thumbSize, bool over, bool) override
    {
        const auto r = vertical ? juce::Rectangle<float> ((float) x + (float) w * 0.5f - 2.0f, (float) thumbPos, 4.0f, (float) thumbSize)
                                : juce::Rectangle<float> ((float) thumbPos, (float) y + (float) h * 0.5f - 2.0f, (float) thumbSize, 4.0f);
        g.setColour (Pal::violet.withAlpha (over ? 0.7f : 0.4f));
        g.fillRoundedRectangle (r, 2.0f);
    }
};
} // namespace pv
