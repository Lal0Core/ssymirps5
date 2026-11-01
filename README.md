# SSYmirPS5

Emulador de **Sega Saturn** para **PS5 con jailbreak**, como app nativa con su propio icono en la pantalla de inicio.
Usa el núcleo de [Ymir](https://github.com/ymir-emu/Ymir) y dibuja con **Vulkan** (RADV) con **AMD FSR 1.0** y shaders
CRT.

- **Descarga:** en [Releases](https://github.com/Lal0Core/ssymirps5/releases), el archivo
  `ppsa99378-SSYmirPS5-v<versión>.zip` (o el payload `SSYmirPS5-v<versión>.elf`).
- **Guía de uso** (instalación, carpetas, controles, ajustes y configuración recomendada):
  [`ssymirps5/README.md`](ssymirps5/README.md).
- **Cambios de cada versión:** [`ssymirps5/CHANGELOG.md`](ssymirps5/CHANGELOG.md).

SSYmirPS5 **no incluye BIOS ni juegos**: usa tus propias copias.

## Qué hay en este repositorio

| Carpeta | Qué es |
|---|---|
| `ssymirps5/` | La app: interfaz, capa de PS5, instalador, helper, shaders y pruebas |
| `ymir-ps5/` | El núcleo de Ymir (vía Emir, su núcleo libretro) con el parche para PS5 |
| `PS5_Vulkan/` | El port de Vulkan (RADV) para PS5 de mihawk-99, con el que se compila la app |
| `PS5_RetroArch/tooling/native/` | El runtime de C++ que usa el núcleo |
| `deps/` | `PS5_Vulkan/.deps/native` ya compilado (RADV, SDK de payloads, zlib), para no tener que compilarlo |

## Compilar

En Ubuntu 24.04 (también en WSL2 en Windows):

```sh
bash setup.sh    # una sola vez: paquetes del sistema y las dependencias de deps/
bash build.sh    # el núcleo (la primera vez) y la app
```

Los paquetes quedan en `ssymirps5/ps5/build/dist/`. Los detalles están en la sección *Compilar* de
[`ssymirps5/README.md`](ssymirps5/README.md).

## Licencia y créditos

SSYmirPS5 es **GPL-3.0-or-later** ([`LICENSE`](LICENSE)). Es posible gracias a Ymir (StrikerX3), Emir (warmenhoven),
MisterTemaki (genplusgxPS5), thatboialex, mihawk-99 (PS5_Vulkan, PS5_Mesa, PS5_PayloadSDK), John Törnblom
(ps5-payload-dev SDK), Spyros (PS5SX2), BlackBearReloaded y muchos más: la lista completa, con la licencia de cada uno,
está en [`ssymirps5/THIRD_PARTY_NOTICES.md`](ssymirps5/THIRD_PARTY_NOTICES.md).

Sega Saturn es una marca de SEGA. PlayStation y PS5 son marcas de Sony Interactive Entertainment. SSYmirPS5 no está
relacionado con ninguna de las dos.
