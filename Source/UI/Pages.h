#pragma once
#include "CreatePage.h"

namespace pv::ui
{
// ---------------------------------------------------------------------------
// Sección genérica: título + power opcional + fila de controles
class Section : public juce::Component
{
public:
    Section (UiContext& c, const juce::String& title, const juce::String& powerId, juce::Colour accent);
    Knob& knob (const juce::String& id, const juce::String& label);
    ParamChoice& choice (const juce::String& id, const juce::String& label);
    ParamToggle& toggle (const juce::String& id, const juce::String& text);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshEnabled();
    juce::Rectangle<int> content;   // zona para gráficos propios (lo que queda por encima de los controles)
    int controlsHeight = 92;
    juce::String note;
protected:
    UiContext& ctx;
    juce::String title;
    juce::Colour accent;
    std::unique_ptr<PowerButton> power;
    std::vector<std::unique_ptr<juce::Component>> items;
    bool lastOn = true;
};

// medidores profesionales (LUFS M/S/I, LRA, TP, PLR, correlación, GR por módulo)
class ProMeters : public juce::Component, public Refreshable
{
public:
    explicit ProMeters (UiContext& c) : ctx (c) {}
    void paint (juce::Graphics&) override;
    void refresh() override;
    void mouseUp (const juce::MouseEvent&) override { ctx.proc.resetMeters(); }
private:
    UiContext& ctx;
    float in = 0, out = 0, gr = 0, opto = 0, ds = 0, lim = 0, inPk = 0, outPk = 0;
    int inHold = 0, outHold = 0;
};

class EqGraph : public juce::Component, public Refreshable
{
public:
    explicit EqGraph (UiContext& c) : ctx (c) {}
    void paint (juce::Graphics&) override;
    void refresh() override { repaint(); }
private:
    UiContext& ctx;
};

class CompGraph : public juce::Component, public Refreshable
{
public:
    explicit CompGraph (UiContext& c) : ctx (c) {}
    void paint (juce::Graphics&) override;
    void refresh() override;
private:
    UiContext& ctx;
    float inDb = -60.0f, gr = 0.0f, opto = 0.0f;
};

// ---------------------------------------------------------------------------
class MixPage : public juce::Component, public Refreshable
{
public:
    explicit MixPage (UiContext& c);
    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh() override;
private:
    std::unique_ptr<Section> input, tune, eq, comp, deess, opto, color, motion, delay, reverb, advanced;
    EqGraph eqGraph;
    CompGraph compGraph;
    ProMeters meters;
    juce::TextButton advToggle { juce::String::fromUTF8 ("AVANZADO  \xe2\x96\xb8") };
    bool advOpen = false;
};

// ---------------------------------------------------------------------------
class FlowDiagram : public juce::Component, public Refreshable
{
public:
    explicit FlowDiagram (UiContext& c) : ctx (c) {}
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void refresh() override { repaint(); }
private:
    struct Node { juce::String name, powerId; juce::Colour colour; };
    std::vector<Node> nodes() const;
    juce::Rectangle<float> nodeBounds (int i, int count) const;
    UiContext& ctx;
};

class ChannelStrip : public juce::Component
{
public:
    ChannelStrip (UiContext& c, int index);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();
private:
    UiContext& ctx;
    int index;
    PowerButton power;
    Knob pan, drive, hp, lp, dly, rev;
    Fader fader;
};

class RoutingPage : public juce::Component, public Refreshable
{
public:
    explicit RoutingPage (UiContext& c);
    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh() override;
private:
    UiContext& ctx;
    FlowDiagram flow;
    juce::OwnedArray<ChannelStrip> strips;
    ParamChoice harmony, octave;
    Knob detune, dlyMix, revMix;
    Segmented route;
};

// ---------------------------------------------------------------------------
class StepLane : public juce::Component
{
public:
    StepLane (UiContext& c, int lane);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    juce::Rectangle<float> stepArea() const;
private:
    void edit (const juce::MouseEvent&, bool first);
    UiContext& ctx;
    int lane;
    Knob depth;
    juce::TextButton clear { "CLEAR" }, full { "FULL" }, randomise { "RANDOM" }, invert { "INVERT" };
    float dragValue = -1.0f;
};

class SequencePage : public juce::Component, public Refreshable
{
public:
    explicit SequencePage (UiContext& c);
    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh() override;
private:
    UiContext& ctx;
    PowerButton on;
    Segmented rate;
    juce::OwnedArray<StepLane> lanes;
};

// ---------------------------------------------------------------------------
class AnalyzePage : public juce::Component, public Refreshable
{
public:
    explicit AnalyzePage (UiContext& c);
    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh() override;
    std::function<void()> onReferenceChanged;
private:
    void rebuildList();
    UiContext& ctx;
    juce::ComboBox refBox;
    juce::TextButton load { "CARGAR REFERENCIA..." }, remove { "BORRAR" }, reset { "RESET ANALISIS" }, applyAll { "APLICAR TODO" };
    juce::OwnedArray<juce::TextButton> apply;
    std::unique_ptr<juce::FileChooser> chooser;
    analysis::Profile live;
    analysis::Match match;
    analysis::Vibe liveVibe;
    juce::String status;
    int frame = 0;
};
} // namespace pv::ui
