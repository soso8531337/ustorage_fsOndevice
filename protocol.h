/*
 * protocol.h
 *
 * This it the public uStorage protocol definaion
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 or version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

/*Protocol Header*/
#define PRO_HDR_SZIE				24
#define PRO_BASIC_MAGIC				0xffeeffee	//this magic is used to get ustorage protocol info, device must support

/*Basic protocol struct*/

/*Used for request protocol magic*/
struct uStorPro_basic{
	int32_t head;	/*protocol magic*/
	int32_t wtag; /*Task ID*/
	int32_t len;
	char reserved[12]; /*reserved for extend*/
}__attribute__((__packed__)); 

struct uStorPro_headInfo{
	int32_t head;	/*protocol magic*/
	int32_t wtag; /*Task ID*/
	int32_t promagic;	/*device support protocol magic*/
	int16_t version; /*protocol version*/
	int16_t relag;	/*Response Code*/	
	char reserved[8]; /*reserved for extend*/
}__attribute__((__packed__));

typedef struct acessory_parameter
{
	char manufacture[32];
	char model_name[32];
	char sn[32];
	char fw_version[16];
	char hw_version[16];
	char cardid[8];
	char license[120];
}__attribute__((__packed__))vs_acessory_parameter;


/*uStorage protocol----Filesystem on Phone*/

#define PRO_HDR				sizeof(struct scsi_head)
#define SCSI_HEAD_SIZE		sizeof(struct scsi_head)
#define SCSI_PHONE_MAGIC	0xccddeeff
#define SCSI_DEVICE_MAGIC	0xaabbccdd
#define SCSI_WFLAG  		1 << 7

enum{
	EREAD = 1,
	EWRITE=2,
	ENODISK = 3,
	EDISKLEN = 4,
	EDISKINFO=5,
	EUPDATE=6
};

enum {
	SCSI_TEST = 0,
	SCSI_READ  = 1,//28
	SCSI_WRITE = 2 | SCSI_WFLAG,//2a
	SCSI_INQUIRY = 3,//12
	SCSI_READ_CAPACITY =4,//25
	SCSI_GET_LUN = 5,
	SCSI_INPUT = 6,
	SCSI_OUTPUT = 7,
	SCSI_UPDATE_START = 8,
	SCSI_UPDATE_DATA = 9 | SCSI_WFLAG,
	SCSI_UPDATE_END = 10,
	SCSI_FIRMWARE_INFO=11,
	SCSI_SYNC_INFO=12,
};

enum {
	PRO_OK = 0,
	PRO_BADMAGIC,
	PRO_INCOMPLTE,
	PRO_BADPACKAGE,
	PRO_NOSPACE,
};

struct scsi_head{
	int32_t head;	/*Receive OR Send*/
	int32_t wtag; /*Task ID*/
	int32_t ctrid; /*Command ID*/
	uint32_t addr; /*Offset addr*512   represent sectors */
	int32_t len;
	int16_t wlun;
	int16_t relag; /*Response Code*/
}__attribute__((__packed__));

struct scsi_inquiry_info{
  int64_t size;
  char vendor[ 16];
  char product[ 32];
  char version[ 32];
  char serial[32];
}__attribute__((__packed__));



/*uStorage protocol----Filesystem on device*/
/*Request == BasicProtocolHeader+SubOperationHeader*/

#define PRO_FSONDEV_MAGIC	0xffeeff02		/*Begin from 0xffeeff00*/
#define PRO_REVER_MAGIC				0x02ffeeff
#define MAX_PATH_SIZE		4096
#define MAX_FILENAME_SIZE	256
#define MAX_PARTITIONS		6

enum{
	PRO_FSONDEV_VER1=0x0001,		/*First Version*/
	PRO_FSONDEV_VER2,	
	PRO_FSONDEV_VER3,
	PRO_FSONDEV_VER4,
	
};
enum{
	FSTYPE_FAT32=1,		/*First Version*/
	FSTYPE_EXFAT,	
	FSTYPE_NTFS,
	FSTYPE_HFS,
};

enum{
	DKTYPE_SD = 1,
	DKTYPE_USB= 2,
};

