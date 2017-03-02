/*
 * @brief U-Storage Project
 *
 * @note
 * Copyright(C) i4season, 2016
 * Copyright(C) Szitman, 2016
 * All rights reserved.
 *
 * @par
 * Software that is described herein is for illustrative purposes only
 * which provides customers with programming information regarding the
 * LPC products.  This software is supplied "AS IS" without any warranties of
 * any kind, and NXP Semiconductors and its licensor disclaim any and
 * all warranties, express or implied, including all implied warranties of
 * merchantability, fitness for a particular purpose and non-infringement of
 * intellectual property rights.  NXP Semiconductors assumes no responsibility
 * or liability for the use of the software, conveys no license or rights under any
 * patent, copyright, mask work right, or any other intellectual property rights in
 * or to any products. NXP Semiconductors reserves the right to make changes
 * in the software without notification. NXP Semiconductors also makes no
 * representation or warranty that such application will be suitable for the
 * specified use without further testing or modification.
 *
 * @par
 * Permission to use, copy, modify, and distribute this software and its
 * documentation is hereby granted, under NXP Semiconductors' and its
 * licensor's relevant copyrights in the software, without fee, provided that it
 * is used in conjunction with NXP Semiconductors microcontrollers.  This
 * copyright, permission, and disclaimer notice must appear in all copies of
 * this code.
 */

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <linux/netlink.h>
#include <sys/socket.h>
#include <poll.h>
#include <pthread.h>
#include "usUsb.h"
#include "usDisk.h"
#include "usProtocol.h"
#include "usSys.h"
#include "usFirmware.h"

#define PHONE_BUS_LOC           "usb1/1-1/1-1.2"
#define DISK_BUS_LOC            "usb1/1-1/1-1.1"




/*****************************************************************************
 * Private functions
 ****************************************************************************/
static int usStorage_sendHEAD(struct scsi_head *header)
{
	uint8_t buffer[PRO_HDR] = {0};

	if(!header){
		return 1;
	}

	memcpy(buffer, header, PRO_HDR);
	/*Send To Phone*/
	if(usProtocol_SendPackage(buffer, PRO_HDR)){
		SDEBUGOUT("Send To Phone[Just Header Error]\r\n");
		return 1;
	}

	return 0;
}

/*
*Read Multi TIme
*/
static int usStorage_diskMULREAD(struct scsi_head *header)
{
	uint8_t *buffer = NULL, rc;
	uint32_t size = 0, rsize = 0, avsize = 0;
	int32_t addr = 0;

	if(!header){
		SDEBUGOUT("usStorage_diskREAD Parameter Failed\r\n");
		return 1;
	}
	addr = header->addr;
	
	if(usProtocol_GetAvaiableBuffer((void **)&buffer, &size)){
		SDEBUGOUT("usProtocol_GetAvaiableBuffer Failed\r\n");
		return 1;
	}
	memcpy(buffer, header, PRO_HDR);
	/*Send To Phone*/
	if((rc = usProtocol_SendPackage(buffer, PRO_HDR)) != 0){
		SDEBUGOUT("Send To Phone[Just Header Error]\r\n");
		return rc;
	}
	if(!header->len){		
		SDEBUGOUT("Read Request Len is 0[MayBeError]\r\n");
		return 0;
	}
	while(rsize < header->len){
		uint32_t secCount = 0;
		if(usProtocol_GetAvaiableBuffer((void **)&buffer, &size)){
			SDEBUGOUT("usProtocol_GetAvaiableBuffer Failed\r\n");
			return 1;
		}

		avsize = min(USDISK_SECTOR*OP_DIV(size), header->len-rsize); /*We leave a sector for safe*/		
		secCount = OP_DIV(avsize);
		if(usDisk_DiskReadSectors(buffer, header->wlun, addr, secCount)){
			SDEBUGOUT("Read Sector Error[SndTotal:%d addr:%d  SectorCount:%d]\r\n",
					rsize, addr, secCount);
			return 1;
		}
		/*Send To Phone*/
		if((rc = usProtocol_SendPackage(buffer, avsize)) != 0){
			SDEBUGOUT("Send To Phone[SndTotal:%d addr:%d  SectorCount:%d]\r\n",
					rsize, addr, secCount);
			return rc;
		}
		SDEBUGOUT("READ INFO:%p[SZ:%dBytes][DS:%d(%d-->%d) [TS:%dBytes]\r\n", 
						buffer, avsize, header->addr, addr, addr +secCount, header->len);
		addr += secCount;
		rsize += avsize;
	}

	SDEBUGOUT("REQUEST READ OK:\r\nwtag=%d\r\nctrid=%d\r\naddr=%d\r\nlen=%d\r\nwlun=%d\r\n", 
			header->wtag, header->ctrid, header->addr, header->len, header->wlun);
	
	return 0;
}

