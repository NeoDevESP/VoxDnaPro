#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <vector>
#include <atomic>

namespace pvscales
{
// máscara de 12 bits: bit n = el semitono n (relativo a la tónica) pertenece a la escala
constexpr std::uint16_t bits (std::initializer_list<int> notes)
{
    std::uint16_t m = 0;
    for (int n : notes) m = (std::uint16_t) (m | (1u << n));
    return m;
}

constexpr std::array<std::uint16_t, 9> masks {
    bits ({ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }),   // Chromatic
    bits ({ 0, 2, 4, 5, 7, 9, 11 }),                  // Major
    bits ({ 0, 2, 3, 5, 7, 8, 10 }),                  // Minor
    bits ({ 0, 2, 3, 5, 7, 8, 11 }),                  // Harmonic Minor
    bits ({ 0, 2, 4, 7, 9 }),                         // Pentatonic Major
    bits ({ 0, 3, 5, 7, 10 }),                        // Pentatonic Minor
    bits ({ 0, 1, 3, 5, 7, 8, 10 }),                  // Phrygian
    bits ({ 0, 2, 3, 5, 7, 9, 10 }),                  // Dorian
    bits ({ 0, 3, 5, 6, 7, 10 }) };                   // Blues

static_assert (masks[0] == 0x0FFF && masks[1] == 0x0AB5 && masks[2] == 0x05AD, "tabla de escalas");
} // namespace pvscales

// ============================================================================
//  PitchCorrector (v2.2 Tune Edition)
//  - Detección YIN sobre señal decimada (~11 kHz) + clasificación de voz:
//    aperiodicidad YIN + proporción de energía > 4 kHz (fricativas/sibilantes)
//  - Protección de errores de octava: continuidad con la F0 previa, confirmación
//    de saltos grandes y mediana de 3 estimaciones
//  - Consonantes: en tramos no sonoros la salida pasa a la señal seca alineada
//    (mismo retardo D) y el cabezal se recentra -> s/sh/t/k sin artefactos
//  - Transition: deslizamiento entre notas independiente del retune
//  - Flex-Tune: sólo corrige cuando la voz está cerca de la nota de la escala
//  - Compensación de formantes: expone la corrección aplicada para que el
//    FormantShifter la deshaga (pitch corregido, timbre intacto)
//  - Shifter de doble cabezal síncrono al periodo, centrado en un retardo FIJO
// ============================================================================
class PitchCorrector
{
public:
    static constexpr int numScales = 9;

    static const juce::StringArray& scaleNames()
    {
        static const juce::StringArray names { "Chromatic", "Major", "Minor", "Harmonic Minor",
                                               "Pentatonic Maj", "Pentatonic Min", "Phrygian", "Dorian", "Blues" };
        return names;
    }

    static const juce::StringArray& keyNames()
    {
        static const juce::StringArray names { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return names;
    }

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        decim = juce::jmax (1, (int) std::round (fs / 11025.0));
        dfs = fs / decim;
        maxLag = (int) std::ceil (dfs / 60.0);
        minLag = juce::jmax (2, (int) std::floor (dfs / 1200.0));
        win = juce::jmax (256, 2 * maxLag);
        histSize = win + maxLag + 2;
        hist.assign ((size_t) histSize, 0.0f);
        frame.assign ((size_t) histSize, 0.0f);
        diff.assign ((size_t) maxLag + 2, 1.0f);
        histWrite = decimCounter = hopCounter = 0;

        const float aaCut = (float) juce::jmin (dfs * 0.42, 4000.0);
        aa1.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (fs, aaCut, 0.54f);
        aa2.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (fs, aaCut, 1.31f);
        aa1.reset();
        aa2.reset();

        const int bufLen = juce::nextPowerOfTwo ((int) (fs * 0.08) + 64);
        for (auto& l : lines)
            l.assign ((size_t) bufLen, 0.0f);
        mask = bufLen - 1;
        writePos = 0;

        setLowLatency (lowLatency);

        correction = targetCorrection = 0.0f;
        smoothMidi = -1.0f;
        targetNote = -1;
        voiced = false;
        unvoicedSamples = 0;
        lenCoef = 1.0f - std::exp (-1.0f / (float) (0.03 * fs));

        mix.reset (fs, 0.03);
        mix.setCurrentAndTargetValue (0.0f);

        // seguidores de energía total y > 4 kHz para distinguir sibilantes / fricativas
        hfLpCoef = std::exp (-2.0f * juce::MathConstants<float>::pi * 4000.0f / (float) fs);
        envCoef = std::exp (-1.0f / (0.01f * (float) fs));
        hfLp = hfEnv = fullEnv = 0.0f;
        hopsWithoutVoice = 1000;
        wetGain = 0.0f;
        wetAttack = 1.0f - std::exp (-1.0f / (0.003f * (float) fs));   // a seco en ~3 ms
        wetRelease = 1.0f - std::exp (-1.0f / (0.008f * (float) fs));  // vuelta a corregido en ~8 ms
        unvoicedHops = 0;
        jumpConfirm = 0;
        prevFreq = -1.0f;
        midiHist.fill (-1.0f);
        targetGlide = -1.0f;
        reference = -1.0f;
        flexWeight = 1.0f;
        appliedCorrection.store (0.0f);
    }

