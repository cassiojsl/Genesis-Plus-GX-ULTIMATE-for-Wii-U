/****************************************************************************
 *  Genesis Plus GX - Wii U port (SDL2 + wut)
 *
 *  Video, sound and controllers go through SDL2 (wiiu-sdl2), so the image
 *  is shown on the TV and mirrored on the GamePad. The emulation core is the
 *  same as the other ports (core/). The platform layer is kept in this one
 *  file (video / audio / input / menus) so that it can later be replaced
 *  piece by piece by a native GX2 / AX implementation.
 *
 *  Controls (GamePad, Pro Controller, Classic Controller):
 *    D-Pad / left stick : D-Pad
 *    Y / B / A          : Genesis A / B / C       (Nintendo face buttons)
 *    L / X / R          : Genesis X / Y / Z
 *    +                  : Start      -  : Mode
 *    ZL + ZR            : pause menu (states, reset, settings, game list)
 *
 *  Files on the SD card:
 *    sd:/roms/                              games
 *    sd:/wiiu/apps/genplusgx/               BIOS files (see osd.h), settings
 *    sd:/wiiu/apps/genplusgx/saves/         battery saves (.srm) and states (.gp0-.gp9)
 ****************************************************************************/

#include <SDL.h>
#include <SDL_ttf.h>
#include <dirent.h>
#include <sys/stat.h>
#include <strings.h>
#include <coreinit/memory.h>

#include "shared.h"
#include "md_ntsc.h"
#include "sms_ntsc.h"
#include "pico32x_wiiu.h"

#define SOUND_FREQUENCY 48000
#define SCREEN_W 1280
#define SCREEN_H 720
#define SAVE_DIR    WIIU_APP_DIR "/saves"
#define CONFIG_FILE WIIU_APP_DIR "/config.bin"
#define CONFIG_MAGIC 0x47505557  /* "GPUW" */
#define CONFIG_VERSION 1

int log_error = 0;
int debug_on  = 0;

md_ntsc_t  *md_ntsc  = NULL;
sms_ntsc_t *sms_ntsc = NULL;

/****************************************************************************
 * Port settings (saved with the core's t_config)
 ****************************************************************************/

enum { ASPECT_4_3, ASPECT_INTEGER, ASPECT_STRETCH, ASPECT_COUNT };
enum { PAD_AUTO, PAD_3B, PAD_6B, PAD_COUNT };

static struct
{
  uint8 aspect;
  uint8 smooth;
  uint8 slot;
  uint8 padtype;
  uint8 show_fps;
} port = { ASPECT_4_3, 0, 0, PAD_AUTO, 0 };

typedef struct
{
  uint32 magic;
  uint32 version;
  uint32 config_size;
  t_config config;
  uint8 aspect, smooth, slot, padtype, show_fps;
} t_config_file;

/****************************************************************************
 * Globals
 ****************************************************************************/

static SDL_Window   *window;
static SDL_Renderer *renderer;
static SDL_Texture  *texture;
static SDL_AudioDeviceID audio_dev;
static TTF_Font     *font;
static uint32       *pixels;      /* the core's bitmap: 720x576, 32bpp 0xAARRGGBB */
static SDL_Rect      srect, drect;

#define MAX_PADS 4
static SDL_GameController *pads[MAX_PADS];

static int16 soundframe[SOUND_FREQUENCY / 10 * 2];
static char  game_path[1024];
static char  game_name[256];

static char   osd_text[128];
static uint32 osd_until;

static uint32 fps_count, fps_value, fps_time;

static const SDL_Color COL_WHITE = { 255, 255, 255, 255 };
static const SDL_Color COL_GREY  = { 150, 150, 170, 255 };
static const SDL_Color COL_HI    = { 255, 210,  60, 255 };

static uint8 brm_format[0x40] =
{
  0x5f,0x5f,0x5f,0x5f,0x5f,0x5f,0x5f,0x5f,0x5f,0x5f,0x5f,0x00,0x00,0x00,0x00,0x40,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x53,0x45,0x47,0x41,0x5f,0x43,0x44,0x5f,0x52,0x4f,0x4d,0x00,0x01,0x00,0x00,0x00,
  0x52,0x41,0x4d,0x5f,0x43,0x41,0x52,0x54,0x52,0x49,0x44,0x47,0x45,0x5f,0x5f,0x5f
};

/****************************************************************************
 * Helpers
 ****************************************************************************/

static void save_path(char *out, size_t len, const char *ext)
{
  snprintf(out, len, "%s/%s.%s", SAVE_DIR, game_name, ext);
}

static int read_file(const char *path, void *buf, size_t size)
{
  FILE *fp = fopen(path, "rb");
  if (!fp) return 0;
  size = fread(buf, 1, size, fp);
  fclose(fp);
  return (int)size;
}

