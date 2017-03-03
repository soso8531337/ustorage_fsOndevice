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

uint8_t usDecode_BasicMagicHandle(struct uStorPro_headInfo *proHeader);
uint8_t usDecode_ReadHandle(uStorPro_fsOnDev *proHeader, uint8_t *readHeader, uint32_t buffSize);
uint8_t usDecode_WriteHandle(uStorPro_fsOnDev *proHeader, uint8_t *writeHeader, uint32_t payloadSize);
uint8_t usDecode_CreateHandle(uStorPro_fsOnDev *proHeader, uint8_t *creatPtr, uint32_t ptrLen);
uint8_t usDecode_DeleteHandle(uStorPro_fsOnDev *proHeader, uint8_t *delPtr, uint32_t ptrLen);
uint8_t usDecode_MoveHandle(uStorPro_fsOnDev *proHeader, uint8_t *MovPtr, uint32_t ptrLen);








#ifdef __cplusplus
}
#endif

#endif /* __USDECODE_H_ */

