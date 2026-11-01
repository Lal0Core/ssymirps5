# SSYmirPS5

App nativa de PS5 (homebrew) para jugar **copias de seguridad de Sega Saturn**, con el emulador
[Ymir](https://github.com/ymir-emu/Ymir) dentro y renderizado por **Vulkan** (RADV) con escalado **AMD FSR 1.0**.

- **ID de título:** `PPSA99378`, instalada en `/data/homebrew/PPSA99378/`
- **Versión:** 1.0, la primera versión pública (lo nuevo de cada versión está en `CHANGELOG.md` y en las [releases](https://github.com/Lal0Core/ssymirps5/releases))
- **Licencia:** GPL-3.0-or-later (`LICENSE`); créditos y licencias de terceros en `THIRD_PARTY_NOTICES.md`
- **Carpeta de datos:** `/data/ssymirps5/` (o `ssymirps5/` en un USB)

## Requisitos

- PS5 con jailbreak, **kstuff**, un **cargador de ELF en el puerto 9021** (elfldr, etaHEN o PS5 Payload Manager) y
  **ShadowMountPlus** para que aparezca el icono (lo mismo que pide PS5SX2).
- La **BIOS de Saturn**: `sega_101.bin` (Japón) o `mpr-17933.bin` (EE. UU./Europa), en `/data/ssymirps5/bios/`,
  en `ssymirps5/bios/` de un USB (por ejemplo `/mnt/usb0/ssymirps5/bios/`) o en una carpeta tuya (ver *Carpetas*). Ymir reconoce los volcados conocidos por su hash; si hay varias, usa la
  de la región del disco.
- Opcional: la ROM del CD block (`/data/ssymirps5/bios/cdb/`) para *CD block LLE*.

## Instalación

1. Envía `SSYmirPS5-v1.0.elf` con PS5 Payload Manager, o desde un PC: `nc -q0 IP_DE_TU_PS5 9021 < SSYmirPS5-v1.0.elf`
2. Aparece la notificación de que SSYmirPS5 se instaló. El payload se queda en marcha como *helper* (puerto 9098): es lo
   que deja a la app leer `/data` y USB, y el que descarga las actualizaciones. Si reinicias la consola, la app vuelve a
   enviarlo sola al cargador de ELF (puerto 9021, el de etaHEN o el que uses).
3. Abre **SSYmirPS5** desde la pantalla de inicio.

Si no hay cargador de ELF en el puerto 9021, la app no puede arrancar el helper y lo avisa con una notificación. Las
portadas y los marcos los descarga entonces la propia app; solo las **actualizaciones** necesitan que envíes el `.elf`
a mano (o pulsa ✕ en *Ajustes > Sistema > Actualizaciones* para que la app lo intente de nuevo).

## Carpetas

La app busca **juegos** en `games/`, y la **BIOS** en `bios/` (o `bios/Saturn/`), tanto en `/data/ssymirps5/`
como en `ssymirps5/` de cada USB (`/mnt/usb0` a `/mnt/usb7`, `/mnt/ext0`, `/mnt/ext1`). Por ejemplo:

```
/mnt/usb0/ssymirps5/games/Panzer Dragoon/Panzer Dragoon (Europe).cue
/mnt/usb0/ssymirps5/bios/sega_101.bin
```

**Cualquier otra carpeta:** edita `/data/ssymirps5/paths.txt` (se crea con ejemplos la primera vez que abres la app) y
añade una línea por carpeta:

```
games=/mnt/usb1/Saturn
games=/data/juegos/saturn
bios=/mnt/usb0/BIOS
```

Las subcarpetas cuentan. Si hay BIOS en varios sitios, se usa primero la del mismo USB que el juego. En
*Ajustes > Sistema > BIOS de Saturn* ves en qué carpeta la encontró. Los juegos de un USB salen en la pestaña USB de la
estantería.

Formatos: `.cue` (con sus pistas `.bin` al lado), `.chd`, `.iso`, `.ccd` (+ `.img`), `.mds` (+ `.mdf`) y `.m3u` para
juegos de varios discos. Las pistas que lista un `.cue` o un `.m3u` no salen repetidas. La estantería muestra la carátula
(primero de LaunchBox Games DB y si no de libretro-thumbnails; se descarga sola), y el juego se identifica por el código de producto de la cabecera del disco (una tabla de 2426 discos), aunque el archivo tenga otro nombre.

### Juegos de varios discos

La estantería junta los discos de un mismo juego en una sola entrada ("2 discos") y empieza por el disco 1. Cuando el
juego pida el siguiente, abre el menú (toca el touchpad) y en **Disco en la consola** pulsa derecha. La partida
guardada es la misma para todos los discos. Se reconocen solos:
- por el nombre: `(Disc 1)`, `(Disc 1 of 2)`, `[Disc 1]`, `(CD1)`, `(Disco 1)`, ` - Disc 1`, o `_1`, `-1` al final;
- por la cabecera de cada disco (número de producto y "CD-1/2"), con cualquier nombre (`GAME1.cue`, `Juego A.cue`);
- en la misma carpeta, o cada disco en su subcarpeta dentro de la carpeta del juego.

Un `.m3u` propio (un disco por línea) también vale, con rutas relativas (`Disc 1/juego.cue`, con `/` o `\`) o solo
con el nombre del archivo: la app busca cada disco en las subcarpetas del `.m3u`.

### Caja 3D (R3)

En la estantería, **R3** abre la caja del juego en 3D: portada, reverso y lomo. **R3** (o Izquierda / Derecha) la
gira, el **stick izquierdo** la mueve a mano, **L1 / R1** pasan al juego anterior / siguiente, **X** juega y **O** vuelve. A la derecha salen el código de producto, la
fecha, los discos y las regiones que dice la cabecera del disco, y el archivo del juego.

El reverso, por orden:
1. **Descargado de internet** (solo el de ese juego, al abrir su caja), del juego que dice el **código de producto**
   (una tabla de 2426 discos: MK-81014 = *Virtua Fighter 2*; el panel lo muestra como *Juego*):
   - **LaunchBox Games DB**: la primera vez se descarga su base de datos pública (tarda unos minutos, solo una vez) y se
     guardan los reversos de Saturn en `covers/Saturn/launchbox-backs.tsv` (bórralo para leerla de nuevo);
   - **Chromagi**: por los códigos que Chromagi pone en cada ficha (MK81014-50 está en `FVIR16SSC`), o por el título;
   - las direcciones de `covers/back-sources.txt` (`${code}` = código en minúsculas, `${CODE}` tal cual).

   Se guarda en `/data/ssymirps5/covers/Saturn/back/<código>.<ext>`. También puedes poner ahí tu imagen como
   `<código>.jpg` o `.png` (por ejemplo `mk-81014.jpg`).
2. **Tu imagen:** `<juego> (reverso).png` / `.jpg` o `<juego> (back).png`, junto al juego o en `covers/Saturn/`.
3. **Generado** con los datos del disco.

Un `.chd` también se lee (su primer bloque); si un disco no tiene código, se busca por el nombre del archivo.

### Filtro por letra

En la estantería, **Arriba / Abajo** cambia la letra de la barra *Todos · # · A–Z* (bajo las pestañas Interno / USB)
y muestra solo los juegos cuyo nombre empieza por esa letra ("The" al principio no cuenta). Las letras sin juegos se ven
en gris y se saltan.

### Animación al abrir un juego

Al elegir un juego, la pantalla se funde a negro, aparece el disco del juego quieto 2 s y luego gira 3 s cada vez más
rápido antes de que arranque (✕ u ○ la saltan). El disco es el de LaunchBox Games DB (se guarda en `covers/Saturn/disc/`), tu imagen `<juego> (disc).png`
o `.jpg` junto al juego o en `covers/Saturn/`, o uno dibujado con la portada. Se apaga en *Ajustes > Vídeo >
Animación al abrir un juego*.

### Recientes y continuar donde lo dejaste

La pestaña **Recientes** (a la izquierda de *Interno*, con L1 / R1) muestra los últimos 20 juegos que abriste, el más
reciente primero, y cuándo los jugaste. **■** quita un juego de la lista (no lo borra). La lista está en
`/data/ssymirps5/recent.txt`.

Al salir de un juego desde el menú de pausa se guarda dónde estabas (`states/Saturn/<juego>.auto`, aparte de las 10
ranuras). Al volver a abrirlo, la app pregunta **¿Continuar donde lo dejaste?**: ✕ continúa, △ empieza de nuevo y ○
vuelve atrás sin abrir el juego. Se
apaga en *Ajustes > Emulación > Continuar donde lo dejaste*.

## Capturas de pantalla

En **Ajustes > Vídeo > Capturas en cualquier momento**, activa o desactiva las capturas (activadas por defecto).
Pulsa el **touchpad** y suéltalo en **1 segundo o menos** para abrir el menú.
Para capturar, **mantenlo pulsado más de 1 segundo**: la captura se toma sin esperar a que lo sueltes y aparece
**«Captura tomada.»**. Seguir pulsándolo no repite la captura ni abre el menú al soltarlo.
No hay límite superior de 2 segundos. Después del aviso inicial se confirma si el PNG se ha guardado o si hubo un error.
La captura incluye toda la imagen final y se guarda en `/data/ssymirps5/screenshots/`.
En la biblioteca: `Biblioteca_2026-10-28_21-45-32.png`; con un juego cargado se usa su nombre.
Si coincide la fecha y hora, se añade un sufijo sin sobrescribir capturas anteriores.
El toque corto abre el menú de pausa durante el juego o Ajustes en la biblioteca, también si las capturas están desactivadas.
L3 + R3 ya no abre ni cierra el menú. L3 sigue rebobinando y R3 sigue siendo avance rápido por separado.
Touchpad + cruceta mantiene los atajos de estados y cancela las acciones por duración hasta soltar el touchpad.
El PNG se comprime y escribe en segundo plano; leer la imagen de la GPU puede causar una pausa breve.
No captura el menú de inicio de PS5 ni otras aplicaciones.

## Controles

| En el juego | Acción |
|---|---|
| Cuadrado / Cruz / Círculo | A / B / C |
| L1 / Triángulo / R1 | X / Y / Z |
| L2 / R2 | L / R |
| OPTIONS | Start |
| **Touchpad: pulsar y soltar en 1 segundo o menos** | Menú de pausa |
| Touchpad + Arriba / Abajo | Guardar / cargar estado (ranura actual) |
| Touchpad + Izquierda / Derecha | Cambiar de ranura (1-10) |
| Mantener R3 | Avance rápido |
| Mantener L3 | Rebobinar (si está activado) |

Los 9 botones se pueden reasignar en *Ajustes > Controles*. Tipos de mando para el jugador 1: **mando Saturn**,
**3D Control Pad** (stick analógico), **Arcade Racer** (volante en el stick izquierdo) y **Mission Stick**. Un segundo
mando de PS5 es el jugador 2.

## Ajustes

**Vídeo**
- **Renderizador:** Vulkan (GPU, por defecto) o CPU (se aplica al reiniciar la app).
- **Escalado:** *Off* (por defecto), *AMD FSR 1.0* (EASU + RCAS en la GPU) o *SMAA + AMD FSR 1.0* (antialiasing de bordes antes de FSR, para el 3D). **Nitidez de FSR** de 0 a 100 %.
- **Shader** (cuando el escalado está en Off; por defecto *crt-easymode*, el aspecto de un televisor de tubo para los juegos 2D): los presets slang de RetroArch incluidos (CRT Easymode, CRT Royale,
  Lottes, Guest Advanced, Mega Bezel, ScaleFX...), con sus parámetros ajustables. También carga tus propios `.slangp`.
- Tamaño de la imagen (ajustar, escala entera, estirar), relación de aspecto, marco (bezel de The Bezel Project, 1997
  juegos de Saturn), imagen suavizada, scanlines, **desentrelazado** de los modos de alta resolución (en la GPU, adaptativo al movimiento, sin coste para el emulador), **mallas
  transparentes** (en vez del tramado de la Saturn), contador de FPS y **animación al abrir un juego**.

**Trucos:** códigos Action Replay de Saturn, descargados solos de la base de datos de libretro (192 juegos) y de la
recopilación de Almar's Guides (unas 388 páginas) al arrancar cada juego. También puedes copiar a `cheats/Saturn/` los
`.cht` que descargues (por ejemplo de GameHacking.org, como "libretro (.cht)") tal como vienen: la app encuentra el del
juego por su título o por el número del disco y los suma a los demás. Se activan en el menú de pausa, **Trucos**.

**Audio:** sonido, volumen, interpolación (lineal / vecino más cercano), chip de sonido en su propio hilo.

**Emulación:** velocidad de avance rápido (150 %, 200 % o sin límite), rebobinado, **continuar donde lo dejaste**, **región** (auto / Japón / Norteamérica / Europa),
**cartucho** (auto: el de RAM o ROM si el juego lo necesita según la base de datos de Ymir, y si no el de
**memoria de partidas** de 4 Mbit; o a mano: ninguno, RAM de 1 MB o 4 MB, ROM de KOF '95 o Ultraman, memoria de
partidas), **cartucho de partidas uno por juego o compartido**,
**velocidad del CD** (2x / 4x / 8x), **reloj de los SH-2** (25-200 %), caché SH-2 y CD block LLE.

Si un juego avisa de que no puede preparar su memoria ("Unable to prepare system memory backup... initialize"), usa
*menú de pausa > Inicializar la memoria interna* (o *el cartucho de partidas*): borra las partidas de ese juego en esa
memoria y lo reinicia.

Cada juego puede tener sus propios ajustes (*menú de pausa > Ajustes de este juego*). Idiomas: español (por defecto),
inglés, portugués, francés y alemán. Las partidas guardadas internas (backup RAM) van a `/data/ssymirps5/saves/` y los
estados a `/data/ssymirps5/states/`, cada uno con su miniatura (`.thumb`), que se ve en el menú de pausa al elegir ranura.

**Biblioteca:** descargar carátulas, **ordenar los juegos** (nombre, último jugado, más jugado o año del disco) y
descargar todos los marcos. El tiempo jugado de cada juego se guarda en `/data/ssymirps5/playtime.txt` y sale en la caja
3D (R3).

## Configuración recomendada

No hay una configuración perfecta para todos los juegos, pero estas dos funcionan muy bien. Necesitan el
renderizador **Vulkan**, que viene activado por defecto. Puedes ponerlas para todos los juegos en *Ajustes > Vídeo*,
o solo para uno en *menú de pausa > Ajustes de este juego*.

### Juegos 2D (lucha, plataformas, matamarcianos, RPG con sprites)

La imagen de un televisor de tubo, como se veían en su época:

| Ajuste | Valor |
|---|---|
| Escalado | **No** |
| Shader | **crt-easymode** |
| Desentrelazado (modos de alta resolución) | **No** |
| Mallas transparentes | **No** |

El shader solo se puede elegir con el escalado en *No*. *crt-easymode* añade scanlines y la máscara de un tubo sin
oscurecer demasiado la imagen. Sus parámetros (brillo, intensidad de las líneas…) se ajustan en *Parámetros del
shader*.

### Juegos 3D

Una imagen limpia y nítida en tu televisor:

| Ajuste | Valor |
|---|---|
| Escalado | **SMAA + AMD FSR 1.0** |
| Mallas transparentes | **Sí** |
| Desentrelazado (modos de alta resolución) | **Sí, solo en los juegos de alta resolución** (lista abajo) |

*SMAA* suaviza los bordes de los polígonos antes de que *FSR* agrande la imagen. *Mallas transparentes* convierte en
transparencias de verdad el tramado de puntos que la Saturn usaba para sombras, cristales y humo.

**Desentrelazado:** algunos juegos usan el modo de alta resolución entrelazado de la Saturn (704×480), y sin
desentrelazar la imagen parpadea y se ven líneas. En los demás juegos el ajuste no hace nada, así que lo mejor es
activarlo solo en estos, con *Ajustes de este juego*:

- Virtua Fighter 2
- Virtua Fighter Kids
- Fighters Megamix
- Fighting Vipers
- Dead or Alive
- Last Bronx

Si otro juego parpadea o muestra líneas horizontales, actívalo también en ese juego.

## Registro de cada juego (para diagnosticar)

Cada vez que abres un juego se escribe `/data/ssymirps5/logs/game.log` (el del juego anterior queda en
`game.prev.log`), aunque los registros de depuración estén apagados. Incluye: la versión y los ajustes, el disco (sus
archivos, las pistas de un `.cue` y si existen, la cabecera del disco), dónde se encontró la BIOS, todos los mensajes del
núcleo al cargar, y luego una línea a los 1, 2, 3, 5, 10, 20, 30, 45 y 60 s y cada minuto (fotogramas, si la imagen es
negra, si hay sonido, en qué dirección está cada SH-2), avisos de pantalla negra o CPU atascada, y el informe de un
cierre inesperado. Si un juego no arranca o se queda en negro, copia ese archivo por FTP y envíalo.

## Cómo está hecha

Basada en lo aprendido de los otros proyectos:

- **Interfaz, estantería, carátulas, marcos, menús, helper, instalador y actualizador:** el frontend MIT de
  [genplusgxPS5](https://github.com/MisterTemaki/genplusgxPS5) (MisterTemaki, compartido con Snes9x PS5 y Mesen2 PS5;
  trucos, pestañas y teclado en pantalla de [thatboialex](https://github.com/thatboialex)), adaptado a Saturn (cabecera IP.BIN,
  formatos de disco, `.m3u`, opciones y mandos de Saturn, atajos con el touchpad porque el mando de Saturn usa todos los
  botones frontales y gatillos).
- **Vulkan:** RADV de [PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan) / PS5_Mesa enlazado dentro del eboot; los
  shaders slang se compilan a SPIR-V en la construcción.
- **FSR:** el preset `edge-smoothing/fsr` de [slang-shaders](https://github.com/libretro/slang-shaders) (cabeceras FidelityFX
  FSR 1 de AMD, MIT), sin grano.
- **Núcleo:** [Ymir](https://github.com/ymir-emu/Ymir) (StrikerX3) a través de [Emir](https://github.com/warmenhoven/Emir)
  (su núcleo libretro, de warmenhoven; GPL-3.0), con un parche para PS5 (`ymir-ps5/`: sin excepciones, BIOS en
  `system/Saturn`), compilado como librerías estáticas (`ps5/tools/build-ymir-core.sh`).
- **Título nativo:** eboot firmado y `sce_module/libc.prx` con las herramientas de ps5-native-app-boilerplate
  (BlackBearReloaded), como [PS5SX2](https://github.com/Swordpdf/PS5SX2) (Spyros), y el
  [SDK de payloads](https://github.com/ps5-payload-dev/sdk) de John Törnblom.

## Compilar

Ubuntu 24.04 con clang/lld 18, cmake, ninja, python3 (numpy y Pillow para el arte), un checkout de PS5_Vulkan con RADV
compilado (`tools/build-radv.sh release`) y su SDK:

```sh
export PS5_PAYLOAD_SDK=$HOME/PS5_Vulkan/.deps/native/ps5-payload-sdk
bash ps5/tools/prepare-source.sh ../ssymirps5/source         # Emir + parche PS5
cd ps5
make ps5-core YMIR_SRC=../../ymir-ps5/source                # el núcleo (librerías estáticas)
make ps5 PS5_VULKAN=$HOME/PS5_Vulkan                         # build/ps5/SSYmirPS5.elf
make dist PS5_VULKAN=$HOME/PS5_Vulkan                        # build/dist/ (el .elf con versión, zips)
make host                                                    # la app en Linux (mando simulado)
```

Sin `PS5_VULKAN` sale la versión solo-CPU (sin FSR). `python3 tools/make_ssymirps5_art.py app/sce_sys` regenera el icono y
los fondos.

## Créditos y agradecimientos

SSYmirPS5 existe gracias a:
- **[Ymir](https://github.com/ymir-emu/Ymir)**, de **StrikerX3** y colaboradores: el emulador de Saturn que hace todo el
  trabajo de verdad.
- **[Emir](https://github.com/warmenhoven/Emir)**, de **warmenhoven**: el núcleo libretro de Ymir.
- **MisterTemaki** ([genplusgxPS5](https://github.com/MisterTemaki/genplusgxPS5), Snes9x PS5, Mesen2 PS5): la base de
  la interfaz.
- **[thatboialex](https://github.com/thatboialex)**: trucos, pestañas y teclado en pantalla.
- **mihawk-99** ([PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan), [PS5_Mesa](https://github.com/mihawk-99/PS5_Mesa),
  [PS5_PayloadSDK](https://github.com/mihawk-99/PS5_PayloadSDK)): Vulkan (RADV) en la PS5.
- **John Törnblom** ([ps5-payload-dev SDK](https://github.com/ps5-payload-dev/sdk)), **Spyros**
  ([PS5SX2](https://github.com/Swordpdf/PS5SX2)) y **BlackBearReloaded** (ps5-native-app-boilerplate).
- **libretro** (slang-shaders, libretro-database, libretro-thumbnails), **LaunchBox Games DB**, **The Bezel Project**,
  **GameDB-Saturn** (Niema Moshiri), **Chromagi** y **Almar's Guides**.
- Las bibliotecas y fuentes incluidas: Mesa, glslang, zlib, minizip, LZMA SDK, Mbed TLS, libavif, dav1d, stb, Inter,
  Roboto, PromptFont y Font Awesome.

La lista completa, con la licencia de cada uno, está en `THIRD_PARTY_NOTICES.md`.

## Licencias

La app en conjunto es **GPL-3.0-or-later** (`LICENSE`): Ymir es GPL-3.0, y también lo son el SDK de payloads,
PS5_Vulkan y el runtime del título nativo. El frontend es MIT, RADV/Mesa MIT, FSR 1 MIT (AMD), zlib y minizip licencia
zlib, Mbed TLS Apache-2.0, libavif y dav1d BSD-2-Clause, glslang BSD-3-Clause, la tabla de GameDB-Saturn GPL-3.0, las
fuentes OFL-1.1 o Apache-2.0. Los shaders llevan sus licencias (`ps5/THIRD_PARTY_SHADERS.md`). Los textos completos
están en `THIRD_PARTY_NOTICES.md`, que también va dentro de la app (carpeta `licenses/`). El zip de código fuente de
cada release (`ssymirps5-v<versión>-src.zip`) trae todo lo necesario para compilarla: la app, el núcleo con su parche
(`ymir-ps5/`), PS5_Vulkan y los scripts. No incluye BIOS ni juegos: usa tus propias copias.


## Nombre, carpetas y paquetes
Nombre visible, icono, fondos e instalador: **SSYmirPS5**, con el título `PPSA99378`.
La raíz de datos es `/data/ssymirps5`, con configuración `ssymirps5.ini`. Los juegos y BIOS se buscan
también en `/mnt/usb0/ssymirps5/games` y `/mnt/usb0/ssymirps5/bios`, y en las demás unidades compatibles.
Los paquetes son `SSYmirPS5-v<versión>.elf`, `ppsa99378-SSYmirPS5-v<versión>.zip`,
`SSYmirPS5-v<versión>-PPSA99378.zip` y `ssymirps5-v<versión>-src.zip` (el ZIP fuente usa la carpeta `ssymirps5/`).
El núcleo, sus símbolos ABI y su repositorio de origen conservan los nombres de Ymir.
El actualizador solo acepta paquetes SSYmirPS5 y los busca en las releases de
[Lal0Core/ssymirps5](https://github.com/Lal0Core/ssymirps5).
