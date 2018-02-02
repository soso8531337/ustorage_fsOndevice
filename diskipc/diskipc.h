#ifndef _DISKIPC_H
#define _DISKIPC_H

#define DISK_FAILURE		1
#define DISK_SUCCESS		0

typedef struct _udev_action_t{
	int major;
	int action;
	char dev[64];
}udev_action;

int disk_init(void);
int disk_aciton_func(udev_action *action);
extern int disk_getdisk_lun(void *buff, int size, int *used);
extern int disk_getdisk_info(void *buff, int size, int *used);

#endif

