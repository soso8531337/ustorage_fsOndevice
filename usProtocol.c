/*
 * @brief U-Storage Project
 *
 * @note
 * Copyright(C) i4season, 2016
 * Copyright(C) Szitman, 2016
 * All rights reserved.
 */

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libusb-1.0/libusb.h>
#include <sys/ioctl.h>
#include <errno.h>
#include "usProtocol.h"
#include "usUsb.h"
#include "usSys.h"
#include "protocol.h"
#include "usError.h"

/*****************************************************************************
 * Private types/enumerations/variables
 ****************************************************************************/



/*****************************************************************************
 * Public types/enumerations/variables
 ****************************************************************************/
/**********IOS Itunes***********/
#define VID_APPLE 0x5ac
#define PID_RANGE_LOW 0x1290
#define PID_RANGE_MAX 0x12af
/**********Android AOA***********/
/* Product IDs / Vendor IDs */
#define AOA_ACCESSORY_VID		0x18D1	/* Google */
#define AOA_ACCESSORY_PID		0x2D00	/* accessory */
#define AOA_ACCESSORY_ADB_PID		0x2D01	/* accessory + adb */
#define AOA_AUDIO_PID			0x2D02	/* audio */
#define AOA_AUDIO_ADB_PID		0x2D03	/* audio + adb */
#define AOA_ACCESSORY_AUDIO_PID		0x2D04	/* accessory + audio */
#define AOA_ACCESSORY_AUDIO_ADB_PID	0x2D05	/* accessory + audio + adb */
#define INTERFACE_CLASS_AOA 255 // Referrance http://www.usb.org/developers/defined_class/#BaseClassFFh
#define INTERFACE_SUBCLASS_AOA 255
/* Android Open Accessory protocol defines */
#define AOA_GET_PROTOCOL		51
#define AOA_SEND_IDENT			52
#define AOA_START_ACCESSORY		53
#define AOA_REGISTER_HID		54
#define AOA_UNREGISTER_HID		55
#define AOA_SET_HID_REPORT_DESC		56
#define AOA_SEND_HID_EVENT		57
#define AOA_AUDIO_SUPPORT		58
/* String IDs */
#define AOA_STRING_MAN_ID		0
#define AOA_STRING_MOD_ID		1
#define AOA_STRING_DSC_ID		2
#define AOA_STRING_VER_ID		3
#define AOA_STRING_URL_ID		4
#define AOA_STRING_SER_ID		5
/* Android Open Accessory protocol Filter Condition */
#define AOA_FTRANS_CLASS					0xff
#define AOA_FTRANS_SUBCLASS			0xff
#define AOA_FTRANS_PROTOCOL			0x00
/* IOS Itunes protocol Filter Condition */
#define IOS_FTRANS_CLASS					0xff
#define IOS_FTRANS_SUBCLASS			0xfe
#define IOS_FTRANS_PROTOCOL			0x02
/*TCP */
#define TH_FIN        0x01
#define TH_SYN        0x02
#define TH_RST        0x04
#define TH_PUSH       0x08
#define TH_ACK        0x10
#define TH_URG        0x20


typedef enum{
	OCTL_DIS_USB_SW = 0X01,
	OCTL_ENABLE_USB_SW,
} USBPowerValue;

#define USB_POWER_CONFIG		"/dev/vs_control"


/*max transmission packet size
// libusb fragments these too, but doesn't send ZLPs so we're safe
// but we need to send a ZLP ourselves at the end (see usb-linux.c)
// we're using 3 * 16384 to optimize for the fragmentation
// this results in three URBs per full transfer, 32 USB packets each
// if there are ZLP issues this should make them show up easily too*/
#define USB_MTU_IOS			(3 * 16384)
//#define IOS_MAX_PACKET		(45*1024)
#define IOS_MAX_PACKET		(32764) /*capture usb package*/
#define IOS_WIN_SIZE				131072 /*Must Not change this value*/


#define PACKAGE_TIMEOUT		5000			/*send or receive package timeout*/

#ifndef IPPROTO_TCP
#define IPPROTO_TCP 		6
#endif


enum mux_protocol {
	MUX_PROTO_VERSION = 0,
	MUX_PROTO_CONTROL = 1,
	MUX_PROTO_SETUP = 2,
	MUX_PROTO_TCP = IPPROTO_TCP,
};


enum{
	PRO_IOS = 1,
	PRO_ANDROID
};


struct tcphdr{
	uint16_t th_sport;         /* source port */
	uint16_t th_dport;         /* destination port */
	uint32_t th_seq;             /* sequence number */
	uint32_t th_ack;             /* acknowledgement number */
	uint8_t th_x2:4;           /* (unused) */
	uint8_t th_off:4;          /* data offset */
	uint8_t th_flags;
	uint16_t th_win;           /* window */
	uint16_t th_sum;           /* checksum */
	uint16_t th_urp;           /* urgent pointer */
};

struct version_header
{
	uint32_t major;
	uint32_t minor;
	uint32_t padding;
};

struct mux_header
{
	uint32_t protocol;
	uint32_t length;
	uint32_t magic;
	uint16_t tx_seq;
	uint16_t rx_seq;
};

static usConfig globalConf = {
	.aoaConf = {
		.manufacturer = "i4season",
		.model = "U-Storage",
		.description = "U-Storage",
		.version = "1.0",
		.url = "https://www.simicloud.com/download/index.html",
		.serial = "0000000012345678",
	},
	.iosPort = 5555,
};



/*****************************************************************************/
/**************************Private functions**********************************/
/*****************************************************************************/
static uint16_t find_sport(void)
{
	static uint16_t tcport =1;
	if(tcport == 0xFFFF){
		DEBUG("Port Reach To Max Reset it..\r\n");
		tcport = 0;
	}
	return (++tcport);
}
/*
********************BECAREFUL**********************
*Used ios struct interel buffer to send ios package
*Payload limit IOS_INTERBUF_SIZE
*/
static int32_t itunes_InterMemSendPacket(mux_itunes *iosDev, enum mux_protocol proto, 
								void *header, const void *data, int length)
{
	int hdrlen;
	int res;	
	uint32_t trueSend = 0;

	if(!iosDev){
		return EUSTOR_ARG;
	}
	
	switch(proto) {
		case MUX_PROTO_VERSION:
			hdrlen = sizeof(struct version_header);
			break;
		case MUX_PROTO_SETUP:
			hdrlen = 0;
			break;
		case MUX_PROTO_TCP:
			hdrlen = sizeof(struct tcphdr);
			break;
		default:
			DEBUG("Invalid protocol %d for outgoing packet (hdr %p data %p len %d)\r\n", proto, header, data, length);	
			return EUSTOR_PRO_PGEPRO;
	}
	DEBUG("send_packet(0x%x, %p, %p, %d)\r\n", proto, header, data, length);

	int mux_header_size = ((iosDev->version < 2) ? 8 : sizeof(struct mux_header));
	int total = mux_header_size + hdrlen + length;

	if(total > sizeof(iosDev->interBuf)){
		DEBUG("Tried to send setup packet larger than %dBytes (hdr %d data %d total %d) to device\n", 
							sizeof(iosDev->interBuf), hdrlen, length, total);
		return EUSTOR_MEM;
	}
	struct mux_header *mhdr = (struct mux_header *)(iosDev->interBuf);
	mhdr->protocol = htonl(proto);
	mhdr->length = htonl(total);
	if (iosDev->version >= 2) {
		mhdr->magic = htonl(0xfeedface);
		if (proto == MUX_PROTO_SETUP) {
			iosDev->itx_seq = 0;
			iosDev->irx_seq = 0xFFFF;
		}
		mhdr->tx_seq = htons(iosDev->itx_seq);
		mhdr->rx_seq = htons(iosDev->irx_seq);
		iosDev->itx_seq++;
	}
	memcpy(iosDev->interBuf+ mux_header_size, header, hdrlen);
	if(data && length)
		memcpy(iosDev->interBuf+ mux_header_size + hdrlen, data, length);
	
	if((res = usUsb_BlukPacketSend(&(iosDev->usbIOS), iosDev->interBuf, 
						total, &trueSend, PACKAGE_TIMEOUT)) != EUSTOR_OK){
		DEBUG("usb_send failed while sending packet (len %d-->%d) to device: %d\r\n", 
							total, trueSend, res);
		return res;
	}
	if(total % 512 == 0){
		DEBUG("Send ZLP.....\n");
		 usUsb_BlukPacketSend(&(iosDev->usbIOS), iosDev->interBuf, 
						0, NULL, PACKAGE_TIMEOUT);
	}
	
	DEBUG("sending packet ok(len %d) to device: successful\r\n", total);
	
	return EUSTOR_OK;
}

