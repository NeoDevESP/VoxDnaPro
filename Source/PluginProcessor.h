#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include "DSP/PitchCorrector.h"
#include "DSP/Formant.h"
#include "DSP/Dynamics.h"
#include "DSP/Analog.h"
#include "DSP/Effects.h"
#include "DSP/Layers.h"
#include "DSP/Sequencer.h"
#include "DSP/Metering.h"
#include "DSP/Limiter.h"
#include "Presets.h"

// ============================================================================
//  VOX DNA PRO v3.0 Studio — procesador
//  Grafo: INPUT -> CLEAN -> TUNE -> FORMANT -> DE-ESS -> [EQ | COMP+OPTO | COLOR]
//         (orden configurable) -> 8 CAPAS (filtros/drive/envíos por capa)
//         -> MOTION -> SEQUENCE -> ENVÍOS (delay / reverb, 4 modos de ruteo)
//         -> TONE TILT -> MASTER -> LIMIT (true peak)
//  Modos de calidad: MONITOR (latencia mínima), MIX (formant + OS 2x), RENDER HQ (OS 4x)
//
//  Reglas de tiempo real:
//   · processBlock no asigna memoria, no toma locks y no llama al sistema.
//   · Bloques mayores que el anunciado se procesan en sub-bloques.
//   · El estado compartido con la UI es atómico o pasa por un FIFO SPSC.
//   · El cambio de modo de calidad se hace en silencio y la nueva latencia se
//     comunica al host desde el hilo de mensajes.
// ============================================================================
class VoxDnaProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    VoxDnaProcessor();
    ~VoxDnaProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "VOX DNA PRO"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---------- presets ----------
    static constexpr int presetSchema = 3;     // v3: 8 capas nuevas (migración automática desde 2)
    void loadFactoryPreset (int index);
    bool loadUserPreset (const juce::File& file);
    bool saveUserPreset (const juce::String& name, const juce::String& tag);
    bool exportPreset (const juce::File& file, const juce::String& name);
    static juce::File getUserPresetFolder();
    static juce::File getDataFolder();
    juce::Array<juce::File> getUserPresets() const;
    juce::String getPresetName() const     { const juce::ScopedLock sl (presetLock); return presetName; }
    juce::String getPresetSubtitle() const { const juce::ScopedLock sl (presetLock); return presetSubtitle; }
    int getLoadedFactoryIndex() const { return loadedFactory.load(); }   // -1 = preset de usuario / modificado desde sesión
    std::atomic<int> presetSerial { 0 };   // cambia en cada carga (la UI refresca arte/descripción)

    // ---------- A/B y undo ----------
    void selectAB (int slot);
    void copyCurrentToOtherSlot();
    int getABSlot() const { return abSlot; }
    juce::UndoManager undoManager;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;

    // ---------- datos para la UI (lock-free) ----------
    std::atomic<float> inputLevel { 0.0f }, outputLevel { 0.0f };
    std::atomic<float> compGainReduction { 0.0f }, optoGainReduction { 0.0f }, deessGainReduction { 0.0f };
    std::atomic<float> compInputDb { -100.0f };
    std::atomic<float> detectedMidi { -1.0f }, targetMidi { -1.0f };
    std::atomic<float> grTarget { 3.0f };
    std::atomic<int> reportedLatency { 0 };
    std::atomic<float> outTruePeak { 0.0f }, truePeakMax { 0.0f }, limiterGainReduction { 0.0f };
    std::atomic<float> outPeakL { 0.0f }, outPeakR { 0.0f };
    std::atomic<double> beatPosition { -1.0 };   // posición en pulsos del DAW (-1 = parado) para animaciones
    pv::LoudnessMeter inLoudness, outLoudness;
    pv::CorrelationMeter correlation;
    pv::LayerEngine layers;
    pv::StepSequencer sequencer;
    void resetMeters() { outLoudness.requestReset(); inLoudness.requestReset(); truePeakMax.store (0.0f); }

    // audio -> UI sin carreras de datos (SPSC lock-free), estéreo para análisis
    pv::StereoUiFifo<32768> analyzerFifo;
    pv::AudioToUiFifo<16384> inputFifo;      // entrada (tras el trim) para la onda A/B de CREATE

    enum Quality { monitor = 0, mix, renderHQ };
    int getQuality() const { return activeQuality.load(); }
    bool isQualityTransitioning() const { return transitionState.load() != idle; }
    int computeLatency (int quality) const;

    // orden de la cadena de inserción (EQ / COMP / COLOR): 6 permutaciones
    static const juce::StringArray& chainOrderNames();
    static std::array<int, 3> chainOrderFor (int index);   // 0 = EQ, 1 = COMP, 2 = COLOR
    static int chainOrderIndex (const std::array<int, 3>& order);

    // ---------- estado extra (no automatizable) en el árbol de la sesión ----------
    juce::String getMacroSlots() const;          // "mTune,formant,mComp,..." (8 entradas)
    void setMacroSlots (const juce::String& s);
    juce::String getReferenceName() const;
    void setReferenceName (const juce::String& n);

