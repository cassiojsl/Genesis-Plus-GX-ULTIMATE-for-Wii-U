/****************************************************************************
 *  Genesis Plus GX ULTIMATE - Wii U port
 *
 *  The Wii U system side (wut headers): controllers through VPAD (GamePad)
 *  and KPAD (Wii Remote, Classic Controller, Pro Controller), time, threads,
 *  the "sd:" device and the way back to the Wii U Menu.
 ****************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/iosupport.h>
#include <sys/stat.h>
#include <coreinit/thread.h>
#include <coreinit/core.h>
#include <coreinit/time.h>
#include <coreinit/systeminfo.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>
#include <vpad/input.h>
#include <padscore/kpad.h>
#include <padscore/wpad.h>
#include <sysapp/launch.h>
#include <SDL.h>

#include "wiiu_bridge.h"

/****************************************************************************
 * Controllers
 ****************************************************************************/

static wiiu_wpad_state wpad[4];
static wiiu_drc_state drc;

/* libogc button bits (wiiuse/wpad.h) */
#define OGC_BUTTON_2      0x0001
#define OGC_BUTTON_1      0x0002
#define OGC_BUTTON_B      0x0004
#define OGC_BUTTON_A      0x0008
#define OGC_BUTTON_MINUS  0x0010
#define OGC_BUTTON_HOME   0x0080
#define OGC_BUTTON_LEFT   0x0100
#define OGC_BUTTON_RIGHT  0x0200
#define OGC_BUTTON_DOWN   0x0400
#define OGC_BUTTON_UP     0x0800
#define OGC_BUTTON_PLUS   0x1000
#define OGC_NUNCHUK_Z     (0x0001 << 16)
#define OGC_NUNCHUK_C     (0x0002 << 16)

void wiiu_input_init(void)
{
  KPADInit();
  WPADEnableURCC(TRUE);      /* Wii U Pro Controller */
  WPADEnableWiiRemote(TRUE);

  /* HOME goes to the menu of the emulator, as on the Wii (its Exit
     option goes back to the Wii U Menu) */
  OSEnableHomeButtonMenu(FALSE);
}

static uint32_t core_buttons(uint32_t h)
{
  uint32_t b = 0;
  if (h & WPAD_BUTTON_LEFT)  b |= OGC_BUTTON_LEFT;
  if (h & WPAD_BUTTON_RIGHT) b |= OGC_BUTTON_RIGHT;
  if (h & WPAD_BUTTON_DOWN)  b |= OGC_BUTTON_DOWN;
  if (h & WPAD_BUTTON_UP)    b |= OGC_BUTTON_UP;
  if (h & WPAD_BUTTON_PLUS)  b |= OGC_BUTTON_PLUS;
  if (h & WPAD_BUTTON_2)     b |= OGC_BUTTON_2;
  if (h & WPAD_BUTTON_1)     b |= OGC_BUTTON_1;
  if (h & WPAD_BUTTON_B)     b |= OGC_BUTTON_B;
  if (h & WPAD_BUTTON_A)     b |= OGC_BUTTON_A;
  if (h & WPAD_BUTTON_MINUS) b |= OGC_BUTTON_MINUS;
  if (h & WPAD_BUTTON_HOME)  b |= OGC_BUTTON_HOME;
  return b;
}