/*Read Once*/
static int usStorage_diskSIGREAD(struct scsi_head *header)
{
	uint8_t *buffer = NULL;
	uint32_t size = 0;
	uint8_t rc;

	if(!header){
		SDEBUGOUT("usStorage_diskREAD Parameter Failed\r\n");
		return 1;
	}
	
	if(usProtocol_GetAvaiableBuffer((void **)&buffer, &size)){
		SDEBUGOUT("usProtocol_GetAvaiableBuffer Failed\r\n");
		return 1;
	}
	if(size < header->len){
		SDEBUGOUT("usStorage_diskSIGREAD Space Not Enough\r\n");
		return 1;
	}

	if(usDisk_DiskReadSectors(buffer, header->wlun, header->addr, OP_DIV(header->len))){
		SDEBUGOUT("Read Sector Error[SndTotal:%d addr:%d  SectorCount:%d]\r\n",
				header->len+PRO_HDR, header->addr, OP_DIV(header->len));
		/*Write to Phone*/
		header->relag = 1;
		usStorage_sendHEAD(header);	
		return 1;
	}
	/*Send To Phone*/
	usStorage_sendHEAD(header); 
	if((rc = usProtocol_SendPackage(buffer, header->len)) != 0){
		SDEBUGOUT("Send To Phone[SndTotal:%d addr:%d  SectorCount:%d]\r\n",
				header->len+PRO_HDR, header->addr, OP_DIV(header->len));
		return rc;
	}
	
	SDEBUGOUT("REQUEST READ OK:\r\nwtag=%d\r\nctrid=%d\r\naddr=%d\r\nlen=%d\r\nwlun=%d\r\n", 
			header->wtag, header->ctrid, header->addr, header->len, header->wlun);
	
	return 0;
}

static int usStorage_diskREAD(struct scsi_head *header)
{
	uint32_t size = 0;
	uint8_t *buffer = NULL;

	if(!header){
		SDEBUGOUT("usStorage_diskREAD Parameter Error\r\n");
		return 1;
	}

	if(!header->len){
		SDEBUGOUT("usStorage_diskREAD 0Bytes\r\n");
		/*Write to Phone*/
		header->relag = 1;
		usStorage_sendHEAD(header);	
		return 1;
	}
	
	if(usProtocol_GetAvaiableBuffer((void **)&buffer, &size)){
		SDEBUGOUT("usStorage_diskREAD Failed\r\n");
		return 1;
	}
	if(size < header->len+PRO_HDR){
		SDEBUGOUT("Use usStorage_diskMULREAD To Send[%d/%d]\r\n",
					header->len+PRO_HDR, size);
		return usStorage_diskMULREAD(header);
	}

	return usStorage_diskSIGREAD(header);
}

