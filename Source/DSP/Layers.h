#pragma once
#include "Effects.h"
#include "PitchCorrector.h"
#include <array>
#include <atomic>

// ============================================================================
//  LayerEngine v3 — 8 capas con cadena propia (filtros, drive, envíos).
//
//  Limitación declarada: una instancia insertada sobre UNA voz no puede
//  recuperar tomas reales. Las capas 2-8 son SINTÉTICAS: se generan a partir
//  del lead procesado. Para dobles/adlibs reales: otra pista + otra instancia.
//
//  1 LEAD      voz principal (estéreo original)                   0 dB / C
//  2 DOUBLE L  micro-pitch -detune, 15 ms, banda 180 Hz-10 kHz     -9 dB / L35
//  3 DOUBLE R  micro-pitch +detune, 23 ms                          -9 dB / R35
//  4 ADLIBS    banda 400-3.5k + drive + ecos ping-pong a 1/8      -12 dB / R45
//  5 HARMONY   armonizador DIATÓNICO (sigue key/scale de DNA TUNE) -12 dB / C
//  6 OCTAVE    -1 / +1 / ambas octavas                             -13 dB / C
//  7 TEXTURE   pad: hall 4.5 s 100 % húmedo con ducking fuerte     -18 dB / ancho
//  8 FX        teléfono 450-3.2k + distorsión dura                 -15 dB / C
//
//  Cada capa: HPF + LPF (12 dB/oct, SVF), drive (tanh compensado), nivel,
//  panorama y dos envíos (delay / reverb, relativos al envío global).
//  Con los valores por defecto (sin filtros, sin drive, envíos 100 %) el
//  resultado es idéntico al ruteo de la v2.
// ============================================================================
namespace pv
{
// SVF de Zavalishin (TPT): estable con modulación, sin asignaciones
struct Svf
{
    float ic1 = 0.0f, ic2 = 0.0f;
    void reset() { ic1 = ic2 = 0.0f; }
    // devuelve (lp, hp) a la vez
    inline void tick (float x, float g, float k, float& lp, float& hp) noexcept
    {
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + g * a1 * v3;
        const float v2 = ic2 + g * v1;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2;
        hp = x - k * v1 - v2;
    }
};

// Intervalo diatónico a partir de la nota (MIDI entero) y la escala de la canción
struct Harmony
{
    enum Interval { thirdUp = 0, fifthUp, thirdDown, fourthDown, sixthUp, stack35 };

    static const juce::StringArray& intervalNames()
    {
        static const juce::StringArray n { "3rd Up", "5th Up", "3rd Down", "4th Down", "6th Up", "3rd + 5th" };
        return n;
    }

    static bool inScale (int midi, int key, int scale) noexcept
    {
        const int rel = ((midi - key) % 12 + 12) % 12;
        return (pvscales::masks[(size_t) juce::jlimit (0, 8, scale)] >> rel) & 1u;
    }

    // semitonos desde 'note' hasta el grado +steps (o -steps) de la escala
    static int diatonic (int note, int steps, int key, int scale) noexcept
    {
        if (scale == 0)   // cromática: intervalos mayores fijos
        {
            static constexpr int up[] { 0, 2, 4, 5, 7, 9, 11, 12 };
            const int s = juce::jlimit (-7, 7, steps);
            return s >= 0 ? up[s] : -up[-s];
        }
        // si la nota no está en la escala se toma la más cercana de la escala como grado de partida
        int base = note;
        for (int d = 0; d < 6; ++d)
        {
            if (inScale (note - d, key, scale)) { base = note - d; break; }
            if (inScale (note + d, key, scale)) { base = note + d; break; }
        }
        int n = base, count = 0;
        const int dir = steps >= 0 ? 1 : -1;
        while (count < std::abs (steps))
        {
            n += dir;
            if (inScale (n, key, scale)) ++count;
            if (std::abs (n - base) > 24) break;
        }
        return n - note;
    }

