#pragma once
#include <juce_core/juce_core.h>
#include <map>

// Tooltips explicativos (blueprint cap. 09). Doble clic = escribir valor, Ctrl/Alt+clic = reset.
namespace pv
{
inline juce::String tooltipFor (const juce::String& id)
{
    static const std::map<juce::String, juce::String> t {
        { "quality", "MONITOR: latencia minima para grabar (sin formant ni oversampling). MIX: formant + OS 2x. RENDER HQ: OS 4x para exportar." },
        { "dryWet", "Mezcla global seca/procesada, con la senal seca compensada en latencia." },
        { "monoCheck", "Suma la salida a mono para comprobar cancelaciones de fase." },
        { "mTune", "Macro TUNE: mas a la derecha = retune mas rapido y menos humanize (efecto mas duro)." },
        { "mBody", "Macro BODY: cuerpo en graves medios + nivelador opto. Cuidado con 150-350 Hz." },
        { "mAir", "Macro AIR: shelf de aire + exciter; el de-esser compensa automaticamente." },
        { "mPunch", "Macro PUNCH: ataque del FET mas lento (consonantes) + compresion paralela." },
        { "mColor", "Macro SATURATION: cantidad de saturacion del modulo COLOR (aunque este apagado)." },
        { "mComp", "Macro COMP: mas compresion (umbral del FET/VCA hasta -8 dB y mas opto)." },
        { "mDelay", "Macro DELAY: nivel y feedback del delay." },
        { "mReverb", "Macro REVERB: nivel y longitud de la reverb." },
        { "dnaMode", "DNA MODE: NATURAL suaviza el preset (retune mas lento, menos saturacion y efectos), MODERN lo deja tal cual, EXTREME lo lleva al limite." },
        { "routeMode", "Ruteo de efectos. SERIES: los ecos del delay entran en la reverb. PARALLEL: delay y reverb independientes. MID/SIDE: los retornos se abren a los lados y el centro queda para la voz. SPLIT: graves y sibilancia no entran en los efectos (300 Hz - 8 kHz)." },
        { "toneTilt", "Tone tilt: inclina todo el espectro alrededor de 1 kHz (positivo = mas brillo, negativo = mas calidez)." },
        { "ceiling", "Techo del limitador true-peak. -1 dBTP es lo recomendado para streaming." },
        { "softClip", "S = Soft clip: redondea los picos antes del limitador (mas densidad, menos trabajo del limitador)." },
        { "ioLink", "Link: el MASTER compensa el trim de entrada (subes la entrada para empujar la cadena sin cambiar el volumen final)." },
        { "chainOrder", "Orden de los insertos EQ / COMP / COLOR. Tambien se puede cambiar arrastrando las tarjetas en CREATE." },
        { "harmInterval", "Intervalo del armonizador. Es DIATONICO: sigue la KEY y la SCALE de DNA TUNE (en Chromatic usa intervalos mayores)." },
        { "octMode", "Capa OCTAVE: una octava abajo, arriba o las dos." },
        { "seqOn", "Secuenciador de 16 pasos sincronizado al tempo del DAW." },
        { "seqRate", "Duracion de cada paso." },
        { "seqThrow", "THROW: en los pasos activos la voz se lanza al delay (+12 dB). Funciona aunque el DELAY este apagado." },
        { "seqFilter", "FILTER: profundidad del filtro paso-bajo ritmico." },
        { "seqGate", "GATE: profundidad de la puerta ritmica de volumen." },
        { "seqPan", "PAN: profundidad del panorama por pasos." },
        { "mSpace", "Macro SPACE: escala delay + reverb y su ducking." },
        { "mWidth", "Macro WIDTH: nivel de las capas DOUBLE L/R + modulacion. El lead sigue en el centro." },
        { "mEnergy", "Macro ENERGY: presencia + paralelo + clip suave antes del limitador." },
        { "inGain", "Ajusta para picos de voz cerca de -10 dBFS (los presets estan calibrados asi)." },
        { "hpFreq", "Filtro paso-alto 24 dB/oct. Voz grave 60-80 Hz, voz ligera 80-110 Hz." },
        { "gateThr", "Puerta de ruido con hold y rango de -40 dB. OFF a la izquierda del todo." },
        { "tuneOn", "Activa DNA TUNE (deteccion YIN + correccion)." },
        { "tuneKey", "Tonalidad de TU CANCION. Ningun preset la cambia." },
        { "tuneScale", "Escala de la cancion. Chromatic = cualquier semitono." },
        { "tuneRetune", "Tiempo en llegar a la nota. 0-15 ms: efecto duro. 20-50 ms: natural." },
        { "tuneHumanize", "En notas largas relaja la correccion para conservar desviaciones naturales." },
        { "tuneVibrato", "Vibrato Protect: corrige hacia el centro de la nota y conserva la oscilacion expresiva." },
        { "tuneConfidence", "Confidence Gate: cuanto mas alto, solo se corrigen tramos con F0 muy estable (no consonantes ni respiraciones)." },
        { "tuneAmount", "Porcentaje de la correccion aplicada." },
        { "tuneTransition", "Transition: tiempo de deslizamiento entre notas. 0 = salto seco (efecto T-Pain); 40-120 ms = legato natural aunque el retune sea rapido." },
        { "tuneFlex", "Flex-Tune: 0 = corrige siempre. Valores altos solo corrigen cuando la voz ya esta cerca de la nota y respetan las desviaciones expresivas." },
        { "tuneFormantPreserve", "Formant Keep: deshace el cambio de timbre que produce la correccion (voz natural). Apagado = el desplazamiento de formantes forma parte del efecto. Requiere MIX/HQ." },
        { "formant", "Formant Shift: cambia el caracter (tamano del tracto vocal) sin mover la nota. +-1 a +-3 st moderado. Requiere modo MIX/HQ." },
        { "deessOn", "De-esser de banda partida: solo atenua la banda alta." },
        { "deessThr", "Busca 2-6 dB de reduccion en las 's' (medidor DS)." },
        { "deessFreq", "Frecuencia de la sibilancia; normalmente 5-10 kHz segun cantante." },
        { "eqBody", "Shelf grave a 150 Hz." },
        { "eqMud", "Campana a 260 Hz (Q 1.2) para limpiar barro del microfono/sala." },
        { "eqHarsh", "EQ dinamica a 3.2 kHz: solo recorta cuando la voz pica. Valor = reduccion maxima." },
        { "eqPres", "Presencia a 3.5 kHz para inteligibilidad." },
        { "eqAir", "Shelf de aire a 11.5 kHz." },
        { "eqExcite", "Exciter: armonicos solo por encima de 3 kHz, en paralelo." },
        { "compOn", "COMPRESSOR: control de picos. Modelo FET (realimentado, inspirado en la topologia del 1176) o VCA limpio." },
        { "compModel", "FET: detecta la salida (realimentacion), release dependiente del programa y 2.o armonico que crece con la reduccion. VCA: feed-forward limpio y transparente. Misma curva estatica: los presets valen para ambos." },
        { "compThr", "Ajusta hasta que la barra FET llegue a la marca blanca (GR objetivo del preset)." },
        { "compRatio", "4:1 tipico para voz; 8:1 rage." },
        { "compAtt", "Ataque: 0.2-3 ms rapido; 8-25 ms deja pasar consonantes." },
        { "compRel", "Release 40-150 ms en voz." },
        { "compMakeup", "Ganancia de compensacion." },
        { "compPar", "Compresion paralela (NY): rama 20:1 aplastada mezclada por debajo." },
        { "optoOn", "OPTICAL LEVELER: modelo de celula T4 (panel EL + fotorresistencia). Ataque ~10 ms y release con memoria: tras una compresion larga suelta mucho mas despacio. Va despues del compresor." },
        { "optoAmount", "Peak reduction: busca 2-4 dB de GR (barra OPTO)." },
        { "optoGain", "Make-up del opto." },
        { "satOn", "COLOR: saturacion analogica en paralelo con oversampling." },
        { "satMode", "Tube: triodo 12AX7 (pares). Tape: histeresis magnetica (impares + perdida HF). Preamp A: diodos asimetricos WDF. Console: diodos suaves, color sutil. Rage Clip: recorte solo 300 Hz-6 kHz. Con senal debil todos dan 0 dB (A/B nivelado)." },
        { "satDrive", "Cantidad de no linealidad de la rama de color." },
        { "satMix", "Mezcla paralela de la rama de color (5-35 % habitual)." },
        { "modOn", "MOTION: chorus / flanger / phaser estereo." },
        { "modMode", "Chorus para dobles, Flanger para intros (estilo MetaFlanger), Phaser psicodelico." },
        { "modRate", "0.15-0.7 Hz recomendado." },
        { "modDepth", "Profundidad de la modulacion." },
        { "modMix", "5-20 % para sutileza." },
        { "dlyOn", "Delay en envio paralelo, sincronizado al BPM del DAW." },
        { "dlyTime", "1/8D y 1/4 para trap; Slap 80-140 ms para rap seco." },
        { "dlyFb", "Feedback 18-35 % tipico; 25-45 % para throws." },
        { "dlyDuck", "Baja el delay mientras cantas (3-8 dB): los ecos aparecen en los huecos." },
        { "dlyMix", "Nivel del envio de delay. Automatizalo para throws en finales de frase." },
        { "revOn", "Reverb FDN en envio paralelo con HPF 220 Hz." },
        { "revType", "Plate (1.1-2.2 s, lead), Hall (2.3-4.2 s, atmos), Room (0.3-0.9 s, pegamento)." },
        { "revDecay", "RT60 real en segundos." },
        { "revPredelay", "Separa la voz de la cola: 20-70 ms." },
        { "revTone", "LPF del envio: 7-12 kHz." },
        { "revDuck", "Ducking de la reverb desde la voz seca." },
        { "revMix", "Nivel del envio de reverb." },
        { "layDetune", "Detune de las capas DOUBLE L/R (+-4-9 cents recomendado)." },
        { "outGain", "MASTER: ganancia de salida antes del limitador true-peak." } };

    if (auto it = t.find (id); it != t.end()) return it->second;
    if (id.startsWith ("layOn"))  return "Activa la capa sintetica (generada a partir del lead). Para dobles/adlibs reales usa otra pista con otra instancia.";
    if (id.startsWith ("layLvl")) return "Nivel de la capa.";
    if (id.startsWith ("layPan")) return "Panorama de la capa (L50 - R50).";
    if (id.startsWith ("layHp"))  return "Filtro paso-alto de la capa (12 dB/oct).";
    if (id.startsWith ("layLp"))  return "Filtro paso-bajo de la capa (12 dB/oct).";
    if (id.startsWith ("layDrv")) return "Saturacion propia de la capa.";
    if (id.startsWith ("layDly")) return "Cuanto de esta capa entra en el DELAY (relativo al envio global).";
    if (id.startsWith ("layRev")) return "Cuanto de esta capa entra en la REVERB (relativo al envio global).";
    return {};
}
} // namespace pv
