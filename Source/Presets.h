#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <functional>
#include <vector>

// ============================================================================
//  VOX DNA PRO — biblioteca de presets de fábrica
//
//  Fuentes: "Investigación técnica de procesamiento vocal en música urbana"
//  (presets de artista, niveles D1/D2/R) y "VOX DNA PRO Blueprint Técnico"
//  (presets A/B/C y etiquetas por objetivo musical).
//
//  TODOS son prototipos de ingeniería / puntos de partida, no cadenas
//  oficiales ni aprobadas por los artistas.
//    D1 = documentado directamente · D2 = documentación secundaria
//    R  = reconstrucción técnica (números propuestos, no extraídos de sesiones)
//
//  Regla: la tonalidad es de la CANCIÓN -> ningún preset toca KEY / SCALE.
//  Los compresores se expresan en GR objetivo (marca en el medidor GR).
// ============================================================================
namespace pv
{
constexpr int numLayers = 8;

struct PresetValues
{
    float inGain = 0, hpFreq = 75, gateThr = -80;
    float tuneOn = 0, tuneRetune = 20, tuneHumanize = 20, tuneVibrato = 0, tuneConfidence = 50, tuneAmount = 100, formant = 0;
    float tuneTransition = 0, tuneFlex = 0, tuneFormantPreserve = 0;
    float deessOn = 1, deessThr = -28, deessFreq = 7000;
    float eqOn = 1, eqBody = 0, eqMud = 0, eqHarsh = 0, eqPres = 0, eqAir = 0, eqExcite = 0;
    float compOn = 1, compThr = -18, compRatio = 3, compAtt = 10, compRel = 80, compMakeup = 3, compPar = 0, compModel = 0;
    float optoOn = 0, optoAmount = 30, optoGain = 1;
    float satOn = 0, satMode = 0, satDrive = 60, satMix = 10;
    float modOn = 0, modMode = 0, modRate = 0.4f, modDepth = 50, modMix = 10;
    float dlyOn = 0, dlyTime = 3, dlyFb = 20, dlyDuck = 5, dlyMix = 12;
    float revOn = 0, revType = 0, revDecay = 1.6f, revPredelay = 30, revTone = 8500, revDuck = 4, revMix = 15;
    float layDetune = 8, harmInterval = 0, octMode = 0;
    // v3: 1 LEAD, 2 DOUBLE L, 3 DOUBLE R, 4 ADLIBS, 5 HARMONY, 6 OCTAVE, 7 TEXTURE, 8 FX
    std::array<float, numLayers> layOn  { 1, 0, 0, 0, 0, 0, 0, 0 };
    std::array<float, numLayers> layLvl { 0, -9, -9, -12, -12, -13, -18, -15 };
    std::array<float, numLayers> layPan { 0, -35, 35, 45, 0, 0, 0, 0 };
    std::array<float, numLayers> layHp  { 20, 20, 20, 20, 20, 20, 20, 20 };
    std::array<float, numLayers> layLp  { 20000, 20000, 20000, 20000, 20000, 20000, 20000, 20000 };
    std::array<float, numLayers> layDrv {};
    std::array<float, numLayers> layDly { 100, 100, 100, 100, 100, 100, 100, 100 };
    std::array<float, numLayers> layRev { 100, 100, 100, 100, 100, 100, 100, 100 };
    float outGain = 0, dryWet = 100, routeMode = 1, toneTilt = 0, chainOrder = 0;

