/****************************************************************************
 *  gx_video.c
 *
 *  Genesis Plus GX video & rendering support
 *
 *  Copyright Eke-Eke (2007-2026), based on original work from Softdev (2006)
 *
 *  Modified 2026 by VrepliroidV: auto crop of black borders (config.autocrop),
 *  see gx/utils/autocrop.h. The game quad is drawn with immediate texture
 *  coordinates so that only the visible part of the frame is shown. With the
 *  FPS counter, the automatic 68k overclock in use (main and Mega CD sub 68k)
 *  is shown under it.
 *
 *  Redistribution and use of this code or any derivative works are permitted
 *  provided that the following conditions are met:
 *
 *   - Redistributions may not be sold, nor may they be used in a commercial
 *     product or activity.
 *
 *   - Redistributions that are modified from the original source must include the
 *     complete source code, including the source code for all components used by a
 *     binary built from the modified sources. However, as a special exception, the
 *     source code distributed need not include anything that is normally distributed
 *     (in either source or binary form) with the major components (compiler, kernel,
 *     and so on) of the operating system on which the executable runs, unless that
 *     component itself accompanies the executable.
 *
 *   - Redistributions must reproduce the above copyright notice, this list of
 *     conditions and the following disclaimer in the documentation and/or other
 *     materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************************/

/****************************************************************************
 *  Genesis Plus GX ULTIMATE - Wii U port
 *
 *  gx_video.h and font.h of the Wii port with SDL2: the menu is drawn in the
 *  640x480 screen of the Wii (stretched to the 1280x720 of the Wii U, on the
 *  TV and the GamePad), the game picture comes from the same RGB565 bitmap
 *  in 4x4 tiles that the line renderer writes for the Wii (CUSTOM_BLITTER).
 *
 *  Not done yet on the Wii U: bezels, auto crop, the sharp scaler, the VI
 *  settings (gamma, trap filter).
 ****************************************************************************/

/* SDL first: on the Wii U its headers bring wut's BOOL, not the one of libogc */
#define BOOL wut_BOOL
#define DCFlushRange wut_DCFlushRange
#define DCStoreRange wut_DCStoreRange
#define DCInvalidateRange wut_DCInvalidateRange
#define ICInvalidateRange wut_ICInvalidateRange
#define DCZeroRange wut_DCZeroRange
#define DCTouchRange wut_DCTouchRange
#define DCFlushRangeNoSync wut_DCFlushRangeNoSync
#define DCStoreRangeNoSync wut_DCStoreRangeNoSync
#define ICBlockInvalidate wut_ICBlockInvalidate
#include <SDL.h>
#include <SDL_ttf.h>
#include <SDL_image.h>
#undef BOOL
#undef DCFlushRange
#undef DCStoreRange
#undef DCInvalidateRange
#undef ICInvalidateRange
#undef DCZeroRange
#undef DCTouchRange
#undef DCFlushRangeNoSync
#undef DCStoreRangeNoSync
#undef ICBlockInvalidate

#include "shared.h"
#include "font.h"
#include "md_ntsc.h"
#include "sms_ntsc.h"
#include "gx_input.h"
#include <math.h>
#include "wiiu_bridge.h"

#include "Crosshair_p1_png.h"
#include "Crosshair_p2_png.h"
#include "CD_access_off_png.h"
#include "CD_access_on_png.h"
#include "CD_ready_off_png.h"
#include "CD_ready_on_png.h"
#include "mplus1p_ttf.h"

#define SCREEN_W 1280
#define SCREEN_H 720

/* the menu screen of the Wii */
static GXRModeObj menu_mode;
GXRModeObj *vmode = &menu_mode;

u32 gc_pal = 0;
u32 videoSync = 0;

sms_ntsc_t *sms_ntsc;
md_ntsc_t *md_ntsc;

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_mutex *render_lock;

/* game picture */
static SDL_Texture *screen_tex;
static int screen_w, screen_h;      /* texture size */
static int vwidth, vheight;         /* picture size in the bitmap */
static u16 *untiled;                /* the bitmap without its tiles */
static int screen_ready;

/* on-screen display */
static int osd;
static gx_texture *crosshair[2];
static gx_texture *cd_leds[2][2];
static char msg[64];
static u64 starttime;
static u32 frameCount;

/* a texture of the menu: the SDL texture, and its pixels for gxTextureRound */
typedef struct
{
  SDL_Texture *tex;
  SDL_Surface *surf;
} wiiu_tex;

#define LOCK()   SDL_LockMutex(render_lock)
#define UNLOCK() SDL_UnlockMutex(render_lock)

/****************************************************************************
 * Events: the system asks the app to close (power button...)
 ****************************************************************************/

static void pump_events(void)
{
  SDL_Event ev;
  while (SDL_PollEvent(&ev))
  {
    if (ev.type == SDL_QUIT)
    {
      shutdown();
      SDL_Quit();
      exit(0);
    }
  }
}

/****************************************************************************
 * Textures
 ****************************************************************************/

static wiiu_tex *tex_of(gx_texture *t)
{
  return t ? (wiiu_tex *)t->data : NULL;
}

/* size of a PNG in memory: up to its IEND chunk */
static size_t png_size(const u8 *p)
{
  size_t off = 8;
  if (memcmp(p, "\x89PNG", 4)) return 0;
  for (;;)
  {
    u32 len = (p[off] << 24) | (p[off + 1] << 16) | (p[off + 2] << 8) | p[off + 3];
    int end = !memcmp(p + off + 4, "IEND", 4);
    off += 12 + len;
    if (end || (off > (32 << 20))) break;
  }
  return off;
}

static gx_texture *texture_from_surface(SDL_Surface *s)
{
  gx_texture *texture;
  wiiu_tex *t;
  SDL_Surface *rgba;

  if (!s) return NULL;
  rgba = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_RGBA32, 0);
  SDL_FreeSurface(s);
  if (!rgba) return NULL;

  texture = (gx_texture *)calloc(1, sizeof(gx_texture));
  t = (wiiu_tex *)calloc(1, sizeof(wiiu_tex));
  if (!texture || !t)
  {
    free(texture);
    free(t);
    SDL_FreeSurface(rgba);
    return NULL;
  }

  LOCK();
  t->surf = rgba;
  t->tex = SDL_CreateTextureFromSurface(renderer, rgba);
  if (t->tex) SDL_SetTextureBlendMode(t->tex, SDL_BLENDMODE_BLEND);
  UNLOCK();

  texture->data = (u8 *)t;
  texture->width = rgba->w;
  texture->height = rgba->h;
  texture->format = GX_TF_RGBA8;
  return texture;
}

