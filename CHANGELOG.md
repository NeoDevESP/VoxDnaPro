# VOX DNA PRO — Changelog

## v3.1 — Motion

Animaciones que reaccionan a la voz y al tempo del DAW. Se ajustan en ⚙ → *Animaciones*: **Completas** (60 fps, por defecto), **Suaves** (30 fps y menos partículas) o **Desactivadas**. Vídeo de muestra renderizado del propio plugin: `docs/motion_preview.mp4` y `docs/motion_preview_hero.mp4`.

### Qué se mueve

- **Hero vivo:**
  - Estrellas con parallax y destellos.
  - Planeta que flota, con la luz de borde girando, un anillo con partículas en órbita y una luna que pasa por delante y por detrás.
  - El skyline de onda se convierte en el **espectro real de tu voz**, en espejo con los graves en el centro.
  - **Chispas** que suben al cantar fuerte.
  - El título respira con el nivel y **late con el beat** del DAW; el marco también se ilumina con el beat.
- **Cambio de preset:**
  - El arte anterior se funde con un barrido de luz.
  - El título aparece con un efecto **decode**: las letras se "descifran" una a una.
  - **Todos los knobs barren** hasta su valor nuevo (300 ms, ease-out) con un destello. Al arrastrar con el ratón, el knob sigue al ratón sin retardo.
- **Cadena de efectos:** partículas de señal recorren la línea de módulo en módulo, toman el color de cada uno y son más rápidas y densas con más nivel de entrada. Los módulos activos se iluminan con la voz.
- **Capas:** cada tarjeta brilla con el nivel de su capa.
- **Pestañas:** la vista nueva entra con un fundido y un deslizamiento (220 ms); la pestaña activa late con el beat.
- **Logo:** un brillo lo recorre cada 5 segundos y "PRO" cambia de color con el beat.
- **Radar VIBE:** barrido de radar y "respiración". El **visualizador** tiene picos que caen por gravedad.

### Coste (medido en las pruebas)

Pintado por frame de las regiones que se animan, con renderizado por **CPU, sin GPU**, a escala 100 % (el peor caso):

| Modo | Coste por frame | Frecuencia |
|---|---|---|
| Completas | 3.73 ms | 60 fps |
| Suaves | 3.73 ms | 30 fps |
| Desactivadas | 2.94 ms | 30 fps (solo datos) |

- **Optimizaciones:**
  - El fondo del hero se renderiza una sola vez.
  - El título y los textos se guardan en caché a la escala física exacta de la pantalla, así que se dibujan 1:1 sin remuestrear.
  - Las 120 barras del skyline van en un solo trazado con un único degradado.
  - Los controles sobre el hero se cachean como imagen.
  - Las capas y el radar se repintan a 30 fps y solo en su zona.
- La interfaz corre en el hilo de mensajes, **nunca en el de audio**: las animaciones no afectan al audio ni a la latencia. En Windows, JUCE 8 dibuja con Direct2D (GPU), así que el coste real allí es menor que esta medida.

### Validación
pluginval nivel 10: **SUCCESS** · Validador VST3 de Steinberg: **537/537** · ThreadSanitizer (audio real + editor a 60 fps + las 5 vistas): **0 avisos** · Banco de pruebas: **122 pruebas, 0 fallos**.

---

## v3.0 — Studio Edition

Interfaz nueva siguiendo el mockup "Urban Vocal Laboratory" y un motor de capas y ruteo rehecho. Cada función nueva tiene su prueba automática. Las sesiones y presets de la v2.x se abren y se migran solos.

### Interfaz (base 1536 x 1024, escala 50–150 %)

