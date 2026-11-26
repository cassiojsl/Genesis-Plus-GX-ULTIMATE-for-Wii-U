/****************************************************************************
 *  Genesis Plus GX ULTIMATE - Wii U port
 *  The sound of the game, mixed with the ASND voices (wiiu_asnd.c)
 ****************************************************************************/

#ifndef _WIIU_AUDIO_H_
#define _WIIU_AUDIO_H_

void wiiu_stream_start(void);
void wiiu_stream_stop(void);
void wiiu_stream_push(const s16 *samples, int frames);  /* stereo frames */
u32  wiiu_stream_queued(void);                          /* frames not played yet */

#endif