/*
********************BECAREFUL**********************
*decrease memcpy  to send ios package
*iosHeaderAddress: ios protocol header memory address
*payload: payload address
*/
static int32_t itunes_SendPacket(mux_itunes *iosDev, enum mux_protocol proto, 
							void *header, const void *iosHeaderAddress, const void *payload, int length)
{
	uint32_t trueSend = 0;
	int hdrlen;
	int res;
	struct mux_header *mhdr =NULL;
	uint8_t *ptrpay = NULL;

	if(!iosDev){
		return EUSTOR_ARG;
	}

	switch(proto) {
		case MUX_PROTO_VERSION:
			hdrlen = sizeof(struct version_header);
			break;
		case MUX_PROTO_SETUP:
			hdrlen = 0;
			break;
		case MUX_PROTO_TCP:
			hdrlen = sizeof(struct tcphdr);
			break;
		default:
			DEBUG("Invalid protocol %d for outgoing packet (hdr %p data %p len %d)\r\n", proto, header, payload, length);	
			return EUSTOR_PRO_PGEPRO;
	}
	DEBUG("send_packet(0x%x, %p, %p, %d)\r\n", proto, header, payload, length);

	int mux_header_size = ((iosDev->version < 2) ? 8 : sizeof(struct mux_header));
	int total = mux_header_size + hdrlen + length;

	/*Used internel memory to send*/
	if(!iosHeaderAddress && total > sizeof(iosDev->interBuf)){
		DEBUG("Tried to send setup packet larger than %dBytes (hdr %d data %d total %d) to device\n", 
							sizeof(iosDev->interBuf), hdrlen, length, total);
		return EUSTOR_MEM;
	}
	if(iosHeaderAddress){
		mhdr = (struct mux_header *)(iosHeaderAddress); 	
		ptrpay = (uint8_t*)iosHeaderAddress;
	}else{
		mhdr = (struct mux_header *)(iosDev->interBuf); 	
		ptrpay = iosDev->interBuf;
	}
	mhdr->protocol = htonl(proto);
	mhdr->length = htonl(total);
	if (iosDev->version >= 2) {
		mhdr->magic = htonl(0xfeedface);
		if (proto == MUX_PROTO_SETUP) {
			iosDev->itx_seq = 0;
			iosDev->irx_seq = 0xFFFF;
		}
		mhdr->tx_seq = htons(iosDev->itx_seq);
		mhdr->rx_seq = htons(iosDev->irx_seq);
		iosDev->itx_seq++;
	}
	memcpy(ptrpay+ mux_header_size, header, hdrlen);
	/*judge iosHeaderAddress is equal payload*/
	if(iosHeaderAddress &&
			(iosHeaderAddress+mux_header_size + hdrlen) == payload){
		DEBUG("No Need to Memcpy[Good]\n");
	}else if(iosHeaderAddress == NULL && length && payload){
		memcpy(ptrpay+ mux_header_size + hdrlen, payload, length);
	}else{
		DEBUG("We Need to Send IOS Header First\n");
		if((res = usUsb_BlukPacketSend(&(iosDev->usbIOS), ptrpay, 
							total, &trueSend, PACKAGE_TIMEOUT)) != EUSTOR_OK){
			DEBUG("usb_send failed while sending packet (len %d-->%d) to device: %d\r\n", 
								mux_header_size + hdrlen, trueSend, res);
			return res;
		}		
		ptrpay = (uint8_t*)payload;
		total = length;
	}
	
	if(total && (res = usUsb_BlukPacketSend(&(iosDev->usbIOS), ptrpay, 
						total, &trueSend, PACKAGE_TIMEOUT)) != EUSTOR_OK){
		DEBUG("usb_send failed while sending packet (len %d-->%d) to device: %d\r\n", 
							total, trueSend, res);
		return res;
	}
	if(total && total % 512 == 0){
		DEBUG("Send ZLP.....\n");
		 usUsb_BlukPacketSend(&(iosDev->usbIOS), ptrpay, 
						0, NULL, PACKAGE_TIMEOUT);
	}
	
	DEBUG("sending packet ok(len %d) to device: successful\r\n", total);
	
	return EUSTOR_OK;

}