gx_texture *gxTextureOpenPNG(const u8 *png_data, FILE *png_file)
{
  SDL_RWops *rw = NULL;
  u8 *buf = NULL;

  if (png_file)
  {
    long size;
    fseek(png_file, 0, SEEK_END);
    size = ftell(png_file);
    fseek(png_file, 0, SEEK_SET);
    if (size <= 0) return NULL;
    buf = (u8 *)malloc(size);
    if (!buf) return NULL;
    if (fread(buf, 1, size, png_file) != (size_t)size)
    {
      free(buf);
      return NULL;
    }
    rw = SDL_RWFromConstMem(buf, size);
  }
  else if (png_data)
  {
    size_t size = png_size(png_data);
    if (!size) return NULL;
    rw = SDL_RWFromConstMem(png_data, size);
  }

  if (!rw)
  {
    free(buf);
    return NULL;
  }

  {
    gx_texture *t = texture_from_surface(IMG_LoadPNG_RW(rw));
    SDL_RWclose(rw);
    free(buf);
    return t;
  }
}

void gxTextureWritePNG(gx_texture *texture, FILE *png_file)
{
  (void)texture;
  (void)png_file;
}

/* rounded (disc label) texture: transparent outside the radius r and inside the hole */
void gxTextureRound(gx_texture *texture, int w, int h, float r, float hole)
{
  wiiu_tex *t = tex_of(texture);
  int x, y;
  float sx, sy;

  if (!t || !t->surf || !w || !h) return;

  sx = (float)w / texture->width;
  sy = (float)h / texture->height;

  SDL_LockSurface(t->surf);
  for (y = 0; y < texture->height; y++)
  {
    float dy = (y + 0.5f) * sy - h * 0.5f;
    u8 *row = (u8 *)t->surf->pixels + y * t->surf->pitch;
    for (x = 0; x < texture->width; x++)
    {
      float dx = (x + 0.5f) * sx - w * 0.5f;
      float d = sqrtf(dx * dx + dy * dy);
      float a = r - d + 0.5f;
      float b = d - hole + 0.5f;
      u8 *alpha = row + x * 4 + 3; /* RGBA32: alpha is the 4th byte */
      if (b < a) a = b;
      if (a >= 1.0f) continue;
      *alpha = (a <= 0.0f) ? 0 : (u8)(*alpha * a);
    }
  }
  SDL_UnlockSurface(t->surf);

  LOCK();
  if (t->tex) SDL_DestroyTexture(t->tex);
  t->tex = SDL_CreateTextureFromSurface(renderer, t->surf);
  if (t->tex) SDL_SetTextureBlendMode(t->tex, SDL_BLENDMODE_BLEND);
  UNLOCK();
}

void gxTextureClose(gx_texture **p_texture)
{
  gx_texture *texture = *p_texture;
  if (texture)
  {
    wiiu_tex *t = tex_of(texture);
    if (t)
    {
      LOCK();
      if (t->tex) SDL_DestroyTexture(t->tex);
      UNLOCK();
      if (t->surf) SDL_FreeSurface(t->surf);
      free(t);
    }
    free(texture);
    *p_texture = NULL;
  }
}

/****************************************************************************
 * Drawing (640x480 menu coordinates)
 *
 * Widescreen menu (WIIU_WIDE_MENU): the 640x480 screen of the Wii is drawn
 * at its own proportions in the middle of the 16:9 picture (854x480, x1.5),
 * and what covers its whole width (backgrounds, banners, dimming) is drawn
 * to the edges: tiles repeat, banners repeat one period of their grid.
 * The game picture keeps its own scaling (the whole 16:9 picture, as the
 * aspect settings of the emulator say).
 ****************************************************************************/

#ifndef WIIU_WIDE_MENU
#define WIIU_WIDE_MENU 1
#endif

#define MENU_FULL_W   854                     /* 1280 / 1.5 */
/* even, so that every edge falls on a whole screen pixel at x1.5 (pieces
   of banners that touch would show a gap or overlap otherwise) */
#define MENU_OX       106
#define BANNER_PERIOD 36                      /* the grid of the banners */
#define BANNER_EDGE   72                      /* their ends, with the corners of the frame */
#define TOP_BANNER_H     108                  /* the top banner (Banner_top.png) */
#define TOP_BANNER_SPLIT 396                  /* a grid line before its curve */

static int view_ox;      /* where the 640 wide Wii screen starts */
static int anchor_dx;    /* moves what is at a side of the Wii screen to that side of the picture */
static int view_w;       /* the whole width, in the same units */

static void set_menu_view(void)
{
#if WIIU_WIDE_MENU
  view_ox = MENU_OX;
  view_w = MENU_FULL_W;
  SDL_RenderSetScale(renderer, (float)SCREEN_H / 480.0f, (float)SCREEN_H / 480.0f);
#else
  view_ox = 0;
  view_w = 640;
  SDL_RenderSetScale(renderer, (float)SCREEN_W / 640.0f, (float)SCREEN_H / 480.0f);
#endif
}

static void set_game_view(void)
{
  anchor_dx = 0;
  view_ox = 0;
  view_w = 640;
  SDL_RenderSetScale(renderer, (float)SCREEN_W / 640.0f, (float)SCREEN_H / 480.0f);
}

/* menu position (0..1 across the screen) to the 640x480 Wii screen */
void wiiu_menu_point(float fx, float fy, float *x, float *y)
{
#if WIIU_WIDE_MENU
  *x = fx * ((float)SCREEN_W * 480.0f / (float)SCREEN_H) - MENU_OX;
#else
  *x = fx * 640.0f;
#endif
  *y = fy * 480.0f;
}

/* the side (-1 left, 0 centered, 1 right) what is drawn next belongs to */
void wiiu_set_anchor(int side)
{
  anchor_dx = side * view_ox;
}

int wiiu_anchor_dx(int side)
{
#if WIIU_WIDE_MENU
  return side * MENU_OX;
#else
  (void)side;
  return 0;
#endif
}

