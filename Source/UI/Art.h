#pragma once
#include "../LookAndFeel.h"

// ============================================================================
//  Arte generativo por preset (sin fotos ni parecidos de personas reales):
//  nebulosa, campo de estrellas, planeta con anillo, hélice de ADN y un
//  "skyline" de onda. Todo determinista a partir del nombre del preset, con
//  la paleta elegida por sus etiquetas (DARK, AIRY, AGGRESSIVE...).
// ============================================================================
namespace pv::ui::art
{
struct Palette { juce::Colour a, b, c, glow; };

inline Palette paletteFor (const juce::StringArray& tags, const juce::String& genre)
{
    auto has = [&] (const char* t) { return tags.contains (t); };
    if (has ("AGGRESSIVE") || has ("DISTORTED") || has ("HARD") || has ("LOUD"))
        return { juce::Colour (0xffff2e63), juce::Colour (0xffff7a1a), juce::Colour (0xff6a0dad), juce::Colour (0xffff5a7a) };
    if (has ("AIRY") || has ("BRIGHT") || has ("HYPER") || genre == "POP")
        return { juce::Colour (0xff38d6ff), juce::Colour (0xffc58bff), juce::Colour (0xff3b2bff), juce::Colour (0xff8fe9ff) };
    if (has ("WARM") || has ("TAPE") || has ("SMOOTH") || genre == "R&B")
        return { juce::Colour (0xffff8a3d), juce::Colour (0xffff4fa3), juce::Colour (0xff6b1d8f), juce::Colour (0xffffb36b) };
    if (has ("CLEAN") || has ("DRY") || has ("CLEAR") || has ("NATURAL"))
        return { juce::Colour (0xff2fd3e0), juce::Colour (0xff4c6bff), juce::Colour (0xff1b1450), juce::Colour (0xff7ff0ff) };
    return { juce::Colour (0xff9b5cff), juce::Colour (0xffff3df2), juce::Colour (0xff2a1470), juce::Colour (0xffc9a7ff) };
}

inline juce::int64 seedFor (const juce::String& key) { return (juce::int64) (key.hashCode64() & 0x7fffffffffffLL); }

// pinta el arte en un rectángulo (fondo del hero); 'detail' 1 = completo, 0 = avatar
inline void paint (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& key, const juce::StringArray& tags,
                   const juce::String& genre, bool avatar = false)
{
    using namespace juce;
    Random rnd (seedFor (key));
    const auto pal = paletteFor (tags, genre);
    const float W = r.getWidth(), H = r.getHeight();

    g.setGradientFill (ColourGradient (Colour (0xff07061a), r.getX(), r.getY(), Colour (0xff0d0a26), r.getRight(), r.getBottom(), false));
    g.fillRect (r);

    // nebulosa: manchas radiales superpuestas
    const int blobs = avatar ? 4 : 9;
    for (int i = 0; i < blobs; ++i)
    {
        const float cx = r.getX() + W * (avatar ? rnd.nextFloat() : 0.35f + 0.6f * rnd.nextFloat());
        const float cy = r.getY() + H * rnd.nextFloat();
        const float rad = (avatar ? W : H) * (0.35f + 0.6f * rnd.nextFloat());
        const auto col = (i % 3 == 0 ? pal.a : i % 3 == 1 ? pal.b : pal.c).withAlpha (0.18f + 0.22f * rnd.nextFloat());
        g.setGradientFill (ColourGradient (col, cx, cy, col.withAlpha (0.0f), cx + rad, cy, true));
        g.fillEllipse (cx - rad, cy - rad, rad * 2, rad * 2);
    }

    // estrellas
    const int stars = avatar ? 14 : 170;
    for (int i = 0; i < stars; ++i)
    {
        const float x = r.getX() + W * rnd.nextFloat(), y = r.getY() + H * rnd.nextFloat();
        const float s = rnd.nextFloat() < 0.08f ? 1.8f : 0.9f;
        g.setColour (Colours::white.withAlpha (0.25f + 0.6f * rnd.nextFloat()));
        g.fillEllipse (x, y, s, s);
    }
    if (avatar)
    {
        // hélice mínima
        for (int strand = 0; strand < 2; ++strand)
        {
            Path p;
            for (int i = 0; i <= 24; ++i)
            {
                const float t = (float) i / 24.0f;
                const float x = r.getCentreX() + W * 0.22f * std::sin (t * 6.2f + (float) strand * MathConstants<float>::pi);
                const float y = r.getY() + H * (0.12f + 0.76f * t);
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            g.setColour ((strand == 0 ? pal.a : pal.b).withAlpha (0.9f));
            g.strokePath (p, PathStrokeType (1.6f));
        }
        return;
    }

    // planeta con borde iluminado y anillo
    {
        const float pr = H * (0.22f + 0.1f * rnd.nextFloat());
        const float px = r.getX() + W * (0.56f + 0.08f * rnd.nextFloat()), py = r.getY() + H * (0.5f + 0.15f * rnd.nextFloat());
        g.setGradientFill (ColourGradient (pal.c.brighter (0.3f), px - pr * 0.5f, py - pr * 0.6f, Colour (0xff05040f), px + pr * 0.6f, py + pr * 0.7f, true));
        g.fillEllipse (px - pr, py - pr, pr * 2, pr * 2);
        Path rim;
        rim.addCentredArc (px, py, pr, pr, 0.0f, -2.4f, -0.4f, true);
        strokeGlow (g, rim, pal.glow.withAlpha (0.8f), 1.4f);
        Path ring;
        ring.addEllipse (px - pr * 1.7f, py - pr * 0.28f, pr * 3.4f, pr * 0.56f);
        ring.applyTransform (AffineTransform::rotation (-0.25f, px, py));
        g.setColour (pal.a.withAlpha (0.35f));
        g.strokePath (ring, PathStrokeType (1.2f));
        // segundo cuerpo pequeño
        const float qr = pr * 0.28f, qx = px + pr * 2.1f, qy = py - pr * 0.9f;
        g.setGradientFill (ColourGradient (pal.b.withAlpha (0.9f), qx - qr * 0.4f, qy - qr * 0.4f, Colour (0xff09071c), qx + qr, qy + qr, true));
        g.fillEllipse (qx - qr, qy - qr, qr * 2, qr * 2);
    }

    // hélice de ADN en diagonal (muy sutil)
    for (int strand = 0; strand < 2; ++strand)
    {
        Path p;
        for (int i = 0; i <= 120; ++i)
        {
            const float t = (float) i / 120.0f;
            const float x = r.getX() + W * (0.25f + 0.55f * t);
            const float y = r.getY() + H * (0.15f + 0.1f * t) + H * 0.12f * std::sin (t * 18.0f + (float) strand * MathConstants<float>::pi);
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour ((strand == 0 ? pal.a : pal.b).withAlpha (0.18f));
        g.strokePath (p, PathStrokeType (1.2f));
    }

    // skyline de onda (barras simétricas)
    {
        const int bars = 120;
        const float x0 = r.getX() + W * 0.42f, x1 = r.getX() + W * 0.98f, base = r.getY() + H * 0.62f;
        float phase = rnd.nextFloat() * 10.0f;
        for (int i = 0; i < bars; ++i)
        {
            const float t = (float) i / (float) (bars - 1);
            const float env = std::pow (std::sin (MathConstants<float>::pi * t), 0.6f);
            const float n = 0.5f + 0.5f * std::sin (t * 37.0f + phase) * std::sin (t * 11.0f + phase * 0.7f);
            const float h = juce::jmax (1.0f, H * 0.22f * env * (0.25f + 0.75f * n * n));   // h = 0 -> degradado degenerado
            const float x = x0 + (x1 - x0) * t;
            g.setGradientFill (ColourGradient (pal.a.withAlpha (0.85f), x, base - h, pal.b.withAlpha (0.6f), x, base + h, false));
            g.fillRect (x, base - h, 2.0f, h * 2.0f);
        }
    }

    // viñeta para que el texto respire a la izquierda
    g.setGradientFill (ColourGradient (Colour (0xf007061a), r.getX(), 0, Colour (0x0007061a), r.getX() + W * 0.48f, 0, false));
    g.fillRect (r);
}

// ============================================================================
//  v3.1 MOTION: escena animada del hero. El fondo (degradado, nebulosa y hélice)
//  se renderiza una vez a imagen; estrellas, planeta, anillo, luna y skyline se
//  pintan en vivo cada frame (baratos: elipses, rectángulos y pocos degradados).
// ============================================================================
struct Star { float x, y, size, phase, speed, depth, alpha; };

struct Scene
{
    Palette pal;
    std::vector<Star> stars;
    float px = 0.6f, py = 0.55f, pr = 0.3f, ringTilt = -0.25f, skyPhase = 0.0f, moonPhase = 0.0f;
    juce::String key;

    static Scene make (const juce::String& key, const juce::StringArray& tags, const juce::String& genre)
    {
        Scene s;
        s.key = key;
        s.pal = paletteFor (tags, genre);
        juce::Random rnd (seedFor (key) ^ 0x5eed);
        for (int i = 0; i < 150; ++i)
        {
            const float depth = 0.2f + 0.8f * rnd.nextFloat();   // parallax: las cercanas se mueven más
            s.stars.push_back ({ rnd.nextFloat(), rnd.nextFloat(), rnd.nextFloat() < 0.1f ? 2.0f : 1.0f,
                                 rnd.nextFloat() * 6.28f, 0.6f + 1.8f * rnd.nextFloat(), depth, 0.3f + 0.6f * rnd.nextFloat() });
        }
        s.pr = 0.22f + 0.1f * rnd.nextFloat();
        s.px = 0.56f + 0.08f * rnd.nextFloat();
        s.py = 0.5f + 0.15f * rnd.nextFloat();
        s.ringTilt = -0.15f - 0.2f * rnd.nextFloat();
        s.skyPhase = rnd.nextFloat() * 10.0f;
        s.moonPhase = rnd.nextFloat() * 6.28f;
        return s;
    }
};

// fondo estático del hero (degradado + nebulosa + hélice)
inline void paintBackground (juce::Graphics& g, juce::Rectangle<float> r, const Scene& sc)
{
    using namespace juce;
    Random rnd (seedFor (sc.key));
    const auto& pal = sc.pal;
    const float W = r.getWidth(), H = r.getHeight();
    g.setGradientFill (ColourGradient (Colour (0xff07061a), r.getX(), r.getY(), Colour (0xff0d0a26), r.getRight(), r.getBottom(), false));
    g.fillRect (r);
    for (int i = 0; i < 9; ++i)
    {
        const float cx = r.getX() + W * (0.35f + 0.6f * rnd.nextFloat());
        const float cy = r.getY() + H * rnd.nextFloat();
        const float rad = H * (0.35f + 0.6f * rnd.nextFloat());
        const auto col = (i % 3 == 0 ? pal.a : i % 3 == 1 ? pal.b : pal.c).withAlpha (0.18f + 0.22f * rnd.nextFloat());
        g.setGradientFill (ColourGradient (col, cx, cy, col.withAlpha (0.0f), cx + rad, cy, true));
        g.fillEllipse (cx - rad, cy - rad, rad * 2, rad * 2);
    }
    for (int strand = 0; strand < 2; ++strand)
    {
        Path p;
        for (int i = 0; i <= 120; ++i)
        {
            const float t = (float) i / 120.0f;
            const float x = r.getX() + W * (0.25f + 0.55f * t);
            const float y = r.getY() + H * (0.15f + 0.1f * t) + H * 0.12f * std::sin (t * 18.0f + (float) strand * MathConstants<float>::pi);
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour ((strand == 0 ? pal.a : pal.b).withAlpha (0.18f));
        g.strokePath (p, PathStrokeType (1.2f));
    }
}

// elementos vivos. t = segundos; energy 0..1 (voz); bands = 32 bandas 0..1 (o nullptr); beat 0..1 (pulso)
inline void paintLive (juce::Graphics& g, juce::Rectangle<float> r, const Scene& sc, double t, float energy,
                       const float* bands, float beat, bool full)
{
    using namespace juce;
    const auto& pal = sc.pal;
    const float W = r.getWidth(), H = r.getHeight();
    const float tf = (float) t;

    // ---- estrellas con parallax y parpadeo ----
    for (size_t i = 0; i < sc.stars.size(); i += (full ? 1 : 2))
    {
        const auto& st = sc.stars[i];
        float x = st.x - tf * 0.004f * st.depth;
        x -= std::floor (x);
        const float twinkle = 0.55f + 0.45f * std::sin (tf * st.speed + st.phase);
        const float leftFade = jmap (jlimit (0.0f, 0.5f, x), 0.0f, 0.5f, 0.15f, 1.0f);
        const float a = jlimit (0.0f, 1.0f, st.alpha * twinkle * leftFade * (0.85f + 0.3f * energy));
        const float sz = st.size * (st.size > 1.5f ? (0.8f + 0.4f * twinkle) : 1.0f);
        g.setColour (Colours::white.withAlpha (a));
        g.fillRect (r.getX() + W * x, r.getY() + H * st.y, sz, sz);   // a este tamaño un rectángulo es igual de redondo y mucho más barato
        if (st.size > 1.5f && twinkle > 0.9f)   // destello en cruz
        {
            const float cx = r.getX() + W * x + sz * 0.5f, cy = r.getY() + H * st.y + sz * 0.5f;
            g.setColour (Colours::white.withAlpha (a * 0.5f));
            g.drawLine (cx - 4.0f, cy, cx + 4.0f, cy, 0.6f);
            g.drawLine (cx, cy - 4.0f, cx, cy + 4.0f, 0.6f);
        }
    }

    // ---- planeta: flota, anillo con partículas orbitando, luna en órbita ----
    const float pr = H * sc.pr;
    const float px = r.getX() + W * sc.px;
    const float py = r.getY() + H * sc.py + H * 0.012f * std::sin (tf * 0.45f);
    const float tilt = sc.ringTilt + 0.04f * std::sin (tf * 0.21f);
    auto ringPoint = [&] (float a, float scale)
    {
        const float ex = std::cos (a) * pr * 1.7f * scale, ey = std::sin (a) * pr * 0.28f * scale;
        return Point<float> (px + ex * std::cos (tilt) - ey * std::sin (tilt), py + ex * std::sin (tilt) + ey * std::cos (tilt));
    };
    auto ringHalf = [&] (bool back)
    {
        Path ring;
        const float a0 = back ? MathConstants<float>::pi : 0.0f;
        for (int i = 0; i <= 40; ++i)
        {
            const auto q = ringPoint (a0 + MathConstants<float>::pi * (float) i / 40.0f, 1.0f);
            if (i == 0) ring.startNewSubPath (q); else ring.lineTo (q);
        }
        g.setColour (pal.a.withAlpha (back ? 0.22f : 0.42f));
        g.strokePath (ring, PathStrokeType (1.2f));
    };
    ringHalf (true);
    // luna detrás o delante según la órbita
    const float ma = sc.moonPhase + tf * 0.22f;
    const float mr = pr * 0.26f;
    const Point<float> moon (px + std::cos (ma) * pr * 2.2f, py - pr * 0.75f + std::sin (ma) * pr * 0.35f);
    const bool moonFront = std::sin (ma) > 0.0f;
    auto drawMoon = [&]
    {
        g.setGradientFill (ColourGradient (pal.b.withAlpha (0.95f), moon.x - mr * 0.4f, moon.y - mr * 0.4f, Colour (0xff09071c), moon.x + mr, moon.y + mr, true));
        g.fillEllipse (moon.x - mr, moon.y - mr, mr * 2, mr * 2);
    };
    if (! moonFront) drawMoon();
    g.setGradientFill (ColourGradient (pal.c.brighter (0.3f), px - pr * 0.5f, py - pr * 0.6f, Colour (0xff05040f), px + pr * 0.6f, py + pr * 0.7f, true));
    g.fillEllipse (px - pr, py - pr, pr * 2, pr * 2);
    {
        // luz de borde que recorre lentamente el planeta
        const float la = -2.4f + 0.25f * std::sin (tf * 0.3f);
        Path rim;
        rim.addCentredArc (px, py, pr, pr, 0.0f, la, la + 2.0f, true);
        strokeGlow (g, rim, pal.glow.withAlpha (0.65f + 0.3f * jmax (energy, beat)), 1.4f, full ? 2 : 1);
    }
    ringHalf (false);
    if (full)   // partículas del anillo
        for (int i = 0; i < 26; ++i)
        {
            const float a = tf * (0.35f + 0.02f * (float) (i % 5)) + (float) i * 0.2417f * MathConstants<float>::twoPi;
            const auto q = ringPoint (a, 0.9f + 0.2f * (float) ((i * 7) % 5) / 4.0f);
            const bool front = std::sin (a) > 0.0f;
            g.setColour (pal.glow.withAlpha (front ? 0.85f : 0.25f));
            g.fillRect (q.x - 0.9f, q.y - 0.9f, 1.8f, 1.8f);
        }
    if (moonFront) drawMoon();

    // ---- skyline: onda en reposo que se convierte en el espectro de la voz ----
    // (todas las barras en un solo Path con un único degradado: mucho más barato que 120 degradados)
    {
        const int bars = full ? 120 : 60;
        const float x0 = r.getX() + W * 0.42f, x1 = r.getX() + W * 0.98f, base = r.getY() + H * 0.62f;
        const float bw = (x1 - x0) / (float) bars;
        const float mixv = jlimit (0.0f, 1.0f, energy * 4.0f);
        Path sky;
        for (int i = 0; i < bars; ++i)
        {
            const float u = (float) i / (float) (bars - 1);
            const float env = std::pow (std::sin (MathConstants<float>::pi * u), 0.6f);
            const float n = 0.5f + 0.5f * std::sin (u * 37.0f + sc.skyPhase + tf * 0.9f) * std::sin (u * 11.0f + sc.skyPhase * 0.7f - tf * 0.6f);
            const float idle = env * (0.25f + 0.75f * n * n) * 0.6f;
            float live = 0.0f;
            if (bands != nullptr && mixv > 0.0f)
            {
                const float fi = std::abs (u - 0.5f) * 2.0f * 30.0f;   // espejo: graves en el centro
                const int k = jlimit (0, 30, (int) fi);
                const float fr = fi - (float) k;
                live = (bands[k] * (1.0f - fr) + bands[k + 1] * fr) * env;
            }
            const float hv = idle * (1.0f - mixv) + live * mixv;
            const float h = jmax (1.0f, H * 0.3f * hv * (1.0f + 0.25f * beat));
            sky.addRectangle (x0 + bw * (float) i, base - h, jmax (1.0f, bw * 0.62f), h * 2.0f);
        }
        g.setGradientFill (ColourGradient (pal.a.withAlpha (0.9f), 0.0f, base - H * 0.3f, pal.b.withAlpha (0.65f), 0.0f, base + H * 0.3f, false));
        g.fillPath (sky);
    }

    // viñeta para que el texto respire a la izquierda (sólo donde tiene efecto)
    g.setGradientFill (ColourGradient (Colour (0xf007061a), r.getX(), 0, Colour (0x0007061a), r.getX() + W * 0.48f, 0, false));
    g.fillRect (r.withWidth (W * 0.48f));
}

inline juce::Image render (int w, int h, const juce::String& key, const juce::StringArray& tags, const juce::String& genre, bool avatar)
{
    juce::Image img (juce::Image::ARGB, juce::jmax (1, w), juce::jmax (1, h), true);
    juce::Graphics g (img);
    paint (g, { 0, 0, (float) w, (float) h }, key, tags, genre, avatar);
    return img;
}
} // namespace pv::ui::art
