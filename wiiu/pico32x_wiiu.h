/****************************************************************************
 *  Genesis Plus GX - Wii U port
 *
 *  Sega 32X games, emulated by PicoDrive (picodrive/, its libretro core,
 *  linked in as picodrive_core.o). The Wii U port keeps the menus, the
 *  controllers, the video and the sound output; PicoDrive runs the game.
 ****************************************************************************/

#ifndef _PICO32X_WIIU_H_
#define _PICO32X_WIIU_H_

#include "types.h"

extern int pico32x_active;                       /* a 32X game is loaded */

extern int  pico32x_is_32x_file(const char *filename);
extern int  pico32x_load(const char *filename, int six_buttons, int region);
extern void pico32x_unload(void);
extern void pico32x_frame(uint16 pad1, uint16 pad2);  /* one frame, picture into bitmap */
extern int  pico32x_audio(int16 *out, int max_frames); /* stereo frames of the last frame */
extern void pico32x_reset(void);
extern int  pico32x_is_pal(void);

extern int  pico32x_state_size(void);
extern int  pico32x_state_save(uint8 *buffer, int size);
extern int  pico32x_state_load(const uint8 *buffer, int size);
extern uint8 *pico32x_sram(int *size);            /* the game's backup RAM */

extern const char *pico32x_last_error(void);

#endif /* _PICO32X_WIIU_H_ */