    std::vector<std::pair<juce::String, float>> asList() const
    {
        std::vector<std::pair<juce::String, float>> v {
            { "inGain", inGain }, { "hpFreq", hpFreq }, { "gateThr", gateThr },
            { "tuneOn", tuneOn }, { "tuneRetune", tuneRetune }, { "tuneHumanize", tuneHumanize },
            { "tuneVibrato", tuneVibrato }, { "tuneConfidence", tuneConfidence }, { "tuneAmount", tuneAmount }, { "formant", formant },
            { "tuneTransition", tuneTransition }, { "tuneFlex", tuneFlex }, { "tuneFormantPreserve", tuneFormantPreserve },
            { "deessOn", deessOn }, { "deessThr", deessThr }, { "deessFreq", deessFreq },
            { "eqOn", eqOn }, { "eqBody", eqBody }, { "eqMud", eqMud }, { "eqHarsh", eqHarsh }, { "eqPres", eqPres },
            { "eqAir", eqAir }, { "eqExcite", eqExcite },
            { "compOn", compOn }, { "compThr", compThr }, { "compRatio", compRatio }, { "compAtt", compAtt },
            { "compRel", compRel }, { "compMakeup", compMakeup }, { "compPar", compPar }, { "compModel", compModel },
            { "optoOn", optoOn }, { "optoAmount", optoAmount }, { "optoGain", optoGain },
            { "satOn", satOn }, { "satMode", satMode }, { "satDrive", satDrive }, { "satMix", satMix },
            { "modOn", modOn }, { "modMode", modMode }, { "modRate", modRate }, { "modDepth", modDepth }, { "modMix", modMix },
            { "dlyOn", dlyOn }, { "dlyTime", dlyTime }, { "dlyFb", dlyFb }, { "dlyDuck", dlyDuck }, { "dlyMix", dlyMix },
            { "revOn", revOn }, { "revType", revType }, { "revDecay", revDecay }, { "revPredelay", revPredelay },
            { "revTone", revTone }, { "revDuck", revDuck }, { "revMix", revMix },
            { "layDetune", layDetune }, { "harmInterval", harmInterval }, { "octMode", octMode },
            { "outGain", outGain }, { "dryWet", dryWet }, { "routeMode", routeMode }, { "toneTilt", toneTilt },
            { "chainOrder", chainOrder } };
        for (int i = 0; i < numLayers; ++i)
        {
            const auto s = juce::String (i);
            v.push_back ({ "layOn" + s, layOn[(size_t) i] });
            v.push_back ({ "layLvl" + s, layLvl[(size_t) i] });
            v.push_back ({ "layPan" + s, layPan[(size_t) i] });
            v.push_back ({ "layHp" + s, layHp[(size_t) i] });
            v.push_back ({ "layLp" + s, layLp[(size_t) i] });
            v.push_back ({ "layDrv" + s, layDrv[(size_t) i] });
            v.push_back ({ "layDly" + s, layDly[(size_t) i] });
            v.push_back ({ "layRev" + s, layRev[(size_t) i] });
        }
        // los macros vuelven a neutro con cada preset
        for (auto id : { "mTune", "mBody", "mAir", "mPunch", "mColor", "mSpace", "mWidth", "mEnergy", "mComp", "mDelay", "mReverb" })
            v.push_back ({ id, 50.0f });
        return v;
    }
};

struct FactoryPreset
{
    juce::String name;
    juce::String evidence;   // D1 / D2 / R / Blueprint
    float grTarget = 0.0f;   // dB de reducción objetivo (marca en el medidor)
    PresetValues values;
    juce::String category;   // sección del navegador
    juce::String tag;        // objetivo musical (blueprint cap. 11)
    // v3.0: presentación en el navegador y en el arte generativo
    juce::String artist;     // "inspirado en" (o VOX DNA para las plantillas propias)
    juce::String title;      // nombre en clave
    juce::String genre;      // URBAN / TRAP / R&B / POP
    juce::StringArray tags;  // 3 descriptores
};

// nombre visible: "Artista - TITULO"
inline juce::String displayName (const FactoryPreset& p) { return p.artist + " - " + p.title; }

// descripción generada a partir de los valores reales del preset (nunca promete más de lo que hace)
inline juce::String describe (const FactoryPreset& p)
{
    const auto& v = p.values;
    juce::StringArray parts;
    if (v.tuneOn > 0.5f)
        parts.add (v.tuneRetune <= 8.0f ? "afinacion dura (" + juce::String (juce::roundToInt (v.tuneRetune)) + " ms)"
                                        : "afinacion natural (" + juce::String (juce::roundToInt (v.tuneRetune)) + " ms)");
    if (std::abs (v.formant) >= 0.5f) parts.add ("formantes " + juce::String (v.formant > 0 ? "+" : "") + juce::String (v.formant, 1) + " st");
    if (v.compOn > 0.5f) parts.add (juce::String (v.compModel > 0.5f ? "VCA" : "FET") + " ~" + juce::String (p.grTarget, 1) + " dB GR");
    if (v.optoOn > 0.5f) parts.add ("opto");
    if (v.satOn > 0.5f)
    {
        static const char* names[] { "tubo", "cinta", "preamp", "consola", "rage clip" };
        parts.add (juce::String (names[juce::jlimit (0, 4, (int) v.satMode)]) + " " + juce::String (juce::roundToInt (v.satMix)) + " %");
    }
    if (v.dlyOn > 0.5f)
    {
        static const char* d[] { "1/16", "1/8T", "1/8", "1/8D", "1/4", "1/4D", "1/2", "slap 80", "slap 90", "slap 100", "slap 105", "slap 120", "slap 140" };
        parts.add ("delay " + juce::String (d[juce::jlimit (0, 12, (int) v.dlyTime)]));
    }
    if (v.revOn > 0.5f)
    {
        static const char* t[] { "plate", "hall", "room" };
        parts.add (juce::String (t[juce::jlimit (0, 2, (int) v.revType)]) + " " + juce::String (v.revDecay, 1) + " s");
    }
    int layersOn = 0;
    for (int i = 1; i < numLayers; ++i) layersOn += v.layOn[(size_t) i] > 0.5f ? 1 : 0;
    if (v.layOn[1] > 0.5f && v.layOn[2] > 0.5f) parts.add ("dobles abiertos");
    else if (layersOn > 0) parts.add (juce::String (layersOn) + " capa" + (layersOn > 1 ? "s" : ""));
    auto txt = parts.joinIntoString (", ");
    return txt.substring (0, 1).toUpperCase() + txt.substring (1) + ".";
}

inline const juce::StringArray& presetTags()
{
    static const juce::StringArray t { "Dark Space", "Melodic Intimate", "Rage Edge", "Clean Rap", "Airy R&B", "Experimental Formant" };
    return t;
}

// Índices de DELAY TIME
enum DelayIdx { d16 = 0, d8T, d8, d8D, d4, d4D, d2, slap80, slap90, slap100, slap105, slap120, slap140 };
// Modos de color / modulación / reverb
enum { tube = 0, tape, preampA, console, rageClip };
enum { chorusMode = 0, flangerMode, phaserMode };
enum { plateType = 0, hallType, roomType };

namespace build
{
    // v2.2: el estilo de afinación define también la transición entre notas y la
    // conservación de formantes. Hard-tune (<= 8 ms): saltos secos, sin compensar formantes
    // (el desplazamiento forma parte del sonido). Afinación natural: deslizamientos y timbre intacto.
    inline void tune (PresetValues& v, float retuneMs, float humanize)
    {
        v.tuneOn = 1; v.tuneRetune = retuneMs; v.tuneHumanize = humanize; v.tuneAmount = 100;
        v.tuneTransition = retuneMs <= 8.0f ? 0.0f : juce::jmin (90.0f, retuneMs * 2.5f);
        v.tuneFormantPreserve = retuneMs >= 15.0f ? 1.0f : 0.0f;
        v.tuneFlex = retuneMs >= 25.0f ? 25.0f : 0.0f;
    }