void wiiu_wpad_scan(void)
{
  int chan;
  for (chan = 0; chan < 4; chan++)
  {
    KPADStatus s;
    KPADError err = KPAD_ERROR_OK;
    wiiu_wpad_state *w = &wpad[chan];

    memset(&s, 0, sizeof(s));
    if (KPADReadEx((KPADChan)chan, &s, 1, &err) <= 0)
    {
      /* no new sample: keep the last state unless the controller is gone */
      if (err == KPAD_ERROR_NO_SAMPLES)
        continue;
      memset(w, 0, sizeof(*w));
      continue;
    }

    w->connected = 1;
    w->lx = w->ly = w->rx = w->ry = 0;
    w->ir_valid = 0;

    switch (s.extensionType)
    {
      case WPAD_EXT_NUNCHUK:
      case WPAD_EXT_MPLUS_NUNCHUK:
        w->ext = WIIU_EXT_NUNCHUK;
        w->held = core_buttons(s.hold);
        if (s.nunchuk.hold & WPAD_NUNCHUK_BUTTON_Z) w->held |= OGC_NUNCHUK_Z;
        if (s.nunchuk.hold & WPAD_NUNCHUK_BUTTON_C) w->held |= OGC_NUNCHUK_C;
        w->lx = s.nunchuk.stick.x;
        w->ly = s.nunchuk.stick.y;
        break;

      case WPAD_EXT_CLASSIC:
      case WPAD_EXT_MPLUS_CLASSIC:
        /* the Classic Controller bits are those of libogc, 16 bits up */
        w->ext = WIIU_EXT_CLASSIC;
        w->held = core_buttons(s.hold) | ((s.classic.hold & 0xffff) << 16);
        w->lx = s.classic.leftStick.x;
        w->ly = s.classic.leftStick.y;
        w->rx = s.classic.rightStick.x;
        w->ry = s.classic.rightStick.y;
        break;

      case WPAD_EXT_PRO_CONTROLLER:
        /* the Wii U Pro Controller is a Classic Controller (type 2) for libogc */
        w->ext = WIIU_EXT_PRO;
        w->held = (s.pro.hold & 0xffff) << 16;
        w->lx = s.pro.leftStick.x;
        w->ly = s.pro.leftStick.y;
        w->rx = s.pro.rightStick.x;
        w->ry = s.pro.rightStick.y;
        break;

      default:
        w->ext = WIIU_EXT_NONE;
        w->held = core_buttons(s.hold);
        break;
    }

    /* pointer (Wii Remote only), -1..1 to the 640x480 menu screen */
    if ((w->ext == WIIU_EXT_NONE || w->ext == WIIU_EXT_NUNCHUK) && (s.posValid > 0))
    {
      w->ir_valid = 1;
      w->ir_x = (s.pos.x + 1.0f) * 320.0f;
      w->ir_y = (s.pos.y + 1.0f) * 240.0f;
    }
  }
}

void wiiu_drc_scan(void)
{
  VPADStatus s;
  VPADReadError err = VPAD_READ_SUCCESS;

  memset(&s, 0, sizeof(s));
  if (VPADRead(VPAD_CHAN_0, &s, 1, &err) <= 0)
  {
    if (err == VPAD_READ_NO_SAMPLES)
      return;
    memset(&drc, 0, sizeof(drc));
    return;
  }

  drc.connected = 1;
  drc.held = s.hold & 0xffff;
  drc.extra = 0;
  if (s.hold & VPAD_BUTTON_STICK_L) drc.extra |= 0x80;
  if (s.hold & VPAD_BUTTON_STICK_R) drc.extra |= 0x40;
  if (s.hold & VPAD_BUTTON_TV)      drc.extra |= 0x20;
  drc.lx = s.leftStick.x;
  drc.ly = s.leftStick.y;
  drc.rx = s.rightStick.x;
  drc.ry = s.rightStick.y;

  drc.touch = 0;
  if (s.tpNormal.touched && (s.tpNormal.validity == VPAD_VALID))
  {
    VPADTouchData p;
    VPADGetTPCalibratedPoint(VPAD_CHAN_0, &p, &s.tpNormal);
    /* calibrated to 1280x720 */
    drc.touch = 1;
    drc.tx = p.x * 640.0f / 1280.0f;
    drc.ty = p.y * 480.0f / 720.0f;
  }
}

const wiiu_wpad_state *wiiu_wpad(int chan)
{
  return &wpad[chan & 3];
}

const wiiu_drc_state *wiiu_drc(void)
{
  return &drc;
}

/****************************************************************************
 * Time
 ****************************************************************************/

uint64_t wiiu_time_us(void)
{
  return (uint64_t)OSTicksToMicroseconds(OSGetSystemTime());
}

void wiiu_sleep_us(uint32_t us)
{
  OSSleepTicks(OSMicrosecondsToTicks(us));
}

