#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
const juce::StringArray delayDivisions { "1/16", "1/8T", "1/8", "1/8D", "1/4", "1/4D", "1/2",
                                         "Slap 80ms", "Slap 90ms", "Slap 100ms", "Slap 105ms", "Slap 120ms", "Slap 140ms" };
const float delayBeats[] { 0.25f, 1.0f / 3.0f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f };
const float slapMs[] { 80.0f, 90.0f, 100.0f, 105.0f, 120.0f, 140.0f };
const juce::StringArray qualityNames { "MONITOR", "MIX", "RENDER HQ" };
const juce::StringArray dnaModeNames { "NATURAL", "MODERN", "EXTREME" };
const juce::StringArray routeModeNames { "SERIES", "PARALLEL", "MID/SIDE", "SPLIT" };

juce::String fmt (float v, int dec, const juce::String& suffix) { return juce::String (v, dec) + suffix; }

std::unique_ptr<juce::AudioParameterFloat> makeFloat (const juce::String& id, const juce::String& name,
                                                       float lo, float hi, float def, float skewCentre,
                                                       std::function<juce::String (float, int)> toText,
                                                       float interval = 0.0f)
{
    juce::NormalisableRange<float> r (lo, hi, interval);
    if (skewCentre > lo && skewCentre < hi)
        r.setSkewForCentre (skewCentre);
    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, r, def,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (std::move (toText)));
}

auto dbText  = [] (float v, int) { return fmt (v, 1, " dB"); };
auto pctText = [] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; };
auto hzText  = [] (float v, int) { return v >= 1000.0f ? fmt (v / 1000.0f, 1, " kHz") : juce::String (juce::roundToInt (v)) + " Hz"; };
auto msText  = [] (float v, int) { return v < 10.0f ? fmt (v, 1, " ms") : juce::String (juce::roundToInt (v)) + " ms"; };
auto macroText = [] (float v, int) { return juce::String (juce::roundToInt (v)); };

inline float macroT (float v) { return juce::jlimit (-1.0f, 1.0f, (v - 50.0f) / 50.0f); }

// recorte suave: lineal hasta 0.5, rodilla tanh hasta 1.0 (pendiente continua)
inline float softClip (float x)
{
    const float a = std::abs (x);
    if (a <= 0.5f) return x;
    return std::copysign (0.5f + 0.5f * std::tanh ((a - 0.5f) * 2.0f), x);
}
} // namespace

// ============================================================================
//  Punteros a parámetros (acceso lock-free desde el hilo de audio)
// ============================================================================
struct VoxDnaProcessor::Params
{
    std::atomic<float> *quality, *dryWet, *monoCheck, *inGain, *outGain,
        *mTune, *mBody, *mAir, *mPunch, *mColor, *mSpace, *mWidth, *mEnergy, *mComp, *mDelay, *mReverb,
        *dnaMode, *routeMode, *toneTilt, *ceiling, *softClip, *ioLink, *chainOrder,
        *hpFreq, *gateThr,
        *tuneOn, *tuneKey, *tuneScale, *tuneRetune, *tuneHumanize, *tuneVibrato, *tuneConfidence, *tuneAmount, *formant,
        *tuneTransition, *tuneFlex, *tuneFormantPreserve,
        *deessOn, *deessThr, *deessFreq,
        *eqOn, *eqBody, *eqMud, *eqHarsh, *eqPres, *eqAir, *eqExcite,
        *compOn, *compThr, *compRatio, *compAtt, *compRel, *compMakeup, *compPar, *compModel,
        *optoOn, *optoAmount, *optoGain,
        *satOn, *satMode, *satDrive, *satMix,
        *modOn, *modMode, *modRate, *modDepth, *modMix,
        *dlyOn, *dlyTime, *dlyFb, *dlyDuck, *dlyMix,
        *revOn, *revType, *revDecay, *revPredelay, *revTone, *revDuck, *revMix,
        *layDetune, *harmInterval, *octMode,
        *seqOn, *seqRate, *seqThrow, *seqFilter, *seqGate, *seqPan;
    std::array<std::atomic<float>*, pv::numLayers> layOn {}, layLvl {}, layPan {}, layHp {}, layLp {}, layDrv {}, layDly {}, layRev {};
};

const juce::StringArray& VoxDnaProcessor::chainOrderNames()
{
    static const juce::StringArray n { "EQ > COMP > COLOR", "EQ > COLOR > COMP", "COMP > EQ > COLOR",
                                       "COMP > COLOR > EQ", "COLOR > EQ > COMP", "COLOR > COMP > EQ" };
    return n;
}

std::array<int, 3> VoxDnaProcessor::chainOrderFor (int index)
{
    static constexpr int o[6][3] { { 0, 1, 2 }, { 0, 2, 1 }, { 1, 0, 2 }, { 1, 2, 0 }, { 2, 0, 1 }, { 2, 1, 0 } };
    const int i = juce::jlimit (0, 5, index);
    return { o[i][0], o[i][1], o[i][2] };
}

int VoxDnaProcessor::chainOrderIndex (const std::array<int, 3>& order)
{
    for (int i = 0; i < 6; ++i)
        if (chainOrderFor (i) == order) return i;
    return 0;
}

