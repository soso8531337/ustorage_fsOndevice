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
#include "usUsb.h"

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

#define IOS_INTERBUF_SIZE	(48*1024)

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

typedef struct{
	uint8_t version; /*Protocol version*/
	uint16_t irx_seq; /*itunes protocol rx seq*/
	uint16_t itx_seq;	/*itunes protocol tx seq*/
	/*tcp connection information*/
	uint16_t sport, dport;
	uint32_t tx_seq, tx_ack, tx_acked, tx_win;
	uint32_t rx_seq, rx_recvd, rx_ack, rx_win;
	int flags;	
	usbInfo usbIOS;
	uint8_t interBuf[IOS_INTERBUF_SIZE]; /*Internel buffer, used for small data send/receive*/
}mux_itunes;


typedef struct {
	uint8_t phoneType;	/*Android or IOS*/
	union{
		mux_itunes phoneIOS;
		usbInfo phoneAndroid;
	}phoneInfo;
	
}usPhoneinfo;

int32_t usProtocol_init(usConfig *conf);
int32_t usProtocol_PhoneDetect(usPhoneinfo *phone);
void usProtocol_PhoneRelease(usPhoneinfo *phone);
int32_t usProtocol_SendPackage(usPhoneinfo *phone, void *buffer, uint32_t size);
int32_t usProtocol_RecvPackage(usPhoneinfo *phone, uint8_t *buffer, uint32_t tsize, uint32_t *rsize);

#ifdef __cplusplus
}
#endif

#endif /* __USPROTOCOL_H_ */
