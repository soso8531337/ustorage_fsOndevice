/*
 * @note
 * Copyright(C) i4season U-Storage, 2016
 * Copyright(C) Szitman, 2016
 * All rights reserved.
 *
 */


#ifndef __USDISK_H_
#define __USDISK_H_

#include <ctype.h>
#include <stdio.h>
#include <stdint.h>
#include "protocol.h"

#ifdef __cplusplus
extern "C" {
#endif


int32_t usDisk_init(void);
void usDisk_PlugCallBack(int action, char *dev);
int32_t usDisk_diskRead(char filename[MAX_FILENAME_SIZE], 
						void *buff, int64_t offset, int32_t size);

int32_t usDisk_diskWrite(char filename[MAX_FILENAME_SIZE], 
						void *buff, int64_t offset, int32_t size);

int32_t usDisk_diskCreate(char abspath[MAX_FILENAME_SIZE], 
						int8_t isdir, int32_t actime, int32_t modtime);

#ifdef __cplusplus
}
#endif

#endif /* __USBDISK_H_ */

