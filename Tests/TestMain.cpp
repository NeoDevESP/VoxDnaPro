#define _USE_MATH_DEFINES   // M_PI en MSVC
// ============================================================================
//  VOX DNA PRO — banco de pruebas offline (blueprint cap. 12)
//  Tiempo real (0 asignaciones) · Presets · Tuning · Formant · Latencia/PDC ·
//  Latencia audible · Sample rates · Bloques variables y mayores que el
//  anunciado · Recall · JSON · Mono · Clicks · Cambio de modo en reproducción ·
//  True peak · BS.1770 (EBU 3341) · Aliasing · CPU · UI
//  Modo "race": audio en un hilo real + UI (para ThreadSanitizer)
// ============================================================================
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <chrono>
#include <cstdio>
#include <random>
#include <map>
#include <thread>
#include <functional>
#include <new>
#include <atomic>
#include <cstdlib>

// ---------------------------------------------------------------------------
// Detector de asignaciones de memoria en el hilo de audio (blueprint cap. 12:
// "cero allocs/locks en processBlock"). Se cuentan las llamadas a new/delete
// mientras g_inAudio está activo en el hilo actual.
static thread_local bool g_inAudio = false;
static std::atomic<long> g_audioAllocs { 0 };
void* operator new (std::size_t n)
{
    if (g_inAudio) g_audioAllocs.fetch_add (1);
    if (void* p = std::malloc (n == 0 ? 1 : n)) return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t n) { return operator new (n); }
void operator delete (void* p) noexcept { if (g_inAudio && p != nullptr) g_audioAllocs.fetch_add (1); std::free (p); }
void operator delete[] (void* p) noexcept { operator delete (p); }
void operator delete (void* p, std::size_t) noexcept { operator delete (p); }
void operator delete[] (void* p, std::size_t) noexcept { operator delete (p); }

struct AudioScope
{
    AudioScope() { g_inAudio = true; }
    ~AudioScope() { g_inAudio = false; }
};

static int failures = 0;
static void check (bool ok, const char* what, const juce::String& detail = {})
{
    if (! ok) ++failures;
    std::printf ("  [%s] %s %s\n", ok ? " OK " : "FAIL", what, detail.toRawUTF8());
}

static void setParam (VoxDnaProcessor& p, const juce::String& id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

static void allNeutral (VoxDnaProcessor& p)
{
    p.setCurrentProgram (0);
    for (auto id : { "compOn", "deessOn", "eqOn", "optoOn", "satOn", "modOn", "dlyOn", "revOn" }) setParam (p, id, 0);
    setParam (p, "hpFreq", 20);
}

static std::vector<float> vocalLike (double fs, int n, float f0, float vibratoCents, float formantScale = 1.0f)
{
    std::vector<float> out ((size_t) n);
    double ph = 0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs;
        const double f = f0 * std::pow (2.0, vibratoCents * std::sin (2 * M_PI * 5.0 * t) / 1200.0);
        ph += 2 * M_PI * f / fs;
        double s = 0;
        for (int h = 1; h <= 30; ++h)
        {
            const double hf = h * f;
            if (hf > fs * 0.45) break;
            const double form = std::exp (-std::pow ((hf - 750 * formantScale) / 300, 2)) + 0.6 * std::exp (-std::pow ((hf - 1200 * formantScale) / 400, 2))
                              + 0.3 * std::exp (-std::pow ((hf - 2600 * formantScale) / 500, 2)) + 0.05;
            s += form * std::sin (h * ph);
        }
        out[(size_t) i] = (float) (0.12 * s);
    }
    return out;
}

static float measurePitch (const std::vector<float>& x, double fs, int start, int len)
{
    auto acf = [&] (int L) { double num = 0, e1 = 0, e2 = 0; for (int i = 0; i < len; ++i) { double a = x[(size_t) (start + i)], b = x[(size_t) (start + i + L)]; num += a * b; e1 += a * a; e2 += b * b; } return num / std::sqrt (e1 * e2 + 1e-12); };
    const int minLag = (int) (fs / 1000), maxLag = (int) (fs / 70);
    int bestLag = minLag; double best = -1;
    for (int lag = minLag; lag <= maxLag; ++lag) { const double r = acf (lag); if (r > best) { best = r; bestLag = lag; } }
    for (int lag = minLag + 1; lag < bestLag; ++lag)
    {
        const double r = acf (lag);
        if (r > 0.97 * best && r >= acf (lag - 1) && r >= acf (lag + 1)) { bestLag = lag; break; }
    }
    const double a = acf (bestLag - 1), b = acf (bestLag), c = acf (bestLag + 1);
    return (float) (fs / (bestLag + 0.5 * (a - c) / (a - 2 * b + c)));
}

// centroide espectral simple (DFT por bins de 50 Hz hasta 5 kHz)
static float centroid (const std::vector<float>& x, double fs, int start, int len)
{
    double num = 0, den = 0;
    for (double f = 100; f < 5000; f += 25)
    {
        double re = 0, im = 0;
        for (int i = 0; i < len; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2 * M_PI * i / len);
            re += w * x[(size_t) (start + i)] * std::cos (2 * M_PI * f * i / fs);
            im += w * x[(size_t) (start + i)] * std::sin (2 * M_PI * f * i / fs);
        }
        const double m = std::sqrt (re * re + im * im);
        num += f * m; den += m;
    }
    return (float) (num / den);
}


// true peak de referencia: sobremuestreo 16x con sinc enventanado de 64 lóbulos (mucho más
// preciso que el medidor 4x de BS.1770) -> sirve para auditar el limitador
static float referenceTruePeakDb (const std::vector<float>& x)
{
    const int up = 16, half = 32;
    float peak = 0.0f;
    for (size_t n = (size_t) half; n + (size_t) half < x.size(); ++n)
        for (int p = 0; p < up; ++p)
        {
            const double t = (double) p / up;
            double acc = 0.0;
            for (int k = -half + 1; k <= half; ++k)
            {
                const double u = (double) k - t;
                const double sinc = std::abs (u) < 1e-12 ? 1.0 : std::sin (M_PI * u) / (M_PI * u);
                const double w = 0.5 + 0.5 * std::cos (M_PI * u / (half + 1));
                acc += x[(size_t) ((long) n + k)] * sinc * w;
            }
            peak = std::max (peak, (float) std::abs (acc));
        }
    return 20.0f * std::log10 (std::max (peak, 1e-9f));
}

static std::vector<float> envelope (const std::vector<float>& x, double fs)
{
    std::vector<float> e (x.size());
    float z = 0;
    const float a = std::exp (-1.0f / (0.002f * (float) fs));
    for (size_t i = 0; i < x.size(); ++i) { const float v = std::abs (x[i]); z = v + a * (z - v); e[i] = z; }
    return e;
}

// retardo MEDIO (centroide de la correlación de envolventes) y pico
struct LagResult { double mean; int peak; };
static LagResult envelopeLag (const std::vector<float>& in, const std::vector<float>& out, double fs, int maxLag)
{
    auto ei = envelope (in, fs), eo = envelope (out, fs);
    const int start = (int) fs / 2, len = (int) fs;
    std::vector<double> r ((size_t) maxLag + 1);
    int best = 0; double bestR = -1;
    for (int lag = 0; lag <= maxLag; ++lag)
    {
        double num = 0, a2 = 0, b2 = 0;
        for (int i = start; i < start + len; i += 2)
        {
            const double a = ei[(size_t) i], b = eo[(size_t) (i + lag)];
            num += a * b; a2 += a * a; b2 += b * b;
        }
        r[(size_t) lag] = num / std::sqrt (a2 * b2 + 1e-20);
        if (r[(size_t) lag] > bestR) { bestR = r[(size_t) lag]; best = lag; }
    }
    // centroide de la zona alta de la correlación (>= 95 % del máximo)
    double num = 0, den = 0;
    for (int lag = 0; lag <= maxLag; ++lag)
    {
        const double w = std::max (0.0, r[(size_t) lag] - 0.95 * bestR);
        num += w * lag; den += w;
    }
    return { den > 0 ? num / den : (double) best, best };
}

// retardo mediano de los ATAQUES de sílaba (cruce de umbral de la envolvente tras silencio)
static double onsetDelay (const std::vector<float>& in, const std::vector<float>& out, double fs)
{
    auto ei = envelope (in, fs), eo = envelope (out, fs);
    auto onsets = [&] (const std::vector<float>& e)
    {
        std::vector<double> t;
        float peak = 0; for (float v : e) peak = std::max (peak, v);
        const float thr = 0.1f * peak;
        int quiet = 0;
        for (size_t i = 1; i < e.size(); ++i)
        {
            if (e[i] < thr * 0.3f) ++quiet;
            else if (e[i - 1] < thr && e[i] >= thr && quiet > (int) (0.02 * fs))
            {
                t.push_back ((double) i - 1 + (thr - e[i - 1]) / (e[i] - e[i - 1]));
                quiet = 0;
            }
        }
        return t;
    };
    auto a = onsets (ei), b = onsets (eo);
    std::vector<double> d;
    for (double ta : a)
        for (double tb : b)
            if (tb >= ta && tb - ta < 0.08 * fs) { d.push_back (tb - ta); break; }
    if (d.empty()) return -1.0;
    std::sort (d.begin(), d.end());
    return d[d.size() / 2];
}

// voz desafinada con sílabas (envolvente irregular) para medir el retardo audible con el tune corrigiendo
static std::vector<float> syllables (double fs, int n, float f0)
{
    auto v = vocalLike (fs, n, f0, 25.0f);
    std::mt19937 rng (11);
    int pos = 0;
    while (pos < n)
    {
        const int len = (int) (fs * (0.08 + 0.22 * (rng() % 1000) / 1000.0));
        const int gap = (int) (fs * (0.03 + 0.12 * (rng() % 1000) / 1000.0));
        for (int i = 0; i < len && pos + i < n; ++i)
            v[(size_t) (pos + i)] *= (float) std::sin (M_PI * i / len);
        for (int i = 0; i < gap && pos + len + i < n; ++i)
            v[(size_t) (pos + len + i)] = 0.0f;
        pos += len + gap;
    }
    return v;
}

// tono en una ventana corta, buscando sólo entre fmin y fmax (autocorrelación con interpolación)
static float pitchIn (const std::vector<float>& x, double fs, int start, int len, float fmin, float fmax)
{
    auto acf = [&] (double L)
    {
        const int l0 = (int) L; const double fr = L - l0; double num = 0, e1 = 0, e2 = 0;
        for (int i = 0; i < len; ++i)
        {
            const double a = x[(size_t) (start + i)];
            const double b = (1 - fr) * x[(size_t) (start + i + l0)] + fr * x[(size_t) (start + i + l0 + 1)];
            num += a * b; e1 += a * a; e2 += b * b;
        }
        return num / std::sqrt (e1 * e2 + 1e-12);
    };
    double bestL = fs / fmax, best = -2;
    for (double L = fs / fmax; L <= fs / fmin; L += 0.2) { const double r = acf (L); if (r > best) { best = r; bestL = L; } }
    return (float) (fs / bestL);
}

static float cents (float f, float ref) { return 1200.0f * std::log2 (f / ref); }

// tono con armónicos arbitrarios (para fundamental débil) y salto de nota opcional
static std::vector<float> toneWith (double fs, int n, std::function<float (double)> freqAt, std::vector<float> harmonics, float vibCents = 0.0f)
{
    std::vector<float> out ((size_t) n);
    double ph = 0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs;
        const double f = freqAt (t) * std::pow (2.0, vibCents * std::sin (2 * M_PI * 5.5 * t) / 1200.0);
        ph += 2 * M_PI * f / fs;
        double s = 0;
        for (size_t h = 0; h < harmonics.size(); ++h)
            if ((h + 1) * f < fs * 0.45) s += harmonics[h] * std::sin ((double) (h + 1) * ph);
        out[(size_t) i] = (float) (0.1 * s);
    }
    return out;
}

static std::vector<float> run (VoxDnaProcessor& p, const std::vector<float>& sig, int block, std::vector<float>* right = nullptr)
{
    std::vector<float> out;
    juce::MidiBuffer midi;
    for (int pos = 0; pos + block <= (int) sig.size(); pos += block)
    {
        juce::AudioBuffer<float> buf (2, block);
        for (int i = 0; i < block; ++i) { buf.setSample (0, i, sig[(size_t) (pos + i)]); buf.setSample (1, i, sig[(size_t) (pos + i)]); }
        p.processBlock (buf, midi);
        for (int i = 0; i < block; ++i)
        {
            out.push_back (buf.getSample (0, i));
            if (right) right->push_back (buf.getSample (1, i));
        }
    }
    return out;
}

static void prepare (VoxDnaProcessor& p, double fs, int block, int ch = 2)
{
    p.setPlayConfigDetails (ch, ch, fs, block);
    p.prepareToPlay (fs, block);
}