static int write_file(const char *path, const void *buf, size_t size)
{
  FILE *fp = fopen(path, "wb");
  if (!fp) return 0;
  size = fwrite(buf, 1, size, fp);
  fclose(fp);
  return (int)size;
}

static int file_exists(const char *path)
{
  struct stat st;
  return stat(path, &st) == 0;
}

/* creates every missing folder of a path */
static void mkdir_p(const char *path)
{
  char tmp[512], *p;
  snprintf(tmp, sizeof(tmp), "%s", path);
  /* skip the device part ("fs:/vol/external01") */
  p = strstr(tmp, "external01");
  p = p ? p + strlen("external01") : tmp + 1;
  for (; *p; p++)
  {
    if (*p == '/')
    {
      *p = 0;
      mkdir(tmp, 0777);
      *p = '/';
    }
  }
  mkdir(tmp, 0777);
}

static void osd_message(const char *msg)
{
  snprintf(osd_text, sizeof(osd_text), "%s", msg);
  osd_until = SDL_GetTicks() + 2000;
}

/****************************************************************************
 * Settings file
 ****************************************************************************/

static void apply_padtype(void)
{
  int i;
  uint8 type = DEVICE_PAD2B | DEVICE_PAD3B | DEVICE_PAD6B;
  if (port.padtype == PAD_3B) type = DEVICE_PAD3B;
  if (port.padtype == PAD_6B) type = DEVICE_PAD6B;
  for (i = 0; i < MAX_INPUTS; i++)
    config.input[i].padtype = type;
}

static void config_load(void)
{
  t_config_file *f = (t_config_file *)malloc(sizeof(t_config_file));
  if (!f) return;
  if (read_file(CONFIG_FILE, f, sizeof(*f)) == sizeof(*f) &&
      f->magic == CONFIG_MAGIC && f->version == CONFIG_VERSION && f->config_size == sizeof(t_config))
  {
    memcpy(&config, &f->config, sizeof(t_config));
    port.aspect   = f->aspect  < ASPECT_COUNT ? f->aspect : ASPECT_4_3;
    port.smooth   = f->smooth ? 1 : 0;
    port.slot     = f->slot < 10 ? f->slot : 0;
    port.padtype  = f->padtype < PAD_COUNT ? f->padtype : PAD_AUTO;
    port.show_fps = f->show_fps ? 1 : 0;
  }
  free(f);
}

static void config_save(void)
{
  t_config_file *f = (t_config_file *)calloc(1, sizeof(t_config_file));
  if (!f) return;
  f->magic       = CONFIG_MAGIC;
  f->version     = CONFIG_VERSION;
  f->config_size = sizeof(t_config);
  memcpy(&f->config, &config, sizeof(t_config));
  f->aspect   = port.aspect;
  f->smooth   = port.smooth;
  f->slot     = port.slot;
  f->padtype  = port.padtype;
  f->show_fps = port.show_fps;
  write_file(CONFIG_FILE, f, sizeof(*f));
  free(f);
}

/****************************************************************************
 * Video
 ****************************************************************************/

static int video_init(void)
{
  window = SDL_CreateWindow("Genesis Plus GX", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                            SCREEN_W, SCREEN_H, 0);
  if (!window) return 0;
  renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  if (!renderer) renderer = SDL_CreateRenderer(window, -1, 0);
  if (!renderer) return 0;
  texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 720, 576);
  if (!texture) return 0;
  pixels = (uint32 *)calloc(720 * 576, sizeof(uint32));
  return pixels != NULL;
}

static void video_update_rects(void)
{
  int w = bitmap.viewport.w + 2 * bitmap.viewport.x;
  int h = bitmap.viewport.h + 2 * bitmap.viewport.y;

  if (w <= 0 || h <= 0) return;
  srect.x = 0; srect.y = 0; srect.w = w; srect.h = h;

  switch (port.aspect)
  {
    case ASPECT_INTEGER:
    {
      /* sharpest picture: whole multiples of the original pixels */
      int sh = SCREEN_H / h, sw = SCREEN_W / w;
      int s = sh < sw ? sh : sw;
      if (s < 1) s = 1;
      drect.w = w * s;
      drect.h = h * s;
      break;
    }
    case ASPECT_STRETCH:
      drect.w = SCREEN_W;
      drect.h = SCREEN_H;
      break;
    default:
      drect.h = SCREEN_H;
      drect.w = SCREEN_H * 4 / 3;
      break;
  }
  drect.x = (SCREEN_W - drect.w) / 2;
  drect.y = (SCREEN_H - drect.h) / 2;
}

