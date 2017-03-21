#include "comlib.h"
#include "disk_manager.h"
#include "libblkid-tiny.h"
#include <linux/hdreg.h>
#include <sys/mount.h>
#include <sys/vfs.h>
#include <ctype.h>
#include <dirent.h>
#include "sg_io.h"
#define _GNU_SOURCE    /* or _SVID_SOURCE or _BSD_SOURCE */
#include <mntent.h>
#include "protocol.h"


#define MMCBLK_MAX_PART	8
#define MAX_HUB				8
#define DISK_LIMIT			(100*1024*1024*1024) //Byte
#define DISK_HUB_LOCATION		"/sys/devices/platform/"
#define DISK_BLOCK_PATH		"/sys/block/"
#define HUB_FLAG				"hci-platform"
#define DISK_DEVICE_PATH	"/proc/devices"
#define DISK_PROC_PARTITION	"/proc/partitions"
#define DISK_MNT_PARAMETER	"/etc/config/mntpara"
#define DISK_MNT_SERVICE	"/etc/config/mntservice"
#define DISK_COMMERICAL_PATH		"/proc/modules"
#define PATH_MTAB_FILE         "/proc/mounts"
#define DISK_TAG_FILE		"/etc/init.d/disktag"
#define DISK_MNT_PREFIX	"/data"
#define DISK_MNT_DIR		"/etc/mnt_preffix"


enum{
	DISK_MMC_MAIN=1,
	DISK_MMC_PART,
	DISK_USB_MAIN,
	DISK_USB_PART,
	DISK_MAIN = 99,
	DISK_PART = 100
};

enum{
	SRV_SMB=1<<0,
	SRV_DLNA=1<<1,
	SRV_UPNPD=1<<2,
};
enum{
	SRV_STOP=1<<0,
	SRV_START=1<<1,
	SRV_RESTART=1<<2,
};

typedef struct _event_block{
	char action[32];
	char type[32];
	char	dev[32];
	int partnum;	
}event_block; //notify upnpd disk add or remove

typedef struct _hub_info{
	char hubflag[DSHORT_STR];
	char baselocation[DMAX_STR];
	int speed;
}hub_info;

typedef struct _device_hub{
	hub_info hub[MAX_HUB];
	int curnum;
}device_hub;

typedef struct _disk_mnt_para_t{	
	struct list_head node;
	char fsname[DSHORT_STR];
	char readahead[DSHORT_STR];
	int rw;
	char umask[DSHORT_STR];
	char iocharset[DSHORT_STR];
	char shortname[DSHORT_STR];
	char errors[DSHORT_STR];
}disk_mnt_para_t;

typedef struct _disktag_t{	
	struct list_head node;
	char busflag[DSHORT_STR];
	char disktag[DSHORT_STR];	
	char diskvolume[DSHORT_STR];
	char displayname[DSHORT_STR];	
}disktag_t;

typedef struct _disk_major_t{
	struct list_head node;
	int major;
	char name[DSHORT_STR];
}disk_major_t;

typedef struct _disk_baseinfo_t{
	char devname[DSHORT_STR];
	int major;
	int minor;
	unsigned long long total;
	unsigned long long used;
}disk_baseinfo_t;

typedef struct _disk_partinfo_t{
	struct list_head node;
	disk_baseinfo_t info;
	int mounted;
	char fstype[DSHORT_STR];
	char label[DSHORT_STR];
	char mntpoint[DMID_STR];	
	char display[DMID_STR];
	int enablewrite;
	time_t uptime;
}disk_partinfo_t;


typedef struct _disk_maininfo_t{	
	struct list_head node;
	disk_baseinfo_t info;
	char vendor[DSHORT_STR];
	char serical[DMID_STR];
	char type[DSHORT_STR]; //usb or sdcard
	char disktag[DSHORT_STR];	
	char display[DSHORT_STR];
	int status; //mounted or saferemove
	int partnum; //more than 0
	int isgpt;
	int speed;
	int plugcount;
	struct list_head partlist;
}disk_maininfo_t;

typedef struct _disk_info_t{
	struct list_head list;
	int disk_num;
	struct list_head disk_major;	
	struct list_head mnt_parameter;
	struct list_head mnt_disktag;
	device_hub hubinfo;
}disk_info_t;

typedef struct _duser_list{
	char username[DSHORT_STR];
	char passwd[DMID_STR];
	char directory[DMAX_STR];
	int enable;
}duser_list;

char mnt_preffix[DMID_STR];

static unsigned int plugcount = 0;
static disk_info_t *i4disk = NULL;


/*Function Declear*/
int disk_chk_init(void);
void disk_print_partition_info(disk_info_t *pdisk);

int disk_gethub_speed(char *spath)
{
	int fd;
	char strspeed[DSHORT_STR] = {0}, speedpath[DMAX_STR];
	
	if(spath == NULL){
		return -1;
	}
	snprintf(speedpath, DMAX_STR-1, "%s/speed", spath);
	fd = open(speedpath, O_RDONLY);
	if(fd < 0){
		DISKCK_DBG("Open %s Failed[%s]\n", speedpath, strerror(errno));
		return -1;
	}
	if(read(fd, strspeed, DSHORT_STR) <=0){
		DISKCK_DBG("Read Error:%s\n", strerror(errno));
		close(fd);
		return -1;
	}
	close(fd);
	return atoi(strspeed);
}

int disk_gethub_info(device_hub *hubinfo)
{
	DIR * dir, *subdir;
	struct dirent *ent, *subent;
	int speed;
	char locadir[DMAX_STR], hubdir[DMAX_STR];
	
	if(hubinfo == NULL){
		return DISK_FAILURE;
	}
	memset(hubinfo, 0, sizeof(device_hub));
	dir = opendir(DISK_HUB_LOCATION);
	if(!dir){
		DISKCK_DBG("OpenDir %s Failed[%s]\n", DISK_HUB_LOCATION, strerror(errno));
		return DISK_FAILURE;
	}
	while ((ent = readdir(dir))){
		if ( *ent->d_name == '.'  
			|| strstr(ent->d_name, HUB_FLAG) == NULL){
			continue;
		}
		memset(locadir, 0, sizeof(locadir));
		snprintf(locadir, sizeof(locadir)-1, "%s%s", DISK_HUB_LOCATION, ent->d_name);
		DISKCK_DBG("Loop SubDir--->%s\n", locadir);
		subdir = opendir(locadir);
		if(!subdir){
			DISKCK_DBG("OpenDir %s Failed[%s]\n", locadir, strerror(errno));
			continue;
		}
		while ((subent = readdir(subdir))){
			if ( *subent->d_name == '.'  
				|| strncmp(subent->d_name, "usb", 3)){
				continue;
			}
			snprintf(hubdir, sizeof(hubdir)-1, "%s/%s/", locadir, subent->d_name);
			if((speed = disk_gethub_speed(hubdir)) < 0){
				continue;
			}
			strcpy(hubinfo->hub[hubinfo->curnum].hubflag, subent->d_name);
			strcpy(hubinfo->hub[hubinfo->curnum].baselocation, locadir);
			hubinfo->hub[hubinfo->curnum].speed = speed;
			DISKCK_DBG("Found A HUB:\n\tLocation:%s\n\tHubFlag:%s\n\tSpeed:%dMB\n",
					hubinfo->hub[hubinfo->curnum].baselocation, hubinfo->hub[hubinfo->curnum].hubflag,
					hubinfo->hub[hubinfo->curnum].speed);
			hubinfo->curnum++;		
		}
		closedir(subdir);
		
	}

	closedir(dir);

	return DISK_SUCCESS;
}

int disk_get_usbspeed(device_hub *hubinfo, char  *dev)
{
	char syspath[DMAX_STR] = {0}, truepath[DMAX_STR] = {0};
	int inter = 0;
	
	if(hubinfo== NULL || dev == NULL){
		return 0;
	}
	snprintf(syspath, DMAX_STR-1, "%s%s", DISK_BLOCK_PATH, dev);
	if(readlink(syspath, truepath, DMAX_STR-1) < 0){
		DISKCK_DBG("ReadLink %s Failed:%s\n", syspath, strerror(errno));
		return 0;
	}
	DISKCK_DBG("TurePath-->%s\n", truepath);
	for(inter = 0; inter <= hubinfo->curnum; inter++){
		if(strstr(truepath, hubinfo->hub[inter].hubflag)){
			DISKCK_DBG("Found HUB Flag:%s Speed:%dMB\n", 
				hubinfo->hub[inter].hubflag, hubinfo->hub[inter].speed);
			return hubinfo->hub[inter].speed;
		}
	}

	return 0;
}

int disk_major_list_parse(disk_info_t *pdisk)
{
	FILE *procpt = NULL;
	char line[DSHORT_STR] = {0}, ptname[DSHORT_STR];
	int ma;
	disk_major_t *node;

	if(!pdisk){
		DISKCK_DBG("Please INIT disk structure...\n");
		return DISK_FAILURE;
	}
	if ((procpt = fopen(DISK_DEVICE_PATH, "r")) == NULL) {
		DISKCK_DBG("Fail to fopen(%s)", DISK_DEVICE_PATH);		
		return DISK_FAILURE;
	}

	while (fgets(line, sizeof(line), procpt) != NULL) {
		memset(ptname, 0, sizeof(ptname));
		if (sscanf(line, " %d %[^\n ]",
			&ma,  ptname) != 2)
			continue;
		if (strcmp(ptname, "sd") != 0 &&
				strcmp(ptname, "mmc") != 0) {
			continue;
		}

		node = calloc(1, sizeof(disk_major_t));
		if(node == NULL){
			DISKCK_DBG("Memory Calloc Failed\n");
			fclose(procpt);			
			return DISK_FAILURE;
		}
		node->major = ma;
		snprintf(node->name, DSHORT_STR, "%s", ptname);		
		list_add_tail(&node->node, &pdisk->disk_major);
		memset(line, 0, sizeof(line));
	}
	
	fclose(procpt);
	return DISK_SUCCESS;
}