// ---- v2.3: utilidades de análisis armónico ----
static double harmonicAmp (const std::vector<float>& x, double fs, double f, int start, int len)
{
    double re = 0, im = 0;
    for (int i = 0; i < len; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * M_PI * i / (len - 1));
        const double ph = 2.0 * M_PI * f * (start + i) / fs;
        re += x[(size_t) (start + i)] * w * std::cos (ph);
        im += x[(size_t) (start + i)] * w * std::sin (ph);
    }
    return 4.0 * std::sqrt (re * re + im * im) / len;   // amplitud (ventana Hann, ganancia 0.5)
}
struct Harm { double h1, h2, h3, thd; };
static Harm harmonics (const std::vector<float>& y, double fs, double f0)
{
    const int len = (int) fs / 2, start = (int) y.size() - len;
    Harm h { harmonicAmp (y, fs, f0, start, len), harmonicAmp (y, fs, 2 * f0, start, len), harmonicAmp (y, fs, 3 * f0, start, len), 0 };
    double e = 0;
    for (int k = 2; k <= 9; ++k) { const double a = harmonicAmp (y, fs, k * f0, start, len); e += a * a; }
    h.thd = std::sqrt (e) / h.h1;
    return h;
}
static std::vector<float> colorRun (pv::ColorMode mode, float drive, float amp, double fs, double f0 = 1000.0)
{
    pv::ColorStage cs; cs.prepare (fs);
    juce::SmoothedValue<float> mix; mix.setCurrentAndTargetValue (1.0f);
    std::vector<float> x ((size_t) fs);
    for (size_t i = 0; i < x.size(); ++i) x[i] = amp * (float) std::sin (2.0 * M_PI * f0 * (double) i / fs);
    cs.process (x.data(), nullptr, (int) x.size(), mode, drive, mix);
    return x;
}

