#include "Pages.h"

namespace pv::ui
{
namespace
{
void drawTitle (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& t, juce::Colour c = Pal::text)
{
    g.setFont (font (17.0f, true));
    g.setColour (c);
    drawSpaced (g, t, r, 0.8f, juce::Justification::centredLeft);
}
float freqToX (float f, juce::Rectangle<float> r) { return r.getX() + r.getWidth() * std::log (f / 20.0f) / std::log (1000.0f); }
} // namespace

// ============================================================================
Section::Section (UiContext& c, const juce::String& t, const juce::String& powerId, juce::Colour a)
    : ctx (c), title (t), accent (a)
{
    if (powerId.isNotEmpty())
    {
        power = std::make_unique<PowerButton> (c.proc.apvts, powerId, a);
        addAndMakeVisible (*power);
    }
}

Knob& Section::knob (const juce::String& id, const juce::String& l)
{
    auto k = std::make_unique<Knob> (ctx.proc.apvts, id, l, Knob::medium, accent);
    auto& ref = *k;
    addAndMakeVisible (*k);
    items.push_back (std::move (k));
    return ref;
}

ParamChoice& Section::choice (const juce::String& id, const juce::String& l)
{
    auto c = std::make_unique<ParamChoice> (ctx.proc.apvts, id, l);
    auto& ref = *c;
    addAndMakeVisible (*c);
    items.push_back (std::move (c));
    return ref;
}

ParamToggle& Section::toggle (const juce::String& id, const juce::String& text)
{
    auto t = std::make_unique<ParamToggle> (ctx.proc.apvts, id, text);
    auto& ref = *t;
    addAndMakeVisible (*t);
    items.push_back (std::move (t));
    return ref;
}

void Section::refreshEnabled()
{
    const bool on = power == nullptr || power->getToggleState();
    if (on != lastOn)
    {
        lastOn = on;
        for (auto& it : items) it->setEnabled (on);
        repaint();
    }
}

void Section::resized()
{
    auto r = getLocalBounds().reduced (8, 6);
    auto header = r.removeFromTop (24);
    if (power) power->setBounds (header.removeFromRight (24).withSizeKeepingCentre (22, 22));
    r.removeFromTop (2);
    // controlsHeight <= 0: los controles ocupan toda la sección (centrados)
    auto controls = controlsHeight <= 0 ? r : r.removeFromBottom (juce::jmin (controlsHeight, r.getHeight()));
    content = controlsHeight <= 0 ? juce::Rectangle<int>() : r;
    if (items.empty()) return;
    auto weight = [&] (size_t i) { return dynamic_cast<ParamChoice*> (items[i].get()) != nullptr ? 1.7f
                                        : dynamic_cast<ParamToggle*> (items[i].get()) != nullptr ? 0.8f : 1.0f; };
    float total = 0.0f;
    for (size_t i = 0; i < items.size(); ++i) total += weight (i);
    float x = (float) controls.getX();
    for (size_t i = 0; i < items.size(); ++i)
    {
        const float cw = (float) controls.getWidth() * weight (i) / total;
        auto cell = juce::Rectangle<float> (x, (float) controls.getY(), cw, (float) controls.getHeight()).toNearestInt().reduced (2, 0);
        if (auto* t = dynamic_cast<ParamToggle*> (items[i].get()))
            t->setBounds (cell.withSizeKeepingCentre (juce::jmin (cell.getWidth(), 70), 26).translated (0, -8));
        else if (auto* ch = dynamic_cast<ParamChoice*> (items[i].get()))
            ch->setBounds (cell.withSizeKeepingCentre (cell.getWidth(), 48).translated (0, -6));
        else
            items[i]->setBounds (cell.withSizeKeepingCentre (cell.getWidth(), juce::jmin (cell.getHeight(), cell.getWidth() + 34)));
        x += cw;
    }
}

void Section::paint (juce::Graphics& g)
{
    drawPanel (g, getLocalBounds().toFloat(), accent, lastOn ? 0.45f : 0.15f);
    auto header = getLocalBounds().toFloat().reduced (12.0f, 6.0f).removeFromTop (24.0f);
    g.setColour (accent.withAlpha (lastOn ? 0.95f : 0.35f));
    g.fillRoundedRectangle (header.removeFromLeft (4.0f).withSizeKeepingCentre (4.0f, 14.0f), 2.0f);
    header.removeFromLeft (8.0f);
    drawTitle (g, header, this->title, lastOn ? Pal::text : Pal::dim);
    if (note.isNotEmpty())
    {
        g.setFont (font (12.5f, true));
        g.setColour (Pal::dim);
        g.drawText (note, header.withTrimmedRight (30.0f), juce::Justification::centredRight);
    }
}

// ============================================================================
void ProMeters::refresh()
{
    const float i = meterNorm (ctx.proc.inputLevel.load()), o = meterNorm (ctx.proc.outputLevel.load());
    in = i > in ? i : in * 0.92f;
    out = o > out ? o : out * 0.92f;
    if (i >= inPk) { inPk = i; inHold = 40; } else if (--inHold < 0) inPk *= 0.97f;
    if (o >= outPk) { outPk = o; outHold = 40; } else if (--outHold < 0) outPk *= 0.97f;
    gr += (juce::jlimit (0.0f, 1.0f, ctx.proc.compGainReduction.load() / 20.0f) - gr) * 0.4f;
    opto += (juce::jlimit (0.0f, 1.0f, ctx.proc.optoGainReduction.load() / 20.0f) - opto) * 0.4f;
    ds += (juce::jlimit (0.0f, 1.0f, ctx.proc.deessGainReduction.load() / 15.0f) - ds) * 0.4f;
    lim += (juce::jlimit (0.0f, 1.0f, ctx.proc.limiterGainReduction.load() / 12.0f) - lim) * 0.4f;
    repaint();
}

void ProMeters::paint (juce::Graphics& g)
{
    using namespace juce;
    const auto b = getLocalBounds().toFloat();
    drawPanel (g, b);
    drawTitle (g, { 12.0f, 8.0f, b.getWidth() - 20.0f, 20.0f }, "METERS");

    auto r = b.reduced (10.0f, 0.0f).withTrimmedTop (36.0f);
    auto bars = r.removeFromTop (r.getHeight() * 0.45f);
    auto labels = bars.removeFromBottom (16.0f);
    const float bw = bars.getWidth() / 6.0f;
    auto bar = [&] (int k, float v, float pk, bool down, Colour c1, Colour c2, const String& name)
    {
        auto br = Rectangle<float> (bars.getX() + bw * (float) k + 3.0f, bars.getY(), bw - 6.0f, bars.getHeight());
        paintBarMeter (g, br, v, pk, c1, c2, down);
        g.setFont (font (11.5f, true));
        g.setColour (Pal::dim);
        g.drawText (name, Rectangle<float> (br.getX() - 6.0f, labels.getY(), br.getWidth() + 12.0f, 16.0f), Justification::centred);
        return br;
    };
    bar (0, in, inPk, false, Pal::cyan, Pal::violet, "IN");
    bar (1, out, outPk, false, Pal::cyan, Pal::violet, "OUT");
    auto grBar = bar (2, gr, 0.0f, true, Pal::orange, Pal::magenta, "COMP");
    bar (3, opto, 0.0f, true, Pal::violet, Pal::cyan, "OPTO");
    bar (4, ds, 0.0f, true, Pal::lime, Pal::cyan, "DS");
    bar (5, lim, 0.0f, true, Pal::red, Pal::orange, "LIM");
    // objetivo de GR del preset
    const float t = jlimit (0.0f, 1.0f, ctx.proc.grTarget.load() / 20.0f);
    g.setColour (Colours::white.withAlpha (0.95f));
    g.fillRect (grBar.getX() - 2.0f, grBar.getY() + grBar.getHeight() * t - 1.0f, grBar.getWidth() + 4.0f, 2.0f);

    r.removeFromTop (12.0f);
    auto lu = [] (float v) { return v < -69.0f ? String ("--") : String (v, 1); };
    const float m = ctx.proc.outLoudness.momentary.load(), st = ctx.proc.outLoudness.shortTerm.load(), integ = ctx.proc.outLoudness.integrated.load();
    const float lra = ctx.proc.outLoudness.loudnessRange.load();
    const float tp = Decibels::gainToDecibels (ctx.proc.truePeakMax.load(), -99.0f);
    auto row = [&] (const String& name, const String& value, Colour c)
    {
        auto rr = r.removeFromTop (22.0f);
        g.setFont (font (13.5f, true));
        g.setColour (Pal::dim);
        g.drawText (name, rr, Justification::centredLeft);
        g.setFont (font (17.0f, true));
        g.setColour (c);
        g.drawText (value, rr, Justification::centredRight);
    };
    row ("MOMENTARY", lu (m), Pal::text);
    row ("SHORT-TERM", lu (st), Pal::text);
    row ("INTEGRATED", lu (integ) + " LUFS", Pal::lilac);
    row ("RANGE (LRA)", String (lra, 1) + " LU", Pal::text);
    row ("TRUE PEAK", tp < -90.0f ? String ("--") : String (tp, 1) + " dBTP", tp > ctx.value ("ceiling") + 0.05f ? Pal::red : Pal::text);
    row ("PLR", integ > -69.0f && tp > -90.0f ? String (tp - integ, 1) + " dB" : String ("--"), Pal::text);
    r.removeFromTop (8.0f);
    const float corr = ctx.proc.correlation.value.load();
    auto cb = r.removeFromTop (10.0f);
    g.setColour (Pal::track);
    g.fillRoundedRectangle (cb, 3.0f);
    const float x = cb.getCentreX() + corr * cb.getWidth() * 0.5f;
    g.setColour (corr < 0.0f ? Pal::red : corr < 0.3f ? Pal::orange : Pal::lime);
    g.fillEllipse (x - 5.0f, cb.getCentreY() - 5.0f, 10.0f, 10.0f);
    g.setFont (font (12.0f, true));
    g.setColour (Pal::dim);
    g.drawText ("-1   CORRELACION   +1", r.removeFromTop (16.0f), Justification::centred);
    g.drawText ("clic: reset I / LRA / TP", r.removeFromTop (16.0f), Justification::centred);
}

// ============================================================================
void EqGraph::paint (juce::Graphics& g)
{
    using namespace juce;
    const auto r = getLocalBounds().toFloat();
    g.setColour (Colours::black.withAlpha (0.3f));
    g.fillRoundedRectangle (r, 4.0f);
    const double fs = 48000.0;
    using C = dsp::IIR::Coefficients<float>;
    const bool on = ctx.value ("eqOn") > 0.5f;
    const float hp = ctx.value ("hpFreq");
    std::vector<C::Ptr> filters;
    if (hp > 21.0f) { filters.push_back (C::makeHighPass (fs, hp, 0.5412f)); filters.push_back (C::makeHighPass (fs, hp, 1.3066f)); }
    if (on)
    {
        filters.push_back (C::makeLowShelf (fs, 150.0f, 0.7f, Decibels::decibelsToGain (ctx.value ("eqBody"))));
        filters.push_back (C::makePeakFilter (fs, 260.0f, 1.2f, Decibels::decibelsToGain (ctx.value ("eqMud"))));
        filters.push_back (C::makePeakFilter (fs, 3500.0f, 1.0f, Decibels::decibelsToGain (ctx.value ("eqPres"))));
        filters.push_back (C::makeHighShelf (fs, 11500.0f, 0.7f, Decibels::decibelsToGain (ctx.value ("eqAir"))));
    }
    const float tilt = ctx.value ("toneTilt");
    if (std::abs (tilt) > 0.01f)
    {
        filters.push_back (C::makeLowShelf (fs, 1000.0f, 0.5f, Decibels::decibelsToGain (-0.5f * tilt)));
        filters.push_back (C::makeHighShelf (fs, 1000.0f, 0.5f, Decibels::decibelsToGain (0.5f * tilt)));
    }
    auto yFor = [&] (float db) { return r.getCentreY() - db / 15.0f * r.getHeight() * 0.5f; };
    g.setFont (font (11.0f, true));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = freqToX (f, r);
        g.setColour (Pal::violet.withAlpha (0.1f));
        g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        g.setColour (Pal::dim);
        g.drawText (f >= 1000 ? String ((int) (f / 1000)) + "k" : String ((int) f), Rectangle<float> (x - 15.0f, r.getBottom() - 13.0f, 30.0f, 12.0f), Justification::centred);
    }
    for (float db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
    {
        g.setColour (Pal::violet.withAlpha (db == 0.0f ? 0.3f : 0.08f));
        g.drawHorizontalLine ((int) yFor (db), r.getX(), r.getRight());
    }
    Path p;
    for (int i = 0; i <= 200; ++i)
    {
        const float f = 20.0f * std::pow (1000.0f, (float) i / 200.0f);
        double mag = 1.0;
        for (auto& c : filters) mag *= c->getMagnitudeForFrequency (f, fs);
        const float y = jlimit (r.getY(), r.getBottom(), yFor ((float) Decibels::gainToDecibels (mag, -60.0)));
        if (i == 0) p.startNewSubPath (freqToX (f, r), y); else p.lineTo (freqToX (f, r), y);
    }
    Path fill (p);
    fill.lineTo (r.getRight(), r.getCentreY());
    fill.lineTo (r.getX(), r.getCentreY());
    fill.closeSubPath();
    g.setColour (Pal::blue.withAlpha (0.12f));
    g.fillPath (fill);
    strokeGlow (g, p, Pal::blue.brighter (0.3f), 1.8f, 3);
    // banda dinámica de dureza (3.2 kHz): recorte máximo
    const float harsh = ctx.value ("eqHarsh");
    if (on && harsh > 0.05f)
    {
        const float x = freqToX (3200.0f, r);
        g.setColour (Pal::orange.withAlpha (0.8f));
        g.drawArrow (Line<float> (x, yFor (0.0f), x, yFor (-harsh)), 1.4f, 6.0f, 6.0f);
        g.setFont (font (11.5f, true));
        g.drawText ("DYN -" + String (harsh, 1), Rectangle<float> (x + 4.0f, yFor (-harsh) - 6.0f, 70.0f, 12.0f), Justification::centredLeft);
    }
}

void CompGraph::refresh()
{
    inDb += (ctx.proc.compInputDb.load() - inDb) * 0.4f;
    gr += (ctx.proc.compGainReduction.load() - gr) * 0.4f;
    opto += (ctx.proc.optoGainReduction.load() - opto) * 0.4f;
    repaint();
}

void CompGraph::paint (juce::Graphics& g)
{
    using namespace juce;
    const auto b = getLocalBounds().toFloat();
    auto r = b.withWidth (b.getHeight());
    g.setColour (Colours::black.withAlpha (0.3f));
    g.fillRoundedRectangle (r, 4.0f);
    const float thr = ctx.value ("compThr"), ratio = ctx.value ("compRatio"), knee = 6.0f;
    auto toX = [&] (float db) { return r.getX() + r.getWidth() * (db + 60.0f) / 60.0f; };
    auto toY = [&] (float db) { return r.getBottom() - r.getHeight() * (db + 60.0f) / 60.0f; };
    g.setColour (Pal::violet.withAlpha (0.1f));
    for (float db = -48.0f; db < 0.0f; db += 12.0f) { g.drawVerticalLine ((int) toX (db), r.getY(), r.getBottom()); g.drawHorizontalLine ((int) toY (db), r.getX(), r.getRight()); }
    g.setColour (Colours::white.withAlpha (0.15f));
    g.drawLine (toX (-60.0f), toY (-60.0f), toX (0.0f), toY (0.0f), 1.0f);
    auto curve = [&] (float x)
    {
        const float over = x - thr, slope = 1.0f / ratio - 1.0f;
        if (2.0f * over < -knee) return x;
        if (2.0f * std::abs (over) <= knee) return x + slope * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.0f * knee);
        return x + slope * over;
    };
    Path p;
    for (int i = 0; i <= 120; ++i)
    {
        const float x = -60.0f + 60.0f * (float) i / 120.0f;
        if (i == 0) p.startNewSubPath (toX (x), toY (curve (x))); else p.lineTo (toX (x), toY (curve (x)));
    }
    strokeGlow (g, p, Pal::orange, 1.8f, 3);
    g.setColour (Pal::orange.withAlpha (0.5f));
    g.drawVerticalLine ((int) toX (thr), r.getY(), r.getBottom());
    // punto de la señal actual
    const float x = jlimit (-60.0f, 0.0f, inDb);
    g.setColour (Colours::white);
    g.fillEllipse (toX (x) - 4.0f, toY (curve (x)) - 4.0f, 8.0f, 8.0f);
    // barras de GR
    auto bars = b.withTrimmedLeft (r.getWidth() + 10.0f);
    auto bar = [&] (Rectangle<float> br, float db, Colour c, const String& name)
    {
        paintBarMeter (g, br.withTrimmedBottom (16.0f), jlimit (0.0f, 1.0f, db / 20.0f), 0.0f, c, Pal::magenta, true);
        g.setFont (font (11.5f, true));
        g.setColour (Pal::dim);
        g.drawText (name, br.removeFromBottom (14.0f).expanded (8.0f, 0.0f), Justification::centred);
    };
    bar (bars.removeFromLeft (12.0f), gr, Pal::orange, "GR");
    bars.removeFromLeft (10.0f);
    bar (bars.removeFromLeft (12.0f), opto, Pal::violet, "OP");
    g.setFont (font (14.0f, true));
    g.setColour (Pal::text);
    g.drawText ("-" + String (gr, 1) + " dB", Rectangle<float> (r.getRight() + 50.0f, r.getY(), 70.0f, 18.0f), Justification::centredLeft);
}