/****************************************************************************
 * "sd:" device: the paths of the Wii code (sd:/genplusultimate/...) on the
 * SD card of the Wii U (fs:/vol/external01/genplusultimate/...)
 ****************************************************************************/

#define SD_ROOT "fs:/vol/external01"

static const devoptab_t *fs_dev;
static devoptab_t sd_dev;
static char sd_cwd[1024] = "/";

/* "sd:/a/b", "/a/b" (default device) or "a/b" (current folder) */
static const char *sd_path(const char *path, char *out, size_t len)
{
  const char *colon = strchr(path, ':');
  if (colon) path = colon + 1;
  if (path[0] == '/')
    snprintf(out, len, "%s%s", SD_ROOT, path);
  else
    snprintf(out, len, "%s%s%s%s", SD_ROOT, sd_cwd, (sd_cwd[strlen(sd_cwd) - 1] == '/') ? "" : "/", path);
  return out;
}

static int sd_open(struct _reent *r, void *fs, const char *path, int flags, int mode)
{ char p[1024]; return fs_dev->open_r(r, fs, sd_path(path, p, sizeof(p)), flags, mode); }

static int sd_stat(struct _reent *r, const char *path, struct stat *st)
{ char p[1024]; return fs_dev->stat_r(r, sd_path(path, p, sizeof(p)), st); }

static int sd_lstat(struct _reent *r, const char *path, struct stat *st)
{ char p[1024]; return fs_dev->lstat_r ? fs_dev->lstat_r(r, sd_path(path, p, sizeof(p)), st)
                                       : fs_dev->stat_r(r, sd_path(path, p, sizeof(p)), st); }

static int sd_unlink(struct _reent *r, const char *path)
{ char p[1024]; return fs_dev->unlink_r(r, sd_path(path, p, sizeof(p))); }

static int sd_rmdir(struct _reent *r, const char *path)
{ char p[1024]; return fs_dev->rmdir_r ? fs_dev->rmdir_r(r, sd_path(path, p, sizeof(p)))
                                       : fs_dev->unlink_r(r, sd_path(path, p, sizeof(p))); }

static int sd_mkdir(struct _reent *r, const char *path, int mode)
{ char p[1024]; return fs_dev->mkdir_r(r, sd_path(path, p, sizeof(p)), mode); }

static int sd_rename(struct _reent *r, const char *a, const char *b)
{ char p[1024], q[1024]; return fs_dev->rename_r(r, sd_path(a, p, sizeof(p)), sd_path(b, q, sizeof(q))); }

static DIR_ITER *sd_diropen(struct _reent *r, DIR_ITER *d, const char *path)
{ char p[1024]; return fs_dev->diropen_r(r, d, sd_path(path, p, sizeof(p))); }

static int sd_statvfs(struct _reent *r, const char *path, struct statvfs *buf)
{ char p[1024]; return fs_dev->statvfs_r ? fs_dev->statvfs_r(r, sd_path(path, p, sizeof(p)), buf) : -1; }

static int sd_chdir(struct _reent *r, const char *path)
{
  char p[1024];
  struct stat st;
  const char *colon = strchr(path, ':');
  const char *rel = colon ? colon + 1 : path;

  if (fs_dev->stat_r(r, sd_path(path, p, sizeof(p)), &st) || !S_ISDIR(st.st_mode))
  {
    r->_errno = ENOENT;
    return -1;
  }
  if (rel[0] == '/')
    snprintf(sd_cwd, sizeof(sd_cwd), "%s", rel);
  else
  {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s%s%s", sd_cwd, (sd_cwd[strlen(sd_cwd) - 1] == '/') ? "" : "/", rel);
    snprintf(sd_cwd, sizeof(sd_cwd), "%s", tmp);
  }
  return 0;
}

