#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace pv
{
// ============================================================================
//  LoudnessMeter — ITU-R BS.1770-4 / EBU R128
//   · Ponderación K exacta para cualquier frecuencia de muestreo (biquads en
//     double a partir del prototipo analógico, igual que libebur128)
//   · Momentary (400 ms), Short-term (3 s): ventanas rectangulares deslizantes
//     construidas con sub-bloques de 100 ms
//   · Integrated: bloques de 400 ms con 75 % de solape, gate absoluto -70 LUFS
//     y gate relativo -10 LU (histograma de 0.01 LU, memoria fija)
//  El hilo de audio escribe; la UI sólo lee atómicos. reset() se pide con un
//  flag atómico y se ejecuta en el hilo de audio.
// ============================================================================
class LoudnessMeter
{
public:
    void prepare (double fs)
    {
        // etapa 1: shelving de alta frecuencia
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (juce::MathConstants<double>::pi * f0 / fs);
            const double Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            b1c = { (Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0 };
            a1c = { 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        }
        // etapa 2: paso-alto RLB
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (juce::MathConstants<double>::pi * f0 / fs);
            const double a0 = 1.0 + K / Q + K * K;
            b2c = { 1.0, -2.0, 1.0 };
            a2c = { 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        }
        subBlockLen = juce::jmax (1, (int) std::round (0.1 * fs));
        resetInternal();
    }

    void requestReset() { resetRequested.store (true); }

    void process (const float* l, const float* r, int n)
    {
        if (resetRequested.exchange (false))
            resetIntegrated();

        for (int i = 0; i < n; ++i)
        {
            double e = 0.0;
            const double zl = filter (0, (double) l[i]);
            e += zl * zl;
            if (r != nullptr)
            {
                const double zr = filter (1, (double) r[i]);
                e += zr * zr;
            }
            acc += e;
            if (++accCount >= subBlockLen)
                finishSubBlock();
        }
    }

    // valores para la UI (LUFS; -inf -> -99)
    std::atomic<float> momentary { -99.0f }, shortTerm { -99.0f }, integrated { -99.0f };
    // Loudness Range (EBU Tech 3342): distribución de la sonoridad short-term, gate
    // absoluto -70 LUFS y relativo -20 LU; LRA = percentil 95 - percentil 10 (LU)
    std::atomic<float> loudnessRange { 0.0f };

    static float toLufs (double meanSquare) { return meanSquare > 1.0e-20 ? (float) (-0.691 + 10.0 * std::log10 (meanSquare)) : -99.0f; }

private:
    static constexpr int ringSize = 30;                    // 30 x 100 ms = 3 s
    static constexpr float histMin = -70.0f, histStep = 0.01f;
    static constexpr int histBins = 8000;                  // -70 .. +10 LUFS

    double filter (int c, double x)
    {
        auto& s = st[(size_t) c];
        // DF-II transpuesta, etapa 1
        const double y1 = b1c[0] * x + s[0];
        s[0] = b1c[1] * x - a1c[0] * y1 + s[1];
        s[1] = b1c[2] * x - a1c[1] * y1;
        // etapa 2
        const double y2 = b2c[0] * y1 + s[2];
        s[2] = b2c[1] * y1 - a2c[0] * y2 + s[3];
        s[3] = b2c[2] * y1 - a2c[1] * y2;
        return y2;
    }

    void finishSubBlock()
    {
        ring[(size_t) ringPos] = acc / (double) accCount;
        ringPos = (ringPos + 1) % ringSize;
        acc = 0.0;
        accCount = 0;
        if (filled < ringSize) ++filled;

        double m = 0.0, s = 0.0;
        for (int k = 0; k < ringSize; ++k)
        {
            const double v = ring[(size_t) ((ringPos - 1 - k + ringSize * 2) % ringSize)];
            if (k < 4) m += v;
            s += v;
        }
        m /= 4.0;
        s /= (double) ringSize;
        momentary.store (toLufs (m));
        shortTerm.store (toLufs (s));

        // LRA: un valor short-term cada 100 ms, cuando la ventana de 3 s está llena
        if (filled >= ringSize)
        {
            const float ls = toLufs (s);
            if (ls > histMin)
            {
                const int bin = juce::jlimit (0, histBins - 1, (int) ((ls - histMin) / histStep));
                stCount[(size_t) bin] += 1;
                stTotal += 1;
                stEnergy += s;
            }
            if (++lraCounter >= 5)   // 2 veces por segundo es suficiente
            {
                lraCounter = 0;
                computeLra();
            }
        }

        // bloque de gating de 400 ms (cada 100 ms)
        if (++subBlocksSinceReset >= 4)
        {
            const float lb = toLufs (m);
            if (lb > histMin)
            {
                const int bin = juce::jlimit (0, histBins - 1, (int) ((lb - histMin) / histStep));
                histCount[(size_t) bin] += 1;
                histEnergy[(size_t) bin] += m;
                totalCount += 1;
                totalEnergy += m;
            }
            computeIntegrated();
        }
    }

    void computeIntegrated()
    {
        if (totalCount == 0) { integrated.store (-99.0f); return; }
        const float relGate = toLufs (totalEnergy / (double) totalCount) - 10.0f;
        const int firstBin = juce::jlimit (0, histBins, (int) std::ceil ((relGate - histMin) / histStep));
        double e = 0.0; long c = 0;
        for (int b = firstBin; b < histBins; ++b)
        {
            e += histEnergy[(size_t) b];
            c += histCount[(size_t) b];
        }
        integrated.store (c > 0 ? toLufs (e / (double) c) : -99.0f);
    }

    void computeLra()
    {
        if (stTotal == 0) { loudnessRange.store (0.0f); return; }
        const float relGate = toLufs (stEnergy / (double) stTotal) - 20.0f;
        const int first = juce::jlimit (0, histBins, (int) std::ceil ((relGate - histMin) / histStep));
        long n = 0;
        for (int b = first; b < histBins; ++b) n += stCount[(size_t) b];
        if (n == 0) { loudnessRange.store (0.0f); return; }
        const long i10 = (long) std::floor (0.10 * (double) (n - 1)), i95 = (long) std::floor (0.95 * (double) (n - 1));
        long cum = 0;
        float l10 = 0.0f, l95 = 0.0f;
        bool got10 = false;
        for (int b = first; b < histBins; ++b)
        {
            cum += stCount[(size_t) b];
            const float centre = histMin + ((float) b + 0.5f) * histStep;
            if (! got10 && cum > i10) { l10 = centre; got10 = true; }
            if (cum > i95) { l95 = centre; break; }
        }
        loudnessRange.store (juce::jmax (0.0f, l95 - l10));
    }

    void resetIntegrated()
    {
        std::fill (stCount.begin(), stCount.end(), 0);
        stTotal = 0; stEnergy = 0.0; lraCounter = 0;
        loudnessRange.store (0.0f);
        std::fill (histCount.begin(), histCount.end(), 0);
        std::fill (histEnergy.begin(), histEnergy.end(), 0.0);
        totalCount = 0;
        totalEnergy = 0.0;
        subBlocksSinceReset = filled;   // el historial de corto plazo sigue siendo válido
        integrated.store (-99.0f);
    }

    void resetInternal()
    {
        for (auto& s : st) s.fill (0.0);
        ring.fill (0.0);
        ringPos = filled = 0;
        acc = 0.0;
        accCount = 0;
        subBlocksSinceReset = 0;
        resetIntegrated();
        subBlocksSinceReset = 0;
        momentary.store (-99.0f);
        shortTerm.store (-99.0f);
    }

    std::array<double, 3> b1c {}, b2c {};
    std::array<double, 2> a1c {}, a2c {};
    std::array<std::array<double, 4>, 2> st {};
    std::array<double, ringSize> ring {};
    int ringPos = 0, filled = 0, subBlockLen = 4800, accCount = 0, subBlocksSinceReset = 0;
    double acc = 0.0;
    std::array<int, histBins> histCount {};
    std::array<double, histBins> histEnergy {};
    long totalCount = 0;
    double totalEnergy = 0.0;
    std::array<int, histBins> stCount {};
    long stTotal = 0;
    double stEnergy = 0.0;
    int lraCounter = 0;
    std::atomic<bool> resetRequested { false };
};

// ----------------------------------------------------------------------------
// Correlación estéreo (-1 .. +1), ventana ~300 ms — comprobación de mono
// ----------------------------------------------------------------------------
class CorrelationMeter
{
public:
    void prepare (double fs) { coef = std::exp (-1.0f / (0.3f * (float) fs)); lr = ll = rr = 0.0; }

    void process (const float* l, const float* r, int n)
    {
        if (r == nullptr) { value.store (1.0f); return; }
        for (int i = 0; i < n; ++i)
        {
            lr = (double) l[i] * r[i] + coef * (lr - (double) l[i] * r[i]);
            ll = (double) l[i] * l[i] + coef * (ll - (double) l[i] * l[i]);
            rr = (double) r[i] * r[i] + coef * (rr - (double) r[i] * r[i]);
        }
        const double d = std::sqrt (ll * rr);
        value.store (d > 1.0e-10 ? (float) juce::jlimit (-1.0, 1.0, lr / d) : 1.0f);
    }

    std::atomic<float> value { 1.0f };

private:
    float coef = 0.999f;
    double lr = 0, ll = 0, rr = 0;
};

// ----------------------------------------------------------------------------
// SPSC FIFO audio -> UI (juce::AbstractFifo: índices atómicos, sin locks).
// Si la UI no consume a tiempo, el audio descarta muestras (nunca espera).
// ----------------------------------------------------------------------------
template <int Capacity>
class AudioToUiFifo
{
public:
    void push (const float* l, const float* r, int n)
    {
        const auto scope = fifo.write (juce::jmin (n, fifo.getFreeSpace()));
        auto put = [&] (int start, int count, int offset)
        {
            for (int i = 0; i < count; ++i)
            {
                const int k = offset + i;
                data[(size_t) (start + i)] = r != nullptr ? 0.5f * (l[k] + r[k]) : l[k];
            }
        };
        put (scope.startIndex1, scope.blockSize1, 0);
        put (scope.startIndex2, scope.blockSize2, scope.blockSize1);
    }

    // devuelve cuántas muestras se copiaron a dest (máx. maxN)
    int pull (float* dest, int maxN)
    {
        const auto scope = fifo.read (juce::jmin (maxN, fifo.getNumReady()));
        std::copy (data.begin() + scope.startIndex1, data.begin() + scope.startIndex1 + scope.blockSize1, dest);
        std::copy (data.begin() + scope.startIndex2, data.begin() + scope.startIndex2 + scope.blockSize2, dest + scope.blockSize1);
        return scope.blockSize1 + scope.blockSize2;
    }

private:
    juce::AbstractFifo fifo { Capacity };
    std::array<float, Capacity> data {};
};

// ----------------------------------------------------------------------------
// FIFO estéreo audio -> UI (análisis de referencia: anchura, espectro L/R/M/S)
// ----------------------------------------------------------------------------
template <int Capacity>
class StereoUiFifo
{
public:
    void push (const float* l, const float* r, int n)
    {
        const auto scope = fifo.write (juce::jmin (n, fifo.getFreeSpace()));
        auto put = [&] (int start, int count, int offset)
        {
            for (int i = 0; i < count; ++i)
            {
                dl[(size_t) (start + i)] = l[offset + i];
                dr[(size_t) (start + i)] = r != nullptr ? r[offset + i] : l[offset + i];
            }
        };
        put (scope.startIndex1, scope.blockSize1, 0);
        put (scope.startIndex2, scope.blockSize2, scope.blockSize1);
    }

    int pull (float* l, float* r, int maxN)
    {
        const auto scope = fifo.read (juce::jmin (maxN, fifo.getNumReady()));
        auto get = [&] (int start, int count, int offset)
        {
            std::copy (dl.begin() + start, dl.begin() + start + count, l + offset);
            std::copy (dr.begin() + start, dr.begin() + start + count, r + offset);
        };
        get (scope.startIndex1, scope.blockSize1, 0);
        get (scope.startIndex2, scope.blockSize2, scope.blockSize1);
        return scope.blockSize1 + scope.blockSize2;
    }

private:
    juce::AbstractFifo fifo { Capacity };
    std::array<float, Capacity> dl {}, dr {};
};
} // namespace pv