int disk_mnt_parameter_parse(disk_info_t *pdisk)
{
	disk_mnt_para_t *pamter, *_pamter;
	FILE *procpt = NULL;
	char *flag = NULL, *base = NULL;
	char line[DMID_STR] = {0}, fstype[DSHORT_STR], fspara[DMID_STR];

	if ((procpt = fopen(DISK_MNT_PARAMETER, "r")) == NULL) {
		DISKCK_DBG("Fail to fopen(%s)", DISK_MNT_PARAMETER);		
		return DISK_FAILURE;
	}
	while (fgets(line, sizeof(line), procpt) != NULL) {
		int vaild = 0;
		memset(fstype, 0, sizeof(fstype));
		memset(fspara, 0, sizeof(fspara));
		
		if (sscanf(line, "%s %[^\n ]",
			fstype,  fspara) != 2){
			continue;
		}
		pamter = calloc(1, sizeof(disk_mnt_para_t));
		if(pamter == NULL){
			DISKCK_DBG("Calloc Memeory Error\n");
			fclose(procpt);
			return DISK_FAILURE;
		}

		strcpy(pamter->fsname, fstype);
		base = fspara;
		while((flag = strchr(base, ',')) || !vaild){	
			char key[DSHORT_STR]={0}, value[DSHORT_STR] = {0};

			if(flag){
				*flag = '\0';
			}
			if (sscanf(base, "%[^=]=%[^\n ]",
				key,  value) != 2){
				vaild = 0;
				break;
			}
			if(flag){
				base = flag+1;
			}
			
			if(strcmp(key, "readahead") == 0){
				strcpy(pamter->readahead, value);
				vaild++;
			}else if(strcmp(key, "rw") == 0){
				pamter->rw = atoi(value);				
				vaild++;
			}else if(strcmp(key, "umask") == 0){
				strcpy(pamter->umask, value);				
				vaild++;
			}else if(strcmp(key, "iocharset") == 0){
				strcpy(pamter->iocharset, value);				
				vaild++;
			}else if(strcmp(key, "shortname") == 0){
				strcpy(pamter->shortname, value);				
				vaild++;
			}else if(strcmp(key, "errors") == 0){
				strcpy(pamter->errors, value);				
				vaild++;
			}else{
				DISKCK_DBG("Unknown Mount Paramter-->%s/%s\n", key, value);
			}
			if(flag == NULL){
				DISKCK_DBG("Just one option  break out-->%s/%s\n", key, value);
				break;
			}
		}
		
		if(vaild){
			list_add_tail(&pamter->node , &pdisk->mnt_parameter);
		}else{
			DISKCK_DBG("Warning::mount Parameter vaild is 0\n");
			free(pamter);
		}	
	}
	
	fclose(procpt);
	
	list_for_each_entry_safe(pamter, _pamter, &(pdisk->mnt_parameter), node) {
		printf("\nFsname:%s\n", pamter->fsname);
		if(pamter->readahead[0])
			printf("\treadahead:%s\n", pamter->readahead);
		printf("\trw:%d\n", pamter->rw);	
		if(pamter->umask[0])
			printf("\tumask:%s\n", pamter->umask);		
		if(pamter->iocharset[0])
			printf("\tiocharset:%s\n", pamter->iocharset);	
		if(pamter->shortname[0])
			printf("\tshortname:%s\n", pamter->shortname);
		printf("\n");
	}

	return DISK_SUCCESS;
}

int disk_mnt_disktag_parse(disk_info_t *pdisk)
{
    FILE *procpt = NULL;
    char line[DMID_STR]={0}, diskflag[DSHORT_STR], disktag[DSHORT_STR], diskvolume[DSHORT_STR];
	char diskdisplay[DSHORT_STR] = {0};
	disktag_t *ptag, *pptag, *ntag;
	
	if(!pdisk){
		DISKCK_DBG("Please INIT disk structure...\n");
		return DISK_FAILURE;
	}

    if ((procpt = fopen(DISK_TAG_FILE, "r")) == NULL) {
            DISKCK_DBG("Fail to fopen(%s), DiskTag List is NULL", DISK_TAG_FILE);
            return DISK_SUCCESS;
    }

    while (fgets(line, sizeof(line), procpt) != NULL) {
        memset(disktag, 0, sizeof(disktag));			
        memset(diskflag, 0, sizeof(diskflag));				
        memset(diskvolume, 0, sizeof(diskvolume));				
        memset(diskdisplay, 0, sizeof(diskdisplay));
		if(sscanf(line, "%s  %s %s %[^\n ]",
                        diskflag, disktag, diskvolume, diskdisplay) == 4){
			DISKCK_DBG("Get DiskDiskPlay: %s\n", diskdisplay);
		}else if(sscanf(line, "%s  %s %[^\n ]",
                 diskflag, disktag, diskvolume) == 3){
			strcpy(diskdisplay, "NONE");
		}else{
			continue;
		}		
		list_for_each_entry_safe(ptag, pptag, &pdisk->mnt_disktag, node) {
			if(strcmp(diskflag, ptag->busflag) == 0){
				DISKCK_DBG("Update DiskTag: %s==>%s->%s...\n", diskflag, ptag->disktag, disktag);
				strcpy(ptag->disktag, disktag);				
				strcpy(ntag->diskvolume, diskvolume);				
				strcpy(ntag->displayname, diskdisplay);
				continue;
			}
		}
		/*New Node to insert*/
		ntag = calloc(1, sizeof(disktag_t));
		if(ntag == NULL){
			DISKCK_DBG("Calloc Memory Failed\n");
			fclose(procpt);
			return DISK_FAILURE;
		}
		strcpy(ntag->busflag, diskflag);
		strcpy(ntag->disktag, disktag);
		strcpy(ntag->diskvolume, diskvolume);		
		strcpy(ntag->displayname, diskdisplay);
		list_add_tail(&ntag->node , &pdisk->mnt_disktag);
		DISKCK_DBG("Insert DISKTAT-->%s %s %s %s\n", 
			ntag->busflag, ntag->disktag, ntag->diskvolume, ntag->displayname);
		memset(line, 0, sizeof(line));
	}
	fclose(procpt);

	return DISK_SUCCESS;
}

int disk_aciton_notify_upnp(disk_partinfo_t *part, int action, char *dev)
{
	return DISK_SUCCESS;
}

/*
* 0 : private
* 1 : public
*/
int disk_check_smb_user_onoff(char *config)
{
	int keylen;
	FILE *fp;
	char line[DMAX_STR] = {0};
#define SMB_USER_ONOFF		"smb_public"

	if(config == NULL){
		return 1;
	}
	fp = fopen(config, "r");
	if (fp == NULL){
		return 1;
	}
	keylen = strlen(SMB_USER_ONOFF);
	while (fgets(line, 2048, fp)) {
		if (strncmp(SMB_USER_ONOFF, line, keylen) == 0) {
			if (line[keylen] == '=') {
				fclose(fp);
				DISKCK_DBG("Public SMB is %d...\n", atoi(line+keylen+1));
				return atoi(line+keylen+1);
			}
		}
	}
	fclose(fp);
	return 1;
}

void disk_excute_factory_script(char *basepath)
{
	return ;
}

int disk_dirlist_display_name(char *preffix, char *before, char *after)
{
	char *flag = NULL;
	
	if(!before || !after){
		return DISK_FAILURE;
	}
	if(preffix && strlen(before) < strlen(preffix)){
		return DISK_FAILURE;
	}
	if(preffix && strncmp(before, preffix, strlen(preffix))){
		DISKCK_DBG("No found %s in %s...\n", preffix, before);
		return DISK_FAILURE;
	}
	flag = before+strlen(preffix);
	while(*flag == '/'){
		flag++;
	}
	strcpy(after, flag);
	while((flag = strchr(after, '/'))){
		*flag = '_';
	}
	DISKCK_DBG("Diskplay name %s->%s...\n", before, after);
	
	return DISK_SUCCESS;
}

char* find_mount_point(char *block)
{
	FILE *fp = fopen("/proc/mounts", "r");
	char line[DMID_STR];
	int len = strlen(block);
	char *point = NULL;

	if(!fp)
		return NULL;

	while (fgets(line, sizeof(line), fp)) {
		if (!strncmp(line, block, len)) {
			char *p = &line[len + 1];
			char *t = strstr(p, " ");

			if (!t) {
				fclose(fp);
				return NULL;
			}
			*t = '\0';
			point = p;
			break;
		}
	}

	fclose(fp);

	return point;
}

int check_commerical_driver(char *fstype, char *caldrv)
{
	FILE *procpt;
	char line[DMID_STR] = {0}, drver[DSHORT_STR];
	int size;
	char *fsflag  = NULL;
	
	if(fstype == NULL || caldrv == NULL){
		return DISK_FAILURE;
	}
	if(strcmp(fstype, "vfat") == 0||
			strcmp(fstype, "fat32") == 0){
		fsflag = "tfat";
	}else if(strcmp(fstype, "exfat") == 0){
		fsflag = "texfat";
	}else if(strcmp(fstype, "ntfs") == 0){
		fsflag = "tntfs";
	}else if(strncmp(fstype, "hfs", 3) == 0){
		fsflag = "thfsplus";
	}else{
		DISKCK_DBG("Use Free filesystem (%s)\n", fstype);
		return DISK_FAILURE;
	}
    if ((procpt = fopen(DISK_COMMERICAL_PATH, "r")) == NULL) {
            DISKCK_DBG("Fail to fopen(%s)", DISK_COMMERICAL_PATH);
	return DISK_FAILURE;
    }

    while (fgets(line, sizeof(line), procpt) != NULL) {
	            memset(drver, 0, sizeof(drver));
        if (sscanf(line, " %s %d",
                        drver, &size) != 2)
                continue;

		if(strcmp(fsflag, drver) == 0){
			strcpy(caldrv, drver);
			fclose(procpt); 
			DISKCK_DBG("Found Commerical Filesystem Driver (%s)\n", caldrv);
			return DISK_SUCCESS;
		}
		memset(line, 0, sizeof(line));
    }
	fclose(procpt);

	return DISK_FAILURE;
}