    // retuneMs: tiempo en llegar a la nota (como el Retune Speed de Auto-Tune, 0 = hard tune)
    // humanize: en notas sostenidas relaja la corrección para conservar el vibrato natural
    // transitionMs: tiempo de deslizamiento entre notas (0 = salto instantáneo)
    // flex0to100: 0 = corrige siempre; 100 = sólo corrige desviaciones muy pequeñas
    void setExpression (float transitionMs, float flex0to100)
    {
        transitionSamples = juce::jmax (0.0f, transitionMs) * 0.001f * (float) fs;
        const float f = juce::jlimit (0.0f, 1.0f, flex0to100 * 0.01f);
        flexWindow = f <= 0.0f ? 1.0e6f : juce::jmap (f, 1.2f, 0.10f);   // ventana en semitonos
    }

    void setParameters (bool enabled, int key, int scale, float retuneMs, float humanize0to100, float amount0to100,
                        float vibratoProtect0to100 = 0.0f, float confidence0to100 = 50.0f)
    {
        vibratoProtect = juce::jlimit (0.0f, 1.0f, vibratoProtect0to100 * 0.01f);
        // Confidence gate: 0 = corrige casi todo (umbral YIN alto), 100 = sólo F0 muy estable
        yinThreshold = juce::jmap (juce::jlimit (0.0f, 100.0f, confidence0to100), 0.0f, 100.0f, 0.30f, 0.06f);
        mix.setTargetValue (enabled ? 1.0f : 0.0f);
        rootKey = juce::jlimit (0, 11, key);
        scaleIndex = juce::jlimit (0, numScales - 1, scale);
        amount = juce::jlimit (0.0f, 1.0f, amount0to100 * 0.01f);
        baseTauMs = juce::jmax (0.4f, retuneMs / 3.0f);   // ~95 % del recorrido en retuneMs
        humanize = juce::jlimit (0.0f, 1.0f, humanize0to100 * 0.01f);
        updateRetuneCoef();
    }

    // ------------------------------------------------------------------
    //  Retardo FIJO y reportable. Los dos cabezales recorren una ventana
    //  [D - W/2, D + W/2] centrada en D, así que el retardo medio es D
    //  con el tune activado, desactivado o corrigiendo.
    //   MONITOR (lowLatency): ventana máx. 11 ms -> D ~ 5.6 ms
    //   MIX / HQ:             ventana máx. 60 ms -> D ~ 30 ms (además alinea la
    //                         corrección con la ventana de análisis, ~ lookahead)
    //  Debe llamarse en silencio (el procesador lo hace durante el fundido).
    // ------------------------------------------------------------------
    static int maxWindowFor (double sampleRate, bool lowLat) { return (int) std::round ((lowLat ? 0.011 : 0.06) * sampleRate); }
    static int centreDelayFor (double sampleRate, bool lowLat) { return guard + maxWindowFor (sampleRate, lowLat) / 2; }

