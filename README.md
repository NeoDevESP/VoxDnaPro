# VOX DNA PRO — Urban Vocal Laboratory v3.1 (Motion)

Plugin vocal modular (VST3 / AU / Standalone) para trap, rap melódico, hip-hop y R&B, construido siguiendo el **VOX DNA PRO Blueprint Técnico** y la investigación de procesamiento vocal urbano.

> **v3.1:** animaciones audio-reactivas y sincronizadas al beat: hero vivo (estrellas, planeta, espectro de tu voz, chispas), transición de preset con título *decode* y knobs que barren a su nuevo valor, partículas de señal en la cadena, pestañas con transición. ⚙ → Animaciones: Completas / Suaves / Desactivadas. Vídeo: `docs/motion_preview.mp4`.
>
> **v3.0:** interfaz nueva "Urban Vocal Laboratory" con 5 vistas (CREATE, MIX, ROUTING, SEQUENCE, ANALYZE); 8 capas con filtros, saturación y envíos propios; armonizador diatónico; 4 modos de ruteo de efectos; orden de insertos arrastrable; secuenciador de 16 pasos; análisis local de referencia con MATCH, VIBE y asistente de EQ; LRA (EBU 3342). Las sesiones y presets de la v2.x se migran solos.
>
> **v2.3:** COLOR y los compresores pasan a modelos de circuito: triodo 12AX7, histéresis de cinta, diodos en WDF, compresor FET realimentado (con modo VCA limpio) y opto con célula T4. Con señal débil todos dan 0 dB, así que el A/B está nivelado.
>
> **v2.2:** DNA TUNE distingue voz de consonantes (s, sh, t y k pasan intactas), evita errores de octava, añade Transition (deslizamiento entre notas), Flex-Tune y Formant Keep (afinar sin cambiar el timbre), y baja MONITOR a 6,9 ms.
>
> **v2.1:** cero asignaciones de memoria en el hilo de audio, sin carreras de datos (verificado con ThreadSanitizer), latencia real medida y reportada en todos los modos, cambio de calidad seguro durante la reproducción, limitador true-peak de −1 dBTP y medidor de sonoridad conforme a BS.1770-4. Pasa pluginval al nivel 10 y el validador VST3 de Steinberg en modo extendido. Detalle en **CHANGELOG.md**.

## Instalación en FL Studio (Windows)