juce::AudioProcessorValueTreeState::ParameterLayout VoxDnaProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> ps;
    auto boolP = [&] (const juce::String& id, const juce::String& name, bool def)
    { ps.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def)); };
    auto choiceP = [&] (const juce::String& id, const juce::String& name, const juce::StringArray& c, int def)
    { ps.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, c, def)); };

    // ---- STUDIO ----
    // no automatizable: cambiar de modo cambia la latencia (PDC) y no debe ocurrir por automatización
    // v3.1.1: por defecto MONITOR (~7 ms) para poder cantar con el plugin insertado; MIX añade ~45 ms
    ps.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "quality", 1 }, "Quality Mode", qualityNames, 0,
                                                                juce::AudioParameterChoiceAttributes().withAutomatable (false)));
    ps.push_back (makeFloat ("dryWet", "Dry/Wet", 0.0f, 100.0f, 100.0f, 0.0f, pctText));
    boolP ("monoCheck", "Mono Check", false);
    // ---- MACROS (CREATE) ----
    for (auto [id, name] : std::initializer_list<std::pair<const char*, const char*>> {
             { "mTune", "Macro Tune" }, { "mBody", "Macro Body" }, { "mAir", "Macro Air" }, { "mPunch", "Macro Punch" },
             { "mColor", "Macro Saturation" }, { "mSpace", "Macro Space" }, { "mWidth", "Macro Width" }, { "mEnergy", "Macro Energy" },
             { "mComp", "Macro Comp" }, { "mDelay", "Macro Delay" }, { "mReverb", "Macro Reverb" } })
        ps.push_back (makeFloat (id, name, 0.0f, 100.0f, 50.0f, 0.0f, macroText));
    choiceP ("dnaMode", "DNA Mode", dnaModeNames, 1);
    // ---- INPUT / CLEAN ----
    ps.push_back (makeFloat ("inGain", "Input Trim", -24.0f, 24.0f, 0.0f, 0.0f, dbText));
    ps.push_back (makeFloat ("hpFreq", "Low Cut", 20.0f, 500.0f, 75.0f, 120.0f,
                             [] (float v, int) { return v <= 21.0f ? juce::String ("OFF") : hzText (v, 0); }));
    ps.push_back (makeFloat ("gateThr", "Gate", -80.0f, -20.0f, -80.0f, -50.0f,
                             [] (float v, int) { return v <= -79.5f ? juce::String ("OFF") : fmt (v, 0, " dB"); }));
    // ---- DNA TUNE ----
    boolP ("tuneOn", "Tune On", false);
    choiceP ("tuneKey", "Key", PitchCorrector::keyNames(), 0);
    choiceP ("tuneScale", "Scale", PitchCorrector::scaleNames(), 2);
    ps.push_back (makeFloat ("tuneRetune", "Retune Speed", 0.0f, 100.0f, 20.0f, 20.0f, msText));
    ps.push_back (makeFloat ("tuneHumanize", "Humanize", 0.0f, 100.0f, 20.0f, 0.0f, pctText));
    ps.push_back (makeFloat ("tuneVibrato", "Vibrato Protect", 0.0f, 100.0f, 0.0f, 0.0f, pctText));
    ps.push_back (makeFloat ("tuneConfidence", "Confidence Gate", 0.0f, 100.0f, 50.0f, 0.0f, pctText));
    ps.push_back (makeFloat ("tuneAmount", "Tune Amount", 0.0f, 100.0f, 100.0f, 0.0f, pctText));
    ps.push_back (makeFloat ("tuneTransition", "Note Transition", 0.0f, 300.0f, 0.0f, 60.0f, msText));
    ps.push_back (makeFloat ("tuneFlex", "Flex-Tune", 0.0f, 100.0f, 0.0f, 0.0f, pctText));
    boolP ("tuneFormantPreserve", "Formant Preserve", false);
    ps.push_back (makeFloat ("formant", "Formant Shift", -12.0f, 12.0f, 0.0f, 0.0f,
                             [] (float v, int) { return std::abs (v) < 0.05f ? juce::String ("0 st") : (v > 0 ? "+" : "") + fmt (v, 1, " st"); }));
    // ---- DE-ESSER ----
    boolP ("deessOn", "De-Ess On", true);
    ps.push_back (makeFloat ("deessThr", "De-Ess Threshold", -50.0f, 0.0f, -28.0f, 0.0f, dbText));
    ps.push_back (makeFloat ("deessFreq", "De-Ess Freq", 4000.0f, 11000.0f, 7000.0f, 7000.0f, hzText));
    // ---- TONE ----
    boolP ("eqOn", "EQ On", true);
    ps.push_back (makeFloat ("eqBody", "Body (150 Hz)", -12.0f, 12.0f, 0.0f, 0.0f, dbText));
    ps.push_back (makeFloat ("eqMud", "Mud (260 Hz)", -12.0f, 12.0f, 0.0f, 0.0f, dbText));
    ps.push_back (makeFloat ("eqHarsh", "Harsh Dyn (3.2 kHz)", 0.0f, 8.0f, 0.0f, 0.0f,
                             [] (float v, int) { return v < 0.05f ? juce::String ("OFF") : "-" + fmt (v, 1, " dB"); }));
    ps.push_back (makeFloat ("eqPres", "Presence (3.5 kHz)", -12.0f, 12.0f, 0.0f, 0.0f, dbText));
    ps.push_back (makeFloat ("eqAir", "Air (11.5 kHz)", -12.0f, 12.0f, 0.0f, 0.0f, dbText));
    ps.push_back (makeFloat ("eqExcite", "Exciter", 0.0f, 100.0f, 0.0f, 0.0f, pctText));
    // ---- COMPRESSOR ----
    boolP ("compOn", "Comp On", true);
    ps.push_back (makeFloat ("compThr", "Comp Threshold", -50.0f, 0.0f, -18.0f, 0.0f, dbText));
    ps.push_back (makeFloat ("compRatio", "Comp Ratio", 1.0f, 20.0f, 3.0f, 4.0f, [] (float v, int) { return fmt (v, 1, ":1"); }));
    ps.push_back (makeFloat ("compAtt", "Comp Attack", 0.1f, 100.0f, 10.0f, 10.0f, msText));
    ps.push_back (makeFloat ("compRel", "Comp Release", 10.0f, 800.0f, 80.0f, 150.0f, msText));
    ps.push_back (makeFloat ("compMakeup", "Comp Makeup", 0.0f, 24.0f, 2.5f, 0.0f, dbText));
    ps.push_back (makeFloat ("compPar", "Parallel (NY)", 0.0f, 100.0f, 0.0f, 0.0f, pctText));
    choiceP ("compModel", "Comp Model", juce::StringArray { "FET", "VCA" }, 0);
    // ---- OPTO ----
    boolP ("optoOn", "Opto On", false);
    ps.push_back (makeFloat ("optoAmount", "Opto Peak Reduction", 0.0f, 100.0f, 30.0f, 0.0f, pctText));
    ps.push_back (makeFloat ("optoGain", "Opto Gain", 0.0f, 18.0f, 1.0f, 0.0f, dbText));
    // ---- COLOR ----
    boolP ("satOn", "Color On", false);
    choiceP ("satMode", "Color Type", pv::colorModeNames(), 0);
    ps.push_back (makeFloat ("satDrive", "Drive", 0.0f, 100.0f, 60.0f, 0.0f, pctText));
    ps.push_back (makeFloat ("satMix", "Color Mix", 0.0f, 100.0f, 10.0f, 0.0f, pctText));
    // orden de la cadena de inserción: no automatizable (cambia la topología)
    ps.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "chainOrder", 1 }, "Insert Order", chainOrderNames(), 0,
                                                                juce::AudioParameterChoiceAttributes().withAutomatable (false)));
    // ---- MOTION ----
    boolP ("modOn", "Mod On", false);
    choiceP ("modMode", "Mod Type", pv::ModFX::modeNames(), 0);
    ps.push_back (makeFloat ("modRate", "Mod Rate", 0.05f, 5.0f, 0.4f, 0.6f, [] (float v, int) { return fmt (v, 2, " Hz"); }));
    ps.push_back (makeFloat ("modDepth", "Mod Depth", 0.0f, 100.0f, 50.0f, 0.0f, pctText));
    ps.push_back (makeFloat ("modMix", "Mod Mix", 0.0f, 50.0f, 10.0f, 0.0f, pctText));
    // ---- DELAY ----
    boolP ("dlyOn", "Delay On", false);
    choiceP ("dlyTime", "Delay Time", delayDivisions, 3);
    ps.push_back (makeFloat ("dlyFb", "Delay Feedback", 0.0f, 90.0f, 20.0f, 0.0f, pctText));
    ps.push_back (makeFloat ("dlyDuck", "Delay Duck", 0.0f, 12.0f, 5.0f, 0.0f, dbText));
    ps.push_back (makeFloat ("dlyMix", "Delay Mix", 0.0f, 100.0f, 12.0f, 0.0f, pctText));
    // ---- REVERB ----
    boolP ("revOn", "Reverb On", false);
    choiceP ("revType", "Reverb Type", pv::FDNReverb::typeNames(), 0);
    ps.push_back (makeFloat ("revDecay", "Reverb Decay", 0.3f, 6.0f, 1.6f, 1.5f, [] (float v, int) { return fmt (v, 2, " s"); }));
    ps.push_back (makeFloat ("revPredelay", "Reverb Predelay", 0.0f, 120.0f, 30.0f, 30.0f, msText));
    ps.push_back (makeFloat ("revTone", "Reverb Tone (LPF)", 3000.0f, 14000.0f, 8500.0f, 7000.0f, hzText));
    ps.push_back (makeFloat ("revDuck", "Reverb Duck", 0.0f, 12.0f, 4.0f, 0.0f, dbText));
    ps.push_back (makeFloat ("revMix", "Reverb Mix", 0.0f, 100.0f, 15.0f, 0.0f, pctText));
    choiceP ("routeMode", "FX Routing", routeModeNames, 1);
    // ---- LAYERS ----
    ps.push_back (makeFloat ("layDetune", "Double Detune", 0.0f, 25.0f, 8.0f, 0.0f, [] (float v, int) { return fmt (v, 1, " c"); }));
    choiceP ("harmInterval", "Harmony Interval", pv::Harmony::intervalNames(), 0);
    choiceP ("octMode", "Octave Mode", pv::LayerEngine::octaveModeNames(), 0);
    for (int i = 0; i < pv::numLayers; ++i)
    {
        const auto nm = "L" + juce::String (i + 1) + " " + pv::LayerEngine::routeNames()[i];
        const auto s = juce::String (i);
        boolP ("layOn" + s, nm + " On", i == 0);
        ps.push_back (makeFloat ("layLvl" + s, nm + " Level", -60.0f, 6.0f, pv::LayerEngine::defaultLevel[i], -12.0f,
                                 [] (float v, int) { return v <= -59.5f ? juce::String ("-inf") : fmt (v, 1, " dB"); }));
        ps.push_back (makeFloat ("layPan" + s, nm + " Pan", -50.0f, 50.0f, pv::LayerEngine::defaultPan[i], 0.0f,
                                 [] (float v, int) { const int p = juce::roundToInt (v); return p == 0 ? juce::String ("C") : (p < 0 ? "L" : "R") + juce::String (std::abs (p)); }));
        ps.push_back (makeFloat ("layHp" + s, nm + " HPF", 20.0f, 2000.0f, 20.0f, 200.0f,
                                 [] (float v, int) { return v <= 20.5f ? juce::String ("OFF") : hzText (v, 0); }));
        ps.push_back (makeFloat ("layLp" + s, nm + " LPF", 1000.0f, 20000.0f, 20000.0f, 6000.0f,
                                 [] (float v, int) { return v >= 19900.0f ? juce::String ("OFF") : hzText (v, 0); }));
        ps.push_back (makeFloat ("layDrv" + s, nm + " Drive", 0.0f, 100.0f, 0.0f, 0.0f, pctText));
        ps.push_back (makeFloat ("layDly" + s, nm + " Delay Send", 0.0f, 100.0f, 100.0f, 0.0f, pctText));
        ps.push_back (makeFloat ("layRev" + s, nm + " Reverb Send", 0.0f, 100.0f, 100.0f, 0.0f, pctText));
    }
    // ---- SEQUENCE ----
    boolP ("seqOn", "Sequencer On", false);
    choiceP ("seqRate", "Sequencer Rate", pv::StepSequencer::rateNames(), 1);
    ps.push_back (makeFloat ("seqThrow", "Seq Throw", 0.0f, 100.0f, 60.0f, 0.0f, pctText));
    ps.push_back (makeFloat ("seqFilter", "Seq Filter", 0.0f, 100.0f, 0.0f, 0.0f, pctText));
    ps.push_back (makeFloat ("seqGate", "Seq Gate", 0.0f, 100.0f, 0.0f, 0.0f, pctText));
    ps.push_back (makeFloat ("seqPan", "Seq Pan", 0.0f, 100.0f, 0.0f, 0.0f, pctText));
    // ---- GLOBAL / OUTPUT ----
    ps.push_back (makeFloat ("toneTilt", "Tone Tilt", -6.0f, 6.0f, 0.0f, 0.0f,
                             [] (float v, int) { return std::abs (v) < 0.05f ? juce::String ("0.0") : (v > 0 ? "+" : "") + fmt (v, 1, " dB"); }));
    ps.push_back (makeFloat ("outGain", "Master", -24.0f, 12.0f, 0.0f, 0.0f, dbText));
    boolP ("softClip", "Soft Clip", false);
    boolP ("ioLink", "In/Out Link", false);
    ps.push_back (makeFloat ("ceiling", "Ceiling", -6.0f, 0.0f, -1.0f, 0.0f, [] (float v, int) { return fmt (v, 1, " dBTP"); }));

    return { ps.begin(), ps.end() };
}

