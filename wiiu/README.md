# Genesis Plus GX ULTIMATE — Wii U

A **native Wii U port** of Genesis Plus GX ULTIMATE. It is a Wii U app, built with the Wii U SDK (wut) and launched from Aroma, not the Wii version running in vWii mode. It has the same interface as the Wii version, adapted to the Wii U: the menus, settings, file browser, cheats, save manager, menu music, bezels, Auto Crop and Sega 32X (PicoDrive) of the Wii port, in widescreen on the TV and the GamePad.

It uses the Wii U hardware:

- **The three CPU cores**: the game is emulated on one core while the picture of the frame before is drawn on another.
- **The GPU** (GX2) at 720p, 16:9, with VSync.
- **The GamePad**: buttons, sticks, touch screen to point and tap the menus, and the Wii U HOME Menu.
- **The JIT (codegen) area of the system**: the 32X runs with PicoDrive's SH-2 recompiler.
- **SD card and FAT32 USB drives**, Wii Remote (with pointer, Nunchuk, Classic Controller) and the Wii U Pro Controller.

*Português abaixo.*

## Install

1. Aroma (Wii U homebrew environment) is required.
2. Copy `genplus_wiiu.wuhb` to `sd:/wiiu/apps/`.
3. The data of the app is in `sd:/wiiu/apps/genplusultimate/` (made on the first start).
4. Games go in `sd:/wiiu/apps/genplusultimate/roms/`, or anywhere on a FAT32 USB drive (Settings > Menu > ROM Load Device: USB; the file browser starts at the root of the drive).
5. BIOS and Lock-On files go in `bios/` and `lock-on/` of the data folder; the 32X BIOS is optional (PicoDrive has its own replacement).

| Folder (SD card) | Contents |
|---|---|
| `wiiu/apps/genplusultimate/roms/` | Mega Drive, Mega CD, 32X, Master System, Game Gear and SG-1000 games |
| `wiiu/apps/genplusultimate/saves/` | Battery saves, save states and their pictures |
| `wiiu/apps/genplusultimate/cheats/` | Cheat files |
| `wiiu/apps/genplusultimate/snaps/` | Screenshots (Shot button) |
| `wiiu/apps/genplusultimate/bezels/` | PNG bezels |
| `wiiu/apps/genplusultimate/music/` | OGG music for the menu |
| `wiiu/apps/genplusultimate/config.ini` | Settings |

The USB drive is only for games: saves, settings and the rest stay on the SD card.

## Controls

- **Wii U GamePad**: player 1 by default. In the menus: D-Pad / left stick to move, **A** to confirm, **B** to go back, **+ / L / ZL** and **− / R / ZR** to change values, and the **touch screen** to point and tap the buttons.
- **Wii Remote** (sideways, with pointer, with or without Nunchuk), **Classic Controller** and **Wii U Pro Controller** (as a Classic Controller): as in the Wii version, chosen per player in Settings > Controls.
- **HOME** while playing opens the emulator menu (as on the Wii). **HOME** again in the emulator menu opens the **Wii U HOME Menu**.
- Keys can be set per player in Settings > Controls, as on the Wii.

## Video