static void apply_video_settings(void)
{
  SDL_SetTextureScaleMode(texture, port.smooth ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
  video_update_rects();
}

static void apply_overscan(void)
{
  if ((system_hw == SYSTEM_GG) && !config.gg_extra)
    bitmap.viewport.x = (config.overscan & 2) ? 14 : -48;
  else
    bitmap.viewport.x = (config.overscan & 2) * 7;
  bitmap.viewport.changed = 3;
}

/* forward */
static void draw_text(int x, int y, const char *text, SDL_Color c);

static void video_draw_game(void)
{
  if (bitmap.viewport.changed & 1)
  {
    bitmap.viewport.changed &= ~1;
    video_update_rects();
  }
  SDL_UpdateTexture(texture, &srect, pixels, bitmap.pitch);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderClear(renderer);
  SDL_RenderCopy(renderer, texture, &srect, &drect);
}

static void video_present(void)
{
  video_draw_game();

  if (osd_text[0] && SDL_GetTicks() < osd_until)
    draw_text(30, SCREEN_H - 60, osd_text, COL_HI);

  if (port.show_fps)
  {
    char buf[16];
    snprintf(buf, sizeof(buf), "%u fps", (unsigned)fps_value);
    draw_text(SCREEN_W - 140, 10, buf, COL_HI);
  }

  SDL_RenderPresent(renderer);

  fps_count++;
  if (SDL_GetTicks() - fps_time >= 1000)
  {
    fps_value = fps_count;
    fps_count = 0;
    fps_time  = SDL_GetTicks();
  }
}

/****************************************************************************
 * Audio: queued, also used to keep the emulation at the right speed
 ****************************************************************************/

static int audio_open(void)
{
  SDL_AudioSpec want, have;
  SDL_zero(want);
  want.freq     = SOUND_FREQUENCY;
  want.format   = AUDIO_S16SYS;
  want.channels = 2;
  want.samples  = 1024;
  audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
  if (!audio_dev) return 0;
  SDL_PauseAudioDevice(audio_dev, 0);
  return 1;
}

static void audio_push(void)
{
  /* stereo sample pairs of the frame */
  int samples = pico32x_active ? pico32x_audio(soundframe, sizeof(soundframe) / 4)
                               : audio_update(soundframe);
  Uint32 bytes = samples * 2 * sizeof(int16);
  /* about 3 frames of latency: wait while the device has more queued */
  Uint32 limit = (SOUND_FREQUENCY / 20) * 2 * sizeof(int16);

  if (!audio_dev)
  {
    SDL_Delay((pico32x_active ? pico32x_is_pal() : vdp_pal) ? 20 : 16);
    return;
  }

  while (SDL_GetQueuedAudioSize(audio_dev) > limit)
    SDL_Delay(1);

  SDL_QueueAudio(audio_dev, soundframe, bytes);
}

/****************************************************************************
 * Input
 ****************************************************************************/

static void pads_open(void)
{
  int i, n = 0;
  for (i = 0; i < MAX_PADS; i++)
  {
    if (pads[i]) { SDL_GameControllerClose(pads[i]); pads[i] = NULL; }
  }
  for (i = 0; i < SDL_NumJoysticks() && n < MAX_PADS; i++)
  {
    if (SDL_IsGameController(i))
      pads[n++] = SDL_GameControllerOpen(i);
  }
}

#define BTN(p,b)  SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_##b)
#define AXIS(p,a) SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_##a)

static uint16 pad_read(SDL_GameController *p)
{
  uint16 pad = 0;
  int x, y;

  if (!p) return 0;

  x = AXIS(p, LEFTX);
  y = AXIS(p, LEFTY);

  if (BTN(p, DPAD_UP)    || y < -16000) pad |= INPUT_UP;
  if (BTN(p, DPAD_DOWN)  || y >  16000) pad |= INPUT_DOWN;
  if (BTN(p, DPAD_LEFT)  || x < -16000) pad |= INPUT_LEFT;
  if (BTN(p, DPAD_RIGHT) || x >  16000) pad |= INPUT_RIGHT;

  /* SDL names the buttons by position (south = A): on Nintendo pads
     south is B, west is Y, east is A, north is X. */
  if (BTN(p, X))             pad |= INPUT_A;     /* west  (Y) */
  if (BTN(p, A))             pad |= INPUT_B;     /* south (B) */
  if (BTN(p, B))             pad |= INPUT_C;     /* east  (A) */
  if (BTN(p, LEFTSHOULDER))  pad |= INPUT_X;
  if (BTN(p, Y))             pad |= INPUT_Y;     /* north (X) */
  if (BTN(p, RIGHTSHOULDER)) pad |= INPUT_Z;
  if (BTN(p, START))         pad |= INPUT_START;
  if (BTN(p, BACK))          pad |= INPUT_MODE;

  return pad;
}

static int menu_combo(SDL_GameController *p)
{
  return p && AXIS(p, TRIGGERLEFT) > 16000 && AXIS(p, TRIGGERRIGHT) > 16000;
}

int wiiu_input_update(void)
{
  int i, player = 0;

  for (i = 0; i < MAX_DEVICES; i++)
  {
    input.pad[i] = 0;
    if (input.dev[i] == NO_DEVICE) continue;
    if (player < MAX_PADS)
      input.pad[i] = pad_read(pads[player]);
    player++;
  }
  return 1;
}

/* menu navigation, from any controller */
enum { NAV_NONE, NAV_UP, NAV_DOWN, NAV_LEFT, NAV_RIGHT, NAV_OK, NAV_BACK, NAV_PGUP, NAV_PGDN, NAV_QUIT };

static int nav_poll(void)
{
  static int held_dir = 0;
  static uint32 repeat_at = 0;
  SDL_Event ev;
  int i, dir = 0;

  while (SDL_PollEvent(&ev))
  {
    if (ev.type == SDL_QUIT) return NAV_QUIT;
    if (ev.type == SDL_CONTROLLERDEVICEADDED || ev.type == SDL_CONTROLLERDEVICEREMOVED) pads_open();
    if (ev.type == SDL_CONTROLLERBUTTONDOWN)
    {
      switch (ev.cbutton.button)
      {
        case SDL_CONTROLLER_BUTTON_B:             return NAV_OK;    /* Nintendo A */
        case SDL_CONTROLLER_BUTTON_A:             return NAV_BACK;  /* Nintendo B */
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  return NAV_PGUP;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return NAV_PGDN;
      }
    }
  }

  /* d-pad / stick, held with auto-repeat */
  for (i = 0; i < MAX_PADS && !dir; i++)
  {
    uint16 p = pad_read(pads[i]);
    if (p & INPUT_UP) dir = NAV_UP;
    else if (p & INPUT_DOWN) dir = NAV_DOWN;
    else if (p & INPUT_LEFT) dir = NAV_LEFT;
    else if (p & INPUT_RIGHT) dir = NAV_RIGHT;
  }
  if (!dir) { held_dir = 0; return NAV_NONE; }
  if (dir != held_dir)
  {
    held_dir = dir;
    repeat_at = SDL_GetTicks() + 400;
    return dir;
  }
  if (SDL_GetTicks() >= repeat_at)
  {
    repeat_at = SDL_GetTicks() + 80;
    return dir;
  }
  return NAV_NONE;
}

/* wait until no controller holds a button, so a menu press does not reach the game */
static void wait_release(void)
{
  int i, held;
  do
  {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {}
    held = 0;
    for (i = 0; i < MAX_PADS; i++)
      if (pad_read(pads[i]) || menu_combo(pads[i])) held = 1;
    SDL_Delay(10);
  } while (held);
}

/****************************************************************************
 * Text (Wii U system font, no files needed)
 ****************************************************************************/

static int font_init(void)
{
  void *data = NULL;
  uint32_t size = 0;

  if (TTF_Init() < 0) return 0;
  if (!OSGetSharedData(OS_SHAREDDATATYPE_FONT_STANDARD, 0, &data, &size) || !data)
    return 0;
  font = TTF_OpenFontRW(SDL_RWFromConstMem(data, size), 1, 28);
  return font != NULL;
}

static void draw_text(int x, int y, const char *text, SDL_Color c)
{
  SDL_Surface *s;
  SDL_Texture *t;
  SDL_Rect r;

  if (!font || !text[0]) return;
  s = TTF_RenderUTF8_Blended(font, text, c);
  if (!s) return;
  t = SDL_CreateTextureFromSurface(renderer, s);
  r.x = x; r.y = y; r.w = s->w; r.h = s->h;
  SDL_RenderCopy(renderer, t, NULL, &r);
  SDL_DestroyTexture(t);
  SDL_FreeSurface(s);
}

static void show_message(const char *msg)
{
  SDL_SetRenderDrawColor(renderer, 0, 0, 40, 255);
  SDL_RenderClear(renderer);
  draw_text(60, 320, msg, COL_WHITE);
  SDL_RenderPresent(renderer);
  SDL_Delay(2000);
}

/****************************************************************************
 * Generic list menu
 ****************************************************************************/

typedef struct
{
  const char *label;
  char value[64];        /* shown right of the label, empty for actions */
} t_menu_item;

/* draws the paused game (dimmed) or a plain background, then the items */
static void menu_draw(const char *title, t_menu_item *items, int n, int sel, int over_game, const char *help)
{
  SDL_Rect box;
  int i, y0 = 140, row_h = 46;

  if (over_game)
  {
    SDL_RenderCopy(renderer, texture, &srect, &drect);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 20, 200);
    box.x = 0; box.y = 0; box.w = SCREEN_W; box.h = SCREEN_H;
    SDL_RenderFillRect(renderer, &box);
  }
  else
  {
    SDL_SetRenderDrawColor(renderer, 10, 10, 40, 255);
    SDL_RenderClear(renderer);
  }

  draw_text(200, 60, title, COL_HI);
  for (i = 0; i < n; i++)
  {
    SDL_Color c = (i == sel) ? COL_HI : COL_WHITE;
    if (i == sel) draw_text(170, y0 + i * row_h, ">", COL_HI);
    draw_text(200, y0 + i * row_h, items[i].label, c);
    if (items[i].value[0])
    {
      char buf[80];
      snprintf(buf, sizeof(buf), "<  %s  >", items[i].value);
      draw_text(700, y0 + i * row_h, buf, c);
    }
  }
  if (help) draw_text(200, SCREEN_H - 60, help, COL_GREY);
  SDL_RenderPresent(renderer);
}