int generate_mount_parameter(struct list_head *plist, char *fstype, char *para)
{
	disk_mnt_para_t *pamter, *_pamter;
	int byte=0;
	char *tpara = para;

	if(fstype == NULL || para == NULL){
		return DISK_FAILURE;
	}
	list_for_each_entry_safe(pamter, _pamter, plist, node) {
		if(strcmp(pamter->fsname, fstype) == 0){
			/*Find the filesystem*/
			if(pamter->iocharset[0]){
				byte = sprintf(tpara, "iocharset=%s,", pamter->iocharset);				
				tpara += byte;
			}
			if(pamter->readahead[0]){
				byte = sprintf(tpara, "readahead=%s,", pamter->readahead);				
				tpara += byte;
			}
			if(pamter->shortname[0]){
				byte = sprintf(tpara, "shortname=%s,", pamter->shortname);				
				tpara += byte;
			}
			if(pamter->errors[0]){
				byte = sprintf(tpara, "errors=%s,", pamter->errors);				
				tpara += byte;
			}			
			if(pamter->umask[0]){
				byte = sprintf(tpara, "umask=%s,", pamter->umask);				
				tpara += byte;
			}
			if(pamter->rw){
				byte = sprintf(tpara, "%s,", "rw");				
				tpara += byte;
			}
			if(tpara != para){
				*(--tpara) = '\0';
			}
			DISKCK_DBG("Mount Parameter is %s\n", para);
			return DISK_SUCCESS;
		}
	}

	return DISK_SUCCESS;
}


int generate_mount_point(char *disktag, char *display, disk_partinfo_t *pinfo)
{
	int volnum = 0, numax = 0;
	char tmntdir[DMAX_STR] = {0}, *ptr = NULL;
	
	if(access(mnt_preffix, F_OK) != 0){
		mkdir(mnt_preffix, 0755);
	}
	ptr = pinfo->info.devname+strlen(pinfo->info.devname)-1;
	while(isdigit(*ptr) && numax < 2){
		volnum += ((numax*10)+(*ptr-'0'));
		numax++;
		ptr--;
	}
	if(strlen(disktag) == 0){
		sprintf(pinfo->mntpoint, "%s/%s", mnt_preffix, pinfo->info.devname);
	}else{
		sprintf(pinfo->mntpoint, "%s/%s%d", mnt_preffix, disktag, volnum==0?1:volnum);
	}
	if(display &&
			strcasecmp(display, "None") != 0){
		sprintf(pinfo->display, "%s%d", display, volnum==0?1:volnum);
	}else{
		memset(pinfo->display, 0, sizeof(pinfo->display));
	}
	DISKCK_DBG("Generate Mount Dir ==>%s\n", pinfo->mntpoint);
	if(access(pinfo->mntpoint, F_OK) != 0){
		ptr = strrchr(pinfo->mntpoint, '/');
		if(ptr){
			strncpy(tmntdir, pinfo->mntpoint, ptr-pinfo->mntpoint);
			if(access(tmntdir, F_OK) != 0){
				mkdir(tmntdir, 0777);
			}
		}
		mkdir(pinfo->mntpoint, 0777);
	}
	return DISK_SUCCESS;
}

int update_partition_capacity_rw(disk_partinfo_t *part)
{
	struct statfs s;
	char filename[DMAX_STR] = {0};
	int fd;
	time_t cmlog = 0;
#define WRITE_TIME_LIMIT	45

	if(part == NULL){
		return DISK_FAILURE;
	}
	
	if (!statfs(part->mntpoint, &s) && (s.f_blocks > 0)) {

		part->info.total = (unsigned long long)s.f_blocks * 
			(unsigned long long)s.f_bsize;
		part->info.used  = (unsigned long long)(s.f_blocks - s.f_bfree) * 
			(unsigned long long)s.f_bsize;
		DISKCK_DBG("Update %s Capacity Finish-->[%lld/%lld]\n", 
			part->info.devname, part->info.used, part->info.total);
	}

	cmlog = time(NULL)-part->uptime;
	if(part->enablewrite == 0 || 
		cmlog  >= WRITE_TIME_LIMIT){
		DISKCK_DBG("Cache time Out or %s Read Only...\n", part->info.devname);		
		sprintf(filename, "%s/.readonly.tst", part->mntpoint);
		fd = open(filename, O_CREAT|O_WRONLY|O_TRUNC, 0755);
		if(fd < 0 && errno == EROFS){
			DISKCK_DBG("WARNING %s Read Only...\n", part->info.devname);		
			part->enablewrite = 0;
		}else{
			DISKCK_DBG("%s Enable Write...\n", part->info.devname);		
			part->enablewrite = 1;
		}
		close(fd);
		remove(filename);
		part->uptime = time(NULL);
	}else{
		DISKCK_DBG("Cache time left %lds...\n", WRITE_TIME_LIMIT -cmlog);		
	}
	return DISK_SUCCESS;
}

int get_partition_capacity_rw(disk_partinfo_t *part)
{
	FILE *file = NULL;
	struct mntent *mount_entry = NULL, mntdata;
	char mbuf[DMID_STR] = {0}, devbuf[DSHORT_STR] = {0};
	int status = 0;
	struct statfs s;
	char filename[DMAX_STR] = {0};
	int fd;

	if(part == NULL){
		return DISK_FAILURE;
	}
	/* Open mount file	*/
	if ((file = setmntent(PATH_MTAB_FILE, "r")) == NULL) {
		DISKCK_DBG("System call setmntent return error\n");
		return DISK_FAILURE;
	}
	memset(mbuf, 0, sizeof(mbuf));
	sprintf(devbuf, "/dev/%s", part->info.devname);
	/* Read mount file  */
	do {
		mount_entry = getmntent_r(file, &mntdata, mbuf, sizeof(mbuf));
		if (mount_entry == NULL) { /* Finish read file */
			status = 0;
			break;
		}
		if (strcmp(devbuf, mount_entry->mnt_fsname)) {
			continue;
		}
		
		strcpy(part->mntpoint, mount_entry->mnt_dir);
		if (!statfs(part->mntpoint, &s) && (s.f_blocks > 0)) {	
			part->info.total = (unsigned long long)s.f_blocks * 
				(unsigned long long)s.f_bsize;
			part->info.used  = (unsigned long long)(s.f_blocks - s.f_bfree) * 
				(unsigned long long)s.f_bsize;
			DISKCK_DBG("Update %s Capacity Finish-->[%lld/%lld]\n", 
				part->info.devname, part->info.used, part->info.total);
		}
		status = 1;
		break; /* Terminate searching  */
		
	} while (1);

	endmntent(file);

	if(status == 0){
		DISKCK_DBG("%s No Found In mounts...\n", part->info.devname);
		return DISK_SUCCESS;
	}
	sprintf(filename, "%s/.readonly.tst", part->mntpoint);
	fd = open(filename, O_CREAT|O_TRUNC|O_RDWR, 0755);
	if(fd < 0 && errno == EROFS){
		DISKCK_DBG("WARNING %s Read Only...\n", part->info.devname);		
		part->enablewrite = 0;
	}else{
		DISKCK_DBG("%s Enable Write...\n", part->info.devname);		
		part->enablewrite = 1;
	}
	close(fd);
	remove(filename);	
	part->uptime = time(NULL);
	
	return DISK_SUCCESS;
}

void disk_trigger_udevadd(char *devname, int major)
{
	char cmdbuf[DMID_STR] = {0};
	if(devname == NULL){
		return;
	}
	
	sprintf(cmdbuf, "/usr/sbin/disktriger 4 %d %s", major, devname);
	DISKCK_DBG("Mannual Exe %s\n", cmdbuf);
	system(cmdbuf);
}
disk_major_t* disk_search_major_list(struct list_head *mlist, int major)
{
	disk_major_t *pnode, *_node;

	if(mlist == NULL){
		return NULL;
	}
	list_for_each_entry_safe(pnode, _node, mlist, node) {
		if(pnode->major == major){
			DISKCK_DBG("Found Major %d\n", major);
			return pnode;
		}
	}
	DISKCK_DBG("No Found Major %d\n", major);
	return NULL;
}

disk_maininfo_t* disk_search_partition_list(disk_info_t *pdisk, char *name, int search_part)
{
	disk_maininfo_t *node, *_node;
	disk_partinfo_t *pnode, *_pnode;

	if(!pdisk || !name){
		return NULL;
	}

	list_for_each_entry_safe(node, _node, &(pdisk->list), node) {
		if(search_part == 0){
			/*Just search Main Partition*/
			if(strcmp(node->info.devname, name) == 0){
				return node;
			}
		}else if(search_part == 1){
			if(strncmp(node->info.devname, name, strlen(node->info.devname)) == 0){
				list_for_each_entry_safe(pnode, _pnode, &(node->partlist), node) {
					if(strcmp(pnode->info.devname, name) == 0){
						return node;
					}
				}
				return NULL;
			}
		}
	}

	return NULL;
}