static int32_t itunes_SendTCP(mux_itunes *iosDev, uint8_t flags, 
				const uint8_t *iosHeaderAddress, const uint8_t *payload, int length)
{
	struct tcphdr *th = NULL;
	struct mux_header *mhdr =NULL;	
	uint32_t trueSend = 0;	
	uint8_t *ptrpay = NULL;
	int res;

	if(!iosDev){
		return EUSTOR_ARG;
	}

	DEBUG("[OUT]sport=%d dport=%d seq=%d ack=%d flags=0x%x window=%d[%d] len=%d\r\n",
				iosDev->sport, iosDev->dport, 
				iosDev->tx_seq, iosDev->tx_ack, flags, 
				iosDev->tx_win, iosDev->tx_win >> 8, length);

	uint8_t mux_header_size = ((iosDev->version < 2) ? 8 : sizeof(struct mux_header));
	int total = mux_header_size + sizeof(struct tcphdr) + length;
	int hdrlen = sizeof(struct tcphdr);

	/*Used internel memory to send*/
	if(!iosHeaderAddress && total > sizeof(iosDev->interBuf)){
		DEBUG("Tried to send setup packet larger than %dBytes (hdr %d data %d total %d) to device\n", 
							sizeof(iosDev->interBuf), hdrlen, length, total);
		return EUSTOR_MEM;
	}
	if(iosHeaderAddress){
		mhdr = (struct mux_header *)(iosHeaderAddress); 	
		ptrpay = (uint8_t*)iosHeaderAddress;
	}else{
		mhdr = (struct mux_header *)(iosDev->interBuf); 	
		ptrpay = iosDev->interBuf;
	}
	/*set muxd header*/
	mhdr->protocol = htonl(MUX_PROTO_TCP);
	mhdr->length = htonl(total);
	if (iosDev->version >= 2) {
		mhdr->magic = htonl(0xfeedface);
		mhdr->tx_seq = htons(iosDev->itx_seq);
		mhdr->rx_seq = htons(iosDev->irx_seq);
		iosDev->itx_seq++;
	}
	/*Set tcp header*/
	th = (struct tcphdr *)(ptrpay+ mux_header_size);
	memset(th, 0, sizeof(struct tcphdr));
	th->th_sport = htons(iosDev->sport);
	th->th_dport = htons(iosDev->dport);
	th->th_seq = htonl(iosDev->tx_seq);
	th->th_ack = htonl(iosDev->tx_ack);
	th->th_flags = flags;
	th->th_off = sizeof(th) / 4;
	th->th_win = htons(iosDev->tx_win >> 8);
	
	/*judge iosHeaderAddress is equal payload*/
	if(iosHeaderAddress &&
			(iosHeaderAddress+mux_header_size + hdrlen) == payload){
		DEBUG("No Need to Memcpy[Good]\n");
	}else if(iosHeaderAddress == NULL && length && payload){
		memcpy(ptrpay+ mux_header_size + hdrlen, payload, length);
	}else{
		DEBUG("We Need to Send IOS Header First\n");
		if((res = usUsb_BlukPacketSend(&(iosDev->usbIOS), ptrpay, 
							total, &trueSend, PACKAGE_TIMEOUT)) != EUSTOR_OK){
			DEBUG("usb_send failed while sending packet (len %d-->%d) to device: %d\r\n", 
								mux_header_size + hdrlen, trueSend, res);
			return res;
		}		
		ptrpay = (uint8_t*)payload;
		total = length;
	}
	
	if(total && (res = usUsb_BlukPacketSend(&(iosDev->usbIOS), ptrpay, 
						total, &trueSend, PACKAGE_TIMEOUT)) != EUSTOR_OK){
		DEBUG("usb_send failed while sending packet (len %d-->%d) to device: %d\r\n", 
							total, trueSend, res);
		return res;
	}
	iosDev->tx_acked = iosDev->tx_ack;
	if(total && total % 512 == 0){
		DEBUG("Send ZLP.....\n");
		 usUsb_BlukPacketSend(&(iosDev->usbIOS), ptrpay, 
						0, NULL, PACKAGE_TIMEOUT);
	}
	
	DEBUG("sending packet ok(len %d) to device: successful\r\n", total);
	
	return EUSTOR_OK;
}

static int32_t itunes_SendTCPAck(mux_itunes *iosDev)
{
	return itunes_SendTCP(iosDev, TH_ACK, NULL, NULL, 0);
}

static int32_t itunes_getIOSProVersion(mux_itunes *iosDev)
{
	struct version_header rvh, *vh;	
	uint8_t mux_header_size; 
	uint8_t cbuffer[512] = {0};
	uint32_t trueRecv = 0;
	int res;
	struct mux_header *itunesHeader = NULL;
	
	if(!iosDev){
		return EUSTOR_ARG;
	}
	if(iosDev->version == 1 || iosDev->version == 2){
		DEBUG("Have Get itunes Protocol Verison[%d]\n", iosDev->version);
		return EUSTOR_OK;
	}
	/*Reset version header*/
	iosDev->version = 0;
	iosDev->itx_seq = iosDev->irx_seq = 0;
	/*Reset sequence*/
	iosDev->tx_seq = iosDev->tx_ack = iosDev->tx_acked = 0;
	iosDev->rx_seq = iosDev->rx_ack = iosDev->rx_recvd = iosDev->rx_win = 0; 
	/*init tcp header*/
	iosDev->sport = find_sport();
	iosDev->dport= globalConf.iosPort;
	iosDev->tx_win = IOS_WIN_SIZE;
	
	/*1.request PROTOCOL_VERSION*/
	rvh.major = htonl(2);
	rvh.minor = htonl(0);
	rvh.padding = 0;

	res = itunes_InterMemSendPacket(iosDev, MUX_PROTO_VERSION, &rvh, NULL, 0);
	if(res != EUSTOR_OK) {
		DEBUG("Error sending version request packet to device\r\n");
		return res;
	}
	/*Send Successful receive response*/
	mux_header_size = ((iosDev->version < 2) ? 8 : sizeof(struct mux_header));
	res = usUsb_BlukPacketReceive(&(iosDev->usbIOS), cbuffer,  
				sizeof(cbuffer), &trueRecv, PACKAGE_TIMEOUT);
	if(res != EUSTOR_OK){
		DEBUG("Error receive version request packet from phone\r\n");
		return res;
	}
	vh = (struct version_header *)(cbuffer+mux_header_size);
	vh->major = ntohl(vh->major);
	vh->minor = ntohl(vh->minor);
	if(vh->major != 2 && vh->major != 1) {
		DEBUG("Device has unknown version %d.%d\r\n", vh->major, vh->minor);
		return EUSTOR_PRO_PROVER;
	}
	iosDev->version = vh->major;

	if (iosDev->version >= 2 &&
		(res = itunes_InterMemSendPacket(iosDev, MUX_PROTO_SETUP, NULL, "\x07", 1)) != EUSTOR_OK) {
		DEBUG("iPhone Send SetUP Package Failed\n");
		return res;
	}
	
	DEBUG("IOS Version Get Successful[ver.%d]\r\n", iosDev->version);
	
	return EUSTOR_OK;
}

static void itunes_ControlPackage(unsigned char *payload, uint32_t payload_length)
{
	if (payload_length > 0) {
		switch (payload[0]) {
		case 3:
			if (payload_length > 1){
				DEBUG("usProtocol_iosControlInput ERROR 3:");
				usUsb_PrintStr((uint8_t*)(payload+1), payload_length-1);
			}else{
				DEBUG("Error occured, but empty error message\n");
			}
			break;
		case 7:
			if (payload_length > 1){
				DEBUG("usProtocol_iosControlInput ERROR 7:");
				usUsb_PrintStr((uint8_t*)(payload+1), payload_length-1);
			}
			break;
		default:			
			DEBUG("usProtocol_iosControlInput ERROR %d:", payload[0]);			
			usUsb_PrintStr((uint8_t*)(payload+1), payload_length-1);
			break;
		}
	} else {
		DEBUG("got a type 1 packet without payload\n");
	}
}

static int32_t itunes_ReceiveAck(mux_itunes *iosDev)
{
	uint8_t buffer[512] = {0};
	uint8_t *payload = NULL;
	uint32_t payload_length, actual_length;
	struct tcphdr *th;
	int res;
	
	if(!iosDev){
		return EUSTOR_ARG;
	}
	if((res = usUsb_BlukPacketReceive(&(iosDev->usbIOS), buffer, 
					sizeof(buffer), &actual_length, 1000)) != EUSTOR_OK){
		DEBUG("Receive ios Package ACK [%d]\n", res);
		return res;
	}
	/*decode ack*/
	struct mux_header *mhdr =  (struct mux_header *)buffer;	
	int mux_header_size = ((iosDev->version < 2) ? 8 : sizeof(struct mux_header));
	
	if (iosDev->version >= 2) {
		iosDev->irx_seq = ntohs(mhdr->rx_seq);
	}		
	if(ntohl(mhdr->protocol) == MUX_PROTO_CONTROL){			
		payload = (unsigned char *)(mhdr+1);
		payload_length = actual_length - mux_header_size;
		itunes_ControlPackage(payload, payload_length);
		return EUSTOR_PRO_REVACK;
	}else if(ntohl(mhdr->protocol) == MUX_PROTO_VERSION){
		DEBUG("Receive ios Package MUX_PROTO_VERSION[Error]\n");
		return EUSTOR_PRO_REVACK;
	}
	if(actual_length != mux_header_size + sizeof(struct tcphdr)){
		DEBUG("Receive ios ACK Package Failed[%d/%d]\r\n", 
									actual_length, mux_header_size);
		return EUSTOR_PRO_REVACK;
	}
	/*We need to decode tcp header*/			
	th = (struct tcphdr *)((char*)mhdr+mux_header_size);
	
	iosDev->rx_seq = ntohl(th->th_seq);
	iosDev->rx_ack = ntohl(th->th_ack);
	iosDev->rx_win = ntohs(th->th_win) << 8;

	return EUSTOR_OK;
}

