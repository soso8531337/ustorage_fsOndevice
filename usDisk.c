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

#include "usDisk.h"
#include "usUsb.h"
#include "usSys.h"
#include "protocol.h"

#if !defined(BLKGETSIZE64)
#define BLKGETSIZE64           _IOR(0x12,114,size_t)
#endif


#define MSC_FTRANS_CLASS			0x08
#define MSC_FTRANS_SUBCLASS			0x06
#define MSC_FTRANS_PROTOCOL			0x50


#define STOR_DFT_PRO		"U-Storage"
#define STOR_DFT_VENDOR		"i4season"

typedef struct {
	uint8_t disknum;
	uint32_t Blocks; /**< Number of blocks in the addressed LUN of the device. */
	uint32_t BlockSize; /**< Number of bytes in each block in the addressed LUN. */
	int64_t disk_cap;
	usb_device diskdev;
}usDisk_info;

usDisk_info uDinfo[STOR_MAX];




#define UEVENT_BUFFER_SIZE		2048
#define UEVENT_NUM_ENVP			32
#ifndef NETLINK_KOBJECT_UEVENT
#define NETLINK_KOBJECT_UEVENT	15
#endif
#define STOR_SUBSYS				"block"
#define STOR_DEVTYPE			"disk"
#define STOR_STR_ADD			"add"
#define STOR_STR_REM			"remove"
#define STOR_STR_CHANGE		"change"
#define PHONE_SUBSYS				"usb"
#define PHONE_DEVTYPE			"usb_device"



struct udevd_uevent_msg {
	unsigned char id;
	char *action;
	char *devpath;
	char *subsystem;	
	char *devname;
	char *devtype;
	dev_t devt;
	unsigned long long seqnum;
	unsigned int timeout;
	char *envp[UEVENT_NUM_ENVP+1];
	char envbuf[];
};

static usDisk_info * usDisk_FindLocation(uint8_t type);

#ifndef BLKROSET
#define BLKROSET   _IO(0x12,93)
#define BLKROGET   _IO(0x12,94)
#define BLKRRPART  _IO(0x12,95)
#define BLKGETSIZE _IO(0x12,96)
#define BLKFLSBUF  _IO(0x12,97)
#define BLKRASET   _IO(0x12,98)
#define BLKRAGET   _IO(0x12,99)
#define BLKSSZGET  _IO(0x12,104)
#define BLKBSZGET  _IOR(0x12,112,size_t)
#define BLKBSZSET  _IOW(0x12,113,size_t)
#define BLKGETSIZE64 _IOR(0x12,114,size_t)
#endif

#define sys_offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)

/* Purpose: container_of - cast a member of a structure out to the containing structure
 * In     : ptr   : the pointer to the member.
 *          type  : the type of the container struct this is embedded in.
 *          member: the name of the member within the struct.
 */
#define container_of(ptr, type, member) ({ \
        const typeof( ((type *)0)->member ) *__mptr = (ptr); \
        (type *)( (char *)__mptr - sys_offsetof(type,member) );})

#define BLKRASIZE			(1024)
#define SYS_CLA_BLK 	"/sys/class/block"
#define SYS_BLK		"/sys/block"
#define SYS_DROP_CACHE		"/proc/sys/vm/drop_caches"

typedef struct  _diskLinux{
	int diskFD;
	char dev[256];
}diskLinux;

static int disk_chk_proc(char *dev)
{
	FILE *procpt = NULL;
	int ma, mi, sz;
	char line[128], ptname[64], devname[256] = {0};

	if ((procpt = fopen("/proc/partitions", "r")) == NULL) {
		DSKDEBUG("Fail to fopen(proc/partitions)\r\n");
		return 0;		
	}
	while (fgets(line, sizeof(line), procpt) != NULL) {
		memset(ptname, 0, sizeof(ptname));
		if (sscanf(line, " %d %d %d %[^\n ]",
				&ma, &mi, &sz, ptname) != 4)
				continue;
		if(!strcmp(ptname, dev)){
			DSKDEBUG("Partition File Found %s\r\n", dev);
			sprintf(devname, "/dev/%s", dev);
			if(access(devname, F_OK)){
				mknod(devname, S_IFBLK|0644, makedev(ma, mi));
			}	
			fclose(procpt);
			return 1;
		}
	}

	fclose(procpt);
	return 0;
}