/****************************************************************************
 * ROM browser
 ****************************************************************************/

#define MAX_FILES 2048
static char *files[MAX_FILES];
static int   nfiles;

static int has_rom_ext(const char *name)
{
  static const char *ext[] = { ".md", ".bin", ".gen", ".smd", ".sms", ".gg", ".sg", ".sc",
                               ".mdx", ".68k", ".32x", ".cue", ".iso", ".chd", ".zip", NULL };
  const char *dot = strrchr(name, '.');
  int i;
  if (!dot) return 0;
  for (i = 0; ext[i]; i++)
    if (!strcasecmp(dot, ext[i])) return 1;
  return 0;
}

static int cmp_names(const void *a, const void *b)
{
  return strcasecmp(*(char * const *)a, *(char * const *)b);
}

static void scan_roms(void)
{
  DIR *dir;
  struct dirent *de;

  while (nfiles) free(files[--nfiles]);
  dir = opendir(WIIU_ROM_DIR);
  if (!dir) return;
  while ((de = readdir(dir)) && nfiles < MAX_FILES)
  {
    if (de->d_name[0] == '.' || !has_rom_ext(de->d_name)) continue;
    files[nfiles++] = strdup(de->d_name);
  }
  closedir(dir);
  qsort(files, nfiles, sizeof(char *), cmp_names);
}

