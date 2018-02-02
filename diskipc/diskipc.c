#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/un.h>

#include "protocol.h"
#include "usSys.h"
#include "diskipc.h"

#define DEBUGIPC	DEBUG
#define PRO_TIMEOUT		10
#define IPC_PATH_MDISK "/tmp/ipc_path_disk"

struct ipc_stor_header {
	int msg;
	int len;
	union {
		int flag; //use for send user assign send type
		int response; //use for send to request for response
	}direction;
};
#define DDEFINE_MSG(module, code) (((module&0x7FFF)<<16)|(code&0xFFFF))
#define MODULE_DISK 1

enum {
	MSG_DISK_INFO = DDEFINE_MSG(MODULE_DISK, 1),
	MSG_DISK_UDEV = DDEFINE_MSG(MODULE_DISK, 2),	
	MSG_DISK_TRIGER = DDEFINE_MSG(MODULE_DISK, 3), 	
	MSG_DISK_DIRLIST = DDEFINE_MSG(MODULE_DISK, 4), 	
	MSG_DISK_DISKLIST = DDEFINE_MSG(MODULE_DISK, 5), 	
	MSG_DISK_UPSMB = DDEFINE_MSG(MODULE_DISK, 6), 
	MSG_DISK_ALLINFO = DDEFINE_MSG(MODULE_DISK, 7),
};
enum {
	IPCF_NORMAL = 0,
	IPCF_ONLY_SEND,
};

enum{
	DISK_INIT=0,
	DISK_MOUNTED,
	DISK_SFREMOVE,
	DISK_WAKEUP,
	DISK_UDEV_ADD,
	DISK_UDEV_REMOVE,
	DISK_UDEV_POWEROFF,
	DISK_MNT_ERR
};

/*Public Function*/
static int ipc_setsock(int sock, int time)
{
	struct timeval timeout;

	timeout.tv_sec = time;
	timeout.tv_usec = 0;
	if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
		DEBUGIPC("rcvtimeo fail, %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

static int ipc_client_init(char *path)
{
	int sock = 0, len = 0, ret = 0;
	struct sockaddr_un addr;

	if ((!path) || (!strlen(path))) {
		DEBUGIPC("path is err\n");
		return -1;
	}

	sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sock < 0) {
		DEBUGIPC("create socket fail, errno:%d, %s\n", errno, strerror(errno));
		return -1;
	}
	/*Set socket timeout to 60s*/
	ipc_setsock(sock, PRO_TIMEOUT);

	memset (&addr, 0, sizeof(struct sockaddr_un));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, path, sizeof(addr.sun_path)-1);
	len = sizeof(addr.sun_family) + sizeof(addr.sun_path);

	ret = connect(sock, (struct sockaddr *)&addr, len);
	if (ret < 0) {
		close(sock);
		DEBUGIPC("connect error:%d, path:%s\n", ret, path);
		return -1;
	} else {
		DEBUGIPC("connect OK,socket=%d\n", sock);
	}
	return sock;
}

static int ipc_write(int fd, char *buf, int len)
{
	int wlen = 0, ylen = 0, offset = 0;

	if ((!buf) || (len <= 0)) {
		DEBUGIPC("data err\n");
		return -1;
	}

	wlen = len;
	while((ylen = write(fd, &buf[offset], wlen)) != wlen) {
		if(ylen > 0) {
			offset += ylen;
			wlen -= ylen;
		} else {
			DEBUGIPC("ylen:%d, errno:%d, err:%s\n", ylen, errno, strerror(errno));
			return -1;
		}
	}
	return 0;
}

static int ipc_read(int fd, char *buf, int len)
{
	int rlen = 0, ylen = 0, offset = 0;

	if ((!buf) || (len <= 0)){
		DEBUGIPC("data err\n");
		return -1;
	}

	rlen = len;
	while ((ylen = read(fd, &buf[offset], rlen)) != rlen) {
		if(ylen > 0) {
			offset += ylen;
			rlen -= ylen;
		} else {
			DEBUGIPC("ylen:%d, errno:%d, err:%s\n", ylen, errno, strerror(errno));
			return -1;
		}
	}
	return 0;
}

/*Send the request and receive response*/
static int stor_ipc_double_duplex(void *request, int reqlen, void**response, int *reslen)
{
	int fd, ret;
	void *resbuf = NULL;
	int memlen = 0, hdrlen = sizeof(struct ipc_stor_header);
	struct ipc_stor_header *hdr;


	if(request == NULL){
		DEBUGIPC("IPC Request is NULL\n");
		return -1;
	}
	fd = ipc_client_init(IPC_PATH_MDISK);
	if(fd < 0){
		DEBUGIPC("Error Init IPC\n");
		return -2;
	}
	ret  = ipc_write(fd, request, reqlen);
	if(ret < 0){
		DEBUGIPC("IPC Write Error[ret=%dmsg=%s]\n", ret, strerror(errno));
		close(fd);
		return -3;
	}
	DEBUGIPC("Send  IPC Request Successful: Command Point->0x%p Payload %d\n", request, reqlen);
	resbuf = calloc(1, hdrlen);
	if(resbuf == NULL){
		DEBUGIPC("IPC Calloc Error[%s]\n", strerror(errno));
		close(fd);
		return -1;
	}
	memlen = hdrlen;
	if(ipc_read(fd, (char *)resbuf, hdrlen) < 0) {
		free(resbuf); 		
		DEBUGIPC("IPC Read Error[msg=%s]\n", strerror(errno));
		close(fd);
		return -1;
	}
	hdr = (struct ipc_stor_header*)resbuf;	
	if(hdr->len){
		resbuf = realloc(resbuf, hdrlen+hdr->len);
		if(resbuf == NULL){
			DEBUGIPC("IPC realloc Error[%s]\n", strerror(errno));
			close(fd);
			return -1;
		}
		if(ipc_read(fd, (char *)(resbuf+hdrlen),  hdr->len) < 0) {
			free(resbuf);		
			DEBUGIPC("IPC Read Error[msg=%s]\n", strerror(errno));
			close(fd);
			return -1;
		}
		memlen += hdr->len;
	}
	DEBUGIPC("Recive  IPC Response Successful: Command Point->0x%p Payload %d\n", resbuf, memlen);
	close(fd);

	*response = resbuf;
	*reslen = memlen;

	return 0;
}