static void itunes_ResetReceive(mux_itunes *iosDev)
{
	uint8_t rc;

	if(!iosDev){
		return ;
	}
	int mux_header_size = ((iosDev->version < 2) ? 8 : sizeof(struct mux_header));
	
	while(iosDev->rx_ack != iosDev->tx_seq){
		uint8_t rstbuf[512] = {0};
		uint32_t actual_length;			
		struct tcphdr *th;
		
		DEBUG("Reset Opeartion Need To receive RX_ACK[%u<--->%u]\r\n", 
						iosDev->rx_ack, iosDev->tx_seq);
		if((rc = usUsb_BlukPacketReceive(&(iosDev->usbIOS), rstbuf, 
								sizeof(rstbuf), &actual_length, 1000)) != EUSTOR_OK){
			if(rc == EUSTOR_USB_OFFLINE){
				DEBUG("Device Disconncet\n");
			}else if(rc == EUSTOR_USB_TIMEOUT){
				DEBUG("Reset Receive Package Finish[%d]\n", rc);
			}	
			return;
		}
		struct mux_header *mhdr =  (struct mux_header *)rstbuf;
		if (iosDev->version >= 2) {
			iosDev->irx_seq = ntohs(mhdr->rx_seq);
		}
		if(actual_length < mux_header_size + sizeof(struct tcphdr)){
			DEBUG("Receive ios Package is Too Small TCP Packet[%d/%d]\n", 
					actual_length, mux_header_size);
			continue;
		}

		/*We need to decode tcp header*/			
		th = (struct tcphdr *)((char*)mhdr+mux_header_size);

		iosDev->rx_seq = ntohl(th->th_seq);
		iosDev->rx_ack = ntohl(th->th_ack);
		iosDev->rx_win = ntohs(th->th_win) << 8;
		DEBUG("[IN][RESET]sport=%d dport=%d seq=%d ack=%d flags=0x%x window=%d[%d]len=%u\n",
					ntohs(th->th_sport), ntohs(th->th_dport),
					iosDev->rx_seq, iosDev->rx_ack, th->th_flags, 
					iosDev->rx_win, iosDev->rx_win >> 8, ntohl(mhdr->length));
	}
	DEBUG("Reset Finish iPhone Device[v/p=%d:%d]\r\n", 
				iosDev->usbIOS.vid, iosDev->usbIOS.pid); 	
}

static int32_t aoa_SendProPackage(usbInfo *aoaDev, void *buffer, uint32_t size)
{
	uint32_t actual_length = 0;
	uint32_t already = 0;
	uint8_t *curBuf = NULL;
	uint8_t rc;

	if(!buffer || !size){
		DEBUG("usUsb_BlukPacketSend Error Parameter:%p Size:%d\r\n", 
							buffer, size);		
		return EUSTOR_ARG;
	}

	curBuf = (uint8_t *)buffer;
	while(already < size){		
		uint32_t sndSize = 0, freeSize = 0;
		freeSize = size-already;
		if(freeSize % 512 == 0){
			sndSize = freeSize-1;
		}else{
			sndSize = freeSize;
		}
		if((rc = usUsb_BlukPacketSend(aoaDev, curBuf+already, 
						sndSize, &actual_length, PACKAGE_TIMEOUT)) != EUSTOR_OK){
			DEBUG("usUsb_BlukPacketSend Error[%d]:%p sndSize:%d already:%d\r\n", 
						rc, buffer, sndSize, already);		
			return rc;
		}
		already+= actual_length;
		DEBUG("usUsb_BlukPacketSend Successful:%p sndSize:%d already:%d\r\n", 
					buffer, sndSize, already);		
	}

	return EUSTOR_OK;
}

static int32_t aoa_RecvProPackage(usbInfo *aoaDev, uint8_t* buffer, 
										uint32_t tsize, uint32_t *rsize)
{
	uint8_t rc;
	
	if(!aoaDev || !buffer || !buffer || !rsize){
		return EUSTOR_ARG;
	}

	rc = usUsb_BlukPacketReceive(aoaDev, buffer, tsize, rsize, PACKAGE_TIMEOUT);
	if(rc != EUSTOR_OK){
		DEBUG("Receive aoa Package Error:%d\r\n",rc);
		return rc;
	}
	DEBUG("Receive aoa Package-->buffer:%p ExceptRecvsize:%d TrueRecvsize:%d\r\n",
							buffer, tsize, *rsize);

	return EUSTOR_OK;
}

/*
*Please reserved ios header before buffer
*We think it reserved when you invoke the function
*/
static int32_t itunes_SendProPackage(mux_itunes *iosDev, void *buffer, uint32_t size)
{	
	uint8_t *tbuffer = (uint8_t *)buffer;
	uint32_t  sndSize = 0, curSize = 0, rx_win = 0;
	int res, headOffset = 0;
	
	if(!buffer || !size || !iosDev){
		return EUSTOR_ARG;
	}
	
	headOffset = ((iosDev->version < 2) ? 8 : sizeof(struct mux_header))+sizeof(struct tcphdr);
	rx_win = iosDev->rx_win;
	while(curSize < size){
		if(!rx_win){
			DEBUG("Peer Windows is full  wait ACK[win:%u/%u]\n", rx_win, iosDev->rx_win);
			if((res = itunes_ReceiveAck(iosDev)) != EUSTOR_OK){
				DEBUG("Wait ACK Error[%d]\n", rx_win);
				return res;
			}
			rx_win = iosDev->rx_win;
			DEBUG("Wait ACK Successful[win:%u]\n", rx_win);
		}
		if(size-curSize >= IOS_MAX_PACKET){
			sndSize = IOS_MAX_PACKET;
		}else{
			sndSize = size-curSize;
		}
		sndSize = min(sndSize, rx_win);
		rx_win -= sndSize;
		/*Send ios package*/
		if((res = itunes_SendTCP(iosDev, TH_ACK, (tbuffer+curSize-headOffset), 
					tbuffer+curSize, sndSize)) != EUSTOR_OK){
			DEBUG("itunes_SendProPackage Error[%d]:%p Headoffset:%d Current:%d Size:%d\n",
					res, buffer, headOffset, curSize, sndSize);
			return res;
		}
		iosDev->tx_seq += sndSize;		
		curSize += sndSize;
		DEBUG("itunes_SendProPackage Successful:%p Size:%d curSize:%d Win:%d\r\n", 
				buffer, sndSize, curSize, rx_win);		
	}

	return EUSTOR_OK;
}