int disk_fill_main_partition_baseinfo(disk_info_t *pdisk, disk_maininfo_t *mdisk)
{	
	char devbuf[DSHORT_STR] = {0}, rlink[DMID_STR], linkbuf[DMID_STR] = {0};
	int ret;
	disktag_t *ptag, *pptag;
	
	if(!mdisk){
		return DISK_FAILURE;
	}
	sprintf(devbuf, "/dev/%s", mdisk->info.devname);
	ret = sg_get_disk_vendor_serical(devbuf, mdisk->vendor, mdisk->serical);
	if(ret != 0){
		DISKCK_DBG("Get %s Vendor Serical Failed\n", devbuf);
	}
	ret = sg_get_disk_space(devbuf, &(mdisk->info.total));
	if(ret != 0){
		DISKCK_DBG("SCSI Get %s Capacity Failed, Use IOCTRL\n", devbuf);
		ret = ioctl_get_disk_space(devbuf, &(mdisk->info.total));
		if(ret != 0){
			DISKCK_DBG("IOCTRL Get %s Capacity Failed, Give up\n", devbuf);
		}
	}
	/*Get disk speed*/
	mdisk->speed = disk_get_usbspeed(&(pdisk->hubinfo), mdisk->info.devname);
	/*Get disk pt*/
	if(probe_ptable_gpt(devbuf) == 1){
		mdisk->isgpt = 1;
	}else{
		mdisk->isgpt = 0;
	}	
	DISKCK_DBG("Foggy Jundge %s PT is %s\n", 
			mdisk->info.devname, mdisk->isgpt ==1?"GPT":"MBR");
	/*Loop Up List*/
	sprintf(rlink, "/sys/block/%s", mdisk->info.devname);
	
	ret = readlink(rlink, linkbuf, DMID_STR-1);
	if(ret == -1){
		DISKCK_DBG("ReadLink %s Failed:%s..\n", rlink, strerror(errno));
		memset(mdisk->disktag, 0, sizeof(mdisk->disktag));
		return DISK_SUCCESS;
	}
	list_for_each_entry_safe(ptag, pptag, &pdisk->mnt_disktag, node) {
		DISKCK_DBG("LinkBuf=%s-->%s\n", linkbuf, ptag->busflag);
		if(strstr(linkbuf, ptag->busflag) != NULL){
			if(strcasecmp(ptag->disktag, "None") == 0){
				strcpy(mdisk->disktag, ptag->diskvolume);
			}else{
				sprintf(mdisk->disktag, "%s/%s", ptag->disktag, ptag->diskvolume);
			}
			strcpy(mdisk->display, ptag->displayname);
			DISKCK_DBG("Found DiskTag: %s==>%s[Display:%s]..\n", 
					ptag->busflag, mdisk->disktag, mdisk->display);
			return DISK_SUCCESS;
		}
	}
	/*We Need to confirm Disktag*/
	if(strcpy(mdisk->type, "SD") == 0){
		strcpy(mdisk->disktag, "SD_Card");
		DISKCK_DBG("No Config DiskTag Finish[%s]..\n", mdisk->disktag);
		return DISK_SUCCESS;
	}
	memset(mdisk->disktag, 0, sizeof(mdisk->disktag));
	
	DISKCK_DBG("No DiskTag [%s]..\n", mdisk->info.devname);
	return DISK_SUCCESS;	
}

int disk_insert_main_partition(disk_info_t *pdisk, disk_maininfo_t **mdisk, char *devname)
{
	FILE *procpt = NULL;
	char line[DMID_STR]={0}, ptname[DSHORT_STR], devbuf[DMID_STR]= {0};
	int ma, mi, sz, ret;
	disk_maininfo_t *tmain = NULL;

	if(!pdisk || !mdisk || !devname){
		return DISK_FAILURE;
	}
	
	if ((procpt = fopen(DISK_PROC_PARTITION, "r")) == NULL) {
			DISKCK_DBG("Fail to fopen(%s)", DISK_PROC_PARTITION);
			return DISK_FAILURE;
	}

	while (fgets(line, sizeof(line), procpt) != NULL) {
				memset(ptname, 0, sizeof(ptname));
		if (sscanf(line, " %d %d %d %[^\n ]",
				&ma, &mi, &sz, ptname) != 4){				
			memset(line, 0, sizeof(line));
			continue;
		}	

		if (strcmp(ptname, devname) != 0) {
			memset(line, 0, sizeof(line));			
			continue;
		}
		memset(devbuf, 0, sizeof(devbuf));
		sprintf(devbuf, "/dev/%s", ptname);
		if(access(devbuf, F_OK) != 0){
			DISKCK_DBG("Create Node %s", devbuf);
			mknod(devbuf, S_IFBLK|0600, makedev(ma, mi));
		}
		tmain = calloc(1, sizeof(disk_maininfo_t));
		if(!tmain){
			fclose(procpt);
			return DISK_FAILURE;
		}
		strcpy(tmain->info.devname, devname);
		tmain->info.major = ma;
		tmain->info.minor = mi;
		tmain->info.total = sz *DSIZ_KB;
		INIT_LIST_HEAD(&tmain->partlist);
		ret = disk_fill_main_partition_baseinfo(pdisk, tmain);
		if(ret != 0){
			free(tmain);
			fclose(procpt);
			return DISK_FAILURE;
		}
		/*Insert it to list*/
		list_add_tail(&tmain->node, &pdisk->list);
		pdisk->disk_num++;
		*mdisk = tmain;
		break;
	}
	
	fclose(procpt);
	if(tmain == NULL)
		return DISK_FAILURE;
	return DISK_SUCCESS;
}

int disk_chk_partition_mounted(disk_info_t *pdisk, char *spedev)
{
	disk_maininfo_t *node, *_node;
	disk_partinfo_t *pnode, *_pnode;
	
	if(!pdisk){
		DISKCK_DBG("Please INIT disk structure...\n");
		return DISK_FAILURE;
	}

	list_for_each_entry_safe(node, _node, &(pdisk->list), node) {
		if(spedev && strcmp(node->info.devname, spedev)){
			DISKCK_DBG("Special Chk Mount %s-->IGNORE %s\n", spedev, node->info.devname);			
			continue;
		}
		list_for_each_entry_safe(pnode, _pnode, &(node->partlist), node) {
			if(pnode->mounted == 1){
				DISKCK_DBG("%s have disk partition mounted\n", spedev ?spedev:"System");
				return DISK_SUCCESS;
			}
		}	
	}
	
	return DISK_FAILURE;
}


int disk_insert_partition_list(disk_info_t *pdisk, disk_baseinfo_t* pinfo)
{
	int ret, type = 0, len;
	char ptname[DSHORT_STR] = {0}, devname[DSHORT_STR] = {0}, *ptr;
	char devbuf[DSHORT_STR] = {0}, tpbuf[DSHORT_STR] = {0};
	disk_maininfo_t *dmain = NULL;
	disk_partinfo_t *dpart = NULL;
	disk_major_t *dmajor = NULL;
	
	if(!pdisk || !pinfo){
		return DISK_FAILURE;
	}
	
	strcpy(ptname, pinfo->devname);
	DISKCK_DBG("Prase Dev %s\n", ptname);
	
	dmajor = disk_search_major_list(&pdisk->disk_major, pinfo->major);
	if(dmajor == NULL){
		DISKCK_DBG("No Found Major %d\n", pinfo->major);
		return DISK_FAILURE;
	}
	if(strcmp(dmajor->name, "mmc") == 0 && dmajor->major != 8){
		/*We think it is mmcblk*/
		if(pinfo->minor % MMCBLK_MAX_PART == 0){
			type = DISK_MMC_MAIN;			
			strcpy(devname,  ptname);	
		}else{
			type = DISK_MMC_PART;
			len = 0;
			ptr = ptname;
			while(*ptr && len < DSHORT_STR-1 &&
					!isdigit(*ptr)){
				tpbuf[len++] = *ptr++;
			}
			tpbuf[len] = '\0';
			DISKCK_DBG("tpbuf=%s@ptr=%d!\n", tpbuf, atoi(ptr));
		
			sprintf(devname, "%s%d", tpbuf, atoi(ptr));
		}
	}else if(strcmp(dmajor->name, "sd") == 0 && dmajor->major == 8){
		if(!isdigit(ptname[strlen(ptname) -1 ])){
			type = DISK_USB_MAIN;			
			strcpy(devname,  ptname);	
		}else{
			type = DISK_USB_PART;
			strcpy(tpbuf, ptname);
			ptr = tpbuf+strlen(tpbuf)-1;
			while(isdigit(*ptr)){
				*ptr--= '\0';
			}
			strcpy(devname, tpbuf);	
		}
	}else{
		DISKCK_DBG("Unknow Disk Type %s\n", ptname);
		return DISK_FAILURE;
	}
	/*Create Dev Node*/
	memset(devbuf, 0, sizeof(devbuf));
	sprintf(devbuf, "/dev/%s", pinfo->devname);
	if(access(devbuf, F_OK) != 0){
		DISKCK_DBG("Create Node %s", devbuf);
		mknod(devbuf, S_IFBLK|0600, 
			makedev(pinfo->major, pinfo->minor));
	}

	dmain = disk_search_partition_list(pdisk, devname, 0);
	if(type == DISK_USB_MAIN || 
			type == DISK_MMC_MAIN){

		if(dmain){
			DISKCK_DBG("No Need To Insert Main Partition %s\n", devname);
			return DISK_SUCCESS;
		}
		DISKCK_DBG("Insert Main Partition %s\n", devname);
		
		dmain = calloc(1, sizeof(disk_maininfo_t));
		if(!dmain){
			return DISK_FAILURE;
		}
		strcpy(dmain->info.devname, ptname);
		dmain->info.major = pinfo->major;
		dmain->info.minor = pinfo->minor;
		dmain->info.total = pinfo->total;
		dmain->plugcount = plugcount++;
		if(type == DISK_USB_MAIN){
			strcpy(dmain->type, "USB");
		}else{
			strcpy(dmain->type, "SD");
		}		
		INIT_LIST_HEAD(&dmain->partlist);
		ret = disk_fill_main_partition_baseinfo(pdisk, dmain);
		if(ret != 0){
			free(dmain);
			return DISK_FAILURE;
		}
		/*Insert it to list*/
		list_add_tail(&dmain->node, &pdisk->list);
		pdisk->disk_num++;
		return DISK_SUCCESS;
	}
	/*Insert Disk Parition*/
	if(dmain == NULL){
		DISKCK_DBG("Something Error..Need Insert Partition Main %s..\n", devname);
		if(disk_chk_init() == 0){
			ret = disk_insert_main_partition(pdisk, &dmain, devname);
			if(ret != 0){
				DISKCK_DBG("Main Partition %s Insert Error...\n", devname);
				return DISK_FAILURE;
			}
		}else{
			disk_trigger_udevadd(devname, dmajor->major);
			return DISK_FAILURE;
		}
	}
	/*Check it again*/
	if(disk_search_partition_list(pdisk, ptname, 1)){
		DISKCK_DBG("No Need to Insert Partition %s..\n", ptname);
		return 0;
	}
	/*Insert Partition num to list*/
	dpart = calloc(1, sizeof(disk_partinfo_t));
	if(dpart == NULL){
		DISKCK_DBG("Calloc Memeory Failed\n");
		return DISK_FAILURE;
	}
	dpart->uptime = 0;
	memcpy(&dpart->info, pinfo, sizeof(disk_baseinfo_t));
	dmain->partnum++;
	list_add_tail(&dpart->node, &dmain->partlist);

	return DISK_SUCCESS;
}

