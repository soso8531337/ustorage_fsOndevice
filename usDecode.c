/*
 * @brief U-Storage Project
 *
 * @note
 * Copyright(C) i4season, 2016
 * Copyright(C) Szitman, 2016
 * All rights reserved.
 */

#include <stdint.h>
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
#include <getopt.h>
#include <pwd.h>
#include <grp.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/un.h>
#include <sys/select.h>
#include <linux/types.h>
#include <linux/netlink.h>
#include <sys/ioctl.h>
#include <scsi/sg.h>

#include "usError.h"
#include "usUsb.h"
#include "protocol.h"
#include "usSys.h"
#include "usProtocol.h"


int32_t usDecode_BasicMagicHandle(struct uStorPro_headInfo *proHeader)
{
	
	if(!proHeader){
		return EUSTOR_ARG;
	}

	proHeader->promagic = PRO_FSONDEV_MAGIC;
	proHeader->version = PRO_FSONDEV_VER1;

	return EUSTOR_OK;
}

int32_t usDecode_ReadHandle(struct uStorPro_fsOnDev *proHeader, uint8_t *readHeader, uint32_t buffSize)
{	

	if(!proHeader ||!readHeader){
		proHeader->len = 0;
		proHeader->relag = USTOR_EERR;		
		return EUSTOR_ARG;
	}
	if(proHeader->version == PRO_FSONDEV_VER1){
		struct operation_rw *readInfo = (struct operation_rw *)readHeader;		
		char abspath[4096] = {0};
		int64_t offset= readInfo->offset;
		int32_t size = readInfo->size;
		int16_t pathlen = readInfo->pathlen;
		strncpy(abspath, readInfo->abspath, pathlen);

		DEBUG("Read Operation:\nName:%s\nSize:%d\nOffset:%lld\n", abspath, size, offset);
		
		if(buffSize >= size&&
				usDisk_diskRead(abspath, readHeader, offset, size) == EUSTOR_OK){
			proHeader->len = size;
		}else{
		
			DEBUG("Read Operation Error:\nName:%s\nSize:%d\nOffset:%ld\n", abspath, size, offset);
			proHeader->len = 0;
			proHeader->relag = USTOR_EERR;
		}
	}else{
		DEBUG("UnSupport Version:%d\n", proHeader->version);
		proHeader->len = 0;
		proHeader->relag = USTOR_EERR;
	}

	return EUSTOR_OK;
}

int32_t usDecode_WriteHandle(struct uStorPro_fsOnDev *proHeader, uint8_t *writeHeader, uint32_t payloadSize)
{
	if(!proHeader ||!writeHeader){
		proHeader->len = 0;
		proHeader->relag = USTOR_EERR;		
		return EUSTOR_ARG;
	}
	if(proHeader->version == PRO_FSONDEV_VER1){
		struct operation_rw *writeInfo = (struct operation_rw *)writeHeader;		
		char abspath[4096] = {0};
		int64_t offset= writeInfo->offset;
		int32_t size = writeInfo->size;
		int16_t pathlen = writeInfo->pathlen;
		strncpy(abspath, writeInfo->abspath, pathlen);

		DEBUG("Write Operation:\nName:%s\nSize:%d\nOffset:%lld\n", abspath, size, offset);
		
		if(usDisk_diskWrite(abspath, writeHeader+sizeof(struct operation_rw)+pathlen, 
							offset, size) == EUSTOR_OK){
			proHeader->len = 0;
		}else{
		
			DEBUG("Read Operation Error:\nName:%s\nSize:%d\nOffset:%ld\n", abspath, size, offset);
			proHeader->len = 0;
			proHeader->relag = USTOR_EERR;
		}
	}else{
		DEBUG("UnSupport Version:%d\n", proHeader->version);
		proHeader->len = 0;
		proHeader->relag = USTOR_EERR;
	}

	return EUSTOR_OK;
}


