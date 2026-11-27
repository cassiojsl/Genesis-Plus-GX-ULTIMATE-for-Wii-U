# Genesis Plus GX ULTIMATE — Wii U

**Genesis Plus GX ULTIMATE** (VrepliroidV) is based on [Genesis Plus GX 1.7.5](https://github.com/ekeeke/Genesis-Plus-GX) by Eke-Eke. It also runs Sega 32X games, emulated by [PicoDrive](https://github.com/irixxxx/picodrive) (notaz, irixxxx and others), with PicoDrive's settings in System > 32X Settings. Paprium runs with its cartridge chip emulated by the code of [Genesis Plus GX PUNCHiUM](https://github.com/pav1388/Genesis-Plus-GX-PUNCHiUM) (pav1388), its music read from MP3 or OGG files in a `paprium` folder next to the ROM. It also has PNG bezels around the game and an Auto Crop of the borders in the video settings, an automatic overclock of the main and Sega CD 68k, of the Z80 of the 8-bit games and of the 32X CPUs, per-game settings, and menu music from the `music` folder with a mini player.

This is a **native Wii U port** (not vWii) of Genesis Plus GX ULTIMATE: a Wii U app, built with the Wii U SDK (wut) and launched from Aroma, with the same interface as the Wii version: the menus, settings, file browser, cheats, save manager, menu music, bezels, Auto Crop and Sega 32X (PicoDrive) of the Wii port, in widescreen on the TV and the GamePad. It uses the Wii U hardware: its three CPU cores (the game is emulated on one core while the frame before is drawn on another), the GPU at 720p, the GamePad (touch screen, Wii U HOME Menu), and the JIT area of the system for PicoDrive's SH-2 recompiler.

## Install

1. Aroma (Wii U homebrew environment) is required.
2. Copy `genplus_wiiu.wuhb` to `sd:/wiiu/apps/`.
3. Games go in `sd:/wiiu/apps/genplusultimate/roms/` (the app makes the other folders on its first start), or anywhere on a FAT32 USB drive (Settings > Menu > ROM Load Device: USB).
4. BIOS and Lock-On files go in `sd:/wiiu/apps/genplusultimate/bios/` and `sd:/wiiu/apps/genplusultimate/lock-on/`; the 32X BIOS is optional (PicoDrive has its own replacement).

| Folder (SD card) | Contents |
|---|---|
| `wiiu/apps/genplusultimate/roms/` | Mega Drive, Mega CD, 32X, Master System, Game Gear and SG-1000 games |
| `wiiu/apps/genplusultimate/saves/` | Battery saves, save states and their pictures |
| `wiiu/apps/genplusultimate/cheats/` | Cheat files |
| `wiiu/apps/genplusultimate/snaps/` | Screenshots (Shot button) |
| `wiiu/apps/genplusultimate/bezels/` | PNG bezels |
| `wiiu/apps/genplusultimate/music/` | OGG music for the menu |
| `wiiu/apps/genplusultimate/config.ini` | Settings |

A USB drive is only for games: the file browser starts at its root, and saves and settings stay on the SD card.

## Controls

- **Wii U GamePad**: player 1 by default; when player 1 uses another controller and no player uses the GamePad, the GamePad also plays as player 1. In the menus: D-Pad / left stick to move, **A** to confirm, **B** to go back, **+ / L / ZL** and **− / R / ZR** to change values, and the **touch screen** to point and tap the buttons.
- **Wii Remote** (pointer, with or without Nunchuk), **Classic Controller** and **Wii U Pro Controller**: as in the Wii version.
- **HOME** opens the emulator menu while playing (as on the Wii). **HOME** again in the emulator menu opens the Wii U HOME Menu.
- Keys can be set per player in Settings > Controls, as on the Wii.

## Differences from the Wii version

- The menu is drawn in widescreen: the Wii menu at its own proportions in the middle, the backgrounds and banners extended to the sides, the title, logo, key helpers and the Play / Shot / Info buttons at the edges.
- Menu font: M+ 1p (free font close to the GameCube/Wii IPL font, which is in their ROM and cannot be read on the Wii U nor shipped).
- Storage: the SD card and FAT32 USB drives (no DVD drive, no GameCube memory cards). The data folder is `sd:/wiiu/apps/genplusultimate/`, next to the app.
- GameCube controllers (through the Wii U GameCube adapter), USB pads (Xbox 360) and the USB mouse are not supported yet.
- Video: the picture is 16:9: ORIGINAL (4:3) keeps the game at 4:3, and PIXEL PERFECT (in place of ORIGINAL (16:9)) draws each pixel of the game as the same whole number of screen pixels. Auto Crop is on (FIT) by default. VSync shows the picture without tearing. The Gamma Correction setting is applied to the game picture in software; the Trap Filter (Wii VI hardware) has no effect.
- Sega 32X: PicoDrive's SH-2 recompiler (Dynamic Recompiler in System > 32X Settings) uses the JIT (codegen) area that Aroma gives to the app. It is on by default, and used only if the JIT area works.
- It has the fixes of the official Genesis Plus GX up to November 11, 2026.

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
| `wiiu/wiiu_sys.c` | Wii U side (wut): VPAD/KPAD, HOME Menu, time, threads, emulation on another core, JIT area for the 32X, the `sd:` and `usb:` devices, exit to the Wii U Menu |
| `wiiu/ogc/` | Replacements of libogc headers |
| `wiiu/libs/` | USB drives: [libfat](https://github.com/wiiu-env/libfat) and [libmocha](https://github.com/wiiu-env/libmocha) (LGPL 3) of wiiu-env |
| `wiiu/images/`, `wiiu/fonts/`, `wiiu/meta/` | Wii U banner, menu font (M+ 1p, SIL OFL 1.1), app icon and splash screens |

Changes to the Wii code are limited to `#ifdef HW_WUP` blocks, so the Wii build is unchanged.
