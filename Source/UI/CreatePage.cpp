#include "CreatePage.h"

namespace pv::ui
{
namespace
{
juce::String midiName (float midi)
{
    if (midi < 0.0f) return "--";
    const int n = juce::roundToInt (midi);
    return PitchCorrector::keyNames()[((n % 12) + 12) % 12] + juce::String (n / 12 - 1);
}

void sectionTitle (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& t)
{
    g.setFont (font (17.0f, true));
    g.setColour (Pal::text);
    drawSpaced (g, t, r, 0.8f, juce::Justification::centredLeft);
}

// slots de macro asignables (id, etiqueta)
const std::vector<std::pair<juce::String, juce::String>>& macroTargets()
{
    static const std::vector<std::pair<juce::String, juce::String>> t {
        { "mTune", "TUNE" }, { "formant", "FORMANT" }, { "mComp", "COMP" }, { "mColor", "SATURATION" },
        { "mDelay", "DELAY" }, { "mReverb", "REVERB" }, { "mWidth", "WIDTH" }, { "dryWet", "MIX" },
        { "mBody", "BODY" }, { "mAir", "AIR" }, { "mPunch", "PUNCH" }, { "mSpace", "SPACE" }, { "mEnergy", "ENERGY" },
        { "inGain", "INPUT" }, { "outGain", "MASTER" }, { "toneTilt", "TONE" }, { "tuneAmount", "TUNE AMT" },
        { "satDrive", "DRIVE" }, { "layDetune", "DETUNE" } };
    return t;
}
juce::String macroLabel (const juce::String& id)
{
    for (auto& [i, l] : macroTargets()) if (i == id) return l;
    return id.toUpperCase();
}
} // namespace

// ============================================================================
//  HERO
// ============================================================================
HeroPanel::HeroPanel (UiContext& c) : ctx (c), dnaMode (c.proc.apvts, "dnaMode", {}, 15.0f)
{
    addAndMakeVisible (dnaMode);
    addAndMakeVisible (referenceBox);
    referenceBox.setTooltip ("Referencia para MATCH y VIBE. Analiza una voz (acapella) en local; no se sube nada a internet.");
    referenceBox.getProperties().set ("left", true);
    referenceBox.onChange = [this]
    {
        const int id = referenceBox.getSelectedId();
        if (id == 1000) { loadReferenceFile(); return; }
        ctx.setReference (id <= 1 ? juce::String() : referenceBox.getText());
        match = -1.0f;
    };
    addAndMakeVisible (imageButton);
    imageButton.onClick = [this] { if (onImageMenu) onImageMenu(); };
    // los controles encima del hero se cachean: el hero se repinta 60 veces/s y ellos casi nunca cambian
    for (juce::Component* comp : std::initializer_list<juce::Component*> { &dnaMode, &referenceBox, &imageButton })
        comp->setBufferedToImage (true);
    rebuildReferenceList();
    info = ctx.currentPreset();   // el arte se genera en resized(), cuando ya hay tamaño
}

void HeroPanel::rebuildReferenceList()
{
    referenceBox.clear (juce::dontSendNotification);
    referenceBox.addItem ("Preset (estimado)", 1);
    const auto names = ctx.refs.names();
    for (int i = 0; i < names.size(); ++i) referenceBox.addItem (names[i], 2 + i);
    referenceBox.addSeparator();
    referenceBox.addItem ("Cargar referencia...", 1000);
    const int idx = names.indexOf (ctx.referenceName);
    referenceBox.setSelectedId (idx >= 0 ? 2 + idx : 1, juce::dontSendNotification);
}

void HeroPanel::loadReferenceFile()
{
    rebuildReferenceList();
    chooser = std::make_unique<juce::FileChooser> ("Referencia (voz o acapella)", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    juce::Component::SafePointer<HeroPanel> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
    {
        if (safe == nullptr || ! fc.getResult().existsAsFile()) return;
        safe->ctx.refs.analyseAsync (fc.getResult(), [safe] (analysis::Profile p)
        {
            if (safe == nullptr) return;
            if (p.valid) safe->ctx.setReference (p.name);
            safe->rebuildReferenceList();
            safe->repaint();
        });
        safe->repaint();
    });
}

void HeroPanel::presetChanged()
{
    const auto newInfo = ctx.currentPreset();
    const bool changed = newInfo.key != info.key;
    if (changed && art.isValid() && motionMode() > 0)
    {
        // transición: el arte anterior se funde y el título se "descifra"
        prevArt = art;
        prevScene = scene;
        prevTitle = info.title;
        transition = 0.0f;
    }
    info = newInfo;
    scene = art::Scene::make (info.key, info.tags, info.genre);
    // fondo a 1x: es una nebulosa difusa, no gana nada con más resolución y se dibuja 60 veces/s
    const int w = juce::jmax (100, getWidth()), h = juce::jmax (60, getHeight());
    juce::Image img (juce::Image::RGB, w, h, false);
    {
        juce::Graphics g (img);
        art::paintBackground (g, { 0, 0, (float) w, (float) h }, scene);
    }
    art = img;
    titleCache = {};
    repaint();
}

void HeroPanel::resized()
{
    dnaMode.setBounds (682, 36, 302, 30);
    imageButton.setBounds (640, 12, 26, 26);
    referenceBox.setBounds (681, 196, 122, 22);
    if (art.getWidth() != getWidth()) { transition = 1.0f; presetChanged(); }
}

void HeroPanel::refresh()
{
    if (++frame % 15 == 0)
    {
        auto live = ctx.live.longTerm.snapshot();
        if (ctx.reference.valid && live.valid)
        {
            match = analysis::compare (live, ctx.reference).total;
            matchVsReference = true;
        }
        else
        {
            auto recent = ctx.live.recent.snapshot();
            matchVsReference = false;
            match = recent.valid ? analysis::vibeMatch (analysis::vibeFrom (recent), ctx.targetVibe()) : -1.0f;
        }
    }
    if (motionMode() == 0) repaint();   // sin animación: refresco de datos a 30 Hz
}

juce::String HeroPanel::titleShown() const
{
    if (transition >= 1.0f) return info.title;
    // efecto "decode": cada letra se fija en su momento; antes, glifos aleatorios
    static const char* glyphs = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789#%&*+";
    juce::String out;
    const int n = info.title.length();
    const int tick = (int) (ctx.motion.time * 30.0);
    for (int i = 0; i < n; ++i)
    {
        const float settle = 0.25f + 0.65f * (float) i / (float) juce::jmax (1, n);
        const auto c = info.title[i];
        if (transition >= settle || c == ' ') out << juce::String::charToString (c);
        else out << juce::String::charToString ((juce::juce_wchar) glyphs[(i * 7 + tick * 3) % 41]);
    }
    return out;
}

void HeroPanel::animate()
{
    const auto& m = ctx.motion;
    if (transition < 1.0f) transition = juce::jmin (1.0f, transition + m.dt / 0.7f);
    // chispas: nacen del skyline con la energía de la voz y suben
    const bool full = m.full();
    const float W = (float) getWidth(), H = (float) getHeight();
    spawnAcc += m.dt * m.energy * (full ? 70.0f : 25.0f);
    while (spawnAcc >= 1.0f && sparks.size() < (full ? 90u : 30u))
    {
        spawnAcc -= 1.0f;
        const float x = W * (0.45f + 0.5f * rng.nextFloat());
        const auto c = rng.nextBool() ? scene.pal.a : scene.pal.glow;
        sparks.push_back ({ x, H * (0.55f + 0.1f * rng.nextFloat()), (rng.nextFloat() - 0.5f) * 14.0f, -(25.0f + 60.0f * rng.nextFloat()) * (0.6f + m.energy),
                            1.0f, 1.0f + 2.2f * rng.nextFloat(), c });
    }
    spawnAcc = juce::jmin (spawnAcc, 3.0f);
    for (auto& sp : sparks)
    {
        sp.x += sp.vx * m.dt;
        sp.y += sp.vy * m.dt;
        sp.vx += std::sin ((float) m.time * 2.0f + sp.y * 0.05f) * 6.0f * m.dt;
        sp.life -= m.dt * 0.9f;
    }
    sparks.erase (std::remove_if (sparks.begin(), sparks.end(), [] (const Spark& sp) { return sp.life <= 0.0f || sp.y < -10.0f; }), sparks.end());
    repaint();
}

void HeroPanel::paint (juce::Graphics& g)
{
    using namespace juce;
    const auto b = getLocalBounds().toFloat();
    const auto& m = ctx.motion;
    const bool animated = motionMode() > 0;
    const double t = animated ? m.time : 0.0;
    Path clip;
    clip.addRoundedRectangle (b, 7.0f);
    {
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (clip);
        auto img = ctx.userImage (info.key);
        // fondo + escena viva (con fundido desde el preset anterior)
        g.setImageResamplingQuality (Graphics::lowResamplingQuality);
        g.drawImageAt (art, 0, 0);
        art::paintLive (g, b, scene, t, animated ? m.energy : 0.0f, animated ? ctx.live.bands.data() : nullptr, m.beat, motionMode() == 2);
        if (transition < 1.0f && prevArt.isValid())
        {
            const float a = 1.0f - transition;
            g.beginTransparencyLayer (a * a);
            g.drawImageAt (prevArt, 0, 0);
            art::paintLive (g, b, prevScene, t, 0.0f, nullptr, 0.0f, false);
            g.endTransparencyLayer();
            // barrido de luz que acompaña la transición
            const float sx = b.getWidth() * transition * 1.3f - 120.0f;
            ColourGradient sweep (Colours::white.withAlpha (0.0f), sx - 80.0f, 0, Colours::white.withAlpha (0.0f), sx + 80.0f, 0, false);
            sweep.addColour (0.5, scene.pal.glow.withAlpha (0.18f * a));
            g.setGradientFill (sweep);
            g.fillRect (b);
        }
        // imagen propia del usuario (si la hay) a la derecha, fundida con el arte
        if (img.isValid())
        {
            auto area = b.withTrimmedLeft (b.getWidth() * 0.3f);
            const float zoom = animated ? 1.0f + 0.015f * (float) std::sin (t * 0.2) : 1.0f;   // "Ken Burns" muy lento
            const float scale = jmax (area.getWidth() / (float) img.getWidth(), area.getHeight() / (float) img.getHeight()) * zoom;
            const float w = (float) img.getWidth() * scale, h = (float) img.getHeight() * scale;
            g.setOpacity (0.92f);
            g.drawImage (img, Rectangle<float> (area.getCentreX() - w * 0.5f, area.getCentreY() - h * 0.5f, w, h), RectanglePlacement::stretchToFit);
            g.setOpacity (1.0f);
            g.setGradientFill (ColourGradient (Colour (0xff07061a), area.getX(), 0, Colour (0x0007061a), area.getX() + area.getWidth() * 0.45f, 0, false));
            g.fillRect (area);
            g.setGradientFill (ColourGradient (Colour (0x0007061a), 0, b.getBottom() - 70.0f, Colour (0xcc07061a), 0, b.getBottom(), false));
            g.fillRect (area);
        }
        // chispas
        for (const auto& sp : sparks)
        {
            const float a = jlimit (0.0f, 1.0f, sp.life);
            g.setColour (sp.colour.withAlpha (0.25f * a));
            g.fillEllipse (sp.x - sp.size * 1.8f, sp.y - sp.size * 1.8f, sp.size * 3.6f, sp.size * 3.6f);
            g.setColour (Colours::white.withAlpha (0.85f * a));
            g.fillEllipse (sp.x - sp.size * 0.5f, sp.y - sp.size * 0.5f, sp.size, sp.size);
        }

        // onda A/B en vivo (cinta compacta): entrada (gris) frente a salida (violeta)
        const auto& lv = ctx.live;
        float sum = 0.0f;
        for (float v : lv.outWave) sum += v;
        if (sum > 1.0e-3f)
        {
            const auto wa = Rectangle<float> (432.0f, 182.0f, 230.0f, 26.0f);
            const int n = LiveAnalysis::waveLen;
            g.setColour (Colours::black.withAlpha (0.35f));
            g.fillRoundedRectangle (wa.expanded (4.0f, 3.0f), 4.0f);
            for (int pass = 0; pass < 2; ++pass)
            {
                const auto& arr = pass == 0 ? lv.inWave : lv.outWave;
                Path p;
                for (int i = 0; i < n; i += 2)
                {
                    const float v = jlimit (0.0f, 1.0f, std::sqrt (arr[(size_t) ((lv.wavePos + i) % n)]) * 1.1f);
                    const float xx = wa.getX() + wa.getWidth() * (float) i / (float) (n - 1);
                    const float h = wa.getHeight() * 0.5f * v;
                    p.addRectangle (xx, wa.getCentreY() - h, 1.0f, jmax (0.6f, h * 2.0f));
                }
                if (pass == 0) { g.setColour (Colours::white.withAlpha (0.25f)); g.fillPath (p); }
                else
                {
                    g.setGradientFill (ColourGradient (Pal::violet.withAlpha (0.95f), wa.getX(), 0, Pal::magenta.withAlpha (0.9f), wa.getRight(), 0, false));
                    g.fillPath (p);
                }
            }
            g.setFont (font (11.0f, true));
            g.setColour (Colours::white.withAlpha (0.5f));
            g.drawText ("A IN", Rectangle<float> (wa.getX(), wa.getY() - 14.0f, 40.0f, 12.0f), Justification::centredLeft);
            g.setColour (Pal::lilac);
            g.drawText ("B OUT", Rectangle<float> (wa.getX() + 36.0f, wa.getY() - 14.0f, 50.0f, 12.0f), Justification::centredLeft);
        }
    }

    // ---- textos ----
    const float x = 26.0f;
    // título grande: brillo que respira con la voz y late con el beat.
    // Fuera de la transición el texto y su halo se cachean en imágenes (pintar el texto 7 veces por frame es caro).
    {
        auto f = display (info.title.length() > 9 ? 46.0f : 60.0f, true);
        const auto tr = Rectangle<float> (x - 2.0f, 50.0f, 600.0f, 70.0f);
        const float pulse = animated ? jlimit (0.0f, 1.0f, m.energySlow * 1.0f + 0.6f * m.beat) : 0.0f;
        auto drawTitle = [&] (Graphics& tg, const String& title, float glowAlpha, float spread, bool glowOnly)
        {
            tg.setFont (f);
            for (int i = 3; i >= 1; --i)
            {
                tg.setColour (scene.pal.a.withAlpha (glowAlpha));
                const float o = (float) i * spread;
                tg.drawText (title, tr.translated (o, o), Justification::centredLeft, false);
                tg.drawText (title, tr.translated (-o, -o), Justification::centredLeft, false);
            }
            if (glowOnly) return;
            tg.setGradientFill (ColourGradient (Colours::white, tr.getX(), tr.getY(), Pal::lilac, tr.getX() + 300.0f, tr.getBottom(), false));
            tg.drawText (title, tr, Justification::centredLeft, false);
        };
        // textos fijos del preset (artista, etiquetas, descripción): también van a la caché
        auto drawStaticText = [&] (Graphics& tg)
        {
            tg.setFont (display (17.0f, false));
            tg.setColour (Pal::text.withAlpha (0.9f));
            drawSpaced (tg, info.artist.toUpperCase(), { x, 22.0f, 520.0f, 26.0f }, 7.0f, Justification::centredLeft);
            tg.setFont (font (15.0f, true));
            tg.setColour (Pal::text.withAlpha (0.75f));
            drawSpaced (tg, "\"" + info.tags.joinIntoString (juce::String::fromUTF8 ("  \xc2\xb7  ")) + "\"", { x, 124.0f, 520.0f, 20.0f }, 1.5f, Justification::centredLeft);
            tg.setFont (font (15.5f));
            tg.setColour (Pal::text.withAlpha (0.85f));
            auto desc = info.description;
            if (info.evidence.isNotEmpty()) desc << "  Evidencia: " << info.evidence << ". Punto de partida no oficial.";
            tg.drawFittedText (desc, Rectangle<int> ((int) x, 148, 392, 66), Justification::topLeft, 3, 0.95f);
        };
        if (transition < 1.0f) { drawStaticText (g); drawTitle (g, titleShown(), 0.12f, 0.8f, false); }
        else
        {
            // la caché se genera a la escala física real: al dibujarla, el píxel cae 1:1 (sin remuestrear)
            const float k = jlimit (0.5f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
            if (std::abs (k - cacheScale) > 0.01f) { titleCache = {}; cacheScale = k; }
            if (! titleCache.isValid())
            {
                // a 2x para que el texto siga nítido con la interfaz escalada o en pantallas HiDPI
                titleCache = Image (Image::ARGB, roundToInt (550.0f * k), roundToInt (220.0f * k), true);
                Graphics tg (titleCache);
                tg.addTransform (AffineTransform::scale (k));
                drawStaticText (tg);
                drawTitle (tg, info.title, 0.10f, 0.6f, false);
                glowCache = Image (Image::ARGB, roundToInt (640.0f * k), roundToInt (130.0f * k), true);
                Graphics gg (glowCache);
                gg.addTransform (AffineTransform::scale (k));
                drawTitle (gg, info.title, 0.22f, 1.5f, true);
            }
            if (pulse > 0.02f)
            {
                g.setOpacity (pulse);
                g.drawImageTransformed (glowCache, AffineTransform::scale (1.0f / cacheScale));
                g.setOpacity (1.0f);
            }
            g.drawImageTransformed (titleCache, AffineTransform::scale (1.0f / cacheScale));
        }
        // destello que gira
        const float sx = x + jmin (560.0f, GlyphArrangement::getStringWidth (f, info.title)) + 10.0f, sy = 66.0f;
        Path star;
        star.addStar ({ 0.0f, 0.0f }, 4, 1.5f, 8.0f + 3.0f * pulse);
        star.applyTransform (AffineTransform::rotation ((float) t * 0.8f).translated (sx, sy));
        g.setColour (Colours::white.withAlpha (0.9f));
        g.fillPath (star);
    }

    // ---- DNA MODE / referencia / match ----
    g.setFont (font (16.0f, true));
    g.setColour (Pal::text);
    drawSpaced (g, "DNA MODE", { 682.0f, 10.0f, 200.0f, 22.0f }, 1.2f, Justification::centredLeft);
    g.setFont (font (12.5f, true));
    g.setColour (Pal::text.withAlpha (0.7f));
    g.drawText ("REFERENCE", Rectangle<float> (681.0f, 180.0f, 120.0f, 14.0f), Justification::centredLeft);
    g.drawText (matchVsReference ? "MATCH" : "MATCH (vs preset)", Rectangle<float> (824.0f, 180.0f, 160.0f, 14.0f), Justification::centredLeft);
    const auto bar = Rectangle<float> (824.0f, 205.0f, 120.0f, 4.0f);
    g.setColour (Pal::track);
    g.fillRoundedRectangle (bar, 2.0f);
    if (match >= 0.0f)
    {
        const float w = bar.getWidth() * jlimit (0.0f, 1.0f, match / 100.0f);
        g.setGradientFill (ColourGradient (Pal::violet, bar.getX(), 0, Pal::magenta, bar.getRight(), 0, false));
        g.fillRoundedRectangle (bar.withWidth (jmax (1.0f, w)), 2.0f);
        const float glow = animated ? 0.5f + 0.5f * (float) std::sin (t * 3.0) : 0.0f;
        g.setColour (Pal::magenta.withAlpha (0.25f * glow));
        g.fillEllipse (bar.getX() + w - 8.0f, bar.getCentreY() - 8.0f, 16.0f, 16.0f);
        g.setColour (Colours::white);
        g.fillEllipse (bar.getX() + w - 4.0f, bar.getCentreY() - 4.0f, 8.0f, 8.0f);
    }
    g.setFont (font (16.0f, true));
    g.setColour (Pal::text);
    g.drawText (ctx.refs.busy() ? String ("...") : match >= 0.0f ? String (roundToInt (match)) + "%" : String ("--"),
                Rectangle<float> (948.0f, 196.0f, 38.0f, 20.0f), Justification::centredRight);

    // marco (se ilumina con el beat)
    g.setColour (Pal::border.interpolatedWith (scene.pal.a, animated ? 0.6f * m.beat : 0.0f).withAlpha (0.9f));
    g.drawRoundedRectangle (b.reduced (0.5f), 7.0f, 1.0f + (animated ? m.beat : 0.0f));
}

// ============================================================================
//  VIBE
// ============================================================================
void VibePanel::refresh()
{
    auto recent = ctx.live.recent.snapshot();
    hasLive = recent.valid;
    if (hasLive)
    {
        const auto v = analysis::vibeFrom (recent);
        auto ease = [] (float& a, float b) { a += (b - a) * 0.15f; };
        ease (live.brightness, v.brightness); ease (live.air, v.air); ease (live.body, v.body); ease (live.width, v.width); ease (live.aggression, v.aggression);
    }
    target = ctx.targetVibe();
    repaint();
}

void VibePanel::paintRadar (juce::Graphics& g, juce::Rectangle<float> area, const analysis::Vibe& live, const analysis::Vibe& target, bool hasLive, bool big,
                            double time, float energy)
{
    using namespace juce;
    const auto c = area.getCentre();
    const float R = jmin (area.getWidth(), area.getHeight()) * 0.5f * (1.0f + 0.025f * (float) std::sin (time * 1.6) * (0.3f + energy));
    if (time > 0.0)
    {
        // barrido de radar
        const float a = (float) std::fmod (time * 1.2, MathConstants<double>::twoPi);
        for (int k = 0; k < 12; ++k)
        {
            Path wedge;
            const float a0 = a - 0.06f * (float) (k + 1);
            wedge.addPieSegment (c.x - R, c.y - R, R * 2.0f, R * 2.0f, a0, a0 + 0.06f, 0.0f);
            g.setColour (Pal::violet.withAlpha (0.10f * (1.0f - (float) k / 12.0f)));
            g.fillPath (wedge);
        }
    }
    // orden horario desde arriba: BRIGHTNESS, AIR, BODY, WIDTH, AGGRESSION
    const int order[5] { 0, 1, 2, 3, 4 };
    auto pt = [&] (int i, float v)
    {
        const float a = -MathConstants<float>::halfPi + MathConstants<float>::twoPi * (float) i / 5.0f;
        return Point<float> (c.x + std::cos (a) * R * v, c.y + std::sin (a) * R * v);
    };
    for (int ring = 1; ring <= 4; ++ring)
    {
        Path p;
        for (int i = 0; i < 5; ++i) { const auto q = pt (i, (float) ring / 4.0f); if (i == 0) p.startNewSubPath (q); else p.lineTo (q); }
        p.closeSubPath();
        g.setColour (Pal::violet.withAlpha (ring == 4 ? 0.45f : 0.16f));
        g.strokePath (p, PathStrokeType (1.0f));
    }
    for (int i = 0; i < 5; ++i)
    {
        g.setColour (Pal::violet.withAlpha (0.18f));
        g.drawLine (Line<float> (c, pt (i, 1.0f)), 1.0f);
    }
    auto poly = [&] (const analysis::Vibe& v)
    {
        Path p;
        for (int i = 0; i < 5; ++i) { const auto q = pt (i, jmax (0.06f, v[order[i]])); if (i == 0) p.startNewSubPath (q); else p.lineTo (q); }
        p.closeSubPath();
        return p;
    };
    // objetivo (preset estimado o referencia): contorno discontinuo
    {
        auto p = poly (target);
        Path dashed;
        const float dash[] { 4.0f, 3.0f };
        PathStrokeType (1.3f).createDashedStroke (dashed, p, dash, 2);
        g.setColour (Pal::cyan.withAlpha (0.8f));
        g.fillPath (dashed);
    }
    const auto v = hasLive ? live : target;
    auto p = poly (v);
    g.setGradientFill (ColourGradient (Pal::violet.withAlpha (hasLive ? 0.55f : 0.18f), c.x, c.y - R, Pal::magenta.withAlpha (hasLive ? 0.35f : 0.1f), c.x, c.y + R, false));
    g.fillPath (p);
    if (hasLive) strokeGlow (g, p, Pal::lilac, 1.4f, 2);
    for (int i = 0; i < 5 && hasLive; ++i)
    {
        const auto q = pt (i, jmax (0.06f, v[order[i]]));
        g.setColour (Colours::white);
        g.fillEllipse (q.x - 2.5f, q.y - 2.5f, 5.0f, 5.0f);
    }
    // etiquetas
    g.setFont (font (big ? 15.0f : 13.0f, true));
    g.setColour (Pal::text.withAlpha (0.85f));
    const auto& names = analysis::Vibe::names();
    for (int i = 0; i < 5; ++i)
    {
        const auto q = pt (i, 1.0f);
        Rectangle<float> lr (0, 0, 92.0f, 16.0f);
        if (i == 0) lr.setCentre (q.x, q.y - 12.0f);
        else if (i == 1) lr = lr.withPosition (q.x - 10.0f, q.y - 18.0f);
        else if (i == 4) lr = lr.withPosition (q.x - 82.0f, q.y - 18.0f);
        else if (i == 2) lr = lr.withPosition (q.x - 30.0f, q.y + 6.0f);
        else lr = lr.withPosition (q.x - 62.0f, q.y + 6.0f);
        const auto just = i == 0 || i == 2 || i == 3 ? Justification::centred : i == 1 ? Justification::centredLeft : Justification::centredRight;
        g.drawText (names[i], lr, just);
    }
}

void VibePanel::paint (juce::Graphics& g)
{
    using namespace juce;
    const auto b = getLocalBounds().toFloat();
    drawPanel (g, b);
    sectionTitle (g, { 12.0f, 8.0f, 120.0f, 20.0f }, "VIBE");
    paintRadar (g, Rectangle<float> (60.0f, 46.0f, 164.0f, 138.0f), live, target, hasLive, false,
                motionMode() > 0 ? ctx.motion.time : 0.0, ctx.motion.energy);
    g.setFont (font (12.0f, true));
    g.setColour (Pal::lilac);
    g.fillEllipse (14.0f, b.getBottom() - 15.0f, 7.0f, 7.0f);
    g.drawText ("EN VIVO", Rectangle<float> (25.0f, b.getBottom() - 20.0f, 70.0f, 16.0f), Justification::centredLeft);
    g.setColour (Pal::cyan);
    g.drawRect (Rectangle<float> (92.0f, b.getBottom() - 15.0f, 10.0f, 7.0f), 1.0f);
    g.drawText (ctx.reference.valid ? "REFERENCIA" : "PRESET (ESTIMADO)", Rectangle<float> (106.0f, b.getBottom() - 20.0f, 170.0f, 16.0f), Justification::centredLeft);
}

// ============================================================================
//  MACROS
// ============================================================================
MacroPanel::MacroPanel (UiContext& c) : ctx (c)
{
    customize.getProperties().set ("fontSize", 13.5f);
    customize.setTooltip ("Asigna que controla cada uno de los 8 macros");
    addAndMakeVisible (customize);
    customize.onClick = [this]
    {
        juce::StringArray slots;
        slots.addTokens (ctx.proc.getMacroSlots(), ",", "");
        juce::PopupMenu menu;
        menu.addSectionHeader ("ASIGNAR MACROS");
        for (int s = 0; s < 8; ++s)
        {
            juce::PopupMenu sub;
            for (int t = 0; t < (int) macroTargets().size(); ++t)
                sub.addItem (1 + s * 100 + t, macroTargets()[(size_t) t].second, true, slots[s] == macroTargets()[(size_t) t].first);
            menu.addSubMenu ("Macro " + juce::String (s + 1) + "  (" + macroLabel (slots[s]) + ")", sub);
        }
        menu.addSeparator();
        menu.addItem (9999, "Restaurar asignacion por defecto");
        juce::Component::SafePointer<MacroPanel> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&customize), [safe, slots] (int r) mutable
        {
            if (safe == nullptr || r == 0) return;
            if (r == 9999) safe->ctx.proc.setMacroSlots ("mTune,formant,mComp,mColor,mDelay,mReverb,mWidth,dryWet");
            else
            {
                const int s = (r - 1) / 100, t = (r - 1) % 100;
                while (slots.size() < 8) slots.add ("mTune");
                slots.set (s, macroTargets()[(size_t) t].first);
                safe->ctx.proc.setMacroSlots (slots.joinIntoString (","));
            }
            safe->rebind();
        });
    };
    for (int i = 0; i < 8; ++i)
    {
        auto* k = knobs.add (new Knob (ctx.proc.apvts, "mTune", "TUNE", Knob::macro));
        k->showShortValue = true;
        k->onHover = [this, i] (bool in) { hovered = in ? i : (hovered == i ? -1 : hovered); repaint (0, 0, getWidth(), 34); };
        addAndMakeVisible (k);
    }
    rebind();
}

