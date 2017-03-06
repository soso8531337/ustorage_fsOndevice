/*
 * @note
 * Copyright(C) i4season U-Storage, 2016
 * Copyright(C) Szitman, 2016
 * All rights reserved.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <libusb-1.0/libusb.h>

#include <ctype.h>
#include <stdio.h>
#include "usUsb.h"
#include "usSys.h"
#include "usError.h"

/*
*osPriv is reserved
*/
int32_t usUsb_Init(void *osPriv)
{
	static uint8_t usbinit = 0;
	int res;

	if(usbinit == 1){
		return EUSTOR_OK;
	}
	res = libusb_init(NULL);
	//libusb_set_debug(NULL, 3);
	if(res != 0) {
		DEBUG("libusb_init failed: %d", res);
		return EUSTOR_USB_INIT;
	}
	return EUSTOR_OK;
}

int32_t usUsb_releaseInterface(usbInfo *usbDev)
{
	if(!usbDev){
		return EUSTOR_ARG;
	}
	libusb_device_handle *dev = (libusb_device_handle *)usbDev->osPriv;
	
	DEBUG("Destory Usb Resource\r\n");
	
	libusb_release_interface(dev, usbDev->interface);
	libusb_close(dev);

	return EUSTOR_OK;
}


int32_t usUsb_SendControlRequest(void *osPriv, 
			uint8_t bmRequestType, uint8_t bRequest, 
			uint16_t wValue, uint16_t wIndex, uint16_t wLength,  void * data)
{
	int8_t rc;

	rc =  libusb_control_transfer((libusb_device_handle *)osPriv, bmRequestType, 
					bRequest, wValue, wIndex, data, wLength, 0);

	return (rc < 0)?EUSTOR_USB_CONTROL:EUSTOR_OK;
}

int32_t usUsb_GetDeviceDescriptor(void *osPriv, USB_StdDesDevice_t *usbDeviceDescriptor)
{
	libusb_device_handle *dev_handle;

	if(!usbDeviceDescriptor || !osPriv){
		return EUSTOR_ARG;
	}
	if(libusb_get_device_descriptor(libusb_get_device((libusb_device_handle*)osPriv), 
				(struct libusb_device_descriptor*)usbDeviceDescriptor)){
		DEBUG("Error Getting Device Descriptor.\r\n");
		return EUSTOR_USB_GETDEVDES;
	}
	
	return EUSTOR_OK;
}

int32_t usUsb_SetDeviceConfigDescriptor(void *osPriv, uint8_t cfgindex)
{
	if(!osPriv){
		return EUSTOR_ARG;
	}
	
	if (libusb_set_configuration((struct libusb_device_handle *)(osPriv), cfgindex) != 0) {
		DEBUG("Error Setting Device Configuration.\n");
		return EUSTOR_USB_SETDEVDES;
	}

	return EUSTOR_OK;
}

int32_t usUsb_BlukPacketReceive(usbInfo *usbDev, uint8_t *buffer, 
										uint32_t length, uint32_t *aLength, int timeout)
{
	int32_t rc;
	int transferred = 0;

	if(!usbDev || !usbDev->osPriv || !buffer || !aLength){
		return EUSTOR_ARG;
	}

	rc = libusb_bulk_transfer((struct libusb_device_handle *)(usbDev->osPriv),
								usbDev->ep_in,
								buffer,
								length,
								&transferred,
								timeout);
	if (rc  == LIBUSB_ERROR_TIMEOUT){
		DEBUG("LIBUSB Receive Timeout\n");
		return EUSTOR_USB_TIMEOUT;
	}else if(rc == LIBUSB_ERROR_NO_DEVICE){
		DEBUG("Device OffLine....\n");				
		return EUSTOR_USB_OFFLINE;
	}else if(rc == LIBUSB_ERROR_OVERFLOW){
		DEBUG("LIBUSB OverFlow[%p/%d/%dBytes]....\n", buffer, length, transferred);
		return EUSTOR_USB_OVERFLOW;
	}else if(rc < 0){
		DEBUG("LIBUSB bulk transfer error %d\n", rc);
		return EUSTOR_USB_BULKERR;
	}

	*aLength = transferred;
	DEBUG("LIBUSB Receive %u/%d.\r\n", *aLength, length);

	return EUSTOR_OK;
}


int32_t usUsb_BlukPacketSend(usbInfo *usbDev, uint8_t *buffer, 
										uint32_t length, uint32_t *aLength, int timeout)
{
	int32_t rc;
	int transferred = 0;
	uint32_t already = 0, sndlen;
	
	if(!usbDev || !usbDev->osPriv){
		return EUSTOR_ARG;
	}
	if(length == 0){
		DEBUG("Send ZLP Package\n");
	}
	do{
		rc = libusb_bulk_transfer((struct libusb_device_handle *)(usbDev->osPriv),
									usbDev->ep_out,
									buffer+already,
									length -already,
									&transferred,
									timeout);
		if (rc  == LIBUSB_ERROR_TIMEOUT){
			DEBUG("LIBUSB Send Timeout\n");
			return EUSTOR_USB_TIMEOUT;
		}else if(rc == LIBUSB_ERROR_NO_DEVICE){
			DEBUG("Device OffLine....\n");				
			return EUSTOR_USB_OFFLINE;
		}else if(rc == LIBUSB_ERROR_OVERFLOW){
			DEBUG("LIBUSB OverFlow[%p/%d/%dBytes]....\n", buffer, length, transferred);
			return EUSTOR_USB_OVERFLOW;
		}else if(rc < 0){
			DEBUG("LIBUSB bulk transfer error %d\n", rc);
			return EUSTOR_USB_BULKERR;
		}

		already += transferred;
		DEBUG("LIBUSB Send %u/%d.\r\n", transferred, length);
	}while(already < length);

	if(aLength){
		*aLength = length;
	}
	DEBUG("LIBUSB Send %uBytes Successful\n", length);
	
	return EUSTOR_OK;
}

void usUsb_Print(uint8_t *buffer, int length)
{
	int cur = 0;
	
	if(!buffer){
		return;
	}

	for(; cur< length; cur++){
		if(cur % 16 == 0){
			printf("\n");
		}
		printf("0x%02x ", buffer[cur]);

	}
	
	printf("\n");
}

void usUsb_PrintStr(uint8_t *buffer, int length)
{
	int cur = 0;
	
	if(!buffer){
		return;
	}

	for(; cur< length; cur++){
		printf("%c", buffer[cur]);
	}
	
	printf("\n");
}

