/*
 * @note
 * Copyright(C) i4season U-Storage, 2016
 * Copyright(C) Szitman, 2016
 * All rights reserved.
 *
 */


#ifndef __USDECODE_H_
#define __USDECODE_H_

#include <ctype.h>
#include <stdio.h>
#include <stdint.h>
#include "protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

int32_t usDecode_BasicMagicHandle(struct uStorPro_headInfo *proHeader);
int32_t usDecode_ReadHandle(struct uStorPro_fsOnDev *proHeader, uint8_t *readHeader, uint32_t buffSize);
int32_t usDecode_WriteHandle(struct uStorPro_fsOnDev *proHeader, uint8_t *writeHeader, uint32_t payloadSize);
int32_t usDecode_CreateHandle(struct uStorPro_fsOnDev *proHeader, uint8_t *creatPtr, uint32_t ptrLen);
int32_t usDecode_DeleteHandle(struct uStorPro_fsOnDev *proHeader, uint8_t *delPtr, uint32_t ptrLen);
int32_t usDecode_MoveHandle(struct uStorPro_fsOnDev *proHeader, uint8_t *MovPtr, uint32_t ptrLen);

int32_t readDirInvoke(void *arg, char *filename, int flag);
int32_t usDecode_DiskLunHandle(struct uStorPro_fsOnDev *proHeader, uint8_t *DiskPtr, uint32_t payLen);
int32_t usDecode_DiskInfoHandle(struct uStorPro_fsOnDev *proHeader, uint8_t *DiskPtr, uint32_t payLen);
int32_t usDecode_GetFileInfoHandle(struct uStorPro_fsOnDev *proHeader, uint8_t *FilePtr, uint32_t payLen);

int32_t usDecode_GetFirmwareInfoHandle(struct uStorPro_fsOnDev *proHeader, uint8_t *FrimwarePtr, uint32_t payLen);
int32_t usDecode_SyncCacheHandle(struct uStorPro_fsOnDev *proHeader, uint8_t *payLoad, uint32_t payLen);
int32_t usDecode_SyncSystemTimeHandle(struct uStorPro_fsOnDev *proHeader, uint8_t *payLoad, uint32_t payLen);
int32_t usDecode_UpgradeFirmwareHandle(struct uStorPro_fsOnDev *proHeader, uint8_t *frimwarePtr, uint32_t payLen);




#ifdef __cplusplus
}
#endif

#endif /* __USDECODE_H_ */

