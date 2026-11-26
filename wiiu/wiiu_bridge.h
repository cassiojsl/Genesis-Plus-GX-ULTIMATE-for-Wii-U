/****************************************************************************
 *  Genesis Plus GX ULTIMATE - Wii U port
 *
 *  Between the libogc replacements (wiiu_ogc.c, built with the libogc headers
 *  like the Wii code) and the Wii U system (wiiu_sys.c, built with the wut
 *  headers): both sets of headers cannot be used in the same file, so they
 *  talk through these plain C types.
 ****************************************************************************/

#ifndef _WIIU_BRIDGE_H_
#define _WIIU_BRIDGE_H_

#include <stdint.h>

/* Wii Remote with its expansion, Classic Controller, Wii U Pro Controller */
enum { WIIU_EXT_NONE = 0, WIIU_EXT_NUNCHUK, WIIU_EXT_CLASSIC, WIIU_EXT_PRO };

typedef struct
{
  int connected;
  int ext;            /* WIIU_EXT_* */
  uint32_t held;      /* buttons, in the libogc layout (WPAD_BUTTON_*, WPAD_CLASSIC_BUTTON_* ...) */
  float lx, ly;       /* left stick (nunchuk, classic, pro), -1..1, up is positive */
  float rx, ry;       /* right stick (classic, pro) */
  int ir_valid;       /* the Wii Remote points at the screen */
  float ir_x, ir_y;   /* where, 0..1 across the TV picture */
} wiiu_wpad_state;

/* Wii U GamePad */
typedef struct
{
  int connected;
  uint16_t held;      /* WIIDRC_BUTTON_* layout (the same bits as VPAD) */
  uint8_t extra;      /* WIIDRC_EXTRA_BUTTON_L3 / R3 / TV */
  float lx, ly, rx, ry; /* sticks, -1..1, up is positive */
  int touch;          /* the screen is touched */
  float tx, ty;       /* where, 0..1 across the screen */
} wiiu_drc_state;

void wiiu_input_init(void);
void wiiu_wpad_scan(void);
void wiiu_drc_scan(void);
const wiiu_wpad_state *wiiu_wpad(int chan);
const wiiu_drc_state *wiiu_drc(void);

/* screen position (0..1) to the 640x480 menu screen of the Wii (wiiu_video.c) */
void wiiu_menu_point(float fx, float fy, float *x, float *y);

/* time */
uint64_t wiiu_time_us(void);
void wiiu_sleep_us(uint32_t us);

/* "sd:" device on the SD card (fs:/vol/external01), the default device */
int wiiu_sd_mount(void);

/* threads (libogc LWP), on the core of the main thread */
int wiiu_thread_create(void **handle, void *(*entry)(void *), void *arg);
int wiiu_thread_join(void *handle, void **value);

/* the system font of the Wii U (TrueType), for the menus */
int wiiu_system_font(const void **data, uint32_t *size);

/* back to the Wii U Menu */
void wiiu_exit(void) __attribute__((noreturn));

#endif /* _WIIU_BRIDGE_H_ */