    void setLowLatency (bool shouldBeLowLatency)
    {
        lowLatency = shouldBeLowLatency;
        winMax = (float) maxWindowFor (fs, lowLatency);
        winMin = juce::jmin ((float) std::round (0.008 * fs), winMax);
        centreDelay = (float) centreDelayFor (fs, lowLatency);
        winLen = winTarget = winMax;
        phase[0] = 0.5f;   // cabezal principal en D, ganancia 1
        phase[1] = 0.0f;
        for (auto& l : lines) std::fill (l.begin(), l.end(), 0.0f);
    }

    int latencySamples() const noexcept { return (int) centreDelay; }

    bool isActive() const noexcept { return mix.getCurrentValue() > 0.0f || mix.getTargetValue() > 0.0f; }

    // Procesa en sitio. ch1 puede ser nullptr (mono).
    void process (float* ch0, float* ch1, int numSamples)
    {
        const bool stereo = ch1 != nullptr;

        for (int i = 0; i < numSamples; ++i)
        {
            const float inL = ch0[i];
            const float inR = stereo ? ch1[i] : inL;

            // ---------- detección ----------
            const float mono = 0.5f * (inL + inR);
            const float f = aa2.processSample (aa1.processSample (mono));

            hfLp = mono + hfLpCoef * (hfLp - mono);
            const float hf = mono - hfLp;
            hfEnv = hf * hf + envCoef * (hfEnv - hf * hf);
            fullEnv = mono * mono + envCoef * (fullEnv - mono * mono);

            if (++decimCounter >= decim)
            {
                decimCounter = 0;
                hist[(size_t) histWrite] = f;
                if (++histWrite >= histSize) histWrite = 0;

                if (++hopCounter >= hop)
                {
                    hopCounter = 0;
                    analyse();
                }
            }

            if (! voiced && ++unvoicedSamples > (int) (0.08 * fs))
            {
                targetCorrection = 0.0f;
            }
            else if (targetNote >= 0 && reference > 0.0f)
            {
                // Transition: el objetivo se desliza hacia la nota nueva a velocidad constante
                if (targetGlide < 0.0f || transitionSamples < 1.0f)
                    targetGlide = (float) targetNote;
                else
                {
                    const float step = juce::jmax (1.0f / transitionSamples, std::abs (glideFrom - (float) targetNote) / transitionSamples);
                    targetGlide += juce::jlimit (-step, step, (float) targetNote - targetGlide);
                }
                targetCorrection = (targetGlide - reference) * amount * flexWeight;
            }

            if (noteHoldSamples < (1 << 30)) ++noteHoldSamples;
            correction += (targetCorrection - correction) * retuneCoef;
            winLen += (winTarget - winLen) * lenCoef;

            const float ratio = std::exp2 (correction / 12.0f);
            const float rate = 1.0f - ratio;   // muestras de retardo que se ganan por muestra

            // ---------- shifter de doble cabezal centrado ----------
            lines[0][(size_t) writePos] = inL;
            lines[1][(size_t) writePos] = inR;

            // Consonantes: en tramos no sonoros la salida pasa a la señal seca alineada (retardo D).
            const float wetTarget = unvoicedHops >= 2 ? 0.0f : 1.0f;
            wetGain += (wetTarget - wetGain) * (wetTarget < wetGain ? wetAttack : wetRelease);

            // Re-centrado sin saltos audibles: cuando la salida es 100 % seca (consonante) o tras
            // una pausa más larga que la línea de retardo, el cabezal vuelve a D.
            quietSamples = std::abs (inL) + std::abs (inR) < 2.0e-4f ? quietSamples + 1 : 0;
            // El ataque de cada frase y de cada sílaba tras una consonante sale exactamente con
            // retardo D (= latencia reportada). Durante una nota sostenida corregida el cabezal
            // deriva dentro de +-W/2, inherente a cualquier shifter por línea de retardo.
            if ((wetGain < 0.002f || quietSamples > (int) (centreDelay + 0.5f * winMax) + guard) && phase[0] != 0.5f)
                phase[0] = 0.5f;
            if (quietSamples > (int) (centreDelay + 0.5f * winMax) + guard)
                correction = 0.0f;

            phase[0] += rate / winLen;
            phase[0] -= std::floor (phase[0]);
            phase[1] = phase[0] + 0.5f;
            phase[1] -= std::floor (phase[1]);

            const float base = centreDelay - 0.5f * winLen;
            const float d0 = base + phase[0] * winLen;
            const float d1 = base + phase[1] * winLen;
            const float s0 = std::sin (juce::MathConstants<float>::pi * phase[0]);
            const float g0 = s0 * s0, g1 = 1.0f - g0;   // sin² + cos² = 1: potencia y nivel constantes

            const float m = mix.getNextValue() * wetGain;
            const float dryL = read (0, centreDelay);
            const float wetL = g0 * read (0, d0) + g1 * read (0, d1);
            ch0[i] = dryL + m * (wetL - dryL);

            if (stereo)
            {
                const float dryR = read (1, centreDelay);
                const float wetR = g0 * read (1, d0) + g1 * read (1, d1);
                ch1[i] = dryR + m * (wetR - dryR);
            }

            writePos = (writePos + 1) & mask;
        }
        appliedCorrection.store (correction * mix.getCurrentValue() * wetGain);
    }

