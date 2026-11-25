/****************************************************************************
 *  Genesis Plus GX - Wii U port
 *
 *  Sega 32X games, emulated by PicoDrive through its libretro API
 *  (picodrive/, built by picodrive/Makefile.wiiu into picodrive_core.o).
 *  This is a lean frontend for that core: the picture is converted into the
 *  bitmap of Genesis Plus GX (32bpp), the sound goes into a small FIFO that
 *  main.c sends to the audio device, the buttons come from main.c.
 ****************************************************************************/

#include <stdarg.h>
#include <strings.h>
#include "shared.h"
#include "../picodrive/platform/libretro/libretro-common/include/libretro.h"
#include "pico32x_wiiu.h"

/* the 32X BIOS files (optional: PicoDrive has its own replacement code) */
extern void *p32x_bios_g, *p32x_bios_m, *p32x_bios_s;
#define BIOS_G WIIU_APP_DIR "/32X_G_BIOS.BIN"
#define BIOS_M WIIU_APP_DIR "/32X_M_BIOS.BIN"
#define BIOS_S WIIU_APP_DIR "/32X_S_BIOS.BIN"
static uint8 bios_g[0x100], bios_m[0x800], bios_s[0x400];

int pico32x_active = 0;

static int  core_ready;
static char system_dir[256] = WIIU_APP_DIR;
static char save_dir[256]   = WIIU_APP_DIR "/saves";
static char last_error[160];
static struct retro_system_av_info av;
static int  opt_region;

#define FIFO_FRAMES 4096
static int16 fifo[FIFO_FRAMES * 2];
static int   fifo_len;

static uint32 pad_bits[2];

/****************************************************************************
 * Core options (the rest keep PicoDrive's defaults)
 ****************************************************************************/

static const char *option_value(const char *key)
{
  if (!strcmp(key, "picodrive_sound_rate")) return "48000";
  if (!strcmp(key, "picodrive_drc"))        return "disabled";  /* no recompiler on the Wii U */
  if (!strcmp(key, "picodrive_renderer"))   return "good";
  if (!strcmp(key, "picodrive_frameskip"))  return "disabled";
  if (!strcmp(key, "picodrive_aspect"))     return "PAR";
  if (!strcmp(key, "picodrive_region"))
  {
    switch (opt_region)
    {
      case 1:  return "US";
      case 2:  return "Europe";
      case 3:  return "Japan NTSC";
      case 4:  return "Japan PAL";
      default: return "Auto";
    }
  }
  return NULL;
}

/****************************************************************************
 * libretro frontend callbacks
 ****************************************************************************/

static void core_log(enum retro_log_level level, const char *fmt, ...)
{
  if (level >= RETRO_LOG_ERROR)
  {
    char *nl;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(last_error, sizeof(last_error), fmt, ap);
    va_end(ap);
    nl = strchr(last_error, '\n');
    if (nl) *nl = 0;
  }
}

