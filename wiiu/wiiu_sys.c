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
#include <stdarg.h>
#include <sys/iosupport.h>
#include <sys/stat.h>
#include <coreinit/thread.h>
#include <coreinit/core.h>
#include <coreinit/time.h>
#include <coreinit/systeminfo.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>
#include <coreinit/codegen.h>
#include <coreinit/semaphore.h>
#include <coreinit/cache.h>
#include <vpad/input.h>
#include <padscore/kpad.h>
#include <padscore/wpad.h>
#include <sysapp/launch.h>
#include <SDL.h>

#include "wiiu_bridge.h"
#include <mocha/mocha.h>
#include <mocha/disc_interface.h>

/* libfat (wiiu/libs), its fat.h is the libogc one for the Wii code */
extern bool fatMount(const char *name, const DISC_INTERFACE *interface, uint32_t startSector, uint32_t cacheSize, uint32_t SectorsPerPage);
extern void fatUnmount(const char *name);

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

  /* HOME goes to the menu of the emulator, as on the Wii; in that menu,
     HOME opens the HOME Menu of the Wii U (wiiu_home_menu) */
  OSEnableHomeButtonMenu(FALSE);
}

/****************************************************************************
 * HOME Menu of the Wii U: allowed in the menus of the emulator (once HOME
 * is released, so the press that opened the menu does not open it too),
 * never while a game runs. While it is allowed, the system takes HOME: the
 * menus do not see it.
 ****************************************************************************/

static int home_wanted = 1, home_enabled, home_held;

void wiiu_home_menu(int allowed)
{
  home_wanted = allowed;
  if (!allowed && home_enabled)
  {
    OSEnableHomeButtonMenu(FALSE);
    home_enabled = 0;
  }
}