    // corrección efectivamente aplicada (semitonos) -> compensación de formantes
    float getAppliedCorrection() const noexcept { return appliedCorrection.load(); }
    bool isConsonant() const noexcept { return wetGain < 0.5f && fullEnv > 1.0e-7f; }

    // --- datos para la UI ---
    float getDetectedMidi() const noexcept { return voiced ? smoothMidi : -1.0f; }
    float getTargetMidi() const noexcept   { return voiced ? (float) targetNote : -1.0f; }

private:
    static constexpr int hop = 48;

    // tabla constexpr: cero inicialización perezosa ni asignaciones en el hilo de audio
    bool noteAllowed (int midiNote) const noexcept
    {
        const int rel = ((midiNote - rootKey) % 12 + 12) % 12;
        return (pvscales::masks[(size_t) scaleIndex] >> rel) & 1u;
    }

    int quantize (float midi) const
    {
        const int centre = (int) std::round (midi);
        int best = centre;
        float bestDist = 1.0e9f;
        for (int n = centre - 6; n <= centre + 6; ++n)
        {
            if (! noteAllowed (n)) continue;
            const float d = std::abs ((float) n - midi);
            if (d < bestDist) { bestDist = d; best = n; }
        }
        return best;
    }

    void analyse()
    {
        for (int i = 0; i < histSize; ++i)
            frame[(size_t) i] = hist[(size_t) ((histWrite + i) % histSize)];

        float energy = 0.0f;
        for (int j = 0; j < win; ++j)
            energy += frame[(size_t) j] * frame[(size_t) j];

        if (std::sqrt (energy / (float) win) < 0.0015f)
        {
            setUnvoiced();
            unvoicedHops = 0;   // silencio: no es una consonante, no hace falta pasar a seco
            return;
        }

        // YIN: diferencia normalizada acumulada
        float running = 0.0f;
        diff[0] = 1.0f;
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            float sum = 0.0f;
            const float* a = frame.data();
            const float* b = frame.data() + tau;
            for (int j = 0; j < win; ++j)
            {
                const float d = a[j] - b[j];
                sum += d * d;
            }
            running += sum;
            diff[(size_t) tau] = running > 0.0f ? sum * (float) tau / running : 1.0f;
        }

        int tauEst = -1;
        for (int tau = minLag; tau < maxLag; ++tau)
        {
            if (diff[(size_t) tau] < yinThreshold)
            {
                while (tau + 1 < maxLag && diff[(size_t) tau + 1] < diff[(size_t) tau])
                    ++tau;
                tauEst = tau;
                break;
            }
        }

        // ---- clasificación de voz: periodicidad + energía en agudos ----
        // Sólo una consonante real (aperiódica y con energía dominante > 4 kHz) pasa a seco.
        // Una pérdida breve de detección sin agudos (cambio de nota legato, aire) mantiene
        // la corrección en curso.
        const float hfRatio = fullEnv > 1.0e-12f ? hfEnv / fullEnv : 0.0f;
        if (tauEst < 0 || hfRatio > 0.5f)
        {
            setUnvoiced();
            if (hfRatio > 0.3f) ++unvoicedHops; else unvoicedHops = 0;
            return;
        }