static int32_t itunes_RecvProPackage(mux_itunes *iosDev, uint8_t* buffer, 
											uint32_t tsize, uint32_t *rsize)
{
	uint8_t *payload = NULL;	
	uint32_t actual_length = 0, avalen, packageTotal = 0, packageCurrent;
	uint32_t payload_length;
	uint8_t rc;
	
	if(!iosDev || !buffer || !rsize){
		return EUSTOR_ARG;
	}

	avalen = sizeof(iosDev->interBuf);	
	int mux_header_size = ((iosDev->version < 2) ? 8 : sizeof(struct mux_header));
	
	rc = usUsb_BlukPacketReceive(&(iosDev->usbIOS), iosDev->interBuf, 
									avalen, &actual_length, PACKAGE_TIMEOUT);
	if(rc != EUSTOR_OK){
		DEBUG("Recvive Package Error:%d\n", rc);
		return rc;
	}
	/*Decode ios package header*/
	struct mux_header *mhdr = (struct mux_header *)(iosDev->interBuf);
	if (iosDev->version >= 2) {
		iosDev->irx_seq = ntohs(mhdr->rx_seq);
	}
	
	if(ntohl(mhdr->protocol) == MUX_PROTO_CONTROL){ 		
		itunes_ControlPackage((unsigned char *)(mhdr+1), 
					actual_length - mux_header_size);
		return EUSTOR_PRO_PACKAGE;
	}else if(ntohl(mhdr->protocol) == MUX_PROTO_VERSION){
		DEBUG("Receive ios Package MUX_PROTO_VERSION[Error]\n");
		return EUSTOR_PRO_PACKAGE;
	}
	if(actual_length < mux_header_size){
		DEBUG("Receive ios Package is Too Small[%d/%d]\r\n", 
							actual_length, mux_header_size);
		return EUSTOR_PRO_PACKAGE;
	}

	packageTotal = ntohl(mhdr->length);
	if(packageTotal > sizeof(iosDev->interBuf)){
		DEBUG("ios Package too Big..[%u/%u]\n", packageTotal, avalen);
		return EUSTOR_PRO_PACKAGE;
	}
	avalen -= actual_length;
	packageCurrent = actual_length;
	while(packageCurrent < packageTotal){
		/*loop utile to recevice finish*/
		rc = usUsb_BlukPacketReceive(&(iosDev->usbIOS), iosDev->interBuf+packageCurrent, 
						avalen, &actual_length, PACKAGE_TIMEOUT);
		if(rc != EUSTOR_OK){
			DEBUG("Recvive Package Error:%d\n", rc);
			return rc;
		}		
		avalen -= actual_length;		
		packageCurrent += actual_length;
	}
	DEBUG("Recevie ios Package successful[%u]...\n", packageTotal);
	/*Send ack*/
	
	/*Decode tcp header*/
	struct tcphdr *th = (struct tcphdr *)(iosDev->interBuf+mux_header_size);
	iosDev->rx_seq = ntohl(th->th_seq);
	iosDev->rx_ack = ntohl(th->th_ack);
	iosDev->rx_win = ntohs(th->th_win) << 8;
	payload = (unsigned char *)(th+1);
	payload_length = packageTotal - mux_header_size- sizeof(struct tcphdr);
	

	DEBUG("[IN]sport=%d dport=%d seq=%d ack=%d flags=0x%x window=%d[%d]len=%u\r\n",
				ntohs(th->th_sport), ntohs(th->th_dport),
				iosDev->rx_seq, iosDev->rx_ack, th->th_flags, 
				iosDev->rx_win, iosDev->rx_win >> 8, packageTotal);
	
	if(th->th_flags & TH_RST ||
			th->th_flags != TH_ACK) {
		/*Connection Reset*/
		DEBUG("Connection Reset:\n");
		usUsb_PrintStr(payload, payload_length);
		itunes_ResetReceive(iosDev);
		return EUSTOR_PRO_PACKAGE;
	}

	if(payload_length){
		char ackbuf[512] = {0};
		DEBUG("Send ACK[Package Finish ack:%u]\r\n", iosDev->tx_ack);
		itunes_SendTCP(iosDev, TH_ACK, ackbuf, NULL, 0);
		memcpy(buffer, payload, payload_length);
	}
	iosDev->tx_ack += payload_length;
	
	*rsize = payload_length;
	
	DEBUG("Receive IOS Package Finish-->buffer:%p Recvsize:%d\n", buffer, *rsize);

	return EUSTOR_OK;
}

static int32_t LINUX_ConnectIOSPhone(mux_itunes *iosInfo)
{
	uint8_t mux_header_size; 
	uint32_t trueRecv = 0;
	struct tcphdr *th;
	uint8_t cbuffer[512] = {0};
	int res;

	if(!iosInfo){
		return EUSTOR_ARG;
	}

	if((res = itunes_getIOSProVersion(iosInfo)) != EUSTOR_OK){
		return res;
	}

	DEBUG("Connected to v%d device\r\n", iosInfo->version);
	/*Send TH_SYNC*/
	if((res = itunes_SendTCP(iosInfo, TH_SYN, NULL, NULL, 0)) != EUSTOR_OK){
		DEBUG("Error sending TCP SYN to device (%d->%d)\n", 
				iosInfo->sport, iosInfo->dport);
		return res; //bleh
	}
	/*Wait TH_ACK*/
	if((res = usUsb_BlukPacketReceive(&(iosInfo->usbIOS), cbuffer, 
				sizeof(cbuffer), &trueRecv, PACKAGE_TIMEOUT)) != EUSTOR_OK){
		DEBUG("Error receive tcp ack response packet from phone\n");
		return res;
	}
	DEBUG("ACK Step1: Receive (%dBytes)\n", trueRecv);	
	struct mux_header *mhdr = (struct mux_header *)cbuffer;
	if(ntohl(mhdr->length) > (sizeof(cbuffer))){
		DEBUG("Setup Package is More than %u/%dByte\n", 
				ntohl(mhdr->length), sizeof(cbuffer));
		return EUSTOR_PRO_CONNECT;
	}
	/*Decode Package*/
	switch(ntohl(mhdr->protocol)) {
		case MUX_PROTO_VERSION:
			DEBUG("Error MUX_PROTO_VERSION Protocol Received\n");
			return EUSTOR_PRO_CONNECT;
		case MUX_PROTO_CONTROL:
			DEBUG("Receive MUX_PROTO_CONTROL[SameThing Happen] Continue Read TCP Packet...\n");		
			if(usUsb_BlukPacketReceive(&(iosInfo->usbIOS), cbuffer, 
					sizeof(cbuffer), &trueRecv, PACKAGE_TIMEOUT)){
				DEBUG("Error receive tcp ack response packet from phone\n");
				return EUSTOR_PRO_CONNECT;
			}
			mhdr = (struct mux_header *)cbuffer;
			/*Not Break continue to decode*/
		case MUX_PROTO_TCP:			
			mux_header_size = ((iosInfo->version < 2) ? 8 : sizeof(struct mux_header));
			th = (struct tcphdr *)((char*)mhdr+mux_header_size);
			iosInfo->rx_seq = ntohl(th->th_seq);
			iosInfo->rx_ack = ntohl(th->th_ack);
			iosInfo->rx_win = ntohs(th->th_win) << 8;
			DEBUG("[IN]sport=%d dport=%d seq=%d ack=%d flags=0x%x window=%d[%d] len=%u\n",
						ntohs(th->th_sport), ntohs(th->th_dport), 
						iosInfo->rx_seq, iosInfo->rx_ack, th->th_flags, 
						iosInfo->rx_win, iosInfo->rx_win >> 8, trueRecv);

			
			if(th->th_flags != (TH_SYN|TH_ACK)) {
				if(th->th_flags & TH_RST){
					DEBUG("Connection refused by device(%d->%d)\r\n", 
								iosInfo->sport , iosInfo->dport);
				}		
				return EUSTOR_PRO_CONNECT;
			} else {			
				iosInfo->tx_seq++;
				iosInfo->tx_ack++;
				iosInfo->rx_recvd = iosInfo->rx_seq;
				if((res = itunes_SendTCP(iosInfo, TH_ACK, NULL, NULL, 0)) != EUSTOR_OK) {
					DEBUG("Error sending TCP ACK to device(%d->%d)\n", 
						iosInfo->sport , iosInfo->dport);
					return res;
				}
			}	
			break;
		default:
			DEBUG("Incoming packet has unknown protocol 0x%x)\n", ntohl(mhdr->protocol));
			return EUSTOR_PRO_CONNECT;
	}

	DEBUG("Successful Connect To iPhone(%d->%d)\n", 
				iosInfo->sport , iosInfo->dport);
	
	return EUSTOR_OK;
}