int32_t usDecode_CreateHandle(struct uStorPro_fsOnDev *proHeader, uint8_t *creatPtr, uint32_t ptrLen)
{

	if(!proHeader || !creatPtr){
		proHeader->len = 0;
		proHeader->relag = USTOR_EERR;		
		return EUSTOR_ARG;
	}
	
	if(proHeader->version == PRO_FSONDEV_VER1){
		struct operation_newdel *createInfo = (struct operation_newdel*)creatPtr;

		DEBUG("Create Operation:[%s]%s\n", createInfo->isdir?"Dir":"File", createInfo->abspath);

		if(usDisk_diskCreate(createInfo->abspath, createInfo->isdir,
					createInfo->actime, createInfo->modtime) != EUSTOR_OK){
			proHeader->relag = USTOR_EERR;
		}
		proHeader->len = 0;
	}else{
		DEBUG("UnSupport Version:%d\n", proHeader->version);
		proHeader->len = 0;
		proHeader->relag = USTOR_EERR;
	}

	return EUSTOR_OK;
}

int32_t usDecode_DeleteHandle(struct uStorPro_fsOnDev *proHeader, uint8_t *delPtr, uint32_t ptrLen)
{
	if(!proHeader || !delPtr){		
		proHeader->len = 0;
		proHeader->relag = USTOR_EERR;
		return EUSTOR_ARG;
	}

	if(proHeader->version == PRO_FSONDEV_VER1){
		struct operation_newdel *deleteInfo = (struct operation_newdel*)delPtr;

		DEBUG("Delete Operation:[%s]%s\n", deleteInfo->isdir?"Dir":"File", deleteInfo->abspath);
		if(remove(deleteInfo->abspath)){
			DEBUG("Remove %s Failed:%s\n", deleteInfo->abspath, strerror(errno));		
			proHeader->relag = USTOR_EERR;
		}
		
		proHeader->len = 0;
	}else{
		DEBUG("UnSupport Version:%d\n", proHeader->version);
		proHeader->len = 0;
		proHeader->relag = USTOR_EERR;
	}

	return EUSTOR_OK;
}

int32_t usDecode_MoveHandle(struct uStorPro_fsOnDev *proHeader, uint8_t *MovPtr, uint32_t ptrLen)
{
	if(!proHeader || !MovPtr){		
		proHeader->len = 0;
		proHeader->relag = USTOR_EERR;
		return EUSTOR_ARG;
	}
	
	if(proHeader->version == PRO_FSONDEV_VER1){
		struct operation_move *movInfo = (struct operation_move*)MovPtr;
		DEBUG("Move Operation:[%s]-->%s\n", movInfo->from, movInfo->to);
		if(rename(movInfo->from, movInfo->to)){
			DEBUG("Move %s Failed:%s\n", movInfo->from, strerror(errno));		
			proHeader->relag = USTOR_EERR;
		}		
		proHeader->len = 0;
	}else{
		DEBUG("UnSupport Version:%d\n", proHeader->version);
		proHeader->len = 0;
		proHeader->relag = USTOR_EERR;
	}
	
	return EUSTOR_OK;
}

int32_t usDecode_DiskLunHandle(struct uStorPro_fsOnDev *proHeader, uint8_t *DiskPtr, uint32_t payLen)
{
	if(!proHeader || !DiskPtr){		
		proHeader->len = 0;
		proHeader->relag = USTOR_EERR;
		return EUSTOR_ARG;
	}
	
	if(proHeader->version == PRO_FSONDEV_VER1){
		DEBUG("Disk Lun Operation\n");
		if(usDisk_diskLun(DiskPtr, payLen, &(proHeader->len)) != EUSTOR_OK){
			DEBUG("Get Disk Lun Failed\n");		
			proHeader->relag = USTOR_EERR;			
			proHeader->len = 0;
		}
	}else{
		DEBUG("UnSupport Version:%d\n", proHeader->version);
		proHeader->len = 0;
		proHeader->relag = USTOR_EERR;
	}
	
	return EUSTOR_OK;
}

int32_t usDecode_DiskInfoHandle(struct uStorPro_fsOnDev *proHeader, uint8_t *DiskPtr, uint32_t payLen)
{
	if(!proHeader || !DiskPtr){		
		proHeader->len = 0;
		proHeader->relag = USTOR_EERR;
		return EUSTOR_ARG;
	}

	if(proHeader->version == PRO_FSONDEV_VER1){
		DEBUG("Disk Info Operation\n");
		if(usDisk_diskInfo(DiskPtr, payLen, &(proHeader->len)) != EUSTOR_OK){
			DEBUG("Get Disk Info Failed\n");		
			proHeader->relag = USTOR_EERR;			
			proHeader->len = 0;
		}
	}else{
		DEBUG("UnSupport Version:%d\n", proHeader->version);
		proHeader->len = 0;
		proHeader->relag = USTOR_EERR;
	}

	return EUSTOR_OK;
}



