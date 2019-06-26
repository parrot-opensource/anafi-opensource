/*
 *
 * Copyright (C) 2012-2016, Ambarella, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rpmsg.h>
#include <linux/err.h>
#include <linux/remoteproc.h>
#include <linux/syscalls.h>
#include <linux/slab.h>
#include <linux/dirent.h>
#include <linux/statfs.h>
#include <asm/uaccess.h>
#include <plat/ambalink_cfg.h>
#include "aipc_priv.h"

#define AIPC_RFS_CMD_OPEN	0
#define AIPC_RFS_CMD_CLOSE	1
#define AIPC_RFS_CMD_READ	2
#define AIPC_RFS_CMD_WRITE	3
#define AIPC_RFS_CMD_TELL	4
#define AIPC_RFS_CMD_SEEK	5
#define AIPC_RFS_CMD_REMOVE	6
#define AIPC_RFS_CMD_MKDIR	7
#define AIPC_RFS_CMD_RMDIR	8
#define AIPC_RFS_CMD_MOVE	9
#define AIPC_RFS_CMD_CHMOD	10
#define AIPC_RFS_CMD_CHDMOD	11
#define AIPC_RFS_CMD_MOUNT	12
#define AIPC_RFS_CMD_UMOUNT	13
#define AIPC_RFS_CMD_SYNC	14
#define AIPC_RFS_CMD_FSYNC	15
#define AIPC_RFS_CMD_STAT	16
#define AIPC_RFS_CMD_GETDEV	17
#define AIPC_RFS_CMD_FEOF	18
#define AIPC_RFS_CMD_OPENDIR	19
#define AIPC_RFS_CMD_READDIR	20
#define AIPC_RFS_CMD_CLOSEDIR	21
#define AIPC_RFS_CMD_CHDIR	22

#define AIPC_RFS_SEEK_SET        0
#define AIPC_RFS_SEEK_CUR        1
#define AIPC_RFS_SEEK_END        2

#define AIPC_RFS_MODE_RD         1
#define AIPC_RFS_MODE_WR         2

#define AIPC_RFS_REPLY_OK        0
#define AIPC_RFS_REPLY_NODEV     1

struct aipc_rfs_msg {
	unsigned char   msg_type;
	unsigned char   xprt;
	unsigned short  msg_len;
	u32		reserved;
	u64		parameter[0];
};

struct aipc_rfs_open {
	int             flag;
	char            name[0];
};

struct aipc_rfs_close {
	struct file*    filp;
};

struct aipc_rfs_seek {
	struct file*    filp;
	int             origin;
	loff_t		offset;
};

struct aipc_rfs_io {
	struct file*    filp;
	int             size;
	void*           data;
};

struct aipc_rfs_move {
	int old_name_size;
	int new_name_size;
	char name[0];
};

struct aipc_rfs_mount {
        int dev_name_size;
        int dir_name_size;
        int fs_name_size;
        char name[0];
};

struct aipc_rfs_stat {
	int		status;
	struct kstat 	*stat;
	char		name[0];
};

struct pf_devinf {
	u32 cls;	/* total number of clusters */
	u32 ecl;	/* number of unused clusters */
	u32 bps;	/* byte count per sector */
	u32 spc;	/* sector count per cluster */
	u32 cpg;	/* cluster count per cluster group */
	u32 ecg;	/* number of unused cluster groups */
	int fmt;	/* format type */
#define PF_FMT_FAT12	0
#define PF_FMT_FAT16	1
#define PF_FMT_FAT32	2
#define PF_FMT_EXFAT	3
};

struct aipc_rfs_getdev {
	int status;
	struct pf_devinf *devinf;
	char name[0];
};

struct DIR {
	int fd;
	/* the current offset of the buffer*/
	unsigned int offset;
	/* the total size read in the last getdents*/
	unsigned int read_size;
	int buf_size;
	void *buf;
};

struct aipc_rfs_dir {
	struct DIR*		*dirp;
	struct linux_dirent64	dirent;
};

/*
 * process OPEN command
 */