static int32_t LINUX_USBPowerControl(USBPowerValue value)
{
	int fd;

	if(access(USB_POWER_CONFIG, F_OK)){
		DEBUG("No Need To Setup USB Power[%s Not Exist]\r\n", USB_POWER_CONFIG);
		return EUSTOR_OK;
	}

	fd= open(USB_POWER_CONFIG, O_RDWR | O_NONBLOCK);
	if (fd < 0 && errno == EROFS)
		fd = open(USB_POWER_CONFIG, O_RDONLY | O_NONBLOCK);
	if (fd<0){
		DEBUG("Open %s Failed:%s", USB_POWER_CONFIG, strerror(errno));
		return EUSTOR_DISK_OPEN; 
	}
	if(ioctl(fd, value)){
		DEBUG("IOCTL Failed:%s", strerror(errno));
	}
	close(fd);
	DEBUG("IOCTL Successful:%d", value);
	return EUSTOR_OK;
}

static int32_t LINUX_SwitchAOAMode(libusb_device* dev, struct accessory_t aoaConfig)
{
	int res=-1, j;
	libusb_device_handle *handle;
	struct libusb_config_descriptor *config;
	uint8_t version[2];
	uint8_t bus = libusb_get_bus_number(dev);
	uint8_t address = libusb_get_device_address(dev);

	// potentially blocking operations follow; they will only run when new devices are detected, which is acceptable
	if((res = libusb_open(dev, &handle)) != 0) {
		DEBUG("Could not open device %d-%d: %d\n", bus, address, res);
		return EUSTOR_USB_CONTROL;
	}
	if((res = libusb_get_active_config_descriptor(dev, &config)) != 0) {
		DEBUG("Could not get configuration descriptor for device %d-%d: %d\n", bus, address, res);
		libusb_close(handle);
		return EUSTOR_USB_CONTROL;
	}
	
	for(j=0; j<config->bNumInterfaces; j++) {
		const struct libusb_interface_descriptor *intf = &config->interface[j].altsetting[0];
		/*We Just limit InterfaceClass, limit InterfaceSubClass may be lost sanxing huawei device*/
		if(config->bNumInterfaces > 1 &&
					intf->bInterfaceClass != INTERFACE_CLASS_AOA){
			continue;
		}
		/*Before switch AOA Mode, we need to notify kernel*/
		LINUX_USBPowerControl(OCTL_DIS_USB_SW);
		/* Now asking if device supports Android Open Accessory protocol */
		res = libusb_control_transfer(handle,
					      LIBUSB_ENDPOINT_IN |
					      LIBUSB_REQUEST_TYPE_VENDOR,
					      AOA_GET_PROTOCOL, 0, 0, version,
					      sizeof(version), 0);
		if (res < 0) {
			DEBUG("Could not getting AOA protocol %d-%d: %d\n", bus, address, res);
			libusb_free_config_descriptor(config);
			libusb_close(handle);
			return EUSTOR_USB_CONTROL;
		}else{
			aoaConfig.aoa_version = ((version[1] << 8) | version[0]);
			DEBUG("Device[%d-%d] supports AOA %d.0!\n", bus, address, aoaConfig.aoa_version);
		}
		/* In case of a no_app accessory, the version must be >= 2 */
		if((aoaConfig.aoa_version < 2) && !aoaConfig.manufacturer) {
			DEBUG("Connecting without an Android App only for AOA 2.0[%d-%d]\n", bus,address);
			libusb_free_config_descriptor(config);
			libusb_close(handle);
			return EUSTOR_PRO_SETAOA;
		}
		if(aoaConfig.manufacturer) {
			DEBUG("sending manufacturer: %s\n", aoaConfig.manufacturer);
			res = libusb_control_transfer(handle,
						  LIBUSB_ENDPOINT_OUT
						  | LIBUSB_REQUEST_TYPE_VENDOR,
						  AOA_SEND_IDENT, 0,
						  AOA_STRING_MAN_ID,
						  (uint8_t *)aoaConfig.manufacturer,
						  strlen(aoaConfig.manufacturer) + 1, 0);
			if(res < 0){
				DEBUG("Could not Set AOA manufacturer %d-%d: %d\n", bus, address, res);
				libusb_free_config_descriptor(config);
				libusb_close(handle);
				return EUSTOR_PRO_SETAOA;
			}
		}
		if(aoaConfig.model) {
			DEBUG("sending model: %s\n", aoaConfig.model);
			res = libusb_control_transfer(handle,
						  LIBUSB_ENDPOINT_OUT
						  | LIBUSB_REQUEST_TYPE_VENDOR,
						  AOA_SEND_IDENT, 0,
						  AOA_STRING_MOD_ID,
						  (uint8_t *)aoaConfig.model,
						  strlen(aoaConfig.model) + 1, 0);
			if(res < 0){
				DEBUG("Could not Set AOA model %d-%d: %d\n", bus, address, res);
				libusb_free_config_descriptor(config);
				libusb_close(handle);
				return EUSTOR_PRO_SETAOA;
			}
		}
		
		DEBUG("sending description: %s\n", aoaConfig.description);
		res = libusb_control_transfer(handle,
					  LIBUSB_ENDPOINT_OUT
					  | LIBUSB_REQUEST_TYPE_VENDOR,
					  AOA_SEND_IDENT, 0,
					  AOA_STRING_DSC_ID,
					  (uint8_t *)aoaConfig.description,
					  strlen(aoaConfig.description) + 1, 0);
		if(res < 0){
			DEBUG("Could not Set AOA description %d-%d: %d\n", bus, address, res);
			libusb_free_config_descriptor(config);
			libusb_close(handle);
			return EUSTOR_PRO_SETAOA;
		}
		DEBUG("sending version string: %s\n", aoaConfig.version);
		res = libusb_control_transfer(handle,
					  LIBUSB_ENDPOINT_OUT
					  | LIBUSB_REQUEST_TYPE_VENDOR,
					  AOA_SEND_IDENT, 0,
					  AOA_STRING_VER_ID,
					  (uint8_t *)aoaConfig.version,
					  strlen(aoaConfig.version) + 1, 0);
		if(res < 0){
			DEBUG("Could not Set AOA version %d-%d: %d\n", bus, address, res);
			libusb_free_config_descriptor(config);
			libusb_close(handle);
			return EUSTOR_PRO_SETAOA;
		}
		DEBUG("sending url string: %s\n", aoaConfig.url);
		res = libusb_control_transfer(handle,
					  LIBUSB_ENDPOINT_OUT
					  | LIBUSB_REQUEST_TYPE_VENDOR,
					  AOA_SEND_IDENT, 0,
					  AOA_STRING_URL_ID,
					  (uint8_t *)aoaConfig.url,
					  strlen(aoaConfig.url) + 1, 0);
		if(res < 0){
			DEBUG("Could not Set AOA url %d-%d: %d\n", bus, address, res);
			libusb_free_config_descriptor(config);
			libusb_close(handle);
			return EUSTOR_PRO_SETAOA;
		}
		DEBUG("sending serial number: %s\n", aoaConfig.serial);
		res = libusb_control_transfer(handle,
					  LIBUSB_ENDPOINT_OUT
					  | LIBUSB_REQUEST_TYPE_VENDOR,
					  AOA_SEND_IDENT, 0,
					  AOA_STRING_SER_ID,
					  (uint8_t *)aoaConfig.serial,
					  strlen(aoaConfig.serial) + 1, 0);
		if(res < 0){
			DEBUG("Could not Set AOA serial %d-%d: %d\n", bus, address, res);
			libusb_free_config_descriptor(config);
			libusb_close(handle);
			return EUSTOR_PRO_SETAOA;
		}
		res = libusb_control_transfer(handle,
					  LIBUSB_ENDPOINT_OUT |
					  LIBUSB_REQUEST_TYPE_VENDOR,
					  AOA_START_ACCESSORY, 0, 0, NULL, 0, 0);
		if(res < 0){
			DEBUG("Could not Start AOA %d-%d: %d\n", bus, address, res);
			libusb_free_config_descriptor(config);
			libusb_close(handle);
			return EUSTOR_PRO_SETAOA;
		}
		DEBUG("Turning the device %d-%d in Accessory mode Successful\n", bus, address);
		libusb_free_config_descriptor(config);
		libusb_close(handle);
		return EUSTOR_OK;
	}	
	
	libusb_free_config_descriptor(config);
	libusb_close(handle);
	DEBUG("No Found Android Device in %d-%d\n", bus, address);

	return EUSTOR_PRO_NOAOA;
}