static int blockdev_readahead(char *devname, int readahead)
{
	int fd;
	long larg;
	
	if(!devname){
		return -1;
	}
	fd = open(devname, O_RDWR);
	if (fd < 0) {
		return -1;
	}
	if(ioctl(fd, BLKRAGET, &larg) == -1){	
		DSKDEBUG("Get ReadAhead Error[%s]...\r\n", devname);
		close(fd);
		return -1;
	}
	if(ioctl(fd, BLKRASET, readahead) == -1){
		DSKDEBUG("Set ReadAhead Error[%s]...\r\n", devname);
		close(fd);
		return -1;
	}
	DSKDEBUG("Set %s ReadAhead From %ld To %d...\r\n", 
				devname, larg, readahead);
	close(fd);

	return 0;
}

static void dropCache(void)
{
	int dev_fd;

	if((dev_fd = open(SYS_DROP_CACHE, O_WRONLY)) < 0 ||
			write(dev_fd, "3", 1) <= 0){
		DSKDEBUG("Drop Cache Failed[%s]...\r\n", strerror(errno));
	}
	close(dev_fd);
}
void usDisk_DeviceInit(void *os_priv)
{
	struct dirent *dent;
	DIR *dir;
	struct stat statbuf;
	char sys_dir[1024] = {0};

	/*Get Block Device*/
	if(stat(SYS_CLA_BLK, &statbuf) == 0){
		strcpy(sys_dir, SYS_CLA_BLK);
	}else{
		if(stat(SYS_BLK, &statbuf) == 0){
			strcpy(sys_dir, SYS_BLK);
		}else{
			DSKDEBUG("SYS_CLASS can not find block\r\n");
			memset(sys_dir, 0, sizeof(sys_dir));
			return ;
		}
	}
		
	dir = opendir(sys_dir);
	if(dir == NULL){
		DSKDEBUG("Opendir Failed\r\n");
		return ;
	}	
	while((dent = readdir(dir)) != NULL){
		char devpath[512], linkbuf[1024] = {0};
		int len;
		char devbuf[128] = {0};
		int fd = -1;
				
		if(strstr(dent->d_name, "sd") == NULL || strlen(dent->d_name) != 3){
			if(strstr(dent->d_name, "mmcblk") == NULL || 
				strlen(dent->d_name) != 7){
				continue;
			}
		}		
		if(disk_chk_proc(dent->d_name) == 0){
			DSKDEBUG("Partition Not Exist %s\r\n", dent->d_name);
			continue;
		}
		len = strlen(sys_dir) + strlen(dent->d_name) + 1;
		sprintf(devpath, "%s/%s", sys_dir, dent->d_name);
		devpath[len] = '\0';
		if(readlink(devpath, linkbuf, sizeof(linkbuf)-1) < 0){
			DSKDEBUG("ReadLink %s Error:%s\r\n", linkbuf, strerror(errno));
			continue;
		}

		sprintf(devbuf, "/dev/%s", dent->d_name);
		/*Check dev can open*/
		if((fd = open(devbuf, O_RDONLY)) < 0){
			DSKDEBUG("Open [%s] Failed:%s\r\n", 
					devbuf, strerror(errno));
			continue;
		}else{
			close(fd);
		}
		/*preread*/
		if(strstr(dent->d_name, "mmcblk")){
			usDisk_DeviceDetect(USB_CARD, (void*)devbuf);
		}else{
			usDisk_DeviceDetect(USB_DISK, (void*)devbuf);
		}
		DSKDEBUG("ADD Device [%s] To Storage List\r\n", dent->d_name);
		break;
	}

	closedir(dir);
}
uint8_t usDisk_DeviceDetect(uint8_t type, void *os_priv)
{
	unsigned char sense_b[32] = {0};
	unsigned char rcap_buff[8] = {0};
	unsigned char cmd[] = {0x25, 0, 0, 0 , 0, 0};
	struct sg_io_hdr io_hdr;
	unsigned int lastblock, blocksize;
	int dev_fd;
	int64_t disk_cap = 0;
	diskLinux *linxDiskinfo = NULL;
	char *dev = (char *)os_priv;

	if(os_priv == NULL){
		return DISK_REGEN;
	}
	usDisk_info *pDiskInfo= usDisk_FindLocation(type);
	if(pDiskInfo == NULL){
		DSKDEBUG("No Found Location\r\n");
		return DISK_REGEN;
	}

	linxDiskinfo = calloc(1, sizeof(diskLinux));
	if(!linxDiskinfo){
		DSKDEBUG("Calloc Memory Failed\r\n");
		return DISK_REGEN;
	}
	strcpy(linxDiskinfo->dev, dev);
	pDiskInfo->diskdev.usb_type = type;
	pDiskInfo->diskdev.os_priv = (void*)(linxDiskinfo->dev);

	/*Set readahead parameter*/
	if(blockdev_readahead(dev, BLKRASIZE) <  0){
		DSKDEBUG("SetReadAhead %s Failed\r\n", dev);
	}
	linxDiskinfo->diskFD = open(dev, O_RDWR);
	if(linxDiskinfo->diskFD < 0){
			DSKDEBUG("Open diskFD %s Failed\r\n", dev);
	}
	DSKDEBUG("Open diskFD %s %d Successful\r\n", dev, linxDiskinfo->diskFD);

	dev_fd= open(dev, O_RDWR | O_NONBLOCK);
	if (dev_fd < 0 && errno == EROFS)
		dev_fd = open(dev, O_RDONLY | O_NONBLOCK);
	if (dev_fd<0){
		DSKDEBUG("Open %s Failed:%s", dev, strerror(errno));
		if(linxDiskinfo){
			close(linxDiskinfo->diskFD);
			free(linxDiskinfo);
		}
		return DISK_REGEN; 
	}

	memset(&io_hdr, 0, sizeof(struct sg_io_hdr));
	io_hdr.interface_id = 'S';
	io_hdr.cmd_len = sizeof(cmd);
	io_hdr.dxferp = rcap_buff;
	io_hdr.dxfer_len = 8;
	io_hdr.mx_sb_len = sizeof(sense_b);
	io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
	io_hdr.cmdp = cmd;
	io_hdr.sbp = sense_b;
	io_hdr.timeout = 20000;

	if(ioctl(dev_fd, SG_IO, &io_hdr)<0){
		DSKDEBUG("IOCTRL error:%s[Used BLKGETSIZE64]!", strerror(errno));
		if (ioctl(dev_fd, BLKGETSIZE64, &disk_cap) != 0) {			
			DSKDEBUG("Get Disk Capatiy Failed");
		}		
		DSKDEBUG("Disk Capacity = %lld Bytes", disk_cap);
		close(dev_fd);
		pDiskInfo->disk_cap = disk_cap;
		pDiskInfo->BlockSize = DEF_SECTOR;
		pDiskInfo->Blocks = disk_cap/pDiskInfo->BlockSize;
		/*Drop cache when the new sdcard insert*/
		DSKDEBUG("Drop Cache Becasuse OF New SDCard Insert[%s]..\r\n", dev);
		dropCache();
		return DISK_REOK;
	}

	/* Address of last disk block */
	lastblock =  ((rcap_buff[0]<<24)|(rcap_buff[1]<<16)|
	(rcap_buff[2]<<8)|(rcap_buff[3]));

	/* Block size */
	blocksize =  ((rcap_buff[4]<<24)|(rcap_buff[5]<<16)|
	(rcap_buff[6]<<8)|(rcap_buff[7]));

	/* Calculate disk capacity */
	pDiskInfo->Blocks= (lastblock+1);
	pDiskInfo->BlockSize= blocksize;	
	pDiskInfo->disk_cap  = (lastblock+1);
	pDiskInfo->disk_cap *= blocksize;
	DSKDEBUG("Disk Blocks = %u BlockSize = %u Disk Capacity=%lld\r\n", 
			pDiskInfo->Blocks, pDiskInfo->BlockSize, pDiskInfo->disk_cap);
	close(dev_fd);
	/*Drop cache when the new card insert*/
	DSKDEBUG("Drop Cache Becasuse OF New Disk Insert[%s]..\r\n", dev);
	dropCache();

	return DISK_REOK;
}