int disk_mnt1_loop_partition(disk_info_t *pdisk, char *spedev)
{
        FILE *procpt = NULL;
        int ma, mi;
	unsigned long long sz;
        char line[DMID_STR]={0}, ptname[DSHORT_STR];
        int ret;
	disk_baseinfo_t dinfo;
	
		if(!pdisk){
			DISKCK_DBG("Please INIT disk structure...\n");
			return DISK_FAILURE;
		}

        if ((procpt = fopen(DISK_PROC_PARTITION, "r")) == NULL) {
                DISKCK_DBG("Fail to fopen(%s)", DISK_PROC_PARTITION);
                return DISK_FAILURE;
        }

        while (fgets(line, sizeof(line), procpt) != NULL) {
                memset(ptname, 0, sizeof(ptname));
                if (sscanf(line, " %d %d %llu %[^\n ]",
                                &ma, &mi, &sz, ptname) != 4)
                        continue;
		if(sz == 1){
			DISKCK_DBG("IGNORE Extend Partition %s\n", ptname);
			continue;
		}
		if(spedev && strncmp(ptname, spedev, strlen(spedev))){
			DISKCK_DBG("Special Loop %s-->IGNORE %s\n", spedev, ptname);			
			continue;
		}
		if(disk_search_major_list(&pdisk->disk_major, ma) == NULL){
			continue;
		}
		memset(&dinfo, 0, sizeof(disk_baseinfo_t));
		strcpy(dinfo.devname, ptname);
		dinfo.major = ma;
		dinfo.minor = mi;
		dinfo.total = sz * DSIZ_KB;
		ret = disk_insert_partition_list(pdisk, &dinfo);
		if(ret != DISK_SUCCESS){
			DISKCK_DBG("Insert %s Error\n", ptname);
		}
		memset(line, 0, sizeof(line));
        }
	fclose(procpt);
	if(spedev){
		DISKCK_DBG("Special Loop %s Handle Finish\n", spedev);		
	}else{
		DISKCK_DBG("Found %d Storage Device...\n", pdisk->disk_num);
	}
	return DISK_SUCCESS;
}

int disk_mnt2_confirm_partition_info(disk_info_t *pdisk, char *spedev)
{
	disk_maininfo_t *node, *_node;
	disk_partinfo_t *pnode, *_pnode, *newnode;	
	struct blkid_struct_probe pr;
	char devname[DSHORT_STR];
	
	if(!pdisk){
		DISKCK_DBG("Please INIT disk structure...\n");
		return DISK_FAILURE;
	}

	list_for_each_entry_safe(node, _node, &(pdisk->list), node) {
		/*Get disktag*/
		if(spedev && strcmp(node->info.devname, spedev)){
			DISKCK_DBG("Special Confirm %s-->IGNORE %s\n", spedev, node->info.devname);			
			continue;
		}			
		if(node->partnum == 0){
			DISKCK_DBG("[%s]Only have main partition....\n", node->info.devname);			

			newnode = calloc(1, sizeof(disk_partinfo_t));
			if(newnode == NULL){
				DISKCK_DBG("No Memory!!!!\n");
				return DISK_FAILURE;
			}
			memcpy(&(newnode->info), &node->info, sizeof(disk_baseinfo_t));
			memset(&pr, 0, sizeof(pr));
			memset(devname, 0, sizeof(devname));
			sprintf(devname, "/dev/%s", node->info.devname);
			probe_block(devname, &pr);
			if (pr.err || !pr.id) {
				DISKCK_DBG("[%s]Can not recginze....\n", node->info.devname);		
				strcpy(newnode->fstype, "unknown");
			}else{			
				strcpy(newnode->fstype, pr.id->name);
				strcpy(newnode->label, pr.label);
			}

			/*add node*/
			list_add_tail(&newnode->node, &node->partlist);
			if(spedev){				
				DISKCK_DBG("Special Confirm %s Handle Finish[Only have main]\n", spedev);			
				return DISK_SUCCESS;
			}
			continue;
		}
		list_for_each_entry_safe(pnode, _pnode, &(node->partlist), node) {			
			memset(&pr, 0, sizeof(pr));
			memset(devname, 0, sizeof(devname));
			sprintf(devname, "/dev/%s", pnode->info.devname);
			probe_block(devname, &pr);
			if (pr.err || !pr.id) {
				DISKCK_DBG("[%s]Can not recginze....\n", pnode->info.devname);		
				strcpy(pnode->fstype, "unknown");
			}else{
				strcpy(pnode->fstype, pr.id->name);
				strcpy(pnode->label, pr.label);
			}
		}
		//if confirm special disk, now we handle it finish, so return
		if(spedev){			
			DISKCK_DBG("Special Confirm %s Handle Finish\n", spedev);			
			return DISK_SUCCESS;
		}		
	}
	if(spedev){ 		
		DISKCK_DBG("Special Confirm No Found %s\n", spedev);			
		return 2;
	}
	return DISK_SUCCESS;
}

int disk_mnt3_automount_partition(disk_info_t *pdisk, char *spedev)
{
	disk_maininfo_t *node, *_node;
	disk_partinfo_t *pnode, *_pnode;
	char devbuf[DSHORT_STR], parameter[DSHORT_STR] = {0}, fs[DSHORT_STR] = {0};
	int ret;
	
	if(!pdisk){
		DISKCK_DBG("Please INIT disk structure...\n");
		return DISK_FAILURE;
	}

	list_for_each_entry_safe(node, _node, &(pdisk->list), node) {
		if(spedev && strcmp(node->info.devname, spedev)){
			DISKCK_DBG("Special Mount %s-->IGNORE %s\n", spedev, node->info.devname);			
			continue;
		}
		list_for_each_entry_safe(pnode, _pnode, &(node->partlist), node) {
			memset(devbuf, 0, sizeof(devbuf));
			sprintf(devbuf, "/dev/%s", pnode->info.devname);
			if(node->isgpt == 1 &&
				probe_efi_partition(node->info.devname, pnode->info.devname) == 1){
				DISKCK_DBG("EFI Partition Found %s-->IGNORE\n", pnode->info.devname);		
				continue;
			}
			if(find_mount_point(devbuf)){
				DISKCK_DBG("%s is already mounted\n", devbuf);				
				get_partition_capacity_rw(pnode);
				pnode->mounted = 1;
				continue;
			}
			if(generate_mount_point(node->disktag, node->display, pnode) != DISK_SUCCESS){
				DISKCK_DBG("Generate Mnt Point Failed\n");
				pnode->mounted = 0;
				continue;
			}
			/*Get commerical filesystem driver*/
			memset(fs, 0, sizeof(fs));
			if(check_commerical_driver(pnode->fstype, fs) != DISK_SUCCESS){
				strcpy(fs, pnode->fstype);
			}
			memset(parameter, 0, sizeof(parameter));
			if(generate_mount_parameter(&(pdisk->mnt_parameter), fs, parameter) != DISK_SUCCESS){
				DISKCK_DBG("Get Mount Parameter Error:%s\n", pnode->info.devname);
			}
			ret = mount(devbuf, pnode->mntpoint , fs, 0,strlen(parameter)?parameter:"");
			if(ret){
				DISKCK_DBG("Mount Error: dev=%s mntpoint=%s fstype=%s option=%s![%s]\n", 
						devbuf, pnode->mntpoint, fs, parameter, strerror(errno));
				pnode->mounted = 0;
				rmdir(pnode->mntpoint);
				continue;
			}else{
				DISKCK_DBG("Mount %s Successful, Update Partition Capatibty\n",devbuf);
				update_partition_capacity_rw(pnode);
			}
			/*Notify to upnpd*/
			disk_aciton_notify_upnp(pnode, DISK_UDEV_ADD, node->info.devname);	
			
			pnode->mounted = 1;
			/*Compiable with old version excute some command*/
			disk_excute_factory_script(pnode->mntpoint);
		}
		node->status = DISK_MOUNTED;
		if(spedev){			
			/*No Disk Mount so we need to display this situation*/
			DISKCK_DBG("Special Mount %s Handle Finish\n", spedev);			
			return DISK_SUCCESS;
		}		
	}
	
	return 2;
}

int disk_mount_process(disk_info_t *pdisk, char *spedev)
{
	int ret = DISK_FAILURE;
	
	if(!pdisk){
		DISKCK_DBG("Please INIT disk structure...\n");
		return DISK_FAILURE;
	}

	ret = disk_mnt1_loop_partition(pdisk, spedev);
	if(ret == DISK_FAILURE){
		DISKCK_DBG("Loop Error...\n");
		return DISK_FAILURE;
	}
	ret = disk_mnt2_confirm_partition_info(pdisk, spedev);
	if(ret  != DISK_SUCCESS){
		return ret;
	}
	ret = disk_mnt3_automount_partition(pdisk, spedev);
	if(ret != DISK_SUCCESS){
		return ret;
	}

	return DISK_SUCCESS;
}


void disk_print_partition_info(disk_info_t *pdisk)
{
	disk_maininfo_t *node, *_node;
	disk_partinfo_t *pnode, *_pnode;
	
	if(!pdisk){
		DISKCK_DBG("Please INIT disk structure...\n");
		return ;
	}
	DISKCK_DBG("\n\n\tDISK Manager Partition\n\n");
	list_for_each_entry_safe(node, _node, &(pdisk->list), node) {
		printf("DiskName:%s\t DiskVendor=%s\nDiskSerical=%s\nDiskPartnum=%d\tDiskSize=%llu\nDiskSpeed=%dMB\n",
			node->info.devname, node->vendor,
			node->serical, node->partnum, node->info.total, node->speed);		
		printf("\nDiskPartition:\n");
		list_for_each_entry_safe(pnode, _pnode, &(node->partlist), node) {
			printf("PartitionName:%s\t  Mounted=%d PartitionType:%s\tPartitionLabel=%s\tTotal=%llu\tUsed=%llu\n", 
					pnode->info.devname, pnode->mounted, pnode->fstype, pnode->label, pnode->info.total, pnode->info.used);
		}
		printf("\n\n");
	}
	printf("\n");
}