- **Cabecera:** logo, barra de preset (deshacer / rehacer, lista completa, favorito, exportar, menú ...), A/B, calidad, escala y ajustes.
- **Navegador de presets** lateral: búsqueda, filtros ALL / URBAN / TRAP / R&B / POP, avatar, 2 etiquetas, favoritos primero, crear preset e importar / exportar.
- **CREATE:** hero con arte generativo por preset (nebulosa, planeta, hélice y onda; nunca fotos ni parecidos de personas reales) o tu propia imagen; DNA MODE; referencia y MATCH; radar VIBE (en vivo frente a objetivo); 8 macros asignables (*Customize*) que muestran qué parámetros mueven al tocarlos; onda A/B en vivo (entrada frente a salida); MASTER, OUTPUT, 8 tarjetas de capa con vistas WAVEFORM / SPECTRUM / LEVELS; cadena de efectos con tarjetas visuales; ROUTING, GLOBAL (con la latencia real) y VISUALIZER (espectro u onda, L+R / L / R / MID / SIDE).
- **MIX:** módulos completos, curva de EQ en vivo (incluye HPF y tilt), curva de compresión con el punto de la señal, barras de GR y sección AVANZADO plegable.
- **ROUTING:** diagrama de la señal y 8 canales con fader, medidor, pan, HPF, LPF, drive y envíos.
- **SEQUENCE** y **ANALYZE:** vistas nuevas (abajo).
- Tipografías OFL incrustadas (Rajdhani y VoxDisplay). Orbitron tiene "Reserved Font Name", así que su versión recortada se distribuye con otro nombre, como exige la licencia.
- Diferencias con el mockup: los textos son reales (no hay texto inventado), la latencia que se muestra es la medida (53,8 ms en MIX, 6,9 ms en MONITOR, no 2,3 ms), el chip "IA" pasa a "análisis local" y las fotos se sustituyen por arte generativo o una imagen que cargas tú.

### Capas y ruteo

| Función | Qué hace | Evidencia |
|---|---|---|
| **8 capas v3** | LEAD, DOUBLE L, DOUBLE R, ADLIBS, HARMONY, OCTAVE, TEXTURE, FX | — |
| **HARMONY diatónica** | Sigue la KEY / SCALE de DNA TUNE: 3.ª, 5.ª, 3.ª abajo, 4.ª abajo, 6.ª o 3.ª+5.ª | A3 en La menor: 3.ª arriba = C4 (+2,99 st), 5.ª = E4 (+6,98), 3.ª abajo = F3 (−3,98). En La mayor: C#4 (+3,98). 6.ª en Do mayor: F4 (+7,96) |
| **OCTAVE** | −1, +1 o ambas | 220 Hz → 109,1 Hz / 435,8 Hz |
| **Canal por capa** | HPF y LPF (SVF de 12 dB/oct), drive, nivel, pan y envíos a delay y reverb | HPF de LEAD a 1 kHz: 100 Hz atenuado 40 dB. Envío de reverb de LEAD a 0 %: cola −42 dB → silencio |
| **Compatibilidad** | Con los valores por defecto (sin filtros ni drive, envíos al 100 %) el resultado es idéntico al de la v2 | Todas las pruebas de la v2 siguen pasando |
| **4 modos de ruteo** | SERIES (los ecos entran en la reverb), PARALLEL, MID/SIDE (retornos a los lados) y SPLIT (los efectos solo reciben 300 Hz–8 kHz) | SERIES: cola tras el eco de −127 a −63 dB. MID/SIDE: centro de los retornos −10,5 dB. SPLIT: cola grave −10,5 dB |
| **Orden de insertos** | EQ / COMP / COLOR en cualquiera de las 6 permutaciones; se arrastran las tarjetas en CREATE | Cambiarlo en reproducción: sin NaN y con un fundido de 10 ms. EQ>COMP>COLOR frente a COLOR>COMP>EQ cambia la señal (diferencia −21,7 dB) |

### Secuenciador, global y medición

- **SEQUENCE:** 16 pasos a 1/32, 1/16, 1/8 o 1/4 sincronizados con el DAW. THROW funciona aunque el DELAY esté apagado. Pruebas: GATE a 1/16 cierra cada paso indicado; THROW genera ecos tras la frase (−200 → −25 dB).
- **DNA MODE:** NATURAL suaviza el preset (retune ×2, menos saturación y efectos) y EXTREME lo intensifica; MODERN lo deja tal cual.
- **Macros nuevos:** COMP, DELAY y REVERB. Los 8 huecos de CREATE se pueden asignar a 19 destinos.
- **Tone tilt:** pivota en 1 kHz. Con +6 dB: 100 Hz −2,9 dB y 10 kHz +3,0 dB.
- **Techo ajustable** (OUTPUT / LIMIT): con −3 dBTP y la voz a +18 dB, la salida queda en −3,15 dBTP.
- **Soft clip (S)** antes del limitador.
- **Link:** el MASTER compensa el trim de entrada (+6 dB de entrada → 0,00 dB en la salida).
- **LRA (EBU Tech 3342):** casos 1 a 4 exactos (10, 5, 20 y 15 LU). En MIX se muestran también PLR y el pico true peak máximo.

