/****************************************************************************
 *  gx_audio.c
 *
 *  Genesis Plus GX audio support
 *
 *  Copyright Eke-Eke (2007-2015), based on original work from Softdev (2006)
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
 *  gx_audio.c of the Wii port: the menu music is the same (OGG player on
 *  ASND voices); the sound of the game goes to the SDL output of
 *  wiiu_asnd.c instead of the audio DMA of the Wii, and the emulation
 *  waits on it to keep its speed.
 ****************************************************************************/

#include "shared.h"
#include "bezel.h"
#include <ogc/lwp_watchdog.h>
#include "wiiu_audio.h"
#include "wiiu_bridge.h"

/* the samples of one frame (stereo) */
static s16 soundbuffer[4096 * 2] ATTRIBUTE_ALIGN(32);
static int bufferSize;

/* frames of sound waiting to be played before the emulation waits */
#define QUEUE_FRAMES 2400

/* Background music: the OGG files of the music folder, chosen by
   config.menu_music when the menu opens the first time, then by the mini
   player of the menu. The file playing is read into memory and looped.
   It plays in the menu before a game is loaded, and in the Load Game menu
   (from the beginning) once one is: the in-game menu is silent. */
#define MENU_MUSIC_FOLDER   DEFAULT_PATH "/music"

/* the music folder of the device the emulator started from, then of the SD
   card and of the USB drive: the first of them with music is listed, so a
   USB drive is not read when the SD card has the music (an asleep or slow
   drive could hold the menu there) */
static const char *music_dirs[] =
{
  MENU_MUSIC_FOLDER,
  "sd:" MENU_MUSIC_FOLDER   /* Wii U: the USB drive is only for the games */
};
#define MUSIC_DIRS ((int)(sizeof(music_dirs) / sizeof(music_dirs[0])))
#define MENU_MUSIC_MAX_SIZE (16 * 1024 * 1024)
static u8 *Bg_music_ogg = NULL;
static u32 Bg_music_ogg_size = 0;
static int Bg_music_loaded = 0;          /* the setting was applied once */
static char (*music_names)[BEZEL_NAME_MAX] = NULL;
static int music_count = 0;
static int music_current = -1;           /* the file of music_names chosen */
static int music_read_index = -1;        /* the file in Bg_music_ogg */
static int music_state = MUSIC_STOPPED;
static int music_active = 0;             /* the OGG player plays Bg_music_ogg */
static int music_enabled = 1;            /* not the in-game menu */

/* Frame Sync */
u32 audioSync;

/***************************************************************************************/
/*   Audio engine                                                                      */
/***************************************************************************************/

/* AUDIO engine initialization */
void gx_audio_Init(void)
{
  /* the SDL sound output with the ASND voices of the menu (wiiu_asnd.c) */
  ASND_Init();
}

/***************************************************************************************/
/*   Menu music                                                                        */
/***************************************************************************************/

int gx_audio_MusicList(char (*names)[BEZEL_NAME_MAX], int max)
{
  int i, count = 0;
  for (i = 0; (i < MUSIC_DIRS) && (count == 0); i++)
  {
    count = bezel_list_ext(music_dirs[i], ".ogg", names, count, max);
  }
  return count;
}

static void music_stop_player(void)
{
  if (music_active)
  {
    PauseOgg(1);
    StopOgg();
    music_active = 0;
  }
}

static void music_scan(void)
{
  if (!music_names)
  {
    music_names = malloc(MAX_MENU_MUSIC * BEZEL_NAME_MAX);
  }
  music_count = music_names ? gx_audio_MusicList(music_names, MAX_MENU_MUSIC) : 0;
  music_current = -1;
  music_read_index = -1; /* the indexes may have changed */
}

