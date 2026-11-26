/****************************************************************************
 *  Genesis Plus GX ULTIMATE - Wii U port
 *
 *  Sound output with SDL2 (48 kHz stereo), mixing:
 *   - the sound of the game (a FIFO the emulation fills each frame), and
 *   - the voices of ASND, the sound library of libogc the Wii menus use
 *     (button sounds, the OGG player of the menu music)
 *  The LWP queues the OGG player waits on are SDL semaphores.
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

#include <gccore.h>
#include <asndlib.h>
#include <string.h>
#include "wiiu_audio.h"

#define RATE 48000

/****************************************************************************
 * ASND voices
 ****************************************************************************/

typedef struct
{
  int active;
  int paused;
  int format;
  u32 step;              /* 16.16 source samples per output sample */
  u32 pos;               /* 16.16 position in the buffer */
  const u8 *snd;
  u32 len;               /* in source samples (frames) */
  const u8 *next;        /* buffer added by ASND_AddVoice */
  u32 next_len;
  int vol_l, vol_r;      /* 0..255 */
  ASNDVoiceCallback cb;
} voice_t;

static voice_t voices[MAX_SND_VOICES];
static SDL_AudioDeviceID dev;
static SDL_mutex *lock;
static int asnd_paused;

/* the game sound (stereo frames, in the order of the core: R, L with BLIP_INVERT) */
#define FIFO_FRAMES 16384
static s16 fifo[FIFO_FRAMES * 2];
static volatile u32 fifo_rd, fifo_wr;
static int stream_on;

static int frame_bytes(int format)
{
  switch (format)
  {
    case VOICE_MONO_8BIT:
    case VOICE_MONO_8BIT_U:     return 1;
    case VOICE_STEREO_8BIT:
    case VOICE_STEREO_8BIT_U:
    case VOICE_MONO_16BIT:
    case VOICE_MONO_16BIT_LE:   return 2;
    default:                    return 4;
  }
}

static void sample_at(const voice_t *v, u32 i, int *l, int *r)
{
  const u8 *p = v->snd + i * frame_bytes(v->format);
  switch (v->format)
  {
    case VOICE_MONO_8BIT:     *l = *r = ((s8)p[0]) << 8; break;
    case VOICE_MONO_8BIT_U:   *l = *r = (p[0] - 128) << 8; break;
    case VOICE_STEREO_8BIT:   *l = ((s8)p[0]) << 8; *r = ((s8)p[1]) << 8; break;
    case VOICE_STEREO_8BIT_U: *l = (p[0] - 128) << 8; *r = (p[1] - 128) << 8; break;
    case VOICE_MONO_16BIT:    *l = *r = (s16)((p[0] << 8) | p[1]); break;
    case VOICE_MONO_16BIT_LE: *l = *r = (s16)((p[1] << 8) | p[0]); break;
    case VOICE_STEREO_16BIT:  *l = (s16)((p[0] << 8) | p[1]); *r = (s16)((p[2] << 8) | p[3]); break;
    default:                  *l = (s16)((p[1] << 8) | p[0]); *r = (s16)((p[3] << 8) | p[2]); break;
  }
}