static int usStorage_diskINQUIRY(struct scsi_head *header)
{
	uint8_t *buffer = NULL;
	uint32_t size = 0, total = 0;
	struct scsi_inquiry_info dinfo;
	uint8_t rc = 0;
	
	if(!header){
		SDEBUGOUT("usStorage_diskINQUIRY Parameter Failed\r\n");
		return 1;
	}
	if(header->len != sizeof(struct scsi_inquiry_info)){
		SDEBUGOUT("usStorage_diskINQUIRY Parameter Error:[%d/%d]\r\n",
					header->len, sizeof(struct scsi_inquiry_info));
		return 1;
	}
	if(usDisk_DiskInquiry(header->wlun, &dinfo)){
		SDEBUGOUT("usDisk_DiskInquiry  Error\r\n");
	}
	if(usProtocol_GetAvaiableBuffer((void **)&buffer, &size)){
		SDEBUGOUT("usProtocol_GetAvaiableBuffer Failed\r\n");
		return 1;
	}
	memcpy(buffer, header, PRO_HDR);
	memcpy(buffer+PRO_HDR, &dinfo,  sizeof(struct scsi_inquiry_info));
	total = PRO_HDR+sizeof(struct scsi_inquiry_info);
	
	if((rc  = usProtocol_SendPackage(buffer, total)) != 0){
		SDEBUGOUT("usStorage_diskINQUIRY Failed\r\n");
		return rc;
	}
	
	SDEBUGOUT("usStorage_diskINQUIRY Successful\r\nDisk INQUIRY\r\nSize:%lld\r\nVendor:%s\r\nProduct:%s\r\nVersion:%s\r\nSerical:%s\r\n", 
				dinfo.size, dinfo.vendor, dinfo.product, dinfo.version, dinfo.serial);
	
	return 0;
}

static int usStorage_diskLUN(struct scsi_head *header)
{
	uint8_t num = usDisk_DiskNum();
	uint8_t *buffer = NULL, rc = 0;
	uint32_t size = 0, total = 0;

	printf("Disk Number-->%d\r\n", num);
	if(usProtocol_GetAvaiableBuffer((void **)&buffer, &size)){
		SDEBUGOUT("usProtocol_GetAvaiableBuffer Failed\r\n");
		return 1;
	}	
	SDEBUGOUT("AvaiableBuffer 0x%p[%dBytes][Disk:%d]\r\n", 
				buffer, size, num);
	
	total = sizeof(struct scsi_head);
	memcpy(buffer, header, total);
	memcpy(buffer+total, &num, 1);
	total += 1;
	
	if((rc = usProtocol_SendPackage(buffer, total)) != 0){
		SDEBUGOUT("usProtocol_SendPackage Failed\r\n");
		return rc;
	}
	SDEBUGOUT("usStorage_diskLUN Successful[DiskNumber %d]\r\n", num);
	return 0;
}

static int usStorage_cacheSYNC(struct scsi_head *header)
{
	if(!header){
		return 1;
	}
	usDisk_cacheSYNC(header->wlun);
	usStorage_sendHEAD(header);

	return 0;
}

static int usStorage_Handle(void)
{	
	uint8_t *buffer;
	uint32_t size=0;
	uint8_t rc;
	struct scsi_head header;

	if((rc = usProtocol_RecvPackage((void **)&buffer, 0, &size)) != 0){
		if(rc == PROTOCOL_DISCONNECT){
			return PROTOCOL_DISCONNECT;
		}
		SDEBUGOUT("usProtocol_RecvPackage Failed\r\n");
		return 1;
	}
	if(size < PRO_HDR){
		SDEBUGOUT("usProtocol_RecvPackage Too Small [%d]Bytes\r\n", size);
		return 1;
	}
	/*Must save the header, it will be erase*/
	memcpy(&header, buffer, PRO_HDR);
	if(header.head != SCSI_PHONE_MAGIC){
		SDEBUGOUT("Package Header Error:0x%x\r\n", header.head);
		return PROTOCOL_REGEN;
	}
	SDEBUGOUT("usProtocol_RecvPackage [%d/%d]Bytes\r\n", 
				header.len, size);
	/*Handle Package*/
	SDEBUGOUT("RQUEST:\r\nwtag=%d\r\nctrid=%d\r\naddr=%d\r\nlen=%d\r\nwlun=%d\r\n", 
			header.wtag, header.ctrid, header.addr, header.len, header.wlun);

	switch(header.ctrid){
		case SCSI_READ:
			return usStorage_diskREAD(&header);
		case SCSI_WRITE:		
			return usStorage_diskWRITE(buffer, size, &header);
		case SCSI_INQUIRY:
			return usStorage_diskINQUIRY(&header);
		case SCSI_GET_LUN:
			return usStorage_diskLUN(&header);
		case SCSI_UPDATE_START:
		case SCSI_UPDATE_DATA:
		case SCSI_UPDATE_END:
			return usStorage_firmwareUP(buffer, size);
		case SCSI_FIRMWARE_INFO:
			return usStorage_firmwareINFO(&header);
		case SCSI_SYNC_INFO:
			return usStorage_cacheSYNC(&header);
		default:
			SDEBUGOUT("Unhandle Command\r\nheader:%x\r\nwtag:%d\r\n"
						"ctrid:%d\r\naddr:%u\r\nlen:%d\r\nwlun:%d\r\n", header.head,
						header.wtag, header.ctrid, header.addr, header.len, header.wlun);
	}

	return 0;
}