### ANALYZE (análisis local)

- Referencia desde WAV, AIFF, FLAC, OGG o MP3, analizada en un hilo aparte. Solo se guarda el perfil (`Documentos/VOX DNA PRO/References/*.vref`).
- El perfil incluye espectro en 1/3 de octava, LUFS, LRA, true peak 4x, PLR, anchura, cresta y centroide.
- **MATCH** = 60 % forma espectral (100 Hz–12,5 kHz) + 20 % dinámica (PLR) + 20 % anchura.
- **Asistente de EQ:** propone cambios de ±4 dB como máximo en BODY, MUD, PRESENCE y AIR, con botón APLICAR.
- **VIBE:** brillo, aire, cuerpo, anchura y agresividad, medidos en vivo. Sin referencia se compara con un perfil **estimado** del preset, y así se indica en pantalla.
- Pruebas:
  - Una voz frente a sí misma da 100 %.
  - Una referencia con +6 dB en agudos sugiere subir AIR (+4 dB) sin tocar BODY.
  - Anchura: 0 % en mono y 20 % con L/R desequilibrado.
  - Un seno de −23 dBFS da −22,99 LUFS y −23,00 dBTP.

### Presets y estado

- Los 41 presets tienen nombre en clave, artista ("inspirado en"), género y 3 etiquetas. La descripción se genera de sus parámetros reales.
- ASTRO ATMOS añade una capa TEXTURE y AIRY R&B una armonía de 3.ª suave.
- **Migración:** al abrir una sesión o un `.vdna` de la v2.x, LOW y HIGH pasan a OCTAVE (con el modo correcto), ADLIBS a la capa 4 y ATMOS a TEXTURE. Las dos rutas tienen prueba.
- El estado de sesión guarda los pasos del secuenciador, los macros asignados y la referencia. El recall es exacto en los 152 parámetros.

### Errores encontrados y corregidos durante la validación

- **ThreadSanitizer** detectó una reserva de memoria de tamaño imposible al crear el editor. La causaba un degradado de altura cero en el arte generativo. Corregido, también en el visualizador y en el espectro de ANALYZE.
- **Valgrind** detectó una lectura fuera de rango en una utilidad de las pruebas: medía 128 muestras más de las procesadas. Corregido; las medidas son ahora exactas.

### Validación
pluginval nivel 10: **SUCCESS** · Validador VST3 de Steinberg, modo extendido: **537/537** · ThreadSanitizer (audio real + las 5 vistas + presets + secuenciador + orden de insertos): **0 avisos** · Banco de pruebas: **119 pruebas, 0 fallos** · 0 asignaciones de memoria en `processBlock`.

---

## v2.3 — Analog Edition

COLOR y la dinámica pasan de curvas genéricas a modelos de circuito, todos de código propio. Cada modelo escala la entrada con el drive y normaliza la salida por su ganancia de pequeña señal: con señal débil da 0 dB (el A/B queda nivelado) y la distorsión crece con el nivel, como en el circuito real. Nada asigna memoria en el audio.

### COLOR: modelos de circuito

| Modo | Modelo | Evidencia (1 kHz, tasa interna de RENDER HQ) |
|---|---|---|
| **Tube Drive** | Etapa 12AX7 en cátodo común: ecuaciones de Koren, recta de carga resuelta con Newton-Raphson, conducción de rejilla. La transferencia se resuelve una vez y se guarda en una tabla de 4096 puntos con interpolación cúbica | Armónicos pares dominantes: H2 −21 dBc, H3 −40 dBc. Sin aliasing audible con drive 90 % a 7,9 kHz: peor alias −106 dBc |
| **Tape Colour** | Histéresis magnética de Jiles-Atherton integrada con Runge-Kutta 2, más pérdida de agudos que depende del nivel | Impares dominantes (H3 −24 dBc, H2 despreciable). Lazo de histéresis real: M = ±0,14 Ms con H = 0 según la dirección |
| **Preamp Class A** | Clipper de diodos asimétricos en WDF (fuente resistiva + condensador + par de diodos), resuelto en forma cerrada con la función omega de Wright | H2/H3 = 1,4: pares presentes |
| **Console Bus** | El mismo WDF con diodos simétricos y suaves | THD 0,24 % a −18 dBFS: color sutil |
| **Rage Clip** | Sin cambios (recorte multibanda 300 Hz–6 kHz) | — |

