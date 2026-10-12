#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cmath>

// ============================================================================
//  StepSequencer (v3.0) — 16 pasos sincronizados al tempo del host, 4 carriles:
//   THROW  : empuja la voz al delay en los pasos activos (throws de final de frase)
//   FILTER : filtro paso-bajo sobre toda la voz (barridos rítmicos)
//   GATE   : puerta rítmica de volumen (trance-gate)
//   PAN    : panorama por paso
//  Los valores de cada paso (0..1) los escribe la UI en atómicos; el audio sólo
//  los lee. Todo se suaviza (3-20 ms): sin clicks entre pasos.
//  Con el transporte parado el secuenciador corre libre al tempo del host.
// ============================================================================
namespace pv
{
class StepSequencer
{
public:
    static constexpr int numSteps = 16, numLanes = 4;
    enum Lane { throwLane = 0, filterLane, gateLane, panLane };

    static const juce::StringArray& laneNames()
    {
        static const juce::StringArray n { "THROW", "FILTER", "GATE", "PAN" };
        return n;
    }
    static const juce::StringArray& rateNames()
    {
        static const juce::StringArray n { "1/32", "1/16", "1/8", "1/4" };
        return n;
    }
    static double beatsPerStep (int rateIdx) { static constexpr double b[] { 0.125, 0.25, 0.5, 1.0 }; return b[juce::jlimit (0, 3, rateIdx)]; }

    // valores por defecto: patrones musicales de partida
    StepSequencer()
    {
        for (int s = 0; s < numSteps; ++s)
        {
            steps[throwLane][(size_t) s].store ((s == 7 || s == 15) ? 1.0f : 0.0f);
            steps[filterLane][(size_t) s].store (0.5f + 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) s / 16.0f));
            steps[gateLane][(size_t) s].store ((s % 4 == 3) ? 0.0f : 1.0f);
            steps[panLane][(size_t) s].store (s % 2 == 0 ? 0.25f : 0.75f);
        }
    }

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (auto* sm : { &throwSm, &filterSm, &gateSm, &panSm })
            sm->reset (fs, 0.01);
        throwSm.setCurrentAndTargetValue (0.0f);
        filterSm.setCurrentAndTargetValue (1.0f);
        gateSm.setCurrentAndTargetValue (1.0f);
        panSm.setCurrentAndTargetValue (0.5f);
        gateSm.reset (fs, 0.004);
        filterSm.reset (fs, 0.02);
        for (auto& f : svf) f.fill (0.0f);
        freeBeats = 0.0;
    }

    struct Depths { float throw01 = 0, filter01 = 0, gate01 = 0, pan01 = 0; };

    // ppq: posición del host al inicio del bloque (si está reproduciendo); <0 = parado
    // Escribe throwGain[n] (0..1) para el envío de delay y procesa L/R en sitio.
    void process (float* L, float* R, int n, bool on, int rateIdx, double bpm, double ppq, const Depths& d, float* throwGain)
    {
        const double bps = beatsPerStep (rateIdx);
        const double beatsPerSample = bpm / 60.0 / fs;
        double beat = ppq >= 0.0 ? ppq : freeBeats;
        const float k = 1.41421356f;

        for (int i = 0; i < n; ++i)
        {
            const int step = ((int) std::floor (beat / bps)) % numSteps;
            const int s = (step + numSteps) % numSteps;
            currentStep.store (on ? s : -1);
            const float tv = steps[throwLane][(size_t) s].load(), fv = steps[filterLane][(size_t) s].load();
            const float gv = steps[gateLane][(size_t) s].load(), pv = steps[panLane][(size_t) s].load();

            throwSm.setTargetValue (on ? d.throw01 * tv : 0.0f);
            filterSm.setTargetValue (on ? 1.0f - d.filter01 * (1.0f - fv) : 1.0f);
            gateSm.setTargetValue (on ? 1.0f - d.gate01 * (1.0f - gv) : 1.0f);
            panSm.setTargetValue (on ? 0.5f + d.pan01 * (pv - 0.5f) : 0.5f);

            throwGain[i] = throwSm.getNextValue();
            const float fpos = filterSm.getNextValue();
            const float g = gateSm.getNextValue();
            const float p = panSm.getNextValue();

            float l = L[i], r = R != nullptr ? R[i] : L[i];
            if (fpos < 0.999f || filterSm.isSmoothing())
            {
                // 0..1 -> 200 Hz .. 20 kHz (exponencial)
                const float hz = juce::jmin (200.0f * std::pow (100.0f, fpos), (float) fs * 0.45f);
                const float gg = std::tan (juce::MathConstants<float>::pi * hz / (float) fs);
                l = tick (0, l, gg, k);
                r = tick (1, r, gg, k);
            }
            // panorama de balance (potencia constante, centro = 0 dB)
            const float a = p * juce::MathConstants<float>::halfPi;
            const float pl = std::cos (a) * juce::MathConstants<float>::sqrt2, pr = std::sin (a) * juce::MathConstants<float>::sqrt2;
            L[i] = l * g * juce::jmin (1.0f, pl);
            if (R != nullptr) R[i] = r * g * juce::jmin (1.0f, pr);
            beat += beatsPerSample;
        }
        freeBeats = std::fmod (beat, 64.0);
    }

    bool isActive() const { return throwSm.getCurrentValue() > 0.0f || filterSm.getCurrentValue() < 0.999f
                                   || gateSm.getCurrentValue() < 0.999f || std::abs (panSm.getCurrentValue() - 0.5f) > 1.0e-3f
                                   || throwSm.isSmoothing() || filterSm.isSmoothing() || gateSm.isSmoothing() || panSm.isSmoothing(); }

    std::array<std::array<std::atomic<float>, numSteps>, numLanes> steps;
    std::atomic<int> currentStep { -1 };

private:
    float tick (int c, float x, float g, float k)
    {
        auto& s = svf[(size_t) c];
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float v3 = x - s[1];
        const float v1 = a1 * s[0] + g * a1 * v3;
        const float v2 = s[1] + g * v1;
        s[0] = 2.0f * v1 - s[0];
        s[1] = 2.0f * v2 - s[1];
        return v2;
    }

    double fs = 48000.0, freeBeats = 0.0;
    juce::SmoothedValue<float> throwSm, filterSm, gateSm, panSm;
    std::array<std::array<float, 2>, 2> svf {};
};
} // namespace pv
