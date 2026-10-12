#pragma once
#include "../PluginProcessor.h"
#include "../Analysis.h"
#include "Widgets.h"

// ============================================================================
//  Contexto compartido por todas las vistas del editor v3:
//   · análisis en vivo (consume los FIFOs del procesador en el hilo de mensajes)
//   · ajustes de usuario (favoritos, escala, imágenes por preset)
//   · biblioteca de referencias (perfiles .vref analizados localmente)
// ============================================================================
namespace pv::ui
{
struct LiveAnalysis
{
    static constexpr int ringSize = 8192, waveLen = 220;
    std::array<float, ringSize> ringL {}, ringR {};
    int ringPos = 0;
    std::array<float, waveLen> outWave {}, inWave {};   // envolventes para la onda A/B (entrada vs salida)
    int wavePos = 0;
    analysis::Accumulator longTerm, recent;
    std::array<float, 4096> tmpL {}, tmpR {}, tmpIn {};
    double fs = 48000.0;
    float outAcc = 0.0f, inAcc = 0.0f;
    int accCount = 0;

    void reset (double sampleRate)
    {
        fs = sampleRate > 0 ? sampleRate : 48000.0;
        longTerm.reset (fs);
        recent.reset (fs, 0.985f);
    }

    void pull (VoxDnaProcessor& p)
    {
        const int hopSamples = juce::jmax (64, (int) (fs / 90.0));   // ~11 ms por punto de onda
        for (;;)
        {
            const int got = p.analyzerFifo.pull (tmpL.data(), tmpR.data(), (int) tmpL.size());
            if (got == 0) break;
            for (int i = 0; i < got; ++i)
            {
                ringL[(size_t) ringPos] = tmpL[(size_t) i];
                ringR[(size_t) ringPos] = tmpR[(size_t) i];
                ringPos = (ringPos + 1) % ringSize;
                outAcc = juce::jmax (outAcc, std::abs (tmpL[(size_t) i]), std::abs (tmpR[(size_t) i]));
                if (++accCount >= hopSamples)
                {
                    outWave[(size_t) wavePos] = outAcc;
                    inWave[(size_t) wavePos] = inAcc;
                    wavePos = (wavePos + 1) % waveLen;
                    outAcc = inAcc = 0.0f;
                    accCount = 0;
                }
            }
            longTerm.push (tmpL.data(), tmpR.data(), got);
            recent.push (tmpL.data(), tmpR.data(), got);
        }
        for (;;)
        {
            const int got = p.inputFifo.pull (tmpIn.data(), (int) tmpIn.size());
            if (got == 0) break;
            for (int i = 0; i < got; ++i) inAcc = juce::jmax (inAcc, std::abs (tmpIn[(size_t) i]));
        }
    }

    // 32 bandas logarítmicas (60 Hz - 16 kHz) 0..1 para animaciones audio-reactivas
    std::array<float, 32> bands {};
    juce::dsp::FFT bandFft { 10 };
    std::array<float, 2048> bandData {};
    void updateBands()
    {
        const int N = 1024;
        for (int i = 0; i < N; ++i)
        {
            const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N);
            bandData[(size_t) i] = sample (3, ringSize - N + i) * w;
        }
        std::fill (bandData.begin() + N, bandData.end(), 0.0f);
        bandFft.performFrequencyOnlyForwardTransform (bandData.data());
        for (int k = 0; k < 32; ++k)
        {
            const float f0 = 60.0f * std::pow (266.0f, (float) k / 32.0f), f1 = 60.0f * std::pow (266.0f, (float) (k + 1) / 32.0f);
            const int i0 = juce::jlimit (1, N / 2 - 1, (int) (f0 * (float) N / (float) fs)), i1 = juce::jlimit (i0, N / 2 - 1, (int) (f1 * (float) N / (float) fs));
            float m = 0.0f;
            for (int i = i0; i <= i1; ++i) m = juce::jmax (m, bandData[(size_t) i]);
            float db = juce::Decibels::gainToDecibels (m / (float) (N / 4), -90.0f) + 3.0f * std::log2 (juce::jmax (f0, 60.0f) / 500.0f);
            const float v = juce::jlimit (0.0f, 1.0f, (db + 66.0f) / 60.0f);
            auto& b = bands[(size_t) k];
            b = v > b ? b + (v - b) * 0.6f : b + (v - b) * 0.12f;
        }
    }

    // muestra del anillo (0 = más antigua)
    float sample (int channelMode, int i) const
    {
        const int k = (ringPos + i) % ringSize;
        const float l = ringL[(size_t) k], r = ringR[(size_t) k];
        switch (channelMode)
        {
            case 1: return l;
            case 2: return r;
            case 3: return 0.5f * (l + r);
            case 4: return 0.5f * (l - r);
            default: return 0.5f * (l + r);
        }
    }
};

