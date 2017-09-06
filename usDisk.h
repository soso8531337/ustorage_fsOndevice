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

#define DISK_PLUGIN		1
#define DISK_PLUGOUT	0
struct nofiyStruct{
	uint8_t valid;
	char dev[16];
	uint8_t event;
	int32_t invokTime;
};
typedef int (*readDirCallBack)(void *arg, char *filename, int flag);

int32_t usDisk_init(void);
int32_t usDisk_getNotifyInfo(struct nofiyStruct *notify);
void usDisk_PlugCallBack(int action, char *dev);
int32_t usDisk_diskRead(char filename[MAX_FILENAME_SIZE], 
						void *buff, int64_t offset, int32_t size, int32_t *trueRead);

int32_t usDisk_diskWrite(char filename[MAX_FILENAME_SIZE], 
						void *buff, int64_t offset, int32_t size, int32_t *trueWrite);

int32_t usDisk_diskCreate(char abspath[MAX_FILENAME_SIZE], 
						int8_t isdir, int32_t actime, int32_t modtime);

int32_t usDisk_diskList(char *dirname, readDirCallBack dirCallback, void *arg);
int32_t usDisk_diskLun(void *buff, int32_t size, int32_t *used);
int32_t usDisk_diskInfo(void *buff, int32_t size, int32_t *used);



#ifdef __cplusplus
}
#endif

#endif /* __USBDISK_H_ */