/*****************************************************************************
 * Public functions
 ****************************************************************************/




typedef enum{
	notifyNONE,
	notifyADD,
	notifyREM
}plugStatus;


void notify_set_action(uint8_t action)
{
	usbLinux.usbDiskStatus = action;
}

uint8_t notify_get_action(void)
{
	return usbLinux.usbDiskStatus;
}

void notify_plug(uint8_t action)
{
	struct scsi_head header;
	static uint32_t wtag = 0;

	if(action == notifyNONE){
		return;
	}
	memset(&header, 0, PRO_HDR);
	header.head = SCSI_DEVICE_MAGIC;
	if(action == notifyADD){
		header.ctrid = SCSI_INPUT;
	}else{
		header.ctrid = SCSI_OUTPUT;
	}

	header.wtag = wtag++;
	header.len = 0;

	usStorage_sendHEAD(&header);
}

static int storage_init_netlink_sock(void)
{
	struct sockaddr_nl snl;
	int retval, sockfd = -1;

	memset(&snl, 0x00, sizeof(struct sockaddr_nl));
	snl.nl_family = AF_NETLINK;
	snl.nl_pid = getpid();
	snl.nl_groups = 1;

	sockfd = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
	if (sockfd == -1) {
		SDEBUGOUT("error getting socket: %s\r\n", strerror(errno));
		return -1;
	}
	retval = bind(sockfd, (struct sockaddr *) &snl,
		      sizeof(struct sockaddr_nl));
	if (retval < 0) {
		SDEBUGOUT("bind failed: %s\r\n", strerror(errno));
		close(sockfd);
		return -1;
	}
	/*Triger phone*/
	system("echo add > /sys/bus/usb/devices/usb1/1-1/1-1.2/uevent &");
	return sockfd;
}

static struct udevd_uevent_msg *get_msg_from_envbuf(const char *buf, int buf_size)
{
	int bufpos;
	int i;
	struct udevd_uevent_msg *msg;
	int maj = 0;
	int min = 0;

	msg = malloc(sizeof(struct udevd_uevent_msg) + buf_size);
	if (msg == NULL)
		return NULL;
	memset(msg, 0x00, sizeof(struct udevd_uevent_msg) + buf_size);

