/*
 * @brief U-Storage Project
 *
 * @note
 * Copyright(C) i4season, 2016
 * Copyright(C) Szitman, 2016
 * All rights reserved.
 *
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

#define PHONE_BUS_LOC           "usb1/1-1/1-1.2"
#define DISK_BUS_LOC            "usb1/1-1/1-1.1"

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

static struct usEventArg eventConf;


static int32_t initNetlinkSock(void)
{
	struct sockaddr_nl snl;
	int retval, sockfd = -1, dev_fd;

	memset(&snl, 0x00, sizeof(struct sockaddr_nl));
	snl.nl_family = AF_NETLINK;
	snl.nl_pid = getpid();
	snl.nl_groups = 1;

	sockfd = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
	if (sockfd == -1) {
		DEBUG("error getting socket: %s\r\n", strerror(errno));
		return -1;
	}
	retval = bind(sockfd, (struct sockaddr *) &snl,
		      sizeof(struct sockaddr_nl));
	if (retval < 0) {
		DEBUG("bind failed: %s\r\n", strerror(errno));
		close(sockfd);
		return -1;
	}
	/*Triger phone*/
	if((dev_fd = open(PHONE_BUS_LOC, O_WRONLY)) < 0 ||
			write(dev_fd, "add", 3) <= 0){
		DEBUG("Write Failed[%s]...\n", strerror(errno));
	}
	close(dev_fd);
	
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
	//	DEBUG( "add '%s' to msg.envp[%i]\r\n", msg->envp[i], i);

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
		DEBUG("DEVPATH or ACTION missing, ignore message\r\n");
		free(msg);
		return NULL;
	}
	return msg;
}

static int32_t handleStoragePlug(struct udevd_uevent_msg *msg)
{
	int countTime = 0;
	uint32_t sendCount=0;
	uint8_t diskPlug;
	uint8_t rc;
	
	if(!msg){
		return -1;
	}

	if(strncmp(msg->devname, "sd", 2) &&
			strncmp(msg->devname, "mmcblk", 6)){
		DEBUG("Unknown Disk [%s]\r\n", msg->devname);
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
			DEBUG("Open [%s] Failed:%s\r\n", 
					devbuf, strerror(errno));
			return 0;
		}else{
			close(fd);
		}
		/*preread*/
		DEBUG("ADD Device %d [%s/%s] To Storage List\r\n", 
				msg->id, msg->devname,  msg->devpath);
		if(eventConf.eventPhoneCall){
			eventConf.eventPhoneCall(1, msg->devname);
		}	
	}else if(!strcasecmp(msg->action, STOR_STR_REM)){
		DEBUG("Remove Device [%s/%s] From Storage List\r\n", 
					 msg->devname,  msg->devpath);		
		char devbuf[128] = {0};
		sprintf(devbuf, "/dev/%s", msg->devname);
		if(eventConf.eventPhoneCall){
			eventConf.eventPhoneCall(0, msg->devname);
		}	
	}else if(!strcasecmp(msg->action, STOR_STR_CHANGE)){		
		char devbuf[128] = {0};		
		int fd = -1;		
		
		DEBUG("Try To Handle Device %s [%s/%s] Event\r\n", 
					msg->action, msg->devname,  msg->devpath);
		sprintf(devbuf, "/dev/%s", msg->devname);
		if(access(devbuf, F_OK)){
			mknod(devbuf, S_IFBLK|0644, msg->devt);
		}
		/*Check dev can open*/
		if((fd = open(devbuf, O_RDONLY)) < 0){
			/*Remove ID*/
			DEBUG("We Think it may be Remove action[%s]\r\n", msg->devname);
			if(eventConf.eventPhoneCall){
				eventConf.eventPhoneCall(0, msg->devname);
			}
		}else{
			close(fd);			
			DEBUG("We Think it may be Add action[%s]\r\n", msg->devname);
			if(eventConf.eventPhoneCall){
				eventConf.eventPhoneCall(1, msg->devname);
			}
		}		
	}else{
		DEBUG("Unhandle Device %s [%s/%s] Event\r\n",
				msg->action, msg->devname,	msg->devpath);
		return 0;
	}

	return 0;
}

