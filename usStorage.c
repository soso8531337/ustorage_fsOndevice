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
#include "usEvent.h"
#include "usError.h"
#include "usDecode.h"

/*512 used for ios header offset, 24 used protocol header, 256K used for payload, 4096 for path name, 128 for other sturct size*/
#define US_RESERVED_SIZE	512
#define US_BUFFER_SIZE		(US_RESERVED_SIZE+24+256*1024+4096+128) 

enum connect_state {
	CON_INIT=0,
	CON_CNTING=1,
	CON_CNTED=2
};


struct usStor{
	usPhoneinfo phone;
	uint8_t usbuf[US_BUFFER_SIZE];
	int32_t uspaylen;
	uint8_t *uspayload;
};

struct usStor usContext;
volatile static uint8_t phonePlugin = 0;

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
	int i;

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
	for (i = getdtablesize(); i >= 0; --i)
		close(i);
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


static int32_t usStorage_RecvPackage(usPhoneinfo *phoneDev, uint8_t *buffer, 
									int32_t bufSize, uint32_t *recvSize)
{
	int res;
	uint32_t paySize = 0, curSize, totalSize = 0;
	int32_t headMagic = 0;
	struct uStorPro_fsOnDev proHeader;

	if(!phoneDev || !buffer || !recvSize){
		return EUSTOR_ARG;
	}

	*recvSize = 0;
	res = usProtocol_RecvPackage(phoneDev, buffer, bufSize, &paySize);
	if(res != EUSTOR_OK || paySize < PRO_HDR_SZIE){
		DEBUG("First Receive Error:%d ReceiveBytes:%u\n", res, paySize);
		return res;
	}
	headMagic = *((int32_t*)buffer);
	DEBUG("Protocol Magic is 0x%0x  [Receive %u Bytes]\n", headMagic, paySize);
	if(headMagic == PRO_BASIC_MAGIC){
		*recvSize = paySize;		
		DEBUG("Request Protocol Magic, Request Receive Finish[%dBytes]\n", *recvSize);
		return EUSTOR_OK;
	}else if(headMagic == SCSI_PHONE_MAGIC){
		*recvSize = paySize;		
		DEBUG("Request Old Protocol DsiskLUN, Request Receive Finish[%dBytes]\n", *recvSize);
		return EUSTOR_OK;
	}
	if(headMagic != PRO_FSONDEV_MAGIC){
		DEBUG("Unknown Magic :0x%0x\n", headMagic);
		*recvSize = 0;
		return EUSTOR_OK;
	}

	memcpy(&proHeader, buffer, sizeof(struct uStorPro_fsOnDev));
	if(bufSize < proHeader.len+PRO_HDR_SZIE){
		DEBUG("Protocol Package is Too Big[%d/%d]\n", bufSize, proHeader.len+PRO_HDR_SZIE);
		return EUSTOR_OK;
	}
	/*Receive full package*/
	curSize = paySize;
	totalSize = proHeader.len + PRO_HDR_SZIE;	
	DEBUG("Request PartInfo[%d/%dBytes]\n", curSize, totalSize);
	while(curSize < totalSize){
		res = usProtocol_RecvPackage(phoneDev, buffer+curSize, bufSize-curSize, &paySize);
		if(res != EUSTOR_OK){
			DEBUG("Receive Error:%d ReceiveBytes:%u\n", res, paySize);
			return res;
		}
		curSize += paySize;
	}

	DEBUG("Receive Package Finish:\nHeader:%0x version:%d wtag:%d ctrid:%d"
					" len:%d\n", proHeader.head, proHeader.version, proHeader.wtag,
							proHeader.ctrid, proHeader.len);
	*recvSize = totalSize;

	return EUSTOR_OK;
}

