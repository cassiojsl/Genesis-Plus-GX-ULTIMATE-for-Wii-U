/****************************************************************************
 *  Genesis Plus GX ULTIMATE - Wii U port
 *
 *  libogc's ogc/lwp_watchdog.h, with gettime() in ticks of the Wii time
 *  base (60.75 MHz): the Wii U time base runs at 62.15625 MHz, so the time
 *  the Wii code measures (diff_usec... the 32X frame skip) would be 2.3%
 *  too long otherwise.
 ****************************************************************************/

#ifndef _WIIU_LWP_WATCHDOG_H
#define _WIIU_LWP_WATCHDOG_H

#define gettime wiiu_ogc_gettime_raw
#include_next <ogc/lwp_watchdog.h>
#undef gettime

/* 60.75 / 62.15625 = 1944 / 1989 */
static inline u64 gettime(void)
{
  u64 t = wiiu_ogc_gettime_raw();
  return (t / 1989) * 1944 + ((t % 1989) * 1944) / 1989;
}

#endif
