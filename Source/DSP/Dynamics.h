#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>

namespace pv
{
inline float msToCoef (double fs, float ms)
{
    return std::exp (-1.0f / (juce::jmax (0.01f, ms) * 0.001f * (float) fs));
}

inline float toDb (float g)   { return 20.0f * std::log10 (juce::jmax (g, 1.0e-6f)); }
inline float fromDb (float d) { return std::pow (10.0f, d * 0.05f); }

// ----------------------------------------------------------------------------
// Noise gate con hold y rango (-40 dB) para no "cortar" de golpe
// ----------------------------------------------------------------------------
class Gate
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        envAtt = msToCoef (fs, 0.3f);
        envRel = msToCoef (fs, 40.0f);
        openCoef = msToCoef (fs, 1.5f);
        closeCoef = msToCoef (fs, 90.0f);
        holdSamples = (int) (0.06 * fs);
        env = 0.0f; gain = 1.0f; holdCounter = 0;
    }

    void setThreshold (float db) { thresholdDb = db; thresholdLin = fromDb (db); }
    bool isBypassed() const { return thresholdDb <= -79.5f; }

    float process (float level)
    {
        env = level > env ? level + envAtt * (env - level) : level + envRel * (env - level);

        float target;
        if (env > thresholdLin)            { target = 1.0f; holdCounter = holdSamples; }
        else if (holdCounter > 0)          { target = 1.0f; --holdCounter; }
        else                                target = floorGain;

        gain = target > gain ? target + openCoef * (gain - target) : target + closeCoef * (gain - target);
        return gain;
    }

    float getGain() const { return gain; }

private:
    double fs = 44100.0;
    float thresholdDb = -80.0f, thresholdLin = 0.0001f;
    float envAtt = 0, envRel = 0, openCoef = 0, closeCoef = 0, env = 0, gain = 1.0f;
    const float floorGain = 0.01f;
    int holdSamples = 0, holdCounter = 0;
};

// ----------------------------------------------------------------------------
// Compresor con dos modelos (v2.3):
//  · VCA  : feed-forward limpio con soft-knee (el de versiones anteriores).
//  · FET  : inspirado en la topología del 1176:
//           - detección por REALIMENTACIÓN (mide la salida). Se resuelve de forma
//             implícita cada muestra -> estable con cualquier ratio y ataque, y con
//             la misma curva estática que el VCA (las calibraciones de GR valen).
//           - release dependiente del programa: tras reducción sostenida, el
//             release se alarga hasta x3 (memoria de ~1 s).
//           - el FET como resistencia controlada añade 2.º armónico proporcional
//             a la reducción (applyColour).
// ----------------------------------------------------------------------------
class Compressor
{
public:
    enum Model { fet = 0, vca };

    void prepare (double sampleRate)
    {
        fs = sampleRate; grSmoothed = 0.0f; env = 0.0f; fbEnvDb = -120.0f; sustain = 0.0f;
        makeupSm.reset (fs, 0.05);
        makeupSm.setCurrentAndTargetValue (makeup);
        thrSm.reset (fs, 0.03); thrSm.setCurrentAndTargetValue (threshold);
        ratioSm.reset (fs, 0.03); ratioSm.setCurrentAndTargetValue (ratio);
        sustainUp = 1.0f - std::exp (-1.0f / (1.0f * (float) fs));
        sustainDown = 1.0f - std::exp (-1.0f / (2.0f * (float) fs));
        dc = {};
    }

    void setModel (Model m) noexcept { model = m; }
    Model getModel() const noexcept { return model; }

    void setParameters (float thrDb, float ratio_, float attMs, float relMs, float makeupDb)
    {
        threshold = thrDb;
        ratio = juce::jmax (1.0f, ratio_);
        attCoef = msToCoef (fs, attMs);
        relCoef = msToCoef (fs, relMs);
        relMsBase = relMs;
        thrSm.setTargetValue (thrDb);
        ratioSm.setTargetValue (ratio);
        makeup = makeupDb;
        makeupSm.setTargetValue (makeupDb);
        detRel = msToCoef (fs, 25.0f);
        detRelFet = msToCoef (fs, 3.0f);   // FET: el suavizado lo hace el propio lazo de realimentación
    }

