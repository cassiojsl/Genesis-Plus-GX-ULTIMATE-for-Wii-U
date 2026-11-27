## Genesis Plus GX ULTIMATE for Wii U — v1.0.1

### Fixes
- **Sound**: the sound now goes straight to AX, the sound system of the Wii U, instead of the audio driver of SDL. This fixes the loud, distorted noise that could appear at random, in games and in the menus.
- **HOME button**: the Wii U HOME Menu now opens only from the main menu of the emulator. In the other menus and while setting up the controls, HOME works as in the original Genesis Plus GX: it ends the button setup early and keeps the rest of the keys, including HOME for the emulator menu.

### Changes
- **GamePad**: when player 1 uses another controller (Wii U Pro Controller, Wii Remote, Classic Controller) and no player uses the GamePad, the GamePad also plays as player 1.
- **Sega 32X**: the Dynamic Recompiler is now on by default (used only if the JIT area of the system works).

### Install
Copy `genplus_wiiu.wuhb` to `sd:/wiiu/apps/` (Aroma required).