static int32_t usStorage_ProtocolHandle(usPhoneinfo *phoneDev, 
									uint8_t *buffer, uint32_t buffsize, uint32_t paylength)
{
	struct uStorPro_fsOnDev *proHeader = NULL;
	uint32_t sndSize = 0;
	uint32_t headLen = sizeof(struct uStorPro_fsOnDev);
	uint8_t *payload = NULL;

	if(!phoneDev || !buffer){
		return EUSTOR_ARG;
	}

	proHeader = (struct uStorPro_fsOnDev *)buffer;

	if(proHeader->head == PRO_BASIC_MAGIC){
		struct uStorPro_headInfo *baseHeader = (struct uStorPro_headInfo *)buffer;
		
		baseHeader->relag = 0;
		usDecode_BasicMagicHandle(baseHeader);		
		return usProtocol_SendPackage(phoneDev, (void*)baseHeader, PRO_HDR_SZIE);
	}else if(proHeader->head == SCSI_PHONE_MAGIC){
		struct scsi_head *ver1Header = (struct scsi_head *)buffer;

		ver1Header->relag = 1;
		return usProtocol_SendPackage(phoneDev, (void*)ver1Header, PRO_HDR_SZIE);
	}
	payload = buffer+headLen;
	if(proHeader->ctrid == USTOR_FSONDEV_READ){
		/*Read operaton*/
		usDecode_ReadHandle(proHeader, payload, buffsize-headLen);
		sndSize = headLen+proHeader->len;
		DEBUG("Read Operation Handle Finish[SendSize:%d]\n", sndSize);
	}else if(proHeader->ctrid == USTOR_FSONDEV_WRITE){
		/*Write Operation*/
		usDecode_WriteHandle(proHeader, payload, paylength-headLen);
		sndSize = headLen;
	}else if(proHeader->ctrid == USTOR_FSONDEV_CREATE){
		usDecode_CreateHandle(proHeader, payload, paylength-headLen);	
		sndSize = headLen;
	}else if(proHeader->ctrid == USTOR_FSONDEV_DELETE){
		usDecode_DeleteHandle(proHeader, payload, paylength-headLen);	
		sndSize = headLen;
	}else if(proHeader->ctrid == USTOR_FSONDEV_MOVE){
		usDecode_MoveHandle(proHeader, payload, paylength-headLen);	
		sndSize = headLen;
	}else if(proHeader->ctrid == USTOR_FSONDEV_LIST){
		usDecode_ListHandle(phoneDev, proHeader, payload, buffsize-headLen);
		return EUSTOR_OK;
	}else if(proHeader->ctrid == USTOR_FSONDEV_DISKINFO){
		usDecode_DiskInfoHandle(proHeader, payload, buffsize-headLen);		
		sndSize = headLen+proHeader->len;
	}else if(proHeader->ctrid == USTOR_FSONDEV_DISKLUN){
		usDecode_DiskLunHandle(proHeader, payload, buffsize-headLen);		
		sndSize = headLen+proHeader->len;
	}else if(proHeader->ctrid == USTOR_FSONDEV_FIRMWARE_INFO){
		usDecode_GetFirmwareInfoHandle(proHeader, payload, buffsize-headLen);
		sndSize = headLen+proHeader->len;
	}else if(proHeader->ctrid == USTOR_FSONDEV_UPDATE_START ||
			proHeader->ctrid == USTOR_FSONDEV_UPDATE_DATA ||
				proHeader->ctrid == USTOR_FSONDEV_UPDATE_END){
		usDecode_UpgradeFirmwareHandle(proHeader, payload, paylength-headLen);		
		sndSize = headLen+proHeader->len;
	}else if(proHeader->ctrid == USTOR_FSONDEV_SAFEREMOVE){

	}else if(proHeader->ctrid == USTOR_FSONDEV_FILE_INFO){
		usDecode_GetFileInfoHandle(proHeader, payload, buffsize-headLen);		
		sndSize = headLen+proHeader->len;
	}else if(proHeader->ctrid == USTOR_FSONDEV_SYNC_TIME){
		usDecode_SyncSystemTimeHandle(proHeader, payload, paylength-headLen);
		sndSize = headLen+proHeader->len;
	}else if(proHeader->ctrid == USTOR_FSONDEV_SYNC_WRITE){
		usDecode_SyncCacheHandle(proHeader, payload, paylength-headLen);		
		sndSize = headLen+proHeader->len;
	}else{
		proHeader->len = 0;
		proHeader->relag = USTOR_EERR;		
		sndSize = headLen;
	}

	return usProtocol_SendPackage(phoneDev, buffer, sndSize);
}