	/* copy environment buffer and reconstruct envp */
	memcpy(msg->envbuf, buf, buf_size);
	bufpos = 0;
	for (i = 0; (bufpos < buf_size) && (i < UEVENT_NUM_ENVP-2); i++) {
		int keylen;
		char *key;

		key = &msg->envbuf[bufpos];
		keylen = strlen(key);
		msg->envp[i] = key;
		bufpos += keylen + 1;
		SDEBUGOUT( "add '%s' to msg.envp[%i]\r\n", msg->envp[i], i);

		/* remember some keys for further processing */
		if (strncmp(key, "ACTION=", 7) == 0)
			msg->action = &key[7];
		else if (strncmp(key, "DEVPATH=", 8) == 0)
			msg->devpath = &key[8];
		else if (strncmp(key, "SUBSYSTEM=", 10) == 0)
			msg->subsystem = &key[10];		
		else if (strncmp(key, "DEVNAME=", 8) == 0)
			msg->devname = &key[8];
		else if (strncmp(key, "DEVTYPE=", 8) == 0)
			msg->devtype = &key[8];		
		else if (strncmp(key, "SEQNUM=", 7) == 0)
			msg->seqnum = strtoull(&key[7], NULL, 10);
		else if (strncmp(key, "MAJOR=", 6) == 0)
			maj = strtoull(&key[6], NULL, 10);
		else if (strncmp(key, "MINOR=", 6) == 0)
			min = strtoull(&key[6], NULL, 10);
		else if (strncmp(key, "TIMEOUT=", 8) == 0)
			msg->timeout = strtoull(&key[8], NULL, 10);
	}
	msg->devt = makedev(maj, min);
	msg->envp[i++] = "UDEVD_EVENT=1";
	msg->envp[i] = NULL;

	if (msg->devpath == NULL || msg->action == NULL) {
		SDEBUGOUT("DEVPATH or ACTION missing, ignore message\r\n");
		free(msg);
		return NULL;
	}
	return msg;
}

static int storage_handle_diskplug(struct udevd_uevent_msg *msg)
{
	int countTime = 0;
	uint32_t sendCount=0;
	uint8_t diskPlug;
	uint8_t rc;
	
	if(!msg){
		return -1;
	}
#define COUNT_TIME		10	

	if(strncmp(msg->devname, "sd", 2) &&
			strncmp(msg->devname, "mmcblk", 6)){
		SDEBUGOUT("Unknown Disk [%s]\r\n", msg->devname);
		return -1;
	}
	/*handle event*/
	if(!strcasecmp(msg->action, STOR_STR_ADD)){
		char devbuf[128] = {0};
		int fd = -1;

		sprintf(devbuf, "/dev/%s", msg->devname);
		if(access(devbuf, F_OK)){
			mknod(devbuf, S_IFBLK|0644, msg->devt);
		}
		/*Check dev can open*/
		if((fd = open(devbuf, O_RDONLY)) < 0){
			SDEBUGOUT("Open [%s] Failed:%s\r\n", 
					devbuf, strerror(errno));
			return 0;
		}else{
			close(fd);
		}
		/*preread*/
		SDEBUGOUT("ADD Device %d [%s/%s] To Storage List\r\n", 
				msg->id, msg->devname,  msg->devpath);
		notify_set_action(notifyADD);
		if(!strncmp(msg->devname, "mmcblk", 6)){
			usDisk_DeviceDetect(USB_CARD, (void*)devbuf);
		}else{
			usDisk_DeviceDetect(USB_DISK, (void*)devbuf);
		}
	}else if(!strcasecmp(msg->action, STOR_STR_REM)){
		SDEBUGOUT("Remove Device [%s/%s] From Storage List\r\n", 
					 msg->devname,  msg->devpath);		
		char devbuf[128] = {0};
		sprintf(devbuf, "/dev/%s", msg->devname);

		if(!strncmp(msg->devname, "mmcblk", 6)){
			rc = usDisk_DeviceDisConnect(USB_CARD, devbuf);
		}else{
			rc = usDisk_DeviceDisConnect(USB_DISK, (void*)devbuf);
		}	
		if(rc){			
			return 0;
		}		
		notify_set_action(notifyREM);			
	}else if(!strcasecmp(msg->action, STOR_STR_CHANGE)){		
		char devbuf[128] = {0};		
		int fd = -1;		
		
		SDEBUGOUT("Try To Handle Device %s [%s/%s] Event\r\n", 
					msg->action, msg->devname,  msg->devpath);
		sprintf(devbuf, "/dev/%s", msg->devname);
		if(access(devbuf, F_OK)){
			mknod(devbuf, S_IFBLK|0644, msg->devt);
		}
		/*Check dev can open*/
		if((fd = open(devbuf, O_RDONLY)) < 0){
			/*Remove ID*/
			SDEBUGOUT("We Think it may be Remove action[%s]\r\n", msg->devname);
			if(!strncmp(msg->devname, "mmcblk", 6)){
				rc = usDisk_DeviceDisConnect(USB_CARD, devbuf);
			}else{
				rc = usDisk_DeviceDisConnect(USB_DISK, (void*)devbuf);
			}
			if(rc){
				return 0;
			}			
			notify_set_action(notifyREM);			
		}else{
			close(fd);			
			SDEBUGOUT("We Think it may be Add action[%s]\r\n", msg->devname);
			notify_set_action(notifyADD);			
			if(!strncmp(msg->devname, "mmcblk", 6)){
				usDisk_DeviceDetect(USB_CARD, (void*)devbuf);
			}else{
				usDisk_DeviceDetect(USB_DISK, (void*)devbuf);
			}
		}		
	}else{
		SDEBUGOUT("Unhandle Device %s [%s/%s] Event\r\n",
				msg->action, msg->devname,	msg->devpath);
		return 0;
	}
	/*confirm try to send to peer wait 500ms*/
	sendCount = usbLinux.usbSendCount;
	while(countTime++ < COUNT_TIME){
		if(usbLinux.usbSendCount != sendCount &&
				notify_get_action() == notifyNONE){
			SDEBUGOUT("Main Thread Notify Disk %s [%s/%s] Event\r\n",
					msg->action, msg->devname, msg->devpath);
			return 0;
		}
		usleep(50000);
	}
	/*May be block receive, we need to notify to peer*/
	diskPlug = usbLinux.usbDiskStatus;
	usbLinux.usbDiskStatus = notifyNONE;
	notify_plug(diskPlug);
	
	SDEBUGOUT("Plug Thread Notify Disk %s [%s/%s] Event\r\n",
			msg->action, msg->devname, msg->devpath);

	return 0;
}

