#pragma once
#include "Context.h"
#include "Art.h"

namespace pv::ui
{
// componente que se refresca desde el timer del editor
// refresh(): datos (30 Hz) · animate(): movimiento (cada frame del reloj de animación)
struct Refreshable { virtual ~Refreshable() = default; virtual void refresh() = 0; virtual void animate() {} };

// ---------------------------------------------------------------------------
class HeroPanel : public juce::Component, public Refreshable
{
public:
    explicit HeroPanel (UiContext& c);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh() override;
    void animate() override;
    void presetChanged();
    std::function<void()> onImageMenu;
private:
    struct Spark { float x, y, vx, vy, life, size; juce::Colour colour; };
    UiContext& ctx;
    Segmented dnaMode;
    juce::ComboBox referenceBox;
    IconButton imageButton { IconButton::image, "Imagen propia para este preset" };
    juce::Image art, prevArt;          // fondo estático (actual y anterior, para el fundido)
    art::Scene scene, prevScene;
    std::vector<Spark> sparks;
    float transition = 1.0f;           // 0 -> 1 tras cambiar de preset
    float spawnAcc = 0.0f;
    juce::String prevTitle;
    juce::Image titleCache, glowCache;
    float cacheScale = 0.0f;
    juce::Random rng;
    PresetInfo info;
    juce::String titleShown() const;
    float match = -1.0f;
    bool matchVsReference = false;
    int frame = 0;
    std::unique_ptr<juce::FileChooser> chooser;
public:
    void rebuildReferenceList();
    void loadReferenceFile();
};

class VibePanel : public juce::Component, public Refreshable
{
public:
    explicit VibePanel (UiContext& c) : ctx (c) {}
    void paint (juce::Graphics&) override;
    void refresh() override;
    void animate() override { repaint (juce::Rectangle<int> (40, 26, 204, 176)); }
    static void paintRadar (juce::Graphics&, juce::Rectangle<float>, const analysis::Vibe& live, const analysis::Vibe& target, bool hasLive, bool big,
                            double time = 0.0, float energy = 0.0f);
private:
    UiContext& ctx;
    analysis::Vibe live, target;
    bool hasLive = false;
};

class MacroPanel : public juce::Component, public Refreshable
{
public:
    explicit MacroPanel (UiContext& c);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh() override;
    void rebind();
private:
    juce::String describeMacro (const juce::String& id) const;
    UiContext& ctx;
    juce::TextButton customize { "Customize" };
    juce::OwnedArray<Knob> knobs;
    int hovered = -1;
};

class MasterPanel : public juce::Component, public Refreshable
{
public:
    explicit MasterPanel (UiContext& c);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh() override;
    bool peakHold = true;
private:
    UiContext& ctx;
    Knob master;
    ParamToggle softClip;
    juce::TextButton hold { "H" };
    IconButton link { IconButton::link, "Link: el MASTER compensa el trim de entrada" };
    std::unique_ptr<juce::ButtonParameterAttachment> linkAttachment;
    float lvL = 0, lvR = 0, pkL = 0, pkR = 0;
    int holdL = 0, holdR = 0;
};

class OutputPanel : public juce::Component, public Refreshable
{
public:
    explicit OutputPanel (UiContext& c) : ctx (c) {}
    void paint (juce::Graphics&) override;
    void refresh() override;
    void mouseUp (const juce::MouseEvent&) override { ctx.proc.resetMeters(); }
private:
    UiContext& ctx;
    float l = 0, r = 0;
};

// ---------------------------------------------------------------------------
class LayerCard : public juce::Component
{
public:
    LayerCard (UiContext& c, int index);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick (int view);
private:
    UiContext& ctx;
    int index;
    PowerButton power;
    Knob pan;
    EditableSlider levelDrag;
    std::unique_ptr<APVTS::SliderAttachment> levelAttachment;
    std::array<float, 48> history {};
    int histPos = 0, view = 0;
};

class LayersPanel : public juce::Component, public Refreshable
{
public:
    explicit LayersPanel (UiContext& c);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh() override;
private:
    UiContext& ctx;
    juce::OwnedArray<LayerCard> cards;
    juce::TextButton viewWave { "WAVEFORM" }, viewSpec { "SPECTRUM" }, viewLevels { "LEVELS" };
    ParamChoice harmony, octave;
    Knob detune;
    int view = 0;
};

// ---------------------------------------------------------------------------
class FxCard : public juce::Component, public juce::SettableTooltipClient
{
public:
    enum Kind { clean = 0, tune, formant, eq, comp, sat, delay, reverb, mod };
    FxCard (UiContext& c, Kind k);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();
    Kind kind;
    bool draggable() const { return kind == eq || kind == comp || kind == sat; }
    std::function<void (FxCard*, const juce::MouseEvent&)> onDrag, onDrop;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    juce::Colour accent() const;
    static juce::String title (Kind k);
    float pulse = 0.0f;   // nivel de señal para el brillo (v3.1)
private:
    void paintViz (juce::Graphics&, juce::Rectangle<float>);
    UiContext& ctx;
    std::unique_ptr<PowerButton> power;
    std::unique_ptr<ParamChoice> choice, choice2;
    std::unique_ptr<MiniSlider> s1, s2;
    float gr = 0.0f, phase = 0.0f;
    bool dragging = false;
};

class ChainPanel : public juce::Component, public Refreshable
{
public:
    explicit ChainPanel (UiContext& c);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh() override;
    void animate() override;
private:
    struct Flow { float x, speed, size; juce::Colour colour; };
    std::vector<Flow> flow;
    float flowAcc = 0.0f;
    void layoutCards (bool animate = false);
    void cardDragged (FxCard*, const juce::MouseEvent&);
    void cardDropped (FxCard*, const juce::MouseEvent&);
    UiContext& ctx;
    juce::OwnedArray<FxCard> cards;
    // INPUT y OUTPUT
    Knob inTrim, ceiling;
    juce::TextButton autoButton { "AUTO" }, monitorButton { "MONITOR" };
    float inLevel = 0.0f, limGr = 0.0f;
    int lastOrder = -1;
    std::array<juce::Rectangle<int>, 9> slots;
    juce::Rectangle<int> inputCard, outputCard;
};

// ---------------------------------------------------------------------------
class RoutingModePanel : public juce::Component
{
public:
    explicit RoutingModePanel (UiContext& c);
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    Segmented modes;
};

class GlobalPanel : public juce::Component, public Refreshable
{
public:
    explicit GlobalPanel (UiContext& c);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh() override { repaint (latencyArea); }
private:
    UiContext& ctx;
    Knob trim, dryWet, tone;
    juce::Rectangle<int> latencyArea;
};

class VisualizerPanel : public juce::Component, public Refreshable
{
public:
    explicit VisualizerPanel (UiContext& c, bool compact = true);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh() override;
private:
    UiContext& ctx;
    juce::ComboBox mode, channels;
    juce::dsp::FFT fft { 12 };
    std::array<float, 8192> fftData {};
    std::array<float, 2048> spectrum {};
    std::array<float, 256> caps {}, capVel {};
    bool compact;
};

// ---------------------------------------------------------------------------
class CreatePage : public juce::Component, public Refreshable
{
public:
    explicit CreatePage (UiContext& c);
    void resized() override;
    void refresh() override;
    void animate() override;
    void presetChanged() { hero.presetChanged(); macros.rebind(); }
    HeroPanel hero;
    std::function<void()> onImageMenu;
private:
    juce::uint32 animFrame = 0;
    VibePanel vibe;
    MacroPanel macros;
    MasterPanel master;
    OutputPanel output;
    LayersPanel layers;
    ChainPanel chain;
    RoutingModePanel routing;
    GlobalPanel global;
    VisualizerPanel visualizer;
};
} // namespace pv::ui