    float process (float level)
    {
        // detector de pico con caída suave (más musical en voces)
        const float dr = model == fet ? detRelFet : detRel;
        env = level > env ? level : level + dr * (env - level);
        const float x = toDb (env);
        const float thr = thrSm.getNextValue();   // cambios de preset sin saltos de ganancia
        const float rat = ratioSm.getNextValue();

        if (model == vca)
        {
            const float over = x - thr;
            const float slope = 1.0f / rat - 1.0f;
            float gr;
            if (2.0f * over < -knee)                 gr = 0.0f;
            else if (2.0f * std::abs (over) <= knee) gr = slope * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.0f * knee);
            else                                     gr = slope * over;
            const float target = -gr;
            grSmoothed = target > grSmoothed ? target + attCoef * (grSmoothed - target)
                                             : target + relCoef * (grSmoothed - target);
        }
        else
        {
            // FET realimentado: e = envolvente (dB) de la SALIDA; gr = (R-1)(e - th).
            // e' = e + c (x - gr(e') - e)  ->  despeje cerrado de e' (implícito, estable)
            const float outNow = x - grSmoothed;
            const bool attack = outNow > fbEnvDb;
            float relC = relCoef;
            if (! attack)
            {
                // release dependiente del programa: hasta x3 tras reducción sostenida
                const float relMs = relMsBase * (1.0f + 2.0f * sustain);
                relC = msToCoef (fs, relMs);
            }
            const float c = 1.0f - (attack ? attCoef : relC);
            const float rm1 = rat - 1.0f;
            float e = fbEnvDb + c * (x - fbEnvDb);   // suponiendo sin reducción
            float gr = 0.0f;
            if (e > thr)
            {
                e = (fbEnvDb + c * (x + rm1 * thr - fbEnvDb)) / (1.0f + c * rm1);
                gr = rm1 * juce::jmax (0.0f, e - thr);
            }
            fbEnvDb = juce::jmax (-120.0f, e);
            grSmoothed = gr;
            sustain += ((gr > 2.0f ? 1.0f : 0.0f) - sustain) * (gr > 2.0f ? sustainUp : sustainDown);
        }
        return fromDb (makeupSm.getNextValue() - grSmoothed);
    }

    // FET: 2.º armónico proporcional a la reducción (g = ganancia SIN make-up aplicada)
    float applyColour (int chan, float xIn, float gTotal) noexcept
    {
        if (model != fet) return xIn * gTotal;
        const float red = 1.0f - fromDb (-grSmoothed);            // 0 .. 1
        const float y = xIn * gTotal * (1.0f + 0.12f * red * juce::jlimit (-1.5f, 1.5f, xIn));
        // bloqueo de DC del término par
        auto& d = dc[(size_t) (chan & 1)];
        const float out = y - d[0] + 0.9995f * d[1];
        d[0] = y; d[1] = out;
        return out;
    }

    float getGainReductionDb() const { return grSmoothed; }

private:
    double fs = 44100.0;
    Model model = fet;
    juce::SmoothedValue<float> makeupSm, thrSm, ratioSm;
    float threshold = -18.0f, ratio = 3.0f, attCoef = 0, relCoef = 0, makeup = 0, detRel = 0, detRelFet = 0, relMsBase = 80.0f;
    float env = 0.0f, grSmoothed = 0.0f, fbEnvDb = -120.0f, sustain = 0.0f, sustainUp = 0.0f, sustainDown = 0.0f;
    std::array<std::array<float, 2>, 2> dc {};
    const float knee = 6.0f;
};

// ----------------------------------------------------------------------------
// De-esser de banda partida: detecta sibilancia con un pasabanda y sólo
// atenúa la banda alta (crossover Linkwitz-Riley) => no apaga la voz entera
// ----------------------------------------------------------------------------
class DeEsser
{
public:
    void prepare (double sampleRate, int maxBlock)
    {
        fs = sampleRate;
        juce::dsp::ProcessSpec spec { fs, (juce::uint32) maxBlock, 2 };
        crossover.prepare (spec);
        crossover.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
        att = msToCoef (fs, 0.4f);
        rel = msToCoef (fs, 60.0f);
        env = envFull = 0.0f; gr = 0.0f; lastFreq = -1.0f;
        detector.coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (fs, 6500.0f, 1.4f);   // se reutiliza: sin allocs en audio
        detector.reset();
        setParameters (-28.0f, 6500.0f);
    }

