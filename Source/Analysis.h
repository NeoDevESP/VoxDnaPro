#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include "DSP/Metering.h"
#include <array>
#include <functional>
#include <memory>

// ============================================================================
//  DNA ANALYZE (v3.0) — análisis local, sin nube ni modelos externos.
//   · Profile: espectro medio a largo plazo en 31 bandas de 1/3 de octava (canal
//     medio), sonoridad integrada y LRA (BS.1770 / EBU 3342), true peak (4x),
//     PLR, anchura (energía lateral / total), factor de cresta y centroide.
//   · Match: compara la FORMA espectral (cada perfil normalizado a su media),
//     la dinámica (PLR) y la anchura con una referencia que carga el usuario.
//   · VIBE: 5 descriptores 0..1 (brillo, aire, cuerpo, anchura, agresividad)
//     calculados del perfil medido; el "perfil del preset" se estima a partir de
//     sus parámetros y se rotula siempre como estimación.
//  Todo se ejecuta fuera del hilo de audio (UI o hilo de análisis).
// ============================================================================
namespace pv::analysis
{
constexpr int numBands = 31;
inline float bandCentre (int k) { return 1000.0f * std::exp2 ((float) (k - 17) / 3.0f); }   // 19.7 Hz .. 19.8 kHz

struct Profile
{
    bool valid = false;
    juce::String name;
    std::array<float, numBands> bandsDb {};   // potencia por banda (dB, escala relativa)
    float lufsI = -99.0f, lra = 0.0f, truePeakDb = -99.0f, plr = 0.0f, widthPct = 0.0f, crestDb = 0.0f, centroidHz = 0.0f, seconds = 0.0f;

    juce::var toVar() const
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("schema", 1);
        o->setProperty ("name", name);
        juce::Array<juce::var> b;
        for (float v : bandsDb) b.add (v);
        o->setProperty ("bands", b);
        o->setProperty ("lufsI", lufsI); o->setProperty ("lra", lra); o->setProperty ("truePeak", truePeakDb);
        o->setProperty ("plr", plr); o->setProperty ("width", widthPct); o->setProperty ("crest", crestDb);
        o->setProperty ("centroid", centroidHz); o->setProperty ("seconds", seconds);
        return juce::var (o);
    }

    static Profile fromVar (const juce::var& v)
    {
        Profile p;
        if (! v.isObject()) return p;
        p.name = v.getProperty ("name", "").toString();
        if (auto* b = v.getProperty ("bands", {}).getArray())
            for (int k = 0; k < numBands && k < b->size(); ++k) p.bandsDb[(size_t) k] = (float) (double) (*b)[k];
        p.lufsI = (float) (double) v.getProperty ("lufsI", -99.0);
        p.lra = (float) (double) v.getProperty ("lra", 0.0);
        p.truePeakDb = (float) (double) v.getProperty ("truePeak", -99.0);
        p.plr = (float) (double) v.getProperty ("plr", 0.0);
        p.widthPct = (float) (double) v.getProperty ("width", 0.0);
        p.crestDb = (float) (double) v.getProperty ("crest", 0.0);
        p.centroidHz = (float) (double) v.getProperty ("centroid", 0.0);
        p.seconds = (float) (double) v.getProperty ("seconds", 0.0);
        p.valid = p.seconds > 0.0f;
        return p;
    }

    // forma espectral: dB relativos a la potencia media de las bandas 100 Hz - 10 kHz
    // (la zona útil de una voz; así los graves casi vacíos no desplazan la referencia)
    std::array<float, numBands> shape() const
    {
        std::array<float, numBands> s {};
        double e = 0; int c = 0;
        for (int k = firstVoiceBand; k <= 27; ++k) { e += std::pow (10.0, bandsDb[(size_t) k] / 10.0); ++c; }
        const double m = 10.0 * std::log10 (e / juce::jmax (1, c) + 1e-20);
        for (int k = 0; k < numBands; ++k) s[(size_t) k] = juce::jlimit (-36.0f, 24.0f, bandsDb[(size_t) k] - (float) m);
        return s;
    }
    static constexpr int firstVoiceBand = 7, lastVoiceBand = 28;   // 100 Hz .. 12.5 kHz
};

struct Vibe
{
    float brightness = 0.5f, air = 0.5f, body = 0.5f, width = 0.5f, aggression = 0.5f;
    float operator[] (int i) const { return i == 0 ? brightness : i == 1 ? air : i == 2 ? body : i == 3 ? width : aggression; }
    static const juce::StringArray& names()
    {
        static const juce::StringArray n { "BRIGHTNESS", "AIR", "BODY", "WIDTH", "AGGRESSION" };
        return n;
    }
};

inline float map01 (float v, float lo, float hi) { return juce::jlimit (0.0f, 1.0f, (v - lo) / (hi - lo)); }

