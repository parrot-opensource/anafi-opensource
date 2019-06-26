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

#include "ambafs.h"
#include <linux/aio.h>
#include <linux/statfs.h>

#define REMOTE_EXFAT	3
#define MAX_FILE_SIZE_FAT               (0xFFFFFFFFuL)
#define MAX_FILE_SIZE_EXFAT             (0x1fffffffc00uLL)

/*
 * check inode stats again remote
 */
static int check_stat(struct inode *inode)
{
	int buf[128];
	struct ambafs_stat *stat;
	struct dentry *dentry = (struct dentry*)inode->i_private;

	AMBAFS_DMSG("%s \r\n", __func__);
	stat = ambafs_get_stat(dentry, NULL, buf, sizeof(buf));
	if (stat->type != AMBAFS_STAT_FILE) {
		AMBAFS_DMSG("%s: ambafs_get_stat() failed\n", __func__);
		return -ENOENT;
	}

	ambafs_update_inode(inode, stat);
	return 0;
}

static int check_remote_type(struct dentry *dentry, struct kstatfs *buf)
{
	int tmp[128];
	struct ambafs_msg *msg = (struct ambafs_msg *)tmp;

	buf->f_namelen = NAME_MAX;

	msg->cmd = AMBAFS_CMD_VOLSIZE;
	strcpy((char*)msg->parameter, dentry->d_sb->s_fs_info);
	ambafs_rpmsg_exec(msg, strlen((char*)msg->parameter)+1);

	if (msg->flag == 0) {
		buf->f_bsize = msg->parameter[2];
		buf->f_blocks = msg->parameter[0];
		buf->f_bavail = buf->f_bfree = msg->parameter[1];
		buf->f_type = msg->parameter[3];
	} else {
		buf->f_bsize = buf->f_bavail = buf->f_bfree = buf->f_blocks = 0;
	}

	return msg->flag;
}

/*
 * request remote core to open a file, returns remote filp
 */
void* ambafs_remote_open(struct dentry *dentry, int flags)
{
	int buf[128];
	struct ambafs_msg *msg  = (struct ambafs_msg*) buf;
	char *path = (char*)&msg->parameter[1];
	char *mode;

	ambafs_get_full_path(dentry, path, (char*)buf + sizeof(buf) - path);

	if ((flags & O_WRONLY) && (flags & O_APPEND)) {
		/* read, write, error if file not exist. We have read-for-write in ambafs, use r+ instead of a */
		mode = "r+";
	} else if ((flags & O_WRONLY) || (flags & O_TRUNC) || (flags & O_CREAT)) {
		/* write, create if file not exist,  destory file content if file exist */
		mode = "w";
	} else if (flags & O_RDWR) {
		if ((flags & O_TRUNC) || (flags & O_CREAT)) {
			/* read, write, create if file not exist,  destory file content if file exist */
			mode = "w+";
		} else {
			/* read, write, error if file not exist, */
			mode = "r+";
		}
	} else {
		/* read, error if file not exist, */
		mode = "r";
	}

	msg->cmd = AMBAFS_CMD_OPEN;
	strncpy((char *)&msg->parameter[0], mode, 4);
	/* the size of msg->parameter[0] becomes 64-bits */
	ambafs_rpmsg_exec(msg, 8 + strlen(path) + 1);
	AMBAFS_DMSG("%s %s %s 0x%x\n", __FUNCTION__, path, mode, msg->parameter[0]);
	return (void*)msg->parameter[0];
}

/*
 * request remote core to close a file
 */
void ambafs_remote_close(void *fp)
{
	int buf[16];
	struct ambafs_msg  *msg  = (struct ambafs_msg  *)buf;

	AMBAFS_DMSG("%s %p\n", __FUNCTION__, fp);
	msg->cmd = AMBAFS_CMD_CLOSE;
        msg->parameter[0] = (unsigned long) fp;
	ambafs_rpmsg_exec(msg, 8);
}

