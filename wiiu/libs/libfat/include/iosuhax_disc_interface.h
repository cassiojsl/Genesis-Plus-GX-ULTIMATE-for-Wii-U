/* Genesis Plus GX ULTIMATE - Wii U port: this libfat was written for
   libiosuhax; libmocha (Aroma) gives the same disc interfaces */
#ifndef _IOSUHAX_DISC_INTERFACE_H_
#define _IOSUHAX_DISC_INTERFACE_H_
#include <mocha/disc_interface.h>
typedef uint32_t sec_t;
#define IOSUHAX_sdio_disc_interface Mocha_sdio_disc_interface
#define IOSUHAX_usb_disc_interface  Mocha_usb_disc_interface
#endif
