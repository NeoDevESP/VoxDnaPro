#pragma once
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>
#include <vector>

namespace pv
{
// Saturación tipo cinta/válvula: ganancia unidad en señal baja, armónicos pares por bias
inline float saturate (float x, float k, float bias)
{
    const float tb = std::tanh (bias);
    return (std::tanh (k * x + bias) - tb) / (k * (1.0f - tb * tb));
}

inline float onePoleCoef (double fs, double hz)
{
    return std::exp (-2.0f * juce::MathConstants<float>::pi * (float) hz / (float) fs);
}

// Línea de retardo circular con interpolación lineal
class DelayBuffer
{
public:
    void prepare (int maxSamples)
    {
        const int size = juce::nextPowerOfTwo (maxSamples + 4);
        data.assign ((size_t) size, 0.0f);
        mask = size - 1;
        w = 0;
    }
    void clear() { std::fill (data.begin(), data.end(), 0.0f); }
    void push (float x) { data[(size_t) w] = x; w = (w + 1) & mask; }
    // delay en muestras respecto a la última muestra escrita
    float read (float delay) const
    {
        const float pos = (float) (w - 1) - delay;
        const float fl = std::floor (pos);
        const float t = pos - fl;
        const int i = (int) fl;
        const float a = data[(size_t) (i & mask)];
        const float b = data[(size_t) ((i + 1) & mask)];
        return a + t * (b - a);
    }
private:
    std::vector<float> data;
    int mask = 0, w = 0;
};

// ----------------------------------------------------------------------------
// Micro-pitch shifter de ratio fijo (doble cabezal) — base del widening estilo
// "MicroShift": L = -cents con 15 ms, R = +cents con 23 ms
// ----------------------------------------------------------------------------
class MicroShifter
{
public:
    void prepare (double sampleRate, float baseDelayMs, float windowMs = 45.0f)
    {
        fs = sampleRate;
        window = windowMs * 0.001f * (float) fs;
        base = baseDelayMs * 0.001f * (float) fs;
        line.prepare ((int) (base + window + 64));
        phase = 0.0f;
    }
    void clear() { line.clear(); }
    float process (float x, float cents) { return processRatio (x, std::exp2 (cents / 1200.0f)); }
    // mantiene la línea al día aunque la capa esté apagada (sin datos viejos al activarla)
    void feed (float x) { line.push (x); }

    float processRatio (float x, float ratio)
    {
        line.push (x);
        phase += (1.0f - ratio) / window;
        phase -= std::floor (phase);
        const float p2 = phase + 0.5f - std::floor (phase + 0.5f);
        const float g1 = std::sin (juce::MathConstants<float>::pi * phase);
        const float g2 = std::sin (juce::MathConstants<float>::pi * p2);
        // ganancias seno -> potencia constante para señales poco correladas
        return g1 * line.read (base + phase * window) + g2 * line.read (base + p2 * window);
    }
private:
    double fs = 44100.0;
    DelayBuffer line;
    float window = 2000.0f, base = 700.0f, phase = 0.0f;
};

// ----------------------------------------------------------------------------
// MODULATION: Chorus / Flanger / Phaser estéreo (blueprint: 0.15–0.7 Hz, mix 5–20 %)
// ----------------------------------------------------------------------------
class ModFX
{
public:
    enum Mode { chorus = 0, flanger, phaser };
    static const juce::StringArray& modeNames()
    {
        static const juce::StringArray n { "Chorus", "Flanger", "Phaser" };
        return n;
    }

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        dl.prepare ((int) (0.04 * fs));
        dr.prepare ((int) (0.04 * fs));
        reset();
        mixSm.reset (fs, 0.05);
        mixSm.setCurrentAndTargetValue (0.0f);
    }

    void reset()
    {
        dl.clear(); dr.clear();
        fbL = fbR = 0; phase = 0;
        for (auto& a : apL) a = 0;
        for (auto& a : apR) a = 0;
    }

    bool isActive() const { return mixSm.isSmoothing() || mixSm.getCurrentValue() > 0.0f; }