static void rfs_open(struct aipc_rfs_msg *msg)
{
	struct aipc_rfs_open *param = (struct aipc_rfs_open*)msg->parameter;
	struct file *filp;
	int mode = -1;

	msg->msg_type = AIPC_RFS_REPLY_NODEV;

	switch (param->flag) {
	case AIPC_RFS_MODE_RD:
		mode = O_RDONLY;
		break;
	case AIPC_RFS_MODE_WR:
		mode = O_CREAT | O_WRONLY;
		break;
	}
	//DMSG("rfs_open mode %d %X\n", param->flag, mode);

	if (mode != -1) {
		filp = filp_open(param->name, mode, 0);
		DMSG("rfs_open %s %p\n", param->name, filp);

		if (!IS_ERR(filp) && filp != NULL) {
			msg->msg_type = AIPC_RFS_REPLY_OK;
			msg->parameter[0] = (u64)filp;
		}
	}

	msg->msg_len = sizeof(struct aipc_rfs_msg) + sizeof(u64);
}

/*
 * process READ command
 */
static void rfs_read(struct aipc_rfs_msg *msg)
{
	struct aipc_rfs_io *param = (struct aipc_rfs_io*)msg->parameter;
	mm_segment_t oldfs = get_fs();
	struct file* filp = param->filp;
	void *data = (void *) ambalink_phys_to_virt((unsigned long) param->data);
	int ret = 0;

	if (param->size) {
		set_fs(KERNEL_DS);
		ret = vfs_read(filp, data, param->size, &filp->f_pos);
		set_fs(oldfs);
		if (ret < 0)
			EMSG("rfs_read error %d\n", ret);
	}

	msg->msg_type = AIPC_RFS_REPLY_OK;
	msg->msg_len = sizeof(struct aipc_rfs_msg) + sizeof(u64);
	msg->parameter[0] = ret;
}

/*
 * process WRITE command
 */
static void rfs_write(struct aipc_rfs_msg *msg)
{
	struct aipc_rfs_io *param = (struct aipc_rfs_io*)msg->parameter;
	mm_segment_t oldfs = get_fs();
	struct file* filp = param->filp;
	void *data = (void *) ambalink_phys_to_virt((unsigned long) param->data);
	int ret = 0;

	if (param->size) {
		set_fs(KERNEL_DS);
		ret = vfs_write(filp, data, param->size, &filp->f_pos);
		set_fs(oldfs);
		if (ret < 0)
			EMSG("rfs_write error %d\n", ret);
	}

	msg->msg_type = AIPC_RFS_REPLY_OK;
	msg->msg_len = sizeof(struct aipc_rfs_msg) + sizeof(u64);
	msg->parameter[0] = ret;
}

/*
 * process CLOSE command
 */
static void rfs_close(struct aipc_rfs_msg *msg)
{
	struct aipc_rfs_close *param = (struct aipc_rfs_close*)msg->parameter;

	if (param->filp) {
		//DMSG("rfs_close %p\n", param->filp);
		filp_close(param->filp, NULL);
	}
	msg->msg_type = AIPC_RFS_REPLY_OK;
	msg->msg_len = sizeof(struct aipc_rfs_msg);
}

/*
 * process TELL command
 */
static void rfs_tell(struct aipc_rfs_msg *msg)
{
        struct aipc_rfs_seek *param = (struct aipc_rfs_seek*)msg->parameter;
	msg->parameter[0] = (long long)(param->filp->f_pos);
	msg->msg_len  = sizeof(struct aipc_rfs_msg) + sizeof(u64);
}

/*
 * process SEEK command
 */
static void rfs_seek(struct aipc_rfs_msg *msg)
{
        struct aipc_rfs_seek *param = (struct aipc_rfs_seek*)msg->parameter;
	loff_t offset;
	int origin = 0;

	switch (param->origin) {
	case AIPC_RFS_SEEK_SET:
		origin = SEEK_SET;
		break;
	case AIPC_RFS_SEEK_CUR:
		origin = SEEK_CUR;
		break;
	case AIPC_RFS_SEEK_END:
		origin = SEEK_END;
		break;
	}

	offset = (loff_t)param->offset;
	offset = vfs_llseek(param->filp, offset, origin);
	msg->msg_type = AIPC_RFS_REPLY_OK;
	msg->msg_len  = sizeof(struct aipc_rfs_msg) + sizeof(long long);
	msg->parameter[0] = offset;
}