/*****************************************************************************/
/**************************Public functions***********************************/
/*****************************************************************************/

int32_t usProtocol_init(usConfig *conf)
{
	/*Init IOS transfer port and AOA app information*/
	if(conf){
		memcpy(&globalConf, conf, sizeof(usConfig));
	}
	return EUSTOR_OK;
}

int32_t usProtocol_PhoneDetect(usPhoneinfo *phone)
{
	int cnt, i, res, j;
	libusb_device **devs;	
	int8_t PhoneType = -1;
	usbInfo phoneTmp;

	/*For safe we need to release first*/
	usProtocol_PhoneRelease(phone);

	memset(phone, 0, sizeof(usPhoneinfo));
	
	cnt = libusb_get_device_list(NULL, &devs);
	if(cnt < 0){
		DEBUG("Get Device List Failed.\n");
		return EUSTOR_PRO_LIST;
	}
	for(i=0; i<cnt; i++) {
		libusb_device *dev = devs[i];		
		struct libusb_device_descriptor devdesc;
		uint8_t bus = libusb_get_bus_number(dev);
		uint8_t address = libusb_get_device_address(dev);
		if((res = libusb_get_device_descriptor(dev, &devdesc)) != 0) {
			DEBUG("Could not get device descriptor for device %d-%d: %d\n", bus, address, res);
			continue;
		}
		memset(&phoneTmp, 0, sizeof(phoneTmp));
		if(devdesc.idVendor == VID_APPLE &&
			(devdesc.idProduct >= PID_RANGE_LOW && devdesc.idProduct <= PID_RANGE_MAX)){
			DEBUG("Found IOS device  v/p %04x:%04x at %d-%d\n", 
					devdesc.idVendor, devdesc.idProduct, bus, address);
			PhoneType = PRO_IOS;
		}else if(devdesc.idVendor == AOA_ACCESSORY_VID &&
			(devdesc.idProduct >= AOA_ACCESSORY_PID && devdesc.idProduct <= AOA_ACCESSORY_AUDIO_ADB_PID)){
			DEBUG("Found Android AOA device  v/p %04x:%04x at %d-%d\n", 
					devdesc.idVendor, devdesc.idProduct, bus, address);
			PhoneType = PRO_ANDROID;
		}else{
			DEBUG("Try To Switch Android AOA Mode  v/p %04x:%04x at %d-%d\n", 
						devdesc.idVendor, devdesc.idProduct, bus, address);
			LINUX_SwitchAOAMode(dev, globalConf.aoaConf);
			continue;
		}
		libusb_device_handle *handle;
		DEBUG("Found new device with v/p %04x:%04x at %d-%d\n", devdesc.idVendor, devdesc.idProduct, bus, address);
		// potentially blocking operations follow; they will only run when new devices are detected, which is acceptable
		if((res = libusb_open(dev, &handle)) != 0) {
			DEBUG("Could not open device %d-%d: %d\n", bus, address, res);
			continue;
		}
		
		int current_config = 0;
		if((res = libusb_get_configuration(handle, &current_config)) != 0) {
			DEBUG("Could not get configuration for device %d-%d: %d\n", bus, address, res);
			libusb_close(handle);
			continue;
		}
		if (current_config != devdesc.bNumConfigurations) {
			struct libusb_config_descriptor *config;
			if((res = libusb_get_active_config_descriptor(dev, &config)) != 0) {
				DEBUG("Could not get old configuration descriptor for device %d-%d: %d\n", bus, address, res);
			} else {
				for(j=0; j<config->bNumInterfaces; j++) {
					const struct libusb_interface_descriptor *intf = &config->interface[j].altsetting[0];
					if((res = libusb_kernel_driver_active(handle, intf->bInterfaceNumber)) < 0) {
						DEBUG("Could not check kernel ownership of interface %d for device %d-%d: %d\n", intf->bInterfaceNumber, bus, address, res);
						continue;
					}
					if(res == 1) {
						DEBUG("Detaching kernel driver for device %d-%d, interface %d\n", bus, address, intf->bInterfaceNumber);
						if((res = libusb_detach_kernel_driver(handle, intf->bInterfaceNumber)) < 0) {
							DEBUG("Could not detach kernel driver (%d), configuration change will probably fail!\n", res);
							continue;
						}
					}
				}
				libusb_free_config_descriptor(config);
			}
		
			DEBUG("Setting configuration for device %d-%d, from %d to %d\n", bus, address, current_config, devdesc.bNumConfigurations);
			if((res = libusb_set_configuration(handle, devdesc.bNumConfigurations)) != 0) {
				DEBUG("Could not set configuration %d for device %d-%d: %d\n", devdesc.bNumConfigurations, bus, address, res);
				libusb_close(handle);
				continue;
			}
		}
		
		struct libusb_config_descriptor *config;
		if((res = libusb_get_active_config_descriptor(dev, &config)) != 0) {
			DEBUG("Could not get configuration descriptor for device %d-%d: %d\n", bus, address, res);
			libusb_close(handle);
			continue;
		}
		
		for(j=0; j<config->bNumInterfaces; j++) {
			const struct libusb_interface_descriptor *intf = &config->interface[j].altsetting[0];
			if(PhoneType == PRO_IOS &&
				   (intf->bInterfaceClass != IOS_FTRANS_CLASS ||
				   intf->bInterfaceSubClass != IOS_FTRANS_SUBCLASS ||
				   intf->bInterfaceProtocol != IOS_FTRANS_PROTOCOL)){
				continue;
			}else if(PhoneType == PRO_ANDROID&&
				   intf->bInterfaceClass != INTERFACE_CLASS_AOA){
				continue;
			}
			if(intf->bNumEndpoints != 2) {
				DEBUG("Endpoint count mismatch for interface %d of device %d-%d\n", intf->bInterfaceNumber, bus, address);
				continue;
			}
			if((intf->endpoint[0].bEndpointAddress & 0x80) == LIBUSB_ENDPOINT_OUT &&
			   (intf->endpoint[1].bEndpointAddress & 0x80) == LIBUSB_ENDPOINT_IN) {
			   
				phoneTmp.interface=intf->bInterfaceNumber;
				phoneTmp.ep_out = intf->endpoint[0].bEndpointAddress;
				phoneTmp.ep_in = intf->endpoint[1].bEndpointAddress;

				DEBUG("Found interface %d with endpoints %02x/%02x for device %d-%d\n", 
						intf->bInterfaceNumber, intf->endpoint[0].bEndpointAddress, intf->endpoint[1].bEndpointAddress, bus, address);
				break;
			} else if((intf->endpoint[1].bEndpointAddress & 0x80) == LIBUSB_ENDPOINT_OUT &&
					  (intf->endpoint[0].bEndpointAddress & 0x80) == LIBUSB_ENDPOINT_IN){

				phoneTmp.interface=intf->bInterfaceNumber;
				phoneTmp.ep_out = intf->endpoint[0].bEndpointAddress;
				phoneTmp.ep_in = intf->endpoint[1].bEndpointAddress;

				DEBUG("Found interface %d with swapped endpoints %02x/%02x for device %d-%d\n", 
							phoneTmp.interface, phoneTmp.ep_out, phoneTmp.ep_in, bus, address);
				break;
			} else {
				DEBUG("Endpoint type mismatch for interface %d of device %d-%d\n", intf->bInterfaceNumber, bus, address);
			}
		}
		
		if(j == config->bNumInterfaces){
			DEBUG("Could not find a suitable USB interface for device %d-%d\n", bus, address);
			libusb_free_config_descriptor(config);
			libusb_close(handle);
			continue;
		}	
		libusb_free_config_descriptor(config);
		
		if((res = libusb_claim_interface(handle, phoneTmp.interface)) != 0) {
			DEBUG("Could not claim interface %d for device %d-%d: %d\n", phoneTmp.interface, bus, address, res);
			libusb_close(handle);
			continue;
		}

		phoneTmp.osPriv = (void*)handle;
		phoneTmp.bus = bus; 				
		phoneTmp.address = address;
		phoneTmp.wMaxPacketSize = libusb_get_max_packet_size(dev, phoneTmp.ep_out);
		if (phoneTmp.wMaxPacketSize <= 0) {
			DEBUG("Could not determine wMaxPacketSize for device %d-%d, setting to 64\n", bus, address);
			phoneTmp.wMaxPacketSize = 64;
		} else {
			DEBUG("Using wMaxPacketSize=%d for device %d-%d\n", phoneTmp.wMaxPacketSize, bus, address);
		}
		
		phoneTmp.vid = devdesc.idVendor;
		phoneTmp.pid = devdesc.idProduct;
		
		/*set usbdev*/		
		phone->phoneType = PhoneType;
		if(PhoneType == PRO_ANDROID){
			memcpy(&(phone->phoneInfo.phoneAndroid), &phoneTmp, sizeof(usbInfo));			
			/*we need to notify kernel found aoa device*/
			LINUX_USBPowerControl(OCTL_ENABLE_USB_SW);
		}else if(PhoneType == PRO_IOS){
			memcpy(&(phone->phoneInfo.phoneIOS.usbIOS), &phoneTmp, sizeof(usbInfo));			
		}
		libusb_free_device_list(devs, 1);
		
		DEBUG("Phone Change to CONNCETING State.\r\n");
		return EUSTOR_OK;		
	}

	libusb_free_device_list(devs, 1);
	DEBUG("No Phone Found...\n");
	
	return EUSTOR_PRO_NODEVICE;
}