static int storage_handle_phoneplug(struct udevd_uevent_msg *msg)
{
	if(!msg){
		return -1;
	}
	
	if(!strcasecmp(msg->action, STOR_STR_ADD)){
		/*Add phone*/
		if(usbLinux.usbPhoneStatus == 1){
			SDEBUGOUT("Phone Have Been PlugIN %s [%s/%s] Event\r\n",
					msg->action, msg->devname, msg->devpath);
			usbLinux.usbPhoneStatus = 0;
		}		
		usProtocol_DeviceDisConnect();
		usbLinux.usbPhoneStatus = 1;
		SDEBUGOUT("Phone PlugIN %s [%s/%s] Event\r\n",
				msg->action, msg->devname, msg->devpath);		
	}else if(!strcasecmp(msg->action, STOR_STR_REM)){
		usbLinux.usbPhoneStatus = 0;
		SDEBUGOUT("Phone PlugOUT %s [%s/%s] Event\r\n",
				msg->action, msg->devname, msg->devpath);		
	}else{
		SDEBUGOUT("Phone Not Handle %s [%s] Event\r\n",
				msg->action,  msg->devpath);		
	}

	return 0;
}

static int storage_action_handle(int sockfd)
{
	char buffer[UEVENT_BUFFER_SIZE*2] = {0};
	struct udevd_uevent_msg *msg;
	int bufpos; 
	ssize_t size;
	char *pos = NULL;

	size = recv(sockfd, &buffer, sizeof(buffer), 0);
	if (size <= 0) {
		SDEBUGOUT("error receiving uevent message: %s\r\n", strerror(errno));
		return -1;
	}
	if ((size_t)size > sizeof(buffer)-1)
		size = sizeof(buffer)-1;
	buffer[size] = '\0';
	/* start of event payload */
	bufpos = strlen(buffer)+1;
	msg = get_msg_from_envbuf(&buffer[bufpos], size-bufpos);
	if (msg == NULL)
		return -1;

	/* validate message */
	pos = strchr(buffer, '@');
	if (pos == NULL) {
		SDEBUGOUT("Invalid uevent '%s'\r\n", buffer);
		free(msg);
		return -1;
	}
	pos[0] = '\0';
	if (msg->action == NULL) {
		SDEBUGOUT("no ACTION in payload found, skip event '%s'\r\n", buffer);
		free(msg);
		return -1;
	}

	if (strcmp(msg->action, buffer) != 0) {
		SDEBUGOUT("ACTION in payload does not match uevent, skip event '%s'\r\n", buffer);
		free(msg);
		return -1;
	}
	if(!msg->subsystem || !msg->devtype){
		SDEBUGOUT("Subsystem/Devtype mismatch [%s/%s]\r\n", 
				msg->subsystem, msg->devtype);
		free(msg);
		return 0;
	}
	if(!strcasecmp(msg->subsystem, STOR_SUBSYS) &&
			!strcasecmp(msg->devtype, STOR_DEVTYPE)){
		storage_handle_diskplug(msg);
	}else if(strstr(msg->devpath, PHONE_BUS_LOC) &&
		!strcasecmp(msg->subsystem, PHONE_SUBSYS) &&
				!strcasecmp(msg->devtype, PHONE_DEVTYPE)){
		SDEBUGOUT("Phone Detect [%s/%s]\r\n", 
						msg->devname,  msg->devpath);
		storage_handle_phoneplug(msg);
	}else{
		SDEBUGOUT("Unhandle DevicePlug %s [%s/%s] Event\r\n",
				msg->action, msg->devname,	msg->devpath);
	}

	free(msg);
	return 0;
}