- Ganancia de pequeña señal medida: Tube 0,00 dB, Tape −0,03 dB, Preamp −0,03 dB, Console +0,26 dB.
- La THD crece con el drive en los cuatro modelos (drive 20 / 50 / 80 %): Tube 0,9 / 3,3 / 13 %, Tape 1,3 / 3,5 / 7,4 %, Preamp 0,1 / 0,4 / 6,2 %, Console 0,25 / 0,6 / 1,6 %.

### Compresores

| Modelo | Qué cambia | Evidencia |
|---|---|---|
| **FET** (nuevo, por defecto) | Detección por realimentación (el detector mide la salida, como en la topología del 1176), resuelta de forma implícita en cada muestra: estable con cualquier ratio y ataque. Release dependiente del programa (hasta ×3 tras reducción sostenida). El FET añade 2.º armónico que crece con la reducción | 63 % de la GR en 0,71 ms. Release hasta 1 dB: 23 ms tras una ráfaga de 50 ms, 46 ms tras una de 3 s. H2: inexistente sin reducción, −32 dBc con ~18 dB de GR |
| **VCA** (nuevo selector) | El feed-forward limpio de versiones anteriores | Sin armónicos añadidos. Curva estática: 9,00 dB de GR para −8 dB con umbral −20 y 4:1 |
| **Misma curva estática** | El FET realimentado tiene la misma curva que el VCA, así que las calibraciones de GR objetivo de los presets valen para los dos | FET: 9,00 dB en el mismo caso |
| **OPTO** (reescrito) | Célula T4: panel electroluminiscente (ataque ~10 ms) y fotorresistencia con una parte rápida y otra lenta con memoria; topología de realimentación | 63 % de la GR en 7,8 ms (FET 11 veces más rápido). Release hasta 0,5 dB: 1,7 s tras una ráfaga de 100 ms, 6,1 s tras una de 5 s |

- Nuevo parámetro **Comp Model** (FET / VCA) en la sección COMPRESSOR de MIX.
- Presets: los de ataque lento y ratio suave (≥ 15 ms y ≤ 3:1, R&B y pop suave) usan VCA; el resto, FET.
- Umbral y ratio se suavizan (30 ms): un cambio de preset no produce saltos de ganancia (paso máximo 0,024 frente a 0,131 sin suavizado).

### Rendimiento
Coste de cada modelo por segundo de audio estéreo a 96 kHz: triodo 0,3 % de un núcleo, WDF 1,6 %, cinta 2,2 %. Una instancia en MIX sigue en ~5 % de un núcleo con bloques de 64.

### Validación
pluginval nivel 10: **SUCCESS** · Validador VST3 de Steinberg, modo extendido: **537/537** · ThreadSanitizer: **0 avisos** · Banco de pruebas: **89 pruebas, 0 fallos**.

---

## v2.2 — Tune Edition

Mejoras en DNA TUNE: detección de voz, transiciones entre notas, conservación de consonantes y formantes, y menos latencia en MONITOR. Cada mejora tiene su prueba automática.

| Mejora | Qué hace | Evidencia |
|---|---|---|
| **Clasificación de voz** | Combina la aperiodicidad de YIN con la proporción de energía por encima de 4 kHz. Solo una consonante real (aperiódica y con agudos dominantes) se trata como no sonora | Una ráfaga de ruido entre dos vocales con hard-tune sale con **correlación 0,996** con la entrada (intacta) |
| **Consonantes intactas** | En las consonantes, la salida pasa a la señal seca alineada (mismo retardo D) y el cabezal se recentra. Las s, sh, t y k no pasan por el shifter | Misma prueba |
| **Protección de octava** | Continuidad con la F0 previa, comprobación de las alternativas τ×2 y τ/2, confirmación de saltos de más de 9 semitonos y mediana de 3 estimaciones | Voz con la fundamental 20 dB por debajo del 2.º armónico y vibrato: **0 de 505 bloques** con error de octava |
| **Transition** (nuevo, 0–300 ms) | Deslizamiento entre notas independiente del retune: el hard-tune puede ser legato | Salto A3 → C4: con 0 ms llega a C4 a los 45 ms (0,5 c); con 150 ms está a 181 c a los 60 ms y a 0,5 c a los 350 ms |
| **Flex-Tune** (nuevo) | Corrige solo cuando la voz ya está cerca de la nota de la escala | Flex 100: una desviación de +45 c se respeta y una de +4 c se corrige; Flex 0: +45 c se corrige a 0,0 c |
| **Formant Keep** (nuevo) | Deshace el desplazamiento de formantes que produce la corrección: afina sin cambiar el timbre (MIX/HQ) | Corrección de 1 semitono: el centroide se desplaza **4,8 %** sin Formant Keep y **2,1 %** con él |
| **Pérdidas breves de detección** | En un cambio de nota legato, YIN puede fallar ~20 ms. Antes eso reiniciaba el deslizamiento y pasaba a seco (artefacto en cada cambio de nota); ahora la corrección se mantiene | Prueba de Transition |
| **MONITOR más rápido** | Ventana del shifter de 14 a 11 ms | Latencia de **8,4 → 6,9 ms** (48 kHz). Una voz grave (G2 +30 c) se afina con error de 0,0 c |