// ============================================================================
MixPage::MixPage (UiContext& c) : eqGraph (c), compGraph (c), meters (c)
{
    input = std::make_unique<Section> (c, "INPUT & CLEAN", "", Pal::cyan);
    input->knob ("inGain", "Trim"); input->knob ("hpFreq", "HPF"); input->knob ("gateThr", "Gate");

    tune = std::make_unique<Section> (c, "DNA TUNE", "tuneOn", Pal::magenta);
    tune->choice ("tuneKey", "Key"); tune->choice ("tuneScale", "Scale");
    tune->knob ("tuneRetune", "Retune"); tune->knob ("tuneHumanize", "Humanize"); tune->knob ("tuneAmount", "Amount");
    tune->knob ("formant", "Formant"); tune->toggle ("tuneFormantPreserve", "KEEP");
    tune->note = "Transition, Flex, Vibrato y Confidence en AVANZADO";

    eq = std::make_unique<Section> (c, "TONE EQ", "eqOn", Pal::blue);
    eq->knob ("eqBody", "Body"); eq->knob ("eqMud", "Mud"); eq->knob ("eqHarsh", "Harsh"); eq->knob ("eqPres", "Presence");
    eq->knob ("eqAir", "Air"); eq->knob ("eqExcite", "Exciter");

    comp = std::make_unique<Section> (c, "COMPRESSOR", "compOn", Pal::orange);
    comp->choice ("compModel", "Model"); comp->knob ("compThr", "Thresh"); comp->knob ("compRatio", "Ratio");
    comp->knob ("compAtt", "Attack"); comp->knob ("compRel", "Release"); comp->knob ("compMakeup", "Makeup"); comp->knob ("compPar", "NY Mix");

    deess = std::make_unique<Section> (c, "DE-ESS", "deessOn", Pal::lime);
    deess->knob ("deessThr", "Thresh"); deess->knob ("deessFreq", "Freq");
    opto = std::make_unique<Section> (c, "OPTO", "optoOn", Pal::violet);
    opto->knob ("optoAmount", "Amount"); opto->knob ("optoGain", "Gain");

    color = std::make_unique<Section> (c, "COLOR", "satOn", Pal::amber);
    color->choice ("satMode", "Type"); color->knob ("satDrive", "Drive"); color->knob ("satMix", "Mix");
    motion = std::make_unique<Section> (c, "MOTION", "modOn", Pal::cyan);
    motion->choice ("modMode", "Type"); motion->knob ("modRate", "Rate"); motion->knob ("modDepth", "Depth"); motion->knob ("modMix", "Mix");
    delay = std::make_unique<Section> (c, "DELAY", "dlyOn", Pal::violet);
    delay->choice ("dlyTime", "Time"); delay->knob ("dlyFb", "Feedback"); delay->knob ("dlyDuck", "Duck"); delay->knob ("dlyMix", "Mix");
    reverb = std::make_unique<Section> (c, "REVERB", "revOn", Pal::blue);
    reverb->choice ("revType", "Type"); reverb->knob ("revDecay", "Decay"); reverb->knob ("revPredelay", "Predelay");
    reverb->knob ("revTone", "Tone"); reverb->knob ("revMix", "Mix");

    advanced = std::make_unique<Section> (c, "AVANZADO", "", Pal::lilac);
    advanced->knob ("tuneTransition", "Transition"); advanced->knob ("tuneFlex", "Flex"); advanced->knob ("tuneVibrato", "Vibrato");
    advanced->knob ("tuneConfidence", "Confidence"); advanced->knob ("revDuck", "Rev Duck"); advanced->knob ("layDetune", "Detune");
    advanced->knob ("toneTilt", "Tilt"); advanced->knob ("ceiling", "Ceiling"); advanced->choice ("chainOrder", "Insert Order");
    advanced->choice ("routeMode", "FX Routing"); advanced->toggle ("softClip", "SOFT"); advanced->toggle ("monoCheck", "MONO");
    advanced->toggle ("ioLink", "LINK");
    advanced->note = "orden de insertos, ruteo de envios, techo y comprobaciones";

    for (auto* s : { input.get(), tune.get(), eq.get(), comp.get(), deess.get(), opto.get(), color.get(), motion.get(), delay.get(), reverb.get(), advanced.get() })
        addAndMakeVisible (s);
    eq->controlsHeight = 86;
    comp->controlsHeight = 86;
    eq->addAndMakeVisible (eqGraph);
    comp->addAndMakeVisible (compGraph);
    addAndMakeVisible (meters);
    advToggle.setClickingTogglesState (true);
    advToggle.getProperties().set ("fontSize", 14.0f);
    advToggle.onClick = [this]
    {
        advOpen = advToggle.getToggleState();
        advToggle.setButtonText (juce::String::fromUTF8 (advOpen ? "AVANZADO  \xe2\x96\xbe" : "AVANZADO  \xe2\x96\xb8"));
        resized();
    };
    addAndMakeVisible (advToggle);
}

