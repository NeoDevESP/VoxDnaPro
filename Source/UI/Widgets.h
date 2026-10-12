#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../LookAndFeel.h"

using APVTS = juce::AudioProcessorValueTreeState;

namespace pv::ui
{
// modo de animación global (v3.1): 0 = desactivadas, 1 = suaves (30 fps), 2 = completas (60 fps)
inline int& motionMode() { static int m = 2; return m; }

// "-2.3 st" -> "-2.3", "68" -> "68", "100 %" -> "100"
juce::String shortValue (const juce::String& text);
juce::String tip (const juce::String& id);
// juce::String (v, 0) imprime todos los decimales: este helper redondea de verdad
inline juce::String num (double v, int dec) { return dec <= 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, dec); }

// ---------------------------------------------------------------------------
// Slider con doble clic -> escribir valor y Ctrl/Alt+clic -> valor por defecto
class EditableSlider : public juce::Slider
{
public:
    std::function<void()> onDoubleClick, onRightClick;
    double defaultValue = 0.0;
    void mouseDoubleClick (const juce::MouseEvent&) override { if (onDoubleClick) onDoubleClick(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onRightClick) { onRightClick(); return; }
        if (e.mods.isCommandDown() || e.mods.isAltDown()) { setValue (defaultValue, juce::sendNotificationSync); return; }
        Slider::mouseDown (e);
    }
};

// ---------------------------------------------------------------------------
// Knob rotatorio con etiqueta y valor
class Knob : public juce::Component, private juce::Timer
{
public:
    enum Style { macro, medium, small, tiny, bare };
    Knob (APVTS& s, const juce::String& id, const juce::String& label, Style st = medium, juce::Colour accent = Pal::violet);
    void bind (const juce::String& id, const juce::String& label);   // reasignar (macros configurables)
    void paint (juce::Graphics&) override;
    void resized() override;
    void setAccent (juce::Colour c);
    juce::String getParamId() const { return paramId; }
    EditableSlider slider;
    bool showShortValue = false;    // macros: "68" en vez de "68 %"
    std::function<void (bool)> onHover;
private:
    void showValueEditor();
    void valueChanged();
    void timerCallback() override;
    void setShown (float pos);
    float shownPos = -1.0f, fromPos = 0.0f, toPos = 0.0f, animT = 1.0f, flash = 0.0f;
    juce::uint32 lastChange = 0;
    void mouseEnter (const juce::MouseEvent&) override { if (onHover) onHover (true); }
    void mouseExit (const juce::MouseEvent&) override { if (onHover) onHover (false); }
    APVTS& state;
    juce::String paramId, label;
    Style style;
    std::unique_ptr<juce::TextEditor> editor;
    std::unique_ptr<APVTS::SliderAttachment> attachment;
};

// Mini deslizador horizontal (tarjetas de la cadena de efectos)
class MiniSlider : public juce::Component
{
public:
    MiniSlider (APVTS& s, const juce::String& id, const juce::String& label, juce::Colour accent = Pal::violet);
    void paint (juce::Graphics&) override;
    void resized() override;
    EditableSlider slider;
private:
    juce::String label;
    std::unique_ptr<APVTS::SliderAttachment> attachment;
};

// Fader vertical (canales de ROUTING)
class Fader : public juce::Component
{
public:
    Fader (APVTS& s, const juce::String& id, juce::Colour accent = Pal::violet);
    void paint (juce::Graphics&) override;
    void resized() override;
    EditableSlider slider;
    float meter = 0.0f;   // 0..1 nivel para pintar al lado
private:
    std::unique_ptr<APVTS::SliderAttachment> attachment;
};

class ParamChoice : public juce::Component
{
public:
    ParamChoice (APVTS& s, const juce::String& id, const juce::String& label = {});
    void paint (juce::Graphics&) override;
    void resized() override;
    juce::ComboBox box;
private:
    juce::String label;
    std::unique_ptr<APVTS::ComboBoxAttachment> attachment;
};

class ParamToggle : public juce::TextButton
{
public:
    ParamToggle (APVTS& s, const juce::String& id, const juce::String& text);
private:
    std::unique_ptr<APVTS::ButtonAttachment> attachment;
};

class PowerButton : public juce::ToggleButton
{
public:
    PowerButton (APVTS& s, const juce::String& id, juce::Colour accent = Pal::violet);
private:
    std::unique_ptr<APVTS::ButtonAttachment> attachment;
};

// fila de botones exclusivos ligada a un parámetro de elección
class Segmented : public juce::Component
{
public:
    Segmented (APVTS& s, const juce::String& id, juce::StringArray labels = {}, float fontSize = 15.0f);
    void resized() override;
    int gap = 6;
private:
    juce::OwnedArray<juce::TextButton> buttons;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    int current = 0;
};

// icono vectorial simple para botones de cabecera
class IconButton : public juce::Button
{
public:
    enum Icon { undo, redo, heart, download, more, gear, search, close, plus, importExport, image, prev, next, link };
    IconButton (Icon i, const juce::String& tooltip = {});
    void paintButton (juce::Graphics&, bool hover, bool down) override;
    Icon icon;
    juce::Colour onColour = Pal::magenta;
    bool frame = false;
};
void drawIcon (juce::Graphics& g, IconButton::Icon icon, juce::Rectangle<float> r, juce::Colour c, bool filled = false);

// medidor vertical con segmentos (verde-violeta) y pico
void paintBarMeter (juce::Graphics& g, juce::Rectangle<float> r, float level01, float peak01, juce::Colour c1, juce::Colour c2, bool fromTop = false);
float meterNorm (float linear, float floorDb = -60.0f, float topDb = 3.0f);
} // namespace pv::ui
