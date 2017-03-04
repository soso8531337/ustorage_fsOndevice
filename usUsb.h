/*
 * @note
 * Copyright(C) i4season U-Storage, 2016
 * Copyright(C) Szitman, 2016
 * All rights reserved.
 *
 */
#ifndef __USUSB_H_
#define __USUSB_H_

#include <ctype.h>
#include <stdio.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** \ingroup desc
 * A structure representing the standard USB device descriptor. This
 * descriptor is documented in section 9.6.1 of the USB 2.0 specification.
 * All multiple-byte fields are represented in host-endian format.
 */
typedef struct  {
	/** Size of this descriptor (in bytes) */
	uint8_t  bLength;
	/** Descriptor type. Will have value
	 * \ref libusb_descriptor_type::LIBUSB_DT_DEVICE LIBUSB_DT_DEVICE in this
	 * context. */
	uint8_t  bDescriptorType;
	/** USB specification release number in binary-coded decimal. A value of
	 * 0x0200 indicates USB 2.0, 0x0110 indicates USB 1.1, etc. */
	uint16_t bcdUSB;
	/** USB-IF class code for the device. See \ref libusb_class_code. */
	uint8_t  bDeviceClass;
	/** USB-IF subclass code for the device, qualified by the bDeviceClass
	 * value */
	uint8_t  bDeviceSubClass;
	/** USB-IF protocol code for the device, qualified by the bDeviceClass and
	 * bDeviceSubClass values */
	uint8_t  bDeviceProtocol;
	/** Maximum packet size for endpoint 0 */
	uint8_t  bMaxPacketSize0;
	/** USB-IF vendor ID */
	uint16_t idVendor;
	/** USB-IF product ID */
	uint16_t idProduct;
	/** Device release number in binary-coded decimal */
	uint16_t bcdDevice;
	/** Index of string descriptor describing manufacturer */
	uint8_t  iManufacturer;
	/** Index of string descriptor describing product */
	uint8_t  iProduct;
	/** Index of string descriptor containing device serial number */
	uint8_t  iSerialNumber;
	/** Number of possible configurations */
	uint8_t  bNumConfigurations;
}USB_StdDesDevice_t;

/** \ingroup desc
 * A structure representing the standard USB interface descriptor. This
 * descriptor is documented in section 9.6.5 of the USB 2.0 specification.
 * All multiple-byte fields are represented in host-endian format.
 */
typedef struct  {
	/** Size of this descriptor (in bytes) */
	uint8_t  bLength;
	/** Descriptor type. Will have value
	 * \ref libusb_descriptor_type::LIBUSB_DT_INTERFACE LIBUSB_DT_INTERFACE
	 * in this context. */
	uint8_t  bDescriptorType;
	/** Number of this interface */
	uint8_t  bInterfaceNumber;
	/** Value used to select this alternate setting for this interface */
	uint8_t  bAlternateSetting;
	/** Number of endpoints used by this interface (excluding the control
	 * endpoint). */
	uint8_t  bNumEndpoints;
	/** USB-IF class code for this interface. See \ref libusb_class_code. */
	uint8_t  bInterfaceClass;
	/** USB-IF subclass code for this interface, qualified by the
	 * bInterfaceClass value */
	uint8_t  bInterfaceSubClass;
	/** USB-IF protocol code for this interface, qualified by the
	 * bInterfaceClass and bInterfaceSubClass values */
	uint8_t  bInterfaceProtocol;
	/** Index of string descriptor describing this interface */
	uint8_t  iInterface;
}USB_StdDesInterface_t;

/** \ingroup desc
 * A structure representing the standard USB endpoint descriptor. This
 * descriptor is documented in section 9.6.3 of the USB 2.0 specification.
 * All multiple-byte fields are represented in host-endian format.
 */
