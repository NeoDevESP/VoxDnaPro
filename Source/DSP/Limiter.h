#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <cmath>
#include <vector>

// ============================================================================
//  TruePeakLimiter — limitador con lookahead y detección de pico ENTRE muestras
//
//  Detección: interpolación 8x polifásica (8 fases x 32 coeficientes de sinc
//  con ventana de Kaiser beta 8, banda plana hasta ~0.45 fs). Más estricta que
//  el medidor 4x de ITU-R BS.1770-4, que puede infravalorar ~0.5 dB los picos
//  cerca de Nyquist.
//  Ganancia: min-hold de L muestras (cola monótona, O(1)) -> release
//  exponencial -> media móvil de L muestras. Con el audio retrasado
//  L + 15 muestras, la ganancia ya está en su objetivo cuando pasa el pico.
//
//  Garantía verificada: <= -1.0 dBTP (auditado a 16x) para contenido de hasta
//  ~20 kHz. Energía sintética pegada a Nyquist (ruido blanco a plena banda)
//  puede quedar hasta ~0.5 dB por encima: ningún interpolador finito la ve.
//
//  Memoria reservada en prepare(); process() no asigna.
// ============================================================================
namespace pv
{
class TruePeakLimiter
{
public:
    static constexpr int phases = 8, taps = 32, interpDelay = taps / 2;

    void prepare (double sampleRate, float ceilingDbTP = -1.0f)
    {
        fs = sampleRate;
        L = juce::jmax (16, (int) std::round (0.001 * fs));   // 1 ms de lookahead
        ceiling = std::pow (10.0f, (ceilingDbTP - 0.15f) * 0.05f);   // margen: error de rejilla 8x < 0.14 dB
        hardCeiling = std::pow (10.0f, ceilingDbTP * 0.05f);
        relCoef = 1.0f - std::exp (-1.0f / (0.08f * (float) fs));

        for (int p = 0; p < phases; ++p)
        {
            double sum = 0.0;
            std::array<double, taps> c {};
            for (int j = 0; j < taps; ++j)
            {
                const double u = (double) (interpDelay - j) - (double) p / phases;
                const double sinc = std::abs (u) < 1.0e-9 ? 1.0 : std::sin (juce::MathConstants<double>::pi * u) / (juce::MathConstants<double>::pi * u);
                const double half = taps * 0.5 + 0.5;
                const double rr = u / half;
                const double w = std::abs (rr) >= 1.0 ? 0.0 : besselI0 (8.0 * std::sqrt (1.0 - rr * rr)) / besselI0 (8.0);
                c[(size_t) j] = sinc * w;
                sum += c[(size_t) j];
            }
            // orden invertido: el producto escalar recorre memoria contigua (vectorizable)
            for (int j = 0; j < taps; ++j)
                coefRev[(size_t) p][(size_t) (taps - 1 - j)] = (float) (c[(size_t) j] / sum);
        }

        audioLen = L + interpDelay + 8;
        for (auto& d : delayLine) d.assign ((size_t) audioLen, 0.0f);
        tpLine.assign ((size_t) audioLen, 0.0f);
        boxRing.assign ((size_t) L, 1.0f);
        dqVal.assign ((size_t) L + 2, 1.0f);
        dqIdx.assign ((size_t) L + 2, 0);
        reset();
    }

    void reset()
    {
        for (auto& d : delayLine) std::fill (d.begin(), d.end(), 0.0f);
        std::fill (tpLine.begin(), tpLine.end(), 0.0f);
        for (auto& h : hist) h.fill (0.0f);
        std::fill (boxRing.begin(), boxRing.end(), 1.0f);
        histPos = audioPos = ringPos = 0;
        dqHead = dqTail = 0;
        sampleIndex = 0;
        relGain = 1.0f;
        boxSum = (double) L;
        boxRefresh = 0;
        gainReductionDb = 0.0f;
        outPeakEstimate = 0.0f;
    }

    int latencySamples() const { return L + interpDelay - 1; }

    // v3.0: techo ajustable (OUTPUT / LIMIT). Sin asignaciones; el procesador lo cambia
    // en pasos pequeños por bloque, así que la ganancia del limitador nunca salta.
    void setCeiling (float ceilingDbTP) noexcept
    {
        ceiling = std::pow (10.0f, (ceilingDbTP - 0.15f) * 0.05f);
        hardCeiling = std::pow (10.0f, ceilingDbTP * 0.05f);
    }