/*No need to implement the api*/
int disk_init(void)
{
	return DISK_SUCCESS;
}

/*No need to implement the api*/
int disk_aciton_func(udev_action *action)
{
	return DISK_SUCCESS;
}

typedef struct _disk_disklist_t{
	char devname[64];
	char vendor[64];
	char serical[512];
	char type[64]; //usb or sdcard
	char disktag[64];	
	unsigned long long total;
	int status; //mounted or saferemove
	int partnum; //more than 0
}__attribute__ ((__packed__)) disk_disklist_t;

int disk_getdisk_lun(void *buff, int size, int *used)
{
	void *request = NULL, *response = NULL;
	struct ipc_stor_header *hdr; 
	int reslen, cur;
	disk_disklist_t  *disklist = NULL;
	struct operation_diskLun *diskPtr = (struct operation_diskLun*)(buff);
	int totalSize = size, nodeSize = sizeof(struct diskPlugNode);

	DEBUGIPC("Get Disk Lun...\n");

	request = calloc(1, sizeof(struct ipc_stor_header));
	if(request == NULL){
		DEBUGIPC("Calloc Memory Failed...\n");
		return DISK_FAILURE;
	}
	hdr = (struct ipc_stor_header*)request;	
	hdr->msg = MSG_DISK_DISKLIST;
	hdr->len = 0;
	hdr->direction.flag = IPCF_NORMAL;
	if(stor_ipc_double_duplex(request, sizeof(struct ipc_stor_header), 
				&response, &reslen) < 0){
		DEBUGIPC("IPC Commuicate Failed...\n");
		free(request);		
		return DISK_FAILURE;
	}
	free(request);

	hdr = (struct ipc_stor_header*)response;
	if(hdr->direction.response  == DISK_FAILURE){
		DEBUGIPC("Handle Disk Error\n");
		if(hdr->len && response){
			free(response);
		}
		return DISK_FAILURE;
	}

	DEBUGIPC("Receive  IPC Response: Command->%d Payload->%d\n", hdr->msg, hdr->len);
	disklist = (disk_disklist_t*)(response+sizeof(struct ipc_stor_header));
	cur = 0;
	totalSize -= sizeof(struct operation_diskLun);
	struct diskPlugNode *diskNode = diskPtr->disks;	
	while(cur < hdr->len){
		if(disklist->status != DISK_MOUNTED){
			DEBUGIPC("Filter Not mounted disk---%s\n", disklist->devname);
			continue;
		}
		if(totalSize < nodeSize){
			DEBUGIPC("Buffer is Full\n");
			*used = size-totalSize;
			return DISK_SUCCESS;
		}
		diskPtr->disknum++;
		strncpy(diskNode->dev, disklist->devname, sizeof(diskNode->dev));
		diskNode->seqnum = 0;
		DEBUGIPC("Found Disk:%s count=%d\n", diskNode->dev, diskNode->seqnum);

		totalSize -= sizeof(struct diskPlugNode);
		DEBUGIPC("DiskLun:disknum:%d Dev:%s [Buffer:%p NextBuffer:%p Totalsize:%d]\n",
					diskPtr->disknum, diskNode->dev, diskNode, diskNode+1, totalSize);

		diskNode++;
		disklist++;
		cur+=sizeof(disk_disklist_t);
	}
	/*Free memory*/
	if(hdr->len && response){
		free(response);
	}
	*used = size-totalSize;
	
	return DISK_SUCCESS;
}

int disk_getdisk_info(void *buff, int size, int *used)
{
	void *request = NULL, *response = NULL;
	struct ipc_stor_header *hdr; 
	int reslen = 0;

	request = calloc(1, sizeof(struct ipc_stor_header));
	if(request == NULL){
		DEBUGIPC("Calloc Memory Failed...\n");
		return DISK_FAILURE;
	}
	hdr = (struct ipc_stor_header*)request;	
	hdr->msg = MSG_DISK_ALLINFO;
	hdr->len = 0;
	hdr->direction.flag = IPCF_NORMAL;
	if(stor_ipc_double_duplex(request, sizeof(struct ipc_stor_header), 
				&response, &reslen) < 0){
		DEBUGIPC("IPC Commuicate Failed...\n");
		free(request);		
		return DISK_FAILURE;
	}
	free(request);

	hdr = (struct ipc_stor_header*)response;
	if(hdr->direction.response  == DISK_FAILURE){
		DEBUGIPC("Handle Disk Error\n");
		if(hdr->len && response){
			free(response);
		}
		return DISK_FAILURE;
	}
	if(size < reslen){
		DEBUGIPC("Buffer Not Enough..\n");
		if(hdr->len && response){
			free(response);
		}
		return DISK_FAILURE;
	}

	*used = reslen;
	memcpy(buff, response+sizeof(struct ipc_stor_header), reslen-sizeof(struct ipc_stor_header));
	/*Free memory*/
	if(hdr->len && response){
		free(response);
	}

	return DISK_SUCCESS;
}