    inline void eq (PresetValues& v, float hpf, float mud, float harsh, float pres, float air, float body = 0)
    { v.hpFreq = hpf; v.eqMud = mud; v.eqHarsh = harsh; v.eqPres = pres; v.eqAir = air; v.eqBody = body; }

    // FET a partir del GR objetivo (voz con picos ~ -10 dBFS)
    inline void comp (PresetValues& v, float ratio, float attMs, float relMs, float grTarget, float parallel = 0)
    {
        v.compOn = 1; v.compRatio = ratio; v.compAtt = attMs; v.compRel = relMs;
        v.compThr = juce::jlimit (-50.0f, 0.0f, -10.0f - grTarget * ratio / (ratio - 1.0f));
        v.compMakeup = std::round (grTarget * 0.8f * 2.0f) / 2.0f;
        v.compPar = parallel;
        // v2.3: ataques lentos y ratios suaves (R&B/pop suave) -> VCA limpio; el resto, FET realimentado
        v.compModel = (attMs >= 15.0f && ratio <= 3.0f) ? 1.0f : 0.0f;
    }

    // Opto leveler por GR objetivo aproximado (2-4 dB típico)
    inline void opto (PresetValues& v, float grTarget)
    { v.optoOn = 1; v.optoAmount = juce::jlimit (0.0f, 100.0f, 10.0f + grTarget * 9.0f); v.optoGain = std::round (grTarget * 0.8f); }

    inline void verb (PresetValues& v, float decay, float predelay, float mix, float duck = 4, float tone = 8500)
    {
        v.revOn = 1; v.revDecay = decay; v.revPredelay = predelay; v.revMix = mix; v.revDuck = duck; v.revTone = tone;
        v.revType = decay >= 2.3f ? (float) hallType : decay <= 0.9f ? (float) roomType : (float) plateType;
    }

    inline void delay (PresetValues& v, int idx, float fb, float mix, float duck = 4)
    { v.dlyOn = 1; v.dlyTime = (float) idx; v.dlyFb = fb; v.dlyMix = mix; v.dlyDuck = duck; }

    // doble artificial = capas DOUBLE L/R (ancho % -> nivel), + modulación opcional
    inline void width (PresetValues& v, float widthPct, float detuneCents, float flangePct = 0)
    {
        if (widthPct > 0.0f)
        {
            v.layOn[1] = v.layOn[2] = 1;
            v.layLvl[1] = v.layLvl[2] = juce::jlimit (-24.0f, -6.0f, -18.0f + widthPct * 0.2f);
            v.layDetune = detuneCents;
        }
        if (flangePct > 0.0f) { v.modOn = 1; v.modMode = flangerMode; v.modMix = flangePct; v.modRate = 0.25f; }
    }

    inline void sat (PresetValues& v, float parallelPct, float drive = 60, int mode = tube)
    { v.satOn = 1; v.satMix = parallelPct; v.satDrive = drive; v.satMode = (float) mode; }

