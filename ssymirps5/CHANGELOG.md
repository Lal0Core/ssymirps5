# SSYmirPS5: cambios

## 1.0 (primera versión pública)
Primera versión publicada de SSYmirPS5, emulador de Sega Saturn para PS5 con jailbreak, con el núcleo de
[Ymir](https://github.com/ymir-emu/Ymir).

**Juegos**
- Discos de Saturn en `.cue` (con sus pistas `.bin`), `.chd`, `.iso`, `.ccd`, `.mds` y `.m3u` (varios discos). Los
  juegos de varios discos se agrupan solos.
- Juegos y BIOS en `/data/ssymirps5/` o en `ssymirps5/` de un USB, o en las carpetas que indiques en `paths.txt`.
- Cada disco se identifica por el número de producto de su cabecera (unos 2400 discos), aunque el archivo tenga otro
  nombre.

**Estantería**
- Cajas 3D (R3) con portada, reverso, disco y tiempo de juego. Carátulas de LaunchBox Games DB y
  libretro-thumbnails, que se descargan solas.
- Pestañas Recientes, Interna y USB; filtro por letra, búsqueda por nombre y orden por nombre, último jugado, más
  jugado o año.
- **Continuar donde lo dejaste**: al volver a un juego, sigue desde el punto en que lo dejaste.
- Animación al abrir un juego (se puede desactivar).

**Imagen y sonido**
- Vulkan (RADV) con **AMD FSR 1.0**, SMAA + FSR, o shaders slang de RetroArch (CRT, Lottes, Guest, Mega Bezel...),
  con sus parámetros. También carga tus propios `.slangp`.
- Marcos de The Bezel Project (1997 juegos de Saturn), desentrelazado, mallas transparentes y scanlines.
- Capturas de pantalla: mantén pulsado el touchpad más de 1 segundo.
- Ajustes de vídeo por defecto, los mejores para juegos 2D: escalado apagado, shader *crt-easymode*, sin
  desentrelazado ni mallas transparentes. El README explica la configuración recomendada para los juegos 2D y 3D.

**Emulación**
- Estados guardados con miniatura, rebobinado, avance rápido (150 %, 200 % o sin límite), región, cartuchos (RAM, ROM
  o memoria de partidas, uno por juego o compartido), velocidad del CD, reloj de los SH-2 (25-200 %) y CD block LLE.
- Trucos Action Replay de libretro y Almar's Guides, que se descargan solos, y tus propios `.cht`.
- Ajustes para cada juego.

**Sistema**
- Interfaz en español, inglés, portugués, francés y alemán.
- Actualizaciones desde las releases de [Lal0Core/ssymirps5](https://github.com/Lal0Core/ssymirps5) (*Ajustes >
  Sistema > Actualizaciones*).
- Registro de cada juego en `/data/ssymirps5/logs/game.log`, para diagnosticar problemas.
- Licencia GPL-3.0-or-later; los créditos y las licencias de terceros están en `THIRD_PARTY_NOTICES.md`.
