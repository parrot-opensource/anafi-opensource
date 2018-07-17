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

#ifndef __AMBAFS_H__
#define __AMBAFS_H__

#include <linux/fs.h>
#include <linux/types.h>
#include <linux/backing-dev.h>

#define AMBAFS_CMD_LS_INIT       0
#define AMBAFS_CMD_LS_NEXT       1
#define AMBAFS_CMD_LS_EXIT       2
#define AMBAFS_CMD_STAT          3
#define AMBAFS_CMD_OPEN          4
#define AMBAFS_CMD_CLOSE         5
#define AMBAFS_CMD_READ          6
#define AMBAFS_CMD_WRITE         7
#define AMBAFS_CMD_CREATE        8
#define AMBAFS_CMD_DELETE        9
#define AMBAFS_CMD_MKDIR         10
#define AMBAFS_CMD_RMDIR         11
#define AMBAFS_CMD_RENAME        12
#define AMBAFS_CMD_MOUNT         13
#define AMBAFS_CMD_UMOUNT        14
#define AMBAFS_CMD_RESERVED0     15
#define AMBAFS_CMD_RESERVED1     16
#define AMBAFS_CMD_VOLSIZE       17
#define AMBAFS_CMD_QUICK_STAT	 18
#define AMBAFS_CMD_SET_TIME	 19

#define AMBAFS_STAT_NULL         0
#define AMBAFS_STAT_FILE         1
#define AMBAFS_STAT_DIR          2

#define AMBAFS_INO_MAX_RESERVED  256


/* This is used in i_opflags to skip ambafs_get_stat() while doing ambafs_getattr().
 * ambafs_dir_readdir() just get ambafs_stat information, thus no need ambafs_get_stat() again.
 * This improves a lots while "ls" 9999 files in one dir.
 */
#define AMBAFS_IOP_SKIP_GET_STAT       0x0100

/*
 * This is used in i_opflags to skip clear_cache_contents() while ambafs_update_inode().
 * The case for linux create a new file to write, the inode contents should not be cleared.
 * Linux would update the inode size and time and it is different from the ThreadX stat size and time,
 * so we need a flag to skip clear_cache_contents().
 */
#define AMBAFS_IOP_CREATE_FOR_WRITE      0x0200

//#define ENABLE_AMBAFS_DEBUG_MSG		1
#ifdef ENABLE_AMBAFS_DEBUG_MSG
#define AMBAFS_DMSG(...)         printk(__VA_ARGS__)
#else
#define AMBAFS_DMSG(...)
#endif

#define AMBAFS_EMSG(...)         printk(KERN_ERR __VA_ARGS__)

#define QSTAT_BUFF_SIZE         1024

struct ambafs_msg {
	unsigned char   cmd;
	unsigned char   flag;
	unsigned short  len;
	u32		padding;
	void*           xfr;
	unsigned long   parameter[0];
};

struct ambafs_stat {
	unsigned long	statp;
	int64_t         size;
	unsigned long   atime;
	unsigned long   mtime;
	unsigned long   ctime;
	int             type;
	char            name[0];
};

struct ambafs_qstat {
	unsigned long	statp;
	int64_t         size;
	unsigned long   atime;
	unsigned long   mtime;
	unsigned long   ctime;
	int             type;
#define AMBAFS_QSTAT_MAGIC	0x99998888
	u32		magic;
};

struct ambafs_timestmp {
	int year;
	int month;
	int day;
	int hour;
	int min;
	int sec;
};

struct ambafs_stat_timestmp {
	struct ambafs_timestmp	atime;
	struct ambafs_timestmp	mtime;
	struct ambafs_timestmp	ctime;
	char			name[0];
};

struct ambafs_bh {
	int64_t      	offset;
	unsigned long	addr;
	int          	len;
	u32		padding;
};

struct ambafs_io {
	unsigned long		fp;
	int			total;
	u32			padding;
	struct ambafs_bh	bh[0];
};

extern int  ambafs_rpmsg_init(void);
extern void ambafs_rpmsg_exit(void);
extern int  ambafs_rpmsg_exec(struct ambafs_msg *, int);
extern int  ambafs_rpmsg_send(struct ambafs_msg *msg, int len,
			void (*cb)(void*, struct ambafs_msg*, int), void*);

extern struct inode* ambafs_new_inode(struct super_block *, struct ambafs_stat*);
extern void   ambafs_update_inode(struct inode *inode, struct ambafs_stat *stat);
extern struct ambafs_stat* ambafs_get_stat(struct dentry *dentry, struct inode *inode, void *buf, int size);

extern int   ambafs_get_full_path(struct dentry *dir, char *buf, int len);
extern void* ambafs_remote_open(struct dentry *dentry, int flags);
extern void  ambafs_remote_close(void *fp);

extern const struct file_operations  ambafs_dir_ops;
extern const struct file_operations  ambafs_file_ops;
extern const struct inode_operations ambafs_dir_inode_ops;
extern const struct inode_operations ambafs_file_inode_ops;
extern const struct address_space_operations ambafs_aops;
extern const struct dentry_operations ambafs_dentry_ops;
extern       struct backing_dev_info ambafs_bdi;
#endif  //__AMBAFS_H__
