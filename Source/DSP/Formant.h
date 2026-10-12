#pragma once
#include <juce_dsp/juce_dsp.h>
#include <complex>
#include <vector>

// ============================================================================
//  FormantShifter — desplaza la envolvente espectral (formantes) SIN mover la
//  fundamental. STFT 1024 / hop 256 (75 % solape), envolvente por cepstrum
//  liftrado, warping de la envolvente y re-síntesis por overlap-add.
//
//  Latencia fija = FFT size (se reporta al host en modos MIX / RENDER HQ).
//  Con shift = 0 el camino es identidad (ventana² con solape normalizado),
//  así la latencia no cambia al activar/desactivar el formant.
// ============================================================================
class FormantShifter
{
public:
    static constexpr int order = 10, N = 1 << order, hop = N / 4;
    static int latencySamples() { return N; }

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        window.resize (N);
        for (int i = 0; i < N; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N);
        // normalización OLA para ventana Hann² a 75 % de solape (suma = 1.5)
        olaNorm = 1.0f / 1.5f;

        // corte del lifter: ~1 ms de quefrencia (resolución de formantes, ignora armónicos)
        lifter = juce::jlimit (12, N / 8, (int) (fs / 1000.0));

        for (auto& c : ch)
        {
            c.in.assign ((size_t) N, 0.0f);
            c.acc.assign ((size_t) N, 0.0f);
            c.out.assign ((size_t) hop, 0.0f);
        }
        count = 0;
        buf.resize (N); spec.resize (N); cep.resize (N); envC.resize (N);
        env.resize (N / 2 + 1); newEnv.resize (N / 2 + 1);
        shiftSm.reset (fs / hop, 0.015);
        shiftSm.setCurrentAndTargetValue (0.0f);
    }

    void setShift (float semitones) { shiftSm.setTargetValue (juce::jlimit (-12.0f, 12.0f, semitones)); }

    void process (float* l, float* r, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            ch[0].in[(size_t) (N - hop + count)] = l[i];
            ch[1].in[(size_t) (N - hop + count)] = r != nullptr ? r[i] : 0.0f;
            l[i] = ch[0].out[(size_t) count];
            if (r != nullptr) r[i] = ch[1].out[(size_t) count];

            if (++count == hop)
            {
                count = 0;
                processFrame (shiftSm.getNextValue());
            }
        }
    }

    void reset()
    {
        for (auto& c : ch)
        {
            std::fill (c.in.begin(), c.in.end(), 0.0f);
            std::fill (c.acc.begin(), c.acc.end(), 0.0f);
            std::fill (c.out.begin(), c.out.end(), 0.0f);
        }
        count = 0;
    }

private:
    struct Channel { std::vector<float> in, acc, out; };

    // Los dos canales en UNA FFT compleja (L = parte real, R = imaginaria). La corrección
    // de envolvente es real y simétrica, así que se aplica al espectro empaquetado y la IFFT
    // devuelve L' en la parte real y R' en la imaginaria: 4 FFT por salto en estéreo (antes 8).
    // La envolvente se estima sobre el canal medio (L+R)/2 -> mismo timbre en ambos lados.
    void processFrame (float semitones)
    {
        auto& L = ch[0];
        auto& R = ch[1];
        if (std::abs (semitones) < 0.01f)
        {
            for (int i = 0; i < N; ++i)
            {
                const float w2 = window[(size_t) i] * window[(size_t) i] * olaNorm;
                L.acc[(size_t) i] += L.in[(size_t) i] * w2;
                R.acc[(size_t) i] += R.in[(size_t) i] * w2;
            }
        }
        else
        {
            for (int i = 0; i < N; ++i)
                buf[(size_t) i] = { L.in[(size_t) i] * window[(size_t) i], R.in[(size_t) i] * window[(size_t) i] };
            fft.perform (buf.data(), spec.data(), false);

            // espectro del canal medio a partir del empaquetado: M_k = (X_k + conj X_{N-k}) / 4 + (X_k - conj X_{N-k}) / (4j)
            for (int k = 0; k < N; ++k)
            {
                const auto xk = spec[(size_t) k];
                const auto xn = std::conj (spec[(size_t) ((N - k) % N)]);
                const auto lk = 0.5f * (xk + xn);
                const auto rk = std::complex<float> (0.0f, -0.5f) * (xk - xn);
                buf[(size_t) k] = { std::log (std::abs (0.5f * (lk + rk)) + 1.0e-9f), 0.0f };
            }
            fft.perform (buf.data(), cep.data(), true);   // JUCE: inverse ya escala 1/N

            for (int q = 0; q < N; ++q)
            {
                const bool keep = q <= lifter || q >= N - lifter;
                envC[(size_t) q] = keep ? std::complex<float> (cep[(size_t) q].real(), 0.0f) : std::complex<float> (0.0f, 0.0f);
            }
            fft.perform (envC.data(), buf.data(), false);
            for (int k = 0; k <= N / 2; ++k)
                env[(size_t) k] = std::exp (buf[(size_t) k].real());

            // warping de la envolvente: formante a f*factor
            const float factor = std::exp2 (semitones / 12.0f);
            for (int k = 0; k <= N / 2; ++k)
            {
                const float src = (float) k / factor;
                const int i0 = (int) src;
                const float t = src - (float) i0;
                float e;
                if (i0 >= N / 2) e = env[(size_t) (N / 2)] * 0.25f;
                else             e = env[(size_t) i0] + t * (env[(size_t) i0 + 1] - env[(size_t) i0]);
                newEnv[(size_t) k] = juce::jlimit (0.0f, 12.0f, e / (env[(size_t) k] + 1.0e-9f));
            }
            for (int k = 0; k < N; ++k)
                spec[(size_t) k] *= newEnv[(size_t) (k <= N / 2 ? k : N - k)];
            fft.perform (spec.data(), buf.data(), true);
            for (int i = 0; i < N; ++i)
            {
                const float w = window[(size_t) i] * olaNorm;
                L.acc[(size_t) i] += buf[(size_t) i].real() * w;
                R.acc[(size_t) i] += buf[(size_t) i].imag() * w;
            }
        }

        for (auto* c : { &L, &R })
        {
            for (int i = 0; i < hop; ++i)
                c->out[(size_t) i] = c->acc[(size_t) i];
            std::move (c->acc.begin() + hop, c->acc.end(), c->acc.begin());
            std::fill (c->acc.end() - hop, c->acc.end(), 0.0f);
            std::move (c->in.begin() + hop, c->in.end(), c->in.begin());
            std::fill (c->in.end() - hop, c->in.end(), 0.0f);
        }
    }

    double fs = 48000.0;
    juce::dsp::FFT fft { order };
    std::vector<float> window, env, newEnv;
    std::vector<std::complex<float>> buf, spec, cep, envC;
    std::array<Channel, 2> ch;
    int count = 0, lifter = 48;
    float olaNorm = 1.0f / 1.5f;
    juce::SmoothedValue<float> shiftSm;
};