private:
    struct Params;
    void timerCallback() override;   // hilo de mensajes: comunica la latencia al host
    void processChunk (juce::AudioBuffer<float>& buffer);
    void switchQualityInSilence (int q);
    void syncParametersFromState();
    void applyPresetValues (const pv::PresetValues& v);
    void setPresetInfo (const juce::String& n, const juce::String& sub);
    void updateHpf (float hz);
    void updateEq (float bodyDb, float mudDb, float presDb, float airDb);
    void updateTilt (float db);
    int chainLatency (int quality) const;
    void writeExtraState();          // pasos del secuenciador -> árbol
    void readExtraState();           // árbol -> atómicos del secuenciador
    static void migrateLayers (std::function<float (const juce::String&, float)> get,
                               std::function<void (const juce::String&, float)> set);

    std::atomic<float>* p (const juce::String& id) { return apvts.getRawParameterValue (id); }
    std::unique_ptr<Params> P;

    double fs = 44100.0;
    int maxBlock = 512;
    std::atomic<int> activeQuality { 1 };
    enum Transition { idle = 0, fadingOut, settling, fadingIn };
    int settleRemaining = 0;
    std::atomic<int> transitionState { idle };
    std::atomic<bool> latencyDirty { false };
    juce::SmoothedValue<float> transitionGain, orderFade;
    int activeOrder = 0;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> inGainSm, outGainSm;
    juce::SmoothedValue<float> satMixSm, parSm, dryWetSm, driveSm, ceilingSm, tiltSm;
    int colorModeActive = 0;
    juce::SmoothedValue<float> fadeGate, fadeDeess, fadeEq, fadeComp, fadeOpto, fadeSoft;

    using Filter = juce::dsp::IIR::Filter<float>;
    std::array<Filter, 2> hp1, hp2, eqBodyF, eqMudF, eqPresF, eqAirF, tiltLoF, tiltHiF;
    bool hpActive = false;
    std::array<Filter, 4> splitHpF, splitLpF;   // SPLIT: [0,1] envío de delay, [2,3] envío de reverb
    float lastHp = -1, lastBody = -100, lastMud = -100, lastPres = -100, lastAir = -100, lastTilt = -100;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> hpSm;
    juce::SmoothedValue<float> bodySm, mudSm, presSm, airSm;

    pv::Gate gate;
    PitchCorrector pitch;
    FormantShifter formant;
    pv::DeEsser deesser;
    pv::HarshTamer harsh;
    pv::Exciter exciter;
    pv::Compressor comp, crush;
    pv::OptoLeveler opto;
    pv::ColorStage color1, color2, color4;
    pv::ModFX modfx;
    pv::DuckDetector ducker;
    pv::PingPongDelay delay;
    pv::FDNReverb reverb;
    juce::dsp::Oversampling<float> os2 { 2, 1, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true };
    juce::dsp::Oversampling<float> os4 { 2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true };
    pv::TruePeakLimiter limiter;
    std::array<float, 2> dcX {}, dcY {};
    pv::DelayBuffer dryDelayL, dryDelayR;

    std::vector<float> duckKey, scratchL, scratchR, wetDryL, wetDryR,
                       sendDlyL, sendDlyR, sendRevL, sendRevR, wetL, wetR, wet2L, wet2R, throwBuf;
    bool wasDelayOn = false, wasReverbOn = false;

    std::array<juce::ValueTree, 2> abStates;
    int abSlot = 0;

    juce::CriticalSection presetLock;
    juce::String presetName { "INIT // CLEAN" }, presetSubtitle { "Punto de partida neutro" };
    int currentProgram = 0;
    std::atomic<int> loadedFactory { 0 };
    std::atomic<bool> presetJustLoaded { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxDnaProcessor)
};
