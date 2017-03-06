#ifndef _DISK_MANAGER_H
#define _DISK_MANAGER_H

#ifndef DEBUG
#define DEBUG
#endif
#ifdef DEBUG
#define DISKCK_DBG(fmt, arg...)                           \
        (fprintf(stderr,"(%s,%d)"fmt,            \
                 __FUNCTION__, __LINE__, ##arg))
#else
#define DISKCK_DBG(fmt, arg...)
#endif

#define DISK_FAILURE		1
#define DISK_SUCCESS		0

#define DSHORT_STR		(64)
#define DMID_STR			(512)
#define DMAX_STR			(4096)
#define DSIZ_KB			1024

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


typedef struct _udev_action_t{
	int major;
	int action;
	char dev[DSHORT_STR];
}udev_action;

typedef struct _disk_proto_t{
	char devname[DSHORT_STR];
	unsigned long long total;
	unsigned long long used;
	unsigned short  ptype; //00000000 00000000, high 8bit represent same disk flag, low 8bit represents main/part type 
	union{
		struct {	
			char vendor[DSHORT_STR];
			char serical[DMID_STR];
			char type[DSHORT_STR]; //usb or sdcard
			char disktag[DSHORT_STR];
			int status; //mounted or saferemove
			int partnum; //more than 0
		}main_info;
		struct {
			int mounted;
			char fstype[DSHORT_STR];
			char label[DSHORT_STR];
			char mntpoint[DMID_STR];
			int enablewrite;
		}part_info;	
	}partition;	
}__attribute__ ((__packed__))
disk_proto_t;

typedef struct _disk_dirlist_t{
	char partname[DSHORT_STR];
	char devname[DSHORT_STR];
	char displayname[DMAX_STR];
	int mounted;
	char type[DSHORT_STR]; //usb or sdcard
	char disktag[DSHORT_STR];	
	char fstype[DSHORT_STR];
	char label[DSHORT_STR];
	char mntpoint[DMID_STR];
	int enablewrite;
}__attribute__ ((__packed__)) disk_dirlist_t;

typedef struct _disk_disklist_t{
	char devname[DSHORT_STR];
	char vendor[DSHORT_STR];
	char serical[DMID_STR];
	char type[DSHORT_STR]; //usb or sdcard
	char disktag[DSHORT_STR];	
	unsigned long long total;
	int status; //mounted or saferemove
	int partnum; //more than 0
}__attribute__ ((__packed__)) disk_disklist_t;


int disk_init(void);
int disk_aciton_func(udev_action *action);
int disk_getdisk_lun(void *buff, int size, int *used);
int disk_getdisk_info(void *buff, int size, int *used);

#endif