        // ---- protección de octava: alternativas tau*2 / tau/2 cerca de la F0 previa ----
        auto localMin = [this] (int centre) -> int
        {
            int best = -1; float bv = 2.0f;
            for (int t = juce::jmax (minLag, centre - 3); t <= juce::jmin (maxLag - 1, centre + 3); ++t)
                if (diff[(size_t) t] < bv) { bv = diff[(size_t) t]; best = t; }
            return best;
        };
        if (prevFreq > 0.0f)
        {
            const float freq0 = (float) dfs / (float) tauEst;
            const float jump = std::abs (std::log2 (freq0 / prevFreq));
            if (jump > 0.75f)   // salto de más de ~9 semitonos: sospecha de octava
            {
                int bestTau = tauEst; float bestJump = jump;
                for (int cand : { tauEst * 2, tauEst / 2 })
                {
                    const int t = localMin (cand);
                    if (t < 0 || diff[(size_t) t] > juce::jmax (yinThreshold * 2.0f, diff[(size_t) tauEst] + 0.1f)) continue;
                    const float j = std::abs (std::log2 ((float) dfs / (float) t / prevFreq));
                    if (j < bestJump) { bestJump = j; bestTau = t; }
                }
                if (bestTau != tauEst)
                    tauEst = bestTau;
                else if (++jumpConfirm < 3)   // un salto real tiene que confirmarse en 3 análisis
                    return;
            }
            else
                jumpConfirm = 0;
        }
        jumpConfirm = 0;

        const float a = diff[(size_t) tauEst - 1], b = diff[(size_t) tauEst], c = diff[(size_t) tauEst + 1];
        const float denom = a - 2.0f * b + c;
        const float shift = std::abs (denom) > 1.0e-9f ? juce::jlimit (-1.0f, 1.0f, 0.5f * (a - c) / denom) : 0.0f;
        const float freq = (float) dfs / ((float) tauEst + shift);

        if (freq < 60.0f || freq > 1200.0f)
        {
            setUnvoiced();
            return;
        }
        prevFreq = freq;
        unvoicedHops = 0;
        const bool newPhrase = hopsWithoutVoice > 25;   // > ~100 ms sin voz: frase nueva
        hopsWithoutVoice = 0;

        // mediana de las 3 últimas estimaciones
        midiHist[(size_t) midiHistPos] = 69.0f + 12.0f * std::log2 (freq / 440.0f);
        midiHistPos = (midiHistPos + 1) % 3;
        float midi = midiHist[(size_t) ((midiHistPos + 2) % 3)];
        if (midiHist[0] > 0.0f && midiHist[1] > 0.0f && midiHist[2] > 0.0f)
            midi = juce::jmax (juce::jmin (midiHist[0], midiHist[1]), juce::jmin (juce::jmax (midiHist[0], midiHist[1]), midiHist[2]));

        const bool wasVoiced = voiced;
        if (voiced && std::abs (midi - smoothMidi) < 1.0f)
            smoothMidi += 0.5f * (midi - smoothMidi);
        else
            smoothMidi = midi;

        // centro lento (~250 ms) para la protección de vibrato
        if (! voiced || slowMidi < 0.0f || std::abs (smoothMidi - slowMidi) > 1.5f)
            slowMidi = smoothMidi;
        else
            slowMidi += 0.017f * (smoothMidi - slowMidi);

        voiced = true;
        unvoicedSamples = 0;

        // histéresis para que el vibrato no salte de nota
        if (targetNote < 0 || ! noteAllowed (targetNote) || std::abs (smoothMidi - (float) targetNote) > 0.62f)
        {
            const int q = quantize (smoothMidi);
            if (q != targetNote)
            {
                noteHoldSamples = 0;
                glideFrom = targetGlide < 0.0f ? (float) q : targetGlide;
            }
            targetNote = q;
        }
        if (! wasVoiced && newPhrase)
            targetGlide = (float) targetNote;   // al empezar frase no hay deslizamiento desde una nota vieja
        updateRetuneCoef();