int udev_action_func_add(disk_info_t *pdisk, udev_action *action)
{
	if(action == NULL){
		return DISK_FAILURE;
	}
	return disk_mount_process(pdisk, action->dev);
}

int udev_action_func_wakeup(disk_info_t *pdisk, udev_action *action)
{
	int ret;
	disk_maininfo_t *node, *_node;
	disk_partinfo_t *pnode, *_pnode;
	char devbuf[DSHORT_STR], fs[DSHORT_STR] = {0}, parameter[DSHORT_STR] = {0};

	if(action->action != DISK_WAKEUP){
		return 2;
	}
	list_for_each_entry_safe(node, _node, &(pdisk->list), node) {
		if(strcmp(node->info.devname, action->dev) == 0&&
			(action->major == 0xFFFF|| node->info.major == action->major)){
			if(node->status != DISK_SFREMOVE){
				DISKCK_DBG("Disk %s Status is Not  DISK_SFREMOVE\n", node->info.devname);
				return 2;
			}
			DISKCK_DBG("WakeUP %s Begin...\n", node->info.devname);
			list_for_each_entry_safe(pnode, _pnode, &(node->partlist), node) {
				memset(devbuf, 0, sizeof(devbuf));
				sprintf(devbuf, "/dev/%s", pnode->info.devname);
				if(find_mount_point(devbuf)){
					DISKCK_DBG("%s is already mounted\n", devbuf);				
					get_partition_capacity_rw(pnode);
					node->status = DISK_MOUNTED;
					return 2;
				}
				if(generate_mount_point(node->disktag, node->display,pnode) != DISK_SUCCESS){
					DISKCK_DBG("Generate Mnt Point Failed\n");
					pnode->mounted = 0;
					return DISK_FAILURE;
				}
				/*Get commerical filesystem driver*/
				memset(fs, 0, sizeof(fs));
				if(check_commerical_driver(pnode->fstype, fs) != DISK_SUCCESS){
					strcpy(fs, pnode->fstype);
				}
				memset(parameter, 0, sizeof(parameter));
				if(generate_mount_parameter(&(pdisk->mnt_parameter), fs, parameter) != DISK_SUCCESS){
					DISKCK_DBG("Get Mount Parameter Error:%s\n", pnode->info.devname);
				}
				ret = mount(devbuf, pnode->mntpoint , fs, 0,strlen(parameter)?parameter:"");
				if(ret){
					DISKCK_DBG("Mount Error: dev=%s mntpoint=%s fstype=%s option=%s![%s]\n", 
							devbuf, pnode->mntpoint, fs, parameter, strerror(errno));
					pnode->mounted = 0;
					continue;
				}else{
					DISKCK_DBG("Mount %s Successful, Update Partition Capatibty\n",devbuf);
					update_partition_capacity_rw(pnode);
				}
				pnode->mounted = 1;

			}
			/*Update Main Disk status*/
			node->status = DISK_MOUNTED;

			return DISK_SUCCESS;
		}
	}
	DISKCK_DBG("No Found Safe Remove Disk: %s\n",action->dev);

	return 2;
}

int udev_action_func_remove(disk_info_t *pdisk, udev_action *action)
{
	int ret;
	disk_maininfo_t *node, *_node;
	disk_partinfo_t *pnode, *_pnode;

	list_for_each_entry_safe(node, _node, &(pdisk->list), node) {
		if(strcmp(node->info.devname, action->dev) == 0&&
			(action->major== 0xFFFF)){
			if(node->status == action->action){
				DISKCK_DBG("Disk %s Have %s\n", node->info.devname, 
					action->action==DISK_UDEV_REMOVE?"Removed":"Safe Removed");
				return DISK_SUCCESS;
			}
			DISKCK_DBG("%s %s Begin...\n", action->action==DISK_UDEV_REMOVE?"Remove":"Safe Remove"
				,node->info.devname);
			list_for_each_entry_safe(pnode, _pnode, &(node->partlist), node) {
				if(pnode->mounted == 1){
					DISKCK_DBG("Umount %s begin[Mounted]->%ld\n", pnode->info.devname, time(NULL));
					ret = umount2(pnode->mntpoint, MNT_DETACH);
					if(ret){
						DISKCK_DBG("umount of %s failed (%d) - %s\n",
							pnode->mntpoint, ret, strerror(errno));
					}
					DISKCK_DBG("Umount %s finish[Mounted]->%ld\n", pnode->info.devname, time(NULL));
				}
				/*Update status*/
				if(action->action == DISK_SFREMOVE){
					pnode->mounted = 0;					
					DISKCK_DBG("Safe Remove %s Successful\n", pnode->info.devname);
				}else if(action->action == DISK_UDEV_REMOVE){
					list_del(&pnode->node);
					rmdir(pnode->mntpoint);
					/*Notify to upnpd*/
					disk_aciton_notify_upnp(pnode, action->action, action->dev);
					DISKCK_DBG("Remove %s Successful\n", pnode->info.devname);
					free(pnode);
				}else{
					DISKCK_DBG("Error Action %d\n", action->action);
				}
			}
			/*Update Main Disk status*/
			if(action->action == DISK_SFREMOVE){
				node->status = DISK_SFREMOVE;
			}else if(action->action == DISK_UDEV_REMOVE){
				list_del(&node->node);
				DISKCK_DBG("Last Remove %s Successful\n", node->info.devname);
				free(node);
			}
			return DISK_SUCCESS;
		}
	}

	return 2;
}

int udev_action_func(disk_info_t *pdisk, udev_action *action)
{
	int ret = DISK_FAILURE;
	
	if(pdisk == NULL || action == NULL){
		return DISK_FAILURE;
	}
	
	if(action->action == DISK_UDEV_ADD){
		ret =  udev_action_func_add(pdisk, action);
	}else if(action->action == DISK_WAKEUP){
		ret = udev_action_func_wakeup(pdisk, action);
	}else{
		ret =  udev_action_func_remove(pdisk, action);
	}
	
	return ret;
}

int udev_action_func_all(disk_info_t *pdisk, int *action)
{
	int ret = DISK_FAILURE;
	disk_maininfo_t *node, *_node;
	disk_partinfo_t *pnode, *_pnode;
	
	if(pdisk == NULL || action == NULL){
		return DISK_FAILURE;
	}
	DISKCK_DBG("Handle ALL: %s\n", *action==DISK_UDEV_ADD?"Add":"Remove");
	if(*action == DISK_UDEV_ADD){
		disk_mount_process(pdisk, NULL);
		return disk_mount_process(pdisk, NULL);
	}
	sync();
	/*Remove Action*/
	list_for_each_entry_safe(node, _node, &(pdisk->list), node) {
		DISKCK_DBG("Remove %s Begin...\n", node->info.devname);
		list_for_each_entry_safe(pnode, _pnode, &(node->partlist), node) {
			if(pnode->mounted == 0){
				DISKCK_DBG("No Need To Handle %s[Not Mounted]\n", pnode->info.devname);
				continue;
			}
			sync();
			if((ret = umount(pnode->mntpoint))){
				DISKCK_DBG("umount of %s failed (%d) - %s-->use umount2\n",
					pnode->mntpoint, ret, strerror(errno));
				ret = umount2(pnode->mntpoint, MNT_DETACH);
				if(ret){
					DISKCK_DBG("umount2 of %s failed (%d) - %s\n",
							pnode->mntpoint, ret, strerror(errno));
					continue;
				}
			}else{
				DISKCK_DBG("Umount %s successful\n",
					pnode->mntpoint);
			}

			list_del(&pnode->node);
			rmdir(pnode->mntpoint);
			/*Notify to upnpd*/
			disk_aciton_notify_upnp(pnode, *action, node->info.devname);
			DISKCK_DBG("Remove %s Successful\n", pnode->info.devname);
			free(pnode);
		}	
		/*Update Main Disk status*/
		list_del(&node->node);
		sg_sleep_disk(node->info.devname, 0);
		DISKCK_DBG("Last Remove %s Successful\n", node->info.devname);
		free(node);
	}
	if(*action == DISK_UDEV_POWEROFF){
		DISKCK_DBG("Disk Manage Let System Disconnect USB Device!!!!\n");
		system("echo 0 > /sys/bus/usb/devices/usb1/authorized");
		system("echo 0 > /sys/bus/usb/devices/usb2/authorized");
		DISKCK_DBG("Disk Manage Receive PowerOFF-->QUIT\n");
		exit(1);
	}
	return DISK_SUCCESS;
}

