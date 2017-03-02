/*
 * @note
 * Copyright(C) i4season U-Storage, 2016
 * Copyright(C) Szitman, 2016
 * All rights reserved.
 *
 */

#ifndef __USPROTOCOL_H_
#define __USPROTOCOL_H_

#include <ctype.h>
#include <stdio.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
#define min(x, y)				(((x) < (y)) ? (x) : (y))

#define htons(A)        ((((uint16_t)(A) & 0xff00) >> 8) | \
                                                   (((uint16_t)(A) & 0x00ff) << 8))

#define htonl(A)        ((((uint32_t)(A) & 0xff000000) >> 24) | \
                                                   (((uint32_t)(A) & 0x00ff0000) >> 8) | \
                                                   (((uint32_t)(A) & 0x0000ff00) << 8) | \
                                                   (((uint32_t)(A) & 0x000000ff) << 24))
#define ntohs(A)		htons(A)
#define ntohl(A)		htonl(A)


struct accessory_t {
	uint32_t aoa_version;
	uint16_t vid;
	uint16_t pid;
	char device[256];
	char manufacturer[64];
	char model[64];
	char description[256];
	char version[32];
	char url[1024];
	char serial[128];
};

typedef struct{
	struct accessory_t aoaConf;
	int iosPort;
}usConfig;


uint8_t usProtocol_init(usConfig *conf);
uint8_t usProtocol_PhoneDetect(usPhoneinfo *phone);
void usProtocol_PhoneRelease(usPhoneinfo *phone);
uint8_t usProtocol_SendPackage(usPhoneinfo *phone, void *buffer, uint32_t size);
uint8_t usProtocol_RecvPackage(usPhoneinfo *phone, uint8_t *buffer, uint32_t tsize, uint32_t *rsize);

#ifdef __cplusplus
}
#endif

#endif /* __USPROTOCOL_H_ */