/* covers the whole width of the Wii screen */
#define FULL_WIDTH(x, w) (((x) <= 0) && ((x) + (w) >= 640))

void gxDrawRectangle(s32 x, s32 y, s32 w, s32 h, u8 alpha, GXColor color)
{
  SDL_Rect r = { x + view_ox + anchor_dx, y, w, h };
  if (FULL_WIDTH(x, w)) { r.x = 0; r.w = view_w; }
  LOCK();
  SDL_SetRenderDrawBlendMode(renderer, (alpha == 255) ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, alpha);
  SDL_RenderFillRect(renderer, &r);
  UNLOCK();
}

/* one piece [sx, sx+sw) of a banner (in 640 menu units) at x */
static void banner_piece(wiiu_tex *t, gx_texture *texture, int bw, int sx, int sw, int x, int y, int h)
{
  SDL_Rect src, dst;
  src.x = (sx * texture->width) / bw;
  src.w = ((sx + sw) * texture->width) / bw - src.x;
  src.y = 0;
  src.h = texture->height;
  dst.x = x; dst.y = y; dst.w = sw; dst.h = h;
  if (src.w > 0) SDL_RenderCopy(renderer, t->tex, &src, &dst);
}

void gxDrawTexture(gx_texture *texture, s32 x, s32 y, s32 w, s32 h, u8 alpha)
{
  wiiu_tex *t = tex_of(texture);
  SDL_Rect r = { x + view_ox + anchor_dx, y, w, h };
  if (!t || !t->tex) return;
  LOCK();
  SDL_SetTextureAlphaMod(t->tex, alpha);

  if (view_ox && FULL_WIDTH(x, w) && (w == 640) && (texture->height == TOP_BANNER_H))
  {
    /* the top banner: its curve goes down to the logo, which is at the right
       of the picture (wiiu_set_anchor): the part from the curve on moves
       right with it, the part before it stays at the left, the grid of the
       flat part before the curve fills between them */
    int right_x = TOP_BANNER_SPLIT + 2 * MENU_OX;
    int end_x = view_w - BANNER_EDGE;
    int dx;

    banner_piece(t, texture, w, 0, TOP_BANNER_SPLIT, 0, r.y, r.h);
    for (dx = TOP_BANNER_SPLIT; dx < right_x; dx += BANNER_PERIOD)
    {
      int pw = (dx + BANNER_PERIOD > right_x) ? (right_x - dx) : BANNER_PERIOD;
      banner_piece(t, texture, w, TOP_BANNER_SPLIT - BANNER_PERIOD, pw, dx, r.y, r.h);
    }
    banner_piece(t, texture, w, TOP_BANNER_SPLIT, w - BANNER_EDGE - TOP_BANNER_SPLIT, right_x, r.y, r.h);
    for (dx = right_x + w - BANNER_EDGE - TOP_BANNER_SPLIT; dx < end_x; dx += BANNER_PERIOD)
    {
      int pw = (dx + BANNER_PERIOD > end_x) ? (end_x - dx) : BANNER_PERIOD;
      banner_piece(t, texture, w, w - BANNER_EDGE - BANNER_PERIOD, pw, dx, r.y, r.h);
    }
    banner_piece(t, texture, w, w - BANNER_EDGE, BANNER_EDGE, end_x, r.y, r.h);
  }
  else if (view_ox && FULL_WIDTH(x, w) && (w > 2 * BANNER_EDGE))
  {
    /* a banner across the screen: its middle in place, its ends (the
       corners of the frame) at the edges of the 16:9 picture, and periods
       of its grid, taken next to each end, between them */
    int mid_l = BANNER_EDGE;                                  /* grid line */
    int mid_r = ((w - BANNER_EDGE + BANNER_PERIOD - 1) / BANNER_PERIOD) * BANNER_PERIOD;
    int dx;

    banner_piece(t, texture, w, mid_l, mid_r - mid_l, r.x + mid_l, r.y, r.h);

    /* the pieces touch without overlapping: the shadows are translucent,
       drawn twice they would show darker */
    for (dx = r.x + mid_l - BANNER_PERIOD; dx + BANNER_PERIOD > BANNER_EDGE; dx -= BANNER_PERIOD)
    {
      int cut = (dx < BANNER_EDGE) ? (BANNER_EDGE - dx) : 0;
      banner_piece(t, texture, w, mid_l + cut, BANNER_PERIOD - cut, dx + cut, r.y, r.h);
    }
    banner_piece(t, texture, w, 0, BANNER_EDGE, 0, r.y, r.h);

    for (dx = r.x + mid_r; dx < view_w - BANNER_EDGE; dx += BANNER_PERIOD)
    {
      int pw = BANNER_PERIOD;
      if (dx + pw > view_w - BANNER_EDGE) pw = view_w - BANNER_EDGE - dx;
      banner_piece(t, texture, w, mid_r - BANNER_PERIOD, pw, dx, r.y, r.h);
    }
    banner_piece(t, texture, w, w - BANNER_EDGE, BANNER_EDGE, view_w - BANNER_EDGE, r.y, r.h);
  }
  else
  {
    SDL_RenderCopy(renderer, t->tex, NULL, &r);
  }
  UNLOCK();
}

void gxDrawTextureRotate(gx_texture *texture, s32 x, s32 y, s32 w, s32 h, f32 angle, u8 alpha)
{
  wiiu_tex *t = tex_of(texture);
  SDL_Rect r = { x + view_ox + anchor_dx, y, w, h };
  if (!t || !t->tex) return;
  LOCK();
  SDL_SetTextureAlphaMod(t->tex, alpha);
  SDL_RenderCopyEx(renderer, t->tex, NULL, &r, angle, NULL, SDL_FLIP_NONE);
  UNLOCK();
}

void gxDrawTextureRepeat(gx_texture *texture, s32 x, s32 y, s32 w, s32 h, u8 alpha)
{
  wiiu_tex *t = tex_of(texture);
  int tx, ty, x0, w0;
  if (!t || !t->tex || !texture->width || !texture->height) return;

  /* tiles across the screen go to the edges, in step with the Wii screen */
  x0 = x + view_ox;
  w0 = w;
  if (FULL_WIDTH(x, w) && view_ox)
  {
    int n = (view_ox + texture->width - 1) / texture->width;
    x0 -= n * texture->width;
    w0 = view_w - x0;
  }

  LOCK();
  SDL_SetTextureAlphaMod(t->tex, alpha);
  for (ty = 0; ty < h; ty += texture->height)
  {
    for (tx = 0; tx < w0; tx += texture->width)
    {
      SDL_Rect src = { 0, 0, texture->width, texture->height };
      SDL_Rect dst;
      if (tx + src.w > w0) src.w = w0 - tx;
      if (ty + src.h > h) src.h = h - ty;
      dst.x = x0 + tx; dst.y = y + ty; dst.w = src.w; dst.h = src.h;
      SDL_RenderCopy(renderer, t->tex, &src, &dst);
    }
  }
  UNLOCK();
}

