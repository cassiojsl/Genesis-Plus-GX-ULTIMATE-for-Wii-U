#ifndef WIIU_AX_H
#define WIIU_AX_H

#include <stdint.h>

/* fills n stereo frames (left, right), interleaved */
typedef void (*wiiu_ax_fill_fn)(int16_t *lr, int n);

int  wiiu_ax_start(wiiu_ax_fill_fn fn);
void wiiu_ax_stop(void);

#endif