struct listCallback{
	uint8_t *buffer;
	uint32_t size;
	uint32_t usedsize;
	void *proHeader;
	void *phoneDev;
};

int32_t readDirInvoke(void *arg, char *filename, int flag)
{
	struct listCallback *listInvoke = (struct listCallback *)arg;
	struct operation_listResponse *listhdr;
	struct fileinfo *fileInfo;
	struct uStorPro_fsOnDev *proHeader = (struct uStorPro_fsOnDev *)(listInvoke->proHeader);
	char *ptrFlag = NULL;
	int32_t res;

	if(!listInvoke){
		return EUSTOR_ARG;
	}

	listhdr = (struct operation_listResponse *)listInvoke->buffer;

	if(filename == NULL && flag == 1){
		goto listFin;
	}


	if(listInvoke->size-listInvoke->usedsize < sizeof(struct fileinfo)){
		DEBUG("Buffer is Full, need Send it immediately[Payload:%dBytes]\n", proHeader->len);
		listhdr->finish = flag;
		proHeader->len =listInvoke->usedsize; 
		if((res = usProtocol_SendPackage((usPhoneinfo *)(listInvoke->phoneDev), 
				listInvoke->proHeader, listInvoke->usedsize+PRO_HDR_SZIE) != EUSTOR_OK)){
			return res;
		}
		if(flag == 1){
			DEBUG("Send List Finish\n");
			return EUSTOR_OK;
		}
		listInvoke->usedsize = 0;
	}

	struct stat st;
	memset(&st, 0, sizeof(struct stat));
	if(stat(filename, &st)!= 0){
		DEBUG("Stat %s Failed:%s\n", filename, strerror(errno));
		struct uStorPro_fsOnDev *proHdr= (struct uStorPro_fsOnDev *)listInvoke->proHeader;
		
		proHdr->relag =1;
		proHeader->len = 0;
		return usProtocol_SendPackage((usPhoneinfo *)(listInvoke->phoneDev), 
				listInvoke->proHeader, PRO_HDR_SZIE);
	}
	
	if(listInvoke->usedsize == 0){
		DEBUG("First Element..\n");
		listhdr->fileNum = 0;
		listInvoke->usedsize = sizeof(struct operation_listResponse);
	}
	fileInfo = (struct fileinfo*)(listInvoke->buffer+listInvoke->usedsize);
	ptrFlag = strrchr(filename, '/');
	if(ptrFlag == NULL){
		strcpy(fileInfo->name, filename);
	}else{
		strcpy(fileInfo->name, ptrFlag+1);
	}
	fileInfo->actime =st.st_atime;
	fileInfo->modtime = st.st_mtime;
	if(S_ISDIR(st.st_mode)){
		fileInfo->isdir= 1;
	}else{
		fileInfo->isdir= 0;
	}
	fileInfo->size = st.st_size;
	
	listhdr->fileNum++;
	
	listInvoke->usedsize += sizeof(struct fileinfo);


listFin:
	if(flag == 1){
		listhdr->finish = flag;
		if(!listInvoke->usedsize){
			listhdr->fileNum = 0;
		}
		proHeader->len = listInvoke->usedsize;
		DEBUG("Last Part To Send List[%dFiles Paylaod:%dBytes]\n", 
							listhdr->fileNum, proHeader->len);	
		return usProtocol_SendPackage((usPhoneinfo *)(listInvoke->phoneDev), 
				listInvoke->proHeader, listInvoke->usedsize+PRO_HDR_SZIE);
	}

	return EUSTOR_OK;
}