/*
 * process REMOVE command
 */
static void rfs_remove(struct aipc_rfs_msg *msg)
{
	struct aipc_rfs_open *param = (struct aipc_rfs_open*)msg->parameter;
	int    ret;
	mm_segment_t oldfs = get_fs();
	set_fs(KERNEL_DS);
	ret = sys_unlink(param->name);
	set_fs(oldfs);

	DMSG("rfs_remove %s %d\n", param->name, ret);
	msg->msg_len = sizeof(struct aipc_rfs_msg) + sizeof(u64);
	msg->parameter[0] = ret;
}

/*
 * process MOVE command
 */
static void rfs_move(struct aipc_rfs_msg *msg)
{
	struct aipc_rfs_move *param = (struct aipc_rfs_move*)msg->parameter;
	int new_name_size, old_name_size, ret;
	char old_name[500], new_name[500];
	mm_segment_t oldfs = get_fs();

	memset(old_name, 0x0, sizeof(old_name));
	memset(new_name, 0x0, sizeof(new_name));
	old_name_size = param->old_name_size;
	new_name_size = param->new_name_size;
	memcpy(old_name, param->name, old_name_size);
	memcpy(new_name, &param->name[old_name_size], new_name_size);

	/* link the original file to a new file */
	set_fs(KERNEL_DS);
	ret = sys_link(old_name, new_name);
	if(ret < 0) {
		DMSG("rfs_move error: %d\n", ret);
		goto done;
	}

	/* remove the original file*/
	ret = sys_unlink(old_name);
done:
	set_fs(oldfs);
	msg->parameter[0] = ret;
}
/*
 * process CHMOD command, change the permission of the file.
 */
static void rfs_chmod(struct aipc_rfs_msg *msg)
{
	struct aipc_rfs_open *param = (struct aipc_rfs_open*)msg->parameter;
	int ret;
	mm_segment_t oldfs = get_fs();

	set_fs(KERNEL_DS);
	ret = sys_chmod(param->name, param->flag);
	set_fs(oldfs);

	if(ret < 0){
		DMSG("rfs_chmod error: %d\n", ret);
	}
	msg->parameter[0] = ret;
}
/*
 * process CHDMOD command, change the permission of the directory.
 */
static void rfs_chdmod(struct aipc_rfs_msg *msg)
{
	struct aipc_rfs_open *param = (struct aipc_rfs_open*)msg->parameter;
	int ret;
	mm_segment_t oldfs = get_fs();

	set_fs(KERNEL_DS);
	ret = sys_chmod(param->name, param->flag);
	set_fs(oldfs);

	if(ret < 0){
		DMSG("rfs_chdmod error: %d\n", ret);
	}
	msg->parameter[0] = ret;
}
/*
 * process MOUNT command, mount the device to the target directory.
 */
static void rfs_mount(struct aipc_rfs_msg *msg)
{
	struct aipc_rfs_mount *param = (struct aipc_rfs_mount*)msg->parameter;
	int dev_name_size, dir_name_size, fs_name_size, ret;
	char dev_name[450], dir_name[450], fs_type[24];
	mm_segment_t oldfs = get_fs();

	dev_name_size = param->dev_name_size;
	dir_name_size = param->dir_name_size;
	fs_name_size = param->fs_name_size;

	memset(dev_name, 0x0, sizeof(dev_name));
	memset(dir_name, 0x0, sizeof(dir_name));
	memset(fs_type, 0x0, sizeof(fs_type));
	memcpy(dev_name, param->name, dev_name_size);
	memcpy(dir_name, &param->name[dev_name_size], dir_name_size);
	memcpy(fs_type, &param->name[dev_name_size + dir_name_size], fs_name_size);

	set_fs(KERNEL_DS);
	ret = sys_mount(dev_name, dir_name, fs_type,  MS_MGC_VAL | MS_RDONLY | MS_NOSUID, (void *)NULL);
	set_fs(oldfs);

	if(ret < 0){
		DMSG("rfs_mount error: %d\n", ret);
	}
	msg->parameter[0] = ret;

}

