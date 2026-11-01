# Ymir para PS5 (core de libretro para PS5_RetroArch)

Port del emulador de Sega Saturn [Ymir](https://github.com/ymir-emu/Ymir) a PS5,
como core de libretro para [PS5_RetroArch](https://github.com/mihawk-99/PS5_RetroArch)
(RetroArch 1.22.2, carpeta del título `PPSA99169`). Parte del core de libretro
[Emir](https://github.com/warmenhoven/Emir) (rama `libretro`, revisión `8174cbb`).

**Estado: experimental.** Compila, pasa el verificador ABI de PS5_RetroArch y
funciona en Linux; el comportamiento en la consola está pendiente de probar.

## Instalación en la PS5

1. Copia por FTP a la carpeta del título (`/data/homebrew/PPSA99169/`, o donde
   tengas instalado PS5_RetroArch):
   - `cores/ymir_libretro.so` → `cores/`
   - `info/ymir_libretro.info` → `info/` (y una copia en `cores/`, como hace el
     propio título)
2. BIOS de Saturn volcada de tu consola, en `system/` o en `system/Saturn/` (donde
   ya la tendrás si usas Beetle Saturn). Nombres aceptados: `sega_101.bin`
   (Japón), `mpr-17933.bin` (EE. UU./Europa) o `saturn_bios.bin`.
3. Opcional: una ROM del bloque de CD en `system/cdb/` activa la emulación de bajo
   nivel del lector.
4. En RetroArch: *Cargar contenido* → tu juego (`.cue`, `.chd`, `.ccd`, `.mds`,
   `.iso` o `.m3u`) → *Sega - Saturn (Ymir)*.

Si el core no carga, el registro de RetroArch dirá `unresolved native runtime
import: …`. Esa lista es exactamente lo que hay que añadir al core. Puede pasar
con otra versión del título: compruébalo con
`tools/check-imports.py dist/cores/ymir_libretro.so --title eboot.bin`.

## Qué cambia respecto a Ymir/Emir

El cargador de cores de PS5_RetroArch no es un `dlopen` normal: resuelve cada
función que pide un core contra una tabla compilada dentro del título, que
contiene solo lo que pedían los cores con los que se compiló. Si falta un
nombre, la consola rechaza el core. Para que Ymir cargue en un título ya
instalado, sin recompilarlo:

- **libc++ y libc++abi van dentro del core**, ocultos: no importa ningún
  símbolo de C++, solo 92 funciones de C. Las 92 están comprobadas contra el
  `eboot.bin` de la v4.0.0-beta-4 (RetroArch 1.22.2, identidad de compilación
  `c480fe1c…`).
- **Capa de locale "C" propia** (`tooling/ymir/ps5_core_locale.c`): las
  funciones `*_l`, las tablas `_DefaultRuneLocale` y los catálogos de mensajes
  que pide libc++, implementados sobre funciones que el título sí tiene
  (`vsnprintf`, `strtod`, `mbrtowc`, `wcrtomb`…). Probada contra glibc.
- **Sin desenrollador ni excepciones de C++**: una excepción interna de libc++
  termina el proceso, como haría una no capturada. Los lectores de
  `.cue`/`.ccd` ya no usan `std::stoi`/`stoll`, así que una imagen corrupta se
  rechaza con un mensaje en vez de cerrar RetroArch.
- **`_umtx_op`** (esperas atómicas de libc++) y **`__udivti3`** (división de
  128 bits) implementados en el core (`tooling/ymir/ps5_core_shims.c`).
- **Sin `iconv`** (la libc de la consola no lo tiene): conversión UTF-8 ↔ UTF-16
  propia (`wchar_t` ocupa 2 bytes en el target de PS5).
- **Renderizador por software**, AVX2 activado (Zen 2), sin el despachador x86 de
  xxHash, sin nombres de hilo, y búsqueda de la BIOS también en `system/Saturn/`.

Todos los cambios al código de Emir están en `tooling/ymir/native.patch`.

## Pruebas

En `test/` hay un mini-frontend de libretro (`harness.c`) y pruebas de las
piezas propias del port, ejecutables en Linux:

- `harness`: carga el core, ejecuta frames y guarda/carga estado.
- `utf_test`: conversión UTF-8 ↔ UTF-16 en modo PS5 (`-fshort-wchar -DPS5`).
- `locale_test`: capa de locale frente a glibc (clasificación de las 256
  entradas y conversiones multibyte con todos los tamaños de búfer).
- `umtx_test`: esperas atómicas entre 8 hilos (160.000 relevos).
- `udiv_test`: división de 128 bits frente a la del compilador (2 millones de casos).

## Compilar desde el código

Requisitos (Ubuntu 24.04): `clang-18`, `lld-18`, `cmake`, `ninja`, `python3`, `git`.

```sh
git clone https://github.com/mihawk-99/PS5_RetroArch
git clone https://github.com/mihawk-99/PS5_PayloadSDK    # junto a PS5_RetroArch
make -C PS5_RetroArch deps                               # instala el SDK en .deps/
PS5_RETROARCH_DIR=$PWD/PS5_RetroArch bash ymir-ps5/tools/build-ymir.sh
```

El script descarga Emir en la revisión fijada, aplica el parche, compila y deja
el resultado en `dist/`. Con `PS5_TITLE_EBOOT=/ruta/eboot.bin` comprueba además
que el título instalado conoce todas las funciones que importa el core.

`tools/check-imports.py` lista las importaciones del core
(`--list`) y las comprueba contra un `eboot.bin` (`--title`).

## Integrarlo en PS5_RetroArch

Para incluirlo en una compilación completa del título:

1. Copia `tools/build-ymir.sh`, `tools/prepare-source.sh`, `tools/check-imports.py`
   y `tooling/ymir/` al repositorio.
2. Haz que `build-ymir.sh` deje el core en `build/cores/stage/cores/` y el `.info`
   en `build/cores/stage/info/`, como los demás `build-*.sh`.
3. Añade `ymir` a la lista de cores de `tools/build-title.sh`.

## Licencia

Ymir y Emir son GPL-3.0 (`dist/info/ymir-LICENSE.txt`); este port también.