void usStorage_notifyPlug(usPhoneinfo *phoneDev)
{
	struct nofiyStruct event;
	struct uStorPro_fsOnDev *proHeader;
	static int32_t dwtag = 0;
	uint8_t tmpbuf[1024];

	if(!phoneDev){
		return;
	}
	event.valid = 0;
	usDisk_getNotifyInfo(&event);
	if(event.valid == 0){
		return ;
	}

	proHeader = (struct uStorPro_fsOnDev *)(tmpbuf+36);

	proHeader->head = PRO_REVER_MAGIC;
	proHeader->version = 1;
	proHeader->relag = 0;
	proHeader->wtag = dwtag++;
	proHeader->ctrid = USTOR_FSONDEV_DISKLUN;
	proHeader->len = 0;

	DEBUG("Notify To Peer:%s-%s[Now:%ld Trigger:%d]\n",
				event.dev, event.event == DISK_PLUGIN?"PlugIn":"PlugOut", time(NULL), event.invokTime);
	usProtocol_SendPackage(phoneDev, (void*)proHeader, sizeof(struct uStorPro_fsOnDev));
}

void phoneCallBack(int state)
{
	phonePlugin = state;
	DEBUG("Change Phone Status:%d\n", state);
}

/*****************************************************************************/
/**************************Public functions***********************************/
/*****************************************************************************/

int main(int argc, char **argv)
{
	volatile static uint8_t conState = CON_INIT;
	uint32_t truRecvSize;	
	struct usEventArg eventarg;
	int32_t res;
	
	DEBUG("uStorage Filesystem on Device Running[%s %s].\n", __DATE__, __TIME__);

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
	usDisk_init();
	eventarg.eventDiskCall = usDisk_PlugCallBack;
	eventarg.eventPhoneCall = phoneCallBack;
	usEvent_init(&eventarg);
	
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
				usleep(500000);
				continue;
			}	
		}
		if(conState == CON_CNTING){		
			/*Connect Phone Device*/
			if(usProtocol_ConnectPhone(&(usContext.phone)) == EUSTOR_OK){				
				conState = CON_CNTED;
			}else{
				/*Connect to Phone Failed*/
				usleep(300000);
				goto loop_next;
			}
		}

		if((res = usStorage_RecvPackage(&(usContext.phone), 
					usContext.uspayload, usContext.uspaylen, &truRecvSize)) != EUSTOR_OK){
			if(res == EUSTOR_USB_TIMEOUT){
				//continue;
			}else if(res == EUSTOR_PRO_REFUSE){		
				conState = CON_CNTING;
			}else{
				DEBUG("Maybe Offline....\n");
				conState = CON_INIT;
				usProtocol_PhoneRelease(&(usContext.phone));				
				usleep(500000);
			}
			goto loop_next;
		}

		if(truRecvSize && (res = usStorage_ProtocolHandle(&(usContext.phone), 
						usContext.uspayload, usContext.uspaylen, truRecvSize)) != EUSTOR_OK){
			if(res == EUSTOR_USB_TIMEOUT){
				DEBUG("Something Error[Package Not Handle Complete]\n");
			}else if(res == EUSTOR_PRO_REFUSE){		
				conState = CON_CNTING;
			}else{
				DEBUG("Maybe Offline....\n");
				conState = CON_INIT;
				usProtocol_PhoneRelease(&(usContext.phone));
				usleep(500000);
			}
			goto loop_next;
		}		

	loop_next:		
		if(phonePlugin == 0){
			DEBUG("Loop Found Device Out..\n");
			usProtocol_PhoneRelease(&(usContext.phone));			
			conState = CON_INIT;
		}else if(conState == CON_CNTED){
			/*Notify function*/
			usStorage_notifyPlug(&(usContext.phone));
		}
	}
}