VoxDnaProcessor::VoxDnaProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "VOX_DNA_PRO", createParameterLayout()),
      P (std::make_unique<Params>())
{
    auto& q = *P;
    q.quality = p ("quality"); q.dryWet = p ("dryWet"); q.monoCheck = p ("monoCheck");
    q.inGain = p ("inGain"); q.outGain = p ("outGain");
    q.mTune = p ("mTune"); q.mBody = p ("mBody"); q.mAir = p ("mAir"); q.mPunch = p ("mPunch");
    q.mColor = p ("mColor"); q.mSpace = p ("mSpace"); q.mWidth = p ("mWidth"); q.mEnergy = p ("mEnergy");
    q.mComp = p ("mComp"); q.mDelay = p ("mDelay"); q.mReverb = p ("mReverb");
    q.dnaMode = p ("dnaMode"); q.routeMode = p ("routeMode"); q.toneTilt = p ("toneTilt"); q.ceiling = p ("ceiling");
    q.softClip = p ("softClip"); q.ioLink = p ("ioLink"); q.chainOrder = p ("chainOrder");
    q.hpFreq = p ("hpFreq"); q.gateThr = p ("gateThr");
    q.tuneOn = p ("tuneOn"); q.tuneKey = p ("tuneKey"); q.tuneScale = p ("tuneScale"); q.tuneRetune = p ("tuneRetune");
    q.tuneHumanize = p ("tuneHumanize"); q.tuneVibrato = p ("tuneVibrato"); q.tuneConfidence = p ("tuneConfidence");
    q.tuneAmount = p ("tuneAmount"); q.formant = p ("formant");
    q.tuneTransition = p ("tuneTransition"); q.tuneFlex = p ("tuneFlex"); q.tuneFormantPreserve = p ("tuneFormantPreserve");
    q.deessOn = p ("deessOn"); q.deessThr = p ("deessThr"); q.deessFreq = p ("deessFreq");
    q.eqOn = p ("eqOn"); q.eqBody = p ("eqBody"); q.eqMud = p ("eqMud"); q.eqHarsh = p ("eqHarsh");
    q.eqPres = p ("eqPres"); q.eqAir = p ("eqAir"); q.eqExcite = p ("eqExcite");
    q.compOn = p ("compOn"); q.compThr = p ("compThr"); q.compRatio = p ("compRatio"); q.compAtt = p ("compAtt");
    q.compRel = p ("compRel"); q.compMakeup = p ("compMakeup"); q.compPar = p ("compPar"); q.compModel = p ("compModel");
    q.optoOn = p ("optoOn"); q.optoAmount = p ("optoAmount"); q.optoGain = p ("optoGain");
    q.satOn = p ("satOn"); q.satMode = p ("satMode"); q.satDrive = p ("satDrive"); q.satMix = p ("satMix");
    q.modOn = p ("modOn"); q.modMode = p ("modMode"); q.modRate = p ("modRate"); q.modDepth = p ("modDepth"); q.modMix = p ("modMix");
    q.dlyOn = p ("dlyOn"); q.dlyTime = p ("dlyTime"); q.dlyFb = p ("dlyFb"); q.dlyDuck = p ("dlyDuck"); q.dlyMix = p ("dlyMix");
    q.revOn = p ("revOn"); q.revType = p ("revType"); q.revDecay = p ("revDecay"); q.revPredelay = p ("revPredelay");
    q.revTone = p ("revTone"); q.revDuck = p ("revDuck"); q.revMix = p ("revMix");
    q.layDetune = p ("layDetune"); q.harmInterval = p ("harmInterval"); q.octMode = p ("octMode");
    q.seqOn = p ("seqOn"); q.seqRate = p ("seqRate"); q.seqThrow = p ("seqThrow"); q.seqFilter = p ("seqFilter");
    q.seqGate = p ("seqGate"); q.seqPan = p ("seqPan");
    for (int i = 0; i < pv::numLayers; ++i)
    {
        const auto s = juce::String (i);
        q.layOn[(size_t) i] = p ("layOn" + s);   q.layLvl[(size_t) i] = p ("layLvl" + s);  q.layPan[(size_t) i] = p ("layPan" + s);
        q.layHp[(size_t) i] = p ("layHp" + s);   q.layLp[(size_t) i] = p ("layLp" + s);    q.layDrv[(size_t) i] = p ("layDrv" + s);
        q.layDly[(size_t) i] = p ("layDly" + s); q.layRev[(size_t) i] = p ("layRev" + s);
    }
    apvts.state.setProperty ("macroSlots", "mTune,formant,mComp,mColor,mDelay,mReverb,mWidth,dryWet", nullptr);
    startTimerHz (15);
}

VoxDnaProcessor::~VoxDnaProcessor() { stopTimer(); }

bool VoxDnaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    if (in != juce::AudioChannelSet::stereo() && in != juce::AudioChannelSet::mono()) return false;
    return ! (in == juce::AudioChannelSet::stereo() && out == juce::AudioChannelSet::mono());
}

int VoxDnaProcessor::chainLatency (int quality) const
{
    // DNA TUNE (retardo central fijo, también con el tune apagado)
    int lat = PitchCorrector::centreDelayFor (fs, quality == monitor);
    if (quality != monitor)
    {
        const auto& os = quality == renderHQ ? os4 : os2;
        lat += FormantShifter::latencySamples() + juce::roundToInt (os.getLatencyInSamples());
    }
    return lat;
}

int VoxDnaProcessor::computeLatency (int quality) const
{
    return chainLatency (quality) + limiter.latencySamples();
}

// Se ejecuta en el hilo de audio SÓLO cuando la salida ya está a 0 (fundido).
void VoxDnaProcessor::switchQualityInSilence (int q)
{
    q = juce::jlimit (0, 2, q);
    pitch.setLowLatency (q == monitor);
    formant.reset();
    os2.reset();
    os4.reset();
    color1.reset();
    color2.reset();
    color4.reset();
    dryDelayL.clear();
    dryDelayR.clear();
    activeQuality.store (q);
    reportedLatency.store (computeLatency (q));
    latencyDirty.store (true);
}

void VoxDnaProcessor::timerCallback()
{
    if (latencyDirty.exchange (false))
        setLatencySamples (reportedLatency.load());
}

void VoxDnaProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fs = sampleRate;
    maxBlock = juce::jmax (1, samplesPerBlock);

    inGainSm.reset (fs, 0.03);  inGainSm.setCurrentAndTargetValue (pv::fromDb (*P->inGain));
    outGainSm.reset (fs, 0.03); outGainSm.setCurrentAndTargetValue (pv::fromDb (*P->outGain));
    for (auto* s : { &satMixSm, &parSm, &dryWetSm, &fadeGate, &fadeDeess, &fadeEq, &fadeComp, &fadeOpto, &fadeSoft })
        s->reset (fs, 0.03);
    satMixSm.setCurrentAndTargetValue (0.0f);
    parSm.setCurrentAndTargetValue (0.0f);
    driveSm.reset (fs, 0.05);
    driveSm.setCurrentAndTargetValue (*P->satDrive * 0.01f);
    ceilingSm.reset (fs, 0.1);
    ceilingSm.setCurrentAndTargetValue (*P->ceiling);
    tiltSm.reset (fs, 0.05);
    tiltSm.setCurrentAndTargetValue (*P->toneTilt);
    colorModeActive = juce::jlimit (0, 4, (int) *P->satMode);
    dryWetSm.setCurrentAndTargetValue (*P->dryWet * 0.01f);
    fadeGate.setCurrentAndTargetValue (1.0f);
    fadeDeess.setCurrentAndTargetValue (*P->deessOn > 0.5f ? 1.0f : 0.0f);
    fadeEq.setCurrentAndTargetValue (*P->eqOn > 0.5f ? 1.0f : 0.0f);
    fadeComp.setCurrentAndTargetValue (*P->compOn > 0.5f ? 1.0f : 0.0f);
    fadeOpto.setCurrentAndTargetValue (*P->optoOn > 0.5f ? 1.0f : 0.0f);
    fadeSoft.setCurrentAndTargetValue (*P->softClip > 0.5f ? 1.0f : 0.0f);

    auto initFilter = [] (Filter& f)
    {
        f.coefficients = new juce::dsp::IIR::Coefficients<float> (1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);   // objeto propio, se reescribe sin allocs
        f.reset();
    };
    for (auto* arr : { &splitHpF, &splitLpF }) for (auto& f : *arr) initFilter (f);
    for (auto* arr : { &hp1, &hp2, &eqBodyF, &eqMudF, &eqPresF, &eqAirF, &tiltLoF, &tiltHiF })
        for (auto& f : *arr)
        {
            f.coefficients = new juce::dsp::IIR::Coefficients<float> (1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);   // objeto propio, se reescribe sin allocs
            f.reset();
        }
    {
        using A = juce::dsp::IIR::ArrayCoefficients<float>;
        const auto h = A::makeHighPass (fs, 300.0f, 0.707f);
        const auto l = A::makeLowPass (fs, (float) juce::jmin (8000.0, fs * 0.45), 0.707f);
        for (auto& f : splitHpF) *f.coefficients = h;
        for (auto& f : splitLpF) *f.coefficients = l;
    }
    lastHp = -1; lastBody = lastMud = lastPres = lastAir = lastTilt = -100;
    hpSm.reset (fs, 0.05);   hpSm.setCurrentAndTargetValue (juce::jmax (20.0f, P->hpFreq->load()));
    for (auto* sm : { &bodySm, &mudSm, &presSm, &airSm }) sm->reset (fs, 0.05);
    bodySm.setCurrentAndTargetValue (*P->eqBody); mudSm.setCurrentAndTargetValue (*P->eqMud);
    presSm.setCurrentAndTargetValue (*P->eqPres); airSm.setCurrentAndTargetValue (*P->eqAir);
    updateHpf (hpSm.getCurrentValue());
    updateEq (*P->eqBody, *P->eqMud, *P->eqPres, *P->eqAir);
    updateTilt (*P->toneTilt);

    gate.prepare (fs);
    pitch.prepare (fs);
    formant.prepare (fs);
    deesser.prepare (fs, maxBlock);
    harsh.prepare (fs);
    exciter.prepare (fs);
    comp.prepare (fs);
    crush.prepare (fs);
    crush.setParameters (-38.0f, 20.0f, 1.0f, 80.0f, 20.0f);   // rama paralela 20:1 (10-20 dB GR)
    opto.prepare (fs);
    color1.prepare (fs);
    color2.prepare (fs * 2.0);
    color4.prepare (fs * 4.0);
    modfx.prepare (fs);
    ducker.prepare (fs);
    delay.prepare (fs);
    reverb.prepare (fs);
    layers.prepare (fs, maxBlock);
    sequencer.prepare (fs);
    inLoudness.prepare (fs);
    outLoudness.prepare (fs);
    correlation.prepare (fs);

    os2.initProcessing ((size_t) maxBlock);
    os4.initProcessing ((size_t) maxBlock);

    limiter.prepare (fs, *P->ceiling);

    const int maxLat = chainLatency (renderHQ) + 64;
    dryDelayL.prepare (maxLat);
    dryDelayR.prepare (maxLat);

    for (auto* v : { &duckKey, &scratchL, &scratchR, &wetDryL, &wetDryR, &sendDlyL, &sendDlyR, &sendRevL, &sendRevR,
                     &wetL, &wetR, &wet2L, &wet2R, &throwBuf })
        v->assign ((size_t) maxBlock, 0.0f);

    dcX = {}; dcY = {};
    orderFade.reset (fs, 0.01);
    orderFade.setCurrentAndTargetValue (1.0f);
    activeOrder = juce::jlimit (0, 5, (int) *P->chainOrder);
    transitionGain.reset (fs, 0.02);
    transitionGain.setCurrentAndTargetValue (1.0f);
    transitionState.store (idle);
    const int q = juce::jlimit (0, 2, (int) *P->quality);
    switchQualityInSilence (q);   // prepareToPlay no es el hilo de audio: se puede comunicar directamente
    jassert (pitch.latencySamples() == PitchCorrector::centreDelayFor (fs, q == monitor));
    latencyDirty.store (false);
    setLatencySamples (reportedLatency.load());
}