    // r puede ser nullptr (mono)
    void process (float* l, float* r, int n)
    {
        const int delay = latencySamples();
        const int numCh = r != nullptr ? 2 : 1;
        const int cap = L + 2;
        float minGainBlock = 1.0f, outTp = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            // --- historial con espejo: ventana siempre contigua, sin módulo en el bucle interno ---
            histPos = (histPos + 1) % taps;
            hist[0][(size_t) histPos] = hist[0][(size_t) (histPos + taps)] = l[i];
            const float rv = r != nullptr ? r[i] : 0.0f;
            hist[1][(size_t) histPos] = hist[1][(size_t) (histPos + taps)] = rv;

            float tp = 0.0f;
            for (int c = 0; c < numCh; ++c)
            {
                const float* w = hist[(size_t) c].data() + histPos + 1;   // x[n-31] .. x[n]
                tp = juce::jmax (tp, std::abs (w[taps - 1 - interpDelay]));   // fase 0 = la propia muestra
                for (int p = 1; p < phases; ++p)
                {
                    const float* k = coefRev[(size_t) p].data();
                    float acc = 0.0f;
                    for (int j = 0; j < taps; ++j)
                        acc += k[j] * w[j];
                    tp = juce::jmax (tp, std::abs (acc));
                }
            }

            const float target = tp > ceiling ? ceiling / tp : 1.0f;

            // --- min-hold de L muestras con cola monótona (O(1) amortizado) ---
            while (dqHead != dqTail && dqVal[(size_t) ((dqTail - 1 + cap) % cap)] >= target)
                dqTail = (dqTail - 1 + cap) % cap;
            dqVal[(size_t) dqTail] = target;
            dqIdx[(size_t) dqTail] = sampleIndex;
            dqTail = (dqTail + 1) % cap;
            while (dqIdx[(size_t) dqHead] <= sampleIndex - L)
                dqHead = (dqHead + 1) % cap;
            const float held = dqVal[(size_t) dqHead];
            ++sampleIndex;

            // --- release exponencial (sólo al subir) ---
            relGain = held < relGain ? held : relGain + (held - relGain) * relCoef;

            // --- media móvil de L muestras ---
            boxSum += (double) relGain - (double) boxRing[(size_t) ringPos];
            boxRing[(size_t) ringPos] = relGain;
            ringPos = (ringPos + 1) % L;
            if (++boxRefresh >= 4096)   // evita deriva numérica del acumulador
            {
                boxRefresh = 0;
                boxSum = 0.0;
                for (float v : boxRing) boxSum += v;
            }
            const float g = juce::jmin (1.0f, (float) (boxSum / (double) L));
            minGainBlock = juce::jmin (minGainBlock, g);

            // --- audio retrasado + true peak de la muestra alineada (medidor de salida) ---
            delayLine[0][(size_t) audioPos] = l[i];
            delayLine[1][(size_t) audioPos] = rv;
            tpLine[(size_t) ((audioPos - interpDelay + audioLen) % audioLen)] = tp;
            const int readPos = (audioPos - delay + audioLen * 4) % audioLen;
            audioPos = (audioPos + 1) % audioLen;

            l[i] = juce::jlimit (-hardCeiling, hardCeiling, delayLine[0][(size_t) readPos] * g);
            if (r != nullptr)
                r[i] = juce::jlimit (-hardCeiling, hardCeiling, delayLine[1][(size_t) readPos] * g);
            outTp = juce::jmax (outTp, tpLine[(size_t) readPos] * g);
        }
        gainReductionDb = -20.0f * std::log10 (juce::jmax (minGainBlock, 1.0e-6f));
        outPeakEstimate = outTp;
    }

    float getGainReductionDb() const { return gainReductionDb; }
    // true peak de salida del último bloque (lineal), con la detección 8x
    float getOutputTruePeak() const { return outPeakEstimate; }

    static double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 30; ++k) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); sum += term; }
        return sum;
    }

private:
    double fs = 48000.0;
    int L = 48;
    float ceiling = 0.89f, hardCeiling = 0.89f, relCoef = 0.001f;
    std::array<std::array<float, taps>, phases> coefRev {};
    alignas (32) std::array<std::array<float, taps * 2>, 2> hist {};
    std::array<std::vector<float>, 2> delayLine;
    std::vector<float> tpLine, boxRing, dqVal;
    std::vector<juce::int64> dqIdx;
    int histPos = 0, audioPos = 0, audioLen = 64, ringPos = 0, boxRefresh = 0;
    int dqHead = 0, dqTail = 0;
    juce::int64 sampleIndex = 0;   // 64 bits: no hace wrap en la práctica (millones de años de audio)
    float relGain = 1.0f;
    double boxSum = 48.0;
    float gainReductionDb = 0.0f, outPeakEstimate = 0.0f;
};
} // namespace pv