        // Vibrato protect: corrige hacia el centro lento -> la oscilación rápida se conserva
        reference = smoothMidi + vibratoProtect * (slowMidi - smoothMidi);

        // Flex-Tune: peso de la corrección según la distancia a la nota de la escala
        const float dev = std::abs ((float) targetNote - reference);
        flexWeight = dev <= 0.5f * flexWindow ? 1.0f
                   : dev >= flexWindow ? 0.0f
                   : 1.0f - (dev - 0.5f * flexWindow) / (0.5f * flexWindow);

        // ventana síncrona: mitad de ventana = múltiplo entero del periodo
        const float period = (float) fs / freq;
        const float m = std::ceil ((float) ((lowLatency ? 0.004 : 0.007) * fs) / period);
        winTarget = juce::jlimit (winMin, winMax, 2.0f * m * period);
    }

    void setUnvoiced()
    {
        voiced = false;
        midiHist.fill (-1.0f);
        if (++hopsWithoutVoice > 40) prevFreq = -1.0f;   // ~170 ms sin voz: se olvida la F0 previa
    }

    void updateRetuneCoef()
    {
        const float sustain = juce::jmin (1.0f, (float) noteHoldSamples / (float) (0.15 * fs));
        const float tauMs = baseTauMs + humanize * sustain * 160.0f;
        retuneCoef = 1.0f - std::exp (-1.0f / (tauMs * 0.001f * (float) fs));
    }

    float read (int ch, float delay) const
    {
        const float pos = (float) writePos - delay;
        const float fl = std::floor (pos);
        const float t = pos - fl;
        const int i = (int) fl;
        const auto& l = lines[(size_t) ch];
        const float y0 = l[(size_t) ((i - 1) & mask)];
        const float y1 = l[(size_t) (i & mask)];
        const float y2 = l[(size_t) ((i + 1) & mask)];
        const float y3 = l[(size_t) ((i + 2) & mask)];
        // Hermite cúbico
        const float c0 = y1;
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + c0;
    }

    double fs = 44100.0, dfs = 11025.0;
    int decim = 4, maxLag = 180, minLag = 9, win = 360, histSize = 542;
    std::vector<float> hist, frame, diff;
    int histWrite = 0, decimCounter = 0, hopCounter = 0;
    juce::dsp::IIR::Filter<float> aa1, aa2;

    std::array<std::vector<float>, 2> lines;
    int mask = 0, writePos = 0;
    static constexpr int guard = 4;   // margen para la interpolación cúbica
    bool lowLatency = false;
    float winLen = 880.0f, winTarget = 880.0f, winMin = 480.0f, winMax = 2880.0f, lenCoef = 0.001f;
    float centreDelay = 1444.0f;
    float phase[2] { 0.5f, 0.0f };
    int quietSamples = 0;

    int rootKey = 0, scaleIndex = 0;
    float amount = 1.0f, retuneCoef = 0.1f, baseTauMs = 5.0f, humanize = 0.0f;
    float vibratoProtect = 0.0f, yinThreshold = 0.15f, slowMidi = -1.0f;
    int noteHoldSamples = 0;
    float correction = 0.0f, targetCorrection = 0.0f, smoothMidi = -1.0f;
    int targetNote = -1;
    bool voiced = false;
    int unvoicedSamples = 0;

    juce::SmoothedValue<float> mix;

    // v2.2
    float hfLpCoef = 0.5f, envCoef = 0.99f, hfLp = 0.0f, hfEnv = 0.0f, fullEnv = 0.0f;
    float wetGain = 0.0f, wetAttack = 0.01f, wetRelease = 0.01f;
    int unvoicedHops = 0, hopsWithoutVoice = 1000, jumpConfirm = 0, midiHistPos = 0;
    float prevFreq = -1.0f;
    std::array<float, 3> midiHist { -1.0f, -1.0f, -1.0f };
    float transitionSamples = 0.0f, flexWindow = 1.0e6f, flexWeight = 1.0f;
    float targetGlide = -1.0f, glideFrom = -1.0f, reference = -1.0f;
    std::atomic<float> appliedCorrection { 0.0f };
};