void VoxDnaProcessor::updateHpf (float hp)
{
    using A = juce::dsp::IIR::ArrayCoefficients<float>;
    if (std::abs (hp - lastHp) < 0.05f) return;
    lastHp = hp;
    const auto c1 = A::makeHighPass (fs, hp, 0.5412f);   // Butterworth 4º orden (24 dB/oct)
    const auto c2 = A::makeHighPass (fs, hp, 1.3066f);
    for (auto& f : hp1) *f.coefficients = c1;
    for (auto& f : hp2) *f.coefficients = c2;
}

void VoxDnaProcessor::updateEq (float body, float mud, float pres, float air)
{
    using A = juce::dsp::IIR::ArrayCoefficients<float>;
    // ArrayCoefficients + asignación sobre objetos existentes: cero asignaciones de memoria
    if (std::abs (body - lastBody) > 0.005f)
    { lastBody = body; const auto c = A::makeLowShelf (fs, 150.0f, 0.7f, pv::fromDb (body)); for (auto& f : eqBodyF) *f.coefficients = c; }
    if (std::abs (mud - lastMud) > 0.005f)
    { lastMud = mud; const auto c = A::makePeakFilter (fs, 260.0f, 1.2f, pv::fromDb (mud)); for (auto& f : eqMudF) *f.coefficients = c; }
    if (std::abs (pres - lastPres) > 0.005f)
    { lastPres = pres; const auto c = A::makePeakFilter (fs, 3500.0f, 1.0f, pv::fromDb (pres)); for (auto& f : eqPresF) *f.coefficients = c; }
    if (std::abs (air - lastAir) > 0.005f)
    { lastAir = air; const auto c = A::makeHighShelf (fs, (float) juce::jmin (11500.0, fs * 0.42), 0.7f, pv::fromDb (air)); for (auto& f : eqAirF) *f.coefficients = c; }
}

// Tone tilt: shelf grave -t/2 y shelf agudo +t/2 cruzando en 1 kHz
void VoxDnaProcessor::updateTilt (float db)
{
    using A = juce::dsp::IIR::ArrayCoefficients<float>;
    if (std::abs (db - lastTilt) < 0.005f) return;
    lastTilt = db;
    const auto lo = A::makeLowShelf (fs, 1000.0f, 0.5f, pv::fromDb (-0.5f * db));
    const auto hi = A::makeHighShelf (fs, 1000.0f, 0.5f, pv::fromDb (0.5f * db));
    for (auto& f : tiltLoF) *f.coefficients = lo;
    for (auto& f : tiltHiF) *f.coefficients = hi;
}

void VoxDnaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();
    if (total == 0) return;

    // Si el host entrega más muestras de las anunciadas en prepareToPlay, se procesa en
    // sub-bloques que referencian el mismo buffer (sin copiar ni asignar memoria).
    for (int start = 0; start < total; start += maxBlock)
    {
        const int len = juce::jmin (maxBlock, total - start);
        if (start == 0 && len == total)
        {
            processChunk (buffer);
        }
        else
        {
            juce::AudioBuffer<float> sub (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), start, len);
            processChunk (sub);
        }
    }
}