static int settings_menu(int in_game);

/* returns 1 with game_path set, 0 to quit */
static int rom_browser(void)
{
  static int sel = 0;
  const int rows = 14, row_h = 40;

  scan_roms();
  if (sel >= nfiles) sel = 0;
  wait_release();

  for (;;)
  {
    int nav = nav_poll(), i, top;

    switch (nav)
    {
      case NAV_QUIT: return 0;
      case NAV_UP:   sel--; break;
      case NAV_DOWN: sel++; break;
      case NAV_PGUP: case NAV_LEFT:  sel -= rows; if (sel < 0) sel = 0; break;
      case NAV_PGDN: case NAV_RIGHT: sel += rows; if (sel >= nfiles) sel = nfiles - 1; break;
      case NAV_OK:
        if (nfiles)
        {
          char *dot;
          snprintf(game_path, sizeof(game_path), "%s/%s", WIIU_ROM_DIR, files[sel]);
          snprintf(game_name, sizeof(game_name), "%s", files[sel]);
          dot = strrchr(game_name, '.');
          if (dot) *dot = 0;
          return 1;
        }
        break;
    }
    /* + : settings */
    for (i = 0; i < MAX_PADS; i++)
    {
      if (pads[i] && BTN(pads[i], START))
      {
        if (!settings_menu(0)) return 0;
        wait_release();
        break;
      }
    }
    if (sel < 0) sel = nfiles ? nfiles - 1 : 0;
    if (sel >= nfiles) sel = 0;

    SDL_SetRenderDrawColor(renderer, 10, 10, 40, 255);
    SDL_RenderClear(renderer);
    draw_text(60, 20, "Genesis Plus GX - Wii U", COL_HI);

    if (!nfiles)
    {
      draw_text(60, 120, "Nenhum jogo encontrado em sd:/roms", COL_WHITE);
    }
    else
    {
      char count[32];
      snprintf(count, sizeof(count), "%d / %d", sel + 1, nfiles);
      draw_text(SCREEN_W - 200, 20, count, COL_GREY);
      top = sel - rows / 2;
      if (top > nfiles - rows) top = nfiles - rows;
      if (top < 0) top = 0;
      for (i = top; i < nfiles && i < top + rows; i++)
        draw_text(80, 80 + (i - top) * row_h, files[i], i == sel ? COL_HI : COL_WHITE);
    }
    draw_text(60, SCREEN_H - 44, "A: jogar    L/R: pagina    +: configuracoes    HOME: sair", COL_GREY);
    SDL_RenderPresent(renderer);
    SDL_Delay(16);
  }
}

/****************************************************************************
 * Game load / save
 ****************************************************************************/

static void load_md_bios(void)
{
  int i;
  memset(boot_rom, 0xFF, sizeof(boot_rom));
  system_bios &= ~SYSTEM_MD;
  if (read_file(MD_BIOS, boot_rom, 0x800) && !memcmp((char *)(boot_rom + 0x120), "GENESIS OS", 10))
  {
    system_bios |= SYSTEM_MD;
#ifdef LSB_FIRST
    for (i = 0; i < 0x800; i += 2)
    {
      uint8 t = boot_rom[i]; boot_rom[i] = boot_rom[i + 1]; boot_rom[i + 1] = t;
    }
#endif
    for (i = 0x800; i < 0x10000; i++)
      boot_rom[i] = boot_rom[i & 0x7ff];
  }
}