void MacroPanel::rebind()
{
    juce::StringArray slots;
    slots.addTokens (ctx.proc.getMacroSlots(), ",", "");
    for (int i = 0; i < 8; ++i)
    {
        const auto id = i < slots.size() && ctx.proc.apvts.getParameter (slots[i]) != nullptr ? slots[i] : juce::String ("mTune");
        if (knobs[i]->getParamId() != id) knobs[i]->bind (id, macroLabel (id));
    }
    repaint();
}

void MacroPanel::resized()
{
    customize.setBounds (176, 8, 72, 20);
    const float w = (float) (getWidth() - 20) / 8.0f;
    for (int i = 0; i < 8; ++i)
        knobs[i]->setBounds (juce::Rectangle<float> (10.0f + w * (float) i, 34.0f, w, (float) getHeight() - 40.0f).toNearestInt().reduced (8, 0));
}

void MacroPanel::refresh()
{
    for (auto* k : knobs) if (k->slider.isMouseButtonDown()) { repaint (0, 0, getWidth(), 34); break; }
}

juce::String MacroPanel::describeMacro (const juce::String& id) const
{
    const float t = juce::jlimit (-1.0f, 1.0f, (ctx.value (id) - 50.0f) / 50.0f);
    auto f1 = [] (float v) { return juce::String (v, 1); };
    if (id == "mTune")   return "retune " + f1 (ctx.value ("tuneRetune") * std::exp2 (-2.0f * t)) + " ms  /  humanize " + juce::String (juce::roundToInt (ctx.value ("tuneHumanize") * (1.0f - 0.5f * t))) + " %";
    if (id == "mComp")   return "umbral " + f1 (ctx.value ("compThr") - 8.0f * t) + " dB  /  opto +" + juce::String (juce::roundToInt (15.0f * t)) + " %";
    if (id == "mColor")  return "saturacion " + juce::String (juce::roundToInt (juce::jlimit (0.0f, 100.0f, (ctx.value ("satOn") > 0.5f ? ctx.value ("satMix") : 0.0f) + 25.0f * t))) + " % (" + pv::colorModeNames()[(int) ctx.value ("satMode")] + ")";
    if (id == "mDelay")  return "delay x" + juce::String (std::exp2 (1.5f * t), 2) + "  /  feedback " + juce::String (juce::roundToInt (juce::jlimit (0.0f, 90.0f, ctx.value ("dlyFb") + 15.0f * t))) + " %";
    if (id == "mReverb") return "reverb x" + juce::String (std::exp2 (1.5f * t), 2) + "  /  decay " + juce::String (ctx.value ("revDecay") * std::exp2 (0.6f * t), 2) + " s";
    if (id == "mWidth")  return "dobles " + (t >= 0 ? juce::String ("+") : juce::String()) + f1 (6.0f * t) + " dB  /  modulacion +" + juce::String (juce::roundToInt (10.0f * juce::jmax (0.0f, t))) + " %";
    if (id == "mBody")   return "150 Hz " + f1 (ctx.value ("eqBody") + 4.0f * t) + " dB  /  260 Hz " + f1 (ctx.value ("eqMud") - 1.5f * t) + " dB";
    if (id == "mAir")    return "aire " + f1 (ctx.value ("eqAir") + 4.0f * t) + " dB  /  exciter + de-esser compensado";
    if (id == "mPunch")  return "ataque x" + juce::String (std::exp2 (1.5f * t), 2) + "  /  paralelo +" + juce::String (juce::roundToInt (30.0f * t)) + " %";
    if (id == "mSpace")  return "delay + reverb x" + juce::String (std::exp2 (1.2f * t), 2) + "  /  ducking +" + f1 (2.0f * t) + " dB";
    if (id == "mEnergy") return "presencia +" + f1 (3.0f * t) + " dB  /  paralelo y clip suave";
    return tip (id).upToFirstOccurrenceOf (".", false, false);
}