static void home_update(void)
{
  if (home_wanted && !home_enabled && !home_held)
  {
    OSEnableHomeButtonMenu(TRUE);
    home_enabled = 1;
  }
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

#define HOME_BITS (OGC_BUTTON_HOME | (WPAD_CLASSIC_BUTTON_HOME << 16) | (WPAD_PRO_BUTTON_HOME << 16))

static int wpad_home;

void wiiu_wpad_scan(void)
{
  int chan;
  wpad_home = 0;
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

    if (w->held & HOME_BITS)
    {
      wpad_home = 1;
      if (home_enabled) w->held &= ~HOME_BITS;
    }

    /* pointer (Wii Remote only), -1..1 to 0..1 */
    if ((w->ext == WIIU_EXT_NONE || w->ext == WIIU_EXT_NUNCHUK) && (s.posValid > 0))
    {
      w->ir_valid = 1;
      w->ir_x = (s.pos.x + 1.0f) * 0.5f;
      w->ir_y = (s.pos.y + 1.0f) * 0.5f;
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
  home_held = wpad_home || (drc.held & VPAD_BUTTON_HOME);
  home_update();
  if (home_enabled) drc.held &= ~VPAD_BUTTON_HOME;
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
    drc.tx = p.x / 1280.0f;
    drc.ty = p.y / 720.0f;
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
 * "sd:" device: the paths of the Wii code (sd:/wiiu/apps/genplusultimate/...)
 * on the SD card of the Wii U (fs:/vol/external01/wiiu/apps/genplusultimate/...)
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
  wiiu_usb_unmount();
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

/****************************************************************************
 * Codegen (JIT) area: only the core given by OSGetCodegenCore() may switch it
 * between writable and executable, so the test runs in a thread on that core.
 ****************************************************************************/

static int jit_thread(int argc, void *argv)
{
  wiiu_jit_state *s = (wiiu_jit_state *)argv;
  uint32_t *code = (uint32_t *)s->addr;
  (void)argc;

  if (!OSSwitchSecCodeGenMode(CODEGEN_RW_))
    return 0;

  if (s->exec_tested)
  {
    code[0] = 0x3860002A;   /* li  r3,42 */
    code[1] = 0x4E800020;   /* blr       */
    DCFlushRange(code, 8);
  }

  if (!OSSwitchSecCodeGenMode(CODEGEN_R_X))
    return 0;
  s->switch_ok = 1;

  if (s->exec_tested)
  {
    int (*fn)(void) = (int (*)(void))code;
    ICInvalidateRange(code, 8);
    s->exec_ok = (fn() == 42);
  }
  return 0;
}

void wiiu_jit_probe(int exec, wiiu_jit_state *state)
{
  static OSThread thread __attribute__((aligned(16)));
  void *addr = NULL;
  uint32_t size = 0;
  uint8_t *stack;
  OSThreadAttributes affinity;
  int ret;

  memset(state, 0, sizeof(*state));
  OSGetCodegenVirtAddrRange(&addr, &size);
  state->core = (int)OSGetCodegenCore();
  if (!addr || (size < 8))
    return;
  state->addr = (uint32_t)addr;
  state->size = size;

  switch (state->core)
  {
    case 0:  affinity = OS_THREAD_ATTRIB_AFFINITY_CPU0; break;
    case 1:  affinity = OS_THREAD_ATTRIB_AFFINITY_CPU1; break;
    case 2:  affinity = OS_THREAD_ATTRIB_AFFINITY_CPU2; break;
    default: return;
  }

  stack = (uint8_t *)MEMAllocFromDefaultHeapEx(THREAD_STACK, 16);
  if (!stack)
    return;
  state->exec_tested = exec;
  if (OSCreateThread(&thread, jit_thread, 0, state, stack + THREAD_STACK, THREAD_STACK, 16, affinity))
  {
    OSResumeThread(&thread);
    OSJoinThread(&thread, &ret);
  }
  MEMFreeToDefaultHeap(stack);
}

/****************************************************************************
 * Debug log (built with -DWIIU_DEBUG_LOG only)
 ****************************************************************************/

void wiiu_log(const char *fmt, ...)
{
#ifdef WIIU_DEBUG_LOG
  static int started;
  char line[256];
  va_list ap;
  FILE *f;
  int n;

  n = snprintf(line, sizeof(line), "[%8llu] ", (unsigned long long)(wiiu_time_us() / 1000));
  va_start(ap, fmt);
  vsnprintf(line + n, sizeof(line) - n, fmt, ap);
  va_end(ap);

  f = fopen("sd:/wiiu/apps/genplusultimate/log.txt", started ? "a" : "w");
  if (!f) return;
  started = 1;
  fprintf(f, "%s\n", line);
  fclose(f);
#else
  (void)fmt;
#endif
}

/****************************************************************************
 * "usb:" device: a FAT32 USB drive, read through libmocha (the Mocha payload
 * of Aroma) and libfat
 ****************************************************************************/

static int usb_state; /* 0: not tried, 1: mounted, -1: Mocha missing */

int wiiu_usb_mount(void)
{
  MochaUtilsStatus st;

  if (usb_state == 1)
    return 1;
  if (usb_state == 0)
  {
    st = Mocha_InitLibrary();
    if (st != MOCHA_RESULT_SUCCESS)
    {
      wiiu_log("USB: Mocha_InitLibrary failed (%d)", (int)st);
      usb_state = -1;
      return 0;
    }
    usb_state = 2; /* Mocha ready, no drive yet */
  }
  if (usb_state < 0)
    return 0;

  if (fatMount("usb", &Mocha_usb_disc_interface, 0, 32, 64))
  {
    wiiu_log("USB: drive mounted as usb:");
    usb_state = 1;
    return 1;
  }
  wiiu_log("USB: no FAT32 drive");
  return 0;
}

void wiiu_usb_unmount(void)
{
  if (usb_state == 1)
  {
    fatUnmount("usb:");
    usb_state = 2;
  }
  if (usb_state == 2)
  {
    Mocha_DeInitLibrary();
    usb_state = 0;
  }
}

/****************************************************************************
 * SH-2 recompiler of PicoDrive (32X): its code goes into the codegen area,
 * which is either writable or executable, and only from one core
 * (OSGetCodegenCore). PicoDrive runs on that core (wiiu_jit_call) and makes
 * the area writable around what writes code (wiiu_jit_write, DRC_WR_* in
 * picodrive/cpu/drc/cmn.h).
 ****************************************************************************/

static wiiu_jit_state jit;
static int jit_checked, jit_depth;
void wiiu_jit_write(int on);

int wiiu_jit_available(void)
{
  if (!jit_checked)
  {
    jit_checked = 1;
    wiiu_jit_probe(1, &jit);
    wiiu_log("JIT area: %u KB at %08x, core %d, switch %d, run %d", (unsigned)(jit.size / 1024),
             (unsigned)jit.addr, jit.core, jit.switch_ok, jit.exec_ok);
  }
  /* PicoDrive needs 4 MB (DRC_TCACHE_SIZE); its calls to the C code of the
     app load the whole address (emith_abicall), so the area can be anywhere */
  return jit.exec_ok && (jit.size >= 4 * 1024 * 1024);
}

void *wiiu_jit_area(size_t size)
{
  if (!wiiu_jit_available() || (size > jit.size))
    return NULL;
  return (void *)jit.addr;
}

void wiiu_jit_write(int on)
{
  if (on)
  {
    if (jit_depth++ == 0)
      OSSwitchSecCodeGenMode(CODEGEN_RW_);
  }
  else if (jit_depth > 0)
  {
    if (--jit_depth == 0)
      OSSwitchSecCodeGenMode(CODEGEN_R_X);
  }
}

/* a thread on the codegen core runs PicoDrive for the main thread */
static OSThread jit_thread_obj __attribute__((aligned(16)));
static uint8_t *jit_stack;
static OSSemaphore jit_go, jit_done;
static void (*volatile jit_fn)(void *);
static void *volatile jit_arg;
static int jit_thread_on;

static int jit_worker(int argc, void *argv)
{
  (void)argc; (void)argv;
  for (;;)
  {
    OSWaitSemaphore(&jit_go);
    jit_fn(jit_arg);
    OSSignalSemaphore(&jit_done);
  }
  return 0;
}

#define JIT_STACK (512 * 1024)

void wiiu_jit_call(void (*fn)(void *), void *arg)
{
  OSThreadAttributes affinity;

  if (!wiiu_jit_available() || ((int)OSGetCoreId() == jit.core))
  {
    fn(arg);
    return;
  }

  if (!jit_thread_on)
  {
    switch (jit.core)
    {
      case 0:  affinity = OS_THREAD_ATTRIB_AFFINITY_CPU0; break;
      case 1:  affinity = OS_THREAD_ATTRIB_AFFINITY_CPU1; break;
      default: affinity = OS_THREAD_ATTRIB_AFFINITY_CPU2; break;
    }
    jit_stack = (uint8_t *)MEMAllocFromDefaultHeapEx(JIT_STACK, 16);
    OSInitSemaphore(&jit_go, 0);
    OSInitSemaphore(&jit_done, 0);
    if (!jit_stack || !OSCreateThread(&jit_thread_obj, jit_worker, 0, NULL,
                                      jit_stack + JIT_STACK, JIT_STACK, 16, affinity))
    {
      wiiu_log("JIT: no thread on core %d", jit.core);
      fn(arg);
      return;
    }
    OSResumeThread(&jit_thread_obj);
    jit_thread_on = 1;
    wiiu_log("JIT: PicoDrive runs on core %d", jit.core);
  }

  jit_fn = fn;
  jit_arg = arg;
  OSSignalSemaphore(&jit_go);
  OSWaitSemaphore(&jit_done);
}

/****************************************************************************
 * Emulation on another core (run_frames in gx/main.c): the frames run in a
 * thread on a core other than the one of the main thread (the GPU, SDL), on
 * the codegen core when there is one (the 32X recompiler), while the main
 * thread shows the frame before.
 ****************************************************************************/

static OSThread emu_thread_obj __attribute__((aligned(16)));
static uint8_t *emu_stack;
static OSSemaphore emu_go, emu_done;
static int (*volatile emu_fn)(void);
static volatile int emu_result;
static int emu_state; /* 0: not made, 1: running, -1: none (frames run here) */

static int emu_worker(int argc, void *argv)
{
  (void)argc; (void)argv;
  for (;;)
  {
    OSWaitSemaphore(&emu_go);
    emu_result = emu_fn();
    OSSignalSemaphore(&emu_done);
  }
  return 0;
}

#define EMU_STACK (1024 * 1024)

void wiiu_emu_start(int (*frame)(void))
{
  if (emu_state == 0)
  {
    int main_core = (int)OSGetCoreId(), core;
    OSThreadAttributes affinity;

    if (wiiu_jit_available() && (jit.core != main_core))
      core = jit.core;
    else
      core = (main_core == 2) ? 1 : 2;
    affinity = (core == 0) ? OS_THREAD_ATTRIB_AFFINITY_CPU0 :
               (core == 1) ? OS_THREAD_ATTRIB_AFFINITY_CPU1 : OS_THREAD_ATTRIB_AFFINITY_CPU2;

    emu_state = -1;
    emu_stack = (uint8_t *)MEMAllocFromDefaultHeapEx(EMU_STACK, 16);
    OSInitSemaphore(&emu_go, 0);
    OSInitSemaphore(&emu_done, 0);
    if (emu_stack && OSCreateThread(&emu_thread_obj, emu_worker, 0, NULL,
                                    emu_stack + EMU_STACK, EMU_STACK, 16, affinity))
    {
      OSResumeThread(&emu_thread_obj);
      emu_state = 1;
    }
    wiiu_log("emulation on core %d (main thread on core %d)%s", core, main_core,
             (emu_state == 1) ? "" : ": no thread, on the main core");
  }

  if (emu_state < 0)
  {
    emu_result = frame();
    return;
  }
  emu_fn = frame;
  OSSignalSemaphore(&emu_go);
}

int wiiu_emu_wait(void)
{
  if (emu_state == 1)
    OSWaitSemaphore(&emu_done);
  return emu_result;
}

/* why the recompiler cannot be used, for the 32X menu */
void wiiu_jit_why(char *text, int size)
{
  wiiu_jit_available();
  if (!jit.size)
    snprintf(text, size, "No JIT area for the app (core %d)", jit.core);
  else if (!jit.exec_ok)
    snprintf(text, size, "JIT area does not run code (%u KB)", (unsigned)(jit.size / 1024));
  else
    snprintf(text, size, "JIT area too small: %u KB of 4096", (unsigned)(jit.size / 1024));
}