void* vs_main_disk(void *pvParameters)
{
	int plugSocket = 0, cnt;
	struct pollfd fds;
	
	usDisk_DeviceInit(NULL);
	while(1){
		if(plugSocket <= 0 && 
				(plugSocket = storage_init_netlink_sock()) < 0){
			usleep(200000);
			continue;
		}
		fds.fd = plugSocket;
		fds.events = POLLIN;
		cnt = poll(&fds, 1, -1);
		if(cnt < 0){
			if(cnt == -1 && errno == EINTR){
				continue;
			}else{
				SDEBUGOUT("POLL Error:%s.\r\n", strerror(errno));
				usleep(200000);
				continue;
			}
		}else if(cnt == 0){
			/*timeout*/
			continue;
		}
		if(fds.revents & POLLIN){
			/*receive plug information*/
			storage_action_handle(plugSocket);
		}
	}

	return NULL;
}































































/*512 used for ios header offset, 24 used protocol header, 256K used for payload*/
#define US_RESERVED_SIZE	512
#define US_BUFFER_SIZE		(US_BUFFER_SIZE+24+256*1024) 



enum connect_state {
	CON_INIT=0,
	CON_CNTING=1,
	CON_CNTED=2
};


struct usStor{
	usPhoneinfo phone;
	char usbuf[US_BUFFER_SIZE];
	int32_t uspaylen;
	char *uspayload;
};

struct usStor usContext;





/*****************************************************************************/
/**************************Private functions**********************************/
/*****************************************************************************/

static int32_t handler_sig(void)
{
	struct sigaction act;
	memset(&act, 0, sizeof(struct sigaction));
	act.sa_handler = SIG_IGN;
	sigfillset(&act.sa_mask);
	if ((sigaction(SIGCHLD, &act, NULL) == -1) ||
		(sigaction(SIGTERM, &act, NULL) == -1) ||
		(sigaction(SIGSEGV, &act, NULL) == -1)) {
		DEBUG("Fail to sigaction[Errmsg:%s]\n", strerror(errno));	
	}

	act.sa_handler = SIG_IGN;
	if (sigaction(SIGPIPE, &act, NULL) == -1) {		
		DEBUG("Fail to signal(SIGPIPE)[Errmsg:%s]\n", strerror(errno));	
	}
	return 0;
}