static void mix(void *user, Uint8 *stream, int len)
{
  s16 *out = (s16 *)stream;
  int frames = len / 4, i, n;
  static int acc[4096 * 2];
  (void)user;

  if (frames > 4096) frames = 4096;
  memset(acc, 0, frames * 2 * sizeof(int));

  /* game sound */
  if (stream_on)
  {
    for (i = 0; i < frames && fifo_rd != fifo_wr; i++)
    {
      u32 p = (fifo_rd % FIFO_FRAMES) * 2;
#ifdef BLIP_INVERT
      acc[i * 2]     = fifo[p + 1];
      acc[i * 2 + 1] = fifo[p];
#else
      acc[i * 2]     = fifo[p];
      acc[i * 2 + 1] = fifo[p + 1];
#endif
      fifo_rd++;
    }
  }

  /* ASND voices */
  SDL_LockMutex(lock);
  if (!asnd_paused)
  {
    for (n = 0; n < MAX_SND_VOICES; n++)
    {
      voice_t *v = &voices[n];
      if (!v->active || v->paused) continue;

      /* the voice handler asks for the next buffer while there is none */
      if (v->cb && !v->next)
        v->cb(n);
      if (!v->active) continue;

      for (i = 0; i < frames; i++)
      {
        int l, r;
        u32 idx = v->pos >> 16;
        if (idx >= v->len)
        {
          if (v->next)
          {
            v->snd = v->next;
            v->len = v->next_len;
            v->next = NULL;
            v->pos = 0;
            if (v->cb) v->cb(n);
            idx = 0;
            if (!v->len) { v->active = 0; break; }
          }
          else
          {
            v->active = 0;
            break;
          }
        }
        sample_at(v, idx, &l, &r);
        acc[i * 2]     += (l * v->vol_l) >> 8;
        acc[i * 2 + 1] += (r * v->vol_r) >> 8;
        v->pos += v->step;
      }
    }
  }
  SDL_UnlockMutex(lock);

  for (i = 0; i < frames * 2; i++)
  {
    int s = acc[i];
    out[i] = (s > 32767) ? 32767 : (s < -32768) ? -32768 : s;
  }
  if (frames * 4 < len)
    memset(stream + frames * 4, 0, len - frames * 4);
}

static void open_device(void)
{
  SDL_AudioSpec want, have;
  if (dev) return;
  if (!lock) lock = SDL_CreateMutex();
  SDL_InitSubSystem(SDL_INIT_AUDIO);
  SDL_zero(want);
  want.freq = RATE;
  want.format = AUDIO_S16SYS;
  want.channels = 2;
  want.samples = 512;
  want.callback = mix;
  dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
  if (dev) SDL_PauseAudioDevice(dev, 0);
}

void ASND_Init(void)
{
  open_device();
  SDL_LockMutex(lock);
  memset(voices, 0, sizeof(voices));
  asnd_paused = 0;
  SDL_UnlockMutex(lock);
}

void ASND_End(void)
{
  if (!lock) return;
  SDL_LockMutex(lock);
  memset(voices, 0, sizeof(voices));
  SDL_UnlockMutex(lock);
}

void ASND_Pause(s32 paused) { asnd_paused = paused; }
s32 ASND_Is_Paused(void) { return asnd_paused; }
s32 ASND_GetAudioRate(void) { return RATE; }

s32 ASND_SetVoice(s32 voice, s32 format, s32 pitch, s32 delay, void *snd, s32 size_snd, s32 volume_l, s32 volume_r, ASNDVoiceCallback callback)
{
  voice_t *v;
  (void)delay;
  if ((voice < 0) || (voice >= MAX_SND_VOICES) || (pitch < MIN_PITCH) || !snd) return SND_INVALID;
  open_device();
  SDL_LockMutex(lock);
  v = &voices[voice];
  memset(v, 0, sizeof(*v));
  v->format = format & 7;
  v->snd = (const u8 *)snd;
  v->len = size_snd / frame_bytes(v->format);
  v->step = (u32)(((u64)pitch << 16) / RATE);
  v->vol_l = volume_l;
  v->vol_r = volume_r;
  v->cb = callback;
  v->active = 1;
  SDL_UnlockMutex(lock);
  return SND_OK;
}

s32 ASND_SetInfiniteVoice(s32 voice, s32 format, s32 pitch, s32 delay, void *snd, s32 size_snd, s32 volume_l, s32 volume_r)
{
  return ASND_SetVoice(voice, format, pitch, delay, snd, size_snd, volume_l, volume_r, NULL);
}

s32 ASND_AddVoice(s32 voice, void *snd, s32 size_snd)
{
  voice_t *v;
  s32 ret = SND_OK;
  if ((voice < 0) || (voice >= MAX_SND_VOICES)) return SND_INVALID;
  SDL_LockMutex(lock);
  v = &voices[voice];
  if (!v->active) ret = SND_INVALID;
  else if (v->next) ret = SND_BUSY;
  else
  {
    v->next = (const u8 *)snd;
    v->next_len = size_snd / frame_bytes(v->format);
  }
  SDL_UnlockMutex(lock);
  return ret;
}