/*
 * process UMOUNT command, umount the device to the target directory.
 */
static void rfs_umount(struct aipc_rfs_msg *msg)
{
	struct aipc_rfs_open *param = (struct aipc_rfs_open*)msg->parameter;
	int    ret;
	mm_segment_t oldfs = get_fs();

	set_fs(KERNEL_DS);
	ret = sys_umount(param->name, MNT_DETACH);
	set_fs(oldfs);

	if(ret < 0){
		DMSG("rfs_umount error: %d\n", ret);
	}
	msg->parameter[0] = ret;
}
/*
 * process sync command, let all buffered modification to be written to the disk.
 */
static void rfs_sync(struct aipc_rfs_msg *msg)
{
	int ret;

	ret = sys_sync();

	if(ret < 0){
		DMSG("rfs_sync error: %d\n", ret);
	}
	msg->parameter[0] = ret;
	msg->msg_type = AIPC_RFS_REPLY_OK;
}
/*
 * process fsync command, let all buffered modification to the specified file be written to the disk.
 */
static void rfs_fsync(struct aipc_rfs_msg *msg)
{
	struct aipc_rfs_close *param = (struct aipc_rfs_close*)msg->parameter;
	int ret;

	ret = vfs_fsync(param->filp, 0);

	if(ret < 0){
		DMSG("rfs_fsync error: %d\n", ret);
	}

	msg->parameter[0] = ret;
}

/*
 * process stat command, display the file status.
 */
static void rfs_stat(struct aipc_rfs_msg *msg)
{
	struct aipc_rfs_stat *param = (struct aipc_rfs_stat*)msg->parameter;
	struct kstat *stat;
	int ret;
	mm_segment_t oldfs = get_fs();

	stat = (struct kstat *) ambalink_phys_to_virt((unsigned long) param->stat);
	set_fs(KERNEL_DS);
	ret = vfs_stat(param->name, stat);
	set_fs(oldfs);

	if(ret < 0){
		DMSG("rfs_stat error: %d\n", ret);
	}
	param->status = ret;
}

/*
 * process statfs command, display the file system status.
 */
static void rfs_getdev(struct aipc_rfs_msg *msg)
{
	struct aipc_rfs_getdev *param = (struct aipc_rfs_getdev*)msg->parameter;
	struct kstatfs statfs;
	struct pf_devinf *info;
	int ret;
	mm_segment_t oldfs = get_fs();

	info = (struct pf_devinf *) ambalink_phys_to_virt((unsigned long) param->devinf);
	memset(info, 0x0, sizeof(struct pf_devinf));
	memset(&statfs, 0x0, sizeof(statfs));
	set_fs(KERNEL_DS);
	ret = user_statfs(param->name, &statfs);
	set_fs(oldfs);

	if(ret < 0){
		DMSG("rfs_getdev error: %d\n", ret);
		goto done;
	}

	info->cls = statfs.f_blocks;
	info->ecl = statfs.f_bfree;
	info->bps = 512;
	info->spc = statfs.f_bsize / info->bps;
	info->cpg = 1;
	info->ecg = info->ecl / info->cpg;
	info->fmt = PF_FMT_FAT32;
done:
	param->status = ret;

}

/*
 * process feof command, check whether end-of-file indicator with stream is set.
 */
static void rfs_feof(struct aipc_rfs_msg *msg)
{
	struct aipc_rfs_close *param = (struct aipc_rfs_close*)msg->parameter;
	long long pos = (long long) param->filp->f_pos;
	long long eof;

	eof = vfs_llseek(param->filp, 0, SEEK_END);

	if(eof < 0){
		DMSG("rfs_feof error: %d\n", eof);
		msg->parameter[0] = eof;
		return;
	}

	if(eof == pos){
		msg->parameter[0] = 0;
	}
	else{
		vfs_llseek(param->filp, pos, SEEK_SET); //recover the position indicator
		msg->parameter[0] = -1;
	}

}
/*
 * process MKDIR command
 */
