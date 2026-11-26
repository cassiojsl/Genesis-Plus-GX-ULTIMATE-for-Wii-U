/****************************************************************************
 *  Genesis Plus GX ULTIMATE - Wii U port
 *
 *  The libogc functions the Wii code (gx/) calls, built with the libogc
 *  headers like that code:
 *   - Wii controllers (WPAD) from the Wii Remotes, Classic Controllers and
 *     Pro Controllers of the Wii U, the GamePad as the Wii U GamePad of the
 *     Wii port (WiiDRC); its touch screen points like a Wii Remote in the
 *     menu (a tap is A)
 *   - threads, system, IOS... with what the Wii U does instead, and nothing
 *     for what it does not have (DVD drive, memory cards, GameCube pads)
 ****************************************************************************/

#include <gccore.h>
#include <ogcsys.h>
#include <string.h>
#include <math.h>
#include <fat.h>
#include <iso9660.h>
#include <di/di.h>
#include <sdcard/wiisd_io.h>
#include <wiiuse/wpad.h>
#include <ogc/usbmouse.h>
#include "wiidrc.h"
#include "vi_encoder.h"
#define USBPAD_SHIM_IMPL
#include "usbpad_shim.h"
#include "wiiu_bridge.h"

/****************************************************************************
 * Wii controllers
 ****************************************************************************/

static WPADData wpad_data[4];
static u32 wpad_last[4];

/* GamePad touch screen as the pointer of the menu: valid while touched and
   for a few frames after, with A pressed when the finger is lifted */
static int touch_frames;
static float touch_x, touch_y;
static int touch_tap;

s32 WPAD_Init(void)
{
  wiiu_input_init();
  memset(wpad_data, 0, sizeof(wpad_data));
  return WPAD_ERR_NONE;
}

s32 WPAD_SetDataFormat(s32 chan, s32 fmt) { (void)chan; (void)fmt; return WPAD_ERR_NONE; }
s32 WPAD_SetVRes(s32 chan, u32 xres, u32 yres) { (void)chan; (void)xres; (void)yres; return WPAD_ERR_NONE; }
void WPAD_SetIdleTimeout(u32 seconds) { (void)seconds; }

static void set_js(struct joystick_t *js, float x, float y)
{
  /* calibration 0..255 around 128, as wpad_StickX/Y of gx_input.c read it */
  int px = 128 + (int)(x * 127.0f);
  int py = 128 + (int)(y * 127.0f);
  js->min.x = js->min.y = 0;
  js->max.x = js->max.y = 255;
  js->center.x = js->center.y = 128;
  js->pos.x = (px < 0) ? 0 : (px > 255) ? 255 : px;
  js->pos.y = (py < 0) ? 0 : (py > 255) ? 255 : py;
  js->mag = sqrtf(x * x + y * y);
  if (js->mag > 1.0f) js->mag = 1.0f;
  /* angle in degrees, 0 is up, clockwise (wiiuse) */
  js->ang = (js->mag > 0.0f) ? (atan2f(x, y) * 180.0f / (float)M_PI) : 0.0f;
  if (js->ang < 0.0f) js->ang += 360.0f;
}