// ---------------------------------------------------------------------------
struct UserSettings
{
    juce::StringArray favourites;
    float uiScale = 0.8f;
    bool tooltips = true;
    int motion = 2;   // 0 off, 1 suaves, 2 completas

    static juce::File file() { return VoxDnaProcessor::getDataFolder().getChildFile ("settings.json"); }

    void load()
    {
        const auto v = juce::JSON::parse (file());
        if (! v.isObject()) return;
        favourites.clear();
        if (auto* a = v.getProperty ("favourites", {}).getArray())
            for (auto& f : *a) favourites.add (f.toString());
        uiScale = juce::jlimit (0.5f, 2.0f, (float) (double) v.getProperty ("uiScale", 0.8));
        tooltips = (bool) v.getProperty ("tooltips", true);
        motion = juce::jlimit (0, 2, (int) v.getProperty ("motion", 2));
    }

    void save() const
    {
        auto* o = new juce::DynamicObject();
        juce::Array<juce::var> f;
        for (auto& s : favourites) f.add (s);
        o->setProperty ("favourites", f);
        o->setProperty ("uiScale", uiScale);
        o->setProperty ("tooltips", tooltips);
        o->setProperty ("motion", motion);
        file().getParentDirectory().createDirectory();
        file().replaceWithText (juce::JSON::toString (juce::var (o)));
    }
};

// ---------------------------------------------------------------------------
class ReferenceLibrary
{
public:
    static juce::File folder() { return VoxDnaProcessor::getDataFolder().getChildFile ("References"); }

    juce::StringArray names() const
    {
        juce::StringArray n;
        for (auto& f : folder().findChildFiles (juce::File::findFiles, false, "*.vref"))
            n.add (f.getFileNameWithoutExtension());
        n.sort (true);
        return n;
    }

    analysis::Profile load (const juce::String& name) const
    {
        if (name.isEmpty()) return {};
        return analysis::Profile::fromVar (juce::JSON::parse (folder().getChildFile (juce::File::createLegalFileName (name) + ".vref")));
    }

    void remove (const juce::String& name) const { folder().getChildFile (juce::File::createLegalFileName (name) + ".vref").deleteFile(); }

    static bool save (const analysis::Profile& p)
    {
        folder().createDirectory();
        return folder().getChildFile (juce::File::createLegalFileName (p.name) + ".vref").replaceWithText (juce::JSON::toString (p.toVar()));
    }

    // analiza en un hilo propio y avisa en el hilo de mensajes
    void analyseAsync (const juce::File& f, std::function<void (analysis::Profile)> done)
    {
        auto job = std::make_unique<Job> (f, std::move (done));
        job->startThread (juce::Thread::Priority::low);
        jobs.add (job.release());
        for (int i = jobs.size() - 1; i >= 0; --i)
            if (! jobs[i]->isThreadRunning() && jobs[i]->finished) jobs.remove (i);
    }
    bool busy() const
    {
        for (auto* j : jobs) if (j->isThreadRunning()) return true;
        return false;
    }
    ~ReferenceLibrary() { for (auto* j : jobs) j->stopThread (4000); }

private:
    struct Job : public juce::Thread
    {
        Job (juce::File f, std::function<void (analysis::Profile)> d) : juce::Thread ("VOX DNA reference analysis"), file (std::move (f)), done (std::move (d)) {}
        void run() override
        {
            auto profile = analysis::analyzeFile (file, [this] { return threadShouldExit(); });
            if (threadShouldExit()) return;
            if (profile.valid) save (profile);
            auto cb = done;
            juce::MessageManager::callAsync ([cb, profile] { if (cb) cb (profile); });
            finished = true;
        }
        juce::File file;
        std::function<void (analysis::Profile)> done;
        std::atomic<bool> finished { false };
    };
    juce::OwnedArray<Job> jobs;
};

// ---------------------------------------------------------------------------
struct PresetInfo
{
    juce::String key;           // clave estable (arte, favoritos)
    juce::String artist, title, genre, evidence, description;
    juce::StringArray tags;
    bool factory = true;
    int factoryIndex = -1;
    juce::File userFile;
};

struct UiContext
{
    explicit UiContext (VoxDnaProcessor& p) : proc (p)
    {
        settings.load();
        motionMode() = settings.motion;
        live.reset (p.getSampleRate());
        referenceName = p.getReferenceName();
        reference = refs.load (referenceName);
    }

    VoxDnaProcessor& proc;
    LiveAnalysis live;