    void setParameters (float thrDb, float freq)
    {
        threshold = thrDb;
        if (std::abs (freq - lastFreq) > 1.0f)
        {
            lastFreq = freq;
            const float f = juce::jmin (freq, (float) (fs * 0.45));
            *detector.coefficients = juce::dsp::IIR::ArrayCoefficients<float>::makeBandPass (fs, f, 1.4f);
            crossover.setCutoffFrequency (f * 0.72f);
        }
    }

    void process (float* l, float* r, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float mono = r != nullptr ? 0.5f * (l[i] + r[i]) : l[i];
            const float d = std::abs (detector.processSample (mono));
            env = d > env ? d + att * (env - d) : d + rel * (env - d);
            const float a = std::abs (mono);
            envFull = a > envFull ? a + att * (envFull - a) : a + rel * (envFull - a);

            // v3.2: detección RELATIVA. Sólo hay "s" cuando la banda sibilante se acerca al nivel
            // de toda la voz (vocales: -20..-35 dB; sibilantes: -2..-8 dB). Antes, con umbral
            // absoluto, recortaba los agudos de forma continua y apagaba la voz.
            const float sib = toDb (env) - toDb (envFull + 1.0e-9f);
            const float w = juce::jlimit (0.0f, 1.0f, (sib + 14.0f) / 8.0f);
            const float dominance = w * w * (3.0f - 2.0f * w);
            const float over = toDb (env) - threshold;
            const float target = over > 0.0f ? juce::jmin (over * 0.8f, 18.0f) * dominance : 0.0f;
            gr = target > gr ? target + att * (gr - target) : target + rel * (gr - target);
            const float g = fromDb (-gr);

            float lo, hi;
            crossover.processSample (0, l[i], lo, hi);
            l[i] = lo + hi * g;
            if (r != nullptr)
            {
                crossover.processSample (1, r[i], lo, hi);
                r[i] = lo + hi * g;
            }
        }
    }

    float getGainReductionDb() const { return gr; }

private:
    double fs = 44100.0;
    juce::dsp::LinkwitzRileyFilter<float> crossover;
    juce::dsp::IIR::Filter<float> detector;
    float threshold = -28.0f, att = 0, rel = 0, env = 0, envFull = 0, gr = 0, lastFreq = -1.0f;
};
// ----------------------------------------------------------------------------
// EQ dinámica de dureza (~3.2 kHz, Q 2): sólo recorta cuando la voz "pica"
// y = x - (1 - g) * bandpass(x)  -> campana de corte dinámica sin desfase en el centro
// ----------------------------------------------------------------------------
class HarshTamer
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        auto c = juce::dsp::IIR::Coefficients<float>::makeBandPass (fs, 3200.0f, 2.0f);
        for (auto& f : bp) { f.coefficients = c; f.reset(); }
        detector.coefficients = c;
        detector.reset();
        att = msToCoef (fs, 2.0f);
        rel = msToCoef (fs, 90.0f);
        env = 0; cut = 0;
    }

    // maxCutDb: reducción máxima (0 = off). Umbral fijo relativo de -30 dBFS en la banda.
    void process (float* l, float* r, int n, float maxCutDb)
    {
        for (int i = 0; i < n; ++i)
        {
            const float mono = r != nullptr ? 0.5f * (l[i] + r[i]) : l[i];
            const float d = std::abs (detector.processSample (mono));
            env = d > env ? d + att * (env - d) : d + rel * (env - d);
            const float over = toDb (env) + 30.0f;
            const float target = over > 0.0f ? juce::jmin (maxCutDb, over * 0.5f) : 0.0f;
            cut = target > cut ? target + att * (cut - target) : target + rel * (cut - target);
            const float k = 1.0f - fromDb (-cut);
            l[i] -= k * bp[0].processSample (l[i]);
            if (r != nullptr) r[i] -= k * bp[1].processSample (r[i]);
        }
    }

private:
    double fs = 44100.0;
    juce::dsp::IIR::Filter<float> bp[2], detector;
    float att = 0, rel = 0, env = 0, cut = 0;
};
} // namespace pv