void MixPage::resized()
{
    const int W = getWidth(), H = getHeight(), gap = 8;
    const int meterW = 168;
    meters.setBounds (W - meterW, 0, meterW, H);
    const int areaW = W - meterW - gap;
    const int advH = advOpen ? 150 : 0;
    const int toggleH = 30;
    const int rowsH = H - advH - toggleH - gap * (advOpen ? 2 : 1);
    const int r1 = (int) ((float) rowsH * 0.22f), r2 = (int) ((float) rowsH * 0.50f), r3 = rowsH - r1 - r2 - 2 * gap;

    auto row = [&] (int y, int h, std::vector<std::pair<Section*, float>> cells)
    {
        float units = 0;
        for (auto& [s, u] : cells) units += u;
        const float unitW = (float) (areaW - gap * ((int) cells.size() - 1)) / units;
        float x = 0;
        for (auto& [s, u] : cells) { s->setBounds (juce::roundToInt (x), y, juce::roundToInt (unitW * u), h); x += unitW * u + (float) gap; }
    };
    row (0, r1, { { input.get(), 3.0f }, { tune.get(), 8.6f } });
    row (r1 + gap, r2, { { eq.get(), 5.4f }, { comp.get(), 5.6f } });
    row (r1 + r2 + 2 * gap, r3, { { deess.get(), 2.0f }, { opto.get(), 2.0f }, { color.get(), 3.2f }, { motion.get(), 4.0f }, { delay.get(), 4.0f }, { reverb.get(), 4.8f } });
    // en la fila 3 hay 6 secciones: apila DE-ESS y OPTO para dar aire
    {
        const auto d = deess->getBounds(), o = opto->getBounds();
        const int w = (o.getRight() - d.getX());
        deess->setBounds (d.getX(), d.getY(), w, d.getHeight() / 2 - gap / 2);
        opto->setBounds (d.getX(), d.getY() + d.getHeight() / 2 + gap / 2, w, d.getHeight() / 2 - gap / 2);
        deess->controlsHeight = opto->controlsHeight = 0;
        // repartir el espacio liberado
        const int freed = w + gap - (o.getRight() - d.getX());
        juce::ignoreUnused (freed);
        const int x0 = d.getX() + w + gap;
        const float rest = (float) (areaW - x0);
        const float units = 3.2f + 4.0f + 4.0f + 4.8f;
        float x = (float) x0;
        for (auto [s, u] : std::initializer_list<std::pair<Section*, float>> { { color.get(), 3.2f }, { motion.get(), 4.0f }, { delay.get(), 4.0f }, { reverb.get(), 4.8f } })
        {
            const float w2 = (rest - (float) gap * 3.0f) * u / units;
            s->setBounds (juce::roundToInt (x), d.getY(), juce::roundToInt (w2), d.getHeight());
            x += w2 + (float) gap;
        }
        deess->resized(); opto->resized();
    }
    for (auto* s : { input.get(), tune.get(), color.get(), motion.get(), delay.get(), reverb.get() }) s->controlsHeight = 0;
    advToggle.setBounds (0, rowsH + gap, 160, toggleH);
    advanced->setVisible (advOpen);
    if (advOpen) advanced->setBounds (0, rowsH + gap + toggleH + gap, areaW, advH);
    advanced->controlsHeight = 0;
    for (auto* s : { input.get(), tune.get(), eq.get(), comp.get(), color.get(), motion.get(), delay.get(), reverb.get(), advanced.get() }) s->resized();
    eqGraph.setBounds (eq->content.reduced (4, 2));
    compGraph.setBounds (comp->content.reduced (4, 2));
}