int32_t usDecode_ListHandle(usPhoneinfo *phoneDev, struct uStorPro_fsOnDev *proHeader, 
						uint8_t *listPtr, uint32_t ptrLen)
{
	struct listCallback listcall;
	char filename[MAX_PATH_SIZE] = {0};
	struct stat st;
	int ret;

	
	memcpy(filename, listPtr, MAX_PATH_SIZE);
	memset(&st, 0, sizeof(struct stat));
	if(stat(filename, &st)!= 0 || !S_ISDIR(st.st_mode)){
		DEBUG("%s is Not Dir\n", filename);
		proHeader->relag =USTOR_EERR;
		proHeader->len = 0;		
		return usProtocol_SendPackage(phoneDev, (void*)proHeader, PRO_HDR_SZIE);
	}
	
	listcall.buffer = listPtr;
	listcall.size = ptrLen;
	listcall.usedsize = 0;
	listcall.proHeader = (void*)proHeader;
	listcall.phoneDev= (void*)phoneDev;

	DEBUG("Buffer:%p Length:%d--List ----->%s\n", listcall.buffer, listcall.size, filename);
	ret = usDisk_diskList(filename, readDirInvoke, (void*)&listcall);
	if(ret == EUSTOR_DISK_LIST){
		DEBUG("OpenDir Failed, Notify to peer[%s]\n", filename);
		proHeader->relag =USTOR_EERR;
		proHeader->len = 0;		
		return usProtocol_SendPackage(phoneDev, (void*)proHeader, PRO_HDR_SZIE);
	}

	return EUSTOR_OK;
}

int32_t usDecode_GetFileInfoHandle(struct uStorPro_fsOnDev *proHeader, uint8_t *FilePtr, uint32_t payLen)
{
	char filename[MAX_PATH_SIZE] = {0};
	struct stat st;
	struct fileinfo *fileInfo;
	char *ptrFlag;

	
	memcpy(filename, FilePtr, MAX_PATH_SIZE);
	memset(&st, 0, sizeof(struct stat));
	if(stat(filename, &st)!= 0){
		DEBUG("%s is Not A Normal File\n", filename);
		proHeader->relag =USTOR_EERR;		
		proHeader->len = 0;
		return EUSTOR_ARG;
	}
	
	fileInfo = (struct fileinfo *)FilePtr;
	ptrFlag = strrchr(filename, '/');
	if(ptrFlag == NULL){
		strcpy(fileInfo->name, filename);
	}else{
		strcpy(fileInfo->name, ptrFlag+1);
	}
	fileInfo->actime =st.st_atime;
	fileInfo->modtime = st.st_mtime;
	if(S_ISDIR(st.st_mode)){
		fileInfo->isdir = 1;
		fileInfo->size = 0;
	}else{
		fileInfo->isdir = 0;
		fileInfo->size = st.st_size;
	}
	proHeader->len = sizeof(struct fileinfo);

	DEBUG("Buffer:%p Length:%d--GetFIle Info OK ----->%s\n",  FilePtr, proHeader->len,filename);
	return EUSTOR_OK;	
}


int32_t usDecode_GetFirmwareInfoHandle(struct uStorPro_fsOnDev *proHeader, uint8_t *FrimwarePtr, uint32_t payLen)
{
	if(usFirmware_GetInfo(FrimwarePtr, payLen, &(proHeader->len)) != EUSTOR_OK){
		DEBUG("Get Firmware Informare Failed\n"); 	
		proHeader->relag = USTOR_EERR;			
		proHeader->len = 0;
	}

	return EUSTOR_OK;
}

int32_t usDecode_SyncSystemTimeHandle(struct uStorPro_fsOnDev *proHeader, uint8_t *payLoad, uint32_t payLen)
{
	struct timeval tval;

	proHeader->len = 0;
	if(payLen != sizeof(int32_t)){
		DEBUG("Sync System Time Payload invaild[%d]\n", payLen); 	
		proHeader->relag = USTOR_EERR;			
		return EUSTOR_ARG;
	}
	/*payload is int32_t*/
	tval.tv_sec = *((int32_t *)payLoad);
	tval.tv_usec = 0;
	DEBUG("Set System Time To %ld\n", tval.tv_sec);
	if(settimeofday(&tval, NULL)){
		DEBUG("Set Sync System Time [%lds] Failed:%s\n", tval.tv_sec, strerror(errno)); 	
		proHeader->relag = USTOR_EERR;			
		return EUSTOR_SYS;
	}

	return EUSTOR_OK;
}

int32_t usDecode_SyncCacheHandle(struct uStorPro_fsOnDev *proHeader, uint8_t *payLoad, uint32_t payLen)
{
	sync();
	proHeader->len = 0;
	return EUSTOR_OK;
}