static int ambafs_file_open(struct inode *inode, struct file *filp)
{
	int ret;
	struct kstatfs stat;

	AMBAFS_DMSG("%s f_mode = 0x%x, f_flags = 0x%x\r\n", __func__, filp->f_mode, filp->f_flags);
	AMBAFS_DMSG("%s O_APPEND = 0x%x O_TRUNC = 0x%x O_CREAT = 0x%x  O_RDWR = 0x%x O_WRONLY=0x%x\r\n",
		__func__, O_APPEND, O_TRUNC, O_CREAT, O_RDWR, O_WRONLY);

	if (inode->i_sb->s_maxbytes == 0) {
		ret = check_remote_type(filp->f_path.dentry, &stat);
		if (ret == 0) {
			if (stat.f_type == REMOTE_EXFAT) {
				inode->i_sb->s_maxbytes = MAX_FILE_SIZE_EXFAT;
			} else {
				inode->i_sb->s_maxbytes = MAX_FILE_SIZE_FAT;
			}

		}
	}

	ret = check_stat(inode);
	if ((ret < 0) && (filp->f_mode & FMODE_READ)) {
		/*
		 *  We are here for the case, RTOS delete the file without notify linux to drop inode.
		 *  For read, just return failure.
		 *  For write, just reuse the inode to write the file.
		 */
		return ret;
	}

	if (filp->f_flags & (O_CREAT | O_TRUNC))
		inode->i_opflags |= AMBAFS_IOP_CREATE_FOR_WRITE;

	filp->private_data = ambafs_remote_open(filp->f_path.dentry, filp->f_flags);
        if (filp->private_data == NULL) {
                AMBAFS_DMSG("%s fail\n", __FUNCTION__);
                return -EBUSY;
        }
	return 0;
}

static int ambafs_file_release(struct inode *inode, struct file *filp)
{
	struct address_space *mapping = &inode->i_data;
	void *fp = filp->private_data;

	AMBAFS_DMSG("%s %p\n", __FUNCTION__, fp);
	mapping->private_data = fp;

	/* When the file pointer has write permission,
	   the page cache sync is allowed */
	if(filp->f_mode & FMODE_WRITE) {
		AMBAFS_DMSG("%s: before filemap_write_and_wait\n", __FUNCTION__);
		filemap_write_and_wait(inode->i_mapping);
		AMBAFS_DMSG("%s after filemap_write_and_wait\n", __FUNCTION__);
	}

	mapping->private_data = NULL;
	ambafs_remote_close(fp);
	/* Another file pointer might be keeping writing, so the
	file pointer with read-only permission cannot remove
	AMBAFS_IOP_CREATE_FOR_WRITE */
	if(filp->f_mode & FMODE_WRITE) {
		inode->i_opflags &= ~AMBAFS_IOP_CREATE_FOR_WRITE;
	}
	check_stat(inode);
	return 0;
}

/*
 * Sync the file to backing device
 *   The @file is just a place-holder and very likely a newly-created file.
 *   Since the remote FS only allows one file for write, we basically can't
 *   do fsync at all with @file.
 *   We can call generic_file_fsync since it fools the VFS that the file
 *   is synced.
 *   We can't return an error either, otherwise many user-space application
 *   will simply fail.
 */
static int ambafs_file_fsync(struct file *file, loff_t start,  loff_t end,
			int datasync)
{

	AMBAFS_DMSG("%s: start=%d end=%d sync=%d\r\n", __func__, (int)start, (int)end, datasync);

	//return generic_file_fsync(file, start, end, datasync);
	return 0;
}

const struct file_operations ambafs_file_ops = {
	.open		= ambafs_file_open,
	.release	= ambafs_file_release,
	.fsync		= ambafs_file_fsync,
	.llseek		= generic_file_llseek,
	.read_iter	= generic_file_read_iter,
        .write_iter	= generic_file_write_iter,
	.mmap		= generic_file_mmap,
	.splice_read	= generic_file_splice_read,
};