inline Vibe vibeFrom (const Profile& p)
{
    Vibe v;
    if (! p.valid) return v;
    double total = 0, body = 0, air = 0, pres = 0;
    for (int k = 0; k < numBands; ++k)
    {
        const double e = std::pow (10.0, p.bandsDb[(size_t) k] / 10.0);
        const float f = bandCentre (k);
        total += e;
        if (f >= 150.0f && f <= 400.0f) body += e;
        if (f >= 10000.0f) air += e;
        if (f >= 2000.0f && f <= 5000.0f) pres += e;
    }
    if (total <= 0) return v;
    auto rel = [&] (double e) { return (float) (10.0 * std::log10 (e / total + 1e-20)); };
    v.body = map01 (rel (body), -14.0f, -3.0f);
    v.air = map01 (rel (air), -55.0f, -20.0f);
    v.brightness = map01 (std::log2 (juce::jmax (100.0f, p.centroidHz)), std::log2 (600.0f), std::log2 (4000.0f));
    v.width = map01 (p.widthPct, 0.0f, 35.0f);
    v.aggression = juce::jlimit (0.0f, 1.0f, 0.5f * map01 (rel (pres), -22.0f, -8.0f) + 0.5f * (1.0f - map01 (p.crestDb, 8.0f, 20.0f)));
    return v;
}

struct Match
{
    bool valid = false;
    float total = 0, spectral = 0, dynamics = 0, width = 0;
    std::array<float, 4> eq {};   // sugerencias dB para Body / Mud / Presence / Air (ref - actual)
    static const juce::StringArray& eqNames()
    {
        static const juce::StringArray n { "eqBody", "eqMud", "eqPres", "eqAir" };
        return n;
    }
};

inline Match compare (const Profile& live, const Profile& ref)
{
    Match m;
    if (! live.valid || ! ref.valid) return m;
    const auto a = live.shape(), b = ref.shape();
    double d = 0; int c = 0;
    for (int k = Profile::firstVoiceBand; k <= Profile::lastVoiceBand; ++k)
    {
        d += juce::jmin (18.0f, std::abs (a[(size_t) k] - b[(size_t) k]));   // una banda vacía no domina
        ++c;
    }
    d /= juce::jmax (1, c);
    m.spectral = 100.0f * (float) std::exp (-d / 6.0);
    m.dynamics = 100.0f * std::exp (-std::abs (live.plr - ref.plr) / 6.0f);
    m.width = 100.0f * std::exp (-std::abs (live.widthPct - ref.widthPct) / 20.0f);
    m.total = 0.6f * m.spectral + 0.2f * m.dynamics + 0.2f * m.width;

    auto region = [&] (float lo, float hi)
    {
        double s = 0; int n = 0;
        for (int k = 0; k < numBands; ++k)
            if (bandCentre (k) >= lo && bandCentre (k) <= hi) { s += b[(size_t) k] - a[(size_t) k]; ++n; }
        const float v = n > 0 ? (float) (s / n) : 0.0f;
        return std::round (juce::jlimit (-4.0f, 4.0f, v) * 2.0f) / 2.0f;
    };
    m.eq = { region (100, 200), region (200, 400), region (2500, 5000), region (8000, 16000) };
    m.valid = true;
    return m;
}

// similitud entre dos VIBE (0..100)
inline float vibeMatch (const Vibe& a, const Vibe& b)
{
    float d = 0;
    for (int i = 0; i < 5; ++i) d += std::abs (a[i] - b[i]);
    return 100.0f * (1.0f - d / 5.0f);
}

// ---------------------------------------------------------------------------
// Acumulador de perfil (estéreo). Se alimenta con bloques; snapshot() da el perfil.
// decay < 1 -> media exponencial (perfil "reciente" para el VIBE en vivo)
// ---------------------------------------------------------------------------
class Accumulator
{
public:
    static constexpr int order = 12, N = 1 << order, hop = N / 2;

    void reset (double sampleRate, float decayPerFrame = 1.0f)
    {
        fs = sampleRate;
        decay = decayPerFrame;
        loud = std::make_unique<LoudnessMeter>();
        loud->prepare (fs);
        bandPow.fill (0.0); midE = sideE = 0.0; peak = 0.0f; sumSq = 0.0; count = 0; frames = 0; fill = 0;
        centroidNum = centroidDen = 0.0;
        tpPeak = 0.0f;
        hist.fill (0.0f);
        histL.fill (0.0f); histR.fill (0.0f);
        tpHist.fill (0.0f);
        tpPos = 0;
        if (window.empty())
        {
            window.resize ((size_t) N);
            for (int i = 0; i < N; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N);
            buildTpCoefs();
        }
    }

