/****************************************************************************
 *  Genesis Plus GX - Wii U port (SDL2 + wut)
 *
 *  Video, sound and controllers go through SDL2 (wiiu-sdl2), so the image
 *  is shown on the TV and mirrored on the GamePad. The emulation core is the
 *  same as the other ports (core/). The platform layer is kept in this one
 *  file (video / audio / input / ROM browser) so that it can later be
 *  replaced piece by piece by a native GX2 / AX implementation.
 *
 *  Controls (GamePad, Pro Controller, Classic Controller):
 *    D-Pad / left stick : D-Pad
 *    Y / B / A          : Genesis A / B / C       (Nintendo face buttons)
 *    L / X / R          : Genesis X / Y / Z
 *    +                  : Start      -  : Mode
 *    ZL + ZR + L        : save state     ZL + ZR + R : load state
 *    ZL + ZR + +        : back to the ROM list
 *
 *  Files on the SD card:
 *    sd:/roms/                              games (.md .bin .gen .smd .sms .gg .sg .cue .chd .zip ...)
 *    sd:/wiiu/apps/genplusgx/               BIOS files (see osd.h)
 *    sd:/wiiu/apps/genplusgx/saves/         battery saves (.srm) and states (.gp0)
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

#define SOUND_FREQUENCY 48000
#define SCREEN_W 1280
#define SCREEN_H 720
#define SAVE_DIR WIIU_APP_DIR "/saves"

int log_error = 0;
int debug_on  = 0;

md_ntsc_t  *md_ntsc  = NULL;
sms_ntsc_t *sms_ntsc = NULL;

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

  srect.x = 0; srect.y = 0; srect.w = w; srect.h = h;

  /* 4:3 picture, as large as possible on a 16:9 screen */
  drect.h = SCREEN_H;
  drect.w = SCREEN_H * 4 / 3;
  drect.x = (SCREEN_W - drect.w) / 2;
  drect.y = 0;
}

static void video_present(void)
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
  SDL_RenderPresent(renderer);
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
  int samples = audio_update(soundframe);          /* stereo sample pairs */
  Uint32 bytes = samples * 2 * sizeof(int16);
  /* about 3 frames of latency: wait while the device has more queued */
  Uint32 limit = (SOUND_FREQUENCY / 20) * 2 * sizeof(int16);

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

static int hotkeys_held(SDL_GameController *p)
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
    if (player < MAX_PADS && !hotkeys_held(pads[player]))
      input.pad[i] = pad_read(pads[player]);
    player++;
  }
  return 1;
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
  SDL_Color white = { 255, 255, 255, 255 };
  SDL_SetRenderDrawColor(renderer, 0, 0, 40, 255);
  SDL_RenderClear(renderer);
  draw_text(60, 320, msg, white);
  SDL_RenderPresent(renderer);
  SDL_Delay(2000);
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
                               ".mdx", ".68k", ".cue", ".iso", ".chd", ".zip", NULL };
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