int protocol_get_all_disk_info(disk_info_t *pdisk, void *data, int len, void **rbuf, int *rlen)
{
	int reslen = 0, partmem = 0;
	void *response = NULL, *prealloc = NULL;
	disk_proto_t *diskinfo, *partdisk = NULL;
	disk_maininfo_t *node, *_node;
	disk_partinfo_t *pnode, *_pnode;
	unsigned char same_disk = (time(NULL)&0xFF);
	unsigned long long cused = 0;
	char devname[DSHORT_STR] = {0};
	
	if(pdisk == NULL){
		return DISK_FAILURE;
	}
	if(data && len){		
		memcpy(devname, data, len);
		DISKCK_DBG("Protocol Get Info--->Get Special Disk %s\n", devname);
	}
	list_for_each_entry_safe(node, _node, &(pdisk->list), node){
		if(strlen(devname) && strcmp(node->info.devname, devname)){			
			DISKCK_DBG("Protocol Filter---%s-->%s\n", devname, node->info.devname);
			continue;
		}
		cused = 0;
		response = realloc(response, sizeof(disk_proto_t)+reslen);
		if(response == NULL){
			DISKCK_DBG("Memory Realloc Failed\n");
			return DISK_FAILURE;
		}		
		diskinfo = (disk_proto_t*)(response+reslen);
		/*Increase memory*/
		reslen += sizeof(disk_proto_t);
		DISKCK_DBG("Protocol Get [%s] Info--->Address[0x%p] memory size[%d]\n", 
			node->info.devname, response, reslen);
		
		/*Fill in main partition*/
		memset(diskinfo, 0, sizeof(disk_proto_t));
		strcpy(diskinfo->devname, node->info.devname);
		diskinfo->total = node->info.total;
		diskinfo->ptype = (same_disk << 8) |  DISK_MAIN;
		strcpy(diskinfo->partition.main_info.vendor, node->vendor);
		strcpy(diskinfo->partition.main_info.serical, node->serical);
		strcpy(diskinfo->partition.main_info.type, node->type);
		strcpy(diskinfo->partition.main_info.disktag, node->disktag);
		diskinfo->partition.main_info.status = node->status;
		diskinfo->partition.main_info.partnum = node->partnum;

		prealloc = NULL;
		partmem = 0;
		list_for_each_entry_safe(pnode, _pnode, &(node->partlist), node){
			/*Update storage information*/
			if(pnode->mounted == 1){
				update_partition_capacity_rw(pnode);
			}
			prealloc = realloc(prealloc, sizeof(disk_proto_t)+partmem);
			if(prealloc == NULL){
				DISKCK_DBG("Memory Realloc Failed\n");
				return DISK_FAILURE;
			}
			partdisk = (disk_proto_t*)(prealloc+partmem);
			/*Increase memory*/
			partmem += sizeof(disk_proto_t);			
			memset(partdisk, 0, sizeof(disk_proto_t));
			DISKCK_DBG("Protocol Get Part [%s] Info--->Address[0x%p] memory size[%d]\n", 
					pnode->info.devname, prealloc, partmem);
			/*Fill in main partition*/
			strcpy(partdisk->devname, pnode->info.devname);
			partdisk->total = pnode->info.total;
			partdisk->ptype = (same_disk << 8) |  DISK_PART;			
			partdisk->partition.part_info.mounted =  pnode->mounted;
			strcpy(partdisk->partition.part_info.fstype, pnode->fstype);
			strcpy(partdisk->partition.part_info.label, pnode->label);			
			strcpy(partdisk->partition.part_info.mntpoint, pnode->mntpoint);
			partdisk->partition.part_info.enablewrite =  pnode->enablewrite;
			
			if(partdisk->partition.part_info.mounted == 0){				
				partdisk->used = pnode->info.total;
			}else{
				partdisk->used = pnode->info.used;
			}
			cused += partdisk->used;

			printf("Part INFO:\n\tName:%s\n\tDiskFlag:%d\n\tTotal:%llu\n\tUsed:%llu\n\tFstype:%s\n\tLabel:%s\n"
					"\tMountPoint:%s\n\tMounted:%d\n\tEnablewrite:%d\n\tcused:%llu\n", partdisk->devname, partdisk->ptype>>8, partdisk->total,
					partdisk->used, partdisk->partition.part_info.fstype, partdisk->partition.part_info.label,
					partdisk->partition.part_info.mntpoint, partdisk->partition.part_info.mounted, 
					partdisk->partition.part_info.enablewrite, cused);			
		}
		/*update disk used info*/
		diskinfo->used = node->info.used = cused;
		printf("DiskMain INFO:\n\tName:%s\n\tDiskFlag:%u\n\tTotal:%llu\n\tUsed:%llu\n\tVendor:%s\n\tSerical:%s\n"
				"\tType:%s\n\tDiskTag:%s\n\tStatus:%d\n\tPartnum:%d\n", diskinfo->devname, diskinfo->ptype>>8, diskinfo->total,
				diskinfo->used, diskinfo->partition.main_info.vendor, diskinfo->partition.main_info.serical,
				diskinfo->partition.main_info.type, diskinfo->partition.main_info.disktag, diskinfo->partition.main_info.status,
				diskinfo->partition.main_info.partnum);	
		
		response = realloc(response, partmem+reslen);
		if(response == NULL){
				DISKCK_DBG("Memory Realloc Failed\n");
				return DISK_FAILURE;
		}
		memcpy(response+reslen, prealloc, partmem);
		reslen += partmem;
		if(prealloc){
			free(prealloc);
			prealloc = NULL;
			partmem = 0;
		}	
		if(strlen(devname)){			
			DISKCK_DBG("Protocol Get Special Disk %s Finish\n", devname);
			break;
		}
		same_disk = ((same_disk+1)%0xFF);		
		DISKCK_DBG("DiskFlag change to  %u! diskinfo->used=%llu\n", same_disk, diskinfo->used);
	}
	*rbuf = response;
	*rlen = reslen;
	DISKCK_DBG("Protocol Get Info Finish--->Address[0x%p] memory size[%d]\n", response, *rlen);

	return DISK_SUCCESS;
}

int protocol_disk_dirlist(disk_info_t *pdisk, void *data, int len, void **rbuf, int *rlen)
{
	int reslen = 0;
	void *response = NULL;
	disk_dirlist_t  *partdisk = NULL;
	disk_maininfo_t *node, *_node;
	disk_partinfo_t *pnode, *_pnode;
	
	if(pdisk == NULL){
		return DISK_FAILURE;
	}

	list_for_each_entry_safe(node, _node, &(pdisk->list), node){
		if(node->status != DISK_MOUNTED){
			DISKCK_DBG("Filter Not mounted disk---%s\n", node->info.devname);
			continue;
		}
		list_for_each_entry_safe(pnode, _pnode, &(node->partlist), node){
			/*Update storage information*/
			if(pnode->mounted != 1){
				DISKCK_DBG("Filter Not mounted partition---%s\n", pnode->info.devname);
				continue;
			}
			response = realloc(response, sizeof(disk_dirlist_t)+reslen);
			if(response == NULL){
				DISKCK_DBG("Memory Realloc Failed\n");
				return DISK_FAILURE;
			}
			partdisk = (disk_dirlist_t*)(response+reslen);
			/*Increase memory*/
			reslen += sizeof(disk_dirlist_t);			
			memset(partdisk, 0, sizeof(disk_dirlist_t));
			DISKCK_DBG("Protocol Get Part [%s] Info--->Address[0x%p] memory size[%d]\n", 
					pnode->info.devname, response, reslen);
			/*Fill in main partition*/
			strcpy(partdisk->devname, node->info.devname);			
			strcpy(partdisk->partname, pnode->info.devname);
			partdisk->mounted =  pnode->mounted;
			strcpy(partdisk->type, node->type);		
			strcpy(partdisk->disktag, node->disktag);			
			strcpy(partdisk->fstype, pnode->fstype);
			strcpy(partdisk->label, pnode->label);			
			strcpy(partdisk->mntpoint, pnode->mntpoint);
			partdisk->enablewrite =  pnode->enablewrite;
			if(strlen(pnode->display)){
				strcpy(partdisk->displayname,  pnode->display);
			}else if(disk_dirlist_display_name(mnt_preffix, pnode->mntpoint, partdisk->displayname) == DISK_FAILURE){
				strcpy(partdisk->displayname,  pnode->info.devname);
			}
			
			printf("DirListPart INFO:\n\tName:%s\n\tPartname:%s\n\tType:%s\n\tDiskTag:%s\n\tFstype:%s\n\tLabel:%s\n"
					"\tMountPoint:%s\n\tMounted:%d\n\tEnablewrite:%d\n\tDiskplay:%s\n", partdisk->devname, partdisk->partname, 
					partdisk->type, partdisk->disktag, partdisk->fstype, partdisk->label,
					partdisk->mntpoint, partdisk->mounted, 
					partdisk->enablewrite, partdisk->displayname);
		}
	}
	*rbuf = response;
	*rlen = reslen;
	DISKCK_DBG("Protocol Get Dirlist Finish--->Address[0x%p] memory size[%d]\n", response, *rlen);

	return DISK_SUCCESS;
}

int protocol_disk_disklist(disk_info_t *pdisk, void *data, int len, void **rbuf, int *rlen)
{
	int reslen = 0;
	void *response = NULL;
	disk_disklist_t  *disklist = NULL;
	disk_maininfo_t *node, *_node;
	
	if(pdisk == NULL){
		return DISK_FAILURE;
	}

	list_for_each_entry_safe(node, _node, &(pdisk->list), node){
		response = realloc(response, sizeof(disk_disklist_t)+reslen);
		if(response == NULL){
			DISKCK_DBG("Memory Realloc Failed\n");
			return DISK_FAILURE;
		}
		disklist = (disk_disklist_t*)(response+reslen);
		/*Increase memory*/
		reslen += sizeof(disk_disklist_t);			
		memset(disklist, 0, sizeof(disk_disklist_t));
		DISKCK_DBG("Protocol Get Disk List [%s] Info--->Address[0x%p] memory size[%d]\n", 
				node->info.devname, response, reslen);
		/*Fill in main partition*/
		strcpy(disklist->devname, node->info.devname);
		disklist->total = node->info.total;
		strcpy(disklist->vendor, node->vendor);
		strcpy(disklist->serical, node->serical);
		strcpy(disklist->type, node->type);
		if(strcasecmp(node->display, "None") != 0){
			strcpy(disklist->disktag, node->display);
		}else{
			strcpy(disklist->disktag, node->disktag);
		}
		disklist->status = node->status;
		disklist->partnum = node->partnum;
		
		printf("DiskMain INFO:\n\tName:%s\n\tTotal:%llu\n\tVendor:%s\n\tSerical:%s\n"
				"\tType:%s\n\tDiskTag:%s\n\tStatus:%d\n\tPartnum:%d\n", disklist->devname, disklist->total,
				disklist->vendor, disklist->serical,
				disklist->type, disklist->disktag, disklist->status, disklist->partnum);			
	}

	*rbuf = response;
	*rlen = reslen;
	DISKCK_DBG("Protocol Get Disklist Finish--->Address[0x%p] memory size[%d]\n", response, *rlen);

	return DISK_SUCCESS;
}