### Latencia real con el tune corrigiendo

- **Ataques de sílaba:** salen a **0,03–0,07 ms** de la latencia reportada en los 3 modos, a 44,1, 48 y 96 kHz. Es el tiempo que percibe quien canta.
- **Notas sostenidas:** como en todo pitch shifter por línea de retardo, el cabezal deriva dentro de ±W/2. Retardo medio medido: +3,9 ms en MONITOR (límite físico ±5,5 ms) y −0,8 ms en MIX/HQ.
- Se probó a arrancar el cabezal desplazado para centrar la deriva: bajaba a +3,0 ms, pero empeoraba la transparencia de las consonantes (correlación 0,925). Se descartó.

### Presets
Los presets con afinación natural (retune ≥ 15 ms) activan Formant Keep y Transition. Los de hard-tune (≤ 8 ms) mantienen los saltos secos y el desplazamiento de formantes, que forma parte de ese sonido.

### Rendimiento (misma máquina que la v2.1)
- Sin Formant Keep: igual que la v2.1, ~4,8 % de un núcleo por instancia (MIX, bloques de 64).
- Con Formant Keep: **+3,4 %**. La STFT de formantes procesa ahora los dos canales en una sola FFT compleja (4 FFT por salto en vez de 8).

### Validación
pluginval nivel 10: **SUCCESS** · Validador VST3 de Steinberg, modo extendido: **537/537** · Banco de pruebas: **0 fallos** · ThreadSanitizer: ver `docs/tsan_report.txt`.

---

## v2.1 — Stability Edition

Esta versión corrige los hallazgos de la revisión de código. Cada arreglo tiene una prueba automática que lo demuestra. Todas las pruebas se ejecutaron sobre la v2.0, para tener una referencia, y sobre la v2.1.

### Hallazgos de la revisión

| Prioridad | Hallazgo | Arreglo | Evidencia |
|---|---|---|---|
| Crítica | El analizador compartía un array entre hilos sin sincronizar | FIFO SPSC lock-free (`juce::AbstractFifo`): el audio escribe, la UI consume en su propio buffer. `activeQuality` pasa a ser atómico | ThreadSanitizer con audio en un hilo real, UI, presets y guardado de estado concurrentes. **v2.0: carrera detectada** (`PluginProcessor.cpp:684` ↔ `PluginEditor.cpp:260`). **v2.1: 0 avisos** |
| Crítica | Vectores redimensionados dentro de `processBlock()` | Toda la memoria se reserva en `prepareToPlay`. Si el host envía más muestras de las anunciadas, el bloque se procesa en sub-bloques que referencian el mismo buffer | Contador de `new`/`delete` dentro de `processBlock`, desde el primer bloque, con bloques de 1 a 2048 muestras (anunciado: 512) y automatización: **0 asignaciones** en los 3 modos |
| Alta | MONITOR reportaba 0 muestras de latencia, pero el pitch shifter retrasaba la señal | El shifter es ahora de doble cabezal centrado en un retardo **fijo** D. La señal seca también pasa por D, así que la latencia es idéntica con el tune activado o desactivado. Tras cada pausa, el cabezal se recentra sin saltos | Impulso: medida = reportada (exacto). Voz desafinada con el tune corrigiendo: el retardo medio queda dentro de ±W/4 (tabla en el README) |
| Alta | Cambiar la calidad reiniciaba el DSP en plena reproducción | El cambio va en 4 fases: fundido a 0 (20 ms), conmutación en silencio, espera a que la cadena nueva se llene y fundido de entrada. La latencia nueva se comunica desde el hilo de mensajes (un timer), nunca desde el audio. El parámetro deja de ser automatizable | 7 cambios de modo con el transporte en marcha: sin clicks (paso máximo 0,059), sin NaN, 0 asignaciones, hueco máximo de 55 ms y latencia del host actualizada |
| Media | El limitador garantizaba un techo de muestra, no de true peak | Limitador propio: detección **8x** entre muestras (8 fases × 32 coeficientes, Kaiser), lookahead de 1 ms, min-hold con cola monótona, release y media móvil. Techo de −1 dBTP | Auditado con sobremuestreo 16x: seno a fs/4 con 45° (−1,15), seno de 18 kHz (−1,15), cuadrada (−1,10), ruido de banda completa hasta 19 kHz (−1,11 dBTP) |
| Media | El medidor LUFS era una media exponencial | Medidor ITU-R BS.1770-4: ponderación K exacta para cualquier frecuencia de muestreo; Momentary 400 ms, Short-term 3 s; Integrated con gate absoluto de −70 y relativo de −10 LU | **EBU Tech 3341**, casos 1 a 5 a 44,1, 48 y 96 kHz: error ≤ 0,02 LU (la tolerancia es ±0,1) |
| — | La tabla estática de escalas se inicializaba con `std::vector` en el hilo de audio | Tabla `constexpr` de máscaras de 12 bits, con `static_assert` | Sin inicialización perezosa. clang eliminaba esa asignación por optimización, pero **MSVC no**: en Windows sí ocurría |