static int32_t handlePhonePlug(struct udevd_uevent_msg *msg)
{
	if(!msg){
		return -1;
	}
	
	if(!strcasecmp(msg->action, STOR_STR_ADD)){
		/*Add phone*/

		DEBUG("Phone PlugIN %s [%s/%s] Event\r\n",
				msg->action, msg->devname, msg->devpath);
		if(eventConf.phoneCall){
			eventConf.phoneCall(1);
		}		
	}else if(!strcasecmp(msg->action, STOR_STR_REM)){

		DEBUG("Phone PlugOUT %s [%s/%s] Event\r\n",
				msg->action, msg->devname, msg->devpath);
		if(eventConf.phoneCall){
			eventConf.phoneCall(0);
		}		
	}else{
		DEBUG("Phone Not Handle %s [%s] Event\r\n",
				msg->action,  msg->devpath);		
	}

	return 0;
}

static int32_t handlePlug(int sockfd)
{
	char buffer[UEVENT_BUFFER_SIZE*2] = {0};
	struct udevd_uevent_msg *msg;
	int bufpos; 
	ssize_t size;
	char *pos = NULL;

	size = recv(sockfd, &buffer, sizeof(buffer), 0);
	if (size <= 0) {
		DEBUG("error receiving uevent message: %s\r\n", strerror(errno));
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
		DEBUG("Invalid uevent '%s'\r\n", buffer);
		free(msg);
		return -1;
	}
	pos[0] = '\0';
	if (msg->action == NULL) {
		DEBUG("no ACTION in payload found, skip event '%s'\r\n", buffer);
		free(msg);
		return -1;
	}

	if (strcmp(msg->action, buffer) != 0) {
		DEBUG("ACTION in payload does not match uevent, skip event '%s'\r\n", buffer);
		free(msg);
		return -1;
	}
	if(!msg->subsystem || !msg->devtype){
		DEBUG("Subsystem/Devtype mismatch [%s/%s]\r\n", 
				msg->subsystem, msg->devtype);
		free(msg);
		return 0;
	}
	if(!strcasecmp(msg->subsystem, STOR_SUBSYS) &&
			!strcasecmp(msg->devtype, STOR_DEVTYPE)){
		handleStoragePlug(msg);
	}else if(strstr(msg->devpath, PHONE_BUS_LOC) &&
		!strcasecmp(msg->subsystem, PHONE_SUBSYS) &&
				!strcasecmp(msg->devtype, PHONE_DEVTYPE)){
		DEBUG("Phone Detect [%s/%s]\r\n", 
						msg->devname,  msg->devpath);
		handlePhonePlug(msg);
	}else{
		DEBUG("Unhandle DevicePlug %s [%s/%s] Event\r\n",
				msg->action, msg->devname,	msg->devpath);
	}

	free(msg);
	return 0;
}


void* vs_eventFunc(void *pvParameters)
{
	int plugSocket = 0, cnt;
	struct pollfd fds;
	

	while(1){
		if(plugSocket <= 0 && 
				(plugSocket = initNetlinkSock()) < 0){
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
				DEBUG("POLL Error:%s.\r\n", strerror(errno));
				usleep(200000);
				continue;
			}
		}else if(cnt == 0){
			/*timeout*/
			continue;
		}
		if(fds.revents & POLLIN){
			/*receive plug information*/
			handlePlug(plugSocket);
		}
	}

	return NULL;
}


int32_t usEvent_init(struct usEventArg *evarg)
{
	pthread_t eventThread;

	if(evarg){
		memcpy(&eventConf, evarg, sizeof(struct usEventArg));
	}

	if (pthread_create(&eventThread, NULL, vs_eventFunc, NULL) != 0) {
		DEBUG("ERROR: Could not start Event Thread[%s]\n", strerror(errno));
	}

	return EUSTOR_OK;
}