void VoxDnaProcessor::processChunk (juce::AudioBuffer<float>& buffer)
{
    const int n = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    jassert (n <= maxBlock);

    if (numIn == 1 && numOut >= 2)
        buffer.copyFrom (1, 0, buffer, 0, 0, n);
    for (int c = juce::jmax (numIn, numIn == 1 ? 2 : numIn); c < numOut; ++c)
        buffer.clear (c, 0, n);

    const int nch = juce::jmin (2, numOut);
    float* L = buffer.getWritePointer (0);
    float* R = nch > 1 ? buffer.getWritePointer (1) : nullptr;
    auto& q = *P;

    // ---------------- modo de calidad: transición segura ----------------
    {
        const int wanted = juce::jlimit (0, 2, (int) *q.quality);
        const int state = transitionState.load();
        if (state == idle && wanted != activeQuality.load())
        {
            transitionState.store (fadingOut);
            transitionGain.setTargetValue (0.0f);
        }
        else if (state == fadingOut && ! transitionGain.isSmoothing())
        {
            switchQualityInSilence (wanted);
            settleRemaining = chainLatency (wanted);
            transitionState.store (settling);
        }
        else if (state == settling)
        {
            settleRemaining -= n;
            if (settleRemaining <= 0)
            {
                transitionState.store (fadingIn);
                transitionGain.setTargetValue (1.0f);
            }
        }
        else if (state == fadingIn && ! transitionGain.isSmoothing())
        {
            transitionState.store (idle);
        }
    }
    const int aq = activeQuality.load();
    const int latency = chainLatency (aq);

    // ---------------- macros + DNA MODE (offsets sobre los parámetros base) ----------------
    const float tTune = macroT (*q.mTune), tBody = macroT (*q.mBody), tAir = macroT (*q.mAir), tPunch = macroT (*q.mPunch);
    const float tColor = macroT (*q.mColor), tSpace = macroT (*q.mSpace), tWidth = macroT (*q.mWidth), tEnergy = macroT (*q.mEnergy);
    const float tComp = macroT (*q.mComp), tDelay = macroT (*q.mDelay), tReverb = macroT (*q.mReverb);
    // NATURAL: más suave y orgánico · MODERN: el preset tal cual · EXTREME: todo más marcado
    const int dna = juce::jlimit (0, 2, (int) *q.dnaMode);
    const float mRetune = dna == 0 ? 2.0f : dna == 2 ? 0.35f : 1.0f;
    const float mHuman  = dna == 0 ? 1.4f : dna == 2 ? 0.3f : 1.0f;
    const float mSat    = dna == 0 ? 0.6f : dna == 2 ? 1.6f : 1.0f;
    const float mFx     = dna == 0 ? 0.75f : dna == 2 ? 1.3f : 1.0f;
    const float dThr    = dna == 0 ? 3.0f : dna == 2 ? -3.0f : 0.0f;
    const float mFmt    = dna == 0 ? 0.5f : dna == 2 ? 1.5f : 1.0f;
    const float dWidth  = dna == 0 ? -3.0f : dna == 2 ? 2.0f : 0.0f;

    const float retune   = juce::jlimit (0.0f, 100.0f, *q.tuneRetune * std::exp2 (-2.0f * tTune) * mRetune);
    const float humanize = juce::jlimit (0.0f, 100.0f, *q.tuneHumanize * (1.0f - 0.5f * tTune) * mHuman);
    const float bodyDb   = *q.eqBody + 4.0f * tBody;
    const float mudDb    = *q.eqMud - 1.5f * tBody;
    // v3.2: AIR y COLOR con más recorrido (un macro tiene que notarse como en un plugin de un knob)
    const float airDb    = *q.eqAir + 6.0f * tAir;
    const float presDb   = *q.eqPres + 3.0f * tEnergy + 1.5f * juce::jmax (0.0f, tAir);
    const float excite   = juce::jlimit (0.0f, 1.0f, (*q.eqExcite + 45.0f * juce::jmax (0.0f, tAir)) * 0.01f);
    const float deessThr = *q.deessThr - 4.0f * juce::jmax (0.0f, tAir);
    const float optoAmt  = juce::jlimit (0.0f, 100.0f, *q.optoAmount + 20.0f * tBody + 15.0f * tComp);
    const float compThr  = juce::jlimit (-60.0f, 0.0f, *q.compThr - 8.0f * tComp + dThr);
    const float compAtt  = juce::jlimit (0.1f, 100.0f, *q.compAtt * std::exp2 (1.5f * tPunch));
    const float parallel = juce::jlimit (0.0f, 100.0f, *q.compPar + 30.0f * tPunch + 20.0f * juce::jmax (0.0f, tEnergy));
    const bool  colorOn  = *q.satOn > 0.5f;
    const float colorMix = juce::jlimit (0.0f, 100.0f, ((colorOn ? q.satMix->load() : 0.0f) + 35.0f * tColor) * (colorOn ? mSat : 1.0f)) * 0.01f;
    const float spaceMul = std::exp2 (1.2f * tSpace) * mFx;
    const float dlyMul   = spaceMul * std::exp2 (1.5f * tDelay);
    const float revMul   = spaceMul * std::exp2 (1.5f * tReverb);
    const float revDecay = juce::jlimit (0.3f, 8.0f, *q.revDecay * std::exp2 (0.6f * tReverb));
    const float dlyFb    = juce::jlimit (0.0f, 0.9f, (*q.dlyFb + 15.0f * tDelay) * 0.01f);
    const float duckAdd  = 2.0f * tSpace;
    const float widthDb  = 6.0f * tWidth + dWidth;
    const float energyDrive = juce::jmax (0.0f, tEnergy);
    const float formantSt = *q.formant * mFmt;

    hpSm.setTargetValue (juce::jmax (20.0f, q.hpFreq->load()));
    bodySm.setTargetValue (bodyDb); mudSm.setTargetValue (mudDb);
    presSm.setTargetValue (presDb); airSm.setTargetValue (airDb);
    constexpr int chunk = 32;   // los filtros se actualizan por sub-bloques mientras se suavizan

    // ---------------- dry para Dry/Wet (compensado con la latencia) ----------------
    for (int i = 0; i < n; ++i)
    {
        dryDelayL.push (L[i]);
        dryDelayR.push (R != nullptr ? R[i] : L[i]);
        wetDryL[(size_t) i] = dryDelayL.read ((float) latency);
        wetDryR[(size_t) i] = dryDelayR.read ((float) latency);
    }

    // ---------------- INPUT ----------------
    inGainSm.setTargetValue (pv::fromDb (*q.inGain));
    float inPeak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float g = inGainSm.getNextValue();
        L[i] *= g;
        if (R) R[i] *= g;
        inPeak = juce::jmax (inPeak, std::abs (L[i]), R ? std::abs (R[i]) : 0.0f);
    }
    inputLevel.store (juce::jmax (inPeak, inputLevel.load() * 0.85f));
    inLoudness.process (L, R, n);
    inputFifo.push (L, R, n);

    // helper: módulo con fundido de bypass (sin clicks al activar/desactivar)
    auto faded = [&] (juce::SmoothedValue<float>& fade, bool on, auto&& fn)
    {
        fade.setTargetValue (on ? 1.0f : 0.0f);
        if (! fade.isSmoothing())
        {
            if (fade.getCurrentValue() > 0.5f) fn();
            return;
        }
        std::copy (L, L + n, scratchL.begin());
        if (R) std::copy (R, R + n, scratchR.begin());
        fn();
        for (int i = 0; i < n; ++i)
        {
            const float m = fade.getNextValue();
            L[i] = scratchL[(size_t) i] + m * (L[i] - scratchL[(size_t) i]);
            if (R) R[i] = scratchR[(size_t) i] + m * (R[i] - scratchR[(size_t) i]);
        }
    };

    // ---------------- CLEAN ----------------
    const bool hpWanted = hpSm.getCurrentValue() > 21.0f || hpSm.getTargetValue() > 21.0f;
    if (hpWanted && ! hpActive)
        for (auto* f : { &hp1, &hp2 }) for (auto& x : *f) x.reset();   // v3.2: sin estado viejo al reactivarlo (click)
    hpActive = hpWanted;
    if (hpWanted)
        for (int s0 = 0; s0 < n; s0 += chunk)
        {
            const int len = juce::jmin (chunk, n - s0);
            updateHpf (hpSm.skip (len));
            for (int i = s0; i < s0 + len; ++i)
            {
                L[i] = hp2[0].processSample (hp1[0].processSample (L[i]));
                if (R) R[i] = hp2[1].processSample (hp1[1].processSample (R[i]));
            }
        }
    else
        hpSm.skip (n);

    gate.setThreshold (*q.gateThr);
    faded (fadeGate, ! gate.isBypassed(), [&]
    {
        for (int i = 0; i < n; ++i)
        {
            const float g = gate.process (juce::jmax (std::abs (L[i]), R ? std::abs (R[i]) : 0.0f));
            L[i] *= g;
            if (R) R[i] *= g;
        }
    });

    // ---------------- DNA TUNE ----------------
    const bool tuneOn = *q.tuneOn > 0.5f;
    pitch.setExpression (*q.tuneTransition, *q.tuneFlex);
    pitch.setParameters (tuneOn, (int) *q.tuneKey, (int) *q.tuneScale, retune, humanize, *q.tuneAmount,
                         *q.tuneVibrato, *q.tuneConfidence);
    pitch.process (L, R, n);
    detectedMidi.store (pitch.getDetectedMidi());
    targetMidi.store (tuneOn ? pitch.getTargetMidi() : -1.0f);

    if (aq != monitor)
    {
        // Formant Preserve: deshace el desplazamiento de formantes que produce la corrección
        const float formantComp = *q.tuneFormantPreserve > 0.5f ? -pitch.getAppliedCorrection() : 0.0f;
        formant.setShift (formantSt + formantComp);
        formant.process (L, R, n);
    }

    // ---------------- DE-ESSER ----------------
    faded (fadeDeess, *q.deessOn > 0.5f, [&]
    {
        deesser.setParameters (deessThr, *q.deessFreq);
        deesser.process (L, R, n);
    });
    deessGainReduction.store (*q.deessOn > 0.5f ? deesser.getGainReductionDb() : 0.0f);

    // ---------------- INSERTOS: EQ / COMP+OPTO / COLOR en el orden elegido ----------------
    auto stageEq = [&]
    {
        faded (fadeEq, *q.eqOn > 0.5f, [&]
        {
            for (int s0 = 0; s0 < n; s0 += chunk)
            {
                const int len = juce::jmin (chunk, n - s0);
                updateEq (bodySm.skip (len), mudSm.skip (len), presSm.skip (len), airSm.skip (len));
                for (int c = 0; c < nch; ++c)
                {
                    float* x = buffer.getWritePointer (c);
                    for (int i = s0; i < s0 + len; ++i)
                        x[i] = eqAirF[(size_t) c].processSample (eqPresF[(size_t) c].processSample (
                               eqMudF[(size_t) c].processSample (eqBodyF[(size_t) c].processSample (x[i]))));
                }
            }
            if (*q.eqHarsh > 0.05f) harsh.process (L, R, n, *q.eqHarsh);
            exciter.process (L, R, n, excite);
        });
        if (*q.eqOn < 0.5f && ! fadeEq.isSmoothing())
        {
            bodySm.skip (n); mudSm.skip (n); presSm.skip (n); airSm.skip (n);
        }
    };

    auto stageComp = [&]
    {
        float blockPeak = 0.0f;
        for (int i = 0; i < n; ++i) blockPeak = juce::jmax (blockPeak, std::abs (L[i]), R ? std::abs (R[i]) : 0.0f);
        compInputDb.store (juce::jmax (pv::toDb (blockPeak), compInputDb.load() - 1.5f));

        faded (fadeComp, *q.compOn > 0.5f, [&]
        {
            comp.setModel ((int) *q.compModel == 0 ? pv::Compressor::fet : pv::Compressor::vca);
            comp.setParameters (compThr, *q.compRatio, compAtt, *q.compRel, *q.compMakeup);
            parSm.setTargetValue (parallel * 0.01f);
            for (int i = 0; i < n; ++i)
            {
                const float level = juce::jmax (std::abs (L[i]), R ? std::abs (R[i]) : 0.0f);
                const float g = comp.process (level);
                const float gc = crush.process (level) * 0.4f * parSm.getNextValue();   // NY: rama 20:1 aplastada
                L[i] = comp.applyColour (0, L[i], g) + gc * L[i];
                if (R) R[i] = comp.applyColour (1, R[i], g) + gc * R[i];
            }
        });
        compGainReduction.store (*q.compOn > 0.5f ? comp.getGainReductionDb() : 0.0f);

        faded (fadeOpto, *q.optoOn > 0.5f, [&]
        {
            opto.setParameters (optoAmt, *q.optoGain);
            for (int i = 0; i < n; ++i)
            {
                const float g = opto.process (juce::jmax (std::abs (L[i]), R ? std::abs (R[i]) : 0.0f));
                L[i] *= g;
                if (R) R[i] *= g;
            }
        });
        optoGainReduction.store (*q.optoOn > 0.5f ? opto.getGainReductionDb() : 0.0f);
    };

    auto stageColor = [&]
    {
        // cambio de topología sin click: baja el mix a 0, conmuta y vuelve a subir
        const int wantedMode = juce::jlimit (0, 4, (int) *q.satMode);
        float targetMix = colorMix;
        if (wantedMode != colorModeActive)
        {
            if (satMixSm.getCurrentValue() < 1.0e-4f) colorModeActive = wantedMode;
            else targetMix = 0.0f;
        }
        satMixSm.setTargetValue (targetMix);
        const auto mode = (pv::ColorMode) colorModeActive;
        driveSm.setTargetValue (juce::jlimit (0.0f, 1.0f, (*q.satDrive + (dna == 2 ? 15.0f : 0.0f) + 25.0f * tColor) * 0.01f));
        const float drive = driveSm.skip (n);
        if (aq == monitor)
        {
            color1.process (L, R, n, mode, drive, satMixSm);
        }
        else
        {
            auto& os = aq == renderHQ ? os4 : os2;
            auto& cs = aq == renderHQ ? color4 : color2;
            juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), (size_t) nch, (size_t) n);
            auto up = os.processSamplesUp (block);
            const int un = (int) up.getNumSamples();
            juce::SmoothedValue<float> upMix;
            upMix.reset (un);
            upMix.setCurrentAndTargetValue (satMixSm.getCurrentValue());
            upMix.setTargetValue (satMixSm.getTargetValue());
            satMixSm.skip (n);
            cs.process (up.getChannelPointer (0), nch > 1 ? up.getChannelPointer (1) : nullptr, un, mode, drive, upMix);
            os.processSamplesDown (block);
        }
        if (colorMix > 0.0f || satMixSm.isSmoothing())
        {
            const float rc = 0.9995f;   // DC blocker (el bias genera DC)
            for (int c = 0; c < nch; ++c)
            {
                float* x = buffer.getWritePointer (c);
                for (int i = 0; i < n; ++i)
                {
                    const float y = x[i] - dcX[(size_t) c] + rc * dcY[(size_t) c];
                    dcX[(size_t) c] = x[i];
                    dcY[(size_t) c] = y;
                    x[i] = y;
                }
            }
        }
    };

    // cambio de orden de la cadena: fundido de 10 ms, conmutación en silencio y vuelta
    {
        const int wantedOrder = juce::jlimit (0, 5, (int) *q.chainOrder);
        if (wantedOrder != activeOrder)
        {
            if (orderFade.getCurrentValue() < 1.0e-4f && orderFade.getTargetValue() == 0.0f)
            {
                activeOrder = wantedOrder;
                orderFade.setTargetValue (1.0f);
            }
            else orderFade.setTargetValue (0.0f);
        }
    }
    for (int stage : chainOrderFor (activeOrder))
    {
        if (stage == 0)      stageEq();
        else if (stage == 1) stageComp();
        else                 stageColor();
    }

    // key de ducking (voz procesada, antes de capas y FX)
    for (int i = 0; i < n; ++i)
        duckKey[(size_t) i] = ducker.process (juce::jmax (std::abs (L[i]), R ? std::abs (R[i]) : 0.0f));

    double bpm = 120.0, ppq = -1.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = juce::jlimit (30.0, 300.0, *b);
            if (pos->getIsPlaying())
                if (auto pq = pos->getPpqPosition()) ppq = *pq;
        }
    beatPosition.store (ppq);

    // ---------------- LAYERS (8 capas con cadena propia) ----------------
    {
        std::array<pv::LayerEngine::Settings, pv::numLayers> st;
        for (int r = 0; r < pv::numLayers; ++r)
        {
            auto& s = st[(size_t) r];
            s.on = *q.layOn[(size_t) r] > 0.5f;
            s.levelDb = *q.layLvl[(size_t) r] + ((r == 1 || r == 2) ? widthDb : 0.0f);
            s.pan = *q.layPan[(size_t) r];
            s.hpHz = *q.layHp[(size_t) r];
            s.lpHz = *q.layLp[(size_t) r];
            s.drive01 = *q.layDrv[(size_t) r] * 0.01f;
            s.sendDly01 = *q.layDly[(size_t) r] * 0.01f;
            s.sendRev01 = *q.layRev[(size_t) r] * 0.01f;
        }
        pv::LayerEngine::Global gl;
        gl.detuneCents = *q.layDetune;
        gl.bpm = bpm;
        gl.harmonyInterval = (int) *q.harmInterval;
        gl.octaveMode = (int) *q.octMode;
        gl.key = (int) *q.tuneKey;
        gl.scale = (int) *q.tuneScale;
        gl.noteMidi = tuneOn ? pitch.getTargetMidi() : pitch.getDetectedMidi();
        layers.process (L, R, duckKey.data(), n, st, gl, sendDlyL.data(), sendDlyR.data(), sendRevL.data(), sendRevR.data());
    }

    if (R != nullptr)
    {
        // ---------------- MOTION ----------------
        modfx.process (L, R, n, *q.modOn > 0.5f, (int) *q.modMode, *q.modRate, *q.modDepth * 0.01f,
                       juce::jlimit (0.0f, 50.0f, *q.modMix + 10.0f * juce::jmax (0.0f, tWidth)) * 0.01f);

        // ---------------- SEQUENCE ----------------
        const bool seqOn = *q.seqOn > 0.5f;
        if (seqOn || sequencer.isActive())
        {
            pv::StepSequencer::Depths d { *q.seqThrow * 0.01f, *q.seqFilter * 0.01f, *q.seqGate * 0.01f, *q.seqPan * 0.01f };
            sequencer.process (L, R, n, seqOn, (int) *q.seqRate, bpm, ppq, d, throwBuf.data());
        }
        else
        {
            std::fill (throwBuf.begin(), throwBuf.begin() + n, 0.0f);
            sequencer.currentStep.store (-1);
        }

        // ---------------- ENVÍOS (4 modos de ruteo) ----------------
        const bool dOn = *q.dlyOn > 0.5f, rOn = *q.revOn > 0.5f;
        const bool throwing = seqOn && *q.seqThrow > 0.0f;
        const int route = juce::jlimit (0, 3, (int) *q.routeMode);
        if (dOn || rOn || throwing || delay.isActive() || reverb.isActive())
        {
            // SPLIT: los efectos sólo reciben 300 Hz - 8 kHz (graves y sibilancia quedan secos)
            if (route == 3)
                for (int i = 0; i < n; ++i)
                {
                    sendDlyL[(size_t) i] = splitLpF[0].processSample (splitHpF[0].processSample (sendDlyL[(size_t) i]));
                    sendDlyR[(size_t) i] = splitLpF[1].processSample (splitHpF[1].processSample (sendDlyR[(size_t) i]));
                    sendRevL[(size_t) i] = splitLpF[2].processSample (splitHpF[2].processSample (sendRevL[(size_t) i]));
                    sendRevR[(size_t) i] = splitLpF[3].processSample (splitHpF[3].processSample (sendRevR[(size_t) i]));
                }

            std::fill (wetL.begin(), wetL.begin() + n, 0.0f);
            std::fill (wetR.begin(), wetR.begin() + n, 0.0f);
            std::fill (wet2L.begin(), wet2L.begin() + n, 0.0f);
            std::fill (wet2R.begin(), wet2R.begin() + n, 0.0f);

            if (dOn || throwing || delay.isActive())
            {
                if ((dOn || throwing) && ! wasDelayOn && ! delay.isActive()) delay.reset();
                // THROW: el secuenciador empuja la voz al delay (+12 dB en el paso)
                for (int i = 0; i < n; ++i)
                {
                    const float g = (dOn ? 1.0f : 0.0f) + 3.0f * throwBuf[(size_t) i];
                    sendDlyL[(size_t) i] *= g;
                    sendDlyR[(size_t) i] *= g;
                }
                const int idx = juce::jlimit (0, delayDivisions.size() - 1, (int) *q.dlyTime);
                const float seconds = idx >= 7 ? slapMs[idx - 7] * 0.001f : (float) (60.0 / bpm) * delayBeats[idx];
                const float mix = dOn ? juce::jmin (1.5f, *q.dlyMix * 0.01f * dlyMul) : (throwing ? 0.35f : 0.0f);
                delay.process (sendDlyL.data(), sendDlyR.data(), duckKey.data(), wetL.data(), wetR.data(), n, seconds, idx < 7,
                               dlyFb, mix, juce::jmax (0.0f, *q.dlyDuck + duckAdd));
            }
            if (rOn || reverb.isActive())
            {
                if (rOn && ! wasReverbOn && ! reverb.isActive()) reverb.reset();
                if (route == 0)   // SERIES: los ecos del delay también entran en la reverb
                    for (int i = 0; i < n; ++i)
                    {
                        sendRevL[(size_t) i] += wetL[(size_t) i];
                        sendRevR[(size_t) i] += wetR[(size_t) i];
                    }
                reverb.process (sendRevL.data(), sendRevR.data(), duckKey.data(), wet2L.data(), wet2R.data(), n, (int) *q.revType,
                                revDecay, *q.revPredelay, *q.revTone,
                                rOn ? juce::jmin (1.5f, *q.revMix * 0.01f * revMul) : 0.0f,
                                juce::jmax (0.0f, *q.revDuck + duckAdd));
            }

            // retorno: MID/SIDE abre los efectos a los lados y deja el centro para la voz
            const bool ms = route == 2;
            for (int i = 0; i < n; ++i)
            {
                float wl = wetL[(size_t) i] + wet2L[(size_t) i];
                float wr = wetR[(size_t) i] + wet2R[(size_t) i];
                if (ms)
                {
                    const float m = 0.5f * (wl + wr) * 0.3f, s = 0.5f * (wl - wr) * 1.4f;
                    wl = m + s; wr = m - s;
                }
                L[i] += wl;
                R[i] += wr;
            }
        }
        wasDelayOn = dOn || throwing;
        wasReverbOn = rOn;
    }

    // ---------------- TONE TILT ----------------
    tiltSm.setTargetValue (*q.toneTilt);
    if (std::abs (tiltSm.getCurrentValue()) > 0.01f || tiltSm.isSmoothing())
        for (int s0 = 0; s0 < n; s0 += chunk)
        {
            const int len = juce::jmin (chunk, n - s0);
            updateTilt (tiltSm.skip (len));
            for (int c = 0; c < nch; ++c)
            {
                float* x = buffer.getWritePointer (c);
                for (int i = s0; i < s0 + len; ++i)
                    x[i] = tiltHiF[(size_t) c].processSample (tiltLoF[(size_t) c].processSample (x[i]));
            }
        }
    else
        tiltSm.skip (n);

    // ---------------- MASTER ----------------
    const float masterDb = q.outGain->load() - (q.ioLink->load() > 0.5f ? q.inGain->load() : 0.0f);
    outGainSm.setTargetValue (pv::fromDb (masterDb));
    dryWetSm.setTargetValue (*q.dryWet * 0.01f);
    fadeSoft.setTargetValue (*q.softClip > 0.5f ? 1.0f : 0.0f);
    presetJustLoaded.store (false);
    const bool mono = *q.monoCheck > 0.5f;
    for (int i = 0; i < n; ++i)
    {
        const float g = outGainSm.getNextValue();
        float l = L[i] * g, r = R ? R[i] * g : 0.0f;
        if (energyDrive > 0.0f)   // ENERGY: clip suave que protege picos
        {
            const float k = 1.0f + 2.0f * energyDrive;
            l = std::tanh (k * l) / k * (1.0f + 0.6f * energyDrive);
            r = std::tanh (k * r) / k * (1.0f + 0.6f * energyDrive);
        }
        const float sc = fadeSoft.getNextValue();
        if (sc > 0.0f)
        {
            l += sc * (softClip (l) - l);
            r += sc * (softClip (r) - r);
        }
        const float w = dryWetSm.getNextValue();
        l = wetDryL[(size_t) i] + w * (l - wetDryL[(size_t) i]);
        if (R) r = wetDryR[(size_t) i] + w * (r - wetDryR[(size_t) i]);
        if (mono && R) { const float m = 0.5f * (l + r); l = r = m; }
        const float tg = transitionGain.getNextValue() * orderFade.getNextValue();
        L[i] = l * tg;
        if (R) R[i] = r * tg;
    }

    // limitador true-peak (techo ajustable, lookahead 1 ms, detección 8x)
    ceilingSm.setTargetValue (*q.ceiling);
    limiter.setCeiling (ceilingSm.skip (n));
    limiter.process (L, R, n);
    limiterGainReduction.store (limiter.getGainReductionDb());

    outLoudness.process (L, R, n);
    correlation.process (L, R, n);
    const float tp = limiter.getOutputTruePeak();
    outTruePeak.store (juce::jmax (tp, outTruePeak.load() * 0.9f));
    if (tp > truePeakMax.load()) truePeakMax.store (tp);
    analyzerFifo.push (L, R, n);

    float pl = 0.0f, pr = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        pl = juce::jmax (pl, std::abs (L[i]));
        pr = juce::jmax (pr, R ? std::abs (R[i]) : std::abs (L[i]));
    }
    outPeakL.store (juce::jmax (pl, outPeakL.load() * 0.85f));
    outPeakR.store (juce::jmax (pr, outPeakR.load() * 0.85f));
    outputLevel.store (juce::jmax (juce::jmax (pl, pr), outputLevel.load() * 0.85f));
}

