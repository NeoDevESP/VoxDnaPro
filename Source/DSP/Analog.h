#pragma once
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include "Dynamics.h"
#include "Circuits.h"

// ============================================================================
//  DNA ANALOG — emulaciones "inspiradas por topología" (blueprint cap. 06).
//  No se declaran modelos 1:1 de hardware; son algoritmos genéricos con el
//  comportamiento característico de cada familia.
// ============================================================================
namespace pv
{
// ----------------------------------------------------------------------------
// OPTICAL LEVELER (v2.3): modelo de célula T4 (panel electroluminiscente +
// fotorresistencia), topología de realimentación como el LA-2A.
//  · El panel EL sigue a la salida (ataque ~10 ms, caída ~30 ms).
//  · La fotorresistencia tiene dos componentes: rápida (10 ms / 60 ms) y lenta
//    (se carga en ~0.8 s y se descarga en ~2.5 s) -> memoria: tras una
//    compresión larga, el release es mucho más lento que tras un pico breve.
//  · Ganancia = 1 / (1 + K * G): rodilla suave y ratio ~2-3:1 de forma natural.
// ----------------------------------------------------------------------------
class OptoLeveler
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        auto c = [this] (float ms) { return 1.0f - std::exp (-1.0f / (ms * 0.001f * (float) fs)); };
        elAtt = c (10.0f);  elRel = c (30.0f);
        f1Att = c (10.0f);  f1Rel = c (60.0f);
        f2Att = c (800.0f); f2Rel = c (2500.0f);
        el = g1 = g2 = 0.0f; lastGain = 1.0f; gr = 0.0f;
        makeupSm.reset (fs, 0.05);
        makeupSm.setCurrentAndTargetValue (makeup);
    }

    // amount 0..100 -> umbral 0 .. -36 dB ; gainDb = make-up
    void setParameters (float amount0to100, float gainDb)
    {
        thresholdLin = fromDb (-6.0f - amount0to100 * 0.30f);
        makeup = gainDb;
        makeupSm.setTargetValue (gainDb);
    }

    float process (float level)
    {
        const float outLevel = level * lastGain;   // realimentación: el detector ve la salida
        const float target = juce::jlimit (0.0f, 8.0f, outLevel / thresholdLin - 1.0f);
        el += (target - el) * (target > el ? elAtt : elRel);
        g1 += (el - g1) * (el > g1 ? f1Att : f1Rel);
        g2 += (el - g2) * (el > g2 ? f2Att : f2Rel);
        lastGain = 1.0f / (1.0f + 1.5f * (0.7f * g1 + 0.3f * g2));
        gr = -toDb (lastGain);
        return lastGain * fromDb (makeupSm.getNextValue());
    }

    float getGainReductionDb() const { return gr; }

private:
    double fs = 44100.0;
    juce::SmoothedValue<float> makeupSm;
    float thresholdLin = 0.1f, makeup = 0.0f;
    float elAtt = 0, elRel = 0, f1Att = 0, f1Rel = 0, f2Att = 0, f2Rel = 0;
    float el = 0, g1 = 0, g2 = 0, lastGain = 1.0f, gr = 0.0f;
};

// ----------------------------------------------------------------------------
// EXCITER: armónicos generados sólo en la banda alta (> 3 kHz) y sumados en
// paralelo — "aire" sin subir sibilancias graves.
// ----------------------------------------------------------------------------
class Exciter
{
public:
    void prepare (double sampleRate)
    {
        auto c = juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, 3000.0f, 0.7071f);
        for (auto& f : hp1) { f.coefficients = c; f.reset(); }
        for (auto& f : hp2) { f.coefficients = c; f.reset(); }
    }

    void process (float* l, float* r, int n, float amount01)
    {
        if (amount01 <= 0.001f) return;
        const float a = amount01 * 0.35f;
        for (int i = 0; i < n; ++i)
        {
            l[i] += a * hp2[0].processSample (std::tanh (4.0f * hp1[0].processSample (l[i])) * 0.5f);
            if (r != nullptr)
                r[i] += a * hp2[1].processSample (std::tanh (4.0f * hp1[1].processSample (r[i])) * 0.5f);
        }
    }

private:
    juce::dsp::IIR::Filter<float> hp1[2], hp2[2];
};

// ----------------------------------------------------------------------------
// COLOR: 5 topologías de saturación, funciona en el dominio sobremuestreado
// ----------------------------------------------------------------------------
enum class ColorMode { tube = 0, tape, preampA, console, rageClip };

inline const juce::StringArray& colorModeNames()
{
    static const juce::StringArray n { "Tube Drive", "Tape Colour", "Preamp Class A", "Console Bus", "Rage Clip" };
    return n;
}

