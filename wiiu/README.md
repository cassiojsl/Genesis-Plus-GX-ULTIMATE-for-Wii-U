# Genesis Plus GX ULTIMATE — Wii U

A native Wii U port (not vWii) of Genesis Plus GX ULTIMATE, with the same interface as the Wii version: the menus, settings, file browser, cheats, save manager, menu music, bezels, Auto Crop and Sega 32X (PicoDrive) of the Wii port, in widescreen on the TV and the GamePad.

> **Status:** builds with devkitPro (wut) and runs in the test build on Linux (the same code with a PC backend). It has **not been tested on a Wii U or in Cemu yet**: reports are welcome.

*Português abaixo.*

## Install

1. Aroma (Wii U homebrew environment) is required.
2. Copy `genplus_wiiu.wuhb` to `sd:/wiiu/apps/`.
3. Games go in `sd:/genplusultimate/roms/` (the same folders as the Wii version; the app makes the others on its first start).
4. BIOS and Lock-On files go in `sd:/genplusultimate/bios/` and `sd:/genplusultimate/lock-on/`; the 32X BIOS is optional (PicoDrive has its own replacement).

| Folder (SD card) | Contents |
|---|---|
| `genplusultimate/roms/` | Mega Drive, Mega CD, 32X, Master System, Game Gear and SG-1000 games |
| `genplusultimate/saves/` | Battery saves, save states and their pictures |
| `genplusultimate/cheats/` | Cheat files |
| `genplusultimate/snaps/` | Screenshots (Shot button) |
| `genplusultimate/bezels/` | PNG bezels |
| `genplusultimate/music/` | OGG music for the menu |
| `genplusultimate/config.ini` | Settings |

## Controls

- **Wii U GamePad**: player 1 by default. In the menus: D-Pad / left stick to move, **A** to confirm, **B** to go back, **+ / L / ZL** and **− / R / ZR** to change values, and the **touch screen** to point and tap the buttons.
- **Wii Remote** (pointer, with or without Nunchuk), **Classic Controller** and **Wii U Pro Controller**: as in the Wii version.
- **HOME** opens the emulator menu while playing (as on the Wii). To go back to the Wii U Menu, use **Exit** in the emulator menu.
- Keys can be set per player in Settings > Controls, as on the Wii.

## Differences from the Wii version

- The menu is drawn in widescreen: the Wii menu at its own proportions in the middle, the backgrounds and banners extended to the sides, the title, logo, key helpers and the Play / Shot / Info buttons at the edges.
- Menu font: M+ 1p (free font close to the GameCube/Wii IPL font, which is in their ROM and cannot be read on the Wii U nor shipped).
- Storage: the SD card only (no DVD drive, no GameCube memory cards; USB drives are not read yet).
- No GameCube controllers, USB pads (Xbox 360) or USB mouse.
- Video: the Gamma Correction setting is applied to the game picture in software; the Trap Filter (Wii VI hardware) has no effect.
- The emulation follows the speed of the sound (48 kHz); the picture is not synchronized with the TV (it may tear).
- Sega 32X: PicoDrive runs **without its SH-2 recompiler** (homebrew cannot run generated code on the Wii U without the system's codegen area), so it is slower than on the Wii; the game list shows whether the JIT area is available (for future work).

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
| `wiiu/wiiu_video.c` | Menus (textures, fonts, widescreen layout), game picture, bezels, Auto Crop, sharp scaling, gamma (SDL2) |
| `wiiu/wiiu_audio.c` | `gx_audio.c` of the Wii with the game sound sent to SDL instead of the audio DMA; the menu music is the same |
| `wiiu/wiiu_asnd.c` | ASND (libogc mixer) on SDL2: game sound, menu sounds, OGG player |
| `wiiu/wiiu_ogc.c` | libogc functions: Wii controllers (WPAD) and the GamePad (WiiDRC) from the Wii U, threads, system, storage |
| `wiiu/wiiu_sys.c` | Wii U side (wut): VPAD/KPAD, time, threads, the `sd:` device, exit to the Wii U Menu |
| `wiiu/ogc/` | Replacements of libogc headers |
| `wiiu/images/`, `wiiu/fonts/` | Wii U banner, menu font (M+ 1p, SIL OFL 1.1) |

Changes to the Wii code are limited to `#ifdef HW_WUP` blocks, so the Wii build is unchanged.

---

# Genesis Plus GX ULTIMATE — Wii U (Português)

Port nativo para Wii U (não é vWii) do Genesis Plus GX ULTIMATE, com a mesma interface da versão de Wii: menus, configurações, navegador de arquivos, cheats, gerenciador de saves, música no menu, bezels, Auto Crop e Sega 32X (PicoDrive), em widescreen na TV e no GamePad.

> **Situação:** compila com o devkitPro (wut) e funciona no build de teste em Linux (o mesmo código com uma camada de PC). **Ainda não foi testado num Wii U nem no Cemu.**

## Instalação

1. É preciso o Aroma.
2. Copie `genplus_wiiu.wuhb` para `sd:/wiiu/apps/`.
3. Os jogos ficam em `sd:/genplusultimate/roms/` (as mesmas pastas da versão de Wii; o app cria as outras na primeira vez).
4. BIOS em `sd:/genplusultimate/bios/`; a BIOS do 32X é opcional.

## Controles

- **GamePad**: jogador 1 por padrão. Nos menus: direcional/analógico para mover, **A** confirma, **B** volta, **+ / L / ZL** e **− / R / ZR** mudam valores, e a **tela de toque** aponta e aperta os botões.
- **Wiimote** (com ponteiro), **Classic Controller** e **Pro Controller**: como no Wii.
- **HOME** abre o menu do emulador durante o jogo. Para voltar ao menu do Wii U, use **Exit** no menu do emulador.

## Diferenças em relação ao Wii

- Menu em widescreen, sem esticar.
- Fonte M+ 1p (a fonte do Wii está na ROM do console e não pode ser usada).
- Só cartão SD (sem DVD, memory card, pen drive USB por enquanto).
- Sem controles de GameCube, controles USB ou mouse USB.
- Gama aplicada por software; o Trap Filter não tem efeito.
- 32X sem o recompilador (dynarec) do PicoDrive: mais lento que no Wii.

## Compilar

```bash
docker run --rm -v "$PWD":/src -w /src devkitpro/devkitppc make -f Makefile.wiiu
```