static void rfs_mkdir(struct aipc_rfs_msg *msg)
{
	struct aipc_rfs_open *param = (struct aipc_rfs_open*)msg->parameter;
	int    ret;
	mm_segment_t oldfs = get_fs();

	set_fs(KERNEL_DS);
	ret = sys_mkdir(param->name, 0644);
	set_fs(oldfs);

	if(ret < 0) {
		DMSG("rfs_mkdir error: %d\n", ret);
	}
	msg->msg_len = sizeof(struct aipc_rfs_msg) + sizeof(u64);
	msg->parameter[0] = ret;
}

/*
 * process RMDIR command
 */
static void rfs_rmdir(struct aipc_rfs_msg *msg)
{
	struct aipc_rfs_open *param = (struct aipc_rfs_open*)msg->parameter;
	int    ret;
	mm_segment_t oldfs = get_fs();

	set_fs(KERNEL_DS);
	ret = sys_rmdir(param->name);
	set_fs(oldfs);

	if(ret < 0) {
		DMSG("rfs_rmdir %s %d\n", param->name, ret);
	}
	msg->msg_len = sizeof(struct aipc_rfs_msg) + sizeof(u64);
	msg->parameter[0] = ret;
}


/*
 * process OPENDIR command
 */
static void rfs_opendir(struct aipc_rfs_msg *msg)
{
	struct aipc_rfs_open *param = (struct aipc_rfs_open*) msg->parameter;
	int fd;
	struct DIR *ptr = NULL;
	int maxsize = 1024;
	mm_segment_t oldfs = get_fs();

	set_fs(KERNEL_DS);
	fd = sys_open(param->name, O_RDONLY | O_NDELAY | O_DIRECTORY, 0x0);
	set_fs(oldfs);

	if(fd == -1) {
		DMSG("rfs_opendir: open directory faild\n");
		msg->parameter[0] = -1;
		return;
	}

	ptr = kmalloc(sizeof(struct DIR), GFP_KERNEL);
	memset(ptr, 0x0, sizeof(struct DIR));
	if(ptr == NULL){
		DMSG("rfs_opendir: malloc for DIR failed\n");
		msg->parameter[0] = -1;
		return;
	}

	ptr->buf = kmalloc(maxsize, GFP_KERNEL);
	if(ptr->buf == NULL){
		DMSG("rfs_opendir: malloc for buf failed\n");
		msg->parameter[0] = -1;
		return;
	}

	ptr->fd = fd;
	ptr->buf_size = maxsize;
	ptr->read_size = 0;
	ptr->offset = 0;
	msg->parameter[0] = (u64)ptr;

}

/*
 * process READDIR command
 */
static void rfs_readdir(struct aipc_rfs_msg *msg)
{
	struct aipc_rfs_dir *param = (struct aipc_rfs_dir*) msg->parameter;
	struct DIR *dir = (struct DIR *) param->dirp;
	struct linux_dirent64 *dirent = NULL;
	int bytes = 0;
	mm_segment_t oldfs = get_fs();

	set_fs(KERNEL_DS);
	do{
		/* run out of the buffer, need to refill it. */
		if(dir->offset >= dir->read_size ){

			/* get several dirdents */
			bytes = sys_getdents64(dir->fd, dir->buf, dir->buf_size);
			if(bytes <= 0 ){
				DMSG("rfs_readdir error: %d\n", bytes);
				msg->parameter[0] = -1;
				goto done;
			}
			/* reinitial the parameter for the buffer */
			dir->read_size = (unsigned int) bytes;
			dir->offset = 0;
		}

		dirent = (struct linux_dirent64 *) (((char *) dir->buf) + dir->offset);
		dir->offset +=	dirent->d_reclen;

	/* skip the deleted files */
	} while(dirent->d_ino == 0);

	memcpy(&param->dirent, dirent, dirent->d_reclen);
	msg->parameter[0] = 1; /* succesfully get information */

done:
	set_fs(oldfs);
}