uint8_t usDisk_DeviceDisConnect(uint8_t type, void *os_priv)
{
	uint8_t curdisk = 0;	
	diskLinux *linxDiskinfo = NULL;

	if(!os_priv){
		return DISK_REPARA;
	}
	for(curdisk = 0; curdisk < STOR_MAX; curdisk++){
		/*We need to use dev[0] represent first element address, if use dev, 
			it represent arrary*/
		if(!uDinfo[curdisk].diskdev.os_priv){
			continue;
		}
		linxDiskinfo = container_of((char*)(uDinfo[curdisk].diskdev.os_priv),
										diskLinux, dev[0]);
		if(linxDiskinfo &&
				!strcmp(linxDiskinfo->dev, os_priv)){
			DSKDEBUG("Disk DiskConncect [%s]\r\n",  os_priv);
			close(linxDiskinfo->diskFD);
			free(linxDiskinfo);
			memset(&uDinfo[curdisk], 0, sizeof(usDisk_info));			
			return DISK_REOK;
		}
	}
	
	DSKDEBUG("Disk Not Found [%s]\r\n", os_priv);
	return DISK_REGEN;
}

uint8_t usDisk_cacheSYNC(int16_t wlun)
{
	diskLinux *linxDiskinfo = NULL;

	if(wlun >= STOR_MAX){
		DSKDEBUG("Cache Disk Error:%d\r\n", wlun);
		return DISK_REPARA;
	}
	if(!uDinfo[wlun].diskdev.os_priv){
		DSKDEBUG("Cache Disk May Be PlugOut:%d\r\n", wlun);
		return DISK_REGEN;
	}
	linxDiskinfo = container_of((char*)(uDinfo[wlun].diskdev.os_priv),
									diskLinux, dev[0]);
	fsync(linxDiskinfo->diskFD);

	DSKDEBUG("ReWrite Cache To DiskFD:%d Successful[wlun:%d]...\r\n", 
					linxDiskinfo->diskFD, wlun);

	return DISK_REOK;
}