    // devuelve hasta dos desplazamientos (semitonos); second = 0 si no hay segunda voz
    static void intervals (int interval, int note, int key, int scale, int& first, int& second) noexcept
    {
        second = 0;
        switch (interval)
        {
            case thirdUp:    first = diatonic (note, 2, key, scale); break;
            case fifthUp:    first = diatonic (note, 4, key, scale); break;
            case thirdDown:  first = diatonic (note, -2, key, scale); break;
            case fourthDown: first = diatonic (note, -3, key, scale); break;
            case sixthUp:    first = diatonic (note, 5, key, scale); break;
            default:         first = diatonic (note, 2, key, scale); second = diatonic (note, 4, key, scale); break;
        }
    }
};

class LayerEngine
{
public:
    static constexpr int numRoutes = 8;
    enum Route { lead = 0, doubleL, doubleR, adlibs, harmony, octave, texture, fxLayer };
    enum OctaveMode { octDown = 0, octUp, octBoth };

    static const juce::StringArray& routeNames()
    {
        static const juce::StringArray n { "LEAD", "DOUBLE L", "DOUBLE R", "ADLIBS", "HARMONY", "OCTAVE", "TEXTURE", "FX" };
        return n;
    }
    static const juce::StringArray& octaveModeNames()
    {
        static const juce::StringArray n { "-1 Oct", "+1 Oct", "-1 & +1" };
        return n;
    }
    static constexpr float defaultLevel[numRoutes] { 0.0f, -9.0f, -9.0f, -12.0f, -12.0f, -13.0f, -18.0f, -15.0f };
    static constexpr float defaultPan[numRoutes]   { 0.0f, -35.0f, 35.0f, 45.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    struct Settings
    {
        bool on = false;
        float levelDb = 0.0f, pan = 0.0f;
        float hpHz = 20.0f, lpHz = 20000.0f, drive01 = 0.0f, sendDly01 = 1.0f, sendRev01 = 1.0f;
    };
    struct Global
    {
        float detuneCents = 8.0f;
        double bpm = 120.0;
        int harmonyInterval = 0, octaveMode = 0;
        int key = 0, scale = 2;
        float noteMidi = -1.0f;   // nota (tras DNA TUNE) para el armonizador; <0 = sin voz
    };

    void prepare (double sampleRate, int maxBlock)
    {
        fs = sampleRate;
        dblL.prepare (fs, 15.0f, 45.0f);
        dblR.prepare (fs, 23.0f, 45.0f);
        octLow.prepare (fs, 8.0f, 60.0f);
        octHigh.prepare (fs, 6.0f, 30.0f);
        harm1.prepare (fs, 10.0f, 40.0f);
        harm2.prepare (fs, 12.0f, 40.0f);
        adlibDelay.prepare (fs);
        atmos.prepare (fs);
        c180 = onePoleCoef (fs, 180.0); c10k = onePoleCoef (fs, 10000.0);
        c2k5 = onePoleCoef (fs, 2500.0); c350 = onePoleCoef (fs, 350.0);
        c400 = onePoleCoef (fs, 400.0); c3k5 = onePoleCoef (fs, 3500.0);
        c450 = onePoleCoef (fs, 450.0); c3k2 = onePoleCoef (fs, 3200.0);
        c200 = onePoleCoef (fs, 200.0); c1k = onePoleCoef (fs, 1000.0); c4k = onePoleCoef (fs, 4000.0);
        for (auto* arr : { &gainL, &gainR, &sendD, &sendR })
            for (auto& g : *arr) { g.reset (fs, 0.03); g.setCurrentAndTargetValue (0.0f); }
        for (auto& s : hpSm) { s.reset (fs, 0.05); s.setCurrentAndTargetValue (20.0f); }
        for (auto& s : lpSm) { s.reset (fs, 0.05); s.setCurrentAndTargetValue (20000.0f); }
        for (auto& s : drvSm) { s.reset (fs, 0.05); s.setCurrentAndTargetValue (0.0f); }
        ratio1.reset (fs, 0.03); ratio1.setCurrentAndTargetValue (1.0f);
        ratio2.reset (fs, 0.03); ratio2.setCurrentAndTargetValue (1.0f);
        for (auto* v : { &src, &adl, &adlEchoL, &adlEchoR, &atmL, &atmR, &bufL, &bufR, &outL, &outR })
            v->assign ((size_t) juce::jmax (1, maxBlock), 0.0f);
        reset();
    }

    void reset()
    {
        dblL.clear(); dblR.clear(); octLow.clear(); octHigh.clear(); harm1.clear(); harm2.clear();
        adlibDelay.reset(); atmos.reset();
        for (auto& f : flt) f = 0.0f;
        for (auto& ch : svf) for (auto& s : ch) s.reset();
        for (auto& b : band) b.fill (0.0f);
        lastNote = -1;
    }

    // L/R entran como lead y salen como mezcla de capas. sendDly*/sendRev* reciben la
    // suma de las capas ponderada por sus envíos (entrada de los efectos de envío).
    void process (float* L, float* R, const float* key, int n, const std::array<Settings, numRoutes>& st, const Global& gl,
                  float* sendDlyL, float* sendDlyR, float* sendRevL, float* sendRevR)
    {
        jassert (n <= (int) src.size());
        n = juce::jmin (n, (int) src.size());

        std::array<bool, numRoutes> active {};
        for (int r = 0; r < numRoutes; ++r)
        {
            const auto& s = st[(size_t) r];
            const float g = s.on ? std::pow (10.0f, s.levelDb * 0.05f) : 0.0f;
            const float p = juce::jlimit (-1.0f, 1.0f, s.pan / 50.0f);
            const float a = (p + 1.0f) * juce::MathConstants<float>::pi * 0.25f;   // potencia constante
            gainL[(size_t) r].setTargetValue (g * std::cos (a) * juce::MathConstants<float>::sqrt2);
            gainR[(size_t) r].setTargetValue (g * std::sin (a) * juce::MathConstants<float>::sqrt2);
            sendD[(size_t) r].setTargetValue (juce::jlimit (0.0f, 1.0f, s.sendDly01));
            sendR[(size_t) r].setTargetValue (juce::jlimit (0.0f, 1.0f, s.sendRev01));
            hpSm[(size_t) r].setTargetValue (juce::jlimit (20.0f, 2000.0f, s.hpHz));
            lpSm[(size_t) r].setTargetValue (juce::jlimit (1000.0f, 20000.0f, s.lpHz));
            drvSm[(size_t) r].setTargetValue (juce::jlimit (0.0f, 1.0f, s.drive01));
            active[(size_t) r] = g > 0.0f || gainL[(size_t) r].isSmoothing() || gainR[(size_t) r].isSmoothing();
        }

        for (int i = 0; i < n; ++i)
            src[(size_t) i] = R != nullptr ? 0.5f * (L[i] + R[i]) : L[i];

        // ---- armonizador: intervalo diatónico desde la nota actual ----
        if (gl.noteMidi >= 0.0f)
            lastNote = (int) std::round (gl.noteMidi);
        if (lastNote >= 0)
        {
            int s1 = 0, s2 = 0;
            Harmony::intervals (gl.harmonyInterval, lastNote, gl.key, gl.scale, s1, s2);
            ratio1.setTargetValue (std::exp2 ((float) s1 / 12.0f));
            ratio2.setTargetValue (std::exp2 ((float) (s2 != 0 ? s2 : s1) / 12.0f));
        }
        const bool stack = gl.harmonyInterval == Harmony::stack35;

        // ---- capas con proceso por bloque (ecos de adlibs y textura) ----
        if (active[adlibs] && ! wasAdlib) adlibDelay.reset();
        if (active[texture] && ! wasAtmos) atmos.reset();
        wasAdlib = active[adlibs]; wasAtmos = active[texture];
        if (active[adlibs])
        {
            for (int i = 0; i < n; ++i)
            {
                float x = src[(size_t) i];
                flt[8] = x + c400 * (flt[8] - x);  x -= flt[8];
                flt[9] = x + c3k5 * (flt[9] - x);  x = flt[9];
                adl[(size_t) i] = std::tanh (3.0f * x) * 0.6f;
                adlEchoL[(size_t) i] = adlEchoR[(size_t) i] = 0.0f;
            }
            const float eighth = (float) (30.0 / juce::jlimit (30.0, 300.0, gl.bpm));
            adlibDelay.process (adl.data(), adl.data(), key, adlEchoL.data(), adlEchoR.data(), n,
                                eighth, true, 0.38f, 1.4f, 6.0f);
        }
        if (active[texture])
        {
            std::fill (atmL.begin(), atmL.begin() + n, 0.0f);
            std::fill (atmR.begin(), atmR.begin() + n, 0.0f);
            atmos.process (src.data(), src.data(), key, atmL.data(), atmR.data(), n,
                           FDNReverb::hall, 4.5f, 60.0f, 6000.0f, 2.2f, 10.0f);
        }

        std::fill (outL.begin(), outL.begin() + n, 0.0f);
        std::fill (outR.begin(), outR.begin() + n, 0.0f);
        std::fill (sendDlyL, sendDlyL + n, 0.0f);
        std::fill (sendDlyR, sendDlyR + n, 0.0f);
        std::fill (sendRevL, sendRevL + n, 0.0f);
        std::fill (sendRevR, sendRevR + n, 0.0f);

        for (int r = 0; r < numRoutes; ++r)
        {
            // las líneas de los shifters siguen al día aunque la capa esté apagada
            if (! active[(size_t) r])
            {
                feedInactive (r, n);
                meter[(size_t) r].store (0.0f);
                for (auto& b : bandOut[(size_t) r]) b.store (0.0f);
                continue;
            }
            const bool stereo = renderLayer (r, L, R, n, gl, stack);   // -> bufL (y bufR si estéreo)
            applyChannelStrip (r, n, stereo);
            accumulate (r, n, stereo, sendDlyL, sendDlyR, sendRevL, sendRevR);
        }

        std::copy (outL.begin(), outL.begin() + n, L);
        if (R != nullptr) std::copy (outR.begin(), outR.begin() + n, R);
        else for (int i = 0; i < n; ++i) L[i] = 0.5f * (outL[(size_t) i] + outR[(size_t) i]);
    }

    // nivel de pico reciente de cada capa (lineal) y energía en 4 bandas (<200, 200-1k, 1k-4k, >4k)
    float getLevel (int r) const { return meter[(size_t) r].load(); }
    float getBand (int r, int b) const { return bandOut[(size_t) r][(size_t) b].load(); }
    bool isAudible (int r) const { return meter[(size_t) r].load() > 1.0e-5f; }

private:
    void feedInactive (int r, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float x = src[(size_t) i];
            switch (r)
            {
                case doubleL: dblL.feed (x); break;
                case doubleR: dblR.feed (x); break;
                case harmony: harm1.feed (x); harm2.feed (x); ratio1.getNextValue(); ratio2.getNextValue(); break;
                case octave:  octLow.feed (x); octHigh.feed (x); break;
                default: break;
            }
        }
    }