s32 WPAD_ScanPads(void)
{
  int chan;

  wiiu_wpad_scan();

  for (chan = 0; chan < 4; chan++)
  {
    const wiiu_wpad_state *s = wiiu_wpad(chan);
    WPADData *d = &wpad_data[chan];
    u32 held = s->connected ? s->held : 0;

    d->err = s->connected ? WPAD_ERR_NONE : WPAD_ERR_NO_CONTROLLER;
    d->data_present = s->connected ? (WPAD_DATA_BUTTONS | WPAD_DATA_IR | WPAD_DATA_EXPANSION) : 0;
    d->btns_h = held;
    d->btns_d = held & ~wpad_last[chan];
    d->btns_u = ~held & wpad_last[chan];
    d->btns_l = wpad_last[chan];
    wpad_last[chan] = held;

    memset(&d->exp, 0, sizeof(d->exp));
    switch (s->ext)
    {
      case WIIU_EXT_NUNCHUK:
        d->exp.type = WPAD_EXP_NUNCHUK;
        set_js(&d->exp.nunchuk.js, s->lx, s->ly);
        break;
      case WIIU_EXT_CLASSIC:
      case WIIU_EXT_PRO:
        d->exp.type = WPAD_EXP_CLASSIC;
        d->exp.classic.type = (s->ext == WIIU_EXT_PRO) ? 2 : 0;
        set_js(&d->exp.classic.ljs, s->lx, s->ly);
        set_js(&d->exp.classic.rjs, s->rx, s->ry);
        break;
      default:
        d->exp.type = WPAD_EXP_NONE;
        break;
    }

    memset(&d->ir, 0, sizeof(d->ir));
    if (s->connected && s->ir_valid)
    {
      float x, y;
      wiiu_menu_point(s->ir_x, s->ir_y, &x, &y);
      d->ir.valid = d->ir.smooth_valid = d->ir.raw_valid = 1;
      d->ir.x = d->ir.sx = d->ir.ax = x;
      d->ir.y = d->ir.sy = d->ir.ay = y;
      d->ir.vres[0] = 640;
      d->ir.vres[1] = 480;
    }
  }

  /* GamePad touch screen: pointer and A of channel 0 */
  {
    const wiiu_drc_state *t;
    wiiu_drc_scan();
    t = wiiu_drc();
    touch_tap = 0;
    if (t->touch)
    {
      touch_frames = 6;
      wiiu_menu_point(t->tx, t->ty, &touch_x, &touch_y);
    }
    else if (touch_frames == 6)
    {
      /* finger lifted: A on the button under it */
      touch_tap = 1;
      touch_frames--;
    }
    else if (touch_frames > 0)
    {
      touch_frames--;
    }

    if (touch_frames && !wpad_data[0].ir.valid)
    {
      WPADData *d = &wpad_data[0];
      d->ir.valid = d->ir.smooth_valid = d->ir.raw_valid = 1;
      d->ir.x = d->ir.sx = d->ir.ax = touch_x;
      d->ir.y = d->ir.sy = d->ir.ay = touch_y;
      d->ir.vres[0] = 640;
      d->ir.vres[1] = 480;
      if (touch_tap)
        d->btns_d |= WPAD_BUTTON_A;
    }
  }

  return WPAD_ERR_NONE;
}

s32 WPAD_Probe(s32 chan, u32 *type)
{
  const wiiu_wpad_state *s = wiiu_wpad(chan);
  if (!s->connected)
  {
    if (type) *type = WPAD_EXP_UNKNOWN;
    return WPAD_ERR_NO_CONTROLLER;
  }
  if (type)
  {
    switch (s->ext)
    {
      case WIIU_EXT_NUNCHUK: *type = WPAD_EXP_NUNCHUK; break;
      case WIIU_EXT_CLASSIC:
      case WIIU_EXT_PRO:     *type = WPAD_EXP_CLASSIC; break;
      default:               *type = WPAD_EXP_NONE; break;
    }
  }
  return WPAD_ERR_NONE;
}

WPADData *WPAD_Data(int chan)
{
  return &wpad_data[chan & 3];
}

u32 WPAD_ButtonsHeld(int chan) { return wpad_data[chan & 3].btns_h; }
u32 WPAD_ButtonsDown(int chan) { return wpad_data[chan & 3].btns_d; }

void WPAD_IR(int chan, struct ir_t *ir)
{
  if (ir) *ir = wpad_data[chan & 3].ir;
}

/****************************************************************************
 * Wii U GamePad (WiiDRC of the Wii port)
 ****************************************************************************/

static u32 drc_held, drc_down, drc_up;

bool WiiDRC_Init(void) { return true; }
bool WiiDRC_Inited(void) { return true; }
bool WiiDRC_Recalibrate(void) { return true; }
bool WiiDRC_Connected(void) { return wiiu_drc()->connected; }
bool WiiDRC_ShutdownRequested(void) { return false; }

bool WiiDRC_ScanPads(void)
{
  const wiiu_drc_state *s;
  u32 held;
  wiiu_drc_scan();
  s = wiiu_drc();
  held = s->connected ? s->held : 0;
  drc_down = held & ~drc_held;
  drc_up = ~held & drc_held;
  drc_held = held;
  return s->connected;
}

u32 WiiDRC_ButtonsHeld(void) { return drc_held; }
u32 WiiDRC_ButtonsDown(void) { return drc_down; }
u32 WiiDRC_ButtonsUp(void)   { return drc_up; }

/* the GamePad sticks of the Wii port go about -75..75 */
s16 WiiDRC_lStickX(void) { return (s16)(wiiu_drc()->lx * 75.0f); }
s16 WiiDRC_lStickY(void) { return (s16)(wiiu_drc()->ly * 75.0f); }
s16 WiiDRC_rStickX(void) { return (s16)(wiiu_drc()->rx * 75.0f); }
s16 WiiDRC_rStickY(void) { return (s16)(wiiu_drc()->ry * 75.0f); }