static void format_brm(uint8 *area, int size, int internal)
{
  memset(area, 0x00, internal ? 0x200 : size);
  if (internal)
  {
    brm_format[0x10] = brm_format[0x12] = brm_format[0x14] = brm_format[0x16] = 0x00;
    brm_format[0x11] = brm_format[0x13] = brm_format[0x15] = brm_format[0x17] = (sizeof(scd.bram) / 64) - 3;
  }
  else
  {
    brm_format[0x10] = brm_format[0x12] = brm_format[0x14] = brm_format[0x16] = ((size / 64) - 3) >> 8;
    brm_format[0x11] = brm_format[0x13] = brm_format[0x15] = brm_format[0x17] = ((size / 64) - 3) & 0xff;
  }
  memcpy(area + size - 0x40, brm_format, 0x40);
}

static void backup_load(void)
{
  char path[1024];

  if (pico32x_active)
  {
    int size;
    uint8 *data = pico32x_sram(&size);
    if (data && size > 0)
    {
      save_path(path, sizeof(path), "srm");
      read_file(path, data, size);
    }
    return;
  }

  if (system_hw == SYSTEM_MCD)
  {
    read_file(SAVE_DIR "/scd.brm", scd.bram, 0x2000);
    if (memcmp(scd.bram + 0x2000 - 0x20, brm_format + 0x20, 0x20))
      format_brm(scd.bram, 0x2000, 1);

    if (scd.cartridge.id)
    {
      int size = scd.cartridge.mask + 1;
      read_file(SAVE_DIR "/cart.brm", scd.cartridge.area, size);
      if (memcmp(scd.cartridge.area + size - 0x20, brm_format + 0x20, 0x20))
        format_brm(scd.cartridge.area, size, 0);
    }
  }

  if (sram.on)
  {
    save_path(path, sizeof(path), "srm");
    read_file(path, sram.sram, 0x10000);
  }
}

static void backup_save(void)
{
  char path[1024];

  if (pico32x_active)
  {
    int size;
    uint8 *data = pico32x_sram(&size);
    if (data && size > 0)
    {
      save_path(path, sizeof(path), "srm");
      write_file(path, data, size);
    }
    return;
  }

  if (system_hw == SYSTEM_MCD)
  {
    if (!memcmp(scd.bram + 0x2000 - 0x20, brm_format + 0x20, 0x20))
      write_file(SAVE_DIR "/scd.brm", scd.bram, 0x2000);
    if (scd.cartridge.id)
    {
      int size = scd.cartridge.mask + 1;
      if (!memcmp(scd.cartridge.area + size - 0x20, brm_format + 0x20, 0x20))
        write_file(SAVE_DIR "/cart.brm", scd.cartridge.area, size);
    }
  }

  if (sram.on)
  {
    save_path(path, sizeof(path), "srm");
    write_file(path, sram.sram, 0x10000);
  }
}

static void state_path(char *out, size_t len, int slot)
{
  char ext[8];
  snprintf(ext, sizeof(ext), "gp%d", slot);
  save_path(out, len, ext);
}

static int state_file(int save)
{
  char path[1024];
  int ok = 0;
  int max = pico32x_active ? pico32x_state_size() : STATE_SIZE;
  uint8 *buf = (uint8 *)malloc(max);
  if (!buf) return 0;
  state_path(path, sizeof(path), port.slot);
  if (pico32x_active)
  {
    if (save)
    {
      int size = pico32x_state_save(buf, max);
      ok = size > 0 && write_file(path, buf, size) == size;
    }
    else if (read_file(path, buf, max) == max)
    {
      ok = pico32x_state_load(buf, max);
    }
  }
  else if (save)
  {
    int size = state_save(buf);
    ok = size > 0 && write_file(path, buf, size) == size;
  }
  else if (read_file(path, buf, STATE_SIZE) > 0)
  {
    ok = state_load(buf) > 0;
  }
  free(buf);
  return ok;
}

static int game_start(void)
{
  load_md_bios();
  apply_padtype();

  if (pico32x_is_32x_file(game_path))
  {
    if (!pico32x_load(game_path, port.padtype != PAD_3B, config.region_detect))
    {
      char msg[256];
      snprintf(msg, sizeof(msg), "Nao foi possivel abrir este jogo de 32X. %s", pico32x_last_error());
      show_message(msg);
      return 0;
    }
    backup_load();
    bitmap.viewport.changed = 3;
    if (audio_dev) SDL_ClearQueuedAudio(audio_dev);
    return 1;
  }

  if (!load_rom(game_path))
  {
    show_message("Nao foi possivel abrir este jogo.");
    return 0;
  }

  audio_init(SOUND_FREQUENCY, 0);
  system_init();
  backup_load();
  system_reset();
  apply_overscan();

  bitmap.viewport.changed = 3;
  if (audio_dev) SDL_ClearQueuedAudio(audio_dev);
  return 1;
}