void gxClearScreen(GXColor color)
{
  gxDrawRectangle(0, 0, vmode->fbWidth, vmode->efbHeight, 255, color);
}

/* the menus draw a frame, then show it: 60 Hz, as the Wii video */
void gxSetScreen(void)
{
  VIDEO_WaitVSync();
  LOCK();
  SDL_RenderPresent(renderer);
  UNLOCK();
  pump_events();
  gx_input_UpdateMenu();
}

/****************************************************************************
 * Game picture
 ****************************************************************************/

/* Same scaling as gxSetAspectRatio of the Wii port: xscale is half the
   width in Wii video pixels (640), yscale a quarter of the height (480) */
static void aspect_ratio(int *xscale, int *yscale)
{
  *yscale = (bitmap.viewport.h + (2 * bitmap.viewport.y)) / 2;

  if (config.aspect)
  {
    if (vdp_pal && (!gc_pal || config.render))
      *yscale = *yscale * VI_MAX_HEIGHT_NTSC / VI_MAX_HEIGHT_PAL;
    else if (!vdp_pal && gc_pal && !config.render)
      *yscale = *yscale * VI_MAX_HEIGHT_PAL / VI_MAX_HEIGHT_NTSC;

    if ((config.overscan & 2) && !pico32x_active)
    {
      if (reg[12] & 1)
        *xscale = (system_clock == MCLOCK_NTSC) ? 350 : 354;
      else
        *xscale = (system_clock == MCLOCK_NTSC) ? 357 : 361;
    }
    else
    {
      if ((system_hw == SYSTEM_GG) && !config.gg_extra)
        *xscale = 202;
      else
        *xscale = (system_clock == MCLOCK_NTSC) ? 322 : 325;
    }

    /* the Wii U picture is 16:9: "ORIGINAL (16:9)" keeps a 4:3 game */
    if (config.aspect & 2)
      *xscale = (*xscale * 3) / 4;
  }
  else
  {
    *xscale = bitmap.viewport.w + (2 * bitmap.viewport.x);

    if (!(reg[12] & 1) && !pico32x_active)
      *xscale = (*xscale * 320) / 256;

    if ((system_hw == SYSTEM_GG) && !config.gg_extra)
    {
      if (!(config.overscan & 1)) *yscale = 112;
      if (!(config.overscan & 2)) *xscale = 320;
    }

    if (gc_pal && !config.render)
      *yscale = *yscale * VI_MAX_HEIGHT_PAL / VI_MAX_HEIGHT_NTSC;

    *xscale += config.xscale;
    *yscale += config.yscale;
  }
}

/* where the game picture is drawn, in menu coordinates */
static SDL_Rect game_rect(void)
{
  SDL_Rect r;
  int xscale, yscale;
  aspect_ratio(&xscale, &yscale);
  r.w = xscale * 2;
  r.h = yscale * 4;
  r.x = 320 + config.xshift - xscale;
  r.y = 240 + config.yshift - yscale * 2;
  return r;
}

/* the bitmap, from its 4x4 tiles of RGB565 pixels to lines */
static void untile(void)
{
  int x, y, tiles = vwidth >> 2;
  const u16 *in = (const u16 *)bitmap.data;
  for (y = 0; y < vheight; y++)
  {
    u16 *out = untiled + y * vwidth;
    const u16 *row = in + ((y >> 2) * tiles * 16) + ((y & 3) << 2);
    for (x = 0; x < tiles; x++)
    {
      out[0] = row[0];
      out[1] = row[1];
      out[2] = row[2];
      out[3] = row[3];
      out += 4;
      row += 16;
    }
  }
}