void MacroPanel::paint (juce::Graphics& g)
{
    drawPanel (g, getLocalBounds().toFloat());
    sectionTitle (g, { 14.0f, 8.0f, 160.0f, 20.0f }, "MACRO CONTROLS");
    int shown = hovered;
    for (int i = 0; i < knobs.size(); ++i) if (knobs[i]->slider.isMouseButtonDown()) shown = i;
    if (shown >= 0)
    {
        const auto id = knobs[shown]->getParamId();
        g.setFont (font (14.0f, true));
        g.setColour (Pal::lilac);
        g.drawText (macroLabel (id) + juce::String::fromUTF8 ("  \xe2\x86\x92  ") + describeMacro (id), juce::Rectangle<int> (260, 8, getWidth() - 274, 20), juce::Justification::centredRight);
    }
}

// ============================================================================
//  MASTER / OUTPUT
// ============================================================================
MasterPanel::MasterPanel (UiContext& c)
    : ctx (c), master (c.proc.apvts, "outGain", "", Knob::bare), softClip (c.proc.apvts, "softClip", "S")
{
    addAndMakeVisible (master);
    master.slider.onValueChange = [this] { repaint(); };
    softClip.getProperties().set ("fontSize", 12.5f);
    addAndMakeVisible (softClip);
    hold.setClickingTogglesState (true);
    hold.setToggleState (true, juce::dontSendNotification);
    hold.getProperties().set ("fontSize", 12.5f);
    hold.setTooltip ("H = Hold: mantiene el pico en el medidor");
    hold.onClick = [this] { peakHold = hold.getToggleState(); };
    addAndMakeVisible (hold);
    link.setClickingTogglesState (true);
    link.frame = true;
    link.onColour = Pal::lilac;
    if (auto* prm = dynamic_cast<juce::RangedAudioParameter*> (c.proc.apvts.getParameter ("ioLink")))
        linkAttachment = std::make_unique<juce::ButtonParameterAttachment> (*prm, link, c.proc.apvts.undoManager);
    addAndMakeVisible (link);
}

