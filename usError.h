#ifndef __USERROR_H_
#define __USERROR_H_

#ifdef __cplusplus
extern "C" {
#endif

	#define EUSTOR_OK	0

	/*ustorage base error from 20170000*/
	#define EUSTOR_ARG			20170001
	#define EUSTOR_MEM			20170002
	

	/*ustorage usb releative error from 20171000*/
	#define EUSTOR_USB_INIT		20171001
	#define EUSTOR_USB_CONTROL 	20171002
	#define EUSTOR_USB_TIMEOUT	20171003
	#define EUSTOR_USB_OFFLINE	20171004
	#define EUSTOR_USB_OVERFLOW	20171005
	#define EUSTOR_USB_BULKERR	20171006
	#define EUSTOR_USB_GETDEVDES 	20171007
	#define EUSTOR_USB_SETDEVDES	20171008
	
	/*ustorage disk releative error from 20172000*/
	#define EUSTOR_DISK_OPEN	20172002
	#define EUSTOR_DISK_SEEK	20172003
	#define EUSTOR_DISK_READ	20172004
	#define EUSTOR_DISK_WRITE	20172005
	
	/*ustorage protocol releative error from 20173000*/
	#define EUSTOR_PRO_LIST		20173001
	#define EUSTOR_PRO_NODEVICE 20173002
	#define EUSTOR_PRO_SETAOA	20173003
	#define EUSTOR_PRO_NOAOA	20173004	
	#define EUSTOR_PRO_CONNECT	20173005	
	#define EUSTOR_PRO_PGEPRO	20173006 /*package protocol error*/	
	#define EUSTOR_PRO_PROVER	20173007 /*package protocol version Error*/
	#define EUSTOR_PRO_REVACK	20173008
	#define EUSTOR_PRO_PACKAGE	20173009

#ifdef __cplusplus
}
#endif
#endif