### Fallos adicionales encontrados durante la validación

- **Crítico: corrupción de memoria.** Un bloque mayor que el anunciado en `prepareToPlay` hacía que el oversampler escribiera fuera de su buffer (`corrupted size vs. prev_size`). Lo corrige el procesamiento por sub-bloques.
- **pluginval, restauración de estado:** 4 interruptores (`Mod On`, `Delay On`, `Double L On`, `Low On`) no se restauraban. Si un booleano estaba en un valor intermedio, `APVTS::replaceState` no lo sobrescribía. Ahora `syncParametersFromState()` fuerza cada parámetro tras restaurar la sesión o cambiar entre A/B.
- **`triggerAsyncUpdate()` desde el audio:** podía bloquear el hilo al publicar un mensaje del sistema. Se sustituye por un flag atómico más un timer.

### Validación externa

| Herramienta | v2.0 | v2.1 |
|---|---|---|
| pluginval nivel 10 (con pruebas de editor) | 4 fallos | **SUCCESS** (26 categorías) |
| Validador VST3 de Steinberg, modo extendido (`validator -e`) | — | **537 superadas, 0 fallos** |
| ThreadSanitizer (audio e interfaz concurrentes) | 1 carrera | **0** |
| Banco de pruebas propio | 32 pruebas | **60 pruebas, 0 fallos** |

Notas:
- pluginval 1.0.4 no puede lanzar el validador actual de Steinberg (le pasa la ruta del `.so` y el validador espera el bundle). Por eso el validador se ejecutó directamente sobre el bundle.
- Las pruebas en FL Studio, Reaper, Ableton, Logic, Cubase y Bitwig requieren esas aplicaciones y deben hacerse a mano. El README incluye una checklist.

### Rendimiento

Una instancia en modo MIX con bloques de 64 muestras usa ~4,3 % de un núcleo, igual que la v2.0, aunque ahora se suman el limitador true-peak y el medidor BS.1770. El detector 8x usa una ventana contigua que el compilador vectoriza.

### Límites documentados

- **True peak:** la garantía de −1 dBTP vale para contenido de hasta ~20 kHz. Un ruido blanco sintético con energía pegada a Nyquist puede quedar ~0,5 dB por encima (−0,50 dBTP medido), porque ningún interpolador finito la reconstruye. Una voz no tiene contenido ahí.
- **Retardo con el tune corrigiendo:** como en cualquier pitch shifter por línea de retardo, el retardo instantáneo oscila ±W/2 alrededor de la latencia reportada (W ≤ 14 ms en MONITOR, ≤ 60 ms en MIX/HQ). El retardo medio coincide con la latencia reportada dentro de W/4.

---

## v2.0
Arquitectura de 4 motores (DNA TUNE / ANALOG / SPACE / STUDIO), 8 capas, 8 macros, vistas CREATE, MIX y ROUTING, y 41 presets.