#endif
static usDisk_info * usDisk_FindLocation(uint8_t type)
{
	if(type == USB_CARD && uDinfo[STOR_CARD].disknum == 0){
		DSKDEBUG("Find SDCard Location\r\n");		
		memset(&uDinfo[STOR_CARD], 0, sizeof(usDisk_info));
		uDinfo[STOR_CARD].disknum = 1<< STOR_CARD;
		return &uDinfo[STOR_CARD];
	}else if(type == USB_DISK && uDinfo[STOR_HDD].disknum == 0){
		DSKDEBUG("Find HDD Location\r\n");
		memset(&uDinfo[STOR_HDD], 0, sizeof(usDisk_info));
		uDinfo[STOR_HDD].disknum = 1<< STOR_HDD;
		return &uDinfo[STOR_HDD];
	}

	return NULL;
}

static int8_t  usDisk_FindStorage(int16_t wlun)
{
	int8_t curdisk;
	if(wlun >= STOR_MAX){
		DSKDEBUG("DiskNum:%d To Large.\r\n", wlun);
		return -1;
	}

	for(curdisk=0; curdisk< STOR_MAX; curdisk++){
		if(uDinfo[curdisk].disknum == (1 << wlun)){
			DSKDEBUG("Found Storage:%d\r\n", wlun);
			return curdisk;
		}
	}

	DSKDEBUG("No Found Storage:%d\r\n", wlun);
	return -1;
}

uint8_t usDisk_DiskReadSectors(void *buff, int16_t wlun, uint32_t secStart, uint32_t numSec)
{
	int8_t cwlun = -1;

	if(!buff || wlun > STOR_MAX){
		DSKDEBUG("DiskReadSectors Failed[DiskNum:%d].\r\n", wlun);
		return DISK_REPARA;
	}
	if((cwlun = usDisk_FindStorage(wlun)) < 0){
		return DISK_REGEN;
	}
	if(usUsb_DiskReadSectors(&(uDinfo[cwlun].diskdev), 
			buff, secStart,numSec, uDinfo[cwlun].BlockSize)){
		DSKDEBUG("DiskReadSectors Failed[DiskNum:%d secStart:%d numSec:%d].\r\n", 
						uDinfo[cwlun].disknum, secStart, numSec);
		return DISK_REGEN;
	}
	
	DSKDEBUG("DiskReadSectors Successful[DiskNum:%d secStart:%d numSec:%d].\r\n", 
					uDinfo[cwlun].disknum, secStart, numSec);
	return DISK_REOK;
}

uint8_t usDisk_DiskWriteSectors(void *buff, int16_t wlun, uint32_t secStart, uint32_t numSec)
{
	int8_t cwlun = -1;

	if(!buff || wlun > STOR_MAX){
		DSKDEBUG("DiskWriteSectors Failed[DiskNum:%d].\r\n", wlun);
		return DISK_REPARA;
	}
	if((cwlun = usDisk_FindStorage(wlun)) < 0){
		return DISK_REGEN;
	}	
	if(usUsb_DiskWriteSectors(&(uDinfo[cwlun].diskdev), 
				buff, secStart,numSec, uDinfo[cwlun].BlockSize)){
		DSKDEBUG("DiskWriteSectors Failed[DiskNum:%d secStart:%d numSec:%d].\r\n", 
						uDinfo[cwlun].disknum, secStart, numSec);
		return DISK_REGEN;
	}
	
	DSKDEBUG("DiskWriteSectors Successful[DiskNum:%d secStart:%d numSec:%d].\r\n", 
					uDinfo[cwlun].disknum, secStart, numSec);
	return DISK_REOK;	
}