1. Copia **`VOX DNA PRO.vst3`** a `C:\Program Files\Common Files\VST3\`
2. FL Studio → *Options → Manage plugins → Find more plugins*.
3. Inserta **VOX DNA PRO** en el Mixer.
4. Pon la **KEY y SCALE de tu canción** (vista MIX → DNA TUNE). Ningún preset las cambia.
5. Para grabar con monitorización: modo **MONITOR** (6,9 ms). Para mezclar o exportar: **MIX** o **RENDER HQ**. FL compensa la latencia automáticamente.

## Los cuatro motores (blueprint cap. 01)

| Motor | Qué incluye |
|---|---|
| **DNA TUNE** | Detección F0 YIN + clasificación de voz/consonante + protección de octava · escalas · Retune 0–100 ms · Humanize · **Transition** 0–300 ms · **Flex-Tune** · **Vibrato Protect** · *confidence gate* · **Formant Shift ±12 st** y **Formant Keep** (STFT + envolvente cepstral) · retardo fijo y alineado |
| **DNA ANALOG** | **FET Fast Comp** + rama paralela NY → **Opto Leveler** (RMS, release dependiente del historial) · **Color**: Tube Drive, Tape Colour, Preamp Class A, Console Bus, Rage Clip (multibanda 300 Hz–6 kHz) con oversampling FIR · De-esser split-band · EQ con **Harsh Dyn** a 3.2 kHz y **Exciter** |
| **DNA SPACE** | Delay ping-pong/slap (80–140 ms) con **ducking** · Reverb FDN **Plate / Hall / Room** con difusión, RT60 real y predelay · **Motion**: Chorus / Flanger / Phaser |
| **DNA STUDIO** | **8 capas** · **8 macros** · **A/B** · **Undo/Redo** · presets **JSON versionados** · sonoridad **BS.1770-4** (M/S/I) · **true peak** · **correlación** · **MONO** check · **Match level** · Dry/Wet con compensación de latencia |

Grafo: `INPUT → CLEAN → TUNE → TONE → FET → OPTO → COLOR → LAYERS → MOTION → DELAY/REVERB (envíos) → OUTPUT → limitador true-peak (−1 dBTP)`

Medidores (vista CREATE): **M / S / I** en LUFS de salida, diferencia de sonoridad respecto a la entrada (para A/B a volumen igualado), **true peak máximo** en dBTP y reducción del limitador. **Clic sobre el panel** para resetear el integrado y el pico máximo.

## Cinco vistas (v3.0)

- **CREATE** — arte generativo del preset (o tu propia imagen), DNA MODE (Natural / Modern / Extreme), referencia y MATCH, radar VIBE, 8 macros asignables (*Customize*; al tocar uno se muestra qué mueve), MASTER con soft clip (S), hold (H) y link entrada/salida, las 8 capas, la cadena de efectos (arrastra EQ / COMP / SAT para cambiar el orden), ruteo, global con la latencia real y visualizador.
- **MIX** — todos los módulos, curva de EQ en vivo, curva de compresión con el punto de la señal y GR, medidores profesionales (M / S / I, LRA, true peak, PLR, correlación) y sección AVANZADO plegable.
- **ROUTING** — diagrama de flujo de la señal y 8 canales: nivel, panorama, HPF, LPF, drive y envíos a delay y reverb por capa; intervalo de armonía, modo de octava y retornos.
- **SEQUENCE** — 16 pasos sincronizados al DAW con 4 carriles: THROW (lanza la voz al delay), FILTER, GATE y PAN.
- **ANALYZE** — carga una voz de referencia (se analiza en tu equipo; solo se guarda su perfil `.vref`), espectro a largo plazo comparado, métricas (LUFS, LRA, true peak, PLR, anchura, cresta, centroide), MATCH desglosado y asistente de EQ con botón APLICAR.

**Capas:** 1 LEAD · 2 DOUBLE L · 3 DOUBLE R · 4 ADLIBS · 5 HARMONY (diatónica, sigue la KEY/SCALE de DNA TUNE) · 6 OCTAVE (−1 / +1 / ambas) · 7 TEXTURE · 8 FX.

> **Limitación declarada (blueprint cap. 02):** sobre una voz mono, las capas 2–8 son **sintéticas**: se generan desde el lead. Para dobles y adlibs reales, graba otra pista y usa otra instancia.

> **Honestidad de los datos:** las imágenes son arte generativo (ninguna foto ni parecido de artistas reales; puedes cargar la tuya por preset). MATCH y VIBE son análisis local: contra tu referencia o, sin referencia, contra un perfil **estimado** a partir de los parámetros del preset. No hay IA en la nube ni se sube audio.

**UX:** doble clic en un knob = escribir valor · Ctrl/Alt+clic = valor por defecto · tooltips en cada control · escala 50–150 % (se recuerda).

## Modos de calidad y latencia real (medida por los tests)

| Modo | Uso | 44,1 kHz | 48 kHz | 96 kHz | Composición |
|---|---|---|---|---|---|
| MONITOR | Grabar | 305 smp · **6,9 ms** | 331 smp · **6,9 ms** | 643 smp · **6,7 ms** | tune (ventana ≤ 11 ms) + limitador 1 ms |
| MIX | Mezclar | 2459 smp · 55,8 ms | 2580 smp · 53,8 ms | 4068 smp · 42,4 ms | tune (≤ 60 ms) + formant + OS 2x FIR + limitador |
| RENDER HQ | Exportar | 2471 smp · 56,0 ms | 2592 smp · 54,0 ms | 4080 smp · 42,5 ms | igual que MIX con OS 4x |

- La latencia es **la misma con el tune activado o desactivado**: la señal seca también pasa por el retardo central. Activar el tune no cambia la PDC.
- **Retardo audible con el tune corrigiendo** (medido con voz desafinada): los **ataques** de cada sílaba salen a menos de 0,1 ms de la latencia reportada. En notas sostenidas el cabezal deriva dentro de ±W/2 (W ≤ 11 ms en MONITOR, ≤ 60 ms en MIX/HQ); retardo medio medido: +3,9 ms en MONITOR y −0,8 ms en MIX. Tras cada consonante o pausa vuelve exactamente al centro.
- **Cambiar de modo** durante la reproducción produce un hueco de silencio de ≤ 55 ms, sin clicks. El parámetro no es automatizable porque cambia la PDC.

## Presets (41)

**Objetivos (blueprint):** INIT · ASTRO ATMOS · EMOTIVE MELODY · RAGE EDGE · AIRY R&B · CHARACTER HIGH · CHARACTER LOW · MELODIC TRAP LEAD v1
**Artistas documentados (D1/D2):** Travis Scott (Atmospheric + Adlibs) · Juice WRLD · Future · Lil Uzi Vert · Young Thug · T-Pain · Kanye West 808s · Post Malone
**Reconstrucciones (R):** Lil Wayne, Kendrick Lamar, J. Cole, Eminem, Jay-Z, 50 Cent, Nas, The Notorious B.I.G., 2Pac, Snoop Dogg, André 3000, Missy Elliott, Nicki Minaj, Cardi B, Megan Thee Stallion, Doja Cat, A$AP Rocky, Playboi Carti, 21 Savage, Lil Baby, Pop Smoke, XXXTentacion, Mac Miller, Kid Cudi

Filtro por etiqueta: **Dark Space · Melodic Intimate · Rage Edge · Clean Rap · Airy R&B · Experimental Formant**.

Todos son **prototipos de ingeniería / puntos de partida, no cadenas oficiales** ni aprobadas por los artistas (D1 = documentado, D2 = secundario, R = reconstrucción). Si vas a **distribuir o vender** el plugin, el blueprint (cap. 14) recomienda no usar nombres de artistas sin autorización: renombra esos presets por su objetivo musical.

### Presets de usuario
**SAVE** → `Documentos/VOX DNA PRO/Presets/*.vdna`, en JSON versionado (`schema: 3`; los de schema 2 se migran al cargar, metadatos, modo, ruteo y cada parámetro con su unidad). Un parámetro desconocido no rompe la carga y los que falten vuelven a su valor por defecto. Key/Scale nunca se guardan en presets.

## Validación (v3.1)

| Herramienta | Resultado v3.1 |
|---|---|
| **pluginval**, nivel 10, con pruebas de editor | SUCCESS (26 categorías) |
| **Validador VST3 de Steinberg**, modo extendido | 537 superadas, 0 fallos |
| **ThreadSanitizer** (audio en un hilo real + las 5 vistas + presets + guardado + secuenciador + orden de insertos) | 0 avisos |
| **Banco de pruebas propio** | 122 pruebas, 0 fallos (`docs/test_report.txt`) |

Banco de pruebas: `cmake -B build -DVD_BUILD_TESTS=ON && cmake --build build --target VoxDnaTests && ./build/VoxDnaTests_artefacts/Release/VoxDnaTests`

- **Tiempo real:** 0 asignaciones en `processBlock` en los 3 modos, contando desde el primer bloque, con bloques de 1 a 2048 muestras (anunciado: 512) y automatización de parámetros.
- Estabilidad de los 41 presets.
- **Tune:** error ≤ 1,1 cents. El hard-tune aplana el vibrato (45 → 10 c) y Vibrato Protect lo conserva (46 c).
- **Formant ±3 st:** F0 intacta; centroide espectral +22 % / −16 %.
- **Latencia:** el impulso medido coincide con la latencia reportada en los 3 modos, también con Dry/Wet al 50 %. Retardo audible medido con el tune corrigiendo a 44,1, 48 y 96 kHz.
- 44,1 / 48 / 88,2 / 96 kHz con bloques aleatorios de 1 a 2048 muestras.
- **Recall:** sesión y preset JSON exactos (152 parámetros), también con interruptores en valores intermedios (el caso que detecta pluginval).
- Mono → mono; correlación estéreo compatible con mono.
- Sin clicks al cambiar presets, al activar o desactivar módulos, ni en **7 cambios de modo de calidad con el transporte en marcha**.
- **True peak** auditado con sobremuestreo 16x: ≤ −1,10 dBTP con seno a fs/4 y 45°, seno de 18 kHz, onda cuadrada y ruido de banda completa a +6/+10 dBFS.
- **Sonoridad:** EBU Tech 3341, casos 1 a 5 a 44,1, 48 y 96 kHz, con error ≤ 0,02 LU.
- **Aliasing** del Rage Clip: −30 dB con OS 2x, −38 dB con 4x.
- **Modelos analógicos (v2.3):** 0 dB de pequeña señal en los 4 modelos de COLOR; firma armónica de cada uno; THD creciente con el drive; lazo de histéresis; sin aliasing audible del triodo en HQ; dinámica de FET, VCA y opto (ver CHANGELOG).
- **v3.0:** armonizador diatónico exacto (±0,04 st), octavas, filtros y envíos por capa, 4 modos de ruteo, secuenciador sincronizado, tone tilt, techo ajustable, link, orden de insertos, migración de sesiones v2 y EBU Tech 3342 (LRA) casos 1–4 exactos (ver CHANGELOG).
- **CPU:** ~5–7 % de un núcleo por instancia en MIX con bloques de 64 (el preset de la prueba usa ahora también la capa TEXTURE).

### Checklist manual en FL Studio (no automatizable desde aquí)
1. Escanear el plugin y abrirlo en el Mixer. Probar el buffer del dispositivo de audio a 64, 256 y 1024 muestras y cambiarlo con el proyecto sonando.
2. Grabar en **MONITOR** con el tune activado: la latencia de escucha es de ~8 ms más la de tu interfaz.
3. Cambiar a **MIX** con el transporte en marcha: debe oírse un hueco corto, sin click, y la voz debe seguir alineada con el beat (la PDC de FL compensa).
4. Automatizar Retune, Formant, Dry/Wet y los macros desde el Playlist. Renderizar la exportación en **RENDER HQ**.
5. Guardar el proyecto, cerrar FL, reabrir y comprobar que todo se restaura (incluidas las capas y A/B).
6. Exportar y medir con un medidor externo: true peak ≤ −1,0 dBTP.

## Compilar

Requisitos: CMake ≥ 3.22 + Visual Studio 2022 (Windows) o Xcode (macOS). JUCE 8 se descarga solo.

- **Windows:** doble clic en `build_windows.bat`
- **Sin instalar nada:** sube la carpeta a un repositorio de GitHub. `.github/workflows/build.yml` compila en Windows, macOS y Linux, ejecuta el banco de pruebas (en Windows con MSVC, que es donde importa el test de asignaciones) y pasa **pluginval al nivel 10** (más auval en macOS). Descarga desde *Actions → Artifacts*.

## Hoja de ruta

Completada hasta la v3.0 (v2.1 Stability, v2.2 Tune, v2.3 Analog, v3.0 Studio). Ideas abiertas: capas desde pistas reales vía sidechain, más tipos de reverb y presets de patrón para SEQUENCE.

## Licencias
JUCE 8 (AGPLv3 o licencia comercial). Para vender el plugin cerrado necesitas la licencia comercial de JUCE adecuada a tus ingresos. Todo el DSP de este proyecto es propio. Fuentes incrustadas: Rajdhani y VoxDisplay (versión modificada de Orbitron), ambas SIL Open Font License 1.1; ver `Resources/Fonts/`.
