#ifndef __USFRIMWARE_H_
#define __USFRIMWARE_H_

#include <ctype.h>
#include <stdio.h>
#include <stdint.h>
#include "protocol.h"

#ifdef __cplusplus
extern "C" {
#endif


int32_t usFirmware_GetInfo(void *buff, int32_t size, int32_t *used);
int usFirmware_FirmwareUP(void *buff, int32_t paySize, int32_t ctrid);










#ifdef __cplusplus
}
#endif

#endif