void MixPage::paint (juce::Graphics& g)
{
    g.setFont (font (12.5f, true));
    g.setColour (Pal::dim);
    g.drawText (juce::String::fromUTF8 ("doble clic en un control: escribir valor  \xc2\xb7  Ctrl/Alt+clic: valor por defecto"), advToggle.getBounds().withX (180).withWidth (600), juce::Justification::centredLeft);
}

void MixPage::refresh()
{
    for (auto* s : { tune.get(), eq.get(), comp.get(), deess.get(), opto.get(), color.get(), motion.get(), delay.get(), reverb.get() }) s->refreshEnabled();
    eqGraph.refresh();
    compGraph.refresh();
    meters.refresh();
}

// ============================================================================
std::vector<FlowDiagram::Node> FlowDiagram::nodes() const
{
    using namespace juce;
    std::vector<Node> n { { "INPUT", "", Pal::dim }, { "CLEAN", "", Pal::violet }, { "TUNE", "tuneOn", Pal::cyan }, { "FORMANT", "", Pal::teal },
                          { "DE-ESS", "deessOn", Pal::lime } };
    const auto order = VoxDnaProcessor::chainOrderFor ((int) ctx.value ("chainOrder"));
    const Node stage[3] { { "EQ", "eqOn", Pal::blue }, { "COMP", "compOn", Pal::orange }, { "COLOR", "satOn", Pal::amber } };
    for (int s : order) n.push_back (stage[s]);
    const int route = (int) ctx.value ("routeMode");
    static const char* rn[] { "SERIES", "PARALLEL", "MID/SIDE", "SPLIT" };
    n.push_back ({ "LAYERS", "", Pal::pink });
    n.push_back ({ "MOTION", "modOn", Pal::cyan });
    n.push_back ({ "SEQ", "seqOn", Pal::magenta });
    n.push_back ({ String ("FX ") + rn[jlimit (0, 3, route)], "", Pal::violet });
    n.push_back ({ "TILT", "", Pal::lilac });
    n.push_back ({ "MASTER", "", Pal::text });
    n.push_back ({ "LIMIT", "", Pal::red });
    return n;
}

juce::Rectangle<float> FlowDiagram::nodeBounds (int i, int count) const
{
    auto r = getLocalBounds().toFloat().reduced (14.0f, 0.0f);
    r.removeFromTop (38.0f);
    const float gap = 12.0f;
    const float w = (r.getWidth() - gap * (float) (count - 1)) / (float) count;
    return { r.getX() + (w + gap) * (float) i, r.getY(), w, juce::jmin (40.0f, r.getHeight() - 30.0f) };
}

void FlowDiagram::paint (juce::Graphics& g)
{
    using namespace juce;
    drawPanel (g, getLocalBounds().toFloat());
    drawTitle (g, { 14.0f, 8.0f, 600.0f, 20.0f }, "SIGNAL FLOW");
    g.setFont (font (12.5f, true));
    g.setColour (Pal::dim);
    g.drawText ("clic en un modulo = on/off  /  el orden EQ/COMP/COLOR se cambia en CREATE o en AVANZADO", Rectangle<int> (150, 8, 700, 20), Justification::centredLeft);
    const auto ns = nodes();
    for (int i = 0; i < (int) ns.size(); ++i)
    {
        auto b = nodeBounds (i, (int) ns.size());
        bool on = true;
        if (ns[(size_t) i].powerId.isNotEmpty()) on = ctx.value (ns[(size_t) i].powerId) > 0.5f;
        if (ns[(size_t) i].name == "LAYERS") { on = false; for (int l = 0; l < pv::numLayers; ++l) on = on || ctx.value ("layOn" + String (l)) > 0.5f; }
        const auto col = ns[(size_t) i].colour;
        if (on) { g.setColour (col.withAlpha (0.16f)); g.fillRoundedRectangle (b.expanded (2.0f), 6.0f); }
        g.setColour (Pal::bg.withAlpha (0.9f));
        g.fillRoundedRectangle (b, 5.0f);
        g.setColour (on ? col : Pal::faint);
        g.drawRoundedRectangle (b, 5.0f, on ? 1.4f : 1.0f);
        g.setFont (font (13.0f, true));
        g.setColour (on ? Pal::text : Pal::dim);
        g.drawFittedText (ns[(size_t) i].name, b.toNearestInt().reduced (2, 0), Justification::centred, 2);
        if (i < (int) ns.size() - 1)
        {
            auto next = nodeBounds (i + 1, (int) ns.size());
            g.setColour (Pal::violet.withAlpha (0.8f));
            g.drawArrow ({ b.getRight() + 1.0f, b.getCentreY(), next.getX() - 1.0f, b.getCentreY() }, 1.2f, 5.0f, 5.0f);
        }
    }
    const int lat = ctx.proc.reportedLatency.load();
    const double sr = ctx.proc.getSampleRate() > 0 ? ctx.proc.getSampleRate() : 48000.0;
    const int q = ctx.proc.getQuality();
    g.setFont (font (13.0f, true));
    g.setColour (Pal::dim);
    g.drawText (String (q == 0 ? "MONITOR" : q == 1 ? "MIX" : "RENDER HQ") + "  /  latencia reportada al host: " + String (lat) + " muestras ("
                    + String (lat * 1000.0 / sr, 1) + " ms)  /  compensacion automatica (PDC)",
                getLocalBounds().reduced (14, 6).removeFromBottom (18), Justification::centredLeft);
}

void FlowDiagram::mouseUp (const juce::MouseEvent& e)
{
    const auto ns = nodes();
    for (int i = 0; i < (int) ns.size(); ++i)
        if (nodeBounds (i, (int) ns.size()).contains (e.position) && ns[(size_t) i].powerId.isNotEmpty())
        {
            ctx.setValue (ns[(size_t) i].powerId, ctx.value (ns[(size_t) i].powerId) > 0.5f ? 0.0f : 1.0f);
            repaint();
        }
}