    void process (float* l, float* r, int n, bool on, int mode, float rateHz, float depth01, float mix01)
    {
        mixSm.setTargetValue (on ? mix01 : 0.0f);
        if (! isActive()) return;
        const float inc = juce::MathConstants<float>::twoPi * rateHz / (float) fs;
        for (int i = 0; i < n; ++i)
        {
            const float m = mixSm.getNextValue();
            phase += inc;
            if (phase > juce::MathConstants<float>::twoPi) phase -= juce::MathConstants<float>::twoPi;
            const float sA = std::sin (phase), sB = std::cos (phase);
            float wl = 0, wr = 0;

            if (mode == chorus)
            {
                dl.push (l[i]); dr.push (r[i]);
                const float base = 0.016f * (float) fs, dep = (0.0005f + 0.004f * depth01) * (float) fs;
                wl = dl.read (base + dep * sA);
                wr = dr.read (base * 1.13f + dep * sB);
            }
            else if (mode == flanger)
            {
                dl.push (l[i] + 0.55f * fbL);
                dr.push (r[i] + 0.55f * fbR);
                const float base = 0.0006f * (float) fs, dep = (0.0004f + 0.0035f * depth01) * (float) fs;
                fbL = dl.read (base + dep * (0.5f + 0.5f * sA));
                fbR = dr.read (base + dep * (0.5f + 0.5f * sB));
                wl = fbL; wr = fbR;
            }
            else // phaser: 4 all-pass de 1er orden barridos
            {
                const float fL = 300.0f + 2500.0f * depth01 * (0.5f + 0.5f * sA);
                const float fR = 300.0f + 2500.0f * depth01 * (0.5f + 0.5f * sB);
                wl = allpassChain (l[i] + 0.4f * fbL, apL, fL);
                wr = allpassChain (r[i] + 0.4f * fbR, apR, fR);
                fbL = wl; fbR = wr;
            }
            l[i] += m * wl;
            r[i] += m * wr;
        }
    }

private:
    float allpassChain (float x, float* st, float freq)
    {
        const float t = std::tan (juce::MathConstants<float>::pi * juce::jmin (freq, (float) fs * 0.45f) / (float) fs);
        const float a = (t - 1.0f) / (t + 1.0f);
        for (int k = 0; k < 4; ++k)
        {
            const float y = a * x + st[k];
            st[k] = x - a * y;
            x = y;
        }
        return x;
    }

    double fs = 44100.0;
    DelayBuffer dl, dr;
    float fbL = 0, fbR = 0, phase = 0;
    float apL[4] {}, apR[4] {};
    juce::SmoothedValue<float> mixSm;
};

// ----------------------------------------------------------------------------
// Envolvente de la voz seca para ducking de delay/reverb (sidechain interno)
// devuelve 0..1 (0 = silencio entre frases, 1 = voz presente)
// ----------------------------------------------------------------------------
class DuckDetector
{
public:
    void prepare (double fs) { att = std::exp (-1.0f / (0.005f * (float) fs)); rel = std::exp (-1.0f / (0.25f * (float) fs)); env = 0.0f; }
    float process (float level)
    {
        env = level > env ? level + att * (env - level) : level + rel * (env - level);
        // rodilla: empieza a -42 dBFS, completo a -18 dBFS
        const float db = 20.0f * std::log10 (env + 1.0e-6f);
        return juce::jlimit (0.0f, 1.0f, (db + 42.0f) / 24.0f);
    }
private:
    float att = 0, rel = 0, env = 0;
};

inline float duckGain (float key01, float duckDb)
{
    return 1.0f - (1.0f - std::pow (10.0f, -duckDb * 0.05f)) * key01;
}

