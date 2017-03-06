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
#include <utime.h>

#include "usDisk.h"
#include "usUsb.h"
#include "usSys.h"
#include "protocol.h"
#include "disk_manager.h"
#include "usError.h"

#define sys_offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)

/* Purpose: container_of - cast a member of a structure out to the containing structure
 * In     : ptr   : the pointer to the member.
 *          type  : the type of the container struct this is embedded in.
 *          member: the name of the member within the struct.
 */
#define container_of(ptr, type, member) ({ \
        const typeof( ((type *)0)->member ) *__mptr = (ptr); \
        (type *)( (char *)__mptr - sys_offsetof(type,member) );})



int32_t usDisk_init(void)
{
	disk_init();
	return EUSTOR_OK;
}

/*
*action: 
*0: remove
*1: add
*/
void usDisk_PlugCallBack(int action, char *dev)
{
	udev_action raction;

	if(!dev){
		return;
	}
	strcpy(raction.dev, dev);
	raction.action = action;

	disk_aciton_func(&raction);
}

int32_t usDisk_diskRead(char filename[MAX_FILENAME_SIZE], 
						void *buff, int64_t offset, int32_t size)
{
	int fd;
	int32_t already = 0, res;

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

int32_t usDisk_diskWrite(char filename[MAX_FILENAME_SIZE], 
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

int32_t usDisk_diskCreate(char abspath[MAX_FILENAME_SIZE], 
						int8_t isdir, int32_t actime, int32_t modtime)
{
	int res;
	
	if(access(abspath, F_OK) == 0){
		return EUSTOR_OK;
	}
	if(isdir){
		res = mkdir(abspath, 0755);
	}else{
		res = creat(abspath, 0755);
	}
	if(res != 0){
		DEBUG("Create Operation Error:%s[%s]\n", strerror(errno), abspath);
		return EUSTOR_DISK_CREATE;
	}
	/*set file attribute*/
	if(actime && modtime){
		struct utimbuf buf;
		
		buf.actime= actime;
		buf.modtime = modtime;

		if(utime(abspath, &buf) != 0){
			DEBUG("Set Attribute Error:%s[%s]\n", strerror(errno), abspath);
			return EUSTOR_DISK_CREATE;
		}
	}

	return EUSTOR_OK;
}