enum {
	USTOR_FSONDEV_READ  	= 1,
	USTOR_FSONDEV_WRITE 	= 2,
	USTOR_FSONDEV_CREATE	= 3,
	USTOR_FSONDEV_DELETE	= 4,
	USTOR_FSONDEV_MOVE		= 5,
	USTOR_FSONDEV_LIST		= 6,	
	USTOR_FSONDEV_DISKINFO	= 7,
	USTOR_FSONDEV_DISKLUN	= 8,
	USTOR_FSONDEV_FIRMWARE_INFO	= 9,
	USTOR_FSONDEV_SAFEREMOVE	= 10,

	USTOR_FSONDEV_UPDATE_START	= 11,
	USTOR_FSONDEV_UPDATE_DATA	= 12,
	USTOR_FSONDEV_UPDATE_END	= 13,
	USTOR_FSONDEV_FILE_INFO		= 14,
	USTOR_FSONDEV_SYNC_TIME 	= 15,	/*payload int32_t*/
	USTOR_FSONDEV_SYNC_WRITE 	= 16,
};

enum{
	USTOR_EOK=0,	/*successful*/
	USTOR_EERR =1,	/*general error*/
	USTOR_ENOTEXISTPRO,	/*protocol not exist*/
};
/*Basic Header uStorage Protocol filesystem on device*/
struct uStorPro_fsOnDev{
	int32_t head;	/*protocol magic*/
	int16_t version; /*protocol version*/
	int16_t relag;	/*Response Code*/
	int32_t wtag; /*Task ID*/
	int32_t ctrid; /*Command ID*/
	int32_t len;/*payload length*/
	int32_t reserved; /*reserved for extend*/
}__attribute__((__packed__)); 


struct diskPlugNode{
	char dev[16];	/*devname*/
	int16_t seqnum;	/*sequence number represent plug counts*/
}__attribute__((__packed__));

/*Get disk Lun sub Header*/
struct operation_diskLun{
	int16_t disknum;	/*disk number*/
	struct diskPlugNode disks[];
}__attribute__((__packed__));

struct diskInfoPartNode{
	char dev[16];	/*devname*/
	int64_t totalSize;
	int64_t usedSize;
	char mountDir[64];
	int8_t fstype;
}__attribute__((__packed__));

struct diskInfoNode{
	char dev[16];	/*devname*/
	int64_t totalSize;
	int64_t usedSize;
	int8_t type;	/*SD Card or USB*/
	int8_t enablePlug;/*1: Plug 0:internel*/
	int8_t partNum;	/*Partition counts*/
	struct diskInfoPartNode partitions[MAX_PARTITIONS];
}__attribute__((__packed__));

/*Get disk info sub Header*/
struct operation_diskInfo{
	int16_t disknum;	/*disk number*/
	struct diskInfoNode diskInfos[];
}__attribute__((__packed__));

/*Read/Write sub Header*/
struct operation_rw{
	int64_t  offset; /*Offset Read/Write Operation*/
	int32_t  size;	/*Read/Write size*/
	int16_t	 pathlen;
	char     abspath[]; /*Filename path length is 4096*/
}__attribute__((__packed__));


/*Ceate/Delete sub Header*/
struct operation_newdel{
	int8_t   isdir;	/*dir or not*/
	int32_t  actime;	/*access time you can set it when create*/
	int32_t  modtime;	/*modify time you can set it when create*/
	char     abspath[MAX_PATH_SIZE];	/*Filename*/
}__attribute__((__packed__));


/*Move operation sub Header*/
struct operation_move{
	char from[MAX_PATH_SIZE]; /*source*/
	char to[MAX_PATH_SIZE];	/*destination*/
}__attribute__((__packed__));

/*List operation sub Header*/
struct operation_list{
	char basedir[MAX_PATH_SIZE]; /*dir name*/
}__attribute__((__packed__));

/*File info sturct used for list response*/
struct fileinfo{
	char name[MAX_FILENAME_SIZE];/*file name*/
	int8_t  isdir;	/*dir or not*/
	int32_t  actime;	/*access time*/
	int32_t  modtime;	/*modify time*/
	int64_t  size;	/*file size, it is 0 when it's dir*/
}__attribute__((__packed__));

struct operation_listResponse{
	int32_t fileNum;	/*file number*/	
	int8_t finish;	/*finish or not*/
	struct fileinfo fileList[];	/*file struct list*/
}__attribute__((__packed__));


#endif
