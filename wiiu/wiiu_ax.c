/****************************************************************************
 *  Genesis Plus GX ULTIMATE - Wii U port
 *
 *  Sound output on AX, the sound library of the Wii U (48 kHz stereo).
 *
 *  Two looping voices (left, right) play two ring buffers of 8192 samples.
 *  A thread on the core of AX is woken by AX each audio frame (3 ms) and
 *  writes the mixed sound ahead of the play position of the voices, about
 *  32 ms ahead. If it ever falls behind, it starts again ahead of the play
 *  position: the ring never holds old sound, only silence or new sound.
 ****************************************************************************/

#ifdef __WIIU__

#include <string.h>
#include <malloc.h>
#include <coreinit/cache.h>
#include <coreinit/thread.h>
#include <coreinit/semaphore.h>
#include <sndcore2/core.h>
#include <sndcore2/voice.h>
#include "wiiu_ax.h"

#define RING     8192            /* samples per channel */
#define LEAD     1536            /* samples written ahead of the play position */
#define CHUNK    256             /* samples mixed at once */

static int16_t *ring[2];
static AXVoice *voice[2];
static uint32_t wr;              /* next sample to write, 0..RING-1 */
static OSSemaphore tick;
static OSThread thread;
static uint8_t *stack;
static volatile int running;
static wiiu_ax_fill_fn fill;

static void frame_cb(void)
{
  OSSignalSemaphore(&tick);
}

static uint32_t play_pos(void)
{
  AXVoiceOffsets o;
  AXGetVoiceOffsets(voice[0], &o);
  return o.currentOffset % RING;
}

static void write_chunk(int n)
{
  static int16_t lr[CHUNK * 2];
  int i, done = 0;

  fill(lr, n);
  while (done < n)
  {
    int run = n - done;
    if (run > (int)(RING - wr)) run = RING - wr;
    for (i = 0; i < run; i++)
    {
      ring[0][wr + i] = lr[(done + i) * 2];
      ring[1][wr + i] = lr[(done + i) * 2 + 1];
    }
    DCFlushRange(&ring[0][wr], run * 2);
    DCFlushRange(&ring[1][wr], run * 2);
    wr = (wr + run) % RING;
    done += run;
  }
}

static int mixer(int argc, void *argv)
{
  (void)argc; (void)argv;
  while (running)
  {
    uint32_t play, ahead;

    OSWaitSemaphore(&tick);
    if (!running) break;

    play = play_pos();
    ahead = (wr - play + RING) % RING;

    /* behind the play position (or far from it): start again ahead of it */
    if (ahead > LEAD * 2)
    {
      wr = (play + CHUNK) % RING;
      ahead = CHUNK;
    }

    while (ahead < LEAD)
    {
      int n = LEAD - ahead;
      if (n > CHUNK) n = CHUNK;
      write_chunk(n);
      ahead += n;
    }
  }
  return 0;
}

static void setup_voice(int ch)
{
  AXVoiceVeData vol = { .volume = 0x8000 };
  AXVoiceDeviceMixData mix[6];
  AXVoiceOffsets o;

  voice[ch] = AXAcquireVoice(31, NULL, NULL);
  if (!voice[ch]) return;

  AXVoiceBegin(voice[ch]);
  AXSetVoiceType(voice[ch], 0);
  AXSetVoiceVe(voice[ch], &vol);

  memset(mix, 0, sizeof(mix));
  mix[ch].bus[0].volume = 0x8000;   /* left voice -> left channel, right -> right */
  AXSetVoiceDeviceMix(voice[ch], AX_DEVICE_TYPE_TV, 0, mix);
  AXSetVoiceDeviceMix(voice[ch], AX_DEVICE_TYPE_DRC, 0, mix);

  AXSetVoiceSrcRatio(voice[ch], 48000.0f / (float)AXGetInputSamplesPerSec());
  AXSetVoiceSrcType(voice[ch], AX_VOICE_SRC_TYPE_LINEAR);

  memset(&o, 0, sizeof(o));
  o.dataType = AX_VOICE_FORMAT_LPCM16;
  o.loopingEnabled = AX_VOICE_LOOP_ENABLED;
  o.loopOffset = 0;
  o.endOffset = RING - 1;
  o.currentOffset = 0;
  o.data = ring[ch];
  AXSetVoiceOffsets(voice[ch], &o);
  AXSetVoiceState(voice[ch], AX_VOICE_STATE_PLAYING);
  AXVoiceEnd(voice[ch]);
}

/* runs on the core of AX (CPU 1, the main core), as AX asks */
static int start_on_core(int argc, void *argv)
{
  AXInitParams p = { .renderer = AX_INIT_RENDERER_48KHZ, .pipeline = AX_INIT_PIPELINE_SINGLE };
  (void)argc; (void)argv;

  if (!AXIsInit())
    AXInitWithParams(&p);

  setup_voice(0);
  setup_voice(1);
  if (!voice[0] || !voice[1])
    return -1;

  wr = CHUNK;
  OSInitSemaphore(&tick, 0);
  AXRegisterAppFrameCallback(frame_cb);
  return 0;
}

int wiiu_ax_start(wiiu_ax_fill_fn fn)
{
  OSThread t;
  static uint8_t init_stack[16 * 1024] __attribute__((aligned(16)));
  int ret = -1;
  const int stack_size = 64 * 1024;

  if (running) return 0;
  fill = fn;

  ring[0] = memalign(64, RING * 2);
  ring[1] = memalign(64, RING * 2);
  if (!ring[0] || !ring[1]) return -1;
  memset(ring[0], 0, RING * 2);
  memset(ring[1], 0, RING * 2);
  DCFlushRange(ring[0], RING * 2);
  DCFlushRange(ring[1], RING * 2);

  /* AX is set up from the main core */
  if (OSCreateThread(&t, start_on_core, 0, NULL, init_stack + sizeof(init_stack), sizeof(init_stack),
                     OSGetThreadPriority(OSGetCurrentThread()), OS_THREAD_ATTRIB_AFFINITY_CPU1))
  {
    OSResumeThread(&t);
    OSJoinThread(&t, &ret);
  }
  if (ret) return -1;

  /* the mixer: on the core of AX, above the threads of the app */
  stack = memalign(16, stack_size);
  if (!stack) return -1;
  running = 1;
  if (!OSCreateThread(&thread, mixer, 0, NULL, stack + stack_size, stack_size,
                      10, OS_THREAD_ATTRIB_AFFINITY_CPU1))
  {
    running = 0;
    return -1;
  }
  OSResumeThread(&thread);
  return 0;
}

void wiiu_ax_stop(void)
{
  int ret, i;
  if (!running) return;
  running = 0;
  OSSignalSemaphore(&tick);
  OSJoinThread(&thread, &ret);
  AXDeregisterAppFrameCallback(frame_cb);
  for (i = 0; i < 2; i++)
  {
    if (voice[i])
    {
      AXSetVoiceState(voice[i], AX_VOICE_STATE_STOPPED);
      AXFreeVoice(voice[i]);
      voice[i] = NULL;
    }
  }
  AXQuit();
}

#endif