/* saves the battery RAM and closes the running game */
static void game_stop(void)
{
  backup_save();
  if (pico32x_active)
    pico32x_unload();
  else
    audio_shutdown();
}

/****************************************************************************
 * Settings menu
 ****************************************************************************/

static const char *region_names[] = { "Automatica", "EUA", "Europa", "Japao (NTSC)", "Japao (PAL)" };
static const char *aspect_names[] = { "4:3", "Pixels inteiros", "Tela cheia 16:9" };
static const char *pad_names[]    = { "Automatico", "3 botoes", "6 botoes" };
static const char *overscan_names[] = { "Sem bordas", "Verticais", "Horizontais", "Todas" };

enum { S_REGION, S_PAD, S_FM, S_ASPECT, S_SMOOTH, S_OVERSCAN, S_FPS, S_BACK, S_COUNT };

/* returns 0 to quit the app. Settings that the core reads when a game is
   loaded (region, controller, FM chip) reload the running game. */
static int settings_menu(int in_game)
{
  t_menu_item items[S_COUNT];
  int sel = 0, done = 0, reload = 0;

  memset(items, 0, sizeof(items));
  wait_release();

  while (!done)
  {
    int nav, d = 0;

    items[S_REGION].label   = "Regiao";
    snprintf(items[S_REGION].value, 64, "%s", region_names[config.region_detect % 5]);
    items[S_PAD].label      = "Controle";
    snprintf(items[S_PAD].value, 64, "%s", pad_names[port.padtype]);
    items[S_FM].label       = "Som FM";
    snprintf(items[S_FM].value, 64, "%s", config.ym3438 ? "YM3438 (preciso)" : "YM2612 (rapido)");
    items[S_ASPECT].label   = "Proporcao";
    snprintf(items[S_ASPECT].value, 64, "%s", aspect_names[port.aspect]);
    items[S_SMOOTH].label   = "Suavizar imagem";
    snprintf(items[S_SMOOTH].value, 64, "%s", port.smooth ? "Sim" : "Nao");
    items[S_OVERSCAN].label = "Bordas (overscan)";
    snprintf(items[S_OVERSCAN].value, 64, "%s", overscan_names[config.overscan & 3]);
    items[S_FPS].label      = "Mostrar FPS";
    snprintf(items[S_FPS].value, 64, "%s", port.show_fps ? "Sim" : "Nao");
    items[S_BACK].label     = "Voltar";

    menu_draw("Configuracoes", items, S_COUNT, sel, in_game,
              in_game ? "Regiao, controle e som FM reiniciam o jogo" : "B: voltar");

    nav = nav_poll();
    if (nav == NAV_QUIT) return 0;
    if (nav == NAV_UP)   sel = (sel + S_COUNT - 1) % S_COUNT;
    if (nav == NAV_DOWN) sel = (sel + 1) % S_COUNT;
    if (nav == NAV_BACK) done = 1;
    if (nav == NAV_LEFT) d = -1;
    if (nav == NAV_RIGHT || nav == NAV_OK) d = 1;
    if (nav == NAV_OK && sel == S_BACK) { done = 1; d = 0; }

    if (d)
    {
      switch (sel)
      {
        case S_REGION:   config.region_detect = (config.region_detect + 5 + d) % 5; reload = 1; break;
        case S_PAD:      port.padtype = (port.padtype + PAD_COUNT + d) % PAD_COUNT; reload = 1; break;
        case S_FM:       config.ym3438 = !config.ym3438; reload = 1; break;
        case S_ASPECT:   port.aspect = (port.aspect + ASPECT_COUNT + d) % ASPECT_COUNT; apply_video_settings(); break;
        case S_SMOOTH:   port.smooth = !port.smooth; apply_video_settings(); break;
        case S_OVERSCAN: config.overscan = (config.overscan + 4 + d) & 3; if (in_game && !pico32x_active) apply_overscan(); break;
        case S_FPS:      port.show_fps = !port.show_fps; break;
      }
    }
    SDL_Delay(16);
  }

  config_save();

  if (in_game && reload)
  {
    game_stop();
    game_start();
    osd_message("Jogo reiniciado com as novas configuracoes");
  }
  wait_release();
  return 1;
}

/****************************************************************************
 * Pause menu
 ****************************************************************************/

enum { P_RESUME, P_SAVE, P_LOAD, P_SLOT, P_RESET, P_SETTINGS, P_EXIT, P_COUNT };
enum { PAUSE_RESUME, PAUSE_TO_LIST, PAUSE_QUIT };