// ============================================================================
//  Presets de fábrica
// ============================================================================
int VoxDnaProcessor::getNumPrograms() { return (int) pv::getFactoryPresets().size(); }

const juce::String VoxDnaProcessor::getProgramName (int index)
{
    const auto& ps = pv::getFactoryPresets();
    return juce::isPositiveAndBelow (index, (int) ps.size()) ? ps[(size_t) index].name : juce::String();
}

void VoxDnaProcessor::setCurrentProgram (int index) { loadFactoryPreset (index); }

void VoxDnaProcessor::applyPresetValues (const pv::PresetValues& v)
{
    for (auto& [id, value] : v.asList())
        if (auto* param = apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
}

void VoxDnaProcessor::setPresetInfo (const juce::String& n, const juce::String& sub)
{
    const juce::ScopedLock sl (presetLock);
    presetName = n;
    presetSubtitle = sub;
}

void VoxDnaProcessor::loadFactoryPreset (int index)
{
    const auto& ps = pv::getFactoryPresets();
    if (! juce::isPositiveAndBelow (index, (int) ps.size())) return;
    currentProgram = index;
    loadedFactory.store (index);
    presetJustLoaded.store (true);
    const auto& pr = ps[(size_t) index];
    undoManager.beginNewTransaction ("Preset " + pr.name);
    applyPresetValues (pr.values);
    grTarget.store (pr.grTarget);
    setPresetInfo (pr.name, pr.evidence + "   //   " + pr.tag + "   //   GR objetivo ~" + juce::String (pr.grTarget, 1)
                                + " dB   //   Key = la de tu cancion");
    presetSerial.fetch_add (1);
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
}

// ============================================================================
//  Migración de capas v2 -> v3
//   v2: 3 LOW, 4 HIGH, 5 ADLIBS, 6 ATMOS, 7 FX
//   v3: 3 ADLIBS, 4 HARMONY, 5 OCTAVE (-1/+1/ambas), 6 TEXTURE, 7 FX
// ============================================================================
void VoxDnaProcessor::migrateLayers (std::function<float (const juce::String&, float)> get,
                                     std::function<void (const juce::String&, float)> set)
{
    auto on  = [&] (int i) { return get ("layOn" + juce::String (i), i == 0 ? 1.0f : 0.0f); };
    auto lvl = [&] (int i, float d) { return get ("layLvl" + juce::String (i), d); };
    auto pan = [&] (int i, float d) { return get ("layPan" + juce::String (i), d); };

    const float lowOn = on (3), highOn = on (4), adlOn = on (5);
    const float lowLvl = lvl (3, -13.0f), highLvl = lvl (4, -12.0f), adlLvl = lvl (5, -12.0f), adlPan = pan (5, 45.0f);

    set ("layOn3", adlOn);  set ("layLvl3", adlLvl); set ("layPan3", adlPan);
    set ("layOn4", 0.0f);   set ("layLvl4", -12.0f); set ("layPan4", 0.0f);
    const bool lo = lowOn > 0.5f, hi = highOn > 0.5f;
    set ("layOn5", lo || hi ? 1.0f : 0.0f);
    set ("layLvl5", lo && hi ? juce::jmax (lowLvl, highLvl) : lo ? lowLvl : hi ? highLvl : -13.0f);
    set ("layPan5", 0.0f);
    set ("octMode", lo && hi ? 2.0f : hi ? 1.0f : 0.0f);
    // 6 (ATMOS -> TEXTURE) y 7 (FX) conservan índice
}

// ============================================================================
//  Presets de usuario: JSON versionado (schema 3) — blueprint cap. 11
// ============================================================================
namespace
{
juce::String unitFor (const juce::String& id)
{
    if (id.endsWith ("On") || id == "monoCheck" || id == "softClip" || id == "ioLink" || id == "tuneFormantPreserve") return "bool";
    if (id == "quality" || id.endsWith ("Mode") || id.endsWith ("Type") || id == "dlyTime" || id == "tuneKey" || id == "tuneScale"
        || id == "chainOrder" || id == "harmInterval" || id == "seqRate") return "choice";
    if (id.startsWith ("m") && id.length() <= 7 && id != "monoCheck") return "macro";
    if (id == "formant") return "st";
    if (id == "layDetune") return "cents";
    if (id == "revDecay") return "s";
    if (id == "modRate") return "Hz";
    if (id.contains ("Freq") || id == "revTone" || id.startsWith ("layHp") || id.startsWith ("layLp")) return "Hz";
    if (id.contains ("Att") || id.contains ("Rel") || id == "tuneRetune" || id == "revPredelay" || id == "tuneTransition") return "ms";
    if (id == "ceiling") return "dBTP";
    if (id.contains ("Gain") || id.contains ("Thr") || id.contains ("Lvl") || id.contains ("Makeup") || id.contains ("Duck")
        || id.startsWith ("eq") || id == "gateThr" || id == "toneTilt") return "dB";
    if (id == "compRatio") return "ratio";
    if (id.startsWith ("layPan")) return "pan";
    return "%";
}

juce::String buildPresetJson (juce::AudioProcessor& proc, const juce::String& name, const juce::String& tag,
                              float grTarget, int quality, const std::array<std::atomic<float>*, pv::numLayers>& layOn)
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("schema", VoxDnaProcessor::presetSchema);
    root->setProperty ("name", name);

    auto* meta = new juce::DynamicObject();
    meta->setProperty ("author", "user");
    meta->setProperty ("unofficial", true);
    meta->setProperty ("plugin", "VOX DNA PRO");
    meta->setProperty ("version", JucePlugin_VersionString);
    meta->setProperty ("tag", tag);
    meta->setProperty ("grTarget", (double) grTarget);
    root->setProperty ("metadata", meta);

    auto* audio = new juce::DynamicObject();
    audio->setProperty ("mode", qualityNames[quality].toLowerCase());
    audio->setProperty ("latencyPolicy", "hostPDC");
    root->setProperty ("audio", audio);

    juce::Array<juce::var> routing;
    for (int r = 0; r < pv::numLayers; ++r)
        if (*layOn[(size_t) r] > 0.5f)
            routing.add (pv::LayerEngine::routeNames()[r].toLowerCase().removeCharacters (" "));
    root->setProperty ("routing", routing);

    auto* params = new juce::DynamicObject();
    for (auto* prm : proc.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            const auto id = rp->getParameterID();
            if (id == "tuneKey" || id == "tuneScale") continue;   // la tonalidad es de la canción
            auto* entry = new juce::DynamicObject();
            entry->setProperty ("value", (double) rp->convertFrom0to1 (rp->getValue()));
            entry->setProperty ("unit", unitFor (id));
            params->setProperty (id, entry);
        }
    root->setProperty ("params", params);
    return juce::JSON::toString (juce::var (root), false);
}
} // namespace