// ============================================================================
ChannelStrip::ChannelStrip (UiContext& c, int i)
    : ctx (c), index (i), power (c.proc.apvts, "layOn" + juce::String (i), Pal::layer (i)),
      pan (c.proc.apvts, "layPan" + juce::String (i), "Pan", Knob::small, Pal::layer (i)),
      drive (c.proc.apvts, "layDrv" + juce::String (i), "Drive", Knob::small, Pal::layer (i)),
      hp (c.proc.apvts, "layHp" + juce::String (i), "HPF", Knob::small, Pal::layer (i)),
      lp (c.proc.apvts, "layLp" + juce::String (i), "LPF", Knob::small, Pal::layer (i)),
      dly (c.proc.apvts, "layDly" + juce::String (i), "Dly Send", Knob::small, Pal::layer (i)),
      rev (c.proc.apvts, "layRev" + juce::String (i), "Rev Send", Knob::small, Pal::layer (i)),
      fader (c.proc.apvts, "layLvl" + juce::String (i), Pal::layer (i))
{
    for (juce::Component* k : std::initializer_list<juce::Component*> { &power, &pan, &drive, &hp, &lp, &dly, &rev, &fader }) addAndMakeVisible (k);
    fader.slider.onValueChange = [this] { repaint(); };
}

void ChannelStrip::resized()
{
    const int w = getWidth();
    power.setBounds (w - 28, 6, 22, 22);
    const int cw = (w - 12) / 2, kh = 74;
    int y = 34;
    for (auto [a, b] : std::initializer_list<std::pair<Knob*, Knob*>> { { &pan, &drive }, { &hp, &lp }, { &dly, &rev } })
    {
        a->setBounds (6, y, cw, kh);
        b->setBounds (6 + cw, y, cw, kh);
        y += kh + 4;
    }
    fader.setBounds (w / 2 - 22, y + 6, 44, getHeight() - y - 36);
}

void ChannelStrip::tick()
{
    fader.meter = meterNorm (ctx.proc.layers.getLevel (index));
    fader.repaint();
}

void ChannelStrip::paint (juce::Graphics& g)
{
    using namespace juce;
    const auto b = getLocalBounds().toFloat();
    const auto col = Pal::layer (index);
    const bool on = power.getToggleState();
    g.setGradientFill (ColourGradient (col.withAlpha (on ? 0.14f : 0.04f), 0, 0, Pal::panel.withAlpha (0.95f), 0, b.getBottom(), false));
    g.fillRoundedRectangle (b, 6.0f);
    g.setColour (col.withAlpha (on ? 0.55f : 0.18f));
    g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, 1.0f);
    auto badge = Rectangle<float> (8.0f, 8.0f, 18.0f, 18.0f);
    g.setColour (col.withAlpha (on ? 0.95f : 0.35f));
    g.fillRoundedRectangle (badge, 3.0f);
    g.setColour (Colours::white);
    g.setFont (font (13.0f, true));
    g.drawText (String (index + 1), badge, Justification::centred);
    g.setFont (font (15.5f, true));
    g.setColour (on ? Pal::text : Pal::dim);
    g.drawText (LayerEngine::routeNames()[index], Rectangle<float> (32.0f, 6.0f, b.getWidth() - 64.0f, 22.0f), Justification::centredLeft);
    g.setFont (font (16.0f, true));
    g.drawText (fader.slider.getTextFromValue (fader.slider.getValue()), Rectangle<float> (0.0f, b.getBottom() - 26.0f, b.getWidth(), 20.0f), Justification::centred);
}

RoutingPage::RoutingPage (UiContext& c)
    : ctx (c), flow (c), harmony (c.proc.apvts, "harmInterval", "HARMONY INTERVAL"), octave (c.proc.apvts, "octMode", "OCTAVE"),
      detune (c.proc.apvts, "layDetune", "Double Detune", Knob::medium, Pal::pink),
      dlyMix (c.proc.apvts, "dlyMix", "Delay Return", Knob::medium), revMix (c.proc.apvts, "revMix", "Reverb Return", Knob::medium, Pal::blue),
      route (c.proc.apvts, "routeMode", {}, 14.0f)
{
    addAndMakeVisible (flow);
    for (int i = 0; i < pv::numLayers; ++i) addAndMakeVisible (strips.add (new ChannelStrip (ctx, i)));
    for (juce::Component* k : std::initializer_list<juce::Component*> { &harmony, &octave, &detune, &dlyMix, &revMix, &route }) addAndMakeVisible (k);
}

void RoutingPage::resized()
{
    flow.setBounds (0, 0, getWidth(), 120);
    const int y = 128, h = 560, gap = 8;
    const int w = (getWidth() - gap * 7) / 8;
    for (int i = 0; i < strips.size(); ++i) strips[i]->setBounds (i * (w + gap), y, w, h);
    const int by = y + h + gap;
    harmony.setBounds (16, by + 40, 200, 48);
    octave.setBounds (16, by + 104, 200, 48);
    detune.setBounds (230, by + 34, 110, 120);
    dlyMix.setBounds (350, by + 34, 110, 120);
    revMix.setBounds (470, by + 34, 110, 120);
    route.setBounds (600, by + 64, 340, 36);
}

void RoutingPage::paint (juce::Graphics& g)
{
    using namespace juce;
    const int by = 128 + 560 + 8;
    auto panel = Rectangle<float> (0.0f, (float) by, (float) getWidth(), (float) (getHeight() - by));
    drawPanel (g, panel);
    drawTitle (g, { 14.0f, (float) by + 8.0f, 400.0f, 20.0f }, "LAYER BUS  /  SENDS");
    g.setFont (font (13.0f, true));
    g.setColour (Pal::text.withAlpha (0.75f));
    g.drawText ("FX ROUTING", Rectangle<int> (600, by + 40, 340, 18), Justification::centredLeft);
    g.setFont (font (14.5f));
    g.setColour (Pal::text.withAlpha (0.8f));
    g.drawFittedText ("Cada capa tiene filtros, saturacion y envios propios. Los envios son relativos al retorno global "
                      "(DELAY / REVERB): 100 % = igual que en la v2. Las capas 2-8 se GENERAN desde el lead procesado; "
                      "para dobles o adlibs reales graba otra pista y usa otra instancia.",
                      Rectangle<int> (960, by + 34, getWidth() - 980, 120), Justification::topLeft, 6);
}

void RoutingPage::refresh()
{
    flow.refresh();
    for (auto* s : strips) s->tick();
}

// ============================================================================
StepLane::StepLane (UiContext& c, int l)
    : ctx (c), lane (l),
      depth (c.proc.apvts, l == 0 ? "seqThrow" : l == 1 ? "seqFilter" : l == 2 ? "seqGate" : "seqPan", "Depth", Knob::medium,
             l == 0 ? Pal::violet : l == 1 ? Pal::cyan : l == 2 ? Pal::magenta : Pal::amber)
{
    addAndMakeVisible (depth);
    auto& steps = c.proc.sequencer.steps[(size_t) l];
    auto setAll = [&steps, this] (std::function<float (int, float)> fn)
    {
        for (int s = 0; s < StepSequencer::numSteps; ++s) steps[(size_t) s].store (juce::jlimit (0.0f, 1.0f, fn (s, steps[(size_t) s].load())));
        repaint();
    };
    clear.onClick = [setAll] { setAll ([] (int, float) { return 0.0f; }); };
    full.onClick = [setAll, l] { setAll ([l] (int, float) { return l == 3 ? 0.5f : 1.0f; }); };
    randomise.onClick = [setAll, l]
    {
        auto& rnd = juce::Random::getSystemRandom();
        setAll ([&rnd, l] (int, float) { return l == 0 || l == 2 ? (rnd.nextFloat() < 0.35f ? 1.0f : 0.0f) : rnd.nextFloat(); });
    };
    invert.onClick = [setAll] { setAll ([] (int, float v) { return 1.0f - v; }); };
    for (auto* b : { &clear, &full, &randomise, &invert })
    {
        b->getProperties().set ("fontSize", 12.5f);
        addAndMakeVisible (b);
    }
}

juce::Rectangle<float> StepLane::stepArea() const { return getLocalBounds().toFloat().withTrimmedLeft (250.0f).reduced (8.0f, 12.0f); }