    // genera la señal de la capa r (antes de filtros/drive/pan). true = estéreo
    bool renderLayer (int r, const float* L, const float* R, int n, const Global& gl, bool stack)
    {
        switch (r)
        {
            case lead:
                for (int i = 0; i < n; ++i) { bufL[(size_t) i] = L[i]; bufR[(size_t) i] = R != nullptr ? R[i] : L[i]; }
                return true;
            case doubleL:
            case doubleR:
            {
                auto& sh = r == doubleL ? dblL : dblR;
                const float cents = (r == doubleL ? -1.0f : 1.0f) * gl.detuneCents;
                float& a = flt[r == doubleL ? 0 : 2];
                float& b = flt[r == doubleL ? 1 : 3];
                for (int i = 0; i < n; ++i)
                {
                    const float d = sh.process (src[(size_t) i], cents);
                    a = d + c10k * (a - d); b = a + c180 * (b - a);
                    bufL[(size_t) i] = a - b;
                }
                return false;
            }
            case adlibs:
                std::copy (adlEchoL.begin(), adlEchoL.begin() + n, bufL.begin());
                std::copy (adlEchoR.begin(), adlEchoR.begin() + n, bufR.begin());
                return true;
            case harmony:
                for (int i = 0; i < n; ++i)
                {
                    const float x = src[(size_t) i];
                    float y = harm1.processRatio (x, ratio1.getNextValue());
                    const float r2 = ratio2.getNextValue();
                    if (stack) y = 0.7f * (y + harm2.processRatio (x, r2));
                    else harm2.feed (x);
                    bufL[(size_t) i] = y;
                }
                return false;
            case octave:
                for (int i = 0; i < n; ++i)
                {
                    const float x = src[(size_t) i];
                    float y = 0.0f;
                    if (gl.octaveMode != octUp)
                    {
                        float lo = octLow.processRatio (x, 0.5f);
                        flt[4] = lo + c2k5 * (flt[4] - lo);
                        y += flt[4];
                    }
                    else octLow.feed (x);
                    if (gl.octaveMode != octDown)
                    {
                        float hi = octHigh.processRatio (x, 2.0f);
                        flt[5] = hi + c350 * (flt[5] - hi);
                        y += hi - flt[5];
                    }
                    else octHigh.feed (x);
                    bufL[(size_t) i] = gl.octaveMode == octBoth ? 0.75f * y : y;
                }
                return false;
            case texture:
                std::copy (atmL.begin(), atmL.begin() + n, bufL.begin());
                std::copy (atmR.begin(), atmR.begin() + n, bufR.begin());
                return true;
            default:   // FX: teléfono + distorsión
                for (int i = 0; i < n; ++i)
                {
                    float fx = src[(size_t) i];
                    flt[6] = fx + c450 * (flt[6] - fx); fx -= flt[6];
                    flt[7] = fx + c3k2 * (flt[7] - fx); fx = flt[7];
                    bufL[(size_t) i] = juce::jlimit (-0.35f, 0.35f, fx * 8.0f) * 1.2f;
                }
                return false;
        }
    }