void MasterPanel::resized()
{
    softClip.setBounds (124, 8, 20, 18);
    hold.setBounds (148, 8, 20, 18);
    master.setBounds (14, 30, 96, 90);
    link.setBounds (112, 120, 24, 22);
}

void MasterPanel::refresh()
{
    auto upd = [this] (float v, float& lv, float& pk, int& hd)
    {
        const float n = meterNorm (v);
        lv = n > lv ? n : lv * 0.9f;
        if (n >= pk) { pk = n; hd = 40; } else if (! peakHold || --hd < 0) pk *= 0.97f;
    };
    upd (ctx.proc.outPeakL.load(), lvL, pkL, holdL);
    upd (ctx.proc.outPeakR.load(), lvR, pkR, holdR);
    repaint (138, 26, 36, 120);
}

void MasterPanel::paint (juce::Graphics& g)
{
    drawPanel (g, getLocalBounds().toFloat());
    sectionTitle (g, { 12.0f, 8.0f, 100.0f, 20.0f }, "MASTER");
    g.setFont (font (20.0f, true));
    g.setColour (Pal::text);
    g.drawText (shortValue (master.slider.getTextFromValue (master.slider.getValue())) + " dB", juce::Rectangle<int> (8, 122, 104, 24), juce::Justification::centred);
    paintBarMeter (g, { 142.0f, 30.0f, 10.0f, 112.0f }, lvL, pkL, Pal::cyan.withAlpha (0.8f), Pal::violet);
    paintBarMeter (g, { 156.0f, 30.0f, 10.0f, 112.0f }, lvR, pkR, Pal::cyan.withAlpha (0.8f), Pal::violet);
}

void OutputPanel::refresh()
{
    const float a = meterNorm (ctx.proc.outPeakL.load()), b = meterNorm (ctx.proc.outPeakR.load());
    l = a > l ? a : l * 0.9f;
    r = b > r ? b : r * 0.9f;
    repaint();
}

void OutputPanel::paint (juce::Graphics& g)
{
    using namespace juce;
    drawPanel (g, getLocalBounds().toFloat());
    sectionTitle (g, { 10.0f, 8.0f, 70.0f, 20.0f }, "OUTPUT");
    const auto area = Rectangle<float> (12.0f, 34.0f, 22.0f, 96.0f);
    paintBarMeter (g, area.withWidth (9.0f), l, 0.0f, Pal::cyan.withAlpha (0.85f), Pal::violet);
    paintBarMeter (g, area.withX (24.0f).withWidth (9.0f), r, 0.0f, Pal::cyan.withAlpha (0.85f), Pal::violet);
    g.setFont (font (11.5f, true));
    g.setColour (Pal::dim);
    for (int db : { 0, -6, -12, -24, -36 })
    {
        const float y = area.getBottom() - area.getHeight() * meterNorm (Decibels::decibelsToGain ((float) db));
        g.drawText (String (db), Rectangle<float> (38.0f, y - 6.0f, 30.0f, 12.0f), Justification::centredLeft);
    }
    const float tp = Decibels::gainToDecibels (ctx.proc.truePeakMax.load(), -99.0f);
    g.setFont (font (14.0f, true));
    g.setColour (tp > ctx.value ("ceiling") + 0.05f ? Pal::red : Pal::text);
    g.drawText (tp < -90.0f ? String ("-- dB") : String (tp, 1) + " dB", Rectangle<float> (2.0f, 132.0f, (float) getWidth() - 4.0f, 18.0f), Justification::centred);
}

// ============================================================================
//  CAPAS
// ============================================================================
LayerCard::LayerCard (UiContext& c, int i)
    : ctx (c), index (i), power (c.proc.apvts, "layOn" + juce::String (i), Pal::layer (i)),
      pan (c.proc.apvts, "layPan" + juce::String (i), "", Knob::tiny, Pal::layer (i))
{
    addAndMakeVisible (power);
    addAndMakeVisible (pan);
    levelDrag.setSliderStyle (juce::Slider::LinearBarVertical);
    levelDrag.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    levelDrag.setAlpha (0.0f);
    levelDrag.setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    levelDrag.setTooltip ("Nivel de la capa: arrastra arriba/abajo (doble clic: 0 dB)");
    levelDrag.setDoubleClickReturnValue (true, 0.0);
    levelDrag.onValueChange = [this] { repaint(); };
    addAndMakeVisible (levelDrag);
    levelAttachment = std::make_unique<APVTS::SliderAttachment> (c.proc.apvts, "layLvl" + juce::String (i), levelDrag);
    pan.slider.onValueChange = [this] { repaint(); };
}

void LayerCard::resized()
{
    power.setBounds (getWidth() - 26, 5, 21, 21);
    levelDrag.setBounds (6, 82, getWidth() - 12, 20);
    pan.setBounds (10, 104, 34, 34);
}

void LayerCard::tick (int v)
{
    view = v;
    history[(size_t) histPos] = ctx.proc.layers.getLevel (index);
    histPos = (histPos + 1) % (int) history.size();
    repaint (6, 30, getWidth() - 12, 50);
}