int disk_chk_init(void)
{
	return i4disk==NULL?0:1;
}
void disk_destory(disk_info_t *pdisk)
{
	int ret;
	disk_maininfo_t *node, *_node;
	disk_partinfo_t *pnode, *_pnode;
	disk_major_t *mnode, *_mpnode;
	disk_mnt_para_t *pamter, *_pamter;
	disktag_t *ptag, *_ptag;

	if(pdisk == NULL){
		return;
	}		
	list_for_each_entry_safe(node, _node, &(pdisk->list), node) {
		DISKCK_DBG("Destory %s Begin...\n", node->info.devname);
		list_for_each_entry_safe(pnode, _pnode, &(node->partlist), node) {
			if(pnode->mounted == 0){
				ret = umount2(pnode->mntpoint, MNT_DETACH);
				if(ret){
					DISKCK_DBG("umount of %s failed (%d) - %s\n",
							pnode->mntpoint, ret, strerror(errno));
				}				
				rmdir(pnode->mntpoint);
			}

			list_del(&pnode->node);
			/*Notify to upnpd*/
			disk_aciton_notify_upnp(pnode, DISK_UDEV_REMOVE, node->info.devname);
			DISKCK_DBG("Destory %s Successful\n", pnode->info.devname);
			free(pnode);
		}	
		/*Update Main Disk status*/
		list_del(&node->node);
		sg_sleep_disk(node->info.devname, 0);
		DISKCK_DBG("Last Destory %s Successful\n", node->info.devname);
		free(node);
	}
	/*Destory Major list*/
	list_for_each_entry_safe(mnode, _mpnode, &(pdisk->disk_major), node) {
		if(mnode){
			free(mnode);
		}
	}
	/*Dstory mount parameter*/
	list_for_each_entry_safe(pamter, _pamter, &(pdisk->mnt_parameter), node) {
		if(pamter){
			free(pamter);
		}
	}
	/*Dstory disktag list*/
	list_for_each_entry_safe(ptag, _ptag, &(pdisk->mnt_disktag), node) {
		if(ptag){
			free(ptag);
		}
	}
	/*Free struct memory*/
	free(pdisk);
	
}

void disk_preinit(void)
{
	FILE *fp;
	char line[DMID_STR] = {0}, tmp[DMID_STR] = {0};
	int slen;

	/*umount unused mount point*/

	/*find mount preffix*/
	if(access(DISK_MNT_DIR, F_OK) ||
			(fp = fopen(DISK_MNT_DIR, "r")) == NULL){
		strcpy(mnt_preffix, DISK_MNT_PREFIX);
		DISKCK_DBG("Use Default Mount preffix %s\n", mnt_preffix);
		return ;
	}
	while (fgets(line, sizeof(line), fp) != NULL) {
		memset(tmp, 0, sizeof(tmp));
		if (sscanf(line, "preffix=%[^\n ]",
						tmp) != 1){
			memset(line, 0, sizeof(line));
			continue;
		}		
		DISKCK_DBG("Found Mount preffix %s\n", tmp);
		slen = strlen(tmp)-1;
		while(tmp[slen] == '/'){
			tmp[slen] = '\0';
			slen--;
		}
		if(slen == 0){
			DISKCK_DBG("Use root as mount preffix, we change it to default\n");
			strcpy(tmp, DISK_MNT_PREFIX);
		}
		DISKCK_DBG("Found Mount preffix %s[Fileter]\n", tmp);
		strcpy(mnt_preffix, tmp);
		fclose(fp);
		return ;
	}		

	fclose(fp);
	
	strcpy(mnt_preffix, DISK_MNT_PREFIX);
	DISKCK_DBG("Finnal Use Default Mount preffix %s\n", mnt_preffix);
}

int disk_init(void)
{
	int ret;
	disk_info_t *i4tmp = NULL;

	/*Preinit disk*/
	disk_preinit();

	i4tmp = calloc(1, sizeof(disk_info_t));
	if(i4tmp == NULL){
		DISKCK_DBG("Memory Calloc Failed\n");
		return DISK_FAILURE;
	}
	
	INIT_LIST_HEAD(&i4tmp->list);	
	INIT_LIST_HEAD(&i4tmp->disk_major);	
	INIT_LIST_HEAD(&i4tmp->mnt_parameter);	
	INIT_LIST_HEAD(&i4tmp->mnt_disktag);

	/*disk hub info*/
	disk_gethub_info(&i4tmp->hubinfo);
	/*Prase device major list, find out block major list*/
	ret = disk_major_list_parse(i4tmp);
	if(ret == DISK_FAILURE){
		DISKCK_DBG("Init Major List Error..\n");
		goto err_destory;
	}
	/*Prase mnt parameter*/
	ret = disk_mnt_parameter_parse(i4tmp);
	if(ret == DISK_FAILURE){
		goto err_destory;
	}
	/*Prase mnt disktag*/
	ret = disk_mnt_disktag_parse(i4tmp);
	if(ret == DISK_FAILURE){
		goto err_destory;
	}
	ret = disk_mount_process(i4tmp, NULL);
	if(ret == DISK_FAILURE){	
		goto err_destory;
	}

	disk_print_partition_info(i4tmp);
	i4disk = i4tmp;
	
	return DISK_SUCCESS;
	
err_destory:
	
	disk_destory(i4tmp);
	return DISK_FAILURE;
}

int disk_aciton_func(udev_action *action)
{
	return udev_action_func(i4disk, action);
}

int disk_getdisk_lun(void *buff, int size, int *used)
{
	disk_maininfo_t *node, *_node;
	int disknum = 0;
	struct operation_diskLun *diskPtr = (struct operation_diskLun*)(buff);
	int totalSize = size, nodeSize = sizeof(struct diskPlugNode);


	if(!buff || !used || size < sizeof(struct operation_diskLun)){
		return DISK_FAILURE;
	}

	totalSize -= sizeof(struct operation_diskLun);
	struct diskPlugNode *diskNode = diskPtr->disks;
	memset(buff, 0, size);
	list_for_each_entry_safe(node, _node, &(i4disk->list), node){
		if(node->status != DISK_MOUNTED){
			DISKCK_DBG("Filter Not mounted disk---%s\n", node->info.devname);
			continue;
		}
		if(totalSize < nodeSize){
			DISKCK_DBG("Buffer is Full\n");
			*used = size-totalSize;
			return DISK_SUCCESS;
		}
		diskPtr->disknum++;
		strncpy(diskNode->dev, node->info.devname, sizeof(diskNode->dev));
		diskNode->seqnum = node->plugcount;
		DISKCK_DBG("Found Disk:%s count=%d\n", diskNode->dev, diskNode->seqnum);
		
	}
	*used = size-totalSize;

	return DISK_SUCCESS;
}	


int disk_getdisk_info(void *buff, int size, int *used)
{
	disk_maininfo_t *node, *_node;	
	disk_partinfo_t *pnode, *_pnode;
	int disknum = 0;
	struct operation_diskInfo *diskPtr = (struct operation_diskInfo*)(buff);
	int totalSize = size, nodeSize = sizeof(struct diskInfoNode);

	if(!buff || !used || size < sizeof(struct operation_diskInfo)){
		return DISK_FAILURE;
	}
	totalSize -= sizeof(struct operation_diskInfo);
	struct diskInfoNode *diskNode = diskPtr->diskInfos;
	memset(buff, 0, size);

	list_for_each_entry_safe(node, _node, &(i4disk->list), node){
		if(node->status != DISK_MOUNTED){
			DISKCK_DBG("Filter Not mounted disk---%s\n", node->info.devname);
			continue;
		}
		if(totalSize < nodeSize){
			DISKCK_DBG("Buffer is Full\n");
			*used = size-totalSize;
			return DISK_SUCCESS;
		}
		strncpy(diskNode->dev, node->info.devname, sizeof(diskNode->dev));
		diskNode->totalSize = node->info.total;
		diskNode->usedSize = node->info.used;
		if(strstr(node->type, "SD")){
			diskNode->type = 1;
			diskNode->enablePlug = 1;
		}else{
			diskNode->type = 0;
			diskNode->enablePlug = 0;
		}
		diskNode->partNum = node->partnum;

		int i = 0;
		list_for_each_entry_safe(pnode, _pnode, &(node->partlist), node){
			/*Update storage information*/
			if(pnode->mounted != 1){
				DISKCK_DBG("Filter Not mounted partition---%s\n", pnode->info.devname);
				continue;
			}
			if(i == MAX_PARTITIONS-1){
				DISKCK_DBG("Partition BUffer overflow\n");
				*used = size-totalSize;
				return DISK_SUCCESS;
			}
			strncpy(diskNode->partitions[i].dev, pnode->info.devname, 16);
			diskNode->partitions[i].totalSize = pnode->info.total;			
			diskNode->partitions[i].usedSize= pnode->info.used;
			strncpy(diskNode->partitions[i].mountDir, pnode->mntpoint, 63);
			if(strcmp(pnode->fstype, "vfat") == 0||
					strcmp(pnode->fstype, "fat32") == 0){
				diskNode->partitions[i].fstype = 1;
			}else if(strcmp(pnode->fstype, "exfat") == 0){
				diskNode->partitions[i].fstype = 2;
			}else if(strcmp(pnode->fstype, "ntfs") == 0){
				diskNode->partitions[i].fstype = 3;
			}else if(strncmp(pnode->fstype, "hfs", 3) == 0){
				diskNode->partitions[i].fstype = 4;
			}else{
				DISKCK_DBG("Unknown filesystem (%s)", pnode->fstype);
				diskNode->partitions[i].fstype = 0xFF;
			}
			DISKCK_DBG("Found Partition:%s fstype=%d\n",
					diskNode->partitions[i].dev, diskNode->partitions[i].fstype);
			i++;
		}
		
		diskPtr->disknum++;
		totalSize -= sizeof(struct diskInfoNode);
		DISKCK_DBG("DiskInfo:%s totalSize=%lld usedSize=%lld [Buffer:%p NextBuffer:%p Totalsize:%d]\n",
				diskNode->dev, diskNode->totalSize, diskNode->usedSize, diskNode, diskNode+1, totalSize);
		
		/*Next Pointer*/
		diskNode++;		
	}
	
	*used = size-totalSize;

	return DISK_SUCCESS;
}