    // HPF + LPF + drive de la capa (se salta si está neutro)
    void applyChannelStrip (int r, int n, bool stereo)
    {
        auto& hs = hpSm[(size_t) r];
        auto& ls = lpSm[(size_t) r];
        auto& ds = drvSm[(size_t) r];
        const bool neutral = ! hs.isSmoothing() && ! ls.isSmoothing() && ! ds.isSmoothing()
                             && hs.getCurrentValue() <= 20.5f && ls.getCurrentValue() >= 19900.0f && ds.getCurrentValue() <= 0.0f;
        if (neutral) { hs.skip (n); ls.skip (n); ds.skip (n); return; }

        const int numCh = stereo ? 2 : 1;
        constexpr int chunk = 32;
        const float nyq = (float) fs * 0.49f;
        for (int s0 = 0; s0 < n; s0 += chunk)
        {
            const int len = juce::jmin (chunk, n - s0);
            const float hp = hs.skip (len), lp = juce::jmin (ls.skip (len), nyq), drv = ds.skip (len);
            const bool useHp = hp > 20.5f, useLp = lp < 19900.0f && lp < nyq - 1.0f;
            const float gH = std::tan (juce::MathConstants<float>::pi * hp / (float) fs);
            const float gL = std::tan (juce::MathConstants<float>::pi * lp / (float) fs);
            constexpr float k = 1.41421356f;   // Butterworth
            const float drvGain = 1.0f + 9.0f * drv * drv;
            const float drvComp = 1.0f / std::tanh (drvGain);   // ~0 dB en pico pleno
            for (int c = 0; c < numCh; ++c)
            {
                auto& buf = c == 0 ? bufL : bufR;
                auto& fh = svf[(size_t) r][(size_t) (c * 2)];
                auto& fl = svf[(size_t) r][(size_t) (c * 2 + 1)];
                for (int i = s0; i < s0 + len; ++i)
                {
                    float x = buf[(size_t) i], lo, hi;
                    if (useHp) { fh.tick (x, gH, k, lo, hi); x = hi; }
                    if (useLp) { fl.tick (x, gL, k, lo, hi); x = lo; }
                    if (drv > 0.0f) x = x + drv * (std::tanh (drvGain * x) * drvComp - x);
                    buf[(size_t) i] = x;
                }
            }
        }
    }