/* Reads music_names[music_current] (the player is stopped) */
static int music_read(void)
{
  char fname[MAXPATHLEN];

  if ((music_current < 0) || (music_current >= music_count))
  {
    return 0;
  }
  if (Bg_music_ogg && (music_read_index == music_current))
  {
    return 1;
  }
  if (Bg_music_ogg)
  {
    free(Bg_music_ogg);
    Bg_music_ogg = NULL;
    Bg_music_ogg_size = 0;
  }
  music_read_index = -1;

  FILE *f = NULL;
  int i;
  for (i = 0; (i < MUSIC_DIRS) && !f; i++)
  {
    snprintf(fname, sizeof(fname), "%s/%s", music_dirs[i], music_names[music_current]);
    f = fopen(fname, "rb");
  }
  if (!f)
  {
    return 0;
  }
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  if ((size > 0) && (size <= MENU_MUSIC_MAX_SIZE))
  {
    Bg_music_ogg = memalign(32, size);
    if (Bg_music_ogg)
    {
      if (fread(Bg_music_ogg, 1, size, f) == (size_t)size)
      {
        Bg_music_ogg_size = size;
        music_read_index = music_current;
      }
      else
      {
        free(Bg_music_ogg);
        Bg_music_ogg = NULL;
      }
    }
  }
  fclose(f);
  return Bg_music_ogg != NULL;
}

static void music_start(s32 at)
{
  music_stop_player();
  if (!music_enabled)
  {
    return; /* the in-game menu: the state is kept for the Load Game menu */
  }
  if (Shutdown || !music_read())
  {
    music_state = MUSIC_STOPPED;
    return;
  }
  PauseOgg(0);
  PlayOgg((char *)Bg_music_ogg, Bg_music_ogg_size, at, OGG_INFINITE_TIME);
  SetVolumeOgg(((int)config.bgm_volume * MAX_VOLUME) / 100);
  music_active = 1;
  music_state = MUSIC_PLAYING;
}

/* The file of the setting: none, one by chance or the one named */
static void music_pick(void)
{
  int i;
  music_current = -1;
  if (!config.menu_music[0] || !music_count)
  {
    return;
  }
  if (!strcmp(config.menu_music, MENU_MUSIC_RANDOM))
  {
    music_current = (int)(gettime() % music_count);
    return;
  }
  for (i = 0; i < music_count; i++)
  {
    if (!strcasecmp(music_names[i], config.menu_music))
    {
      music_current = i;
      return;
    }
  }
}

static void music_apply_setting(void)
{
  music_stop_player();
  music_scan();
  music_pick();
  Bg_music_loaded = 1;
  if (music_current < 0)
  {
    music_state = MUSIC_STOPPED;
  }
  else if (music_enabled)
  {
    music_start(0);
  }
  else
  {
    music_state = MUSIC_PLAYING; /* once the Load Game menu opens */
  }
}

void gx_audio_MusicEnable(int on)
{
  music_enabled = on;
  if (!on)
  {
    music_stop_player(); /* the state is kept */
  }
  else if ((music_state == MUSIC_PLAYING) && !music_active)
  {
    music_start(0); /* from the beginning */
  }
}

int gx_audio_MusicEnabled(void)
{
  return music_enabled;
}

void gx_audio_MusicRestart(void)
{
  music_apply_setting();
}

void gx_audio_MusicPlay(void)
{
  if (!music_count)
  {
    return;
  }
  if ((music_state == MUSIC_PAUSED) && music_active)
  {
    PauseOgg(0);
    music_state = MUSIC_PLAYING;
  }
  else if ((music_state != MUSIC_PLAYING) || !music_active)
  {
    if (music_current < 0)
    {
      music_current = 0;
    }
    music_start(0);
  }
}

void gx_audio_MusicPause(void)
{
  if (music_state == MUSIC_PLAYING)
  {
    if (music_active)
    {
      PauseOgg(1);
    }
    music_state = MUSIC_PAUSED;
  }
}

void gx_audio_MusicStop(void)
{
  music_stop_player();
  music_state = MUSIC_STOPPED;
}

void gx_audio_MusicSkip(int dir)
{
  if (!music_count)
  {
    return;
  }
  if (music_current < 0)
  {
    music_current = (dir > 0) ? 0 : (music_count - 1);
  }
  else
  {
    music_current = (music_current + dir + music_count) % music_count;
  }
  music_start(0);
}

int gx_audio_MusicCount(void)
{
  return music_count;
}

int gx_audio_MusicState(void)
{
  return music_state;
}

void gx_audio_MusicName(char *out, int len)
{
  if ((music_current >= 0) && (music_current < music_count))
  {
    bezel_display_name_ext(music_names[music_current], ".ogg", out, len);
  }
  else
  {
    out[0] = 0;
  }
}

/* AUDIO engine shutdown */
void gx_audio_Shutdown(void)
{
  PauseOgg(1);
  StopOgg();
  music_active = 0;
  ASND_Pause(1);
  ASND_End();
  if (Bg_music_ogg)
  {
    free(Bg_music_ogg);
    Bg_music_ogg = NULL;
  }
}