// ----------------------------------------------------------------------------
// Delay ping-pong en ENVÍO: lee la voz seca, filtra repeticiones (HPF 250 / LPF 5k)
// y se hace "ducking" mientras hay voz -> throws que aparecen entre frases
// ----------------------------------------------------------------------------
class PingPongDelay
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        lineL.prepare ((int) (2.6 * fs));
        lineR.prepare ((int) (2.6 * fs));
        lpCoef = onePoleCoef (fs, 5000.0);
        hpCoef = onePoleCoef (fs, 250.0);
        reset();
        timeSmoothed.reset (fs, 0.08);
        timeSmoothed.setCurrentAndTargetValue ((float) (0.25 * fs));
        duckSm.reset (fs, 0.02);
        duckSm.setCurrentAndTargetValue (1.0f);
        mixSm.reset (fs, 0.05);
        mixSm.setCurrentAndTargetValue (0.0f);
    }

    void reset() { lineL.clear(); lineR.clear(); fbL = fbR = lpL = lpR = hpL = hpR = 0.0f; }
    bool isActive() const { return mixSm.isSmoothing() || mixSm.getCurrentValue() > 0.0f; }

    void process (const float* dryL, const float* dryR, const float* key, float* outL, float* outR, int n,
                  float timeSeconds, bool pingPong, float feedback01, float mix01, float duckDb)
    {
        timeSmoothed.setTargetValue (juce::jlimit (1.0f, (float) (2.5 * fs), timeSeconds * (float) fs));
        mixSm.setTargetValue (mix01);
        for (int i = 0; i < n; ++i)
        {
            const float in = 0.5f * (dryL[i] + dryR[i]);
            const float d = timeSmoothed.getNextValue();
            if (pingPong)
            {
                lineL.push (in + feedback01 * fbR);
                lineR.push (feedback01 * fbL);
            }
            else // slap: mono centrado
            {
                lineL.push (in + feedback01 * fbL);
                lineR.push (in + feedback01 * fbR);
            }
            const float oL = lineL.read (d);
            const float oR = lineR.read (d);

            lpL = oL + lpCoef * (lpL - oL);
            lpR = oR + lpCoef * (lpR - oR);
            hpL = lpL + hpCoef * (hpL - lpL);
            hpR = lpR + hpCoef * (hpR - lpR);
            fbL = lpL - hpL;
            fbR = lpR - hpR;

            duckSm.setTargetValue (duckGain (key[i], duckDb));
            const float g = mixSm.getNextValue() * duckSm.getNextValue();
            outL[i] += g * fbL;
            outR[i] += g * fbR;
        }
    }

private:
    double fs = 44100.0;
    DelayBuffer lineL, lineR;
    float lpCoef = 0, hpCoef = 0, lpL = 0, lpR = 0, hpL = 0, hpR = 0, fbL = 0, fbR = 0;
    juce::SmoothedValue<float> timeSmoothed, duckSm, mixSm;
};