void StepLane::resized()
{
    depth.setBounds (10, 34, 100, getHeight() - 40);
    clear.setBounds (120, 40, 58, 24);
    full.setBounds (184, 40, 58, 24);
    randomise.setBounds (120, 72, 58, 24);
    invert.setBounds (184, 72, 58, 24);
}

void StepLane::edit (const juce::MouseEvent& e, bool first)
{
    const auto a = stepArea();
    const int s = (int) ((e.position.x - a.getX()) / (a.getWidth() / (float) StepSequencer::numSteps));
    if (! juce::isPositiveAndBelow (s, StepSequencer::numSteps)) return;
    auto& st = ctx.proc.sequencer.steps[(size_t) lane][(size_t) s];
    const bool binary = lane == 0 || lane == 2;
    if (binary)
    {
        if (first) dragValue = st.load() > 0.5f ? 0.0f : 1.0f;
        st.store (dragValue);
    }
    else st.store (juce::jlimit (0.0f, 1.0f, (a.getBottom() - e.position.y) / a.getHeight()));
    repaint();
}
void StepLane::mouseDown (const juce::MouseEvent& e) { edit (e, true); }
void StepLane::mouseDrag (const juce::MouseEvent& e) { edit (e, false); }

void StepLane::paint (juce::Graphics& g)
{
    using namespace juce;
    const auto b = getLocalBounds().toFloat();
    const auto col = lane == 0 ? Pal::violet : lane == 1 ? Pal::cyan : lane == 2 ? Pal::magenta : Pal::amber;
    drawPanel (g, b, col);
    g.setFont (font (18.0f, true));
    g.setColour (Pal::text);
    g.drawText (StepSequencer::laneNames()[lane], Rectangle<float> (14.0f, 8.0f, 200.0f, 22.0f), Justification::centredLeft);
    static const char* hint[] { "lanza la voz al delay", "filtro paso-bajo ritmico", "puerta de volumen", "panorama por paso" };
    g.setFont (font (12.5f, true));
    g.setColour (Pal::dim);
    g.drawText (hint[lane], Rectangle<float> (120.0f, 104.0f, 130.0f, 16.0f), Justification::centredLeft);

    const auto a = stepArea();
    const float sw = a.getWidth() / (float) StepSequencer::numSteps;
    const int cur = ctx.proc.sequencer.currentStep.load();
    for (int s = 0; s < StepSequencer::numSteps; ++s)
    {
        auto cell = Rectangle<float> (a.getX() + sw * (float) s + 3.0f, a.getY(), sw - 6.0f, a.getHeight());
        g.setColour ((s % 4 == 0 ? Pal::panel2.brighter (0.12f) : Pal::panel2).withAlpha (0.9f));
        g.fillRoundedRectangle (cell, 4.0f);
        const float v = ctx.proc.sequencer.steps[(size_t) lane][(size_t) s].load();
        if (lane == 3)
        {
            const float cx = cell.getX() + cell.getWidth() * v;
            g.setColour (col.withAlpha (0.85f));
            g.fillRoundedRectangle (Rectangle<float> (jmin (cell.getCentreX(), cx), cell.getCentreY() - 6.0f, std::abs (cx - cell.getCentreX()) + 2.0f, 12.0f), 3.0f);
            g.setColour (Colours::white.withAlpha (0.4f));
            g.drawVerticalLine ((int) cell.getCentreX(), cell.getY() + 4.0f, cell.getBottom() - 4.0f);
        }
        else
        {
            auto fill = cell.withTop (cell.getBottom() - cell.getHeight() * v);
            g.setGradientFill (ColourGradient (col, 0, cell.getBottom(), col.brighter (0.6f), 0, cell.getY(), false));
            g.fillRoundedRectangle (fill, 4.0f);
        }
        if (s == cur)
        {
            g.setColour (Colours::white.withAlpha (0.85f));
            g.drawRoundedRectangle (cell.expanded (1.5f), 4.0f, 1.5f);
        }
    }
}

SequencePage::SequencePage (UiContext& c) : ctx (c), on (c.proc.apvts, "seqOn", Pal::magenta), rate (c.proc.apvts, "seqRate", {}, 14.0f)
{
    addAndMakeVisible (on);
    addAndMakeVisible (rate);
    for (int l = 0; l < StepSequencer::numLanes; ++l) addAndMakeVisible (lanes.add (new StepLane (ctx, l)));
}

void SequencePage::resized()
{
    on.setBounds (176, 14, 28, 28);
    rate.setBounds (300, 13, 300, 30);
    const int y0 = 66, gap = 8;
    const int h = (getHeight() - y0 - gap * 3) / 4;
    for (int i = 0; i < lanes.size(); ++i) lanes[i]->setBounds (0, y0 + i * (h + gap), getWidth(), h);
}

void SequencePage::paint (juce::Graphics& g)
{
    using namespace juce;
    auto top = Rectangle<float> (0.0f, 0.0f, (float) getWidth(), 56.0f);
    drawPanel (g, top);
    g.setFont (font (18.0f, true));
    g.setColour (Pal::text);
    g.drawText ("SEQUENCE", Rectangle<float> (14.0f, 0.0f, 160.0f, 56.0f), Justification::centredLeft);
    g.setFont (font (13.0f, true));
    g.setColour (Pal::dim);
    g.drawText ("RATE", Rectangle<float> (250.0f, 0.0f, 46.0f, 56.0f), Justification::centredLeft);
    const int cur = ctx.proc.sequencer.currentStep.load();
    for (int s = 0; s < 16; ++s)
    {
        const float x = 630.0f + (float) s * 18.0f;
        g.setColour (s == cur ? Pal::magenta : (s % 4 == 0 ? Pal::faint.brighter (0.3f) : Pal::faint));
        g.fillEllipse (x, 22.0f, 10.0f, 10.0f);
    }
    g.setFont (font (13.5f, true));
    g.setColour (Pal::text.withAlpha (0.75f));
    g.drawFittedText ("16 pasos sincronizados al BPM del DAW (con el transporte parado corre libre).  Clic o arrastre para editar.",
                      Rectangle<int> (930, 6, getWidth() - 944, 44), Justification::centredLeft, 2);
}

void SequencePage::refresh()
{
    repaint (0, 0, getWidth(), 56);
    for (auto* l : lanes) l->repaint (l->stepArea().toNearestInt().expanded (3));
}

// ============================================================================
AnalyzePage::AnalyzePage (UiContext& c) : ctx (c)
{
    refBox.getProperties().set ("left", true);
    refBox.onChange = [this]
    {
        const int id = refBox.getSelectedId();
        ctx.setReference (id <= 1 ? juce::String() : refBox.getText());
        if (onReferenceChanged) onReferenceChanged();
        frame = 0;
        refresh();
    };
    addAndMakeVisible (refBox);
    for (auto* b : { &load, &remove, &reset, &applyAll }) { b->getProperties().set ("fontSize", 14.0f); addAndMakeVisible (b); }
    load.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Referencia (voz o acapella)", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        juce::Component::SafePointer<AnalyzePage> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
        {
            if (safe == nullptr || ! fc.getResult().existsAsFile()) return;
            safe->status = "Analizando " + fc.getResult().getFileName() + "...";
            safe->ctx.refs.analyseAsync (fc.getResult(), [safe] (analysis::Profile p)
            {
                if (safe == nullptr) return;
                safe->status = p.valid ? "Referencia lista: " + p.name + " (" + num (p.seconds, 0) + " s analizados)" : "No se pudo analizar el archivo";
                if (p.valid) safe->ctx.setReference (p.name);
                safe->rebuildList();
                if (safe->onReferenceChanged) safe->onReferenceChanged();
            });
            safe->repaint();
        });
    };
    remove.onClick = [this]
    {
        if (ctx.referenceName.isEmpty()) return;
        ctx.refs.remove (ctx.referenceName);
        ctx.setReference ({});
        rebuildList();
        if (onReferenceChanged) onReferenceChanged();
    };
    reset.onClick = [this]
    {
        ctx.live.reset (ctx.proc.getSampleRate());
        ctx.proc.resetMeters();
        status = "Analisis reiniciado";
    };
    static const char* names[] { "BODY  150 Hz", "MUD  260 Hz", "PRESENCE  3.5 kHz", "AIR  11.5 kHz" };
    for (int i = 0; i < 4; ++i)
    {
        auto* b = apply.add (new juce::TextButton ("APLICAR"));
        b->getProperties().set ("fontSize", 13.0f);
        b->setTooltip (juce::String ("Suma la sugerencia al EQ: ") + names[i]);
        b->onClick = [this, i]
        {
            if (! match.valid) return;
            const auto id = analysis::Match::eqNames()[i];
            ctx.setValue ("eqOn", 1.0f);
            ctx.setValue (id, juce::jlimit (-12.0f, 12.0f, ctx.value (id) + match.eq[(size_t) i]));
        };
        addAndMakeVisible (b);
    }
    applyAll.onClick = [this] { for (auto* b : apply) b->triggerClick(); };
    rebuildList();
}