s32 ASND_StopVoice(s32 voice)
{
  if ((voice < 0) || (voice >= MAX_SND_VOICES)) return SND_INVALID;
  SDL_LockMutex(lock);
  voices[voice].active = 0;
  voices[voice].next = NULL;
  SDL_UnlockMutex(lock);
  return SND_OK;
}

s32 ASND_PauseVoice(s32 voice, s32 pause)
{
  if ((voice < 0) || (voice >= MAX_SND_VOICES)) return SND_INVALID;
  voices[voice].paused = pause;
  return SND_OK;
}

s32 ASND_StatusVoice(s32 voice)
{
  voice_t *v;
  if ((voice < 0) || (voice >= MAX_SND_VOICES)) return SND_INVALID;
  v = &voices[voice];
  if (!v->active) return SND_UNUSED;
  return (v->cb && !v->next) ? SND_WAITING : SND_WORKING;
}

/* voice 0 is kept for the music, as in libogc */
s32 ASND_GetFirstUnusedVoice(void)
{
  int i;
  for (i = 1; i < MAX_SND_VOICES; i++)
    if (!voices[i].active) return i;
  return SND_INVALID;
}

s32 ASND_ChangePitchVoice(s32 voice, s32 pitch)
{
  if ((voice < 0) || (voice >= MAX_SND_VOICES) || (pitch < MIN_PITCH)) return SND_INVALID;
  voices[voice].step = (u32)(((u64)pitch << 16) / RATE);
  return SND_OK;
}

s32 ASND_ChangeVolumeVoice(s32 voice, s32 volume_l, s32 volume_r)
{
  if ((voice < 0) || (voice >= MAX_SND_VOICES)) return SND_INVALID;
  voices[voice].vol_l = volume_l;
  voices[voice].vol_r = volume_r;
  return SND_OK;
}

s32 ASND_TestPointer(s32 voice, void *pointer)
{
  voice_t *v;
  if ((voice < 0) || (voice >= MAX_SND_VOICES)) return SND_INVALID;
  v = &voices[voice];
  if (!v->active) return 0;
  return ((const u8 *)pointer == v->snd) || ((const u8 *)pointer == v->next);
}

s32 ASND_TestVoiceBufferReady(s32 voice)
{
  if ((voice < 0) || (voice >= MAX_SND_VOICES)) return SND_INVALID;
  return voices[voice].active && !voices[voice].next;
}

/****************************************************************************
 * Game sound
 ****************************************************************************/

void wiiu_stream_start(void)
{
  open_device();
  fifo_rd = fifo_wr = 0;
  stream_on = 1;
}

void wiiu_stream_stop(void)
{
  stream_on = 0;
  fifo_rd = fifo_wr = 0;
}

u32 wiiu_stream_queued(void)
{
  return fifo_wr - fifo_rd;
}

void wiiu_stream_push(const s16 *samples, int frames)
{
  int i;
  for (i = 0; i < frames; i++)
  {
    if (wiiu_stream_queued() >= FIFO_FRAMES - 1)
      break; /* full (fast-forward): the rest is dropped */
    {
      u32 p = (fifo_wr % FIFO_FRAMES) * 2;
      fifo[p] = samples[i * 2];
      fifo[p + 1] = samples[i * 2 + 1];
      fifo_wr++;
    }
  }
}

/****************************************************************************
 * LWP queues (the OGG player thread)
 ****************************************************************************/

s32 LWP_InitQueue(lwpq_t *thequeue)
{
  SDL_sem *s = SDL_CreateSemaphore(0);
  if (!s) return -1;
  *thequeue = (lwpq_t)(u32)s;
  return 0;
}

void LWP_CloseQueue(lwpq_t thequeue)
{
  if (thequeue) SDL_DestroySemaphore((SDL_sem *)(u32)thequeue);
}

s32 LWP_ThreadSleep(lwpq_t thequeue)
{
  /* not forever: the player also polls */
  if (thequeue) SDL_SemWaitTimeout((SDL_sem *)(u32)thequeue, 50);
  return 0;
}

void LWP_ThreadSignal(lwpq_t thequeue)
{
  if (thequeue) SDL_SemPost((SDL_sem *)(u32)thequeue);
}