void LayerCard::paint (juce::Graphics& g)
{
    using namespace juce;
    const auto b = getLocalBounds().toFloat();
    const auto col = Pal::layer (index);
    const bool on = power.getToggleState();
    g.setGradientFill (ColourGradient (col.withAlpha (on ? 0.16f : 0.05f), b.getX(), b.getY(), Pal::panel.withAlpha (0.9f), b.getX(), b.getBottom(), false));
    g.fillRoundedRectangle (b, 6.0f);
    // la tarjeta "respira" con el nivel de su capa (v3.1)
    const float lvl = on && motionMode() > 0 ? jlimit (0.0f, 1.0f, meterNorm (ctx.proc.layers.getLevel (index)) * 1.3f) : 0.0f;
    g.setColour (col.withAlpha (on ? 0.6f : 0.18f));
    g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, 1.0f);
    if (lvl > 0.05f)   // halo dentro de la zona de onda (lo único que se repinta a 30 Hz)
    {
        const auto vz = Rectangle<float> (6.0f, 30.0f, b.getWidth() - 12.0f, 48.0f);
        g.setGradientFill (ColourGradient (col.withAlpha (0.22f * lvl), vz.getCentreX(), vz.getCentreY(), col.withAlpha (0.0f), vz.getRight(), vz.getCentreY(), true));
        g.fillRect (vz);
    }

    auto badge = Rectangle<float> (8.0f, 8.0f, 17.0f, 17.0f);
    g.setColour (col.withAlpha (on ? 0.95f : 0.35f));
    g.fillRoundedRectangle (badge, 3.0f);
    g.setColour (Colours::white);
    g.setFont (font (13.0f, true));
    g.drawText (String (index + 1), badge, Justification::centred);
    g.setColour (on ? Pal::text : Pal::dim);
    g.setFont (font (15.0f, true));
    g.drawText (LayerEngine::routeNames()[index], Rectangle<float> (31.0f, 6.0f, b.getWidth() - 60.0f, 20.0f), Justification::centredLeft);

    auto viz = Rectangle<float> (6.0f, 30.0f, b.getWidth() - 12.0f, 48.0f);
    const int n = (int) history.size();
    if (view == 0)   // forma de onda: historial de nivel en espejo
    {
        Path p;
        for (int i = 0; i < n; ++i)
        {
            const float v = jlimit (0.0f, 1.0f, std::sqrt (history[(size_t) ((histPos + i) % n)]) * 1.4f);
            const float x = viz.getX() + viz.getWidth() * (float) i / (float) (n - 1);
            const float h = jmax (0.6f, viz.getHeight() * 0.5f * v * (0.75f + 0.25f * std::sin ((float) i * 1.7f)));
            p.addRectangle (x - 0.8f, viz.getCentreY() - h, 1.6f, h * 2.0f);
        }
        if (! on)
        {
            // línea plana discreta
            g.setColour (col.withAlpha (0.25f));
            g.drawHorizontalLine ((int) viz.getCentreY(), viz.getX(), viz.getRight());
        }
        g.setColour (col.withAlpha (0.25f));
        g.fillPath (p, AffineTransform::scale (1.0f, 1.25f, 0.0f, viz.getCentreY()));
        g.setColour (col.brighter (0.2f));
        g.fillPath (p);
    }
    else if (view == 1)   // espectro: 4 bandas
    {
        static const char* bn[] { "LOW", "MID", "HI-M", "AIR" };
        const float bw = viz.getWidth() / 4.0f;
        for (int k = 0; k < 4; ++k)
        {
            const float e = meterNorm (ctx.proc.layers.getBand (index, k) * 2.0f, -60.0f, 0.0f);
            auto br = Rectangle<float> (viz.getX() + bw * (float) k + 3.0f, viz.getY(), bw - 6.0f, viz.getHeight() - 10.0f);
            g.setColour (Colours::black.withAlpha (0.35f));
            g.fillRoundedRectangle (br, 2.0f);
            g.setGradientFill (ColourGradient (col, 0, br.getBottom(), col.brighter (0.5f), 0, br.getY(), false));
            g.fillRoundedRectangle (br.withTop (br.getBottom() - br.getHeight() * e), 2.0f);
            g.setFont (font (10.5f, true));
            g.setColour (Pal::dim);
            g.drawText (bn[k], Rectangle<float> (br.getX() - 2.0f, viz.getBottom() - 10.0f, br.getWidth() + 4.0f, 10.0f), Justification::centred);
        }
    }
    else   // niveles: medidor horizontal grande
    {
        const float lv = meterNorm (ctx.proc.layers.getLevel (index));
        auto mr = Rectangle<float> (viz.getX(), viz.getCentreY() - 7.0f, viz.getWidth(), 14.0f);
        g.setColour (Colours::black.withAlpha (0.4f));
        g.fillRoundedRectangle (mr, 3.0f);
        g.setGradientFill (ColourGradient (col.withAlpha (0.7f), mr.getX(), 0, col.brighter (0.6f), mr.getRight(), 0, false));
        g.fillRoundedRectangle (mr.withWidth (mr.getWidth() * lv), 3.0f);
        g.setFont (font (12.0f, true));
        g.setColour (Pal::text.withAlpha (0.8f));
        const float db = Decibels::gainToDecibels (ctx.proc.layers.getLevel (index), -99.0f);
        g.drawText (db < -90.0f ? String ("-inf") : String (db, 1) + " dBFS", mr.translated (0.0f, 16.0f), Justification::centred);
    }

    g.setFont (font (16.0f, true));
    g.setColour (on ? Pal::text : Pal::dim);
    g.drawText (levelDrag.getTextFromValue (levelDrag.getValue()), Rectangle<float> (6.0f, 82.0f, b.getWidth() - 12.0f, 20.0f), Justification::centred);

    g.setFont (font (13.0f, true));
    g.setColour (Pal::text.withAlpha (0.7f));
    g.drawText ("PAN", Rectangle<float> (50.0f, 112.0f, 36.0f, 18.0f), Justification::centredLeft);
    g.setColour (on ? Pal::text : Pal::dim);
    g.drawText (pan.slider.getTextFromValue (pan.slider.getValue()), Rectangle<float> (b.getWidth() - 50.0f, 112.0f, 42.0f, 18.0f), Justification::centredRight);
}

LayersPanel::LayersPanel (UiContext& c)
    : ctx (c), harmony (c.proc.apvts, "harmInterval", "HARMONY"), octave (c.proc.apvts, "octMode", "OCTAVE"),
      detune (c.proc.apvts, "layDetune", "Detune", Knob::small, Pal::pink)
{
    for (int i = 0; i < pv::numLayers; ++i) addAndMakeVisible (cards.add (new LayerCard (ctx, i)));
    for (auto* b : { &viewWave, &viewSpec, &viewLevels })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (707);
        b->getProperties().set ("fontSize", 13.0f);
        addAndMakeVisible (b);
    }
    viewWave.setToggleState (true, juce::dontSendNotification);
    viewWave.onClick = [this] { view = 0; };
    viewSpec.onClick = [this] { view = 1; };
    viewLevels.onClick = [this] { view = 2; };
    addAndMakeVisible (harmony);
    addAndMakeVisible (octave);
    addAndMakeVisible (detune);
}

void LayersPanel::resized()
{
    viewLevels.setBounds (getWidth() - 74, 7, 64, 22);
    viewSpec.setBounds (viewLevels.getX() - 80, 7, 74, 22);
    viewWave.setBounds (viewSpec.getX() - 80, 7, 74, 22);
    const int cw = 133, gap = 7;
    for (int i = 0; i < cards.size(); ++i)
        cards[i]->setBounds (10 + i * (cw + gap), 36, cw, 146);
    const int sx = 10 + 8 * (cw + gap);
    harmony.setBounds (sx + 4, 38, getWidth() - sx - 14, 40);
    octave.setBounds (sx + 4, 82, getWidth() - sx - 14, 40);
    detune.setBounds (sx + 4, 124, getWidth() - sx - 14, 58);
}

void LayersPanel::refresh()
{
    for (auto* c : cards) c->tick (view);
}

void LayersPanel::paint (juce::Graphics& g)
{
    drawPanel (g, getLocalBounds().toFloat());
    sectionTitle (g, { 14.0f, 8.0f, 200.0f, 20.0f }, "VOCAL LAYERS");
    g.setFont (font (13.0f, true));
    g.setColour (Pal::text.withAlpha (0.7f));
    g.drawText ("LAYER VIEW", juce::Rectangle<int> (viewWave.getX() - 90, 7, 84, 22), juce::Justification::centredRight);
    g.setFont (font (12.0f, true));
    g.setColour (Pal::dim);
    g.drawText ("capas sinteticas desde el lead", juce::Rectangle<int> (150, 8, 260, 20), juce::Justification::centredLeft);
}

// ============================================================================
//  CADENA DE EFECTOS
// ============================================================================
juce::String FxCard::title (Kind k)
{
    static const char* t[] { "CLEAN", "TUNE", "FORMANT", "EQ", "COMP", "SAT", "DELAY", "REVERB", "MOD" };
    return t[(int) k];
}

juce::Colour FxCard::accent() const
{
    switch (kind)
    {
        case clean: return Pal::violet;
        case tune: return Pal::cyan;
        case formant: return Pal::teal;
        case eq: return Pal::blue;
        case comp: return Pal::orange;
        case sat: return Pal::amber;
        case delay: return Pal::violet;
        case reverb: return Pal::blue;
        case mod: return Pal::cyan;
    }
    return Pal::violet;
}

FxCard::FxCard (UiContext& c, Kind k) : kind (k), ctx (c)
{
    auto& s = c.proc.apvts;
    const auto a = accent();
    auto pw = [&] (const char* id) { power = std::make_unique<PowerButton> (s, id, a); addAndMakeVisible (*power); };
    auto ch = [&] (const char* id) { choice = std::make_unique<ParamChoice> (s, id); addAndMakeVisible (*choice); };
    auto sl = [&] (const char* id, const char* l, std::unique_ptr<MiniSlider>& dst) { dst = std::make_unique<MiniSlider> (s, id, l, a); addAndMakeVisible (*dst); };
    switch (k)
    {
        case clean:   pw ("deessOn"); sl ("hpFreq", "HPF", s1); sl ("gateThr", "GATE", s2); break;
        case tune:    pw ("tuneOn"); ch ("tuneKey"); choice2 = std::make_unique<ParamChoice> (s, "tuneScale"); addAndMakeVisible (*choice2);
                      sl ("tuneRetune", "RETUNE", s1); sl ("tuneHumanize", "HUMAN", s2); break;
        case formant: sl ("formant", "SHIFT", s1); sl ("tuneAmount", "TUNE AMT", s2); break;
        case eq:      pw ("eqOn"); sl ("eqPres", "PRESENCE", s1); sl ("eqAir", "AIR", s2); break;
        case comp:    pw ("compOn"); ch ("compModel"); sl ("compThr", "THRESH", s1); sl ("compRatio", "RATIO", s2); break;
        case sat:     pw ("satOn"); ch ("satMode"); sl ("satDrive", "DRIVE", s1); sl ("satMix", "MIX", s2); break;
        case delay:   pw ("dlyOn"); ch ("dlyTime"); sl ("dlyMix", "MIX", s1); sl ("dlyFb", "FEEDBACK", s2); break;
        case reverb:  pw ("revOn"); ch ("revType"); sl ("revMix", "MIX", s1); sl ("revDecay", "DECAY", s2); break;
        case mod:     pw ("modOn"); ch ("modMode"); sl ("modDepth", "DEPTH", s1); sl ("modMix", "MIX", s2); break;
    }
    if (power && k == clean) power->setTooltip ("De-esser on/off (el HPF y la puerta van siempre en CLEAN)");
    if (draggable()) setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    setTooltip (draggable() ? "Arrastra la tarjeta para cambiar el orden EQ / COMP / SAT" : juce::String());
}

void FxCard::resized()
{
    const int w = getWidth();
    if (power) power->setBounds (w - 24, 4, 20, 20);
    if (choice2)
    {
        choice->setBounds (6, 74, (w - 16) / 2, 22);
        choice2->setBounds (10 + (w - 16) / 2, 74, (w - 16) / 2, 22);
    }
    else if (choice) choice->setBounds (6, 74, w - 12, 22);
    if (s1) s1->setBounds (6, 98, w - 12, 22);
    if (s2) s2->setBounds (6, 119, w - 12, 22);
}