void AnalyzePage::rebuildList()
{
    refBox.clear (juce::dontSendNotification);
    refBox.addItem ("Sin referencia (perfil del preset estimado)", 1);
    const auto names = ctx.refs.names();
    for (int i = 0; i < names.size(); ++i) refBox.addItem (names[i], 2 + i);
    const int idx = names.indexOf (ctx.referenceName);
    refBox.setSelectedId (idx >= 0 ? 2 + idx : 1, juce::dontSendNotification);
    repaint();
}

void AnalyzePage::resized()
{
    refBox.setBounds (16, 44, 388, 28);
    load.setBounds (16, 80, 190, 28);
    remove.setBounds (214, 80, 90, 28);
    reset.setBounds (312, 80, 92, 28);
    const int ey = 600;
    for (int i = 0; i < 4; ++i) apply[i]->setBounds (getWidth() - 110, ey + 46 + i * 44, 92, 28);
    applyAll.setBounds (getWidth() - 230, ey + 8, 120, 24);
}

void AnalyzePage::refresh()
{
    if (frame++ % 10 == 0)
    {
        live = ctx.live.longTerm.snapshot ("EN VIVO");
        match = analysis::compare (live, ctx.reference);
        auto recent = ctx.live.recent.snapshot();
        if (recent.valid) liveVibe = analysis::vibeFrom (recent);
        for (auto* b : apply) b->setEnabled (match.valid);
        applyAll.setEnabled (match.valid);
        repaint();
    }
}