// ----------------------------------------------------------------------------
// Reverb FDN de 8 líneas (Householder) con difusión de entrada (4 all-pass),
// tipos Plate / Hall / Room, RT60 real, pre-delay, envío filtrado
// (HPF 220 Hz / LPF "Tone") y ducking desde la voz seca.
// ----------------------------------------------------------------------------
class FDNReverb
{
public:
    static constexpr int N = 8;
    enum Type { plate = 0, hall, room };
    static const juce::StringArray& typeNames()
    {
        static const juce::StringArray n { "Plate", "Hall", "Room" };
        return n;
    }

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (int i = 0; i < N; ++i)
            lines[i].prepare ((int) (baseMs[i] * 0.001 * 1.8 * fs + 64));
        static const float apMs[4] { 4.7f, 3.6f, 12.7f, 9.3f };
        for (int i = 0; i < 4; ++i)
        {
            apLen[i] = apMs[i] * 0.001f * (float) fs;
            ap[i].prepare ((int) (apLen[i] * 1.8f + 8));
        }
        pre.prepare ((int) (0.2 * fs));
        inHp = onePoleCoef (fs, 220.0);
        duckSm.reset (fs, 0.02);
        duckSm.setCurrentAndTargetValue (1.0f);
        mixSm.reset (fs, 0.05);
        mixSm.setCurrentAndTargetValue (0.0f);
        currentType = -1;
        reset();
    }

    bool isActive() const { return mixSm.isSmoothing() || mixSm.getCurrentValue() > 0.0f; }

    void reset()
    {
        for (auto& l : lines) l.clear();
        for (auto& a : ap) a.clear();
        pre.clear();
        for (auto& v : lp) v = 0.0f;
        hpState = lpState = 0.0f;
        lastDecay = lastTone = -1.0f;
    }

    void process (const float* dryL, const float* dryR, const float* key, float* outL, float* outR, int n,
                  int type, float decaySec, float predelayMs, float toneHz, float mix01, float duckDb)
    {
        if (type != currentType)
        {
            currentType = type;
            const float scale = type == hall ? 1.65f : type == room ? 0.55f : 1.0f;
            diffusion = type == plate ? 0.72f : type == hall ? 0.62f : 0.5f;
            for (int i = 0; i < N; ++i) len[i] = baseMs[i] * 0.001f * scale * (float) fs;
            for (auto& l : lines) l.clear();
            lastDecay = -1.0f;
        }
        if (std::abs (decaySec - lastDecay) > 1.0e-4f || std::abs (toneHz - lastTone) > 0.5f)
        {
            lastDecay = decaySec; lastTone = toneHz;
            for (int i = 0; i < N; ++i)
                gain[i] = std::pow (10.0f, -3.0f * len[i] / ((float) fs * juce::jmax (0.1f, decaySec)));
            inLp = onePoleCoef (fs, toneHz);
            damp = onePoleCoef (fs, juce::jmin ((double) toneHz * 1.4, fs * 0.45));
        }
        const float pd = juce::jmax (1.0f, predelayMs * 0.001f * (float) fs);
        const float modInc = juce::MathConstants<float>::twoPi * 0.7f / (float) fs;
        mixSm.setTargetValue (mix01);

        for (int i = 0; i < n; ++i)
        {
            const float mono = 0.5f * (dryL[i] + dryR[i]);
            pre.push (mono);
            float x = pre.read (pd);
            lpState = x + inLp * (lpState - x);
            hpState = lpState + inHp * (hpState - lpState);
            x = lpState - hpState;

            // difusión de entrada (Schroeder all-pass)
            for (int a = 0; a < 4; ++a)
            {
                const float d = ap[a].read (apLen[a] - 1.0f);
                const float v = x + diffusion * d;
                ap[a].push (v);
                x = d - diffusion * v;
            }

            modPhase += modInc;
            if (modPhase > juce::MathConstants<float>::twoPi) modPhase -= juce::MathConstants<float>::twoPi;
            const float mod = 6.0f * std::sin (modPhase);

            float o[N];
            float sum = 0.0f;
            for (int k = 0; k < N; ++k)
            {
                const float d = len[k] + (k == 0 ? mod : k == 1 ? -mod : 0.0f);
                const float v = lines[k].read (d);
                lp[k] = v + damp * (lp[k] - v);
                o[k] = lp[k] * gain[k];
                sum += o[k];
            }
            const float hh = sum * (2.0f / (float) N);
            for (int k = 0; k < N; ++k)
                lines[k].push (x * 0.35f + (o[k] - hh));

            const float wl = o[0] - o[2] + o[4] - o[6];
            const float wr = o[1] - o[3] + o[5] - o[7];

            duckSm.setTargetValue (duckGain (key[i], duckDb));
            const float g = mixSm.getNextValue() * 0.55f * duckSm.getNextValue();
            outL[i] += g * wl;
            outR[i] += g * wr;
        }
    }

private:
    static constexpr float baseMs[N] { 31.7f, 37.1f, 41.9f, 47.3f, 53.3f, 59.9f, 67.1f, 73.7f };
    double fs = 44100.0;
    DelayBuffer lines[N], ap[4];
    float len[N] {}, gain[N] {}, lp[N] {}, apLen[4] {};
    DelayBuffer pre;
    float inHp = 0, inLp = 0, damp = 0, hpState = 0, lpState = 0, modPhase = 0, diffusion = 0.7f;
    float lastDecay = -1, lastTone = -1;
    int currentType = -1;
    juce::SmoothedValue<float> duckSm, mixSm;
};
} // namespace pv