void FxCard::tick()
{
    phase += 0.06f;
    if (kind == comp) gr += (ctx.proc.compGainReduction.load() - gr) * 0.35f;
    repaint (6, 26, getWidth() - 12, 46);
}

void FxCard::mouseDown (const juce::MouseEvent& e) { dragging = false; juce::ignoreUnused (e); }
void FxCard::mouseDrag (const juce::MouseEvent& e)
{
    if (! draggable()) return;
    dragging = true;
    if (onDrag) onDrag (this, e);
}
void FxCard::mouseUp (const juce::MouseEvent& e)
{
    if (dragging && onDrop) onDrop (this, e);
    dragging = false;
}

void FxCard::paintViz (juce::Graphics& g, juce::Rectangle<float> r)
{
    using namespace juce;
    const auto a = accent();
    const bool on = power == nullptr || power->getToggleState();
    const float alpha = on ? 1.0f : 0.35f;
    g.setColour (Colours::black.withAlpha (0.3f));
    g.fillRoundedRectangle (r, 3.0f);
    Path p;
    auto line = [&] (std::function<float (float)> fn, int steps = 60)
    {
        Path q;
        for (int i = 0; i <= steps; ++i)
        {
            const float t = (float) i / (float) steps;
            const float y = r.getBottom() - r.getHeight() * jlimit (0.0f, 1.0f, fn (t));
            if (i == 0) q.startNewSubPath (r.getX() + r.getWidth() * t, y); else q.lineTo (r.getX() + r.getWidth() * t, y);
        }
        return q;
    };
    auto freqT = [] (float t) { return 20.0f * std::pow (1000.0f, t); };
    switch (kind)
    {
        case clean:
        {
            const float hp = ctx.value ("hpFreq");
            p = line ([&] (float t) { const float f = freqT (t); const float x = f / jmax (20.0f, hp); return hp <= 21.0f ? 0.6f : 0.6f * jmin (1.0f, x * x * x * x / (1.0f + x * x * x * x)) * 1.0f + 0.0f; });
            const float gt = ctx.value ("gateThr");
            if (gt > -79.5f)
            {
                g.setColour (Pal::magenta.withAlpha (0.6f * alpha));
                const float y = r.getBottom() - r.getHeight() * (gt + 80.0f) / 80.0f;
                g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
            }
            break;
        }
        case tune:
        {
            const float d = ctx.proc.detectedMidi.load(), tg = ctx.proc.targetMidi.load();
            p = line ([&] (float t) { return 0.5f + 0.32f * std::sin (t * 9.0f + phase) * (on ? std::exp (-t * 3.0f) : 1.0f); });
            g.setFont (font (16.0f, true));
            g.setColour (Pal::text.withAlpha (alpha));
            g.drawText (on ? midiName (tg >= 0.0f ? tg : d) : String ("OFF"), r.reduced (4.0f, 2.0f), Justification::topRight);
            if (on && d >= 0.0f && tg >= 0.0f)
            {
                g.setFont (font (11.5f, true));
                g.setColour (Pal::lilac);
                g.drawText (String (roundToInt ((d - tg) * 100.0f)) + " c", r.reduced (4.0f, 2.0f), Justification::bottomRight);
            }
            break;
        }
        case formant:
        {
            const float sh = ctx.value ("formant") / 12.0f;
            auto env = [] (float t, float s) { const float u = t - s * 0.15f; return 0.25f + 0.25f * std::exp (-std::pow ((u - 0.3f) * 9.0f, 2.0f)) + 0.35f * std::exp (-std::pow ((u - 0.55f) * 8.0f, 2.0f)) + 0.2f * std::exp (-std::pow ((u - 0.78f) * 10.0f, 2.0f)); };
            Path ref = line ([&] (float t) { return env (t, 0.0f); });
            g.setColour (Colours::white.withAlpha (0.2f));
            g.strokePath (ref, PathStrokeType (1.0f));
            p = line ([&] (float t) { return env (t, sh); });
            break;
        }
        case eq:
        {
            const float body = ctx.value ("eqBody"), mud = ctx.value ("eqMud"), pres = ctx.value ("eqPres"), air = ctx.value ("eqAir");
            p = line ([&] (float t)
            {
                const float f = freqT (t);
                auto bell = [&] (float fc, float gain, float w) { const float o = std::log2 (f / fc) / w; return gain * std::exp (-o * o); };
                const float lo = body / (1.0f + std::pow (f / 150.0f, 2.0f)), hi = air / (1.0f + std::pow (11500.0f / f, 2.0f));
                return 0.5f + (lo + bell (260.0f, mud, 0.8f) + bell (3500.0f, pres, 1.0f) + hi) / 24.0f;
            });
            break;
        }
        case comp:
        {
            // transferencia + barra de GR
            const float thr = ctx.value ("compThr"), ratio = ctx.value ("compRatio");
            p = line ([&] (float t) { const float in = -60.0f + 60.0f * t; const float out = in > thr ? thr + (in - thr) / ratio : in; return (out + 60.0f) / 60.0f; });
            const float grN = jlimit (0.0f, 1.0f, gr / 20.0f);
            auto bar = Rectangle<float> (r.getRight() - 8.0f, r.getY() + 3.0f, 4.0f, r.getHeight() - 6.0f);
            g.setColour (Colours::black.withAlpha (0.4f));
            g.fillRect (bar);
            g.setColour (Pal::orange);
            g.fillRect (bar.withHeight (bar.getHeight() * grN));
            g.setFont (font (11.5f, true));
            g.setColour (Pal::text.withAlpha (0.8f * alpha));
            g.drawText ("GR " + String (gr, 1), r.reduced (4.0f, 2.0f).withTrimmedRight (8.0f), Justification::bottomRight);
            break;
        }
        case sat:
        {
            const float drv = ctx.value ("satDrive") / 100.0f;
            p = line ([&] (float t) { const float x = (t - 0.5f) * 2.0f; const float k = 1.0f + 6.0f * drv; return 0.5f + 0.45f * std::tanh (k * x) / std::tanh (k); });
            break;
        }
        case delay:
        {
            const float fb = ctx.value ("dlyFb") / 100.0f;
            for (int i = 0; i < 6; ++i)
            {
                const float x = r.getX() + 8.0f + (r.getWidth() - 16.0f) * (float) i / 5.0f;
                const float h = (r.getHeight() - 8.0f) * (i == 0 ? 1.0f : std::pow (fb, (float) i) * 0.9f + 0.05f);
                g.setColour ((i % 2 == 0 ? a : Pal::magenta).withAlpha (0.85f * alpha));
                g.fillRoundedRectangle (x - 2.0f, r.getBottom() - 4.0f - h, 4.0f, h, 1.5f);
            }
            return;
        }
        case reverb:
        {
            const float dec = ctx.value ("revDecay");
            for (int i = 0; i < 40; ++i)
            {
                const float t = (float) i / 39.0f;
                const float h = (r.getHeight() - 6.0f) * std::exp (-t * 4.0f / jmax (0.3f, dec)) * (0.55f + 0.45f * std::abs (std::sin ((float) i * 2.3f + phase)));
                g.setColour (a.withAlpha (0.8f * alpha));
                g.fillRect (r.getX() + 4.0f + (r.getWidth() - 8.0f) * t, r.getBottom() - 3.0f - h, 1.6f, h);
            }
            return;
        }
        case mod:
        {
            const float rate = ctx.value ("modRate");
            p = line ([&] (float t) { return 0.5f + 0.35f * std::sin (t * 6.2832f * jlimit (1.0f, 4.0f, rate * 2.0f) + phase * rate * 3.0f); });
            break;
        }
    }
    strokeGlow (g, p, a.withAlpha (alpha), 1.5f, 2);
}

void FxCard::paint (juce::Graphics& g)
{
    using namespace juce;
    const auto b = getLocalBounds().toFloat();
    const auto a = accent();
    const bool on = power == nullptr || power->getToggleState();
    g.setGradientFill (ColourGradient (a.withAlpha (on ? 0.10f : 0.03f), 0, 0, Pal::panel.withAlpha (0.95f), 0, b.getBottom(), false));
    g.fillRoundedRectangle (b, 5.0f);
    g.setColour ((dragging ? Pal::lilac : a.withAlpha (on ? 0.5f + 0.4f * pulse : 0.18f)));
    g.drawRoundedRectangle (b.reduced (0.5f), 5.0f, dragging ? 1.6f : 1.0f);
    if (on && pulse > 0.05f && motionMode() > 0)   // el módulo activo se ilumina con la voz
    {
        g.setColour (a.withAlpha (0.12f * pulse));
        g.drawRoundedRectangle (b.reduced (1.5f), 5.0f, 3.0f);
    }
    // cabecera: icono + título
    g.setColour (a.withAlpha (on ? 0.9f : 0.35f));
    g.drawEllipse (7.0f, 7.0f, 13.0f, 13.0f, 1.3f);
    g.fillEllipse (11.5f, 11.5f, 4.0f, 4.0f);
    g.setFont (font (15.0f, true));
    g.setColour (on ? Pal::text : Pal::dim);
    g.drawText (title (kind), Rectangle<float> (25.0f, 4.0f, b.getWidth() - 50.0f, 20.0f), Justification::centredLeft);
    if (draggable())
    {
        g.setColour (Pal::dim.withAlpha (0.6f));
        for (int i = 0; i < 3; ++i) g.fillEllipse (b.getWidth() - 34.0f, 9.0f + (float) i * 4.0f, 2.0f, 2.0f);
    }
    paintViz (g, Rectangle<float> (6.0f, 26.0f, b.getWidth() - 12.0f, choice != nullptr ? 44.0f : 68.0f));
}