juce::File VoxDnaProcessor::getDataFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("VOX DNA PRO");
}

juce::File VoxDnaProcessor::getUserPresetFolder() { return getDataFolder().getChildFile ("Presets"); }

juce::Array<juce::File> VoxDnaProcessor::getUserPresets() const
{
    auto files = getUserPresetFolder().findChildFiles (juce::File::findFiles, false, "*.vdna");
    files.sort();
    return files;
}

bool VoxDnaProcessor::saveUserPreset (const juce::String& name, const juce::String& tag)
{
    auto folder = getUserPresetFolder();
    if (! folder.createDirectory()) return false;
    const auto json = buildPresetJson (*this, name, tag, grTarget.load(), activeQuality.load(), P->layOn);
    auto file = folder.getChildFile (juce::File::createLegalFileName (name) + ".vdna");
    if (! file.replaceWithText (json)) return false;
    setPresetInfo (name, "Preset de usuario   //   " + (tag.isEmpty() ? juce::String ("sin etiqueta") : tag));
    loadedFactory.store (-1);
    presetSerial.fetch_add (1);
    return true;
}

bool VoxDnaProcessor::exportPreset (const juce::File& file, const juce::String& name)
{
    const auto json = buildPresetJson (*this, name, "export", grTarget.load(), activeQuality.load(), P->layOn);
    return file.replaceWithText (json);
}