/*** 
      gx_audio_Update

     This function retrieves samples for the frame then set the next DMA parameters 
     Parameters will be taken in account only when current DMA operation is over

     To keep audio & video synchronized, DMA from external memory to audio interface is 
     started once by video update function when first frame is ready to be displayed, 
     then anytime video mode is changed and emulation resynchronized to video hardware.

     Once started, audio DMA restarts automatically when all samples have been played.
     At that time:
       - if DMA settings have not been updated, previous sound buffer will be played again
       - if DMA settings are updated too fast, one sound buffer frame might be skipped
     
     Therefore, in order to maintain perfect audio playback without any sound skipping  
     or lagging, we need to make sure frame emulation is completed and this function is 
     called before previous DMA transfer is finished and after it has been started.

     This is done by synchronizing frame emulation with audio DMA interrupt (which happens
     anytime audio DMA restarts). When video sync is enabled, to keep emulation in sync 
     with both video AND audio, an appropriate number of samples is rendered per frame by
     adjusting emulator output samplerate.
 ***/
int gx_audio_Update(int status)
{
  if (status & AUDIO_UPDATE)
  {
    int frames = pico32x_active ? pico32x_audio(soundbuffer) : audio_update(soundbuffer);
    wiiu_stream_push(soundbuffer, frames);
    bufferSize = frames * 4;
    status &= ~AUDIO_UPDATE;
  }

  /* the emulation runs at the speed of the sound: it waits while more
     than about three frames of sound are waiting to be played (not when
     audioSync is off, the fast-forward) */
  if (status & AUDIO_WAIT)
  {
    if (audioSync)
    {
      /* if the sound stops being played (nothing leaves the queue for a
         second) the game goes on without waiting for it. With VSYNC the
         picture sets the speed: the sound only keeps the queue from
         growing. */
      extern int wiiu_vsync;
      u32 limit = wiiu_vsync ? (QUEUE_FRAMES * 2) : QUEUE_FRAMES;
      int waited = 0;
      u32 last = wiiu_stream_queued();
      while (wiiu_stream_queued() > limit)
      {
        wiiu_sleep_us(1000);
        if (wiiu_stream_queued() != last)
        {
          last = wiiu_stream_queued();
          waited = 0;
        }
        else if (++waited > 1000)
        {
          wiiu_log("sound queue stuck at %u frames: not waiting", (unsigned)last);
          wiiu_stream_stop();
          wiiu_stream_start();
          audioSync = 0;
          break;
        }
      }
    }
    status &= ~AUDIO_WAIT;
  }

  return status;
}

/***
      gx_audio_Queued

     How long the sound already handed to the audio DMA lasts, in microseconds:
     the rest of the block playing and the block waiting to start after it.
     The next frame must hand its samples over before then, or the DMA plays
     the last block again. -1 when nothing was handed over since the restart.
 ***/
int gx_audio_Queued(void)
{
  if (!bufferSize)
    return -1;

  /* 48000 frames a second */
  return (int)(wiiu_stream_queued() * 125 / 6);
}

/*** 
      gx_audio_Start

     This function restarts the audio engine
     This is called when coming back from Main Menu
 ***/
void gx_audio_Start(void)
{
  wiiu_log("gx_audio_Start");
  /* shutdown background music */
  PauseOgg(1);
  StopOgg();
  music_active = 0;

  /* menu sounds off */
  ASND_Pause(1);
  ASND_End();

  /* emulation is synchronized with the sound by default */
  audioSync = AUDIO_WAIT;
  bufferSize = 0;
  wiiu_stream_start();
}

/***
      gx_audio_Stop

     This function stops current Audio DMA process
     This is called when going back to Main Menu
 ***/
void gx_audio_Stop(void)
{
  /* game sound off, menu sounds on */
  wiiu_stream_stop();
  ASND_Init();
  ASND_Pause(0);
	
  /* background music: the one of the setting the first time (once the
     settings are loaded). It plays before a game is loaded only: the
     in-game menu is silent (the Load Game menu plays it again). */
  music_enabled = !system_hw;
  if (!Bg_music_loaded)
  {
    music_apply_setting();
  }
  else if (music_enabled && (music_state == MUSIC_PLAYING))
  {
    music_start(0);
  }
}