int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);   // sin búfer: si algo falla, el informe no se pierde
    juce::ScopedJuceInitialiser_GUI gui;
    const double fs = 48000.0;
    const int block = 256;
    // Modo carrera: audio en un hilo real + UI en el hilo de mensajes (para ThreadSanitizer)
    if (argc > 1 && juce::String (argv[1]) == "race")
    {
        VoxDnaProcessor p;
        prepare (p, fs, 256);
        p.setCurrentProgram (2);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        std::atomic<bool> running { true };
        auto sig = vocalLike (fs, (int) fs * 2, 210.0f, 30.0f);
        std::thread audio ([&]
        {
            juce::MidiBuffer midi;
            juce::AudioBuffer<float> buf (2, 256);
            std::mt19937 rng (1);
            int pos = 0;
            while (running.load())
            {
                for (int i = 0; i < 256; ++i) { buf.setSample (0, i, sig[(size_t) pos]); buf.setSample (1, i, sig[(size_t) pos]); pos = (pos + 1) % (int) sig.size(); }
                if (rng() % 4 == 0)   // automatización del host desde el hilo de audio
                    p.getParameters()[(int) (rng() % (unsigned) p.getParameters().size())]->setValue ((float) (rng() % 1000) / 999.0f);
                p.processBlock (buf, midi);
                std::this_thread::sleep_for (std::chrono::microseconds (500));
            }
        });
        const auto t0 = juce::Time::getMillisecondCounter();
        int k = 0;
        while (juce::Time::getMillisecondCounter() - t0 < 4000)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);   // timers del editor: analizador, medidores
            if (++k % 10 == 0) { juce::MemoryBlock mb; p.getStateInformation (mb); }
            if (k % 25 == 0) p.setCurrentProgram (k % 12);
            if (k % 40 == 0) { auto* prm = p.apvts.getParameter ("quality"); prm->setValueNotifyingHost (prm->getValue() < 0.25f ? 0.5f : 0.0f); }
            if (k % 15 == 0) if (auto* ve = dynamic_cast<VoxDnaEditor*> (ed.get())) ve->setView ((k / 15) % 5);   // las 5 vistas leen el audio
            if (k % 30 == 0)   // edición de pasos del secuenciador desde la UI
                p.sequencer.steps[(size_t) (k % 4)][(size_t) (k % 16)].store ((float) (k % 7) / 6.0f);
            if (k % 50 == 0) { auto* prm = p.apvts.getParameter ("chainOrder"); prm->setValueNotifyingHost ((float) ((k / 50) % 6) / 5.0f); }
            if (k % 20 == 0) { auto* prm = p.apvts.getParameter ("seqOn"); prm->setValueNotifyingHost (prm->getValue() > 0.5f ? 0.0f : 1.0f); }
        }
        running.store (false);
        audio.join();
        std::printf ("race run done: %d ciclos de UI\n", k);
        return 0;
    }


    // --------------------------------------------- tiempo real: asignaciones
    std::printf ("\n== TIEMPO REAL: asignaciones de memoria dentro de processBlock ==\n");
    {
        std::mt19937 rng (3);
        for (int q : { 0, 1, 2 })
        {
            VoxDnaProcessor p;
            prepare (p, fs, 512);
            setParam (p, "quality", (float) q);
            p.setCurrentProgram (3);
            for (int l = 0; l < pv::numLayers; ++l) setParam (p, "layOn" + juce::String (l), 1);
            for (auto id : { "tuneOn", "dlyOn", "revOn", "modOn", "optoOn", "satOn" }) setParam (p, id, 1);
            auto sig = vocalLike (fs, (int) fs * 2, 200.0f, 30.0f);
            juce::MidiBuffer midi;
            juce::AudioBuffer<float> buf (2, 2048);
            // los primeros bloques también cuentan: en un host real son el hilo de audio
            g_audioAllocs = 0;
            for (int k = 0; k < 40; ++k)
            {
                buf.setSize (2, 512, false, false, true);
                for (int i = 0; i < 512; ++i) { buf.setSample (0, i, sig[(size_t) (k * 512 + i)]); buf.setSample (1, i, sig[(size_t) (k * 512 + i)]); }
                AudioScope scope;
                p.processBlock (buf, midi);
            }
            juce::MessageManager::getInstance()->runDispatchLoopUntil (150);
            int pos = 0, calls = 0;
            const int sizes[] { 1, 7, 64, 128, 512, 1024, 2048 };   // incluye bloques MAYORES que el anunciado (512)
            while (pos + 2048 < (int) sig.size())
            {
                const int b = sizes[rng() % 7];
                buf.setSize (2, b, false, false, true);
                for (int i = 0; i < b; ++i) { buf.setSample (0, i, sig[(size_t) (pos + i)]); buf.setSample (1, i, sig[(size_t) (pos + i)]); }
                // automatización de parámetros desde el hilo de audio, como hace un host
                if (calls % 3 == 0)
                    for (auto* prm : p.getParameters())
                        if (rng() % 8 == 0 && prm != p.apvts.getParameter ("quality"))
                            prm->setValue ((float) (rng() % 1000) / 999.0f);
                {
                    AudioScope scope;
                    p.processBlock (buf, midi);
                }
                pos += b; ++calls;
            }
            check (g_audioAllocs.load() == 0, ("Cero asignaciones en modo " + juce::String (q == 0 ? "MONITOR" : q == 1 ? "MIX" : "RENDER HQ")).toRawUTF8(),
                   juce::String (calls) + " bloques (1-2048 muestras, max anunciado 512) + automatizacion: " + juce::String ((int) g_audioAllocs.load()) + " asignaciones");
        }
    }

    // ---------------------------------------------------------------- presets
    std::printf ("\n== PRESETS (estabilidad, techo -1 dBFS) ==\n");
    {
        VoxDnaProcessor p;
        prepare (p, fs, block);
        auto sig = vocalLike (fs, (int) fs * 2, 196.0f, 40.0f);
        int ok = 0;
        for (int pr = 0; pr < p.getNumPrograms(); ++pr)
        {
            p.setCurrentProgram (pr);
            setParam (p, "tuneKey", 7); setParam (p, "tuneScale", 2);
            auto out = run (p, sig, block);
            float peak = 0; bool finite = true;
            for (float v : out) { finite &= std::isfinite (v); peak = std::max (peak, std::abs (v)); }
            const bool good = finite && peak <= 0.8914f;
            ok += good ? 1 : 0;
            if (! good) check (false, p.getProgramName (pr).toRawUTF8(), "peak " + juce::String (peak, 3));
        }
        check (ok == p.getNumPrograms(), "Todos los presets estables", juce::String (ok) + "/" + juce::String (p.getNumPrograms()));
    }

    // ----------------------------------------------------------------- tuning
    std::printf ("\n== DNA TUNE (precision) ==\n");
    {
        struct Case { float inHz; int key, scale; float retune; float expectHz; const char* name; };
        const Case cases[] {
            { 220.0f * std::pow (2.0f, 0.35f / 12.0f), 0, 0, 0.0f, 220.0f, "A3 +35c chromatic, retune 0" },
            { 220.0f * std::pow (2.0f, -0.40f / 12.0f), 0, 0, 0.0f, 220.0f, "A3 -40c chromatic, retune 0" },
            { 146.83f * std::pow (2.0f, 0.3f / 12.0f), 2, 2, 20.0f, 146.83f, "D3 +30c D minor, retune 20" },
            { 392.0f * std::pow (2.0f, -0.25f / 12.0f), 7, 1, 10.0f, 392.0f, "G4 -25c G major, retune 10" } };
        for (auto& c : cases)
        {
            VoxDnaProcessor p;
            prepare (p, fs, block);
            allNeutral (p);
            setParam (p, "tuneOn", 1); setParam (p, "tuneKey", (float) c.key); setParam (p, "tuneScale", (float) c.scale);
            setParam (p, "tuneRetune", c.retune); setParam (p, "tuneHumanize", 0); setParam (p, "tuneAmount", 100);
            auto out = run (p, vocalLike (fs, (int) fs * 2, c.inHz, 0.0f), block);
            const float measured = measurePitch (out, fs, (int) fs, 4096);
            const float err = 1200.0f * std::log2 (measured / c.expectHz);
            check (std::abs (err) < 8.0f, c.name, juce::String (measured, 2) + " Hz, err " + juce::String (err, 1) + " c");
        }
        // vibrato protect: la oscilación de 5 Hz debe sobrevivir con Vibrato 100
        for (float vp : { 0.0f, 100.0f })
        {
            VoxDnaProcessor p;
            prepare (p, fs, block);
            allNeutral (p);
            setParam (p, "tuneOn", 1); setParam (p, "tuneRetune", 0); setParam (p, "tuneHumanize", 0);
            setParam (p, "tuneVibrato", vp); setParam (p, "tuneScale", 0);
            auto out = run (p, vocalLike (fs, (int) fs * 3, 220.0f, 45.0f), block);
            // F0 por autocorrelación restringida a 180-270 Hz, ventanas de 25 ms cada 5 ms
            std::vector<float> track;
            for (int st = (int) fs; st < (int) fs * 2; st += 240)
            {
                const int len = 1200;
                auto acf = [&] (double L)
                {
                    const int l0 = (int) L; const double fr = L - l0; double num = 0, e1 = 0, e2 = 0;
                    for (int i = 0; i < len; ++i)
                    {
                        const double a = out[(size_t) (st + i)];
                        const double b = (1 - fr) * out[(size_t) (st + i + l0)] + fr * out[(size_t) (st + i + l0 + 1)];
                        num += a * b; e1 += a * a; e2 += b * b;
                    }
                    return num / std::sqrt (e1 * e2 + 1e-12);
                };
                double bestL = 0, best = -2;
                for (double L = fs / 270.0; L <= fs / 180.0; L += 0.25) { const double r = acf (L); if (r > best) { best = r; bestL = L; } }
                track.push_back ((float) (fs / bestL));
            }
            std::sort (track.begin(), track.end());
            const float lo = track[track.size() / 20], hi = track[track.size() - 1 - track.size() / 20];
            const float depth = 1200.0f * std::log2 (hi / lo) * 0.5f;
            if (vp == 0.0f) check (depth < 20.0f, "Vibrato aplanado con Protect 0", juce::String (depth, 1) + " c");
            else            check (depth > 25.0f, "Vibrato conservado con Protect 100", juce::String (depth, 1) + " c");
        }
    }

    // ---------------------------------------------------------------- formant
    std::printf ("\n== FORMANT SHIFT ==\n");
    {
        float cBase = 0;
        for (float st : { 0.0f, 3.0f, -3.0f })
        {
            VoxDnaProcessor p;
            prepare (p, fs, block);
            allNeutral (p);
            setParam (p, "formant", st);
            auto out = run (p, vocalLike (fs, (int) fs * 2, 150.0f, 0.0f), block);
            const float f0 = measurePitch (out, fs, (int) fs, 4096);
            const float c = centroid (out, fs, (int) fs, 4096);
            if (st == 0.0f) cBase = c;
            const float f0err = 1200.0f * std::log2 (f0 / 150.0f);
            check (std::abs (f0err) < 10.0f, ("F0 intacta con formant " + pv::ui::num (st, 0) + " st").toRawUTF8(),
                   juce::String (f0, 1) + " Hz");
            if (st > 0) check (c > cBase * 1.08f, "Formantes suben con +3 st", "centroide " + pv::ui::num (cBase, 0) + " -> " + pv::ui::num (c, 0) + " Hz");
            if (st < 0) check (c < cBase * 0.94f, "Formantes bajan con -3 st", "centroide " + pv::ui::num (cBase, 0) + " -> " + pv::ui::num (c, 0) + " Hz");
        }
    }

    // ------------------------------------------------------------ latencia/PDC
    std::printf ("\n== LATENCIA / PDC (impulso) ==\n");
    for (int q : { 0, 1, 2 })
    {
        VoxDnaProcessor p;
        prepare (p, fs, block);
        allNeutral (p);
        setParam (p, "quality", (float) q);
        for (float dw : { 100.0f, 50.0f })
        {
            setParam (p, "dryWet", dw);
            std::vector<float> sig ((size_t) fs / 2, 0.0f);
            run (p, sig, block);   // aplica el cambio de modo
            sig[1000] = 0.5f;
            auto out = run (p, sig, block);
            int peakIdx = 0; float peak = 0; int peaks = 0;
            for (int i = 0; i < (int) out.size(); ++i)
            {
                if (std::abs (out[(size_t) i]) > peak) { peak = std::abs (out[(size_t) i]); peakIdx = i; }
                if (std::abs (out[(size_t) i]) > 0.1f) ++peaks;
            }
            const int lat = peakIdx - 1000;
            check (std::abs (lat - p.reportedLatency.load()) <= 1 && (dw > 99.0f || peaks <= 3),
                   ("Modo " + juce::String (q == 0 ? "MONITOR" : q == 1 ? "MIX" : "RENDER HQ") + " dry/wet " + juce::String ((int) dw) + "%").toRawUTF8(),
                   "medida " + juce::String (lat) + " / reportada " + juce::String (p.reportedLatency.load()) + " smp, picos>0.1: " + juce::String (peaks));
        }
    }

    // --------------------------------------------- sample rates + bloques
    std::printf ("\n== SAMPLE RATES x BLOQUES VARIABLES (preset RAGE EDGE + 8 capas) ==\n");
    {
        std::mt19937 rng (42);
        const int sizes[] { 1, 16, 32, 64, 128, 512, 2048 };
        for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0 })
        {
            VoxDnaProcessor p;
            prepare (p, sr, 2048);
            p.setCurrentProgram (3);
            for (int l = 0; l < pv::numLayers; ++l) setParam (p, "layOn" + juce::String (l), 1);
            setParam (p, "dlyOn", 1); setParam (p, "revOn", 1); setParam (p, "modOn", 1);
            auto sig = vocalLike (sr, (int) sr * 2, 180.0f, 30.0f);
            bool finite = true; float peak = 0; int pos = 0;
            juce::MidiBuffer midi;
            while (pos < (int) sig.size())
            {
                const int b = std::min ((int) sig.size() - pos, sizes[rng() % 7]);
                juce::AudioBuffer<float> buf (2, b);
                for (int i = 0; i < b; ++i) { buf.setSample (0, i, sig[(size_t) (pos + i)]); buf.setSample (1, i, sig[(size_t) (pos + i)]); }
                p.processBlock (buf, midi);
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < b; ++i) { const float v = buf.getSample (c, i); finite &= std::isfinite (v); peak = std::max (peak, std::abs (v)); }
                pos += b;
            }
            check (finite && peak <= 0.8914f && peak > 0.01f, (juce::String (sr / 1000.0, 1) + " kHz").toRawUTF8(), "peak " + juce::String (peak, 3));
        }
    }

    // ---------------------------------------------------------- preset recall
    std::printf ("\n== PRESET RECALL / JSON ==\n");
    {
        VoxDnaProcessor a;
        prepare (a, fs, block);
        std::mt19937 rng (7);
        for (auto* prm : a.getParameters()) prm->setValueNotifyingHost ((float) (rng() % 1000) / 999.0f);
        juce::MemoryBlock mb;
        a.getStateInformation (mb);
        VoxDnaProcessor b;
        prepare (b, fs, block);
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        int diff = 0;
        for (int i = 0; i < a.getParameters().size(); ++i)
            if (a.getParameters()[i]->getCurrentValueAsText() != b.getParameters()[i]->getCurrentValueAsText())
            {
                ++diff;
                if (diff <= 4) std::printf ("    %s: %f vs %f\n", a.getParameters()[i]->getName (40).toRawUTF8(), a.getParameters()[i]->getValue(), b.getParameters()[i]->getValue());
            }
        check (diff == 0, "Sesion guardar/reabrir exacta", juce::String (a.getParameters().size()) + " parametros, diferencias: " + juce::String (diff));

        const juce::String name = "__vdna_test__";
        check (a.saveUserPreset (name, "Dark Space"), "Guardar preset JSON (.vdna schema 3)");
        auto file = VoxDnaProcessor::getUserPresetFolder().getChildFile (name + ".vdna");
        const auto json = juce::JSON::parse (file);
        check ((int) json.getProperty ("schema", 0) == VoxDnaProcessor::presetSchema && json.getProperty ("params", {}).isObject(), "Esquema JSON versionado");
        VoxDnaProcessor c;
        prepare (c, fs, block);
        c.loadUserPreset (file);
        int diff2 = 0;
        for (int i = 0; i < a.getParameters().size(); ++i)
        {
            auto* rp = dynamic_cast<juce::RangedAudioParameter*> (a.getParameters()[i]);
            if (rp->getParameterID() == "tuneKey" || rp->getParameterID() == "tuneScale") continue;
            if (rp->getCurrentValueAsText() != c.getParameters()[i]->getCurrentValueAsText()) ++diff2;
        }
        check (diff2 == 0, "Preset JSON recall (sin key/scale)", "diferencias: " + juce::String (diff2));

        // parámetro desconocido + ausente: no debe destruir el resto
        auto obj = juce::JSON::parse (file);
        obj.getProperty ("params", {}).getDynamicObject()->setProperty ("parametroFuturo", 1.0);
        obj.getProperty ("params", {}).getDynamicObject()->removeProperty ("revMix");
        file.replaceWithText (juce::JSON::toString (obj));
        check (c.loadUserPreset (file), "Preset con parametro desconocido/ausente se carga");
        file.deleteFile();
    }

    // ------------------------------------------------------------------- mono
    std::printf ("\n== MONO / FASE ==\n");
    {
        VoxDnaProcessor p;
        juce::AudioProcessor::BusesLayout mono;
        mono.inputBuses.add (juce::AudioChannelSet::mono());
        mono.outputBuses.add (juce::AudioChannelSet::mono());
        check (p.setBusesLayout (mono), "Layout mono -> mono aceptado");
        p.prepareToPlay (fs, block);
        p.setCurrentProgram (2);
        auto sig = vocalLike (fs, (int) fs, 200.0f, 20.0f);
        juce::MidiBuffer midi; bool finite = true;
        for (int pos = 0; pos + block <= (int) sig.size(); pos += block)
        {
            juce::AudioBuffer<float> buf (1, block);
            buf.copyFrom (0, 0, sig.data() + pos, block);
            p.processBlock (buf, midi);
            for (int i = 0; i < block; ++i) finite &= std::isfinite (buf.getSample (0, i));
        }
        check (finite, "Proceso mono estable");

        VoxDnaProcessor s;
        prepare (s, fs, block);
        s.setCurrentProgram (2);   // Astro Atmos con dobles
        std::vector<float> right;
        auto left = run (s, vocalLike (fs, (int) fs * 2, 200.0f, 20.0f), block, &right);
        check (s.correlation.value.load() > 0.3f, "Correlacion estereo mono-compatible", juce::String (s.correlation.value.load(), 2));
    }

    // ----------------------------------------------------------------- clicks
    std::printf ("\n== CLICKS AL CAMBIAR PRESETS / MODULOS ==\n");
    {
        VoxDnaProcessor p;
        prepare (p, fs, 128);
        allNeutral (p);
        std::vector<float> sine ((size_t) fs * 3);
        for (size_t i = 0; i < sine.size(); ++i) sine[i] = 0.3f * (float) std::sin (2 * M_PI * 220.0 * (double) i / fs);
        juce::MidiBuffer midi;
        float maxStep = 0, baseStep = 0; float prev = 0; int maxAt = 0, lastEventPos = 0; juce::String lastEvent;
        juce::String maxEvent;
        int presetIdx = 0;
        for (int pos = 0; pos + 128 <= (int) sine.size(); pos += 128)
        {
            if (pos > (int) fs && pos % 9600 == 0)
            {
                lastEventPos = pos;
                if ((pos / 9600) % 2 == 0)
                {
                    lastEvent = "preset " + p.getProgramName (presetIdx % 12); p.setCurrentProgram (presetIdx++ % 12);
                }
                else
                {
                    lastEvent = "toggle modulos";
                    for (auto id : { "eqOn", "compOn", "deessOn", "optoOn" })
                        setParam (p, id, (p.apvts.getRawParameterValue (id)->load() > 0.5f) ? 0.0f : 1.0f);
                }
            }
            juce::AudioBuffer<float> buf (2, 128);
            for (int i = 0; i < 128; ++i) { buf.setSample (0, i, sine[(size_t) (pos + i)]); buf.setSample (1, i, sine[(size_t) (pos + i)]); }
            p.processBlock (buf, midi);
            for (int i = 0; i < 128; ++i)
            {
                const float v = buf.getSample (0, i);
                const float step = std::abs (v - prev);
                prev = v;
                if (pos < (int) fs && pos > 9600) baseStep = std::max (baseStep, step);
                else if (pos >= (int) fs && step > maxStep) { maxStep = step; maxAt = pos + i; maxEvent = lastEvent + " (+" + juce::String (pos + i - lastEventPos) + " smp)"; }
            }
        }
        // un seno de 220 Hz a 0.9 de pico tiene un paso máximo ~0.026; un click lo supera claramente
        check (maxStep < 0.06f, "Sin discontinuidades al cambiar presets/modulos", "paso max " + juce::String (maxStep, 4) + " (base " + juce::String (baseStep, 4) + ") tras: " + maxEvent);
    }

    // --------------------------------------------------------------- aliasing
    std::printf ("\n== ALIASING (Rage Clip, seno 3.5 kHz, energia 400-3000 Hz) ==\n");
    {
        float alias[3] {};
        for (int q : { 0, 1, 2 })
        {
            VoxDnaProcessor p;
            prepare (p, fs, block);
            allNeutral (p);
            setParam (p, "quality", (float) q);
            setParam (p, "satOn", 1); setParam (p, "satMode", 4); setParam (p, "satDrive", 100); setParam (p, "satMix", 100);
            std::vector<float> sig ((size_t) fs);
            for (size_t i = 0; i < sig.size(); ++i) sig[i] = 0.25f * (float) std::sin (2 * M_PI * 3500.0 * (double) i / fs);
            auto out = run (p, sig, block);
            // energía en 400-3000 Hz (sin la fundamental) — productos de aliasing/intermod
            double e = 0;
            for (double f = 400; f < 3000; f += 50)
            {
                double re = 0, im = 0;
                for (int i = 0; i < 8192; ++i)
                {
                    const double w = 0.5 - 0.5 * std::cos (2 * M_PI * i / 8192);
                    re += w * out[(size_t) (24000 + i)] * std::cos (2 * M_PI * f * i / fs);
                    im += w * out[(size_t) (24000 + i)] * std::sin (2 * M_PI * f * i / fs);
                }
                e += re * re + im * im;
            }
            alias[q] = (float) (10.0 * std::log10 (e + 1e-20));
        }
        std::printf ("  MONITOR %.1f dB  MIX %.1f dB  RENDER HQ %.1f dB\n", alias[0], alias[1], alias[2]);
        check (alias[1] < alias[0] - 3.0f && alias[2] <= alias[1] + 1.0f, "Oversampling reduce el aliasing");
    }



    // ---------------------------------------------------------- v2.2 Tune Edition
    std::printf ("\n== DNA TUNE v2.2 (voz, octavas, consonantes, transiciones, flex, formantes) ==\n");
    {
        auto tuneSetup = [&] (VoxDnaProcessor& p, int quality)
        {
            prepare (p, fs, 256);
            allNeutral (p);
            setParam (p, "quality", (float) quality);
            run (p, std::vector<float> ((size_t) fs / 4, 0.0f), 256);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            setParam (p, "tuneOn", 1); setParam (p, "tuneScale", 0);
            setParam (p, "tuneRetune", 0); setParam (p, "tuneHumanize", 0);
        };

        // 1) consonantes: una ráfaga de ruido entre dos vocales debe salir intacta (sin shifter)
        {
            VoxDnaProcessor p; tuneSetup (p, 1);
            auto a = vocalLike (fs, (int) (0.4 * fs), 220.0f * std::pow (2.0f, 0.4f / 12.0f), 0.0f);
            std::vector<float> sig (a);
            std::mt19937 rng (21);
            const int noiseStart = (int) sig.size(), noiseLen = (int) (0.12 * fs);
            for (int i = 0; i < noiseLen; ++i) sig.push_back (0.08f * ((float) (rng() % 20001) / 10000.0f - 1.0f));
            sig.insert (sig.end(), a.begin(), a.end());
            sig.resize (sig.size() + (size_t) fs / 2, 0.0f);
            auto out = run (p, sig, 256);
            const int rep = p.getLatencySamples();
            double num = 0, e1 = 0, e2 = 0;
            for (int i = noiseStart + (int) (0.03 * fs); i < noiseStart + noiseLen - (int) (0.01 * fs); ++i)
            {
                const double x = sig[(size_t) i], y = out[(size_t) (i + rep)];
                num += x * y; e1 += x * x; e2 += y * y;
            }
            const double corr = num / std::sqrt (e1 * e2 + 1e-20);
            check (corr > 0.9, "Consonante (ruido) intacta entre vocales con hard-tune", "correlacion con la entrada " + juce::String (corr, 3));
        }

        // 2) errores de octava: fundamental débil (-20 dB respecto al 2o armónico)
        {
            VoxDnaProcessor p; tuneSetup (p, 1);
            const float f0 = 130.81f;   // C3
            auto sig = toneWith (fs, (int) fs * 3, [&] (double) { return (double) f0; }, { 0.1f, 1.0f, 0.6f, 0.4f, 0.3f, 0.2f }, 25.0f);
            juce::MidiBuffer midi;
            int blocks = 0, octaveErr = 0;
            const float trueMidi = 69.0f + 12.0f * std::log2 (f0 / 440.0f);
            for (int pos = 0; pos + 256 <= (int) sig.size(); pos += 256)
            {
                juce::AudioBuffer<float> buf (2, 256);
                for (int i = 0; i < 256; ++i) { buf.setSample (0, i, sig[(size_t) (pos + i)]); buf.setSample (1, i, sig[(size_t) (pos + i)]); }
                p.processBlock (buf, midi);
                const float d = p.detectedMidi.load();
                if (pos > (int) (0.3 * fs) && d > 0) { ++blocks; if (std::abs (d - trueMidi) > 6.0f) ++octaveErr; }
            }
            check (blocks > 400 && octaveErr * 100 < blocks * 2, "Sin errores de octava con fundamental debil",
                   juce::String (octaveErr) + " de " + juce::String (blocks) + " bloques fuera de octava");
        }

        // 3) Flex-Tune
        {
            struct FlexCase { float devCents, flex, expectMaxCents, expectMinCents; const char* name; };
            const FlexCase cases[] {
                { 45.0f, 0.0f,   6.0f,  0.0f,  "Flex 0: desviacion de +45 c corregida" },
                { 45.0f, 100.0f, 99.0f, 35.0f, "Flex 100: +45 c se respeta (expresion)" },
                { 4.0f,  100.0f, 2.5f,  0.0f,  "Flex 100: +4 c si se corrige" } };
            for (auto& c : cases)
            {
                VoxDnaProcessor p; tuneSetup (p, 1);
                setParam (p, "tuneFlex", c.flex);
                auto out = run (p, vocalLike (fs, (int) fs * 2, 220.0f * std::pow (2.0f, c.devCents / 1200.0f), 0.0f), 256);
                const float cc = std::abs (cents (pitchIn (out, fs, (int) fs, 4096, 180.0f, 270.0f), 220.0f));
                check (cc <= c.expectMaxCents && cc >= c.expectMinCents, c.name, "salida a " + juce::String (cc, 1) + " c de A3");
            }
        }

        // 4) Transition: salto A3 -> C4 en t = 1 s
        {
            const float A3 = 220.0f, C4 = 261.63f;
            auto jumpSig = vocalLike (fs, 1, 1.0f, 0.0f);
            auto sig = toneWith (fs, (int) fs * 2, [&] (double t) { return t < 1.0 ? (double) A3 : (double) C4; }, { 1.0f, 0.6f, 0.4f, 0.3f, 0.2f });
            float atFast = 0, atSlowMid = 0, atSlowEnd = 0;
            for (float tr : { 0.0f, 150.0f })
            {
                VoxDnaProcessor p; tuneSetup (p, 1);
                setParam (p, "tuneTransition", tr);
                setParam (p, "tuneRetune", 0);
                auto out = run (p, sig, 256);
                const int rep = p.getLatencySamples();
                auto at = [&] (double secAfter) { return std::abs (cents (pitchIn (out, fs, (int) ((1.0 + secAfter) * fs) + rep, 960, 200.0f, 290.0f), C4)); };
                if (tr == 0.0f) atFast = at (0.045);
                else { atSlowMid = at (0.06); atSlowEnd = at (0.35); }
            }
            check (atFast < 35.0f, "Transition 0 ms: salto inmediato a C4", pv::ui::num (atFast, 0) + " c de C4 a los 45 ms");
            check (atSlowMid > 60.0f && atSlowEnd < 15.0f, "Transition 150 ms: deslizamiento y llegada",
                   pv::ui::num (atSlowMid, 0) + " c a los 60 ms, " + juce::String (atSlowEnd, 1) + " c a los 350 ms");
        }

        // 5) Formant Preserve: corrección de 1 semitono (A#3 en Do mayor) sin mover el timbre
        {
            float ratioOff = 0, ratioOn = 0;
            auto sig = vocalLike (fs, (int) fs * 2, 233.08f, 0.0f);
            const float cIn = centroid (sig, fs, (int) fs, 4096);
            for (int preserve = 0; preserve < 2; ++preserve)
            {
                VoxDnaProcessor p; tuneSetup (p, 1);
                setParam (p, "tuneScale", 1);   // Do mayor: A# no pertenece -> se corrige a A o B
                setParam (p, "tuneFormantPreserve", (float) preserve);
                auto out = run (p, sig, 256);
                const float c = centroid (out, fs, (int) fs + p.getLatencySamples(), 4096);
                (preserve ? ratioOn : ratioOff) = c / cIn;
            }
            check (std::abs (ratioOff - 1.0f) > 0.035f && std::abs (ratioOn - 1.0f) < std::abs (ratioOff - 1.0f) * 0.5f,
                   "Formant Preserve mantiene el timbre al corregir 1 st",
                   "centroide sin preserve " + juce::String ((ratioOff - 1.0f) * 100.0f, 1) + " %, con preserve " + juce::String ((ratioOn - 1.0f) * 100.0f, 1) + " %");
        }

        // 6) MONITOR: nota grave con la ventana corta
        {
            VoxDnaProcessor p; tuneSetup (p, 0);
            setParam (p, "tuneRetune", 10);
            auto out = run (p, vocalLike (fs, (int) fs * 2, 98.0f * std::pow (2.0f, 0.3f / 12.0f), 0.0f), 256);
            const float err = cents (pitchIn (out, fs, (int) fs, 6000, 85.0f, 115.0f), 98.0f);
            check (std::abs (err) < 8.0f, "MONITOR afina una voz grave (G2 +30 c)", "error " + juce::String (err, 1) + " c, latencia "
                   + juce::String (p.getLatencySamples() * 1000.0 / fs, 2) + " ms");
        }
    }

    // ---------------------------------------------------------------- true peak
    std::printf ("\n== TRUE PEAK (limitador -1 dBTP, auditado con sobremuestreo 16x) ==\n");
    {
        struct Sig { const char* name; std::function<float (int)> fn; };
        const double sr = 48000.0;
        const Sig sigs[] {
            { "Seno fs/4 a 45 grados (+3 dB entre muestras), +6 dBFS", [&] (int i) { return 2.0f * (float) std::sin (M_PI * 0.5 * i + M_PI / 4); } },
            { "Seno 18 kHz +6 dBFS",                                   [&] (int i) { return 2.0f * (float) std::sin (2 * M_PI * 18000.0 * i / sr + 0.3); } },
            { "Onda cuadrada 997 Hz +6 dBFS",                          [&] (int i) { return std::sin (2 * M_PI * 997.0 * i / sr) >= 0 ? 2.0f : -2.0f; } },
            { "Ruido banda completa hasta 19 kHz (Butterworth 8o), +10 dBFS", [&] (int i) {
                  static std::mt19937 r (5);
                  static juce::dsp::IIR::Filter<float> f[4];
                  if (i == 0)
                  {
                      const float qs[] { 0.5098f, 0.6013f, 0.9000f, 2.5629f };
                      for (int k = 0; k < 4; ++k) { f[k].coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (48000.0, 19000.0f, qs[k]); f[k].reset(); }
                  }
                  float x = 3.0f * ((float) (r() % 20001) / 10000.0f - 1.0f);
                  for (auto& ff : f) x = ff.processSample (x);
                  return x * 1.5f; } } };
        for (auto& sg : sigs)
        {
            VoxDnaProcessor p;
            prepare (p, sr, 256);
            allNeutral (p);
            setParam (p, "deessOn", 0);
            std::vector<float> x ((size_t) sr);
            for (int i = 0; i < (int) x.size(); ++i) x[(size_t) i] = sg.fn (i);
            auto out = run (p, x, 256);
            std::vector<float> tail (out.begin() + 4800, out.end());
            const float tp = referenceTruePeakDb (tail);
            check (tp <= -0.95f, sg.name, "true peak " + juce::String (tp, 2) + " dBTP");
        }
        {   // límite documentado: ruido blanco hasta Nyquist (informativo, no bloqueante)
            VoxDnaProcessor p;
            prepare (p, sr, 256);
            allNeutral (p);
            std::mt19937 r (5);
            std::vector<float> x ((size_t) sr);
            for (auto& v : x) v = 3.0f * ((float) (r() % 20001) / 10000.0f - 1.0f);
            auto out = run (p, x, 256);
            std::vector<float> tail (out.begin() + 4800, out.end());
            std::printf ("  [INFO] Ruido blanco hasta Nyquist (limite documentado): %.2f dBTP\n", referenceTruePeakDb (tail));
        }
    }

    // ---------------------------------------------------------------- LUFS
    std::printf ("\n== SONORIDAD BS.1770-4 (senales EBU Tech 3341) ==\n");
    {
        auto feed = [] (pv::LoudnessMeter& m, double sr, float dbfs, double seconds, float freq = 1000.0f)
        {
            const float a = std::pow (10.0f, dbfs / 20.0f);
            const int n = (int) std::round (seconds * sr);
            std::vector<float> buf (512);
            static double ph = 0;
            for (int pos = 0; pos < n; pos += 512)
            {
                const int b = std::min (512, n - pos);
                for (int i = 0; i < b; ++i) { buf[(size_t) i] = a * (float) std::sin (ph); ph += 2 * M_PI * freq / sr; }
                m.process (buf.data(), buf.data(), b);
            }
        };
        struct Case { const char* name; double sr; std::vector<std::pair<float, double>> segs; float expectI, expectM; };
        const Case cases[] {
            { "Caso 1: seno 1 kHz -23 dBFS, 20 s (48 kHz)",   48000.0, { { -23.0f, 20.0 } }, -23.0f, -23.0f },
            { "Caso 1: seno 1 kHz -23 dBFS, 20 s (44.1 kHz)", 44100.0, { { -23.0f, 20.0 } }, -23.0f, -23.0f },
            { "Caso 1: seno 1 kHz -23 dBFS, 20 s (96 kHz)",   96000.0, { { -23.0f, 20.0 } }, -23.0f, -23.0f },
            { "Caso 2: seno 1 kHz -33 dBFS, 20 s",            48000.0, { { -33.0f, 20.0 } }, -33.0f, -33.0f },
            { "Caso 3: -36/-23/-36 dBFS (10/60/10 s)",        48000.0, { { -36.0f, 10.0 }, { -23.0f, 60.0 }, { -36.0f, 10.0 } }, -23.0f, -99.0f },
            { "Caso 4: -72/-36/-23/-36/-72 (gate abs.)",      48000.0, { { -72.0f, 10.0 }, { -36.0f, 10.0 }, { -23.0f, 60.0 }, { -36.0f, 10.0 }, { -72.0f, 10.0 } }, -23.0f, -99.0f },
            { "Caso 5: -26/-20/-26 dBFS (20/20.1/20 s)",      48000.0, { { -26.0f, 20.0 }, { -20.0f, 20.1 }, { -26.0f, 20.0 } }, -23.0f, -99.0f } };
        for (auto& c : cases)
        {
            pv::LoudnessMeter m;
            m.prepare (c.sr);
            for (auto& [db, sec] : c.segs) feed (m, c.sr, db, sec);
            const float I = m.integrated.load(), M = m.momentary.load(), S = m.shortTerm.load();
            bool ok = std::abs (I - c.expectI) <= 0.1f;
            if (c.expectM > -90.0f) ok = ok && std::abs (M - c.expectM) <= 0.1f && std::abs (S - c.expectM) <= 0.1f;
            check (ok, c.name, "I " + juce::String (I, 2) + "  M " + juce::String (M, 2) + "  S " + juce::String (S, 2) + " LUFS");
        }
    }

    // ------------------------------------------------- latencia audible real
    std::printf ("\n== LATENCIA AUDIBLE REAL con DNA TUNE corrigiendo (envolvente) ==\n");
    {
        for (double sr : { 44100.0, 48000.0, 96000.0 })
            for (int q : { 0, 1, 2 })
            {
                LagResult lags[2] {};
                double onsetLag[2] {};
                int rep = 0;
                for (int tuneOn = 0; tuneOn < 2; ++tuneOn)
                {
                    VoxDnaProcessor p;
                    prepare (p, sr, 256);
                    allNeutral (p);
                    setParam (p, "quality", (float) q);
                    run (p, std::vector<float> ((size_t) sr / 4, 0.0f), 256);   // aplicar el modo
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (120);
                    setParam (p, "tuneOn", (float) tuneOn); setParam (p, "tuneScale", 0);
                    setParam (p, "tuneRetune", 10); setParam (p, "tuneHumanize", 0);
                    auto in = syllables (sr, (int) sr * 2, 220.0f * std::pow (2.0f, 0.3f / 12.0f));   // +30 cents: el tune corrige
                    auto out = run (p, in, 256);
                    lags[tuneOn] = envelopeLag (in, out, sr, (int) (0.1 * sr));
                    onsetLag[tuneOn] = onsetDelay (in, out, sr);
                    rep = p.getLatencySamples();
                }
                // tune OFF: exacto. Tune ON: el ATAQUE de cada sílaba debe salir a la latencia
                // reportada (+-0.5 ms); la deriva en notas sostenidas (+-W/2) se informa aparte.
                const double wmax = (q == 0 ? 0.011 : 0.06) * sr;
                const bool ok = std::abs (onsetLag[1] - rep) <= 0.0005 * sr && lags[0].peak == rep
                                && std::abs (lags[1].mean - rep) <= wmax / 2.0;
                check (ok, (juce::String (q == 0 ? "MONITOR  " : q == 1 ? "MIX      " : "RENDER HQ") + " @ " + juce::String (sr / 1000.0, 1) + " kHz").toRawUTF8(),
                       "reportada " + juce::String (rep * 1000.0 / sr, 2) + " ms | tune ON: ataques " + juce::String ((onsetLag[1] - rep) * 1000.0 / sr, 2)
                           + " ms, deriva media en notas sostenidas " + juce::String ((lags[1].mean - rep) * 1000.0 / sr, 2) + " ms (limite +-"
                           + juce::String (wmax * 500.0 / sr, 1) + ")");
            }
    }

    // ------------------------------------------- cambio de calidad en reproducción
    std::printf ("\n== CAMBIO DE MODO DE CALIDAD DURANTE LA REPRODUCCION ==\n");
    {
        VoxDnaProcessor p;
        prepare (p, fs, 128);
        p.setCurrentProgram (2);
        setParam (p, "tuneOn", 1);
        std::vector<float> sig ((size_t) fs * 4);
        for (size_t i = 0; i < sig.size(); ++i) sig[i] = 0.3f * (float) std::sin (2 * M_PI * 220.0 * (double) i / fs);
        juce::MidiBuffer midi;
        float prev = 0, maxStep = 0; bool finite = true;
        int silentRun = 0, maxSilent = 0, switches = 0, latencyOk = 0;
        const int seq[] { 0, 2, 1, 0, 1, 2, 0 };
        for (int pos = 0, k = 0; pos + 128 <= (int) sig.size(); pos += 128, ++k)
        {
            if (k > 300 && k % 160 == 0 && switches < 7)
            {
                setParam (p, "quality", (float) seq[switches]);   // como si el usuario lo cambiara con el transporte en marcha
                ++switches;
            }
            juce::AudioBuffer<float> buf (2, 128);
            for (int i = 0; i < 128; ++i) { buf.setSample (0, i, sig[(size_t) (pos + i)]); buf.setSample (1, i, sig[(size_t) (pos + i)]); }
            {
                AudioScope scope;
                p.processBlock (buf, midi);
            }
            for (int i = 0; i < 128; ++i)
            {
                const float v = buf.getSample (0, i);
                finite &= std::isfinite (v);
                maxStep = std::max (maxStep, std::abs (v - prev));
                prev = v;
                silentRun = std::abs (v) < 1e-4f ? silentRun + 1 : 0;
                if (k > 300) maxSilent = std::max (maxSilent, silentRun);
            }
            if (k % 8 == 0)
            {
                juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
                if (! p.isQualityTransitioning() && p.getLatencySamples() == p.computeLatency (p.getQuality())) ++latencyOk;
            }
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil (150);
        check (finite && maxStep < 0.08f, "Sin clicks ni NaN en 7 cambios de modo", "paso max " + juce::String (maxStep, 4));
        check (maxSilent < (int) (0.15 * fs), "Hueco de silencio acotado por cambio", juce::String (maxSilent * 1000.0 / fs, 1) + " ms max");
        check (p.getLatencySamples() == p.computeLatency (p.getQuality()) && p.getQuality() == 0,
               "Latencia reportada al host actualizada", juce::String (p.getLatencySamples()) + " smp en modo final MONITOR");
        check (g_audioAllocs.load() == 0, "Cambios de modo sin asignaciones en audio", juce::String ((int) g_audioAllocs.load()));
    }

    // ---------------------------------------------- restauración de interruptores
    std::printf ("\n== RESTAURACION DE ESTADO CON VALORES INTERMEDIOS (caso pluginval) ==\n");
    {
        VoxDnaProcessor a;
        prepare (a, fs, 256);
        std::mt19937 rng (99);
        std::vector<float> saved;
        for (auto* prm : a.getParameters()) { prm->setValueNotifyingHost ((float) (rng() % 1000) / 999.0f); }
        juce::MemoryBlock mb;
        a.getStateInformation (mb);
        for (auto* prm : a.getParameters()) saved.push_back (prm->getValue());
        for (auto* prm : a.getParameters()) prm->setValueNotifyingHost ((float) (rng() % 1000) / 999.0f);   // el host "ensucia" los valores
        a.setStateInformation (mb.getData(), (int) mb.getSize());
        int bad = 0; juce::String names;
        for (int i = 0; i < a.getParameters().size(); ++i)
        {
            auto* rp = dynamic_cast<juce::RangedAudioParameter*> (a.getParameters()[i]);
            const float expect = rp->convertTo0to1 (rp->convertFrom0to1 (saved[(size_t) i]));
            if (std::abs (rp->getValue() - expect) > 0.01f) { ++bad; if (bad <= 4) names << rp->getName (20) << " "; }
        }
        check (bad == 0, "Todos los parametros restaurados (bools/choices incluidos)", juce::String (bad) + " fallos " + names);
    }

    // -------------------------------------------------------------------- CPU
    // =====================================================================
    std::printf ("\n== DNA ANALOG v2.3: MODELOS DE CIRCUITO ==\n");
    {
        const double ofs = 192000.0;   // tasa interna en RENDER HQ (48 kHz x4)
        const char* names[] = { "Tube (12AX7)", "Tape (J-A)", "Preamp A (WDF)", "Console (WDF)" };
        for (int m = 0; m < 4; ++m)
        {
            auto y = colorRun ((pv::ColorMode) m, 0.5f, 0.001f, ofs);
            const double g = 20.0 * std::log10 (harmonics (y, ofs, 1000.0).h1 / 0.001);
            check (std::abs (g) < 0.5, (juce::String ("Ganancia de pequena senal 0 dB: ") + names[m]).toRawUTF8(), juce::String (g, 2) + " dB");
        }
        auto tube = harmonics (colorRun (pv::ColorMode::tube, 0.6f, 0.5f, ofs), ofs, 1000.0);
        check (tube.h2 > tube.h3, "Tube: 2.o armonico > 3.o (triodo asimetrico)", "H2 " + juce::String (20 * std::log10 (tube.h2 / tube.h1), 1) + " dBc, H3 " + juce::String (20 * std::log10 (tube.h3 / tube.h1), 1) + " dBc");
        auto pre = harmonics (colorRun (pv::ColorMode::preampA, 0.6f, 0.5f, ofs), ofs, 1000.0);
        check (pre.h2 / pre.h3 > 0.5, "Preamp A: armonicos pares presentes (diodos asimetricos)", "H2/H3 " + juce::String (pre.h2 / pre.h3, 2));
        auto tape = harmonics (colorRun (pv::ColorMode::tape, 0.6f, 0.5f, ofs), ofs, 1000.0);
        check (tape.h3 > tape.h2, "Tape: 3.o armonico > 2.o (histeresis simetrica)", "H3 " + juce::String (20 * std::log10 (tape.h3 / tape.h1), 1) + " dBc, H2 " + juce::String (20 * std::log10 (tape.h2 / tape.h1 + 1e-12), 1) + " dBc");
        auto con = harmonics (colorRun (pv::ColorMode::console, 0.5f, 0.126f, ofs), ofs, 1000.0);
        check (con.thd < 0.003, "Console: THD < 0.3 % a -18 dBFS (color sutil)", juce::String (con.thd * 100.0, 3) + " %");

        for (int m = 0; m < 4; ++m)
        {
            double prev = -1; bool mono = true; juce::String d;
            for (float drv : { 0.2f, 0.5f, 0.8f })
            {
                const double t = harmonics (colorRun ((pv::ColorMode) m, drv, 0.3f, ofs), ofs, 1000.0).thd;
                mono = mono && t > prev; prev = t; d << juce::String (t * 100.0, 2) << "% ";
            }
            check (mono, (juce::String ("THD crece con el drive: ") + names[m]).toRawUTF8(), d);
        }

        // histéresis: con la misma H, M depende de la dirección (lazo con área)
        pv::circuits::TapeHysteresis hy; hy.prepare();
        double up = 0, down = 0;
        for (int i = 0; i < 4000; ++i)
        {
            const double ph = 2.0 * M_PI * i / 1000.0;
            const double mm = hy.process (40000.0 * std::sin (ph));
            if (i >= 3000) { if (i % 1000 == 0) up = mm; if (i % 1000 == 500) down = mm; }
        }
        check (up < 0 && down > 0, "Tape: lazo de histeresis (remanencia con H = 0)", "M(H=0, subiendo) " + juce::String (up / 3.5e5, 3) + " Ms, bajando " + juce::String (down / 3.5e5, 3) + " Ms");

        // aliasing del triodo con drive alto en RENDER HQ (proceso completo)
        {
            VoxDnaProcessor p; prepare (p, fs, 256); allNeutral (p);
            setParam (p, "quality", 2); setParam (p, "satOn", 1); setParam (p, "satMode", 0); setParam (p, "satDrive", 90); setParam (p, "satMix", 100);
            const int n = (int) fs * 2;
            std::vector<float> sig ((size_t) n);
            for (int i = 0; i < n; ++i) sig[(size_t) i] = 0.4f * (float) std::sin (2.0 * M_PI * 7919.0 * i / fs);
            run (p, std::vector<float> ((size_t) fs, 0.0f), 256);
            auto y = run (p, sig, 256);
            // energía fuera de los armónicos de 7919 Hz (los alias caen en otras frecuencias)
            double worst = 0;
            const double fund = harmonicAmp (y, fs, 7919.0, n - (int) fs / 2, (int) fs / 2);
            for (int k = 4; k <= 9; ++k)   // armónicos por encima de Nyquist -> frecuencia plegada
            {
                double f = std::fmod (k * 7919.0, fs); if (f > fs / 2) f = fs - f;
                worst = std::max (worst, harmonicAmp (y, fs, f, n - (int) fs / 2, (int) fs / 2));
            }
            const double db = 20.0 * std::log10 (worst / fund + 1e-12);
            check (db < -60.0, "Tube a drive 90 % sin aliasing audible (HQ, 7.9 kHz)", "peor alias " + juce::String (db, 1) + " dBc");
        }
    }

    std::printf ("\n== DNA ANALOG v2.3: COMPRESORES FET / VCA / OPTO ==\n");
    {
        // GR frente a tiempo con una señal en escalón
        auto stepGr = [&] (auto& c, float lowDb, float highDb, int holdSamples, int tailSamples)
        {
            std::vector<float> gr;
            for (int i = 0; i < (int) fs / 2; ++i) c.process (juce::Decibels::decibelsToGain (lowDb));
            for (int i = 0; i < holdSamples; ++i) { c.process (juce::Decibels::decibelsToGain (highDb)); gr.push_back (c.getGainReductionDb()); }
            for (int i = 0; i < tailSamples; ++i) { c.process (juce::Decibels::decibelsToGain (lowDb)); gr.push_back (c.getGainReductionDb()); }
            return gr;
        };
        auto timeTo = [] (const std::vector<float>& g, int from, int to, float target, bool rising)
        {
            for (int i = from; i < to; ++i) if (rising ? g[(size_t) i] >= target : g[(size_t) i] <= target) return i - from;
            return to - from;
        };

        pv::Compressor fet; fet.prepare (fs); fet.setModel (pv::Compressor::fet); fet.setParameters (-24, 4, 0.8f, 80, 0);
        pv::OptoLeveler opto; opto.prepare (fs); opto.setParameters (60, 0);
        auto gF = stepGr (fet, -40, -6, (int) fs, 0);
        auto gO = stepGr (opto, -40, -6, (int) fs, 0);
        const float finF = gF.back(), finO = gO.back();
        const int tF = timeTo (gF, 0, (int) gF.size(), 0.63f * finF, true), tO = timeTo (gO, 0, (int) gO.size(), 0.63f * finO, true);
        check (tF * 4 < tO, "FET mas rapido que el opto (63 % de la GR final)", "FET " + juce::String (1000.0 * tF / fs, 2) + " ms, opto " + juce::String (1000.0 * tO / fs, 1) + " ms");

        // curva estática: VCA y FET realimentado comparten la misma curva (calibración de presets)
        for (auto model : { pv::Compressor::vca, pv::Compressor::fet })
        {
            pv::Compressor c; c.prepare (fs); c.setModel (model); c.setParameters (-20, 4, 1, 80, 0);
            for (int i = 0; i < (int) fs; ++i) c.process (juce::Decibels::decibelsToGain (-8.0f));
            const float g = c.getGainReductionDb();
            check (std::abs (g - 9.0f) < 0.6f, model == pv::Compressor::vca ? "VCA: curva estatica (-8 dB, umbral -20, 4:1 -> 9 dB GR)" : "FET realimentado: misma curva estatica",
                   juce::String (g, 2) + " dB");
        }

        // release dependiente del programa (FET): ráfaga corta vs larga
        auto relTime = [&] (int burst)
        {
            pv::Compressor c; c.prepare (fs); c.setModel (pv::Compressor::fet); c.setParameters (-24, 4, 0.8f, 80, 0);
            auto g = stepGr (c, -40, -6, burst, (int) fs * 2);
            return 1000.0 * timeTo (g, burst, (int) g.size(), 1.0f, false) / fs;
        };
        const double rShort = relTime ((int) (0.05 * fs)), rLong = relTime ((int) (3.0 * fs));
        check (rLong > rShort * 1.8, "FET: release dependiente del programa", "hasta 1 dB de GR: rafaga 50 ms -> " + pv::ui::num (rShort, 0) + " ms, rafaga 3 s -> " + pv::ui::num (rLong, 0) + " ms");

        auto optoRel = [&] (int burst)
        {
            pv::OptoLeveler o; o.prepare (fs); o.setParameters (60, 0);
            auto g = stepGr (o, -40, -6, burst, (int) fs * 8);
            return 1000.0 * timeTo (g, burst, (int) g.size(), 0.5f, false) / fs;
        };
        const double oShort = optoRel ((int) (0.1 * fs)), oLong = optoRel ((int) (5.0 * fs));
        check (oLong > oShort * 2.0, "Opto T4: memoria de la celula (release mas lento tras compresion larga)", "hasta 0.5 dB: rafaga 100 ms -> " + pv::ui::num (oShort, 0) + " ms, rafaga 5 s -> " + pv::ui::num (oLong, 0) + " ms");

        // FET: el 2.º armónico crece con la reducción
        auto fetH2 = [&] (float thr, pv::Compressor::Model model)
        {
            pv::Compressor c; c.prepare (fs); c.setModel (model); c.setParameters (thr, 4, 0.8f, 80, 0);
            std::vector<float> y ((size_t) fs);
            for (size_t i = 0; i < y.size(); ++i)
            {
                const float x = 0.5f * (float) std::sin (2.0 * M_PI * 220.0 * (double) i / fs);
                y[i] = c.applyColour (0, x, c.process (std::abs (x)));
            }
            auto h = harmonics (y, fs, 220.0);
            return 20.0 * std::log10 (h.h2 / h.h1 + 1e-12);
        };
        const double h2none = fetH2 (0, pv::Compressor::fet), h2gr = fetH2 (-30, pv::Compressor::fet), h2vca = fetH2 (-30, pv::Compressor::vca);
        check (h2gr > h2none + 20.0 && h2gr > h2vca + 20.0, "FET: 2.o armonico crece con la reduccion (VCA limpio)",
               "sin GR " + pv::ui::num (h2none, 0) + " dBc, con ~18 dB GR " + juce::String (h2gr, 1) + " dBc, VCA " + pv::ui::num (h2vca, 0) + " dBc");
    }

    // =====================================================================
    std::printf ("\n== v3.0 STUDIO: CAPAS, ARMONIZADOR Y RUTEO POR CAPA ==\n");
    auto energyDb = [] (const std::vector<float>& x, int a, int b)
    {
        b = juce::jmin (b, (int) x.size());   // run() procesa bloques completos: la salida puede ser algo más corta
        double e = 0; for (int i = a; i < b; ++i) e += (double) x[(size_t) i] * x[(size_t) i];
        return 10.0 * std::log10 (e / juce::jmax (1, b - a) + 1e-20);
    };
    auto layerOnly = [&] (VoxDnaProcessor& p, int layer)
    {
        allNeutral (p);
        for (int l = 0; l < pv::numLayers; ++l) setParam (p, "layOn" + juce::String (l), l == layer ? 1.0f : 0.0f);
        setParam (p, "layLvl" + juce::String (layer), 0);
        setParam (p, "layPan" + juce::String (layer), 0);
    };
    {
        // armonizador diatónico: A3 en La menor
        struct Case { int interval, key, scale; float expectSemis; const char* what; };
        const Case cases[] { { 0, 9, 2, 3.0f, "3rd Up en La menor: A3 -> C4 (+3 st)" },
                             { 1, 9, 2, 7.0f, "5th Up en La menor: A3 -> E4 (+7 st)" },
                             { 2, 9, 2, -4.0f, "3rd Down en La menor: A3 -> F3 (-4 st)" },
                             { 0, 9, 1, 4.0f, "3rd Up en La mayor: A3 -> C#4 (+4 st)" },
                             { 4, 0, 1, 9.0f, "6th Up en Do mayor: A3 -> F#? no: A3 -> F4 (+8 st)" } };
        for (const auto& c : cases)
        {
            VoxDnaProcessor p; prepare (p, fs, 256);
            layerOnly (p, pv::LayerEngine::harmony);
            setParam (p, "tuneKey", (float) c.key); setParam (p, "tuneScale", (float) c.scale);
            setParam (p, "harmInterval", (float) c.interval);
            auto sig = toneWith (fs, (int) fs * 2, [] (double) { return 220.0; }, { 1.0f, 0.5f, 0.3f, 0.2f });
            auto y = run (p, sig, 256);
            const float expect = c.interval == 4 ? 8.0f : c.expectSemis;   // A en Do mayor: 6.ª diatónica = F (+8)
            const float f = pitchIn (y, fs, (int) fs, 4096, 80.0f, 700.0f);
            const float st = 12.0f * std::log2 (f / 220.0f);
            check (std::abs (st - expect) < 0.2f, c.interval == 4 ? "6th Up en Do mayor: A3 -> F4 (+8 st)" : c.what,
                   juce::String (f, 1) + " Hz (" + juce::String (st, 2) + " st)");
        }
        // octava
        for (int mode : { 0, 1 })
        {
            VoxDnaProcessor p; prepare (p, fs, 256);
            layerOnly (p, pv::LayerEngine::octave);
            setParam (p, "octMode", (float) mode);
            auto sig = toneWith (fs, (int) fs * 2, [] (double) { return 220.0; }, { 1.0f, 0.4f, 0.2f });
            auto y = run (p, sig, 256);
            const float f = mode == 0 ? pitchIn (y, fs, (int) fs, 4096, 60.0f, 300.0f) : pitchIn (y, fs, (int) fs, 4096, 300.0f, 900.0f);
            check (std::abs (12.0f * std::log2 (f / (mode == 0 ? 110.0f : 440.0f))) < 0.3f,
                   mode == 0 ? "OCTAVE -1: 220 Hz -> 110 Hz" : "OCTAVE +1: 220 Hz -> 440 Hz", juce::String (f, 1) + " Hz");
        }
        // filtros y drive por capa (canal de la capa LEAD)
        {
            VoxDnaProcessor p; prepare (p, fs, 256);
            layerOnly (p, 0);
            std::vector<float> sig ((size_t) fs);
            for (size_t i = 0; i < sig.size(); ++i) sig[i] = 0.3f * (float) std::sin (2 * M_PI * 100.0 * i / fs);
            auto y0 = run (p, sig, 256);
            setParam (p, "layHp0", 1000);
            auto y1 = run (p, sig, 256);
            const double att = energyDb (y0, (int) fs / 2, (int) fs) - energyDb (y1, (int) fs / 2, (int) fs);
            check (att > 30.0, "HPF por capa (LEAD a 1 kHz): 100 Hz atenuado", juce::String (att, 1) + " dB");
        }
        // envío de reverb por capa: LEAD con envío 0 -> sin cola
        {
            auto tail = [&] (float send)
            {
                VoxDnaProcessor p; prepare (p, fs, 256);
                layerOnly (p, 0);
                setParam (p, "revOn", 1); setParam (p, "revMix", 40); setParam (p, "revDuck", 0);
                setParam (p, "layRev0", send);
                std::vector<float> sig ((size_t) fs * 2, 0.0f);
                for (int i = 0; i < (int) fs / 2; ++i) sig[(size_t) i] = 0.3f * (float) std::sin (2 * M_PI * 440.0 * i / fs);
                auto y = run (p, sig, 256);
                return energyDb (y, (int) (fs * 0.8), (int) (fs * 1.2));
            };
            const double withSend = tail (100), noSend = tail (0);
            check (withSend - noSend > 40.0, "Envio de reverb por capa (100 % vs 0 %)", "cola " + juce::String (withSend, 1) + " dB vs " + juce::String (noSend, 1) + " dB");
        }
    }

    std::printf ("\n== v3.0 STUDIO: MODOS DE RUTEO DE EFECTOS ==\n");
    {
        auto renderRoute = [&] (int route, bool dly, bool rev, float toneHz, float revDecay, int dlyIdx)
        {
            VoxDnaProcessor p; prepare (p, fs, 256);
            layerOnly (p, 0);
            setParam (p, "routeMode", (float) route);
            if (dly) { setParam (p, "dlyOn", 1); setParam (p, "dlyMix", 50); setParam (p, "dlyFb", 0); setParam (p, "dlyDuck", 0); setParam (p, "dlyTime", (float) dlyIdx); }
            if (rev) { setParam (p, "revOn", 1); setParam (p, "revMix", 40); setParam (p, "revDuck", 0); setParam (p, "revDecay", revDecay); setParam (p, "revType", 2); }
            std::vector<float> sig ((size_t) fs * 2, 0.0f);
            for (int i = 0; i < (int) (fs * 0.15); ++i) sig[(size_t) i] = 0.3f * (float) std::sin (2 * M_PI * toneHz * i / fs);
            std::vector<float> r;
            auto l = run (p, sig, 256, &r);
            return std::make_pair (l, r);
        };
        // SERIES: el eco (1/4 a 120 BPM = 0.5 s) entra en la reverb -> más energía tras el eco
        {
            auto par = renderRoute (1, true, true, 600.0f, 0.4f, 4).first;
            auto ser = renderRoute (0, true, true, 600.0f, 0.4f, 4).first;
            const int lat = (int) (0.06 * fs);
            const double a = energyDb (par, lat + (int) (fs * 0.75), lat + (int) (fs * 1.0));
            const double b = energyDb (ser, lat + (int) (fs * 0.75), lat + (int) (fs * 1.0));
            check (b - a > 6.0, "SERIES: los ecos del delay se reverberan", "cola tras el eco " + juce::String (a, 1) + " -> " + juce::String (b, 1) + " dB");
        }
        // MID/SIDE: el centro de los retornos baja (slap mono)
        {
            auto par = renderRoute (1, true, false, 600.0f, 1.0f, 9).first;
            auto ms = renderRoute (2, true, false, 600.0f, 1.0f, 9).first;
            const int lat = (int) (0.06 * fs);
            const double a = energyDb (par, lat + (int) (fs * 0.16), lat + (int) (fs * 0.24));
            const double b = energyDb (ms, lat + (int) (fs * 0.16), lat + (int) (fs * 0.24));
            check (a - b > 8.0, "MID/SIDE: el centro de los retornos baja (la voz manda en el centro)", juce::String (a - b, 1) + " dB menos en el centro");
        }
        // SPLIT: los graves no entran en los efectos
        {
            auto par = renderRoute (1, false, true, 150.0f, 1.5f, 4).first;
            auto sp = renderRoute (3, false, true, 150.0f, 1.5f, 4).first;
            const int lat = (int) (0.06 * fs);
            const double a = energyDb (par, lat + (int) (fs * 0.3), lat + (int) (fs * 0.8));
            const double b = energyDb (sp, lat + (int) (fs * 0.3), lat + (int) (fs * 0.8));
            check (a - b > 6.0, "SPLIT: graves (150 Hz) fuera de la reverb", juce::String (a - b, 1) + " dB menos de cola grave");
        }
    }

    std::printf ("\n== v3.0 STUDIO: SECUENCIADOR, TONE TILT, TECHO, LINK ==\n");
    {
        // GATE: el paso 4 de cada grupo (s % 4 == 3) silencia; 1/16 a 120 BPM = 125 ms
        {
            VoxDnaProcessor p; prepare (p, fs, 256);
            layerOnly (p, 0);
            setParam (p, "seqOn", 1); setParam (p, "seqGate", 100); setParam (p, "seqThrow", 0);
            std::vector<float> sig ((size_t) fs * 2);
            for (size_t i = 0; i < sig.size(); ++i) sig[i] = 0.3f * (float) std::sin (2 * M_PI * 440.0 * i / fs);
            auto y = run (p, sig, 256);
            const int lat = p.getLatencySamples();
            const int stepLen = (int) (0.125 * fs);
            // paso 3 (silencio) frente a paso 2 (abierto) del segundo compás de 16 pasos
            const int base = 4 * stepLen;   // el gate actúa antes del limitador (la latencia no afecta al reloj)
            (void) lat;
            const double open = energyDb (y, base + 2 * stepLen + 400, base + 3 * stepLen - 400);
            const double shut = energyDb (y, base + 3 * stepLen + 600, base + 4 * stepLen - 600);
            check (open - shut > 30.0, "GATE secuenciado a 1/16 (125 ms por paso)", juce::String (open - shut, 1) + " dB entre paso abierto y cerrado");
        }
        // THROW: con el delay apagado, los pasos 8 y 16 lanzan ecos
        {
            auto tailWith = [&] (float depth)
            {
                VoxDnaProcessor p; prepare (p, fs, 256);
                layerOnly (p, 0);
                setParam (p, "seqOn", 1); setParam (p, "seqThrow", depth); setParam (p, "dlyTime", 3); setParam (p, "dlyFb", 30);
                std::vector<float> sig ((size_t) fs * 3, 0.0f);
                for (int i = 0; i < (int) (fs * 2); ++i) sig[(size_t) i] = 0.3f * (float) std::sin (2 * M_PI * 440.0 * i / fs);
                auto y = run (p, sig, 256);
                return energyDb (y, (int) (fs * 2.15), (int) (fs * 2.6));
            };
            const double none = tailWith (0), thr = tailWith (100);
            check (thr - none > 30.0, "THROW secuenciado: ecos en los pasos activos (delay apagado)", juce::String (none, 1) + " -> " + juce::String (thr, 1) + " dB tras la frase");
        }
        // TONE TILT +6 dB: 10 kHz sube, 100 Hz baja
        {
            auto gainAt = [&] (float hz, float tilt)
            {
                VoxDnaProcessor p; prepare (p, fs, 256);
                layerOnly (p, 0);
                setParam (p, "toneTilt", tilt);
                std::vector<float> sig ((size_t) fs);
                for (size_t i = 0; i < sig.size(); ++i) sig[i] = 0.05f * (float) std::sin (2 * M_PI * hz * i / fs);
                auto y = run (p, sig, 256);
                return energyDb (y, (int) fs / 2, (int) fs) - energyDb (sig, (int) fs / 2, (int) fs);
            };
            const double hi = gainAt (10000, 6), lo = gainAt (100, 6), flat = gainAt (1000, 0);
            check (std::abs ((hi - lo) - 6.0) < 1.0 && std::abs (flat) < 0.3, "TONE TILT +6 dB (100 Hz vs 10 kHz)",
                   "100 Hz " + juce::String (lo, 1) + " dB, 10 kHz +" + juce::String (hi, 1) + " dB");
        }
        // techo ajustable: -3 dBTP
        {
            VoxDnaProcessor p; prepare (p, fs, 256);
            layerOnly (p, 0);
            setParam (p, "ceiling", -3.0f); setParam (p, "inGain", 18);
            auto sig = vocalLike (fs, (int) fs * 2, 210.0f, 20.0f);
            auto y = run (p, sig, 256);
            std::vector<float> tail (y.begin() + (int) fs, y.end());
            const float tp = referenceTruePeakDb (tail);
            check (tp <= -3.0f + 0.1f, "OUTPUT / LIMIT: techo de -3 dBTP respetado (voz +18 dB)", juce::String (tp, 2) + " dBTP");
        }
        // LINK: subir la entrada con link no cambia el nivel de salida (cadena lineal)
        {
            auto outLevel = [&] (float in, bool link)
            {
                VoxDnaProcessor p; prepare (p, fs, 256);
                layerOnly (p, 0);
                setParam (p, "inGain", in); setParam (p, "ioLink", link ? 1.0f : 0.0f);
                std::vector<float> sig ((size_t) fs);
                for (size_t i = 0; i < sig.size(); ++i) sig[i] = 0.05f * (float) std::sin (2 * M_PI * 700.0 * i / fs);
                auto y = run (p, sig, 256);
                return energyDb (y, (int) fs / 2, (int) fs);
            };
            const double ref = outLevel (0, false), linked = outLevel (6, true), unlinked = outLevel (6, false);
            check (std::abs (linked - ref) < 0.2 && std::abs (unlinked - ref - 6.0) < 0.3, "IN/OUT LINK: trim de entrada compensado en el master",
                   "sin link +" + juce::String (unlinked - ref, 1) + " dB, con link " + juce::String (linked - ref, 2) + " dB");
        }
        // orden de la cadena: cambiarlo en reproducción sin clicks ni NaN
        {
            VoxDnaProcessor p; prepare (p, fs, 256);
            p.setCurrentProgram (3);   // RAGE EDGE: EQ + FET + Rage Clip
            auto sig = vocalLike (fs, (int) fs * 3, 200.0f, 20.0f);
            float maxStep = 0.0f; bool nan = false; float prev = 0.0f;
            juce::MidiBuffer midi;
            for (int pos = 0, k = 0; pos + 256 <= (int) sig.size(); pos += 256, ++k)
            {
                if (k % 40 == 20) setParam (p, "chainOrder", (float) ((k / 40) % 6));
                juce::AudioBuffer<float> buf (2, 256);
                for (int i = 0; i < 256; ++i) { buf.setSample (0, i, sig[(size_t) (pos + i)]); buf.setSample (1, i, sig[(size_t) (pos + i)]); }
                p.processBlock (buf, midi);
                for (int i = 0; i < 256; ++i)
                {
                    const float v = buf.getSample (0, i);
                    nan = nan || ! std::isfinite (v);
                    if (pos > (int) fs / 2) maxStep = std::max (maxStep, std::abs (v - prev));
                    prev = v;
                }
            }
            check (! nan && maxStep < 0.25f, "Orden de la cadena (6 permutaciones) cambiado en reproduccion", "paso max " + juce::String (maxStep, 3));
        }
        // el orden se aplica de verdad: EQ>COMP>COLOR frente a COLOR>COMP>EQ (diferencia de forma de onda)
        {
            std::vector<std::vector<float>> outs;
            for (int o : { 0, 5 })
            {
                VoxDnaProcessor p; prepare (p, fs, 256);
                p.setCurrentProgram (3);
                setParam (p, "chainOrder", (float) o);
                auto sig = vocalLike (fs, (int) fs * 2, 200.0f, 20.0f);
                outs.push_back (run (p, sig, 256));
            }
            double d = 0, e = 0;
            for (size_t i = (size_t) fs; i < outs[0].size(); ++i) { const double x = outs[0][i] - outs[1][i]; d += x * x; e += (double) outs[0][i] * outs[0][i]; }
            const double rel = 10.0 * std::log10 (d / (e + 1e-20) + 1e-20);
            check (rel > -40.0, "Orden de insercion aplicado (RAGE EDGE: EQ>COMP>COLOR vs COLOR>COMP>EQ)", "diferencia " + juce::String (rel, 1) + " dB respecto a la senal");
        }
    }

    std::printf ("\n== v3.0 STUDIO: MIGRACION DE SESIONES Y PRESETS v2.x ==\n");
    {
        VoxDnaProcessor a; prepare (a, fs, 256);
        allNeutral (a);
        juce::MemoryBlock mb;
        a.getStateInformation (mb);
        auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
        xml->setAttribute ("schema", 2);
        auto setX = [&] (const char* id, float v) { for (auto* c : xml->getChildIterator()) if (c->getStringAttribute ("id") == id) c->setAttribute ("value", v); };
        setX ("layOn3", 1); setX ("layLvl3", -10);      // v2: LOW
        setX ("layOn5", 1); setX ("layLvl5", -8); setX ("layPan5", 30);   // v2: ADLIBS
        setX ("layOn4", 0);
        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*xml, old);
        VoxDnaProcessor b; prepare (b, fs, 256);
        b.setStateInformation (old.getData(), (int) old.getSize());
        auto val = [&] (const char* id) { auto* pp = b.apvts.getParameter (id); return pp->convertFrom0to1 (pp->getValue()); };
        const bool ok = val ("layOn3") > 0.5f && std::abs (val ("layLvl3") + 8) < 0.01f && std::abs (val ("layPan3") - 30) < 0.01f
                        && val ("layOn5") > 0.5f && std::abs (val ("layLvl5") + 10) < 0.01f && (int) val ("octMode") == 0 && val ("layOn4") < 0.5f;
        check (ok, "Sesion v2.x: ADLIBS y LOW se trasladan a las capas v3 (ADLIBS, OCTAVE -1)", ok ? "OK" : "valores incorrectos");

        // preset de usuario schema 2
        auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("vdna_mig");
        folder.createDirectory();
        auto f = folder.getChildFile ("old.vdna");
        f.replaceWithText (R"({"schema":2,"name":"old","params":{"layOn4":{"value":1},"layLvl4":{"value":-11},"layOn6":{"value":1}}})");
        VoxDnaProcessor c; prepare (c, fs, 256);
        c.loadUserPreset (f);
        auto vc = [&] (const char* id) { auto* pp = c.apvts.getParameter (id); return pp->convertFrom0to1 (pp->getValue()); };
        const bool ok2 = vc ("layOn5") > 0.5f && std::abs (vc ("layLvl5") + 11) < 0.01f && (int) vc ("octMode") == 1 && vc ("layOn6") > 0.5f && vc ("layOn4") < 0.5f;
        check (ok2, "Preset .vdna schema 2: HIGH -> OCTAVE +1, ATMOS -> TEXTURE", ok2 ? "OK" : "valores incorrectos");
        folder.deleteRecursively();
    }

    std::printf ("\n== LOUDNESS RANGE (EBU Tech 3342) ==\n");
    {
        struct LraCase { std::vector<float> levels; float expect; };
        const LraCase cases[] { { { -20, -30 }, 10.0f }, { { -20, -15 }, 5.0f }, { { -40, -20 }, 20.0f }, { { -50, -35, -20, -35, -50 }, 15.0f } };
        int k = 1;
        for (const auto& c : cases)
        {
            pv::LoudnessMeter m; m.prepare (48000.0);
            std::vector<float> l (4800), r (4800);
            double ph = 0;
            for (float lv : c.levels)
            {
                const float a = juce::Decibels::decibelsToGain (lv);
                for (int b = 0; b < 200; ++b)   // 20 s
                {
                    for (int i = 0; i < 4800; ++i) { l[(size_t) i] = r[(size_t) i] = a * (float) std::sin (ph); ph += 2 * M_PI * 1000.0 / 48000.0; }
                    m.process (l.data(), r.data(), 4800);
                }
            }
            const float lra = m.loudnessRange.load();
            check (std::abs (lra - c.expect) <= 1.0f, ("Caso " + juce::String (k++) + ": LRA esperado " + pv::ui::num (c.expect, 0) + " LU").toRawUTF8(),
                   juce::String (lra, 2) + " LU");
        }
    }

    std::printf ("\n== v3.0 ANALYZE: PERFILES, MATCH Y SUGERENCIAS DE EQ ==\n");
    {
        auto makeProfile = [&] (float hiShelfDb, float side)
        {
            pv::analysis::Accumulator acc;
            acc.reset (fs);
            juce::dsp::IIR::Filter<float> sh;
            sh.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf (fs, 6000.0f, 0.7f, juce::Decibels::decibelsToGain (hiShelfDb));
            auto v = vocalLike (fs, (int) fs * 8, 220.0f, 25.0f);
            juce::Random rnd (11);   // aire: soplo de banda ancha ~-30 dB (una voz real lo tiene)
            for (auto& x : v) x += 0.012f * (rnd.nextFloat() * 2.0f - 1.0f);
            std::vector<float> l (v.size()), r (v.size());
            for (size_t i = 0; i < v.size(); ++i) { const float x = sh.processSample (v[i]); l[i] = x * (1.0f + side); r[i] = x * (1.0f - side); }
            acc.push (l.data(), r.data(), (int) l.size());
            return acc.snapshot ("t");
        };
        const auto a = makeProfile (0.0f, 0.0f), b = makeProfile (0.0f, 0.0f), bright = makeProfile (6.0f, 0.0f), wide = makeProfile (0.0f, 0.5f);
        const auto same = pv::analysis::compare (a, b);
        check (same.valid && same.total > 99.0f, "Match de una voz consigo misma", juce::String (same.total, 1) + " %");
        const auto vsBright = pv::analysis::compare (a, bright);
        check (vsBright.eq[3] >= 1.5f && std::abs (vsBright.eq[0]) <= 1.0f && vsBright.total < same.total - 3.0f,
               "Referencia con +6 dB en agudos -> sugiere subir AIR (y no tocar BODY)",
               "AIR " + juce::String (vsBright.eq[3], 1) + " dB, BODY " + juce::String (vsBright.eq[0], 1) + " dB, match " + pv::ui::num (vsBright.total, 0) + " %");
        check (a.widthPct < 0.5f && wide.widthPct > 10.0f, "Anchura: mono = 0 %, L/R desequilibrado > 10 %",
               juce::String (a.widthPct, 1) + " % vs " + juce::String (wide.widthPct, 1) + " %");
        const auto va = pv::analysis::vibeFrom (a), vb = pv::analysis::vibeFrom (bright);
        check (vb.brightness > va.brightness && vb.air > va.air, "VIBE: mas agudos -> mas BRIGHTNESS y AIR",
               "brillo " + juce::String (va.brightness, 2) + " -> " + juce::String (vb.brightness, 2) + ", aire " + juce::String (va.air, 2) + " -> " + juce::String (vb.air, 2));

        // archivo: la sonoridad integrada coincide con el medidor BS.1770 del plugin
        auto wav = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("vdna_ref_test.wav");
        wav.deleteFile();
        std::vector<float> tone ((size_t) fs * 10);
        for (size_t i = 0; i < tone.size(); ++i) tone[i] = juce::Decibels::decibelsToGain (-23.0f) * (float) std::sin (2 * M_PI * 1000.0 * i / fs);
        {
            juce::WavAudioFormat fmt;
            std::unique_ptr<juce::AudioFormatWriter> w (fmt.createWriterFor (new juce::FileOutputStream (wav), fs, 2, 24, {}, 0));
            juce::AudioBuffer<float> buf (2, (int) tone.size());
            for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, tone.data(), (int) tone.size());
            w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
        }
        const auto pf = pv::analysis::analyzeFile (wav);
        check (pf.valid && std::abs (pf.lufsI + 23.0f) < 0.1f && std::abs (pf.truePeakDb + 23.0f) < 0.2f,
               "Analisis de archivo (seno 1 kHz -23 dBFS): LUFS y true peak", juce::String (pf.lufsI, 2) + " LUFS, TP " + juce::String (pf.truePeakDb, 2) + " dBTP");
        wav.deleteFile();
    }

    std::printf ("\n== RENDIMIENTO (64 muestras @ 48 kHz = 1.33 ms por bloque) ==\n");
    for (int keep = 0; keep < 2; ++keep)
        for (int instances : { 1, 4, 8, 16 })
        {
            std::vector<std::unique_ptr<VoxDnaProcessor>> ps;
            for (int i = 0; i < instances; ++i)
            {
                ps.push_back (std::make_unique<VoxDnaProcessor>());
                prepare (*ps.back(), fs, 64);
                ps.back()->setCurrentProgram (1);   // ASTRO ATMOS: tune + FET + opto + color + delay + reverb + dobles
                setParam (*ps.back(), "tuneOn", 1);
                setParam (*ps.back(), "tuneFormantPreserve", (float) keep);
            }
            auto sig = vocalLike (fs, (int) fs * 2, 205.0f, 30.0f);
            juce::MidiBuffer midi;
            juce::AudioBuffer<float> buf (2, 64);
            const auto t0 = std::chrono::high_resolution_clock::now();
            int blocks = 0;
            for (int pos = 0; pos + 64 <= (int) sig.size(); pos += 64, ++blocks)
                for (auto& p : ps)
                {
                    buf.copyFrom (0, 0, sig.data() + pos, 64);
                    buf.copyFrom (1, 0, sig.data() + pos, 64);
                    p->processBlock (buf, midi);
                }
            const double secs = std::chrono::duration<double> (std::chrono::high_resolution_clock::now() - t0).count();
            const double audio = blocks * 64.0 / fs;
            std::printf ("  %2d instancia(s) MIX%s: %.1f %% de tiempo real (un nucleo de este servidor)\n",
                         instances, keep ? " + Formant Keep" : "               ", 100.0 * secs / audio);
        }

    // --------------------------------------------------------------------- UI
    if (argc > 1)
    {
        std::printf ("\n== CAPTURAS UI ==\n");
        VoxDnaProcessor p;
        prepare (p, fs, block);
        p.setCurrentProgram (4);   // TRAVIS (Atmospheric) tras los 6 de blueprint? se resuelve abajo
        for (int i = 0; i < p.getNumPrograms(); ++i)
            if (p.getProgramName (i).startsWith ("TRAVIS SCOTT")) { p.setCurrentProgram (i); break; }
        setParam (p, "tuneKey", 9); setParam (p, "tuneScale", 2);
        setParam (p, "optoOn", 1);
        auto sig = vocalLike (fs, (int) fs * 3, 220.0f * std::pow (2.0f, 0.2f / 12.0f), 30.0f);
        juce::MidiBuffer midi;
        // referencia de demostración (voz sintética más brillante) para la vista ANALYZE
        {
            auto refSig = vocalLike (fs, (int) fs * 6, 260.0f, 25.0f, 1.15f);
            auto wav = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Demo Vocal Ref.wav");
            wav.deleteFile();
            {
                juce::WavAudioFormat fmt;
                std::unique_ptr<juce::AudioFormatWriter> w (fmt.createWriterFor (new juce::FileOutputStream (wav), fs, 2, 24, {}, 0));
                juce::AudioBuffer<float> buf (2, (int) refSig.size());
                for (int i = 0; i < (int) refSig.size(); ++i) { buf.setSample (0, i, refSig[(size_t) i] * 1.6f); buf.setSample (1, i, refSig[(size_t) i] * 1.4f); }
                w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
            }
            auto prof = pv::analysis::analyzeFile (wav);
            pv::ui::ReferenceLibrary::save (prof);
            p.setReferenceName (prof.name);
        }
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        ed->setSize (VoxDnaEditor::baseW, VoxDnaEditor::baseH);
        auto* vEd = dynamic_cast<VoxDnaEditor*> (ed.get());
        for (int view = 0; view < 5; ++view)
        {
            vEd->setView (view);
            for (int pos = 0; pos + block <= (int) sig.size(); pos += block)
            {
                juce::AudioBuffer<float> buf (2, block);
                for (int i = 0; i < block; ++i) { buf.setSample (0, i, sig[(size_t) (pos + i)]); buf.setSample (1, i, sig[(size_t) (pos + i)]); }
                p.processBlock (buf, midi);
                if (pos % (block * 4) == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil (2);
            }
            juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::File f (juce::String (argv[1]) + "/ui_" + juce::String (view) + ".png");
            f.deleteFile();
            juce::FileOutputStream os (f);
            juce::PNGImageFormat().writeImageToStream (img, os);
            std::printf ("  -> %s\n", f.getFullPathName().toRawUTF8());
        }

        // ---------------- v3.1 MOTION: coste de pintado y vídeo de muestra ----------------
        std::printf ("\n== v3.1 MOTION: COSTE DE LA INTERFAZ Y VIDEO DE MUESTRA ==\n");
        struct FakeHead : juce::AudioPlayHead
        {
            double ppq = 0.0;
            juce::Optional<PositionInfo> getPosition() const override
            {
                PositionInfo pi; pi.setIsPlaying (true); pi.setBpm (120.0); pi.setPpqPosition (ppq);
                return pi;
            }
        } head;
        p.setPlayHead (&head);
        vEd->setView (0);
        auto voice = syllables (fs, (int) fs * 12, 220.0f);
        int pos = 0;
        auto feed = [&] (double seconds)
        {
            const int n = (int) (seconds * fs);
            for (int done = 0; done < n; done += block)
            {
                juce::AudioBuffer<float> buf (2, block);
                for (int i = 0; i < block; ++i) { const float v = voice[(size_t) ((pos + i) % (int) voice.size())] * 2.0f; buf.setSample (0, i, v); buf.setSample (1, i, v); }
                pos += block;
                head.ppq += (double) block / fs * 2.0;   // 120 BPM
                p.processBlock (buf, midi);
            }
        };
        for (auto mode : { 2, 1, 0 })
        {
            pv::ui::motionMode() = mode;
            double total = 0.0, anim = 0.0;
            for (int k = 0; k < 30; ++k)
            {
                feed (1.0 / 60.0);
                vEd->tickForTest();
                const auto t0 = juce::Time::getMillisecondCounterHiRes();
                auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
                total += juce::Time::getMillisecondCounterHiRes() - t0;
                // lo que realmente se repinta en cada frame animado (regiones sucias reales):
                // hero completo + radar VIBE (30 fps) + franja de flujo de la cadena + zonas de onda de las capas (30 fps)
                // se pinta en un lienzo reservado una vez (como hace la ventana real), recortado a cada región
                static juce::Image canvas (juce::Image::ARGB, 1536, 1024, true);
                auto paintRegion = [&] (juce::Component* comp, juce::Rectangle<int> area)
                {
                    juce::Graphics cg (canvas);
                    cg.reduceClipRegion (area);
                    comp->paintEntireComponent (cg, true);
                };
                const auto t1 = juce::Time::getMillisecondCounterHiRes();
                for (auto* c : ed->getChildComponent (0)->getChildren())
                    if (c->isVisible() && dynamic_cast<pv::ui::CreatePage*> (c) != nullptr)
                        for (auto* sub : c->getChildren())
                        {
                            if (dynamic_cast<pv::ui::HeroPanel*> (sub)) paintRegion (sub, sub->getLocalBounds());
                            if (dynamic_cast<pv::ui::ChainPanel*> (sub)) paintRegion (sub, { 114, 177, 1072, 12 });
                            if (k % 2 == 0 && dynamic_cast<pv::ui::VibePanel*> (sub)) paintRegion (sub, { 40, 26, 204, 176 });
                            if (k % 2 == 0)
                                if (auto* lp = dynamic_cast<pv::ui::LayersPanel*> (sub))
                                    for (auto* card : lp->getChildren())
                                        if (dynamic_cast<pv::ui::LayerCard*> (card)) paintRegion (card, { 6, 30, card->getWidth() - 12, 50 });
                        }
                anim += juce::Time::getMillisecondCounterHiRes() - t1;
            }
            static const char* names[] { "desactivadas", "suaves", "completas" };
            check (anim / 30.0 < 8.0, (juce::String ("Animaciones ") + names[mode] + ": coste de pintado por frame (CPU, sin GPU, escala 100 %)").toRawUTF8(),
                   "regiones animadas " + juce::String (anim / 30.0, 2) + " ms/frame (ventana completa " + juce::String (total / 30.0, 1) + " ms)");
        }
        pv::ui::motionMode() = 2;
        // vídeo: 8 s a 30 fps (cada frame avanza 2 ticks de 60 Hz), con cambios de preset a los 2.5 y 5.5 s
        juce::File frames (juce::String (argv[1]) + "/frames");
        frames.deleteRecursively();
        frames.createDirectory();
        int presetA = 0, presetB = 0;
        for (int i = 0; i < p.getNumPrograms(); ++i)
        {
            if (p.getProgramName (i) == "RAGE EDGE") presetA = i;
            if (p.getProgramName (i) == "AIRY R&B") presetB = i;
        }
        for (int fr = 0; fr < 240; ++fr)
        {
            if (fr == 75) p.setCurrentProgram (presetA);
            if (fr == 165) p.setCurrentProgram (presetB);
            for (int t2 = 0; t2 < 2; ++t2) { feed (1.0 / 60.0); vEd->tickForTest(); }
            juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 0.75f);
            juce::FileOutputStream os (frames.getChildFile (juce::String::formatted ("f%04d.png", fr)));
            juce::PNGImageFormat().writeImageToStream (img, os);
        }
        std::printf ("  -> %d frames en %s\n", 240, frames.getFullPathName().toRawUTF8());
        p.setPlayHead (nullptr);
    }

    std::printf ("\n%s (%d fallos)\n", failures == 0 ? "TODOS LOS TESTS OK" : "HAY FALLOS", failures);
    return failures == 0 ? 0 : 1;
}