    void accumulate (int r, int n, bool stereo, float* dL, float* dR, float* rL, float* rR)
    {
        auto& gl = gainL[(size_t) r];
        auto& gr = gainR[(size_t) r];
        auto& sd = sendD[(size_t) r];
        auto& sr = sendR[(size_t) r];
        float peak = 0.0f;
        auto& bs = band[(size_t) r];
        std::array<float, 4> be {};
        for (int i = 0; i < n; ++i)
        {
            const float xl = bufL[(size_t) i];
            const float xr = stereo ? bufR[(size_t) i] : xl;
            const float l = xl * gl.getNextValue(), rr = xr * gr.getNextValue();
            const float a = sd.getNextValue(), b = sr.getNextValue();
            outL[(size_t) i] += l; outR[(size_t) i] += rr;
            dL[i] += a * l; dR[i] += a * rr;
            rL[i] += b * l; rR[i] += b * rr;

            // medidores por capa: pico + 4 bandas (crossovers de un polo)
            const float m = 0.5f * (l + rr);
            peak = juce::jmax (peak, std::abs (l), std::abs (rr));
            bs[0] = m + c200 * (bs[0] - m);          // < 200
            bs[1] = m + c1k * (bs[1] - m);           // < 1k
            bs[2] = m + c4k * (bs[2] - m);           // < 4k
            const float b0 = bs[0], b1 = bs[1] - bs[0], b2 = bs[2] - bs[1], b3 = m - bs[2];
            be[0] += b0 * b0; be[1] += b1 * b1; be[2] += b2 * b2; be[3] += b3 * b3;
        }
        meter[(size_t) r].store (juce::jmax (peak, meter[(size_t) r].load() * 0.8f));
        for (int b = 0; b < 4; ++b)
            bandOut[(size_t) r][(size_t) b].store (std::sqrt (be[(size_t) b] / (float) juce::jmax (1, n)));
    }

    double fs = 48000.0;
    MicroShifter dblL, dblR, octLow, octHigh, harm1, harm2;
    PingPongDelay adlibDelay;
    FDNReverb atmos;
    std::vector<float> src, adl, adlEchoL, adlEchoR, atmL, atmR, bufL, bufR, outL, outR;
    float flt[10] {};
    float c180 = 0, c10k = 0, c2k5 = 0, c350 = 0, c400 = 0, c3k5 = 0, c450 = 0, c3k2 = 0, c200 = 0, c1k = 0, c4k = 0;
    std::array<juce::SmoothedValue<float>, numRoutes> gainL, gainR, sendD, sendR, hpSm, lpSm, drvSm;
    juce::SmoothedValue<float> ratio1, ratio2;
    std::array<std::array<Svf, 4>, numRoutes> svf {};
    std::array<std::array<float, 3>, numRoutes> band {};
    std::array<std::atomic<float>, numRoutes> meter {};
    std::array<std::array<std::atomic<float>, 4>, numRoutes> bandOut {};
    int lastNote = -1;
    bool wasAdlib = false, wasAtmos = false;
};
} // namespace pv