typedef struct {
	/** Size of this descriptor (in bytes) */
	uint8_t  bLength;
	/** Descriptor type. Will have value
	 * \ref libusb_descriptor_type::LIBUSB_DT_ENDPOINT LIBUSB_DT_ENDPOINT in
	 * this context. */
	uint8_t  bDescriptorType;
	/** The address of the endpoint described by this descriptor. Bits 0:3 are
	 * the endpoint number. Bits 4:6 are reserved. Bit 7 indicates direction,
	 * see \ref libusb_endpoint_direction.
	 */
	uint8_t  bEndpointAddress;
	/** Attributes which apply to the endpoint when it is configured using
	 * the bConfigurationValue. Bits 0:1 determine the transfer type and
	 * correspond to \ref libusb_transfer_type. Bits 2:3 are only used for
	 * isochronous endpoints and correspond to \ref libusb_iso_sync_type.
	 * Bits 4:5 are also only used for isochronous endpoints and correspond to
	 * \ref libusb_iso_usage_type. Bits 6:7 are reserved.
	 */
	uint8_t  bmAttributes;
	/** Maximum packet size this endpoint is capable of sending/receiving. */
	uint16_t wMaxPacketSize;
	/** Interval for polling endpoint for data transfers. */
	uint8_t  bInterval;
}USB_StdDesEndpoint_t;

typedef  struct{
	uint8_t  ResponseCode;
	uint8_t  SegmentNumber;
	unsigned SenseKey			 : 4;
	unsigned Reserved			 : 1;
	unsigned ILI				 : 1;
	unsigned EOM				 : 1;
	unsigned FileMark			 : 1;

	uint8_t  Information[4];
	uint8_t  AdditionalLength;
	uint8_t  CmdSpecificInformation[4];
	uint8_t  AdditionalSenseCode;
	uint8_t  AdditionalSenseQualifier;
	uint8_t  FieldReplaceableUnitCode;
	uint8_t  SenseKeySpecific[3];
} SCSI_Sense_Response_t;


typedef  struct{
	unsigned DeviceType 		 : 5;
	unsigned PeripheralQualifier : 3;

	unsigned Reserved			 : 7;
	unsigned Removable			 : 1;

	uint8_t  Version;

	unsigned ResponseDataFormat  : 4;
	unsigned Reserved2			 : 1;
	unsigned NormACA			 : 1;
	unsigned TrmTsk 			 : 1;
	unsigned AERC				 : 1;

	uint8_t  AdditionalLength;
	uint8_t  Reserved3[2];

	unsigned SoftReset			 : 1;
	unsigned CmdQue 			 : 1;
	unsigned Reserved4			 : 1;
	unsigned Linked 			 : 1;
	unsigned Sync				 : 1;
	unsigned WideBus16Bit		 : 1;
	unsigned WideBus32Bit		 : 1;
	unsigned RelAddr			 : 1;

	uint8_t  VendorID[8];
	uint8_t  ProductID[16];
	uint8_t  RevisionID[4];
}SCSI_Inquiry_t;


typedef struct  {
	uint8_t bus, address;
	uint16_t vid, pid;
	uint8_t interface, ep_in, ep_out;
	int wMaxPacketSize;
	void *osPriv;
}usbInfo;

int32_t usUsb_Init(void *osPriv);
int32_t usUsb_releaseInterface(usbInfo *usbDev);

int32_t usUsb_SendControlRequest(void *osPriv, 
			uint8_t bmRequestType, uint8_t bRequest, 
			uint16_t wValue, uint16_t wIndex, uint16_t wLength,  void * data);
int32_t usUsb_GetDeviceDescriptor(void *osPriv, USB_StdDesDevice_t *usbDeviceDescriptor);

int32_t usUsb_SetDeviceConfigDescriptor(void *osPriv, uint8_t cfgindex);

int32_t usUsb_BlukPacketReceive(usbInfo *usbDev, uint8_t *buffer, 
							uint32_t length, uint32_t *aLength, int timeout);

int32_t usUsb_BlukPacketSend(usbInfo *usbDev, uint8_t *buffer, 
							uint32_t length, uint32_t *aLength, int timeout);

void usUsb_Print(uint8_t *buffer, int length);
void usUsb_PrintStr(uint8_t *buffer, int length);

#ifdef __cplusplus
}
#endif
#endif