static void screen_texture(void)
{
  if (screen_tex && (screen_w == vwidth) && (screen_h == vheight))
  {
    SDL_SetTextureScaleMode(screen_tex, config.bilinear ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
    return;
  }
  if (screen_tex) SDL_DestroyTexture(screen_tex);
  screen_tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, vwidth, vheight);
  screen_w = vwidth;
  screen_h = vheight;
  if (screen_tex)
  {
    SDL_SetTextureBlendMode(screen_tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(screen_tex, config.bilinear ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
  }
}

static void draw_game(u8 alpha)
{
  SDL_Rect r;
  if (!screen_tex || !screen_ready) return;
  r = game_rect();
  if ((r.w < 640) || (r.h < 480))
    gxDrawRectangle(0, 0, 640, 480, 255, (GXColor)BLACK);
  LOCK();
  SDL_SetTextureAlphaMod(screen_tex, alpha);
  SDL_RenderCopy(renderer, screen_tex, NULL, &r);
  UNLOCK();
}

/* the game picture behind the menu: as in the game */
void gxDrawScreenshot(u8 alpha)
{
  LOCK();
  set_game_view();
  draw_game(alpha);
  set_menu_view();
  UNLOCK();
}

/* the game picture as a PNG (save state previews, snapshots) */
void gxSaveScreenshot(char *filename)
{
  SDL_Surface *s, *rgb;
  int w = bitmap.viewport.w + 2 * bitmap.viewport.x;
  int h = bitmap.viewport.h + 2 * bitmap.viewport.y;
  if (!untiled || !screen_ready || !vwidth || !vheight) return;
  if (w > vwidth) w = vwidth;
  if (h > vheight) h = vheight;
  s = SDL_CreateRGBSurfaceWithFormatFrom(untiled, w, h, 16, vwidth * 2, SDL_PIXELFORMAT_RGB565);
  if (!s) return;
  rgb = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_RGB24, 0);
  SDL_FreeSurface(s);
  if (rgb)
  {
    IMG_SavePNG(rgb, filename);
    SDL_FreeSurface(rgb);
  }
}

static void screen_point(int px, int py, int *x, int *y)
{
  SDL_Rect r = game_rect();
  *x = r.x + (px * r.w) / (bitmap.viewport.w + 2 * bitmap.viewport.x);
  *y = r.y + (py * r.h) / (bitmap.viewport.h + 2 * bitmap.viewport.y);
}

void gx_video_MapPointer(f32 fx, f32 fy, int *x, int *y)
{
  SDL_Rect r = game_rect();
  f32 qx = (fx * 640.0f - r.x) / r.w;
  f32 qy = (fy * 480.0f - r.y) / r.h;
  *x = (int)(qx * (bitmap.viewport.w + 2 * bitmap.viewport.x)) - bitmap.viewport.x;
  *y = (int)(qy * (bitmap.viewport.h + 2 * bitmap.viewport.y)) - bitmap.viewport.y;
}

static void draw_crosshair(gx_texture *texture, int x, int y)
{
  int w = texture->width, h = texture->height;
  if (config.aspect & 2) w = (w * 3) / 4;
  screen_point(x + bitmap.viewport.x, y + bitmap.viewport.y, &x, &y);
  gxDrawTexture(texture, x - w / 2, y - h / 2, w, h, 255);
}

static void draw_cd_leds(gx_texture *l, gx_texture *r)
{
  int w = l->width, h = l->height, xl, xr, y;
  if (config.aspect & 2) w = (w * 3) / 4;
  screen_point(bitmap.viewport.x, bitmap.viewport.y + bitmap.viewport.h - 4, &xl, &y);
  screen_point(bitmap.viewport.x + bitmap.viewport.w, bitmap.viewport.y + bitmap.viewport.h - 4, &xr, &y);
  xl += 8;
  xr -= 8 + w;
  y -= h;
  gxDrawTexture(l, xl, y, w, h, 255);
  gxDrawTexture(r, xr, y, w, h, 255);
}

/* bezels and auto crop: not on the Wii U yet */
void gx_video_AutoCropLoad(void) {}
void gx_video_AutoCropSave(void) {}
void gx_video_AutoCropReset(void) {}
int gx_video_BezelList(char (*names)[BEZEL_NAME_MAX], int max) { (void)names; (void)max; return 0; }

/* Emulation mode -> Menu mode */
void gx_video_Stop(void)
{
  if (sms_ntsc) free(sms_ntsc);
  if (md_ntsc) free(md_ntsc);
  sms_ntsc = NULL;
  md_ntsc = NULL;

  gxTextureClose(&crosshair[0]);
  gxTextureClose(&crosshair[1]);
  gxTextureClose(&cd_leds[0][0]);
  gxTextureClose(&cd_leds[0][1]);
  gxTextureClose(&cd_leds[1][0]);
  gxTextureClose(&cd_leds[1][1]);

  gxClearScreen((GXColor)BLACK);
  gxDrawScreenshot(0xff);
  gxSetScreen();
}

/* Menu mode -> Emulation mode */
void gx_video_Start(void)
{
  int i, player = 0;

  gc_pal = ((config.tv_mode == 1) || ((config.tv_mode == 2) && vdp_pal)) ? 1 : 0;

  if ((system_hw == SYSTEM_GG) && !config.gg_extra)
    bitmap.viewport.x = (config.overscan & 2) ? 14 : -48;
  else
    bitmap.viewport.x = (config.overscan & 2) * 7;

  bitmap.viewport.changed = 3;

  /* NTSC filter */
  if (config.ntsc)
  {
    if (!sms_ntsc) sms_ntsc = (sms_ntsc_t *)memalign(32, sizeof(sms_ntsc_t));
    if (!md_ntsc)  md_ntsc  = (md_ntsc_t *)memalign(32, sizeof(md_ntsc_t));

    switch (config.ntsc)
    {
      case 1:
        sms_ntsc_init(sms_ntsc, &sms_ntsc_composite);
        md_ntsc_init(md_ntsc, &md_ntsc_composite);
        break;
      case 2:
        sms_ntsc_init(sms_ntsc, &sms_ntsc_svideo);
        md_ntsc_init(md_ntsc, &md_ntsc_svideo);
        break;
      case 3:
        sms_ntsc_init(sms_ntsc, &sms_ntsc_rgb);
        md_ntsc_init(md_ntsc, &md_ntsc_rgb);
        break;
      case 4:
      {
        md_ntsc_setup_t ntsc_custom = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
        ntsc_custom.sharpness  = config.ntsc_sharpness;
        ntsc_custom.resolution = config.ntsc_resolution;
        ntsc_custom.artifacts  = config.ntsc_artifacts;
        ntsc_custom.fringing   = config.ntsc_fringing;
        ntsc_custom.bleed      = config.ntsc_bleed;
        sms_ntsc_init(sms_ntsc, (sms_ntsc_setup_t *)&ntsc_custom);
        md_ntsc_init(md_ntsc, &ntsc_custom);
        break;
      }
    }
  }

  osd = config.fps;
  memset(msg, 0, sizeof(msg));

  /* lightgun cursors */
  for (i = 0; i < MAX_DEVICES; i++)
  {
    if ((input.dev[i] == DEVICE_LIGHTGUN) || (input.dev[i] == DEVICE_SMASH))
    {
      if (config.input[player].device >= 0)
      {
        if ((i == 0) || ((i == 4) && (input.system[1] != SYSTEM_LIGHTPHASER) && (input.system[1] != SYSTEM_SMASH)))
        {
          if (config.gun_cursor[0])
          {
            crosshair[0] = gxTextureOpenPNG(Crosshair_p1_png, 0);
            osd = 1;
          }
        }
        else if (config.gun_cursor[1])
        {
          crosshair[1] = gxTextureOpenPNG(Crosshair_p2_png, 0);
          osd = 1;
        }
      }
    }
    if (input.dev[i] != NO_DEVICE)
      player++;
  }

  /* CD leds */
  if ((system_hw == SYSTEM_MCD) && config.cd_leds)
  {
    cd_leds[0][0] = gxTextureOpenPNG(CD_access_off_png, 0);
    cd_leds[0][1] = gxTextureOpenPNG(CD_access_on_png, 0);
    cd_leds[1][0] = gxTextureOpenPNG(CD_ready_off_png, 0);
    cd_leds[1][1] = gxTextureOpenPNG(CD_ready_on_png, 0);
    osd = 1;
  }

  /* the sound sets the speed (wiiu_audio.c) */
  videoSync = 0;
  frameCount = 0;
  starttime = wiiu_time_us();
}

int gx_video_Update(int status)
{
  if (status & VIDEO_UPDATE)
  {
    if (bitmap.viewport.changed & 1)
    {
      vwidth = bitmap.viewport.w + (2 * bitmap.viewport.x);
      vheight = bitmap.viewport.h + (2 * bitmap.viewport.y);

      if (config.render && interlaced)
        vheight = vheight << 1;

      if (config.ntsc && !pico32x_active)
      {
        vwidth = (reg[12] & 1) ? MD_NTSC_OUT_WIDTH(vwidth) : SMS_NTSC_OUT_WIDTH(vwidth);
        vwidth = (vwidth >> 2) << 2;
      }

      if (vwidth > 720) vwidth = 720;
      if (vheight > 576) vheight = 576;
      bitmap.viewport.changed &= ~1;
    }

    if ((vwidth > 0) && (vheight > 0))
    {
      untile();

      LOCK();
      set_game_view();
      screen_texture();
      if (screen_tex)
        SDL_UpdateTexture(screen_tex, NULL, untiled, vwidth * 2);
      screen_ready = 1;

      SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
      SDL_RenderClear(renderer);
      UNLOCK();

      draw_game(0xff);

      if (osd)
      {
        if (crosshair[0])
        {
          if ((input.system[0] == SYSTEM_LIGHTPHASER) || (input.system[0] == SYSTEM_SMASH))
            draw_crosshair(crosshair[0], input.analog[0][0], input.analog[0][1]);
          else
            draw_crosshair(crosshair[0], input.analog[4][0], input.analog[4][1]);
        }
        if (crosshair[1])
        {
          if ((input.system[1] == SYSTEM_LIGHTPHASER) || (input.system[1] == SYSTEM_SMASH))
            draw_crosshair(crosshair[1], input.analog[4][0], input.analog[4][1]);
          else
            draw_crosshair(crosshair[1], input.analog[5][0], input.analog[5][1]);
        }

        if (cd_leds[1][1])
        {
          u8 mode = scd.regs[0x06 >> 1].byte.h;
          draw_cd_leds(cd_leds[1][(mode >> 1) & 1], cd_leds[0][mode & 1]);
        }

        if (config.fps)
        {
          u64 now = wiiu_time_us();
          u32 delta = (u32)(now - starttime);
          frameCount++;
          if (delta > 1000000)
          {
            sprintf(msg, "%3.2f FPS", (float)frameCount * 1000000.0 / (float)delta);
            frameCount = 0;
            starttime = now;
          }
          FONT_write(msg, 20, (bitmap.viewport.x > 0) ? (24 + bitmap.viewport.x) : 24, 40, 640, (GXColor)WHITE);
        }
      }

      LOCK();
      SDL_RenderPresent(renderer);
      set_menu_view();
      UNLOCK();
    }

    pump_events();

    /* interlaced mode change */
    if (bitmap.viewport.changed & 4)
    {
      if (!config.render && config.vsync && (gc_pal == vdp_pal))
        audio_set_rate(snd.sample_rate, get_framerate());
      bitmap.viewport.changed &= ~4;
    }
  }

  return status & ~(VIDEO_WAIT | VIDEO_UPDATE);
}

void gx_video_Init(void)
{
  memset(&menu_mode, 0, sizeof(menu_mode));
  menu_mode.fbWidth = 640;
  menu_mode.efbHeight = 480;
  menu_mode.xfbHeight = 480;
  menu_mode.viWidth = 640;
  menu_mode.viHeight = 480;
  menu_mode.viTVMode = VI_TVMODE_NTSC_INT;

  SDL_SetHint(SDL_HINT_RENDER_VSYNC, "0");
  SDL_Init(SDL_INIT_VIDEO);
  window = SDL_CreateWindow("Genesis Plus GX ULTIMATE", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SCREEN_W, SCREEN_H, 0);
  renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  if (!renderer) renderer = SDL_CreateRenderer(window, -1, 0);
  render_lock = SDL_CreateMutex();

  /* the 640x480 menu screen of the Wii in the 16:9 picture */
  set_menu_view();

  untiled = (u16 *)calloc(720 * 576, sizeof(u16));

  IMG_Init(IMG_INIT_PNG);
  FONT_Init();

  gxClearScreen((GXColor)BLACK);
  LOCK();
  SDL_RenderPresent(renderer);
  UNLOCK();
}

void gx_video_Shutdown(void)
{
  FONT_Shutdown();
}

/****************************************************************************
 * Fonts: the system font of the Wii U (font.h of the Wii port)
 *
 * FONT_write draws a line with its bottom at y, "size" pixels high in the
 * 480 lines of the menu screen, as the IPL font of the Wii is drawn.
 ****************************************************************************/

#define MAX_SIZES 16

typedef struct
{
  int size;               /* height in menu pixels */
  TTF_Font *font;
  SDL_Texture *glyph[256];
  int gw[256], gh[256];   /* glyph texture size (screen pixels) */
  int adv[256];           /* advance, in menu pixels */
} font_cache;

static font_cache fonts[MAX_SIZES];
static const void *font_data;
static uint32_t font_bytes;
static float font_scale = (float)SCREEN_H / 480.0f;   /* screen pixels per menu pixel, vertically */

/* letters spaced a little, as those of the IPL font */
#define FONT_TRACKING 0.08f

/* M+ 1p Medium (SIL Open Font License, wiiu/fonts, Latin characters only):
   a free gothic font close to the IPL font of the GameCube and Wii, which is
   in their ROM and cannot be read on the Wii U nor shipped. The Wii U
   system font is the fallback. */
int FONT_Init(void)
{
  if (TTF_Init() < 0) return 0;
  font_data = mplus1p_ttf;
  font_bytes = mplus1p_ttf_size;
  if (font_bytes) return 1;
  return wiiu_system_font(&font_data, &font_bytes);
}

void FONT_Shutdown(void)
{
  int i, c;
  for (i = 0; i < MAX_SIZES; i++)
  {
    for (c = 0; c < 256; c++)
      if (fonts[i].glyph[c]) SDL_DestroyTexture(fonts[i].glyph[c]);
    if (fonts[i].font) TTF_CloseFont(fonts[i].font);
  }
  memset(fonts, 0, sizeof(fonts));
}

static font_cache *font_get(int size)
{
  int i, pt;
  font_cache *f = NULL;

  for (i = 0; i < MAX_SIZES; i++)
  {
    if (fonts[i].font && (fonts[i].size == size))
      return &fonts[i];
    if (!fonts[i].font && !f)
      f = &fonts[i];
  }
  if (!f || !font_data) return NULL;

  /* the whole line height (ascent + descent) is the size, on the screen */
  pt = (int)(size * font_scale);
  f->font = TTF_OpenFontRW(SDL_RWFromConstMem(font_data, font_bytes), 1, pt);
  if (f->font && TTF_FontHeight(f->font) > 0)
  {
    int pt2 = (pt * pt) / TTF_FontHeight(f->font);
    if (pt2 != pt)
    {
      TTF_CloseFont(f->font);
      f->font = TTF_OpenFontRW(SDL_RWFromConstMem(font_data, font_bytes), 1, pt2);
    }
  }
  if (!f->font) return NULL;
  f->size = size;
  return f;
}

/* Latin-1 character of the Wii font */
static int glyph_load(font_cache *f, u8 c)
{
  if (!f->adv[c])
  {
    int minx, maxx, miny, maxy, advance = 0;
    if (TTF_GlyphMetrics(f->font, c, &minx, &maxx, &miny, &maxy, &advance) == 0)
      f->adv[c] = (int)(advance / font_scale + f->size * FONT_TRACKING + 0.5f);
    if (f->adv[c] <= 0) f->adv[c] = 1;
    /* spaces as wide as in the IPL font: the menus leave room for button
       icons with them ("Press    to continue.") */
    if ((c == ' ') && (f->adv[c] < (f->size * 40) / 100))
      f->adv[c] = (f->size * 40) / 100;
    if (c > ' ')
    {
      SDL_Color white = { 255, 255, 255, 255 };
      SDL_Surface *s = TTF_RenderGlyph_Blended(f->font, c, white);
      if (s)
      {
        LOCK();
        f->glyph[c] = SDL_CreateTextureFromSurface(renderer, s);
        UNLOCK();
        f->gw[c] = s->w;
        f->gh[c] = s->h;
        SDL_FreeSurface(s);
      }
    }
  }
  return f->adv[c];
}

static int char_width(font_cache *f, u8 c)
{
  return f ? glyph_load(f, c) : (c ? 1 : 0);
}

static void draw_char(font_cache *f, u8 c, int x, int y, GXColor color)
{
  SDL_Rect r;
  glyph_load(f, c);
  if (!f->glyph[c]) return;
  r.x = x + view_ox + anchor_dx;
  r.y = y - f->size;
  r.w = (int)(f->gw[c] / font_scale + 0.5f);
  r.h = (int)(f->gh[c] / font_scale + 0.5f);
  LOCK();
  SDL_SetTextureColorMod(f->glyph[c], color.r, color.g, color.b);
  SDL_SetTextureAlphaMod(f->glyph[c], color.a);
  SDL_RenderCopy(renderer, f->glyph[c], NULL, &r);
  UNLOCK();
}

int FONT_getWidth(char *string, int size)
{
  font_cache *f = font_get(size);
  int w = 0;
  while (*string && (*string != '\n'))
    w += char_width(f, (u8)*string++);
  return w;
}

int FONT_write(char *string, int size, int x, int y, int max_width, GXColor color)
{
  font_cache *f = font_get(size);
  int ox = x;
  if (!f) return 0;
  while (*string)
  {
    if (*string == '\n')
    {
      x = ox;
      y += size;
    }
    else
    {
      int w = char_width(f, (u8)*string);
      if ((x + w) > (ox + max_width)) return strlen(string);
      draw_char(f, (u8)*string, x, y, color);
      x += w;
    }
    string++;
  }
  return 0;
}

int FONT_writeCenter(char *string, int size, int x1, int x2, int y, GXColor color)
{
  font_cache *f = font_get(size);
  int x, w = FONT_getWidth(string, size);
  if (!f) return 0;
  if ((x1 + w) > x2) w = x2 - x1;
  x = x1 + (x2 - x1 - w) / 2;
  while (*string && (*string != '\n'))
  {
    int cw = char_width(f, (u8)*string);
    if ((x + cw) > x2) return strlen(string);
    draw_char(f, (u8)*string, x, y, color);
    x += cw;
    string++;
  }
  if (*string == '\n')
    return FONT_writeCenter(string + 1, size, x1, x2, y + size, color);
  return 0;
}

int FONT_alignRight(char *string, int size, int x, int y, GXColor color)
{
  font_cache *f = font_get(size);
  int ox = x, w = FONT_getWidth(string, size);
  if (!f) return 0;
  x -= w;
  while (*string && (*string != '\n'))
  {
    int cw = char_width(f, (u8)*string);
    if ((x + cw) > ox) return strlen(string);
    draw_char(f, (u8)*string, x, y, color);
    x += cw;
    string++;
  }
  if (*string == '\n')
    return FONT_alignRight(string + 1, size, ox, y + size, color);
  return 0;
}

/****************************************************************************
 * NTSC filters: the blitters of the Wii port (RGB565 in 4x4 tiles)
 ****************************************************************************/

typedef unsigned short sms_ntsc_out_t;
typedef unsigned short md_ntsc_out_t;

void sms_ntsc_blit( sms_ntsc_t const* ntsc, SMS_NTSC_IN_T const* table, unsigned char* input,
                    int in_width, int vline)
{
  int const chunk_count = in_width / sms_ntsc_in_chunk;

  /* handle extra 0, 1, or 2 pixels by placing them at beginning of row */
  int const in_extra = in_width - chunk_count * sms_ntsc_in_chunk;
  unsigned const extra2 = (unsigned) -(in_extra >> 1 & 1); /* (unsigned) -1 = ~0 */
  unsigned const extra1 = (unsigned) -(in_extra & 1) | extra2;

  /* use palette entry 0 for unused pixels */
  SMS_NTSC_IN_T border = table[0];

  SMS_NTSC_BEGIN_ROW( ntsc, border,
      (SMS_NTSC_ADJ_IN( table[input[0]] )) & extra2,
      (SMS_NTSC_ADJ_IN( table[input[extra2 & 1]] )) & extra1 );

  /* directly fill the RGB565 texture */
  /* one tile is 32 byte = 4x4 pixels */
  /* tiles are stored continuously in texture memory */
  in_width = SMS_NTSC_OUT_WIDTH(in_width) / 4;
  int offset = ((in_width * 32) * (vline / 4)) + ((vline & 3) * 8);
  sms_ntsc_out_t* __restrict__ line_out  = (sms_ntsc_out_t*)(bitmap.data + offset);
  offset = 0;

  int n;
  input += in_extra;

  for ( n = chunk_count; n; --n )
  {
    /* order of input and output pixels must not be altered */
    SMS_NTSC_COLOR_IN( 0, ntsc, SMS_NTSC_ADJ_IN( table[*input++] ) );
    SMS_NTSC_RGB_OUT( 0, line_out[offset++] );
    if ((offset % 4) == 0) offset += 12;
    SMS_NTSC_RGB_OUT( 1, line_out[offset++] );
    if ((offset % 4) == 0) offset += 12;
    
    SMS_NTSC_COLOR_IN( 1, ntsc, SMS_NTSC_ADJ_IN( table[*input++] ) );
    SMS_NTSC_RGB_OUT( 2, line_out[offset++] );
    if ((offset % 4) == 0) offset += 12;
    SMS_NTSC_RGB_OUT( 3, line_out[offset++] );
    if ((offset % 4) == 0) offset += 12;
      
    SMS_NTSC_COLOR_IN( 2, ntsc, SMS_NTSC_ADJ_IN( table[*input++] ) );
    SMS_NTSC_RGB_OUT( 4, line_out[offset++] );
    if ((offset % 4) == 0) offset += 12;
    SMS_NTSC_RGB_OUT( 5, line_out[offset++] );
    if ((offset % 4) == 0) offset += 12;
    SMS_NTSC_RGB_OUT( 6, line_out[offset++] );
    if ((offset % 4) == 0) offset += 12;
  }

  /* finish final pixels */
  SMS_NTSC_COLOR_IN( 0, ntsc, border );
  SMS_NTSC_RGB_OUT( 0, line_out[offset++] );
  if ((offset % 4) == 0) offset += 12;
  SMS_NTSC_RGB_OUT( 1, line_out[offset++] );
  if ((offset % 4) == 0) offset += 12;

  SMS_NTSC_COLOR_IN( 1, ntsc, border );
  SMS_NTSC_RGB_OUT( 2, line_out[offset++] );
  if ((offset % 4) == 0) offset += 12;
  SMS_NTSC_RGB_OUT( 3, line_out[offset++] );
  if ((offset % 4) == 0) offset += 12;

  SMS_NTSC_COLOR_IN( 2, ntsc, border );
  SMS_NTSC_RGB_OUT( 4, line_out[offset++] );
  if ((offset % 4) == 0) offset += 12;
  SMS_NTSC_RGB_OUT( 5, line_out[offset++] );
  if ((offset % 4) == 0) offset += 12;
  SMS_NTSC_RGB_OUT( 6, line_out[offset++] );
  if ((offset % 4) == 0) offset += 12;
}

void md_ntsc_blit( md_ntsc_t const* ntsc, MD_NTSC_IN_T const* table, unsigned char* input,
                   int in_width, int vline)
{
  int const chunk_count = in_width / md_ntsc_in_chunk - 1;

  /* use palette entry 0 for unused pixels */
  MD_NTSC_IN_T border = table[0];

  MD_NTSC_BEGIN_ROW( ntsc, border,
        MD_NTSC_ADJ_IN( table[*input++] ),
        MD_NTSC_ADJ_IN( table[*input++] ),
        MD_NTSC_ADJ_IN( table[*input++] ) );

  /* directly fill the RGB565 texture */
  /* one tile is 32 byte = 4x4 pixels */
  /* tiles are stored continuously in texture memory */
  in_width = MD_NTSC_OUT_WIDTH(in_width) >> 2;
  int offset = ((in_width << 5) * (vline >> 2)) + ((vline & 3) * 8);
  md_ntsc_out_t* __restrict__ line_out  = (md_ntsc_out_t*)(bitmap.data + offset);

  int n;

  for ( n = chunk_count; n; --n )
  {
    /* order of input and output pixels must not be altered */
    MD_NTSC_COLOR_IN( 0, ntsc, MD_NTSC_ADJ_IN( table[*input++] ) );
    MD_NTSC_RGB_OUT( 0, *line_out++ );
    MD_NTSC_RGB_OUT( 1, *line_out++ );

    MD_NTSC_COLOR_IN( 1, ntsc, MD_NTSC_ADJ_IN( table[*input++] ) );
    MD_NTSC_RGB_OUT( 2, *line_out++ );
    MD_NTSC_RGB_OUT( 3, *line_out++ );

    line_out += 12;

    MD_NTSC_COLOR_IN( 2, ntsc, MD_NTSC_ADJ_IN( table[*input++] ) );
    MD_NTSC_RGB_OUT( 4, *line_out++ );
    MD_NTSC_RGB_OUT( 5, *line_out++ );

    MD_NTSC_COLOR_IN( 3, ntsc, MD_NTSC_ADJ_IN( table[*input++] ) );
    MD_NTSC_RGB_OUT( 6, *line_out++ );
    MD_NTSC_RGB_OUT( 7, *line_out++ );

    line_out += 12;
  }

  /* finish final pixels */
  MD_NTSC_COLOR_IN( 0, ntsc, MD_NTSC_ADJ_IN( table[*input++] ) );
  MD_NTSC_RGB_OUT( 0, *line_out++ );
  MD_NTSC_RGB_OUT( 1, *line_out++ );

  MD_NTSC_COLOR_IN( 1, ntsc, border );
  MD_NTSC_RGB_OUT( 2, *line_out++ );
  MD_NTSC_RGB_OUT( 3, *line_out++ );

  line_out += 12;

  MD_NTSC_COLOR_IN( 2, ntsc, border );
  MD_NTSC_RGB_OUT( 4, *line_out++ );
  MD_NTSC_RGB_OUT( 5, *line_out++ );

  MD_NTSC_COLOR_IN( 3, ntsc, border );
  MD_NTSC_RGB_OUT( 6, *line_out++ );
  MD_NTSC_RGB_OUT( 7, *line_out++ );
}