uint8_t usDisk_DiskNum(void)
{
	uint8_t curdisk = 0, totalDisk = 0;

	for(curdisk = 0; curdisk < STOR_MAX; curdisk++){
		totalDisk += uDinfo[curdisk].disknum;
	}
	DSKDEBUG("Total Disk %d\r\n", totalDisk);
	return totalDisk;
}

uint8_t usDisk_DiskInquiry(int16_t wlun, struct scsi_inquiry_info *inquiry)
{
	int8_t cwlun;
	if(!inquiry){
		DSKDEBUG("usDisk_DiskInquiry Parameter Error\r\n");
		return DISK_REPARA;
	}	
	memset(inquiry, 0, sizeof(struct scsi_inquiry_info));
	if((cwlun = usDisk_FindStorage(wlun)) < 0){
		return DISK_REPARA;
	}
	memset(inquiry, 0, sizeof(struct scsi_inquiry_info));
	/*Init Other Info*/
	inquiry->size = uDinfo[cwlun].disk_cap;
	strcpy(inquiry->product, STOR_DFT_PRO);
	strcpy(inquiry->vendor, STOR_DFT_VENDOR);
	strcpy(inquiry->serial, "1234567890abcdef");
	strcpy(inquiry->version, "1.0");

	return DISK_REOK;	
}



































uint8_t usDisk_diskRead(char filename[MAX_FILENAME_SIZE], 
						void *buff, int64_t offset, int32_t size)
{
	int fd;
	int already = 0, res;

	if(strlen(filename)==0 || !buff || !size){
		DEBUG("Argument Error\n");
		return EUSTOR_ARG;
	}
	fd = open(filename, O_RDONLY);
	if(fd < 0){
		DEBUG("Open %s Error:%s\n", filename, strerror(errno));
		return EUSTOR_DISK_OPEN;
	}	
	if(lseek(fd, offset, SEEK_SET) < 0){
		DEBUG("Lseek %s Error:%s\n", filename, strerror(errno));
		close(fd);
		return EUSTOR_DISK_SEEK;
	}

	already = 0;
	do {
		res  = read(fd, buff + already, size - already);
		if (res < 0) {
			if(errno ==  EINTR ||
					errno ==  EAGAIN){
				continue;
			}
			DEBUG("Read %s Error:%s\r\n", filename, strerror(errno));
			close(fd);
			return EUSTOR_DISK_READ;		
		}else if(res == 0){
			DEBUG("Read End OF File: %s Error:%s[already=%d total=%d]\r\n", 
					filename, strerror(errno), already, size);
			close(fd);
			return EUSTOR_DISK_READ;		
		}
		already += res;
	} while (already < size);
	close(fd);

	return EUSTOR_OK;

}

uint8_t usDisk_diskWrite(char filename[MAX_FILENAME_SIZE], 
						void *buff, int64_t offset, int32_t size)
{
	int fd;
	int already = 0, res;

	if(strlen(filename)==0 || !buff || !size){
		DEBUG("Argument Error\n");
		return EUSTOR_ARG;
	}
	fd = open(filename, O_RDWR);
	if(fd < 0){
		DEBUG("Open %s Error:%s\n", filename, strerror(errno));
		return EUSTOR_DISK_OPEN;
	}	
	if(lseek(fd, offset, SEEK_SET) < 0){
		DEBUG("Lseek %s Error:%s\n", filename, strerror(errno));
		close(fd);
		return EUSTOR_DISK_SEEK;
	}

	already = 0;
	do {
		res  = write(fd, buff + already, size - already);
		if (res < 0) {
			if(errno ==  EINTR ||
					errno ==  EAGAIN){
				continue;
			}
			DEBUG("Read %s Error:%s\r\n", filename, strerror(errno));
			close(fd);
			return EUSTOR_DISK_WRITE;		
		}
		already += res;
	} while (already < size);
	close(fd);

	return EUSTOR_OK;
}


