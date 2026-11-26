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

/* "usb:" device: a FAT32 USB drive (libfat + libmocha), 1 when mounted */
int wiiu_usb_mount(void);
void wiiu_usb_unmount(void);

/* threads (libogc LWP), on the core of the main thread */
int wiiu_thread_create(void **handle, void *(*entry)(void *), void *arg);
int wiiu_thread_join(void *handle, void **value);

/* the system font of the Wii U (TrueType), for the menus */
int wiiu_system_font(const void **data, uint32_t *size);

/* codegen (JIT) area of the system, for a future SH-2 recompiler of the 32X:
   where it is, the core allowed to use it, whether switching it between
   writable and executable works, and (exec) whether a small generated
   function runs from it */
typedef struct
{
  uint32_t addr, size;  /* 0 when the app has no codegen area */
  int core;             /* core allowed to use it */
  int switch_ok;        /* OSSwitchSecCodeGenMode works (RW then RX) */
  int exec_tested;      /* the run test was done */
  int exec_ok;          /* the generated code returned the right value */
} wiiu_jit_state;

void wiiu_jit_probe(int exec, wiiu_jit_state *state);

/* the SH-2 recompiler of PicoDrive (32X) can be used: the codegen area runs
   code, and is big enough */
int wiiu_jit_available(void);
/* runs fn on the core of the codegen area (PicoDrive with its recompiler) */
void wiiu_jit_call(void (*fn)(void *), void *arg);
/* why it cannot (the 32X menu) */
void wiiu_jit_why(char *text, int size);

/* debug log on the SD card (sd:/wiiu/apps/genplusultimate/log.txt), one line
   at a time, in a build made with -DWIIU_DEBUG_LOG (nothing otherwise) */
void wiiu_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* HOME Menu of the Wii U allowed (the emulator menus) or not (a game) */
void wiiu_home_menu(int allowed);

/* back to the Wii U Menu */
void wiiu_exit(void) __attribute__((noreturn));

#endif /* _WIIU_BRIDGE_H_ */