static int32_t daemonize(void)
{
	pid_t pid;
	pid_t sid;

	// already a daemon
	if (getppid() == 1)
		return 0;

	pid = fork();
	if (pid < 0) {
		DEBUG("fork() failed.\r\n");
		return pid;
	}

	if (pid > 0) {
		// exit parent process
		exit(0);
	}
	
	// Create a new SID for the child process
	sid = setsid();
	if (sid < 0) {
		DEBUG("setsid() failed.\r\n");
		return -1;
	}

	pid = fork();
	if (pid < 0) {
		DEBUG("fork() failed (second).\r\n");
		return pid;
	}

	if (pid > 0) {
		// exit parent process
		exit(0);
	}

	// Change the current working directory.
	if ((chdir("/")) < 0) {
		DEBUG("chdir() failed\r\n");
		return -2;
	}
	// Redirect standard files to /dev/null
	if (!freopen("/dev/null", "r", stdin)) {
		DEBUG("Redirection of stdin failed.\r\n");
		return -3;
	}
	if (!freopen("/dev/null", "w", stdout)) {
		DEBUG("Redirection of stdout failed.\r\n");
		return -3;
	}

	return 0;
}


static void usStorage_init(void)
{
	memset(&usContext, 0, sizeof(usContext));
	usContext.uspaylen = sizeof(usContext.usbuf) - US_RESERVED_SIZE;
	usContext.uspayload = usContext.usbuf + US_RESERVED_SIZE;

	DEBUG("Init Finish[Buffer:%p   PayBuffer:%p  PayloadLength:%d]...\n", 
				usContext.usbuf, usContext.uspayload, usContext.uspaylen);
}


static uint8_t usStorage_RecvPackage(usPhoneinfo *phoneDev, uint8_t *buffer, 
									int32_t bufSize, int32_t recvSize)
{

}

static uint8_t usStorage_ProtocolHandle(usPhoneinfo *phoneDev, 
									uint8_t *payload, int32_t paylength)
{

}



/*****************************************************************************/
/**************************Public functions***********************************/
/*****************************************************************************/

int main(int argc, char **argv)
{
	volatile static uint8_t phonePlugin = 0;
	volatile static uint8_t conState = CON_INIT;
	int32_t truRecvSize;
	
	DEBUG("uStorage Filesystem on Device Running[%s %s].\r\n", __DATE__, __TIME__);

	if(argc <= 1){
		daemonize();
		handler_sig();
	}
	if(usUsb_Init(NULL) != EUSTOR_OK){
		DEBUG("Init USB Error\n");
		return 1;
	}
	usProtocol_init(NULL);
	usStorage_init();
	
	while(1){
		if(phonePlugin == 0){
			DEBUG("Wait Phone Plug IN.\n");
			usleep(500000);
			continue;
		}
		if(conState == CON_INIT){
			if(usProtocol_PhoneDetect(&(usContext.phone)) == EUSTOR_OK){
				conState = CON_CNTING;				
				DEBUG("Detect Phone Change to ING\n");
			}else{
				DEBUG("Detect Phone Failed.\n");
				usleep(200000);
				continue;
			}	
		}else if(conState == CON_CNTING){		
			/*Connect Phone Device*/
			if(usProtocol_ConnectPhone(&(usContext.phone)) == EUSTOR_OK){				
				conState = CON_CNTED;
			}else{
				/*Connect to Phone Failed*/
				usleep(300000);
				continue;
			}
		}

		if(usStorage_RecvPackage(&(usContext.phone), 
					usContext.uspayload, usContext.uspaylen, truRecvSize) != EUSTOR_OK){
			DEBUG("RecvPackage Error\n");
			conState == CON_CNTING;
			goto loop_next;
		}
		if(usStorage_ProtocolHandle(&(usContext.phone), 
					usContext.uspayload, truRecvSize) != EUSTOR_OK){
			DEBUG("RecvPackage Error\n");
			conState == CON_CNTING;
			goto loop_next;
		}		

	loop_next:		
		if(phonePlugin == 0){
			usProtocol_PhoneRelease(&(usContext.phone));			
			conState == CON_INIT;
		}
	}
}