int wiiu_sd_mount(void)
{
  int dev;

  if (fs_dev)
    return 1;

  fs_dev = GetDeviceOpTab("fs:");
  if (!fs_dev)
    return 0;

  sd_dev = *fs_dev;
  sd_dev.name      = "sd";
  sd_dev.open_r    = sd_open;
  sd_dev.stat_r    = sd_stat;
  sd_dev.lstat_r   = sd_lstat;
  sd_dev.unlink_r  = sd_unlink;
  sd_dev.rmdir_r   = sd_rmdir;
  sd_dev.mkdir_r   = sd_mkdir;
  sd_dev.rename_r  = sd_rename;
  sd_dev.chdir_r   = sd_chdir;
  sd_dev.diropen_r = sd_diropen;
  sd_dev.statvfs_r = sd_statvfs;
  sd_dev.link_r    = NULL;
  sd_dev.chmod_r   = NULL;
  sd_dev.utimes_r  = NULL;
  sd_dev.symlink_r = NULL;
  sd_dev.readlink_r = NULL;
  sd_dev.pathconf_r = NULL;

  if (AddDevice(&sd_dev) < 0)
    return 0;

  /* "/genplusultimate/..." without a device is on the SD card, as on the Wii */
  dev = FindDevice("sd:");
  if (dev >= 0)
    setDefaultDevice(dev);
  return 1;
}

/****************************************************************************
 * Threads (libogc LWP): on the core of the main thread, so that a thread
 * that draws (the message box of the menu) uses the same graphics context
 ****************************************************************************/

typedef struct
{
  OSThread thread;           /* first: OSThread must be aligned */
  void *(*entry)(void *);
  void *arg;
  void *value;
  uint8_t *stack;
} wiiu_thread;

#define THREAD_STACK (128 * 1024)

static int thread_main(int argc, void *argv)
{
  wiiu_thread *t = (wiiu_thread *)argv;
  (void)argc;
  t->value = t->entry(t->arg);
  return 0;
}

int wiiu_thread_create(void **handle, void *(*entry)(void *), void *arg)
{
  wiiu_thread *t = (wiiu_thread *)MEMAllocFromDefaultHeapEx(sizeof(wiiu_thread), 16);
  OSThreadAttributes core;

  if (!t) return -1;
  memset(t, 0, sizeof(*t));
  t->stack = (uint8_t *)MEMAllocFromDefaultHeapEx(THREAD_STACK, 16);
  if (!t->stack)
  {
    MEMFreeToDefaultHeap(t);
    return -1;
  }
  t->entry = entry;
  t->arg = arg;

  switch (OSGetCoreId())
  {
    case 0:  core = OS_THREAD_ATTRIB_AFFINITY_CPU0; break;
    case 2:  core = OS_THREAD_ATTRIB_AFFINITY_CPU2; break;
    default: core = OS_THREAD_ATTRIB_AFFINITY_CPU1; break;
  }

  /* a lower priority than the main thread (16): it runs while the main
     thread waits (files...) */
  if (!OSCreateThread(&t->thread, thread_main, 0, t, t->stack + THREAD_STACK, THREAD_STACK, 17, core))
  {
    MEMFreeToDefaultHeap(t->stack);
    MEMFreeToDefaultHeap(t);
    return -1;
  }
  OSResumeThread(&t->thread);
  *handle = t;
  return 0;
}

int wiiu_thread_join(void *handle, void **value)
{
  wiiu_thread *t = (wiiu_thread *)handle;
  int ret;
  if (!t) return -1;
  OSJoinThread(&t->thread, &ret);
  if (value) *value = t->value;
  MEMFreeToDefaultHeap(t->stack);
  MEMFreeToDefaultHeap(t);
  return 0;
}

/****************************************************************************
 * System font
 ****************************************************************************/

int wiiu_system_font(const void **data, uint32_t *size)
{
  void *p = NULL;
  uint32_t n = 0;
  if (!OSGetSharedData(OS_SHAREDDATATYPE_FONT_STANDARD, 0, &p, &n) || !p)
    return 0;
  *data = p;
  *size = n;
  return 1;
}

/****************************************************************************
 * Exit: back to the Wii U Menu (the system then ends the app, SDL tells it)
 ****************************************************************************/

void wiiu_exit(void)
{
  SYSLaunchMenu();
  for (;;)
  {
    SDL_Event ev;
    while (SDL_PollEvent(&ev))
    {
      if (ev.type == SDL_QUIT)
      {
        SDL_Quit();
        exit(0);
      }
    }
    SDL_Delay(10);
  }
}