/* returns 1 with game_path set, 0 to quit */
static int rom_browser(void)
{
  static int sel = 0;
  SDL_Color white = { 255, 255, 255, 255 }, grey = { 150, 150, 170, 255 }, hi = { 255, 210, 60, 255 };
  const int rows = 16, row_h = 38;
  uint32 repeat = 0;
  int last_dir = 0;

  scan_roms();
  if (sel >= nfiles) sel = 0;

  for (;;)
  {
    SDL_Event ev;
    int dir = 0, i, top;

    while (SDL_PollEvent(&ev))
    {
      if (ev.type == SDL_QUIT) return 0;
      if (ev.type == SDL_CONTROLLERDEVICEADDED || ev.type == SDL_CONTROLLERDEVICEREMOVED) pads_open();
      if (ev.type == SDL_CONTROLLERBUTTONDOWN)
      {
        if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_B && nfiles)   /* Nintendo A */
        {
          snprintf(game_path, sizeof(game_path), "%s/%s", WIIU_ROM_DIR, files[sel]);
          snprintf(game_name, sizeof(game_name), "%s", files[sel]);
          { char *dot = strrchr(game_name, '.'); if (dot) *dot = 0; }
          return 1;
        }
        if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER)  sel -= rows;
        if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) sel += rows;
      }
    }

    /* held up/down with auto-repeat */
    if (pads[0])
    {
      uint16 p = pad_read(pads[0]);
      if (p & INPUT_UP) dir = -1; else if (p & INPUT_DOWN) dir = 1;
    }
    if (dir && (dir != last_dir || SDL_GetTicks() > repeat))
    {
      sel += dir;
      repeat = SDL_GetTicks() + (dir != last_dir ? 400 : 80);
    }
    last_dir = dir;
    if (sel < 0) sel = nfiles ? nfiles - 1 : 0;
    if (sel >= nfiles) sel = 0;

    SDL_SetRenderDrawColor(renderer, 10, 10, 40, 255);
    SDL_RenderClear(renderer);
    draw_text(60, 20, "Genesis Plus GX - Wii U", hi);

    if (!nfiles)
    {
      draw_text(60, 120, "No games found in sd:/roms", white);
    }
    else
    {
      top = sel - rows / 2;
      if (top > nfiles - rows) top = nfiles - rows;
      if (top < 0) top = 0;
      for (i = top; i < nfiles && i < top + rows; i++)
        draw_text(80, 80 + (i - top) * row_h, files[i], i == sel ? hi : white);
    }
    draw_text(60, SCREEN_H - 44, "A: play    L/R: page    HOME: exit", grey);
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

static void state_file(int save)
{
  char path[1024];
  uint8 *buf = (uint8 *)malloc(STATE_SIZE);
  if (!buf) return;
  save_path(path, sizeof(path), "gp0");
  if (save)
  {
    int size = state_save(buf);
    write_file(path, buf, size);
  }
  else if (read_file(path, buf, STATE_SIZE))
  {
    state_load(buf);
  }
  free(buf);
}

static int game_start(void)
{
  load_md_bios();

  if (!load_rom(game_path))
  {
    show_message("Could not load this game.");
    return 0;
  }

  audio_init(SOUND_FREQUENCY, 0);
  system_init();
  backup_load();
  system_reset();

  bitmap.viewport.changed = 3;
  SDL_ClearQueuedAudio(audio_dev);
  return 1;
}

/* returns 1: back to the ROM list, 0: quit */
static int game_loop(void)
{
  int combo_was = 0;

  for (;;)
  {
    SDL_Event ev;
    int combo = 0;

    while (SDL_PollEvent(&ev))
    {
      if (ev.type == SDL_QUIT) return 0;
      if (ev.type == SDL_CONTROLLERDEVICEADDED || ev.type == SDL_CONTROLLERDEVICEREMOVED) pads_open();
    }

    /* hotkeys: ZL + ZR + (L | R | +) on the first controller */
    if (hotkeys_held(pads[0]))
    {
      if (BTN(pads[0], LEFTSHOULDER))  combo = 1;
      if (BTN(pads[0], RIGHTSHOULDER)) combo = 2;
      if (BTN(pads[0], START))         combo = 3;
    }
    if (combo && !combo_was)
    {
      if (combo == 1) state_file(1);
      if (combo == 2) state_file(0);
      if (combo == 3) return 1;
    }
    combo_was = combo;

    if (system_hw == SYSTEM_MCD)
      system_frame_scd(0);
    else if ((system_hw & SYSTEM_PBC) == SYSTEM_MD)
      system_frame_gen(0);
    else
      system_frame_sms(0);

    video_present();
    audio_push();
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

  mkdir(WIIU_APP_DIR, 0777);
  mkdir(SAVE_DIR, 0777);

  error_init();
  set_config_defaults();
  system_bios = 0;

  if (!video_init()) { SDL_Quit(); return 1; }
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
    backup_save();
    audio_shutdown();
  }

  error_shutdown();
  if (font) TTF_CloseFont(font);
  TTF_Quit();
  if (audio_dev) SDL_CloseAudioDevice(audio_dev);
  SDL_Quit();
  return 0;
}