void AnalyzePage::paint (juce::Graphics& g)
{
    using namespace juce;
    const float W = (float) getWidth();
    // ---- referencia ----
    auto refPanel = Rectangle<float> (0.0f, 0.0f, 420.0f, 220.0f);
    drawPanel (g, refPanel);
    drawTitle (g, { 14.0f, 10.0f, 300.0f, 22.0f }, "REFERENCE");
    g.setFont (font (14.0f));
    g.setColour (Pal::text.withAlpha (0.8f));
    g.drawFittedText ("Carga una voz de referencia (acapella o stem vocal; una mezcla completa sesga el resultado). "
                      "Se analiza en tu equipo y solo se guarda su perfil (.vref): espectro, sonoridad, dinamica y anchura.",
                      Rectangle<int> (16, 116, 390, 66), Justification::topLeft, 4);
    g.setFont (font (13.5f, true));
    g.setColour (ctx.refs.busy() ? Pal::amber : Pal::lilac);
    g.drawText (ctx.refs.busy() ? "Analizando..." : status, Rectangle<int> (16, 186, 390, 20), Justification::centredLeft);

    // ---- match ----
    auto mp = Rectangle<float> (428.0f, 0.0f, 300.0f, 220.0f);
    drawPanel (g, mp);
    drawTitle (g, { mp.getX() + 14.0f, 10.0f, 260.0f, 22.0f }, "MATCH");
    const auto ringC = Point<float> (mp.getX() + 80.0f, 122.0f);
    const float rr = 52.0f;
    Path track;
    track.addCentredArc (ringC.x, ringC.y, rr, rr, 0.0f, -2.5f, 2.5f, true);
    g.setColour (Pal::track);
    g.strokePath (track, PathStrokeType (8.0f, PathStrokeType::curved, PathStrokeType::rounded));
    const float total = match.valid ? match.total : (live.valid ? analysis::vibeMatch (liveVibe, ctx.targetVibe()) : -1.0f);
    if (total >= 0.0f)
    {
        Path v;
        v.addCentredArc (ringC.x, ringC.y, rr, rr, 0.0f, -2.5f, -2.5f + 5.0f * total / 100.0f, true);
        g.setGradientFill (ColourGradient (Pal::violet, ringC.x - rr, ringC.y, Pal::magenta, ringC.x + rr, ringC.y, false));
        g.strokePath (v, PathStrokeType (8.0f, PathStrokeType::curved, PathStrokeType::rounded));
    }
    g.setFont (display (26.0f));
    g.setColour (Pal::text);
    g.drawText (total >= 0.0f ? String (roundToInt (total)) + "%" : String ("--"), Rectangle<float> (ringC.x - 50.0f, ringC.y - 18.0f, 100.0f, 36.0f), Justification::centred);
    g.setFont (font (12.5f, true));
    g.setColour (Pal::dim);
    g.drawText (match.valid ? "vs referencia" : "vs perfil del preset (estimado)", Rectangle<float> (mp.getX() + 10.0f, 186.0f, 280.0f, 18.0f), Justification::centredLeft);
    auto brk = [&] (int i, const String& name, float v)
    {
        auto r = Rectangle<float> (mp.getX() + 152.0f, 64.0f + (float) i * 38.0f, 134.0f, 32.0f);
        g.setFont (font (13.0f, true));
        g.setColour (Pal::text.withAlpha (0.8f));
        g.drawText (name, r.withHeight (14.0f), Justification::centredLeft);
        g.drawText (match.valid ? String (roundToInt (v)) + "%" : String ("--"), r.withHeight (14.0f), Justification::centredRight);
        auto bar = r.withTop (r.getY() + 18.0f).withHeight (5.0f);
        g.setColour (Pal::track);
        g.fillRoundedRectangle (bar, 2.0f);
        if (match.valid) { g.setColour (Pal::violet); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * v / 100.0f), 2.0f); }
    };
    brk (0, "ESPECTRO", match.spectral);
    brk (1, "DINAMICA (PLR)", match.dynamics);
    brk (2, "ANCHURA", match.width);

    // ---- VIBE ----
    auto vp = Rectangle<float> (736.0f, 0.0f, W - 736.0f, 220.0f);
    drawPanel (g, vp);
    drawTitle (g, { vp.getX() + 14.0f, 10.0f, 300.0f, 22.0f }, "VIBE");
    VibePanel::paintRadar (g, Rectangle<float> (vp.getCentreX() - 110.0f, 44.0f, 220.0f, 156.0f), liveVibe, ctx.targetVibe(), live.valid, true);

    // ---- espectro comparado ----
    auto sp = Rectangle<float> (0.0f, 228.0f, W, 364.0f);
    drawPanel (g, sp);
    drawTitle (g, { 14.0f, sp.getY() + 10.0f, 600.0f, 22.0f }, "LONG-TERM SPECTRUM  (1/3 octava, forma normalizada)");
    g.setFont (font (13.0f, true));
    g.setColour (Pal::lilac);
    g.drawText ("EN VIVO", Rectangle<float> (W - 240.0f, sp.getY() + 10.0f, 80.0f, 20.0f), Justification::centredRight);
    g.setColour (Pal::cyan);
    g.drawText (ctx.reference.valid ? "REFERENCIA" : "(sin referencia)", Rectangle<float> (W - 150.0f, sp.getY() + 10.0f, 136.0f, 20.0f), Justification::centredRight);
    auto plot = sp.reduced (50.0f, 44.0f).withTrimmedBottom (-20.0f);
    auto yFor = [&] (float db) { return plot.getCentreY() - db / 24.0f * plot.getHeight() * 0.5f; };
    g.setFont (font (12.0f, true));
    for (float db : { -18.0f, -12.0f, -6.0f, 0.0f, 6.0f, 12.0f, 18.0f })
    {
        g.setColour (Pal::violet.withAlpha (db == 0.0f ? 0.3f : 0.08f));
        g.drawHorizontalLine ((int) yFor (db), plot.getX(), plot.getRight());
        g.setColour (Pal::dim);
        g.drawText (String ((int) db), Rectangle<float> (plot.getX() - 40.0f, yFor (db) - 7.0f, 34.0f, 14.0f), Justification::centredRight);
    }
    const auto ls = live.shape(), rs = ctx.reference.shape();
    const float bw = plot.getWidth() / (float) analysis::numBands;
    for (int k = 0; k < analysis::numBands; ++k)
    {
        const float x = plot.getX() + bw * (float) k;
        if (k % 3 == 2)
        {
            const float f = analysis::bandCentre (k);
            g.setColour (Pal::dim);
            g.drawText (f >= 1000.0f ? String (f / 1000.0f, f >= 10000.0f ? 0 : 1) + "k" : String (roundToInt (f)), Rectangle<float> (x - 6.0f, plot.getBottom() + 4.0f, bw + 12.0f, 14.0f), Justification::centred);
        }
        if (k < 3 || k > 29) continue;
        if (live.valid)
        {
            const float y = jlimit (plot.getY(), plot.getBottom(), yFor (jlimit (-24.0f, 24.0f, ls[(size_t) k])));
            g.setGradientFill (ColourGradient (Pal::violet.withAlpha (0.85f), 0, y, Pal::violet.withAlpha (0.15f), 0, plot.getCentreY() + (std::abs (y - plot.getCentreY()) < 1.0f ? 1.0f : 0.0f), false));
            g.fillRect (Rectangle<float> (x + 3.0f, jmin (y, plot.getCentreY()), bw - 6.0f, std::abs (y - plot.getCentreY()) + 1.0f));
        }
    }
    if (ctx.reference.valid)
    {
        Path p;
        bool started = false;
        for (int k = 3; k <= 29; ++k)
        {
            const float x = plot.getX() + bw * ((float) k + 0.5f);
            const float y = jlimit (plot.getY(), plot.getBottom(), yFor (jlimit (-24.0f, 24.0f, rs[(size_t) k])));
            if (! started) { p.startNewSubPath (x, y); started = true; } else p.lineTo (x, y);
        }
        strokeGlow (g, p, Pal::cyan, 1.8f, 2);
    }
    if (! live.valid)
    {
        g.setFont (font (16.0f, true));
        g.setColour (Pal::dim);
        g.drawText ("Reproduce la voz para empezar el analisis (se acumula mientras la ventana esta abierta)", plot, Justification::centred);
    }

    // ---- métricas ----
    auto mt = Rectangle<float> (0.0f, 600.0f, 600.0f, (float) getHeight() - 600.0f);
    drawPanel (g, mt);
    drawTitle (g, { 14.0f, mt.getY() + 10.0f, 300.0f, 22.0f }, "METRICS");
    const auto& rf = ctx.reference;
    struct Row { const char* name; float a, b; const char* unit; int dec; };
    const Row rows[] {
        { "LUFS INTEGRADO", live.lufsI, rf.lufsI, " LUFS", 1 }, { "LOUDNESS RANGE", live.lra, rf.lra, " LU", 1 },
        { "TRUE PEAK", live.truePeakDb, rf.truePeakDb, " dBTP", 1 }, { "PLR (pico / sonoridad)", live.plr, rf.plr, " dB", 1 },
        { "ANCHURA (lados)", live.widthPct, rf.widthPct, " %", 0 }, { "CRESTA", live.crestDb, rf.crestDb, " dB", 1 },
        { "CENTROIDE", live.centroidHz, rf.centroidHz, " Hz", 0 } };
    float y = mt.getY() + 40.0f;
    g.setFont (font (13.0f, true));
    g.setColour (Pal::dim);
    g.drawText ("EN VIVO", Rectangle<float> (250.0f, y, 110.0f, 18.0f), Justification::centredRight);
    g.drawText ("REFERENCIA", Rectangle<float> (370.0f, y, 110.0f, 18.0f), Justification::centredRight);
    g.drawText ("DIF.", Rectangle<float> (490.0f, y, 90.0f, 18.0f), Justification::centredRight);
    y += 22.0f;
    for (const auto& r : rows)
    {
        g.setFont (font (15.0f, true));
        g.setColour (Pal::text.withAlpha (0.85f));
        g.drawText (r.name, Rectangle<float> (16.0f, y, 240.0f, 22.0f), Justification::centredLeft);
        g.setColour (Pal::lilac);
        g.drawText (live.valid ? num (r.a, r.dec) + r.unit : String ("--"), Rectangle<float> (250.0f, y, 110.0f, 22.0f), Justification::centredRight);
        g.setColour (Pal::cyan);
        g.drawText (rf.valid ? num (r.b, r.dec) + r.unit : String ("--"), Rectangle<float> (370.0f, y, 110.0f, 22.0f), Justification::centredRight);
        g.setColour (Pal::text);
        g.drawText (live.valid && rf.valid ? (r.a - r.b >= 0 ? "+" : "") + num (r.a - r.b, r.dec) : String ("--"), Rectangle<float> (490.0f, y, 90.0f, 22.0f), Justification::centredRight);
        y += 24.0f;
    }
    g.setFont (font (12.5f, true));
    g.setColour (Pal::dim);
    g.drawText ("Analizado: " + num (live.seconds, 0) + " s de salida", Rectangle<float> (16.0f, mt.getBottom() - 22.0f, 400.0f, 16.0f), Justification::centredLeft);

    // ---- asistente de EQ ----
    auto ep = Rectangle<float> (608.0f, 600.0f, W - 608.0f, (float) getHeight() - 600.0f);
    drawPanel (g, ep);
    drawTitle (g, { ep.getX() + 14.0f, ep.getY() + 10.0f, 400.0f, 22.0f }, "EQ ASSISTANT");
    static const char* names[] { "BODY  150 Hz", "MUD  260 Hz", "PRESENCE  3.5 kHz", "AIR  11.5 kHz" };
    for (int i = 0; i < 4; ++i)
    {
        const float yy = ep.getY() + 46.0f + (float) i * 44.0f;
        g.setFont (font (16.0f, true));
        g.setColour (Pal::text.withAlpha (0.85f));
        g.drawText (names[i], Rectangle<float> (ep.getX() + 16.0f, yy, 200.0f, 28.0f), Justification::centredLeft);
        const float sug = match.valid ? match.eq[(size_t) i] : 0.0f;
        auto bar = Rectangle<float> (ep.getX() + 220.0f, yy + 11.0f, ep.getWidth() - 470.0f, 6.0f);
        g.setColour (Pal::track);
        g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (Colours::white.withAlpha (0.4f));
        g.drawVerticalLine ((int) bar.getCentreX(), bar.getY() - 4.0f, bar.getBottom() + 4.0f);
        if (match.valid)
        {
            const float w = bar.getWidth() * 0.5f * sug / 4.0f;
            g.setColour (sug >= 0 ? Pal::cyan : Pal::magenta);
            g.fillRoundedRectangle (Rectangle<float> (jmin (bar.getCentreX(), bar.getCentreX() + w), bar.getY(), std::abs (w) + 1.0f, bar.getHeight()), 3.0f);
        }
        g.setFont (font (16.0f, true));
        g.setColour (Pal::text);
        g.drawText (match.valid ? (sug >= 0 ? "+" : "") + String (sug, 1) + " dB" : String ("--"),
                    Rectangle<float> (ep.getRight() - 240.0f, yy, 110.0f, 28.0f), Justification::centredRight);
    }
    g.setFont (font (13.0f));
    g.setColour (Pal::dim);
    g.drawFittedText (match.valid ? "Sugerencias limitadas a +-4 dB a partir de la diferencia de forma espectral. Aplica, escucha y ajusta: es una guia, no una regla."
                                  : "Carga una referencia para obtener sugerencias de EQ.",
                      Rectangle<int> ((int) ep.getX() + 16, (int) ep.getBottom() - 46, (int) ep.getWidth() - 140, 40), Justification::topLeft, 2);
}
} // namespace pv::ui