static int pause_menu(void)
{
  t_menu_item items[P_COUNT];
  int sel = 0;

  memset(items, 0, sizeof(items));
  if (audio_dev) SDL_PauseAudioDevice(audio_dev, 1);
  wait_release();

  for (;;)
  {
    char path[1024];
    int nav;

    state_path(path, sizeof(path), port.slot);
    items[P_RESUME].label   = "Continuar";
    items[P_SAVE].label     = "Salvar estado";
    items[P_LOAD].label     = file_exists(path) ? "Carregar estado" : "Carregar estado (vazio)";
    items[P_SLOT].label     = "Slot";
    snprintf(items[P_SLOT].value, 64, "%d", port.slot);
    items[P_RESET].label    = "Reiniciar jogo";
    items[P_SETTINGS].label = "Configuracoes";
    items[P_EXIT].label     = "Voltar a lista de jogos";

    menu_draw(game_name, items, P_COUNT, sel, 1, "A: escolher    B: continuar");

    nav = nav_poll();
    if (nav == NAV_QUIT) return PAUSE_QUIT;
    if (nav == NAV_UP)   sel = (sel + P_COUNT - 1) % P_COUNT;
    if (nav == NAV_DOWN) sel = (sel + 1) % P_COUNT;
    if (sel == P_SLOT && (nav == NAV_LEFT || nav == NAV_RIGHT))
      port.slot = (port.slot + 10 + (nav == NAV_LEFT ? -1 : 1)) % 10;
    if (nav == NAV_BACK) break;
    if (nav == NAV_OK)
    {
      if (sel == P_RESUME) break;
      if (sel == P_SAVE)
      {
        osd_message(state_file(1) ? "Estado salvo" : "Erro ao salvar o estado");
        config_save();
        break;
      }
      if (sel == P_LOAD)
      {
        osd_message(state_file(0) ? "Estado carregado" : "Nenhum estado neste slot");
        break;
      }
      if (sel == P_SLOT) port.slot = (port.slot + 1) % 10;
      if (sel == P_RESET)
      {
        if (pico32x_active) pico32x_reset(); else system_reset();
        osd_message("Jogo reiniciado");
        break;
      }
      if (sel == P_SETTINGS)
      {
        if (!settings_menu(1)) return PAUSE_QUIT;
        if (audio_dev) SDL_PauseAudioDevice(audio_dev, 1);
      }
      if (sel == P_EXIT) return PAUSE_TO_LIST;
    }
    SDL_Delay(16);
  }

  wait_release();
  if (audio_dev)
  {
    SDL_ClearQueuedAudio(audio_dev);
    SDL_PauseAudioDevice(audio_dev, 0);
  }
  return PAUSE_RESUME;
}

/****************************************************************************
 * Game loop
 ****************************************************************************/

/* returns 1: back to the ROM list, 0: quit */
static int game_loop(void)
{
  uint32 last_autosave = SDL_GetTicks();

  for (;;)
  {
    SDL_Event ev;
    int i;

    while (SDL_PollEvent(&ev))
    {
      if (ev.type == SDL_QUIT) return 0;
      if (ev.type == SDL_CONTROLLERDEVICEADDED || ev.type == SDL_CONTROLLERDEVICEREMOVED) pads_open();
    }

    for (i = 0; i < MAX_PADS; i++)
    {
      if (menu_combo(pads[i]))
      {
        int r = pause_menu();
        if (r == PAUSE_QUIT)    return 0;
        if (r == PAUSE_TO_LIST) return 1;
        break;
      }
    }

    if (pico32x_active)
      pico32x_frame(pad_read(pads[0]), pad_read(pads[1]));
    else if (system_hw == SYSTEM_MCD)
      system_frame_scd(0);
    else if ((system_hw & SYSTEM_PBC) == SYSTEM_MD)
      system_frame_gen(0);
    else
      system_frame_sms(0);

    video_present();
    audio_push();

    /* battery saves are also written every minute, in case the console is turned off */
    if (SDL_GetTicks() - last_autosave > 60000)
    {
      backup_save();
      last_autosave = SDL_GetTicks();
    }
  }
}

/****************************************************************************
 * Main
 ****************************************************************************/

int main(int argc, char **argv)
{
  int running = 1;

  (void)argc; (void)argv;

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0)
    return 1;

  mkdir_p(SAVE_DIR);

  error_init();
  set_config_defaults();
  config_load();
  system_bios = 0;

  if (!video_init()) { SDL_Quit(); return 1; }
  apply_video_settings();
  audio_open();
  font_init();
  pads_open();

  memset(&bitmap, 0, sizeof(t_bitmap));
  bitmap.width  = 720;
  bitmap.height = 576;
  bitmap.pitch  = 720 * 4;
  bitmap.data   = (uint8 *)pixels;
  bitmap.viewport.changed = 3;

  while (running && rom_browser())
  {
    if (!game_start()) continue;
    running = game_loop();
    game_stop();
  }

  config_save();
  error_shutdown();
  if (font) TTF_CloseFont(font);
  TTF_Quit();
  if (audio_dev) SDL_CloseAudioDevice(audio_dev);
  SDL_Quit();
  return 0;
}
