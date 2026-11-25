#ifndef _OSD_H_
#define _OSD_H_

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <stdlib.h>

#include "main.h"
#include "config.h"
#include "error.h"
#include "unzip.h"
#include "fileio.h"

#define osd_input_update wiiu_input_update

/* SD card root as mounted by wut's devoptab */
#define WIIU_SD_ROOT  "fs:/vol/external01"
#define WIIU_APP_DIR  WIIU_SD_ROOT "/wiiu/apps/genplusgx"
#define WIIU_ROM_DIR  WIIU_SD_ROOT "/roms"

#define GG_ROM      WIIU_APP_DIR "/ggenie.bin"
#define AR_ROM      WIIU_APP_DIR "/areplay.bin"
#define SK_ROM      WIIU_APP_DIR "/sk.bin"
#define SK_UPMEM    WIIU_APP_DIR "/sk2chip.bin"
#define CD_BIOS_US  WIIU_APP_DIR "/bios_CD_U.bin"
#define CD_BIOS_EU  WIIU_APP_DIR "/bios_CD_E.bin"
#define CD_BIOS_JP  WIIU_APP_DIR "/bios_CD_J.bin"
#define MD_BIOS     WIIU_APP_DIR "/bios_MD.bin"
#define MS_BIOS_US  WIIU_APP_DIR "/bios_U.sms"
#define MS_BIOS_EU  WIIU_APP_DIR "/bios_E.sms"
#define MS_BIOS_JP  WIIU_APP_DIR "/bios_J.sms"
#define GG_BIOS     WIIU_APP_DIR "/bios.gg"

#endif /* _OSD_H_ */