    // ---- reloj de animación (v3.1) ----
    struct Motion
    {
        double time = 0.0;
        float dt = 1.0f / 60.0f;
        float energy = 0.0f, energySlow = 0.0f;   // nivel de salida suavizado 0..1
        float beat = 0.0f;                        // pulso que decae tras cada pulso del DAW
        double lastBeat = -1.0;
        bool enabled() const { return motionMode() > 0; }
        bool full() const { return motionMode() == 2; }
        void update (VoxDnaProcessor& p, float seconds)
        {
            dt = seconds;
            time += seconds;
            const float lv = juce::jlimit (0.0f, 1.0f, std::sqrt (p.outputLevel.load()) * 1.25f);
            energy += (lv - energy) * (lv > energy ? 0.5f : 0.12f);
            energySlow += (lv - energySlow) * 0.03f;
            const double b = p.beatPosition.load();
            if (b >= 0.0)
            {
                if (lastBeat < 0.0 || std::floor (b) != std::floor (lastBeat)) beat = 1.0f;
                lastBeat = b;
            }
            else lastBeat = -1.0;
            beat *= std::exp (-seconds / 0.18f);
        }
    } motion;
    UserSettings settings;
    ReferenceLibrary refs;
    analysis::Profile reference;
    juce::String referenceName;
    std::map<juce::String, juce::Image> imageCache;

    float value (const juce::String& id) const
    {
        auto* prm = proc.apvts.getParameter (id);
        return prm != nullptr ? prm->convertFrom0to1 (prm->getValue()) : 0.0f;
    }
    void setValue (const juce::String& id, float v)
    {
        if (auto* prm = proc.apvts.getParameter (id))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 (v));
            prm->endChangeGesture();
        }
    }

    void setReference (const juce::String& name)
    {
        referenceName = name;
        reference = refs.load (name);
        proc.setReferenceName (name);
    }

    PresetInfo currentPreset() const
    {
        PresetInfo info;
        const int idx = proc.getLoadedFactoryIndex();
        const auto& fp = getFactoryPresets();
        if (juce::isPositiveAndBelow (idx, (int) fp.size()) && fp[(size_t) idx].name == proc.getPresetName())
            return factoryInfo (idx);
        info.factory = false;
        info.artist = "USER";
        info.title = proc.getPresetName().toUpperCase();
        info.key = "U:" + proc.getPresetName();
        info.genre = "USER";
        info.description = "Preset de usuario. " + proc.getPresetSubtitle().replace ("   //   ", " / ");
        info.tags.add ("USER");
        return info;
    }

    static PresetInfo factoryInfo (int idx)
    {
        PresetInfo info;
        const auto& p = getFactoryPresets()[(size_t) idx];
        info.key = "F:" + p.name;
        info.artist = p.artist; info.title = p.title; info.genre = p.genre; info.tags = p.tags;
        info.evidence = p.evidence;
        info.description = describe (p);
        info.factoryIndex = idx;
        return info;
    }

    // imagen propia del usuario para un preset (opcional)
    static juce::File imageFolder() { return VoxDnaProcessor::getDataFolder().getChildFile ("Art"); }
    static juce::File imageFileFor (const juce::String& key)
    {
        for (auto ext : { ".png", ".jpg" })
        {
            auto f = imageFolder().getChildFile (juce::File::createLegalFileName (key.replaceCharacter (':', '_')) + ext);
            if (f.existsAsFile()) return f;
        }
        return {};
    }
    juce::Image userImage (const juce::String& key)
    {
        auto it = imageCache.find (key);
        if (it != imageCache.end()) return it->second;
        juce::Image img;
        auto f = imageFileFor (key);
        if (f.existsAsFile()) img = juce::ImageFileFormat::loadFrom (f);
        imageCache[key] = img;
        return img;
    }
    bool setUserImage (const juce::String& key, const juce::File& source)
    {
        auto img = juce::ImageFileFormat::loadFrom (source);
        if (! img.isValid()) return false;
        if (img.getWidth() > 1400) img = img.rescaled (1400, juce::roundToInt (1400.0 * img.getHeight() / img.getWidth()), juce::Graphics::highResamplingQuality);
        imageFolder().createDirectory();
        clearUserImage (key);
        auto dest = imageFolder().getChildFile (juce::File::createLegalFileName (key.replaceCharacter (':', '_')) + ".png");
        juce::FileOutputStream os (dest);
        if (! os.openedOk()) return false;
        juce::PNGImageFormat png;
        png.writeImageToStream (img, os);
        imageCache.erase (key);
        return true;
    }
    void clearUserImage (const juce::String& key)
    {
        for (auto ext : { ".png", ".jpg" })
            imageFolder().getChildFile (juce::File::createLegalFileName (key.replaceCharacter (':', '_')) + ext).deleteFile();
        imageCache.erase (key);
    }

    bool isFavourite (const juce::String& key) const { return settings.favourites.contains (key); }
    void toggleFavourite (const juce::String& key)
    {
        if (isFavourite (key)) settings.favourites.removeString (key);
        else settings.favourites.add (key);
        settings.save();
    }

    // VIBE objetivo: referencia cargada o estimación desde el preset
    analysis::Vibe targetVibe() const
    {
        if (reference.valid) return analysis::vibeFrom (reference);
        return analysis::presetVibe ([this] (const char* id) { return value (id); }, proc.grTarget.load());
    }
};
} // namespace pv::ui