/****************************************************************************
 * GameCube and USB pads, USB mouse: none on the Wii U
 ****************************************************************************/

u32 PAD_Init(void) { return 1; }
s8 PAD_StickX(int pad) { (void)pad; return 0; }
s8 PAD_StickY(int pad) { (void)pad; return 0; }
s8 PAD_SubStickX(int pad) { (void)pad; return 0; }
s8 PAD_SubStickY(int pad) { (void)pad; return 0; }

u32 USBPAD_ScanPads(void) { return 0; }
u16 USBPAD_ButtonsHeld(int chan) { (void)chan; return 0; }
u16 USBPAD_ButtonsDown(int chan) { (void)chan; return 0; }
u16 USBPAD_ButtonsUp(int chan) { (void)chan; return 0; }
u16 USBPAD_MenuButtonsHeld(int chan) { (void)chan; return 0; }
u16 USBPAD_MenuButtonsDown(int chan) { (void)chan; return 0; }
int USBPAD_MegaDriveMenu(void) { return 0; }

s32 MOUSE_Init(void) { return -1; }
s32 MOUSE_Deinit(void) { return 0; }
s32 MOUSE_GetEvent(mouse_event *event) { memset(event, 0, sizeof(*event)); return 0; }
s32 MOUSE_FlushEvents(void) { return 0; }
bool MOUSE_IsConnected(void) { return false; }

/****************************************************************************
 * Threads
 ****************************************************************************/

s32 LWP_CreateThread(lwp_t *thethread, void *(*entry)(void *), void *arg, void *stackbase, u32 stack_size, u8 prio)
{
  void *h = NULL;
  (void)stackbase; (void)stack_size; (void)prio;
  if (wiiu_thread_create(&h, entry, arg) < 0)
    return -1;
  *thethread = (lwp_t)(u32)h;
  return 0;
}

s32 LWP_JoinThread(lwp_t thethread, void **value_ptr)
{
  return wiiu_thread_join((void *)(u32)thethread, value_ptr);
}

/* "return to loader" of the menu: the Wii U Menu */
void __lwp_thread_stopmultitasking(void (*exitfunc)())
{
  (void)exitfunc;
  wiiu_exit();
}

/****************************************************************************
 * System
 ****************************************************************************/

void *__Arena2Lo = NULL;

static powercallback power_cb;
static resetcallback reset_cb;

powercallback SYS_SetPowerCallback(powercallback cb) { powercallback old = power_cb; power_cb = cb; return old; }
resetcallback SYS_SetResetCallback(resetcallback cb) { resetcallback old = reset_cb; reset_cb = cb; return old; }

void SYS_ResetSystem(s32 reset, u32 reset_code, s32 force_menu)
{
  (void)reset; (void)reset_code; (void)force_menu;
  wiiu_exit();
}

/* free memory shown in the menu: the Wii U has a lot more */
u32 SYS_GetArena1Size(void) { return 0; }
void *SYS_GetArena2Lo(void) { return NULL; }
u32 SYS_GetArena2Size(void) { return 0; }

void L2Enhance(void) {}
s32 IOS_GetVersion(void) { return 58; }
s32 IOS_ReloadIOS(int version) { (void)version; return 0; }

/****************************************************************************
 * Storage: the SD card as "sd:", no DVD drive, no USB storage yet
 ****************************************************************************/

bool fatInitDefault(void)
{
  return wiiu_sd_mount();
}

static bool disc_false(void) { return false; }
static bool disc_rw(sec_t s, sec_t n, void *b) { (void)s; (void)n; (void)b; return false; }

const DISC_INTERFACE __io_wiisd =
{
  DEVICE_TYPE_WII_SD, FEATURE_MEDIUM_CANREAD | FEATURE_MEDIUM_CANWRITE | FEATURE_WII_SD,
  (FN_MEDIUM_STARTUP)disc_false, (FN_MEDIUM_ISINSERTED)disc_false,
  (FN_MEDIUM_READSECTORS)disc_rw, (FN_MEDIUM_WRITESECTORS)disc_rw,
  (FN_MEDIUM_CLEARSTATUS)disc_false, (FN_MEDIUM_SHUTDOWN)disc_false
};