ChainPanel::ChainPanel (UiContext& c)
    : ctx (c), inTrim (c.proc.apvts, "inGain", "", Knob::bare, Pal::violet), ceiling (c.proc.apvts, "ceiling", "", Knob::bare, Pal::lilac)
{
    for (int k = 0; k < 9; ++k)
    {
        auto* card = cards.add (new FxCard (ctx, (FxCard::Kind) k));
        card->onDrag = [this] (FxCard* fc, const juce::MouseEvent& e) { cardDragged (fc, e); };
        card->onDrop = [this] (FxCard* fc, const juce::MouseEvent& e) { cardDropped (fc, e); };
        addAndMakeVisible (card);
    }
    addAndMakeVisible (inTrim);
    addAndMakeVisible (ceiling);
    inTrim.slider.onValueChange = [this] { repaint (inputCard); };
    ceiling.slider.onValueChange = [this] { repaint (outputCard); };
    for (auto* b : { &autoButton, &monitorButton })
    {
        b->getProperties().set ("fontSize", 11.5f);
        addAndMakeVisible (b);
    }
    autoButton.setTooltip ("AUTO: canta 3 s y pulsa. Ajusta la entrada al nivel para el que estan calibrados los presets (~-15 LUFS)");
    autoButton.onClick = [this]
    {
        // v3.2: -15 LUFS a corto plazo = voz a ~-18 dBFS RMS (pista mono en canal estéreo), el punto de calibración
        const float st = ctx.proc.inLoudness.shortTerm.load();
        if (st < -70.0f) return;
        ctx.setValue ("inGain", juce::jlimit (-24.0f, 24.0f, ctx.value ("inGain") + (-15.0f - st)));
    };
    monitorButton.setTooltip ("MONITOR: latencia minima (~7 ms) para grabar. Desactivalo (MIX, ~50 ms) solo para mezclar");
    monitorButton.setClickingTogglesState (true);
    monitorButton.onClick = [this] { ctx.setValue ("quality", monitorButton.getToggleState() ? 0.0f : 1.0f); };
}

void ChainPanel::resized()
{
    inputCard = { 10, 34, 104, 154 };
    outputCard = { getWidth() - 96, 34, 86, 154 };
    inTrim.setBounds (inputCard.getX() + 14, inputCard.getY() + 24, 58, 58);
    autoButton.setBounds (inputCard.getX() + 4, inputCard.getBottom() - 26, 46, 22);
    monitorButton.setBounds (inputCard.getX() + 52, inputCard.getBottom() - 26, 48, 22);
    ceiling.setBounds (outputCard.getX() + 14, outputCard.getY() + 24, 58, 58);
    const int x0 = inputCard.getRight() + 8, x1 = outputCard.getX() - 8, gap = 6;
    const int w = (x1 - x0 - gap * 8) / 9;
    for (int i = 0; i < 9; ++i) slots[(size_t) i] = { x0 + i * (w + gap), 34, w, 142 };
    layoutCards();
}

void ChainPanel::layoutCards (bool)
{
    // CLEAN, TUNE, FORMANT fijos; los 3 insertos según chainOrder; luego DELAY, REVERB, MOD
    const auto order = VoxDnaProcessor::chainOrderFor ((int) ctx.value ("chainOrder"));
    static const FxCard::Kind stageKind[3] { FxCard::eq, FxCard::comp, FxCard::sat };
    std::array<FxCard::Kind, 9> seq { FxCard::clean, FxCard::tune, FxCard::formant,
                                      stageKind[order[0]], stageKind[order[1]], stageKind[order[2]],
                                      FxCard::delay, FxCard::reverb, FxCard::mod };
    for (int i = 0; i < 9; ++i)
        cards[(int) seq[(size_t) i]]->setBounds (slots[(size_t) i]);
    lastOrder = (int) ctx.value ("chainOrder");
    repaint();
}

void ChainPanel::cardDragged (FxCard* c, const juce::MouseEvent& e)
{
    auto pos = e.getEventRelativeTo (this).getPosition();
    const int minX = slots[3].getX() - 30, maxX = slots[5].getX() + 30;
    c->setTopLeftPosition (juce::jlimit (minX, maxX, pos.x - c->getWidth() / 2), slots[3].getY() - 4);
    c->toFront (false);
}

void ChainPanel::cardDropped (FxCard*, const juce::MouseEvent&)
{
    std::array<std::pair<int, int>, 3> xs { std::make_pair (cards[FxCard::eq]->getX(), 0), std::make_pair (cards[FxCard::comp]->getX(), 1),
                                            std::make_pair (cards[FxCard::sat]->getX(), 2) };
    std::sort (xs.begin(), xs.end());
    const int idx = VoxDnaProcessor::chainOrderIndex ({ xs[0].second, xs[1].second, xs[2].second });
    ctx.setValue ("chainOrder", (float) idx);
    layoutCards();
}

void ChainPanel::refresh()
{
    if ((int) ctx.value ("chainOrder") != lastOrder) layoutCards();
    for (auto* c : cards) c->tick();
    inLevel = juce::jmax (meterNorm (ctx.proc.inputLevel.load()), inLevel * 0.9f);
    limGr += (ctx.proc.limiterGainReduction.load() - limGr) * 0.3f;
    monitorButton.setToggleState (ctx.proc.getQuality() == 0, juce::dontSendNotification);
    repaint (inputCard);
    repaint (outputCard);
}

void ChainPanel::animate()
{
    const auto& m = ctx.motion;
    const float x0 = (float) inputCard.getRight(), x1 = (float) outputCard.getX();
    // partículas de señal: más y más rápidas cuanto más nivel entra
    const float level = juce::jlimit (0.0f, 1.0f, meterNorm (ctx.proc.inputLevel.load()) * 1.2f);
    flowAcc += m.dt * (2.0f + 40.0f * level) * (m.full() ? 1.0f : 0.4f);
    while (flowAcc >= 1.0f && flow.size() < 80)
    {
        flowAcc -= 1.0f;
        flow.push_back ({ x0, 140.0f + 260.0f * level + 60.0f * (float) (flow.size() % 3), 2.0f + 2.5f * level, Pal::lilac });
    }
    for (auto& f : flow)
    {
        f.x += f.speed * m.dt;
        // toma el color del módulo que atraviesa
        for (auto* c : cards)
            if (f.x >= (float) c->getX() && f.x <= (float) c->getRight()) { f.colour = c->accent(); break; }
    }
    flow.erase (std::remove_if (flow.begin(), flow.end(), [x1] (const Flow& f) { return f.x > x1; }), flow.end());
    repaint (juce::Rectangle<int> (inputCard.getRight(), 177, outputCard.getX() - inputCard.getRight(), 12));
    for (auto* c : cards) c->pulse = juce::jlimit (0.0f, 1.0f, level);
}

void ChainPanel::paint (juce::Graphics& g)
{
    using namespace juce;
    drawPanel (g, getLocalBounds().toFloat());
    sectionTitle (g, { 14.0f, 8.0f, 200.0f, 20.0f }, "EFFECTS CHAIN");
    g.setFont (font (12.0f, true));
    g.setColour (Pal::dim);
    g.drawText ("arrastra EQ / COMP / SAT para cambiar el orden", Rectangle<int> (160, 8, 360, 20), Justification::centredLeft);

    // INPUT
    auto ic = inputCard.toFloat();
    g.setColour (Pal::panel2.withAlpha (0.9f));
    g.fillRoundedRectangle (ic, 5.0f);
    g.setColour (Pal::border);
    g.drawRoundedRectangle (ic.reduced (0.5f), 5.0f, 1.0f);
    g.setFont (font (15.0f, true));
    g.setColour (Pal::text);
    g.drawText ("INPUT", ic.withHeight (22.0f).reduced (8.0f, 0.0f), Justification::centredLeft);
    paintBarMeter (g, { ic.getRight() - 16.0f, ic.getY() + 26.0f, 7.0f, 60.0f }, inLevel, 0.0f, Pal::cyan, Pal::violet);
    g.setFont (font (15.0f, true));
    g.drawText (shortValue (inTrim.slider.getTextFromValue (inTrim.slider.getValue())) + " dB", Rectangle<float> (ic.getX(), ic.getY() + 86.0f, ic.getWidth() - 14.0f, 18.0f), Justification::centred);

    // OUTPUT / LIMIT
    auto oc = outputCard.toFloat();
    g.setColour (Pal::panel2.withAlpha (0.9f));
    g.fillRoundedRectangle (oc, 5.0f);
    g.setColour (Pal::border);
    g.drawRoundedRectangle (oc.reduced (0.5f), 5.0f, 1.0f);
    g.setFont (font (15.0f, true));
    g.setColour (Pal::text);
    g.drawText ("OUTPUT", oc.withHeight (22.0f).reduced (8.0f, 0.0f), Justification::centredLeft);
    g.drawText (shortValue (ceiling.slider.getTextFromValue (ceiling.slider.getValue())) + " dB", Rectangle<float> (oc.getX(), oc.getY() + 86.0f, oc.getWidth(), 18.0f), Justification::centred);
    g.setFont (font (13.0f, true));
    g.setColour (Pal::text.withAlpha (0.75f));
    g.drawText ("LIMIT", Rectangle<float> (oc.getX(), oc.getBottom() - 42.0f, oc.getWidth(), 16.0f), Justification::centred);
    auto grBar = Rectangle<float> (oc.getX() + 12.0f, oc.getBottom() - 22.0f, oc.getWidth() - 24.0f, 5.0f);
    g.setColour (Colours::black.withAlpha (0.4f));
    g.fillRoundedRectangle (grBar, 2.0f);
    g.setColour (Pal::red.withAlpha (0.85f));
    g.fillRoundedRectangle (grBar.withWidth (grBar.getWidth() * jlimit (0.0f, 1.0f, limGr / 12.0f)), 2.0f);

    // flujo: línea con flechas y puntos de color
    const float y = 182.0f;
    g.setColour (Pal::violet.withAlpha (0.5f));
    g.drawHorizontalLine ((int) y, ic.getRight(), oc.getX());
    std::array<FxCard*, 9> sorted {};
    for (int i = 0; i < 9; ++i) sorted[(size_t) i] = cards[i];
    std::sort (sorted.begin(), sorted.end(), [] (FxCard* a, FxCard* b) { return a->getX() < b->getX(); });
    float prevX = ic.getRight();
    for (auto* c : sorted)
    {
        const float cx = (float) c->getBounds().getCentreX();
        g.setColour (c->accent().withAlpha (0.9f));
        g.drawArrow (Line<float> (prevX + 4.0f, y, cx - 6.0f, y), 1.2f, 6.0f, 6.0f);
        g.fillEllipse (cx - 3.0f, y - 3.0f, 6.0f, 6.0f);
        prevX = cx;
    }
    g.setColour (Pal::lilac.withAlpha (0.9f));
    g.drawArrow (Line<float> (prevX + 4.0f, y, oc.getX() - 2.0f, y), 1.2f, 6.0f, 6.0f);

    // partículas de señal recorriendo la cadena (v3.1)
    if (motionMode() > 0)
        for (const auto& f : flow)
        {
            g.setColour (f.colour.withAlpha (0.25f));
            g.fillEllipse (f.x - f.size * 1.4f, y - f.size * 1.4f, f.size * 2.8f, f.size * 2.8f);
            g.setColour (Colours::white.withAlpha (0.9f));
            g.fillEllipse (f.x - f.size * 0.6f, y - f.size * 0.6f, f.size * 1.2f, f.size * 1.2f);
        }
}