int32_t usProtocol_ConnectPhone(usPhoneinfo *phone)
{
	if(!phone){
		DEBUG("Bad Argument\n");
		return EUSTOR_ARG;
	}

	if(phone->phoneType == PRO_ANDROID){
		DEBUG("Android Phone Connect Successful\n");
		return EUSTOR_OK;
	}
	
	/*Connect iPhone, we need to the large buffer to send packet*/
	return LINUX_ConnectIOSPhone(&(phone->phoneInfo.phoneIOS));
}

void usProtocol_PhoneRelease(usPhoneinfo *phone)
{
	libusb_device_handle *dev = NULL;
	uint8_t intface;

	if(!phone){
		return;
	}

	if(phone->phoneType == PRO_ANDROID){
		dev = (libusb_device_handle* )phone->phoneInfo.phoneAndroid.osPriv;
		intface = phone->phoneInfo.phoneAndroid.interface;
	}else if(phone->phoneType == PRO_IOS){
		dev = (libusb_device_handle* )phone->phoneInfo.phoneIOS.usbIOS.osPriv;
		intface = phone->phoneInfo.phoneIOS.usbIOS.interface;
	}else{
		DEBUG("Unknown Type===>%d\r\n", phone->phoneType);		
		memset(phone, 0, sizeof(usPhoneinfo));
		return;
	}
	DEBUG("Destory Usb Resource\r\n");

	libusb_release_interface(dev, intface);
	libusb_close(dev);

	memset(phone, 0, sizeof(usPhoneinfo));

}




int32_t usProtocol_SendPackage(usPhoneinfo *phone, void *buffer, uint32_t size)
{
	if(!phone){
		return EUSTOR_ARG;
	}

	if(phone->phoneType == PRO_IOS){
		return itunes_SendProPackage(&(phone->phoneInfo.phoneIOS), buffer, size);
	}else if(phone->phoneType == PRO_ANDROID){
		return aoa_SendProPackage(&(phone->phoneInfo.phoneAndroid), buffer, size);
	}else{
		DEBUG("Unknown Phone Type:%d\n", phone->phoneType);
		return EUSTOR_ARG;
	}

	return EUSTOR_OK;
}

int32_t usProtocol_RecvPackage(usPhoneinfo *phone, uint8_t *buffer, uint32_t tsize, uint32_t *rsize)
{
	if(phone->phoneType == PRO_IOS){
		return itunes_RecvProPackage(&(phone->phoneInfo.phoneIOS), buffer, tsize, rsize);
	}else if(phone->phoneType == PRO_ANDROID){
		return aoa_RecvProPackage(&(phone->phoneInfo.phoneAndroid), buffer, tsize, rsize);
	}else{
		DEBUG("Unknown Phone Type:%d\n", phone->phoneType);
		return EUSTOR_ARG;
	}
	
	return EUSTOR_OK;	
}