const DISC_INTERFACE __io_wiidvd =
{
  DEVICE_TYPE_WII_DVD, FEATURE_MEDIUM_CANREAD | FEATURE_WII_DVD,
  (FN_MEDIUM_STARTUP)disc_false, (FN_MEDIUM_ISINSERTED)disc_false,
  (FN_MEDIUM_READSECTORS)disc_rw, (FN_MEDIUM_WRITESECTORS)disc_rw,
  (FN_MEDIUM_CLEARSTATUS)disc_false, (FN_MEDIUM_SHUTDOWN)disc_false
};

int DI_Init(void) { return -1; }
void DI_Close(void) {}
int DI_StopMotor(void) { return 0; }
void DI_UseCache(bool use) { (void)use; }
bool ISO9660_Mount(const char *name, const DISC_INTERFACE *disc) { (void)name; (void)disc; return false; }
bool ISO9660_Unmount(const char *name) { (void)name; return true; }
void dvmUnmountVolume(const char *name) { (void)name; }

s32 USB_Deinitialize(void) { return 0; }
void USBStorage_Deinitialize(void) {}

/* GameCube memory cards: none (saves go to the SD card) */
s32 CARD_Init(const char *gamecode, const char *company) { (void)gamecode; (void)company; return CARD_ERROR_NOCARD; }
s32 CARD_ProbeEx(s32 chn, s32 *mem_size, s32 *sect_size) { (void)chn; (void)mem_size; (void)sect_size; return CARD_ERROR_NOCARD; }
s32 CARD_Mount(s32 chn, void *workarea, cardcallback detach_cb) { (void)chn; (void)workarea; (void)detach_cb; return CARD_ERROR_NOCARD; }
s32 CARD_Unmount(s32 chn) { (void)chn; return CARD_ERROR_READY; }
s32 CARD_Open(s32 chn, const char *filename, card_file *file) { (void)chn; (void)filename; (void)file; return CARD_ERROR_NOCARD; }
s32 CARD_Close(card_file *file) { (void)file; return CARD_ERROR_READY; }
s32 CARD_Create(s32 chn, const char *filename, u32 size, card_file *file) { (void)chn; (void)filename; (void)size; (void)file; return CARD_ERROR_NOCARD; }
s32 CARD_Delete(s32 chn, const char *filename) { (void)chn; (void)filename; return CARD_ERROR_NOCARD; }
s32 CARD_Read(card_file *file, void *buffer, u32 len, u32 offset) { (void)file; (void)buffer; (void)len; (void)offset; return CARD_ERROR_NOCARD; }
s32 CARD_Write(card_file *file, void *buffer, u32 len, u32 offset) { (void)file; (void)buffer; (void)len; (void)offset; return CARD_ERROR_NOCARD; }
s32 CARD_GetSectorSize(s32 chn, u32 *sector_size) { (void)chn; (void)sector_size; return CARD_ERROR_NOCARD; }
s32 CARD_GetStatus(s32 chn, s32 fileno, card_stat *stats) { (void)chn; (void)fileno; (void)stats; return CARD_ERROR_NOCARD; }
s32 CARD_SetStatus(s32 chn, s32 fileno, card_stat *stats) { (void)chn; (void)fileno; (void)stats; return CARD_ERROR_NOCARD; }

/****************************************************************************
 * Video interface (the picture itself is in wiiu_video.c)
 ****************************************************************************/

/* the menus pace themselves on the 60 Hz of the Wii video */
void VIDEO_WaitVSync(void)
{
  static u64 next;
  u64 now = wiiu_time_us();
  if (!next || (now > next + 50000)) next = now;
  next += 16683;
  if (next > now) wiiu_sleep_us((u32)(next - now));
}

void VIDEO_Flush(void) {}
void VIDEO_Configure(GXRModeObj *rmode) { (void)rmode; }
u32 VIDEO_HaveComponentCable(void) { return 1; }
void VIDEO_SetGamma(VIGamma gamma) { (void)gamma; }

/****************************************************************************
 * Data cache (bezel.c flushes the texture it makes for the Wii GPU): the
 * Wii U port reads that texture with the CPU (wiiu_video.c), nothing to do
 ****************************************************************************/

void PPCDCacheFlushAsync(const volatile void *buf, size_t size) { (void)buf; (void)size; }
void PPCDCacheStoreAsync(const volatile void *buf, size_t size) { (void)buf; (void)size; }
void PPCDCacheInvalidate(const volatile void *buf, size_t size) { (void)buf; (void)size; }
