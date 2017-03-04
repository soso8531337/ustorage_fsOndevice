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

		DEBUG("Read Operation:\nName:%s\nSize:%d\nOffset:%ld\n", abspath, size, offset);
		
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

		DEBUG("Write Operation:\nName:%s\nSize:%d\nOffset:%ld\n", abspath, size, offset);
		
		if(usDisk_diskRead(abspath, writeHeader+sizeof(struct operation_rw)+pathlen, 
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