/*
 * process CLOSEDIR command
 */
static void rfs_closedir(struct aipc_rfs_msg *msg)
{
	struct DIR *dir = (struct DIR*)msg->parameter[0];
	int ret;

	if(!dir){
		msg->parameter[0] = -1;
		DMSG("rfs_closedir error: parameter directory is NULL\n");
		return;
	}

	if(dir->fd > 0){
		ret = sys_close(dir->fd);
	} else {
		msg->parameter[0] = -2;
		DMSG("rfs_closedir error: fd does not exist\n");
	}

	kfree(dir);

	if(dir->buf){
		kfree(dir->buf);
		msg->parameter[0] = 0;
	} else {
		msg->parameter[0] = -3;
		DMSG("rfs_closedir error: buf does not exist\n");
	}
}
/*
 * process CHDIR command
 */
static void rfs_chdir(struct aipc_rfs_msg *msg)
{
	struct aipc_rfs_open *param = (struct aipc_rfs_open*)msg->parameter;
	int ret;
	mm_segment_t oldfs = get_fs();

	set_fs(KERNEL_DS);
	ret = sys_chdir(param->name);
	set_fs(oldfs);

	if(ret < 0){
		DMSG("rfs_chdir error: %d", ret);
	}
}

static void (*msg_handler[])(struct aipc_rfs_msg*) = {
	rfs_open,
	rfs_close,
	rfs_read,
	rfs_write,
	rfs_tell,
	rfs_seek,
	rfs_remove,
	rfs_mkdir,
	rfs_rmdir,
	rfs_move,
	rfs_chmod,
	rfs_chdmod,
	rfs_mount,
	rfs_umount,
	rfs_sync,
	rfs_fsync,
	rfs_stat,
	rfs_getdev,
	rfs_feof,
	rfs_opendir,
	rfs_readdir,
	rfs_closedir,
	rfs_chdir,
};

/*
 * process requests from remote core
 */
static int rpmsg_rfs_recv(struct rpmsg_device *rpdev, void *data, int len,
			void *priv, u32 src)
{
	struct aipc_rfs_msg *msg = (struct aipc_rfs_msg*)data;
	int cmd = msg->msg_type;

	if (cmd < 0 || cmd >= ARRAY_SIZE(msg_handler)) {
		EMSG("unknown rfs command %d\n", cmd);
		return -1;
	}

	msg_handler[cmd](msg);
	return rpmsg_send(rpdev->ept, msg, msg->msg_len);
}

static int rpmsg_rfs_probe(struct rpmsg_device *rpdev)
{
	int ret = 0;
	struct rpmsg_channel_info chinfo;

	strncpy(chinfo.name, rpdev->id.name, sizeof(chinfo.name));
	chinfo.src = RPMSG_ADDR_ANY;
	chinfo.dst = rpdev->dst;

	rpmsg_send(rpdev->ept, &chinfo, sizeof(chinfo));

	return ret;
}

static void rpmsg_rfs_remove(struct rpmsg_device *rpdev)
{
}

static struct rpmsg_device_id rpmsg_rfs_id_table[] = {
	{ .name	= "aipc_rfs", },
	{ },
};
MODULE_DEVICE_TABLE(rpmsg, rpmsg_rfs_id_table);

static struct rpmsg_driver rpmsg_rfs_driver = {
	.drv.name	= KBUILD_MODNAME,
	.drv.owner	= THIS_MODULE,
	.id_table	= rpmsg_rfs_id_table,
	.probe		= rpmsg_rfs_probe,
	.callback	= rpmsg_rfs_recv,
	.remove		= rpmsg_rfs_remove,
};

static int __init rpmsg_rfs_init(void)
{
	return register_rpmsg_driver(&rpmsg_rfs_driver);
}

static void __exit rpmsg_rfs_fini(void)
{
	unregister_rpmsg_driver(&rpmsg_rfs_driver);
}

module_init(rpmsg_rfs_init);
module_exit(rpmsg_rfs_fini);

MODULE_DESCRIPTION("RPMSG RFS Server");