- **VSync** (Settings > Video): the picture waits for the TV, without tearing (60 Hz games; 50 Hz games follow the sound).
- **Aspect**: ORIGINAL (4:3) keeps the game at 4:3 with borders on the 16:9 screen; **PIXEL PERFECT** (in place of the Wii's ORIGINAL 16:9) draws every pixel of the game as the same whole number of screen pixels, without smoothing; SCALED as on the Wii.
- **Auto Crop** is on (FIT) by default.
- Bezels, Auto Crop, NTSC filters and the sharp scaling of the Wii version; the Gamma Correction is applied in software. The Trap Filter (Wii video hardware) has no effect.

## Sega 32X

The 32X is emulated by PicoDrive. Its **Dynamic Recompiler** (System > 32X Settings) puts the code it makes in the JIT (codegen) area that Aroma gives to the app, so the 32X runs much faster than with the interpreter. It is **experimental** on the Wii U and off by default; it shows N/A, with the reason, when the system gives no usable JIT area.

## Differences from the Wii version

- The menu is drawn in widescreen: the Wii menu at its own proportions in the middle, the backgrounds and banners extended to the sides, the title, logo, key helpers and the Play / Shot / Info buttons at the edges.
- Menu font: M+ 1p (free font close to the GameCube/Wii IPL font, which is in their ROM and cannot be read on the Wii U nor shipped).
- Storage: SD card and FAT32 USB drives (no DVD drive, no GameCube memory cards).
- No GameCube controllers, USB pads (Xbox 360) or USB mouse.
- The data folder is `sd:/wiiu/apps/genplusultimate/` (next to the app) instead of `sd:/genplusultimate/`.

## Base

Genesis Plus GX ULTIMATE with the fixes of the official Genesis Plus GX up to November 11, 2026 (ZIP files that could not be opened, checks of the save states).

## Build

With Docker:

```bash
docker run --rm -v "$PWD":/src -w /src devkitpro/devkitppc make -f Makefile.wiiu
```

Or with devkitPro installed (`wiiu-dev`, `wii-dev` for the libogc headers, `wiiu-sdl2`, `wiiu-sdl2_ttf`, `wiiu-sdl2_image`):

```bash
make -f Makefile.wiiu
```

The output is `genplus_wiiu.wuhb` (and `.rpx`).

## How the port works

The Wii code (`gx/`) is built as it is, with the libogc headers. What talks to the Wii hardware is replaced by `wiiu/`:

| File | Role |
|---|---|
| `wiiu/wiiu_video.c` | Menus (textures, fonts, widescreen layout), game picture, VSync, aspect, bezels, Auto Crop, sharp scaling, gamma (SDL2 on GX2) |
| `wiiu/wiiu_audio.c` | `gx_audio.c` of the Wii with the game sound sent to SDL instead of the audio DMA; the menu music is the same |
| `wiiu/wiiu_asnd.c` | ASND (libogc mixer) on SDL2: game sound, menu sounds, OGG player |
| `wiiu/wiiu_ogc.c` | libogc functions: Wii controllers (WPAD) and the GamePad (WiiDRC) from the Wii U, threads, system, storage |
| `wiiu/wiiu_sys.c` | Wii U side (wut): VPAD/KPAD, HOME Menu, time, threads, emulation on another core, JIT area for the 32X, the `sd:` and `usb:` devices, exit to the Wii U Menu |
| `wiiu/ogc/` | Replacements of libogc headers |
| `wiiu/libs/` | USB drives: [libfat](https://github.com/wiiu-env/libfat) and [libmocha](https://github.com/wiiu-env/libmocha) (LGPL 3) of wiiu-env |
| `wiiu/images/`, `wiiu/fonts/`, `wiiu/meta/` | Wii U banner and B icon, menu font (M+ 1p, SIL OFL 1.1), app icon and splash screens |

Changes to the Wii code are limited to `#ifdef HW_WUP` blocks, so the Wii build is unchanged.

---

# Genesis Plus GX ULTIMATE — Wii U (Português)

Um **port nativo para Wii U** do Genesis Plus GX ULTIMATE. É um app de Wii U, feito com o SDK do Wii U (wut) e aberto pelo Aroma, não a versão de Wii rodando no modo vWii. Tem a mesma interface da versão de Wii, adaptada ao Wii U: menus, configurações, navegador de arquivos, cheats, gerenciador de saves, música no menu, bezels, Auto Crop e Sega 32X (PicoDrive), em widescreen na TV e no GamePad.

Usa o hardware do Wii U:

- **Os três núcleos do processador**: o jogo é emulado num núcleo enquanto a imagem do quadro anterior é desenhada em outro.
- **A GPU** (GX2) em 720p, 16:9, com VSync.
- **O GamePad**: botões, analógicos, tela de toque para apontar e tocar nos menus, e o HOME Menu do Wii U.
- **A área de JIT (codegen) do sistema**: o 32X roda com o recompilador de SH-2 do PicoDrive.
- **Cartão SD e pen drives/HDs USB em FAT32**, Wiimote (com ponteiro, Nunchuk, Classic Controller) e o Wii U Pro Controller.

## Instalação

1. É preciso o Aroma.
2. Copie `genplus_wiiu.wuhb` para `sd:/wiiu/apps/`.
3. Os dados do app ficam em `sd:/wiiu/apps/genplusultimate/` (criada na primeira vez).
4. Os jogos ficam em `sd:/wiiu/apps/genplusultimate/roms/`, ou em qualquer lugar de um pen drive/HD USB em FAT32 (Settings > Menu > ROM Load Device: USB; o navegador começa na raiz do drive).
5. BIOS em `bios/` e arquivos de Lock-On em `lock-on/`, dentro da pasta de dados; a BIOS do 32X é opcional.

O USB é só para os jogos: saves, configurações e o resto ficam no SD.

## Controles

- **GamePad**: jogador 1 por padrão. Nos menus: direcional/analógico para mover, **A** confirma, **B** volta, **+ / L / ZL** e **− / R / ZR** mudam valores, e a **tela de toque** aponta e aperta os botões.
- **Wiimote** (deitado, com ponteiro, com ou sem Nunchuk), **Classic Controller** e **Pro Controller** (como Classic Controller): como no Wii, escolhidos por jogador em Settings > Controls.
- **HOME** durante o jogo abre o menu do emulador. **HOME** de novo no menu do emulador abre o **HOME Menu do Wii U**.

## Vídeo

- **VSync**: a imagem espera a TV, sem rasgos (jogos de 60 Hz).
- **Aspecto**: ORIGINAL (4:3) mantém o jogo em 4:3 com bordas; **PIXEL PERFECT** (no lugar do ORIGINAL 16:9 do Wii) desenha cada pixel do jogo com o mesmo número inteiro de pixels da tela, sem suavização.
- **Auto Crop** em FIT por padrão.
- Bezels, Auto Crop, filtros NTSC e escala nítida da versão de Wii; gama aplicada por software; o Trap Filter não tem efeito.

## Sega 32X

O **Dynamic Recompiler** do PicoDrive (System > 32X Settings) usa a área de JIT que o Aroma dá ao app, e o 32X roda muito mais rápido. É **experimental** no Wii U e vem desligado; aparece N/A, com o motivo, quando o sistema não dá uma área de JIT utilizável.

## Diferenças em relação ao Wii

- Menu em widescreen, sem esticar.
- Fonte M+ 1p (a fonte do Wii está na ROM do console e não pode ser usada).
- Cartão SD e USB em FAT32 (sem DVD, sem memory card).
- Sem controles de GameCube, controles USB ou mouse USB.
- Pasta de dados em `sd:/wiiu/apps/genplusultimate/`.

## Base

Genesis Plus GX ULTIMATE com as correções do Genesis Plus GX oficial até 11 de novembro de 2026.

## Compilar

```bash
docker run --rm -v "$PWD":/src -w /src devkitpro/devkitppc make -f Makefile.wiiu
```