    void push (const float* l, const float* r, int n)
    {
        if (loud == nullptr) return;
        loud->process (l, r, n);
        for (int i = 0; i < n; ++i)
        {
            const float L = l[i], R = r != nullptr ? r[i] : l[i];
            const float m = 0.5f * (L + R), s = 0.5f * (L - R);
            if (decay < 1.0f) { midE *= kSample; sideE *= kSample; sumSq *= kSample; }
            midE += (double) m * m; sideE += (double) s * s;
            peak = juce::jmax (peak, std::abs (L), std::abs (R));
            sumSq += 0.5 * ((double) L * L + (double) R * R);
            ++count;
            truePeakFeed (L, R);
            histL[(size_t) fill] = L; histR[(size_t) fill] = R;
            hist[(size_t) fill] = m;
            if (++fill == N)
            {
                analyseFrame();
                // solape 50 %: la segunda mitad pasa a ser la primera
                std::copy (hist.begin() + hop, hist.end(), hist.begin());
                std::copy (histL.begin() + hop, histL.end(), histL.begin());
                std::copy (histR.begin() + hop, histR.end(), histR.begin());
                fill = hop;
            }
        }
    }

    Profile snapshot (const juce::String& name = {}) const
    {
        Profile p;
        p.name = name;
        if (frames == 0 || loud == nullptr) return p;
        double num = 0, den = 0;
        for (int k = 0; k < numBands; ++k)
        {
            const double e = bandPow[(size_t) k] / juce::jmax (1.0, effFrames());
            p.bandsDb[(size_t) k] = (float) (10.0 * std::log10 (e + 1e-20));
            num += e * bandCentre (k); den += e;
        }
        p.centroidHz = den > 0 ? (float) (num / den) : 0.0f;
        p.lufsI = loud->integrated.load();
        if (decay < 1.0f) p.lufsI = loud->shortTerm.load();
        p.lra = loud->loudnessRange.load();
        p.truePeakDb = juce::Decibels::gainToDecibels (tpPeak, -99.0f);
        p.plr = p.lufsI > -90.0f ? p.truePeakDb - p.lufsI : 0.0f;
        p.widthPct = (float) (100.0 * sideE / (midE + sideE + 1e-20));
        const double rms = std::sqrt (sumSq / juce::jmax (1.0, effCount()));
        p.crestDb = juce::Decibels::gainToDecibels (peak, -99.0f) - (float) juce::Decibels::gainToDecibels (rms, -99.0);
        p.seconds = (float) ((double) count / fs);
        p.valid = p.lufsI > -70.0f;
        return p;
    }

    double getSeconds() const { return (double) count / fs; }

private:
    double effFrames() const { return decay < 1.0f ? (1.0 - std::pow ((double) decay, (double) frames)) / (1.0 - decay) : (double) frames; }
    double effCount() const { return decay < 1.0f ? 1.0 / (1.0 - (double) kSample) : (double) count; }

    void analyseFrame()
    {
        for (int i = 0; i < N; ++i) fftData[(size_t) i] = hist[(size_t) i] * window[(size_t) i];
        std::fill (fftData.begin() + N, fftData.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (fftData.data());
        if (decay < 1.0f) for (auto& b : bandPow) b *= decay;
        // pico (decae en modo reciente)
        if (decay < 1.0f) { peak *= 0.97f; tpPeak *= 0.97f; }
        const double norm = 1.0 / ((double) N * 0.375 * (double) N);   // Hann: potencia media
        for (int i = 1; i < N / 2; ++i)
        {
            const float f = (float) (i * fs / N);
            const int k = juce::jlimit (0, numBands - 1, (int) std::round (3.0f * std::log2 (juce::jmax (1.0f, f) / 1000.0f)) + 17);
            if (f < 17.5f || f > 22000.0f) continue;
            const double mag = fftData[(size_t) i];
            bandPow[(size_t) k] += mag * mag * norm;
        }
        ++frames;
    }

    // true peak aproximado: interpolación 4x (sinc enventanado, 16 coeficientes por fase)
    void buildTpCoefs()
    {
        for (int p = 0; p < 4; ++p)
            for (int j = 0; j < 16; ++j)
            {
                const double u = (double) (8 - j) - p / 4.0;
                const double sinc = std::abs (u) < 1e-9 ? 1.0 : std::sin (juce::MathConstants<double>::pi * u) / (juce::MathConstants<double>::pi * u);
                const double w = 0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * u / 9.0);
                tpCoef[(size_t) p][(size_t) j] = (float) (sinc * w);
            }
    }
    void truePeakFeed (float L, float R)
    {
        tpHist[(size_t) tpPos] = L; tpHist[(size_t) (tpPos + 16)] = R;
        tpPos = (tpPos + 1) & 15;
        if ((count & 3) != 0 && tpPeak > 0.0f) { tpPeak = juce::jmax (tpPeak, std::abs (L), std::abs (R)); return; }   // 1 de cada 4 muestras: suficiente para el perfil
        for (int c = 0; c < 2; ++c)
            for (int p = 0; p < 4; ++p)
            {
                float acc = 0.0f;
                for (int j = 0; j < 16; ++j)
                    acc += tpCoef[(size_t) p][(size_t) j] * tpHist[(size_t) (c * 16 + ((tpPos + j) & 15))];
                tpPeak = juce::jmax (tpPeak, std::abs (acc));
            }
    }