bool VoxDnaProcessor::loadUserPreset (const juce::File& file)
{
    const auto parsed = juce::JSON::parse (file);
    if (! parsed.isObject()) return false;
    const int schema = (int) parsed.getProperty ("schema", 0);
    if (schema < 1 || schema > presetSchema) return false;   // esquema futuro: no se toca el estado

    const auto params = parsed.getProperty ("params", juce::var());
    if (! params.isObject()) return false;

    // valores del archivo; ausentes -> valor por defecto; desconocidos -> se ignoran
    std::map<juce::String, float> values;
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            const auto id = rp->getParameterID();
            const auto entry = params.getProperty (id, juce::var());
            values[id] = entry.isObject() ? (float) (double) entry.getProperty ("value", 0.0)
                                          : rp->convertFrom0to1 (rp->getDefaultValue());
        }
    if (schema < 3)
        migrateLayers ([&] (const juce::String& id, float d)
                       {
                           const auto entry = params.getProperty (id, juce::var());
                           return entry.isObject() ? (float) (double) entry.getProperty ("value", (double) d) : d;
                       },
                       [&] (const juce::String& id, float v) { values[id] = v; });

    undoManager.beginNewTransaction ("User preset");
    presetJustLoaded.store (true);
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            const auto id = rp->getParameterID();
            if (id == "tuneKey" || id == "tuneScale" || id == "quality") continue;
            rp->setValueNotifyingHost (rp->convertTo0to1 (values[id]));
        }
    const auto meta = parsed.getProperty ("metadata", juce::var());
    grTarget.store ((float) (double) meta.getProperty ("grTarget", 3.0));
    const juce::String tag = meta.getProperty ("tag", "").toString();
    setPresetInfo (parsed.getProperty ("name", file.getFileNameWithoutExtension()).toString(),
                   "Preset de usuario   //   " + (tag.isEmpty() ? juce::String ("sin etiqueta") : tag));
    loadedFactory.store (-1);
    presetSerial.fetch_add (1);
    return true;
}

// ============================================================================
//  A/B
// ============================================================================
void VoxDnaProcessor::selectAB (int slot)
{
    slot = juce::jlimit (0, 1, slot);
    if (slot == abSlot) return;
    writeExtraState();
    abStates[(size_t) abSlot] = apvts.copyState();
    if (abStates[(size_t) slot].isValid())
    {
        presetJustLoaded.store (true);
        apvts.replaceState (abStates[(size_t) slot].createCopy());
        syncParametersFromState();
        readExtraState();
    }
    abSlot = slot;
}

void VoxDnaProcessor::copyCurrentToOtherSlot() { writeExtraState(); abStates[(size_t) (1 - abSlot)] = apvts.copyState(); }

// ============================================================================
//  Estado extra (pasos del secuenciador, macros asignados, referencia)
// ============================================================================
void VoxDnaProcessor::writeExtraState()
{
    for (int l = 0; l < pv::StepSequencer::numLanes; ++l)
    {
        juce::StringArray v;
        for (int s = 0; s < pv::StepSequencer::numSteps; ++s)
            v.add (juce::String (sequencer.steps[(size_t) l][(size_t) s].load(), 3));
        apvts.state.setProperty ("seq" + juce::String (l), v.joinIntoString (","), nullptr);
    }
}

void VoxDnaProcessor::readExtraState()
{
    for (int l = 0; l < pv::StepSequencer::numLanes; ++l)
    {
        const auto txt = apvts.state.getProperty ("seq" + juce::String (l)).toString();
        if (txt.isEmpty()) continue;
        juce::StringArray v;
        v.addTokens (txt, ",", "");
        for (int s = 0; s < pv::StepSequencer::numSteps && s < v.size(); ++s)
            sequencer.steps[(size_t) l][(size_t) s].store (juce::jlimit (0.0f, 1.0f, v[s].getFloatValue()));
    }
    if (! apvts.state.hasProperty ("macroSlots"))
        apvts.state.setProperty ("macroSlots", "mTune,formant,mComp,mColor,mDelay,mReverb,mWidth,dryWet", nullptr);
}

juce::String VoxDnaProcessor::getMacroSlots() const { return apvts.state.getProperty ("macroSlots").toString(); }
void VoxDnaProcessor::setMacroSlots (const juce::String& s) { apvts.state.setProperty ("macroSlots", s, nullptr); }
juce::String VoxDnaProcessor::getReferenceName() const { return apvts.state.getProperty ("reference").toString(); }
void VoxDnaProcessor::setReferenceName (const juce::String& n) { apvts.state.setProperty ("reference", n, nullptr); }

// ============================================================================
//  Estado de sesión
// ============================================================================
void VoxDnaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    writeExtraState();
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    xml->setAttribute ("schema", presetSchema);
    xml->setAttribute ("presetName", getPresetName());
    xml->setAttribute ("presetSubtitle", getPresetSubtitle());
    xml->setAttribute ("program", currentProgram);
    xml->setAttribute ("factory", loadedFactory.load());
    xml->setAttribute ("grTarget", (double) grTarget.load());
    copyXmlToBinary (*xml, destData);
}

void VoxDnaProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;

    auto tree = juce::ValueTree::fromXml (*xml);
    if (xml->getIntAttribute ("schema", 2) < 3)
    {
        // sesión de la v2.x: las capas cambiaron de índice
        auto findParam = [&tree] (const juce::String& id) { return tree.getChildWithProperty ("id", id); };
        migrateLayers ([&] (const juce::String& id, float d)
                       {
                           auto c = findParam (id);
                           return c.isValid() && c.hasProperty ("value") ? (float) c.getProperty ("value") : d;
                       },
                       [&] (const juce::String& id, float v)
                       {
                           auto c = findParam (id);
                           if (! c.isValid())
                           {
                               c = juce::ValueTree ("PARAM");
                               c.setProperty ("id", id, nullptr);
                               tree.appendChild (c, nullptr);
                           }
                           c.setProperty ("value", v, nullptr);
                       });
    }
    apvts.replaceState (tree);
    syncParametersFromState();
    readExtraState();
    currentProgram = xml->getIntAttribute ("program", 0);
    loadedFactory.store (xml->getIntAttribute ("factory", currentProgram));
    grTarget.store ((float) xml->getDoubleAttribute ("grTarget", 3.0));
    setPresetInfo (xml->getStringAttribute ("presetName", "INIT // CLEAN"),
                   xml->getStringAttribute ("presetSubtitle", ""));
    presetSerial.fetch_add (1);
}

// APVTS::replaceState no reescribe un parámetro cuyo valor desnormalizado coincide con el
// del árbol, aunque su valor normalizado real sea otro (p. ej. un bool que el host dejó en
// 0.3). pluginval lo detecta. Aquí se fuerza cada parámetro al valor guardado.
void VoxDnaProcessor::syncParametersFromState()
{
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            const auto child = apvts.state.getChildWithProperty ("id", rp->getParameterID());
            if (! child.isValid() || ! child.hasProperty ("value")) continue;
            const float target = rp->convertTo0to1 ((float) child.getProperty ("value"));
            if (std::abs (rp->getValue() - target) > 1.0e-6f)
                rp->setValueNotifyingHost (target);
        }
}

juce::AudioProcessorEditor* VoxDnaProcessor::createEditor() { return new VoxDnaEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VoxDnaProcessor(); }
