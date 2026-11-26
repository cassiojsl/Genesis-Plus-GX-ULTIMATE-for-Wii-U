**Genesis Plus GX ULTIMATE for Wii U**: the first version of the **native Wii U port** of Genesis Plus GX ULTIMATE, a Mega Drive / Genesis, Mega CD, **32X**, Master System, Game Gear and SG-1000 emulator based on [Genesis Plus GX 1.7.5](https://github.com/ekeeke/Genesis-Plus-GX) (Eke-Eke). The 32X games run with [PicoDrive](https://github.com/irixxxx/picodrive) (notaz, irixxxx and others).

It is a Wii U app for Aroma (not the Wii version in vWii mode), with the same interface as the Wii version in widescreen on the TV and the GamePad.

## Features

- Uses the Wii U hardware: the game is emulated on one CPU core while the frame before is drawn on another; the GPU at 720p, 16:9, with VSync.
- GamePad: buttons, sticks, touch screen to point and tap the menus. HOME while playing opens the emulator menu; HOME again opens the Wii U HOME Menu.
- Wii Remote (pointer, Nunchuk, Classic Controller) and Wii U Pro Controller.
- SD card and FAT32 USB drives (games only on USB).
- Sega 32X with PicoDrive's SH-2 Dynamic Recompiler in the JIT area of the system (experimental, off by default: System > 32X Settings).
- PIXEL PERFECT aspect, Auto Crop (FIT by default), bezels, NTSC filters, per-game settings, cheats, save states, menu music.
- The fixes of the official Genesis Plus GX up to November 11, 2026.

## Install

1. Aroma is required.
2. Copy `genplus_wiiu.wuhb` to `sd:/wiiu/apps/`.
3. Games go in `sd:/wiiu/apps/genplusultimate/roms/` (made on the first start) or on a FAT32 USB drive.

Details: [wiiu/README.md](https://github.com/cassiojsl/GenesisPlusGX-ULTIMATE/blob/main/wiiu/README.md).