static bool core_environment(unsigned cmd, void *data)
{
  switch (cmd)
  {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
      ((struct retro_log_callback *) data)->log = core_log;
      return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
      return *(const enum retro_pixel_format *) data == RETRO_PIXEL_FORMAT_RGB565;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
      *(const char **) data = system_dir;
      return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
      *(const char **) data = save_dir;
      return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE:
    {
      struct retro_variable *v = (struct retro_variable *) data;
      v->value = option_value(v->key);
      return v->value != NULL;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
      *(bool *) data = false;
      return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
      *(bool *) data = true;
      return true;
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
      return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
      *(unsigned *) data = 0;
      return true;
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
      av = *(const struct retro_system_av_info *) data;
      return true;
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
      av.geometry = *(const struct retro_game_geometry *) data;
      return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES:
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
    case RETRO_ENVIRONMENT_SET_MESSAGE:
    case RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY:
    case RETRO_ENVIRONMENT_SET_CONTENT_INFO_OVERRIDE:
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
      return true;
    default:
      return false;
  }
}

/* RGB565 from PicoDrive into the 32bpp bitmap (0xAARRGGBB) of main.c */
static void core_video(const void *data, unsigned w, unsigned h, size_t pitch)
{
  unsigned x, y;

  if (!data)
    return; /* the same picture again */

  if (w > 320) w = 320;
  if (h > 240) h = 240;

  if ((bitmap.viewport.w != (int)w) || (bitmap.viewport.h != (int)h) || bitmap.viewport.x || bitmap.viewport.y)
  {
    bitmap.viewport.w = w;
    bitmap.viewport.h = h;
    bitmap.viewport.x = 0;
    bitmap.viewport.y = 0;
    bitmap.viewport.changed |= 1;
  }

  for (y = 0; y < h; y++)
  {
    const uint16 *in = (const uint16 *)((const uint8 *)data + y * pitch);
    uint32 *out = (uint32 *)(bitmap.data + y * bitmap.pitch);
    for (x = 0; x < w; x++)
    {
      uint32 p = in[x];
      uint32 r = (p >> 11) & 0x1f, g = (p >> 5) & 0x3f, b = p & 0x1f;
      out[x] = 0xff000000 | (((r << 3) | (r >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) | ((b << 3) | (b >> 2));
    }
  }
}

static size_t core_audio_batch(const int16_t *data, size_t frames)
{
  size_t space = FIFO_FRAMES - fifo_len;
  if (frames > space)
    frames = space;
  memcpy(fifo + fifo_len * 2, data, frames * 2 * sizeof(int16));
  fifo_len += frames;
  return frames;
}

static void core_audio_sample(int16_t left, int16_t right)
{
  int16_t s[2];
  s[0] = left;
  s[1] = right;
  core_audio_batch(s, 1);
}

static void core_input_poll(void)
{
}

static int16_t core_input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
  (void) index;
  if (port > 1 || (device & RETRO_DEVICE_MASK) != RETRO_DEVICE_JOYPAD)
    return 0;
  if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
    return (int16_t) pad_bits[port];
  return (pad_bits[port] >> id) & 1;
}

/* Mega Drive buttons (input.pad bits) to PicoDrive's libretro buttons:
   retro B = B, A = C, Y = A, X = Y, L = X, R = Z, Select = Mode */
#define RB(id) (1u << RETRO_DEVICE_ID_JOYPAD_##id)
static uint32 md_to_retro(uint16 p)
{
  uint32 b = 0;
  if (p & INPUT_UP)    b |= RB(UP);
  if (p & INPUT_DOWN)  b |= RB(DOWN);
  if (p & INPUT_LEFT)  b |= RB(LEFT);
  if (p & INPUT_RIGHT) b |= RB(RIGHT);
  if (p & INPUT_A)     b |= RB(Y);
  if (p & INPUT_B)     b |= RB(B);
  if (p & INPUT_C)     b |= RB(A);
  if (p & INPUT_X)     b |= RB(L);
  if (p & INPUT_Y)     b |= RB(X);
  if (p & INPUT_Z)     b |= RB(R);
  if (p & INPUT_MODE)  b |= RB(SELECT);
  if (p & INPUT_START) b |= RB(START);
  return b;
}

/****************************************************************************
 * API for main.c
 ****************************************************************************/

static int read_bios(const char *path, uint8 *buf, size_t size)
{
  FILE *fp = fopen(path, "rb");
  size_t n;
  if (!fp) return 0;
  n = fread(buf, 1, size, fp);
  fclose(fp);
  return n == size;
}

int pico32x_is_32x_file(const char *filename)
{
  const char *dot = strrchr(filename, '.');
  return dot && !strcasecmp(dot, ".32x");
}

int pico32x_load(const char *filename, int six_buttons, int region)
{
  struct retro_game_info info;
  unsigned dev = six_buttons ? RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 1)
                             : RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 0);

  opt_region = region;
  last_error[0] = 0;

  if (!core_ready)
  {
    retro_set_environment(core_environment);
    retro_set_video_refresh(core_video);
    retro_set_audio_sample(core_audio_sample);
    retro_set_audio_sample_batch(core_audio_batch);
    retro_set_input_poll(core_input_poll);
    retro_set_input_state(core_input_state);
    retro_init();
    core_ready = 1;
  }

  p32x_bios_g = read_bios(BIOS_G, bios_g, sizeof(bios_g)) ? bios_g : NULL;
  p32x_bios_m = read_bios(BIOS_M, bios_m, sizeof(bios_m)) ? bios_m : NULL;
  p32x_bios_s = read_bios(BIOS_S, bios_s, sizeof(bios_s)) ? bios_s : NULL;

  memset(&info, 0, sizeof(info));
  info.path = filename;
  if (!retro_load_game(&info))
  {
    if (!last_error[0])
      snprintf(last_error, sizeof(last_error), "PicoDrive could not load the game");
    return 0;
  }

  retro_get_system_av_info(&av);
  retro_set_controller_port_device(0, dev);
  retro_set_controller_port_device(1, dev);

  fifo_len = 0;
  pad_bits[0] = pad_bits[1] = 0;
  pico32x_active = 1;
  return 1;
}

void pico32x_unload(void)
{
  if (!pico32x_active)
    return;
  retro_unload_game();
  pico32x_active = 0;
}

void pico32x_frame(uint16 pad1, uint16 pad2)
{
  pad_bits[0] = md_to_retro(pad1);
  pad_bits[1] = md_to_retro(pad2);
  retro_run();
}

int pico32x_audio(int16 *out, int max_frames)
{
  int n = fifo_len < max_frames ? fifo_len : max_frames;
  memcpy(out, fifo, n * 2 * sizeof(int16));
  if (n < fifo_len)
    memmove(fifo, fifo + n * 2, (fifo_len - n) * 2 * sizeof(int16));
  fifo_len -= n;
  return n;
}

void pico32x_reset(void)
{
  retro_reset();
}

int pico32x_is_pal(void)
{
  return av.timing.fps > 0 && av.timing.fps < 55.0;
}

int pico32x_state_size(void)
{
  return (int) retro_serialize_size();
}

int pico32x_state_save(uint8 *buffer, int size)
{
  return retro_serialize(buffer, size) ? size : 0;
}

int pico32x_state_load(const uint8 *buffer, int size)
{
  return retro_unserialize(buffer, size) ? 1 : 0;
}

uint8 *pico32x_sram(int *size)
{
  *size = (int) retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
  return (uint8 *) retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
}

const char *pico32x_last_error(void)
{
  return last_error;
}