// ============================================================================
//  ROUTING / GLOBAL / VISUALIZER
// ============================================================================
RoutingModePanel::RoutingModePanel (UiContext& c) : modes (c.proc.apvts, "routeMode", {}, 14.0f)
{
    addAndMakeVisible (modes);
}
void RoutingModePanel::resized() { modes.setBounds (12, 36, getWidth() - 24, 36); }
void RoutingModePanel::paint (juce::Graphics& g)
{
    drawPanel (g, getLocalBounds().toFloat());
    sectionTitle (g, { 14.0f, 8.0f, 200.0f, 20.0f }, "ROUTING");
}

GlobalPanel::GlobalPanel (UiContext& c)
    : ctx (c), trim (c.proc.apvts, "inGain", "Input Trim", Knob::small), dryWet (c.proc.apvts, "dryWet", "Dry/Wet", Knob::small),
      tone (c.proc.apvts, "toneTilt", "Tone", Knob::small)
{
    addAndMakeVisible (trim);
    addAndMakeVisible (dryWet);
    addAndMakeVisible (tone);
}

void GlobalPanel::resized()
{
    trim.setBounds (110, 6, 80, 78);
    dryWet.setBounds (196, 6, 80, 78);
    tone.setBounds (282, 6, 80, 78);
    latencyArea = { 366, 6, 76, 78 };
}

void GlobalPanel::paint (juce::Graphics& g)
{
    using namespace juce;
    drawPanel (g, getLocalBounds().toFloat());
    sectionTitle (g, { 14.0f, 8.0f, 100.0f, 20.0f }, "GLOBAL");
    const int q = ctx.proc.getQuality();
    g.setFont (font (12.5f, true));
    g.setColour (Pal::dim);
    g.drawText ("QUALITY", Rectangle<int> (14, 36, 90, 16), Justification::centredLeft);
    g.setFont (font (16.0f, true));
    g.setColour (Pal::lilac);
    g.drawText (q == 0 ? "MONITOR" : q == 1 ? "MIX" : "RENDER HQ", Rectangle<int> (14, 52, 96, 20), Justification::centredLeft);

    // latencia real reportada al host
    const int lat = ctx.proc.reportedLatency.load();
    const double sr = ctx.proc.getSampleRate() > 0 ? ctx.proc.getSampleRate() : 48000.0;
    const float ms = (float) (lat * 1000.0 / sr);
    auto dial = Rectangle<float> ((float) latencyArea.getCentreX() - 17.0f, 10.0f, 34.0f, 34.0f);
    g.setColour (Pal::track);
    Path arc;
    arc.addCentredArc (dial.getCentreX(), dial.getCentreY(), 15.0f, 15.0f, 0.0f, -2.4f, 2.4f, true);
    g.strokePath (arc, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));
    Path val;
    val.addCentredArc (dial.getCentreX(), dial.getCentreY(), 15.0f, 15.0f, 0.0f, -2.4f, -2.4f + 4.8f * jlimit (0.0f, 1.0f, ms / 60.0f), true);
    g.setColour (ms < 12.0f ? Pal::lime : ms < 25.0f ? Pal::amber : Pal::red);
    g.strokePath (val, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setFont (font (12.5f, true));
    g.setColour (Pal::text.withAlpha (0.8f));
    g.drawText ("LATENCY", Rectangle<float> ((float) latencyArea.getX(), 49.0f, (float) latencyArea.getWidth(), 14.0f), Justification::centred);
    g.setColour (ms < 12.0f ? Pal::lilac : ms < 25.0f ? Pal::amber : Pal::red);
    g.drawText (String (ms, 1) + " ms", Rectangle<float> ((float) latencyArea.getX(), 63.0f, (float) latencyArea.getWidth(), 16.0f), Justification::centred);
}

VisualizerPanel::VisualizerPanel (UiContext& c, bool isCompact) : ctx (c), compact (isCompact)
{
    mode.addItemList ({ "SPECTRUM", "WAVE" }, 1);
    mode.setSelectedId (1, juce::dontSendNotification);
    channels.addItemList ({ "L+R", "L", "R", "MID", "SIDE" }, 1);
    channels.setSelectedId (1, juce::dontSendNotification);
    for (auto* b : { &mode, &channels }) addAndMakeVisible (b);
    spectrum.fill (-100.0f);
}

void VisualizerPanel::resized()
{
    channels.setBounds (getWidth() - 82, 7, 72, 22);
    mode.setBounds (channels.getX() - 108, 7, 100, 22);
}

void VisualizerPanel::refresh()
{
    if (mode.getSelectedId() == 1)
    {
        const int N = 4096;
        const int ch = channels.getSelectedId() - 1;
        for (int i = 0; i < N; ++i)
        {
            const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N);
            fftData[(size_t) i] = ctx.live.sample (ch == 0 ? 3 : ch, LiveAnalysis::ringSize - N + i) * w;
        }
        std::fill (fftData.begin() + N, fftData.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (fftData.data());
        for (int i = 1; i < N / 2; ++i)
        {
            const float db = juce::Decibels::gainToDecibels (fftData[(size_t) i] / (float) (N / 4), -120.0f);
            auto& s = spectrum[(size_t) i];
            s = db > s ? s + (db - s) * 0.6f : s + (db - s) * 0.15f;
        }
    }
    repaint();
}

void VisualizerPanel::paint (juce::Graphics& g)
{
    using namespace juce;
    const auto b = getLocalBounds().toFloat();
    drawPanel (g, b);
    sectionTitle (g, { 14.0f, 8.0f, 200.0f, 20.0f }, "VISUALIZER");
    auto plot = Rectangle<float> (12.0f, 32.0f, b.getWidth() - 24.0f, b.getHeight() - 50.0f);
    const double sr = ctx.proc.getSampleRate() > 0 ? ctx.proc.getSampleRate() : 48000.0;
    if (mode.getSelectedId() == 1)
    {
        const int bars = compact ? 150 : 220;
        for (int k = 0; k < bars; ++k)
        {
            const float f0 = 20.0f * std::pow (1000.0f, (float) k / (float) bars), f1 = 20.0f * std::pow (1000.0f, (float) (k + 1) / (float) bars);
            const int i0 = jlimit (1, 2047, (int) (f0 * 4096.0f / (float) sr)), i1 = jlimit (i0, 2047, (int) (f1 * 4096.0f / (float) sr));
            float m = -120.0f;
            for (int i = i0; i <= i1; ++i) m = jmax (m, spectrum[(size_t) i]);
            m += 4.5f * std::log2 (jmax (f0, 20.0f) / 1000.0f);   // pendiente para lectura musical
            const float h = jmax (1.0f, plot.getHeight() * jlimit (0.0f, 1.0f, (m + 84.0f) / 84.0f));
            const float x = plot.getX() + plot.getWidth() * (float) k / (float) bars;
            const float bwid = jmax (1.0f, plot.getWidth() / (float) bars - 1.0f);
            g.setGradientFill (ColourGradient (Pal::violet.withAlpha (0.9f), 0, plot.getBottom(), Pal::magenta, 0, plot.getBottom() - h, false));
            g.fillRect (x, plot.getBottom() - h, bwid, h);
            if (motionMode() > 0 && k < (int) caps.size())
            {
                // picos que caen por gravedad
                auto& cp = caps[(size_t) k];
                auto& cv = capVel[(size_t) k];
                if (h >= cp) { cp = h; cv = 0.0f; }
                else { cv += 0.35f; cp = jmax (0.0f, cp - cv); }
                g.setColour (Colours::white.withAlpha (0.75f));
                g.fillRect (x, plot.getBottom() - cp - 2.0f, bwid, 1.5f);
            }
        }
    }
    else
    {
        const int ch = channels.getSelectedId() - 1;
        Path p;
        const int N = 2048;
        for (int i = 0; i < N; i += 4)
        {
            const float v = ctx.live.sample (ch == 0 ? 3 : ch, LiveAnalysis::ringSize - N + i);
            const float x = plot.getX() + plot.getWidth() * (float) i / (float) N, y = plot.getCentreY() - v * plot.getHeight() * 0.5f;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        strokeGlow (g, p, Pal::violet, 1.2f, 2);
    }
    g.setFont (font (12.0f, true));
    g.setColour (Pal::dim);
    g.drawText ("20 Hz", Rectangle<float> (plot.getX(), plot.getBottom(), 60.0f, 16.0f), Justification::centredLeft);
    g.drawText ("20 kHz", Rectangle<float> (plot.getRight() - 60.0f, plot.getBottom(), 60.0f, 16.0f), Justification::centredRight);
}

// ============================================================================
CreatePage::CreatePage (UiContext& c)
    : hero (c), vibe (c), macros (c), master (c), output (c), layers (c), chain (c), routing (c), global (c), visualizer (c)
{
    for (juce::Component* comp : std::initializer_list<juce::Component*> { &hero, &vibe, &macros, &master, &output, &layers, &chain, &routing, &global, &visualizer })
        addAndMakeVisible (comp);
    hero.onImageMenu = [this] { if (onImageMenu) onImageMenu(); };
}

void CreatePage::resized()
{
    hero.setBounds (0, 0, 992, 224);
    vibe.setBounds (998, 0, 284, 224);
    macros.setBounds (0, 230, 1016, 154);
    master.setBounds (1024, 230, 176, 154);
    output.setBounds (1208, 230, 74, 154);
    layers.setBounds (0, 392, 1282, 190);
    chain.setBounds (0, 590, 1282, 198);
    routing.setBounds (0, 794, 317, 86);
    global.setBounds (323, 794, 446, 86);
    visualizer.setBounds (775, 794, 507, 86);
}

void CreatePage::animate()
{
    hero.animate();
    if ((++animFrame & 1) == 0) vibe.animate();   // el radar va a 30 fps
    chain.animate();
}

void CreatePage::refresh()
{
    for (Refreshable* r : std::initializer_list<Refreshable*> { &hero, &vibe, &macros, &master, &output, &layers, &chain, &global, &visualizer })
        r->refresh();
}
} // namespace pv::ui