// v2.3: Tube = triodo 12AX7 (Koren) · Tape = histéresis Jiles-Atherton + pérdida HF ·
// Preamp A = clipper de diodos asimétricos en WDF · Console = diodos suaves en WDF ·
// Rage Clip = diseño propio (recorte multibanda 300 Hz-6 kHz).
// Cada modelo escala la entrada con el drive y normaliza la salida por su ganancia de
// pequeña señal: 0 dB con señal débil, distorsión creciente con el nivel.
class ColorStage
{
public:
    // prepare: en prepareToPlay (calcula tablas y calibraciones). reset: sin asignaciones.
    void prepare (double rate)
    {
        osRate = rate;
        circuits::DiodeClipperWDF::Params pre;          // preamp clase A: diodos asimétricos -> pares
        pre.rs = 2200.0; pre.c = 3.3e-9;
        pre.isPos = 2.52e-9; pre.nVtPos = 1.752 * 25.85e-3;
        pre.isNeg = 1.0e-7;  pre.nVtNeg = 2.1 * 25.85e-3;
        circuits::DiodeClipperWDF::Params con;          // consola: simétricos, más suaves
        con.rs = 10000.0; con.c = 5.0e-10;
        con.isPos = con.isNeg = 1.0e-6; con.nVtPos = con.nVtNeg = 2.0 * 25.85e-3;
        for (auto& c : chans)
        {
            c.preamp.prepare (rate, pre);
            c.console.prepare (rate, con);
            c.tape.prepare();
        }
        triode.prepare();
        reset();
    }

    void reset() noexcept
    {
        for (auto& c : chans)
        {
            c.preamp.reset(); c.console.reset(); c.tape.reset();
            c.lp = c.hp = c.bump = 0.0f;
        }
    }

    // x1 puede ser nullptr (mono). mixSm se avanza una vez por muestra.
    void process (float* x0, float* x1, int n, ColorMode mode, float drive01, juce::SmoothedValue<float>& mixSm)
    {
        Coefs k;
        k.tapeLp = onePole ((double) (18000.0f - 9000.0f * drive01));
        k.bpHp = onePole (300.0); k.bpLp = onePole (6000.0);
        k.tubeIn = 0.15 * std::pow (10.0, drive01 * 1.9);              // 0.15 .. 12 V en la rejilla
        k.tubeOut = 1.0 / (triode.getSmallSignalGain() * k.tubeIn);     // incluye la inversión de fase
        k.tapeIn = 2000.0 * std::pow (10.0, drive01 * 1.6);             // 2e3 .. 8e4 A/m
        k.tapeOut = 1.0 / (chans[0].tape.getSmallSignalSusceptibility() * k.tapeIn);
        k.preIn = 0.05 * std::pow (10.0, drive01 * 1.8);                // 0.05 .. 3.2 V
        k.conIn = 0.02 * std::pow (10.0, drive01 * 1.2);                // 0.02 .. 0.32 V (sutil)
        k.preOut = 1.0 / (k.preIn * chans[0].preamp.getSmallSignalGain());
        k.conOut = 1.0 / (k.conIn * chans[0].console.getSmallSignalGain());
        k.drive01 = drive01;
        for (int i = 0; i < n; ++i)
        {
            const float mix = mixSm.getNextValue();
            if (mix <= 0.0f) continue;
            x0[i] = shape (x0[i], chans[0], mode, k, mix);
            if (x1 != nullptr)
                x1[i] = shape (x1[i], chans[1], mode, k, mix);
        }
    }

private:
    struct Chan
    {
        circuits::DiodeClipperWDF preamp, console;
        circuits::TapeHysteresis tape;
        float lp = 0, hp = 0, bump = 0;
    };
    struct Coefs
    {
        float tapeLp = 0, bpHp = 0, bpLp = 0, drive01 = 0;
        double tubeIn = 1, tubeOut = 1, tapeIn = 1, tapeOut = 1, preIn = 1, preOut = 1, conIn = 1, conOut = 1;
    };

    float shape (float in, Chan& c, ColorMode mode, const Coefs& k, float mix) noexcept
    {
        float y = in;
        switch (mode)
        {
            case ColorMode::tube:
                y = (float) (triode.process ((double) in * k.tubeIn) * k.tubeOut);
                break;
            case ColorMode::tape:
            {
                y = (float) (c.tape.process ((double) in * k.tapeIn) * k.tapeOut);
                c.lp = y + k.tapeLp * (c.lp - y);   // pérdida de agudos dependiente del nivel de grabación
                y = c.lp;
                break;
            }
            case ColorMode::preampA:
                y = (float) (c.preamp.process ((double) in * k.preIn) * k.preOut);
                break;
            case ColorMode::console:
                y = (float) (c.console.process ((double) in * k.conIn) * k.conOut);
                break;
            case ColorMode::rageClip:
            {   // multibanda: sólo 300 Hz - 6 kHz se recorta
                c.hp = in + k.bpHp * (c.hp - in);
                const float high = in - c.hp;
                c.lp = high + k.bpLp * (c.lp - high);
                const float band = c.lp;
                const float kk = 1.0f + k.drive01 * 20.0f;
                const float clipped = juce::jlimit (-0.5f, 0.5f, kk * band) / std::sqrt (kk);
                y = in - band + clipped * 1.4f;
                break;
            }
        }
        return in + mix * (y - in);
    }

    float onePole (double hz) const { return std::exp (-2.0f * juce::MathConstants<float>::pi * (float) hz / (float) osRate); }
    std::array<Chan, 2> chans;
    circuits::TriodeStage triode;   // estático (tabla): se comparte entre canales
    double osRate = 88200.0;
};

} // namespace pv