    static constexpr float kSample = 0.99997f;
    double fs = 48000.0;
    float decay = 1.0f;
    std::unique_ptr<LoudnessMeter> loud;
    juce::dsp::FFT fft { order };
    std::vector<float> window;
    std::array<float, N> hist {}, histL {}, histR {};
    std::array<float, N * 2> fftData {};
    std::array<double, numBands> bandPow {};
    std::array<std::array<float, 16>, 4> tpCoef {};
    std::array<float, 32> tpHist {};
    int tpPos = 0;
    float tpPeak = 0.0f;
    double midE = 0, sideE = 0, sumSq = 0, centroidNum = 0, centroidDen = 0;
    float peak = 0.0f;
    long count = 0;
    int frames = 0, fill = 0;
};

// Analiza un archivo de audio completo (llamar desde un hilo que no sea el de audio)
inline Profile analyzeFile (const juce::File& file, std::function<bool()> shouldExit = {})
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr) return {};
    auto acc = std::make_unique<Accumulator>();
    acc->reset (reader->sampleRate);
    const int block = 8192;
    juce::AudioBuffer<float> buf ((int) juce::jmax (1u, juce::jmin (2u, reader->numChannels)), block);
    const juce::int64 maxLen = (juce::int64) (reader->sampleRate * 600.0);   // como mucho 10 minutos
    for (juce::int64 pos = 0; pos < juce::jmin (reader->lengthInSamples, maxLen); pos += block)
    {
        if (shouldExit && shouldExit()) return {};
        const int n = (int) juce::jmin ((juce::int64) block, reader->lengthInSamples - pos);
        reader->read (&buf, 0, n, pos, true, buf.getNumChannels() > 1);
        acc->push (buf.getReadPointer (0), buf.getNumChannels() > 1 ? buf.getReadPointer (1) : nullptr, n);
    }
    return acc->snapshot (file.getFileNameWithoutExtension());
}

// ---------------------------------------------------------------------------
// VIBE objetivo estimado a partir de los parámetros del preset (rotulado como estimación)
// ---------------------------------------------------------------------------
inline Vibe presetVibe (std::function<float (const char*)> v, float grTarget)
{
    Vibe t;
    const bool eq = v ("eqOn") > 0.5f, sat = v ("satOn") > 0.5f;
    const float pres = eq ? v ("eqPres") : 0.0f, air = eq ? v ("eqAir") : 0.0f, body = eq ? v ("eqBody") : 0.0f;
    const float mud = eq ? v ("eqMud") : 0.0f, exc = eq ? v ("eqExcite") : 0.0f;
    t.brightness = juce::jlimit (0.05f, 0.95f, 0.45f + 0.04f * pres + 0.03f * air + 0.002f * exc - 0.02f * body
                                                + (sat && (int) v ("satMode") == 4 ? 0.1f : 0.0f) + 0.01f * v ("formant"));
    t.air = juce::jlimit (0.05f, 0.95f, 0.35f + 0.06f * air + 0.004f * exc);
    t.body = juce::jlimit (0.05f, 0.95f, 0.5f + 0.05f * body + 0.02f * mud - 0.002f * (v ("hpFreq") - 75.0f) - 0.02f * v ("formant"));
    float w = 0.15f;
    if (v ("layOn1") > 0.5f && v ("layOn2") > 0.5f) w += 0.3f + 0.015f * (v ("layLvl1") + 18.0f);
    if (v ("modOn") > 0.5f) w += 0.08f;
    if (v ("revOn") > 0.5f) w += 0.004f * v ("revMix");
    if (v ("dlyOn") > 0.5f && (int) v ("dlyTime") < 7) w += 0.08f;
    t.width = juce::jlimit (0.05f, 0.95f, w);
    float ag = 0.25f + 0.03f * grTarget;
    if (sat) ag += 0.006f * v ("satMix") * (0.5f + 0.01f * v ("satDrive")) + ((int) v ("satMode") == 4 ? 0.2f : 0.0f);
    if (v ("tuneOn") > 0.5f && v ("tuneRetune") <= 8.0f) ag += 0.08f;
    t.aggression = juce::jlimit (0.05f, 0.95f, ag);
    return t;
}
} // namespace pv::analysis