    inline void layer (PresetValues& v, int idx, float levelDb, float pan)
    { v.layOn[(size_t) idx] = 1; v.layLvl[(size_t) idx] = levelDb; v.layPan[(size_t) idx] = pan; }
}

inline const std::vector<FactoryPreset>& getFactoryPresets()
{
    static const std::vector<FactoryPreset> presets = []
    {
        using namespace build;
        std::vector<FactoryPreset> p;
        juce::String cat;
        auto add = [&p, &cat] (const char* name, const char* evidence, float gr, std::function<void (PresetValues&)> fn,
                               const char* tag = "")
        {
            PresetValues v;
            fn (v);
            p.push_back ({ name, evidence, gr, v, cat, tag, {}, {}, {}, {} });
        };

        // ===================== BLUEPRINT: OBJETIVOS MUSICALES =====================
        cat = "OBJETIVOS // BLUEPRINT";
        add ("INIT // CLEAN", "Punto de partida neutro", 3.0f, [] (PresetValues& v) { comp (v, 3, 10, 80, 3); }, "Clean Rap");

        add ("ASTRO ATMOS", "Blueprint A: trap espacial oscuro", 5.0f, [] (PresetValues& v) {
            tune (v, 12, 20); eq (v, 80, -2, 0, 2, 0);
            comp (v, 4, 3, 90, 5); opto (v, 2);
            v.deessFreq = 7000; sat (v, 22, 30, tube);
            verb (v, 1.7f, 35, 20, 4); v.revType = plateType;
            delay (v, d4, 28, 14, 5);
            width (v, 45, 7);
            layer (v, 6, -20, 0); v.layHp[6] = 300; }, "Dark Space");   // textura de pad

        add ("EMOTIVE MELODY", "Blueprint B: rap cantado e intimo", 4.0f, [] (PresetValues& v) {
            tune (v, 28, 52); v.tuneVibrato = 60; eq (v, 75, -2, 0, 1.5f, 0);
            v.compOn = 0; opto (v, 4); v.deessFreq = 6500;
            sat (v, 40, 25, tape);
            verb (v, 1.25f, 25, 16, 4); v.revType = plateType;
            delay (v, slap105, 8, 10, 3);
            layer (v, 1, -10, -35); layer (v, 2, -10, 35); }, "Melodic Intimate");

        add ("RAGE EDGE", "Blueprint C: alta energia distorsionada", 6.5f, [] (PresetValues& v) {
            tune (v, 4, 5); v.formant = -1; eq (v, 90, -2, 3, 1, 1);
            comp (v, 8, 1, 70, 6.5f); sat (v, 25, 60, rageClip);
            verb (v, 0.55f, 10, 10, 3); v.revType = roomType;
            delay (v, slap90, 8, 10, 3);
            layer (v, 3, -12, 50); layer (v, 5, -16, 0); v.octMode = 1; }, "Rage Edge");

        add ("AIRY R&B", "Blueprint: objetivo Airy R&B", 3.5f, [] (PresetValues& v) {
            tune (v, 25, 45); v.tuneVibrato = 70; eq (v, 85, -1.5f, 1.5f, 0.5f, 3); v.eqExcite = 25;
            comp (v, 3, 15, 90, 3.5f); opto (v, 2); v.deessThr = -31;
            verb (v, 2.0f, 40, 22, 4); delay (v, d4D, 22, 10, 5);
            width (v, 35, 8); v.modOn = 1; v.modMode = chorusMode; v.modMix = 8;
            layer (v, 4, -17, 20); v.harmInterval = 0; v.layHp[4] = 250; v.layRev[4] = 100; }, "Airy R&B");   // armonía de 3.ª suave

        add ("CHARACTER HIGH", "Blueprint: Experimental Formant (+4 st)", 4.0f, [] (PresetValues& v) {
            tune (v, 6, 10); v.formant = 4; eq (v, 110, -2, 2, 1, 2);
            comp (v, 4, 8, 60, 4); sat (v, 15, 50, preampA);
            delay (v, d8, 20, 10, 4); verb (v, 1.0f, 20, 10, 3); }, "Experimental Formant");

        add ("CHARACTER LOW", "Blueprint: Experimental Formant (-4 st)", 4.0f, [] (PresetValues& v) {
            v.formant = -4; eq (v, 60, -2, 0, 1.5f, 0, 2);
            comp (v, 4, 10, 70, 4); sat (v, 20, 55, tube);
            layer (v, 5, -14, 0); v.octMode = 0;
            verb (v, 1.4f, 30, 14, 4); }, "Experimental Formant");

        cat = "OBJETIVOS // BLUEPRINT";
        add ("MELODIC TRAP LEAD v1", "Plantilla generica // R", 4.5f, [] (PresetValues& v) {
            tune (v, 6, 15); eq (v, 78, -2, 2.5f, 0, 2);
            comp (v, 4, 8, 55, 4.5f); v.deessFreq = 7300;
            sat (v, 8); width (v, 40, 8);
            delay (v, d8D, 23, 11, 5); verb (v, 1.65f, 38, 14, 4); }, "Dark Space");

        // ===================== ARTISTAS (informe) =====================
        cat = "ARTISTAS // DOCUMENTADOS (D1/D2)";
        add ("TRAVIS SCOTT // Atmospheric", "D1 parcial / R", 5.0f, [] (PresetValues& v) {
            tune (v, 3, 5); eq (v, 75, -2, 2, 0, 2);
            comp (v, 4, 8, 50, 5); v.deessFreq = 7200; v.deessThr = -30;
            sat (v, 12); width (v, 35, 9);
            delay (v, d8D, 27, 16, 6); verb (v, 2.35f, 52, 26, 6, 8500); }, "Dark Space");

        // adlib del YAML travis_atmospheric_reconstruction (Fmt -100 c, banda 180-7k, dist 20 %, reverb 3.2 s)
        add ("TRAVIS SCOTT // Adlibs", "D1 parcial / R", 5.0f, [] (PresetValues& v) {
            tune (v, 3, 5); v.formant = -1.0f; eq (v, 180, -2, 2, 1, -5);   // band-pass aprox. 180 Hz - 7 kHz, Fmt -100 c
            comp (v, 4, 8, 50, 5); v.deessFreq = 7200;
            sat (v, 20, 75); width (v, 45, 10);
            delay (v, d8D, 30, 18, 6); verb (v, 3.2f, 52, 30, 6, 7000); }, "Experimental Formant");

        add ("JUICE WRLD // Melodic", "D2 / R", 4.5f, [] (PresetValues& v) {
            tune (v, 10, 25); eq (v, 75, -2.5f, 1.5f, 0, 1.5f);
            comp (v, 3, 10, 70, 4.5f); opto (v, 3); v.deessFreq = 7200;   // captura CL-1B-like -> opto
            sat (v, 6, 40, preampA); width (v, 28, 7);
            delay (v, d4, 20, 14, 4); verb (v, 1.65f, 32, 20, 4); }, "Melodic Intimate");

        add ("FUTURE // Draco-style", "D1/D2 / R", 5.5f, [] (PresetValues& v) {
            tune (v, 4, 8); eq (v, 70, -2, 1.5f, 0, 1.5f);
            comp (v, 3, 10, 70, 5.5f); v.deessFreq = 6700;
            width (v, 0, 0, 8);   // MetaFlanger documentado en la voz de intro
            verb (v, 1.7f, 20, 18, 3); }, "Dark Space");

        add ("LIL UZI VERT // Tune first", "D1 / R", 4.0f, [] (PresetValues& v) {
            tune (v, 4, 8); eq (v, 80, -2, 0, 2, 2);
            v.compOn = 0; opto (v, 4); v.compPar = 15; v.deessFreq = 7700;   // CLA-3A -> opto
            width (v, 22, 6);
            delay (v, d8D, 21, 13, 4); verb (v, 1.7f, 30, 16, 4); }, "Rage Edge");

        add ("YOUNG THUG", "D1 parcial / R", 4.5f, [] (PresetValues& v) {
            tune (v, 6, 10); eq (v, 80, -2, 0, 2, 2);
            comp (v, 4, 6, 50, 4.5f);
            width (v, 30, 9);
            delay (v, d8D, 25, 15, 5); verb (v, 1.85f, 35, 20, 4); }, "Experimental Formant");

        add ("T-PAIN // Hard tune", "Historico / R", 5.5f, [] (PresetValues& v) {
            tune (v, 1, 0); eq (v, 80, -2, 0, 2, 2);
            comp (v, 4, 8, 60, 5.5f);
            delay (v, slap100, 10, 12, 2); verb (v, 1.2f, 25, 16, 3); }, "Airy R&B");

        add ("KANYE WEST // 808s era", "Historico / R", 4.5f, [] (PresetValues& v) {
            tune (v, 5, 5); eq (v, 70, -2, 0, 1.5f, 1);
            comp (v, 4, 15, 80, 4.5f); sat (v, 10);
            delay (v, d4, 25, 14, 4); verb (v, 1.9f, 30, 22, 4); }, "Dark Space");

        add ("POST MALONE", "Antares / R", 4.0f, [] (PresetValues& v) {
            tune (v, 18, 35); eq (v, 70, -2, 2, 0, 2);
            comp (v, 4, 6, 60, 2.0f, 10); opto (v, 3);   // óptico + 1176 peak
            width (v, 40, 7);
            delay (v, d4, 20, 13, 4); verb (v, 2.2f, 40, 24, 4); }, "Melodic Intimate");

        cat = "ARTISTAS // RECONSTRUCCION (R)";
        add ("LIL WAYNE", "R", 5.5f, [] (PresetValues& v) {
            tune (v, 12, 15); eq (v, 80, -2, 0, 2, 0);
            comp (v, 4, 10, 60, 5.5f); sat (v, 12);
            delay (v, slap100, 8, 12, 2); verb (v, 1.5f, 25, 14, 3); }, "Clean Rap");

        add ("KENDRICK LAMAR", "R", 4.0f, [] (PresetValues& v) {
            eq (v, 70, -2, 2, 0, 0);
            comp (v, 4, 15, 70, 4.0f);
            delay (v, slap80, 5, 9, 2); verb (v, 0.75f, 12, 12, 2); }, "Clean Rap");

        add ("J. COLE", "R", 4.0f, [] (PresetValues& v) {
            eq (v, 70, -2, 0, 1, 0);
            comp (v, 3, 20, 80, 4.0f);
            delay (v, slap100, 5, 6, 2); verb (v, 1.1f, 20, 12, 3); }, "Clean Rap");

        add ("EMINEM", "R", 6.5f, [] (PresetValues& v) {
            eq (v, 80, -2, 0, 2, 0); v.deessFreq = 7000;
            comp (v, 4, 8, 50, 6.5f);
            width (v, 30, 5); verb (v, 0.7f, 10, 9, 2); }, "Clean Rap");

        add ("JAY-Z", "R", 3.0f, [] (PresetValues& v) {
            eq (v, 70, -2, 0, 1.5f, 0);
            comp (v, 3, 20, 80, 3.0f);
            delay (v, slap80, 5, 6, 2); verb (v, 0.75f, 12, 9, 2); }, "Clean Rap");

        add ("50 CENT", "R", 5.0f, [] (PresetValues& v) {
            eq (v, 75, -2, 0, 2, 0);
            comp (v, 4, 10, 70, 5.0f);
            width (v, 20, 6);
            delay (v, slap120, 8, 10, 2); verb (v, 1.25f, 25, 12, 3); }, "Clean Rap");

        add ("NAS", "R", 3.0f, [] (PresetValues& v) {
            eq (v, 70, -2, 0, 1.5f, 0);
            comp (v, 2.5f, 20, 90, 3.0f);
            verb (v, 0.65f, 10, 9, 2); }, "Clean Rap");

        add ("THE NOTORIOUS B.I.G.", "R", 4.0f, [] (PresetValues& v) {
            eq (v, 65, -2, 0, 1, 0);
            comp (v, 3, 20, 100, 4.0f);
            delay (v, slap100, 5, 6, 2); verb (v, 0.8f, 12, 10, 2); }, "Clean Rap");

        add ("2PAC", "R", 5.0f, [] (PresetValues& v) {
            eq (v, 75, -2, 0, 2, 0);
            comp (v, 4, 12, 70, 5.0f);
            width (v, 20, 6); verb (v, 1.15f, 25, 13, 3); }, "Clean Rap");

        add ("SNOOP DOGG", "R", 4.0f, [] (PresetValues& v) {
            eq (v, 70, -2, 0, 1, 0);
            comp (v, 3, 15, 80, 4.0f);
            v.modOn = 1; v.modMode = chorusMode; v.modMix = 8;   // chorus 5-10 %
            delay (v, slap120, 8, 10, 2); verb (v, 1.3f, 25, 13, 3); }, "Clean Rap");

        add ("ANDRE 3000 / OUTKAST", "R", 4.0f, [] (PresetValues& v) {
            eq (v, 75, -2, 0, 1.5f, 0);
            comp (v, 3, 15, 70, 4.0f);
            width (v, 0, 0, 10);
            delay (v, d4, 25, 10, 5); verb (v, 1.6f, 35, 16, 4); }, "Experimental Formant");

        add ("MISSY ELLIOTT", "R", 5.0f, [] (PresetValues& v) {
            eq (v, 80, -2, 0, 2, 0);
            comp (v, 4, 10, 60, 5.0f); sat (v, 15);
            width (v, 0, 0, 15);
            delay (v, d16, 20, 8, 4); verb (v, 1.0f, 15, 10, 3); }, "Experimental Formant");

        add ("NICKI MINAJ", "R", 5.5f, [] (PresetValues& v) {
            tune (v, 15, 20); eq (v, 85, -2, 0, 2, 2);
            comp (v, 4, 10, 60, 5.5f);
            delay (v, d4, 25, 10, 5); verb (v, 1.5f, 30, 15, 4); }, "Airy R&B");

        add ("CARDI B", "R", 6.0f, [] (PresetValues& v) {
            tune (v, 20, 20); eq (v, 85, -2, 0, 2, 0); v.deessFreq = 7200; v.deessThr = -31;
            comp (v, 4, 10, 60, 6.0f); sat (v, 8);
            delay (v, slap100, 8, 8, 2); verb (v, 1.2f, 25, 12, 3); }, "Clean Rap");

        add ("MEGAN THEE STALLION", "R", 5.0f, [] (PresetValues& v) {
            eq (v, 80, -2, 0, 2, 0);
            comp (v, 4, 12, 60, 5.0f);
            width (v, 18, 6);
            delay (v, slap80, 6, 8, 2); verb (v, 0.8f, 15, 10, 2); }, "Clean Rap");

        add ("DOJA CAT", "R", 4.0f, [] (PresetValues& v) {
            tune (v, 12, 20); eq (v, 80, -2, 2, 0, 2);
            comp (v, 3, 12, 80, 4.0f);
            width (v, 30, 8);
            delay (v, d8D, 22, 12, 4); verb (v, 1.8f, 35, 18, 4); }, "Airy R&B");

        add ("A$AP ROCKY", "R", 4.0f, [] (PresetValues& v) {
            tune (v, 12, 15); eq (v, 70, -2, 0, 1, 0);
            comp (v, 3, 15, 80, 4.0f);
            v.modOn = 1; v.modMode = phaserMode; v.modMix = 15; v.modRate = 0.3f;
            delay (v, d4, 30, 14, 5); verb (v, 2.7f, 50, 24, 5); }, "Dark Space");

        add ("PLAYBOI CARTI", "R", 6.5f, [] (PresetValues& v) {
            tune (v, 3, 5); v.formant = 1.5f; eq (v, 100, -3, 0, 2, 2);
            comp (v, 4, 7, 50, 6.5f); sat (v, 18, 75);
            delay (v, d8, 18, 12, 4); verb (v, 1.05f, 20, 12, 3); }, "Rage Edge");

        add ("21 SAVAGE", "R", 4.0f, [] (PresetValues& v) {
            eq (v, 70, -2, 0, 1.5f, 0);
            comp (v, 3, 15, 70, 4.0f);
            verb (v, 0.6f, 10, 7, 2); }, "Clean Rap");

        add ("LIL BABY", "R", 5.5f, [] (PresetValues& v) {
            tune (v, 4, 8); eq (v, 80, -2, 0, 2, 2);
            comp (v, 4, 8, 55, 5.5f);
            width (v, 15, 6);
            delay (v, d8D, 21, 12, 4); verb (v, 1.5f, 30, 15, 4); }, "Melodic Intimate");

        add ("POP SMOKE", "R", 5.0f, [] (PresetValues& v) {
            tune (v, 18, 15); v.formant = -0.5f; eq (v, 65, -2, 0, 2, 0);
            comp (v, 4, 10, 70, 5.0f); sat (v, 8);
            delay (v, slap120, 8, 10, 2); verb (v, 1.3f, 25, 14, 3, 5000); }, "Dark Space");

        add ("XXXTENTACION", "R", 4.5f, [] (PresetValues& v) {
            tune (v, 15, 20); eq (v, 80, -2, 2, 0, 0);
            comp (v, 3, 12, 80, 4.5f); sat (v, 15, 75, rageClip);
            verb (v, 2.0f, 35, 20, 4); }, "Melodic Intimate");

        add ("MAC MILLER", "R", 4.0f, [] (PresetValues& v) {
            eq (v, 70, -2, 0, 1, 0);
            comp (v, 3, 18, 80, 4.0f); sat (v, 7, 40, tape);
            delay (v, slap100, 8, 8, 2); verb (v, 1.4f, 25, 14, 3); }, "Clean Rap");

        add ("KID CUDI", "R", 4.0f, [] (PresetValues& v) {
            tune (v, 12, 15); eq (v, 70, -2, 0, 1, 0);
            comp (v, 3, 15, 80, 4.0f);
            width (v, 30, 8);
            delay (v, d4, 25, 13, 5); verb (v, 2.5f, 45, 24, 5); }, "Dark Space");

        // ---------------- presentación (v3.0) ----------------
        struct Meta { const char* name; const char* artist; const char* title; const char* genre; const char* tags; };
        static const Meta meta[] {
            { "INIT // CLEAN",               "VOX DNA",          "INIT",      "URBAN", "CLEAN NEUTRAL START" },
            { "ASTRO ATMOS",                 "VOX DNA",          "ASTRO",     "TRAP",  "DARK WIDE SPACED" },
            { "EMOTIVE MELODY",              "VOX DNA",          "EMOTIVE",   "TRAP",  "MELODIC INTIMATE WARM" },
            { "RAGE EDGE",                   "VOX DNA",          "RAGE",      "TRAP",  "AGGRESSIVE DISTORTED HARD" },
            { "AIRY R&B",                    "VOX DNA",          "SILK",      "R&B",   "AIRY SMOOTH WIDE" },
            { "CHARACTER HIGH",              "VOX DNA",          "PITCHED",   "URBAN", "BRIGHT CHARACTER FX" },
            { "CHARACTER LOW",               "VOX DNA",          "DEEP",      "URBAN", "DARK HEAVY CHARACTER" },
            { "MELODIC TRAP LEAD v1",        "VOX DNA",          "LEAD",      "TRAP",  "MELODIC WIDE CLEAN" },
            { "TRAVIS SCOTT // Atmospheric", "Travis Scott",     "ATMOS",     "TRAP",  "DARK WIDE SPACED" },
            { "TRAVIS SCOTT // Adlibs",      "Travis Scott",     "ADLIB",     "TRAP",  "DARK DISTORTED SPACED" },
            { "JUICE WRLD // Melodic",       "Juice WRLD",       "EMO",       "TRAP",  "MELODIC AIRY WARM" },
            { "FUTURE // Draco-style",       "Future",           "MUMBLE",    "TRAP",  "DARK MUMBLE FLANGE" },
            { "LIL UZI VERT // Tune first",  "Lil Uzi Vert",     "NEON",      "TRAP",  "HYPER BRIGHT TUNED" },
            { "YOUNG THUG",                  "Young Thug",       "ELASTIC",   "TRAP",  "COLOR UNIQUE WIDE" },
            { "T-PAIN // Hard tune",         "T-Pain",           "ROBOTIC",   "R&B",   "HARD TUNED CLASSIC" },
            { "KANYE WEST // 808s era",      "Kanye West",       "COLD",      "URBAN", "TUNED MINIMAL DARK" },
            { "POST MALONE",                 "Post Malone",      "SMOOTH",    "POP",   "MELODIC WIDE POLISHED" },
            { "LIL WAYNE",                   "Lil Wayne",        "SWAGGER",   "URBAN", "RAW TUNED PUNCHY" },
            { "KENDRICK LAMAR",              "Kendrick Lamar",   "LYRIC",     "URBAN", "DRY CLEAR PUNCHY" },
            { "J. COLE",                     "J. Cole",          "STORY",     "URBAN", "WARM DRY NATURAL" },
            { "EMINEM",                      "Eminem",           "RAPID",     "URBAN", "AGGRESSIVE TIGHT PUNCHY" },
            { "JAY-Z",                       "Jay-Z",            "BOSS",      "URBAN", "SMOOTH DRY CLASSIC" },
            { "50 CENT",                     "50 Cent",          "STREET",    "URBAN", "PUNCHY WIDE CLASSIC" },
            { "NAS",                         "Nas",              "BOOMBAP",   "URBAN", "DRY RAW CLASSIC" },
            { "THE NOTORIOUS B.I.G.",        "The Notorious B.I.G.", "VELVET", "URBAN", "WARM DRY CLASSIC" },
            { "2PAC",                        "2Pac",             "ANTHEM",    "URBAN", "RAW WIDE CLASSIC" },
            { "SNOOP DOGG",                  "Snoop Dogg",       "LAIDBACK",  "URBAN", "SMOOTH CHORUS WEST" },
            { "ANDRE 3000 / OUTKAST",        "Andre 3000",       "COSMIC",    "URBAN", "COLOR FLANGE UNIQUE" },
            { "MISSY ELLIOTT",               "Missy Elliott",    "QUIRK",     "URBAN", "FX PUNCHY UNIQUE" },
            { "NICKI MINAJ",                 "Nicki Minaj",      "PRISM",     "POP",   "BRIGHT TUNED POP" },
            { "CARDI B",                     "Cardi B",          "BOLD",      "URBAN", "LOUD TUNED PUNCHY" },
            { "MEGAN THEE STALLION",         "Megan Thee Stallion", "POWER",  "URBAN", "PUNCHY DRY CONFIDENT" },
            { "DOJA CAT",                    "Doja Cat",         "VELOUR",    "R&B",   "AIRY WIDE POP" },
            { "A$AP ROCKY",                  "A$AP Rocky",       "HAZE",      "TRAP",  "DARK PHASER SPACED" },
            { "PLAYBOI CARTI",               "Playboi Carti",    "VAMP",      "TRAP",  "AGGRESSIVE BRIGHT DISTORTED" },
            { "21 SAVAGE",                   "21 Savage",        "SHADOW",    "TRAP",  "DRY DARK HARD" },
            { "LIL BABY",                    "Lil Baby",         "FLOW",      "TRAP",  "MELODIC TUNED TIGHT" },
            { "POP SMOKE",                   "Pop Smoke",        "DRILL",     "TRAP",  "DARK HEAVY DEEP" },
            { "XXXTENTACION",                "XXXTentacion",     "STORM",     "TRAP",  "DISTORTED MELODIC RAW" },
            { "MAC MILLER",                  "Mac Miller",       "SUNDAY",    "URBAN", "WARM TAPE NATURAL" },
            { "KID CUDI",                    "Kid Cudi",         "DRIFT",     "URBAN", "HUMMING SPACED DARK" } };
        for (auto& pr : p)
        {
            pr.artist = "VOX DNA"; pr.title = pr.name.toUpperCase(); pr.genre = "URBAN";
            for (const auto& m : meta)
                if (pr.name == m.name)
                {
                    pr.artist = m.artist; pr.title = m.title; pr.genre = m.genre;
                    pr.tags.addTokens (m.tags, " ", "");
                }
        }
        return p;
    }();
    return presets;
}
} // namespace pv
