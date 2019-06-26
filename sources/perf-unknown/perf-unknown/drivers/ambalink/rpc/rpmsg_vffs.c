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
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/rpmsg.h>
#include <linux/err.h>
#include <linux/remoteproc.h>
#include <plat/ambalink_cfg.h>
#include "aipc_priv.h"

//#define ENABLE_DEBUG_MSG_VFFS
#ifdef ENABLE_DEBUG_MSG_VFFS
#define DEBUG_MSG printk
#else
#define DEBUG_MSG(...)
#endif

#define rpdev_name "aipc_fuse_vffs"
#define XFR_ARRAY_SIZE     32

#define VFFS_CMD_OPEN		0
#define VFFS_CMD_READ		1
#define VFFS_CMD_WRITE		2
#define VFFS_CMD_CLOSE		3
#define VFFS_CMD_STAT		4
#define VFFS_CMD_SEEK		5
#define VFFS_CMD_REMOVE		6
#define VFFS_CMD_FSFIRST	7
#define VFFS_CMD_FSNEXT		8
#define VFFS_CMD_CHMOD		9
#define VFFS_CMD_GETDEV		10
#define VFFS_CMD_RENAME		11
#define VFFS_CMD_MKDIR		12
#define VFFS_CMD_RMDIR		13

#define IPC_VFFS_SEEK_SET	0
#define IPC_VFFS_SEEK_CUR	1
#define IPC_VFFS_SEEK_END	2


struct vffs_msg {
	unsigned char   cmd;
	unsigned char   flag;
	unsigned short  len;
	void*           xfr;
	u64             parameter[0];
};

struct vffs_xfr {
	struct completion comp;
	int               refcnt;
	void              (*cb)(void*, struct vffs_msg *, int);
	void              *priv;
};

struct vffs_fopen_arg {
	int	mode;
	char	file[0];
};

struct vffs_io_arg {
	u64 buf;
	unsigned int size;
	int fp;
};

struct vffs_fseek_arg {
	int fp;
	int origin;
	long long offset;
};

struct vffs_fsfirst_arg {
	u8	attr;
	u64	res;
	char	path[0];
};

struct vffs_stat {
	int 	rval;
	int 	fs_type;
	u64 	fstfz;		/* file size in bytes */
	u16 	fstact;		/* file last access time */
	u16 	fstad;		/* last file access date */
	u8  	fstautc;	/* last file access date and time UTC offset */
	u16 	fstut;		/* last file update time */
	u16 	fstuc;		/* last file update time[10ms] */
	u16 	fstud;		/* last file update date */
	u8  	fstuutc;	/* last file update date and time UTC offset */
	u16 	fstct;		/* file create time */
	u16 	fstcd;		/* file create date */
	u16 	fstcc;		/* file create component time (ms) */
	u8  	fstcutc;	/* file create date and time UTC offset */
	u16 	fstat;		/* file attribute */
};

#define VFFS_SHORT_NAME_LEN	26
#define VFFS_LONG_NAME_LEN	512
struct vffs_fsfind
{
	int rval;
	u32 dta;
	u16 Time;
	u16 Date;
	u64 FileSize;
	char Attribute;
	char FileName[VFFS_SHORT_NAME_LEN];
	char LongName[VFFS_LONG_NAME_LEN];
};

static DEFINE_SPINLOCK(xfr_lock);
static struct semaphore		xfr_sem;
static struct rpmsg_device	*rpdev_vffs;
static struct vffs_xfr		xfr_slot[XFR_ARRAY_SIZE];

static int last_mapped_vfd = -1;
static int last_unmapped_vfd = -1;
static struct proc_dir_entry *proc_ffs = NULL;
static struct vffs_stat last_ffs_stat;
static struct vffs_getdev last_ffs_getdev;
static struct vffs_fsfind last_fsfind;

struct vffs_getdev
{
	int fs_type;
	u32 cls;	/* total number of clusters */
	u32 ecl;	/* number of unused clusters */
	u32 bps;	/* bytes per sector */
	u32 spc;	/* sectors per cluster, for udf, spc=1 */
	u32 cpg;	/* clusters per cluster group */
	u32 ecg;	/* number of empty cluster groups */
	int fmt;	/* format type */
};

struct vffs_rename_arg
{
	int src_len;		/* the length of filename of src file */
	int dst_len;		/* the length of filename of src file */
	char filename[0];	/* src_name & dst_name */
};

/*
 * Mapped entry to virtual ffs.
 */
struct proc_ffs_list
{
	struct list_head list;
	int fp;				/* File pointer */
	char name[16];			/* As presented on proc fs */
	char filename[256];		/* Actual file name on ffs */
	struct proc_dir_entry *proc;	/* proc entry created for this file */
};

static LIST_HEAD(pl_head);
static struct mutex g_mlock;

/*
 * used by proc_file_read to use specified buffer instead of 3K
 * the buffer size is 128KB by default
 */
int vffs_b_size = PAGE_SIZE<<5;
EXPORT_SYMBOL(vffs_b_size);
static char logbs=0;

/*
 * find a free xfr slot, inc the ref count and return the slot
 */
static struct vffs_xfr *find_free_xfr(void)
{
	int i;
	unsigned long flags;

	down(&xfr_sem);
	spin_lock_irqsave(&xfr_lock, flags);
	for (i = 0; i < XFR_ARRAY_SIZE; i++) {
		if (!xfr_slot[i].refcnt) {
			xfr_slot[i].refcnt = 1;
			reinit_completion(&xfr_slot[i].comp);
			spin_unlock_irqrestore(&xfr_lock, flags);
			return &xfr_slot[i];
		}
	}
	spin_unlock_irqrestore(&xfr_lock, flags);

	/* FIXME: should wait for a xfr slot becoming available */
	BUG();
	return NULL;
}

/*
 * increase xfr refcount
 */
static void xfr_get(struct vffs_xfr *xfr)
{
	unsigned long flags;

	spin_lock_irqsave(&xfr_lock, flags);
	xfr->refcnt++;
	spin_unlock_irqrestore(&xfr_lock, flags);
}

/*
 * decrease xfr refcount
 */
static void xfr_put(struct vffs_xfr *xfr)
{
	unsigned long flags;
	int count;

	spin_lock_irqsave(&xfr_lock, flags);
	count = --xfr->refcnt;
	spin_unlock_irqrestore(&xfr_lock, flags);

	if (!count)
		up(&xfr_sem);
}

/*
 * xfr callback for vffs_rpsmg_exec
 * copy the incoming msg and wake up the waiting thread
 */
static void exec_cb(void *priv, struct vffs_msg *msg, int len)
{
	struct vffs_msg *out_msg = (struct vffs_msg*)priv;
	struct vffs_xfr *xfr = (struct vffs_xfr*) msg->xfr;

	memcpy(out_msg, msg, len);
	complete(&xfr->comp);
}


/*
 * send msg and wait on reply
 */
int vffs_rpmsg_exec(struct vffs_msg *msg, int len)
{
	struct vffs_xfr *xfr = find_free_xfr();

	/* set up xfr for the msg */
	xfr->cb = exec_cb;
	xfr->priv = msg;
	msg->xfr = xfr;

	/*
	 * Need lock xfr here, otherwise xfr could be freed in rpmsg_vfs_recv
	 * before wait_for_completion starts to execute.
	 */
	xfr_get(xfr);
	rpmsg_send(rpdev_vffs->ept, msg, len + sizeof(struct vffs_msg));
	wait_for_completion(&xfr->comp);
	xfr_put(xfr);

	return 0;
}

/*
 * send msg and return immediately
 */
int vffs_rpmsg_send(struct vffs_msg *msg, int len,
		void (*cb)(void*, struct vffs_msg *, int), void* priv)
{
	if (cb) {
		struct vffs_xfr *xfr = find_free_xfr();
		xfr->cb = cb;
		xfr->priv = priv;
		msg->xfr = xfr;
	} else {
		msg->xfr = NULL;
	}

	rpmsg_send(rpdev_vffs->ept, msg, len + sizeof(struct vffs_msg));
	return 0;
}

int vffs_rpmsg_fopen(const char *file, const char *mode, int *error)
{
	int buf[128];
	struct vffs_msg *msg = (struct vffs_msg *)buf;
	struct vffs_fopen_arg *arg = (struct vffs_fopen_arg *)msg->parameter;

	strcpy(arg->file, file);
	memcpy(&arg->mode, mode, 4);
	msg->cmd = VFFS_CMD_OPEN;

	vffs_rpmsg_exec(msg, 8 + strlen(file) + 1);
	*error = msg->parameter[1];

	return (int)(msg->parameter[0] & 0x00000000FFFFFFFF);
}

int vffs_rpmsg_fclose(int fd)
{
	int buf[128];
	struct vffs_msg *msg = (struct vffs_msg *)buf;

	msg->parameter[0] = fd;
	msg->cmd = VFFS_CMD_CLOSE;

	vffs_rpmsg_exec(msg, 8);

	return msg->parameter[0];
}

int vffs_rpmsg_fread(char *buf, unsigned int size, int fp)
{
	int msg_buf[128];
	struct vffs_msg *msg = (struct vffs_msg *)msg_buf;
	struct vffs_io_arg *arg = (struct vffs_io_arg *)msg->parameter;

	arg->buf = (u64) ambalink_virt_to_phys((unsigned long) buf);
	arg->size = size;
	arg->fp = fp;
	msg->cmd = VFFS_CMD_READ;

	vffs_rpmsg_exec(msg, sizeof(struct vffs_io_arg));

	return msg->parameter[0];
}

int vffs_rpmsg_fwrite(const char *buf, unsigned int size, int fp)
{
	int msg_buf[128];
	struct vffs_msg *msg = (struct vffs_msg *)msg_buf;
	struct vffs_io_arg *arg = (struct vffs_io_arg *)msg->parameter;

	arg->buf = (u64) ambalink_virt_to_phys((unsigned long) buf);
	arg->size = size;
	arg->fp = fp;
	msg->cmd = VFFS_CMD_WRITE;

	vffs_rpmsg_exec(msg, sizeof(struct vffs_io_arg));

	return msg->parameter[0];
}

int vffs_rpmsg_fstat(const char *path, struct vffs_stat *ffs_stat)
{
	int msg_buf[128];
	struct vffs_msg *msg = (struct vffs_msg *)msg_buf;

	msg->parameter[0] = ambalink_virt_to_phys((unsigned long) ffs_stat);
	strcpy((char *)&msg->parameter[1], path);
	msg->cmd = VFFS_CMD_STAT;

	vffs_rpmsg_exec(msg, 8 + strlen(path) + 1);

	return ffs_stat->rval;
}

int vffs_rpmsg_fseek(int fp, unsigned int offset, int origin)
{
	int msg_buf[128];
	struct vffs_msg *msg = (struct vffs_msg *)msg_buf;
	struct vffs_fseek_arg *arg = (struct vffs_fseek_arg *)msg->parameter;

	arg->fp = fp;
	arg->offset = offset;
	arg->origin = origin;
	msg->cmd = VFFS_CMD_SEEK;

	vffs_rpmsg_exec(msg, sizeof(struct vffs_fseek_arg));

	return msg->parameter[0];
}

int vffs_rpmsg_remove(const char *fname)
{
	int msg_buf[128];
	struct vffs_msg *msg = (struct vffs_msg *)msg_buf;

	strcpy((char *)msg->parameter, fname);
	msg->cmd = VFFS_CMD_REMOVE;

	vffs_rpmsg_exec(msg, strlen(fname) + 1);

	return msg->parameter[0];
}

int vffs_rpmsg_fsfirst(const char *path, unsigned char attr,
			struct vffs_fsfind *fsfind)
{
	int msg_buf[128];
	struct vffs_msg *msg = (struct vffs_msg *)msg_buf;
	struct vffs_fsfirst_arg *arg = (struct vffs_fsfirst_arg *)msg->parameter;

	strcpy(arg->path, path);
	arg->attr = attr;
	arg->res = (u64) ambalink_virt_to_phys((unsigned long) fsfind);
	msg->cmd = VFFS_CMD_FSFIRST;

	vffs_rpmsg_exec(msg, sizeof(struct vffs_fsfirst_arg) + strlen(path) + 1);

	return 0;
}

int vffs_rpmsg_fsnext(struct vffs_fsfind *fsfind)
{
	int msg_buf[128];
	struct vffs_msg *msg = (struct vffs_msg *)msg_buf;

	msg->parameter[0] = fsfind->dta;
	msg->parameter[1] = ambalink_virt_to_phys((unsigned long) fsfind);
	msg->cmd = VFFS_CMD_FSNEXT;

	vffs_rpmsg_exec(msg, 16);

	return 0;
}

int vffs_rpmsg_chmod(const char *file, int attr)
{
	int msg_buf[128];
	struct vffs_msg *msg = (struct vffs_msg *)msg_buf;

	msg->parameter[0] = attr;
	strcpy((char *)&msg->parameter[1], file);
	msg->cmd = VFFS_CMD_CHMOD;

	vffs_rpmsg_exec(msg, 8 + strlen(file) + 1);

	return msg->parameter[0];
}

int vffs_rpmsg_getdev(const char *path, struct vffs_getdev *ffs_getdev)
{
	int msg_buf[128];
	struct vffs_msg *msg = (struct vffs_msg *)msg_buf;

	msg->parameter[0] = ambalink_virt_to_phys((unsigned long) ffs_getdev);
	strcpy((char *)&msg->parameter[1], path);
	msg->cmd = VFFS_CMD_GETDEV;

	vffs_rpmsg_exec(msg, 8 + strlen(path) + 1);

	return msg->parameter[0];
}

int vffs_rpmsg_rename(const char *srcname, const char *dstname)
{
	int msg_buf[128];
	int src_len, dst_len;
	struct vffs_msg *msg = (struct vffs_msg *)msg_buf;
	struct vffs_rename_arg *arg = (struct vffs_rename_arg *)msg->parameter;

	src_len = strlen(srcname) + 1;
	dst_len = strlen(dstname) + 1;
	strcpy((char *)arg->filename, srcname);
	strcpy((char *)&arg->filename[src_len], dstname);
	arg->src_len = src_len;
	arg->dst_len = dst_len;
	msg->cmd = VFFS_CMD_RENAME;

	vffs_rpmsg_exec(msg, sizeof(struct vffs_rename_arg) + src_len + dst_len);

	return msg->parameter[0];
}

int vffs_rpmsg_mkdir(const char *dname)
{
	int msg_buf[128];
	struct vffs_msg *msg = (struct vffs_msg *)msg_buf;

	strcpy((char *)msg->parameter, dname);
	msg->cmd = VFFS_CMD_MKDIR;

	vffs_rpmsg_exec(msg, strlen(dname) + 1);

	return msg->parameter[0];
}

int vffs_rpmsg_rmdir(const char *dname)
{
	int msg_buf[128];
	struct vffs_msg *msg = (struct vffs_msg *)msg_buf;

	strcpy((char *)msg->parameter, dname);
	msg->cmd = VFFS_CMD_RMDIR;

	vffs_rpmsg_exec(msg, strlen(dname) + 1);

	return msg->parameter[0];
}
/*
 * Mapped file read.
 */
ssize_t vffs_proc_fs_actual_read(struct file *filp, char __user *buf, size_t count, loff_t *offp)
{
	struct proc_ffs_list *pl = (struct proc_ffs_list *) PDE_DATA(file_inode(filp));
	int rval;
	int len = 0;
	char *rbuf;

	DEBUG_MSG("%s read file fp %p\n", __func__, pl->fp);

	rbuf = kmalloc(count, GFP_KERNEL);
	if (!rbuf) {
		rval = -ENOMEM;
		printk("%s kmalloc size %lu failed\n", __func__, count);
		goto done;
	}

	mutex_lock(&g_mlock);

	rval = vffs_rpmsg_fseek(pl->fp, *offp, IPC_VFFS_SEEK_SET);
	if (rval < 0) {
		rval = -EFAULT;
		goto done;
	}

	rval = vffs_rpmsg_fread(rbuf, count, pl->fp);
	if (rval < 0) {
		DEBUG_MSG("actual_read fail\n");
		rval = -EIO;
		goto done;
	} else {
		len = rval;
		rval = copy_to_user(buf, rbuf, len);
		DEBUG_MSG("%s read file %p len %d\n", __func__, pl->fp, len);
	}

done:
	mutex_unlock(&g_mlock);

	if (rbuf)
		kfree(rbuf);

	if (rval < 0) {
		return rval;
	}

	return len;
}
EXPORT_SYMBOL(vffs_proc_fs_actual_read);

/*
 * Mapped file write.
 */
static ssize_t vffs_proc_fs_actual_write(struct file *filp, const char  __user *buf, size_t count,loff_t *offp)
{
	char* data = PDE_DATA(file_inode(filp));
	struct proc_ffs_list *pl = (struct proc_ffs_list *) data;
	void *wbuf = NULL;
	int rval, len = 0;

	wbuf = kmalloc(vffs_b_size, GFP_KERNEL);
	if (!wbuf) {
		rval = -ENOMEM;
		goto done;
	}

	if (count > vffs_b_size)
		len = vffs_b_size;
	else
		len = count;

	if (copy_from_user(wbuf, buf, len)) {
		rval = -EFAULT;
		goto done;
	}

	rval = vffs_rpmsg_fwrite(wbuf, len, pl->fp);
	if (rval < 0) {
		rval = -EIO;
		goto done;
	}

done:
	if (wbuf)
		kfree(wbuf);
	if (logbs)
		printk(" %d/%d",rval,len);

	return rval;
}


/*
 * /proc/aipc/ffs/map read function.
 */
static int vffs_proc_fs_map_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%.8x\n", last_mapped_vfd);
	return 0;
}

static int vffs_proc_fs_map_open(struct inode *inode, struct file *file)
{
	return single_open(file, vffs_proc_fs_map_show, PDE_DATA(inode));
}

static struct file_operations proc_map_write_fops = {
		.owner   = THIS_MODULE,
		.read    = vffs_proc_fs_actual_read,
		.write   = vffs_proc_fs_actual_write,
};
/*
 * /proc/aipc/ffs/map write function.
 */
static ssize_t vffs_proc_fs_map_write(struct file *file,
				   const char __user *buffer,
				   size_t count, loff_t *data)
{
	struct proc_ffs_list *pl;
	struct proc_dir_entry *proc;

	char name[16];
	char filename[260];
	char mode[3];
	int vfd, rval = 0;
	int i;

	if (count > sizeof(filename))
		return -EINVAL;

	memset(&filename, 0x0, sizeof(filename));
	if (copy_from_user(filename, buffer, count))
		return -EFAULT;

	/* Strip trailing \r or \n */
	for (i = count - 1 ; i >= 0; i--) {
		if (filename[i] == '\r' || filename[i] == '\n')
			filename[i] = '\0';
		else
			break;
	}

	/* Get the parameters */
	for (; i >= 0; i--) {
		if (filename[i] == ',') {
			strncpy(mode, &filename[i + 1], sizeof(mode));
			mode[sizeof(mode) - 1] = '\0';
			filename[i] = '\0';
			break;
		}
	}

	if (i < 0)
		strcpy(mode, "r");

	mutex_lock(&g_mlock);

	/* Try to obtain a file descriptor */
	vfd = vffs_rpmsg_fopen(filename, mode, &rval);
	if (vfd == 0) {
		goto done;
	}
	snprintf(name, sizeof(name), "%.8x", vfd);
	last_mapped_vfd = vfd;

	/* Create a new node */
	pl = kzalloc(sizeof(*pl), GFP_KERNEL);
	if (pl) {
		pl->fp = vfd;
		strncpy(pl->name, name, sizeof(pl->name));
		pl->name [sizeof(pl->name) - 1] = '\0';
		strncpy(pl->filename, filename, sizeof(pl->filename));
		pl->filename [sizeof(pl->filename) - 1]  = '\0';
		pl->proc = NULL;
		list_add_tail(&pl->list, &pl_head);
	} else {
		rval = -ENOMEM;
		goto done;
	}

	DEBUG_MSG(KERN_WARNING "map_write: %s, %s, %s\n", filename, mode, pl->name);

	/* Create a new proc entry */
	proc = proc_create_data(pl->name, S_IRUGO | S_IWUSR, proc_ffs, &proc_map_write_fops, pl);
	if (proc) {
		pl->proc = proc;	/* Save pointer to pl */
		// proc->size = pl->stat.fstfz;
	} else {
		DEBUG_MSG("%s proc create failed\n", pl->name);
	}

done:
	mutex_unlock(&g_mlock);
	if (rval) {
		DEBUG_MSG("%s open file %s %s failed %d\n", __func__, filename, mode, rval);
		return -rval;
	}

	return count;
}

/*
 * /proc/aipc/ffs/bs read function.
 */
static int vffs_proc_fs_bs_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", vffs_b_size);
	return 0;
}

static int vffs_proc_fs_bs_open(struct inode *inode, struct file *file)
{
	return single_open(file, vffs_proc_fs_bs_show, PDE_DATA(inode));
}

/*
 * /proc/aipc/ffs/bs write function.
 */
static ssize_t vffs_proc_fs_bs_write(struct file *file,
				     const char __user *buffer,
				     size_t count, loff_t *data)
{
	char buf[12];

	memset(&buf, 0x0, sizeof(buf));
	if (copy_from_user(buf, buffer, count))
		return -EFAULT;

	if (0==strcmp(buf,"0")) {
		logbs++;
		logbs%=2;
	} else {
		vffs_b_size=(int)simple_strtol(buf, NULL, 10);
	}

	return count;
}

/*
 * /proc/aipc/ffs/unmap read function.
 */
static int vffs_proc_fs_unmap_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%.8x\n", last_unmapped_vfd);
	return 0;
}

static int vffs_proc_fs_unmap_open(struct inode *inode, struct file *file)
{
	return single_open(file, vffs_proc_fs_unmap_show, PDE_DATA(inode));
}

/*
 * /proc/aipc/ffs/unmap write function.
 */
static ssize_t vffs_proc_fs_unmap_write(struct file *file,
				     const char __user *buffer,
				     size_t count, loff_t *data)
{
	struct proc_ffs_list *pl;
	struct list_head *pos, *q;
	char name[16];
	int all = 0;
	int vfd = 0;
	int i, name_len;

	if (count > sizeof(name))
		return -EINVAL;

	memset(&name, 0x0, sizeof(name));
	if (copy_from_user(name, buffer, count))
		return -EFAULT;

	/* Strip trailing \r or \n */
	for (i = count - 1 ; i >= 0; i--) {
		if (name[i] == '\r' || name[i] == '\n')
			name[i] = '\0';
		else
			break;
	}
	name_len = i + 1;

	if (strcasecmp(name, "all") == 0) {
		all = 1;
	} else {
		/* Scan the name for exact 8-digit hex character match */
		if (name_len != 8)
			goto done;
		for (i = 0; i < 8; i++) {
			int x;

			if (name[i] >= '0' && name[i] <= '9')
				x = name[i] - '0';
			else if (name[i] >= 'a' && name[i] <= 'f')
				x = name[i] - 'a' + 10;
			else if (name[i] >= 'A' && name[i] <= 'F')
				x = name[i] - 'A' + 10;
			else
				goto done;

			vfd |= (x << ((7 - i) * 4));
		}
	}
	mutex_lock(&g_mlock);
	list_for_each_safe(pos, q, &pl_head) {
		pl = list_entry(pos, struct proc_ffs_list, list);
		if (all || pl->fp == vfd) {
			DEBUG_MSG(KERN_WARNING "unmap_write: %s, %x\n",
					pl->filename, pl->fp);
			last_unmapped_vfd = pl->fp;
			vffs_rpmsg_fclose(pl->fp);
			remove_proc_entry(pl->name, proc_ffs);
			list_del(pos);
			kfree(pl);
			break;
		}
	}
	mutex_unlock(&g_mlock);

done:

	return count;
}

static ssize_t vffs_proc_fs_fstat_bin_read(struct file *filp, char *buf, size_t count, loff_t *offp)
{
	int ret, len;

	len = sizeof(last_ffs_stat);
	ret = copy_to_user(buf, &(last_ffs_stat.rval), len);

	if(ret) {
		DEBUG_MSG("%s copy_to_user %d failed!", __func__, ret);
		return (sizeof(last_ffs_stat) - ret);
	}

	return sizeof(last_ffs_stat);
}

/*
 * /proc/aipc/ffs/fstat write function.
 */
static ssize_t vffs_proc_fs_fstat_write(struct file *file,
				     const char __user *buffer,
				     size_t count, loff_t *data)
{
	char filename[256];
	int rval, i;

	if (count > sizeof(filename))
		return -EINVAL;

	memset(&filename, 0x0, sizeof(filename));
	if (copy_from_user(filename, buffer, count))
		return -EFAULT;

	/* Strip trailing \r or \n */
	for (i = count - 1 ; i >= 0; i--) {
		if (filename[i] == '\r' || filename[i] == '\n')
			filename[i] = '\0';
		else
			break;
	}

	DEBUG_MSG("%s fstat file %s\n", __func__, filename);

	rval = vffs_rpmsg_fstat(filename, &last_ffs_stat);
	if (rval) {
		DEBUG_MSG("fstat_write: %s %d\n", filename, rval);
		return -rval;
	}

	return count;
}

/*
 * /proc/aipc/ffs/remove write function.
 */
static ssize_t vffs_proc_fs_remove_write(struct file *file,
				     const char __user *buffer,
				     size_t count, loff_t *data)
{
	char filename[256];
	int rval, i;

	if (count > sizeof(filename))
		return -EINVAL;

	memset(&filename, 0x0, sizeof(filename));
	if (copy_from_user(filename, buffer, count))
		return -EFAULT;

	/* Strip trailing \r or \n */
	for (i = count - 1 ; i >= 0; i--) {
		if (filename[i] == '\r' || filename[i] == '\n')
			filename[i] = '\0';
		else
			break;
	}

	rval = vffs_rpmsg_remove(filename);
	if (rval) {
		DEBUG_MSG("remove_write: %s %d\n", filename, rval);
		return -rval;
	}

	return count;
}

/*
 * /proc/aipc/ffs/fsfirst read function.
 */
static ssize_t vffs_proc_fs_fsfirst_bin_read(struct file *filp, char *buf,
	size_t count, loff_t *offp)
{
	int len = 0;
	int ret;

	len = sizeof(last_fsfind);
	ret = copy_to_user(buf, &last_fsfind, len);

	if(ret) {
		DEBUG_MSG("%s copy_to_user %d failed!", __func__, ret);
		return (sizeof(last_fsfind) - ret);
	}

	return len;
}

/*
 * /proc/aipc/ffs/fsfirst write function. ipc_ffs_fsfirst
 */
#define ATTR_ALL                (0x007FuL)          /* for fsfirst function */
static ssize_t vffs_proc_fs_fsfirst_write(struct file *file,
				     const char __user *buffer,
				     size_t count, loff_t *data)
{
	char pathname[256];
	int rval, i;

	if (count > sizeof(pathname))
		return -EINVAL;

	memset(&pathname, 0x0, sizeof(pathname));
	if (copy_from_user(pathname, buffer, count))
		return -EFAULT;

	/* Strip trailing \r or \n */
	for (i = count - 1 ; i >= 0; i--) {
		if (pathname[i] == '\r' || pathname[i] == '\n')
			pathname[i] = '\0';
		else
			break;
	}

	DEBUG_MSG("%s fsfirst %s\n", __func__, pathname);

	/* Try to obtain a file descriptor */
	rval = vffs_rpmsg_fsfirst(pathname, ATTR_ALL, &last_fsfind);
	if (rval) {
		DEBUG_MSG("fsfirst_write: %s %d\n", pathname, rval);
		return -rval;
	}

	return count;
}

/*
 * /proc/aipc/ffs/fsnext read function.
 */
static ssize_t vffs_proc_fs_fsnext_bin_read(struct file *filp, char *buf,
	size_t count, loff_t *offp)
{
	int len = 0, rval;

	if (*offp > 0)
		goto done;

	vffs_rpmsg_fsnext(&last_fsfind);

	len = sizeof(last_fsfind);
	rval = copy_to_user(buf, &last_fsfind, len);

	if(rval) {
		DEBUG_MSG("%s copy_to_user %d failed!", __func__, rval);
		return (sizeof(last_ffs_stat) - rval);
	}

done:
	return len;
}

/*
 * /proc/aipc/ffs/chmod write function.
 */
static ssize_t vffs_proc_fs_chmod_write(struct file *file,
				     const char __user *buffer,
				     size_t count, loff_t *data)
{
	char filename[260];
	char *ptr;
	int i, attr, rval;

	if (count > sizeof(filename) - 1)
		return -EINVAL;

	memset(&filename, 0x0, sizeof(filename));
	if (copy_from_user(filename, buffer, count))
		return -EFAULT;
	filename[count] = '\0';

	/* Strip trailing \r or \n */
	for (i = count - 1 ; i >= 0; i--) {
		if (filename[i] == '\r' || filename[i] == '\n')
			filename[i] = '\0';
		else
			break;
	}

	ptr = strstr(filename, " ");
	if (ptr == NULL) {
		rval = -EINVAL;
		goto done;
	}

	attr = simple_strtol(ptr + 1, NULL, 0);
	attr %= 1000;

	ptr[0] = '\0';

	rval = vffs_rpmsg_chmod(filename, attr);
	if (rval) {
		DEBUG_MSG("vffs chmod fail, error %d\n", rval);
		rval = -rval;
	} else
		rval = count;
done:
	return rval;
}

/*
 * /proc/aipc/ffs/getdev read function.
 */
static ssize_t vffs_proc_fs_getdev_bin_read(struct file *filp, char *buf,
	size_t count, loff_t *offp)
{
	int ret;

	ret = copy_to_user(buf, &last_ffs_getdev, sizeof(last_ffs_getdev));

	if(ret) {
		DEBUG_MSG("%s copy_to_user %d failed!", __func__, ret);
		return (sizeof(last_ffs_getdev) - ret);
	}

	return sizeof(last_ffs_getdev);
}

/*
 * /proc/aipc/ffs/getdev write function.
 */
static ssize_t vffs_proc_fs_getdev_write(struct file *file,
				     const char __user *buffer,
				     size_t count, loff_t *data)
{
	char drive[2];
	int rval;

	memset(&drive, 0x0, sizeof(drive));
	if (copy_from_user(drive, buffer, 1))
		return -EFAULT;

	rval = vffs_rpmsg_getdev(drive, &last_ffs_getdev);
	if (rval) {
		DEBUG_MSG("getdev_write: %s %d\n", drive, rval);
		return -rval;
	}

	return count;
}

/*
 * /proc/aipc/ffs/rename write function.
 */
static ssize_t vffs_proc_fs_rename_write(struct file *file,
				     const char __user *buffer,
				     size_t count, loff_t *data)
{
	char filename[260];
	char *srcname, *dstname, *ptr;
	int i, rval;

	if (count > sizeof(filename) - 1)
		return -EINVAL;

	memset(&filename, 0x0, sizeof(filename));
	if (copy_from_user(filename, buffer, count))
		return -EFAULT;
	filename[count] = '\0';

	/* Strip trailing \r or \n */
	for (i = count - 1 ; i >= 0; i--) {
		if (filename[i] == '\r' || filename[i] == '\n')
			filename[i] = '\0';
		else
			break;
	}

	ptr = strstr(filename, " ");
	if (ptr == NULL) {
		rval = -EINVAL;
		goto done;
	}
	ptr[0] = '\0';
	srcname = filename;

	dstname = ptr + 1;

	DEBUG_MSG(KERN_WARNING "vffs move %s %s\n", srcname, dstname);

	rval = vffs_rpmsg_rename(srcname, dstname);

	if (rval) {
		DEBUG_MSG("vffs move fail, error code %d\n", rval);
		rval = -rval;
	} else
		rval = count;
done:
	return rval;
}

/*
 * /proc/aipc/ffs/mkdir write function.
 */
static ssize_t vffs_proc_fs_mkdir_write(struct file *file,
				     const char __user *buffer,
				     size_t count, loff_t *data)
{
	char pathname[256];
	int rval, i;

	if (count > sizeof(pathname))
		return -EINVAL;

	memset(&pathname, 0x0, sizeof(pathname));
	if (copy_from_user(pathname, buffer, count))
		return -EFAULT;

	/* Strip trailing \r or \n */
	for (i = count - 1 ; i >= 0; i--) {
		if (pathname[i] == '\r' || pathname[i] == '\n')
			pathname[i] = '\0';
		else
			break;
	}

	/* Try to obtain a file descriptor */
	rval = vffs_rpmsg_mkdir(pathname);
	if (rval) {
		DEBUG_MSG("mkdir_write: %s %d\n", pathname, rval);
		return -rval;
	}

	return count;
}

/*
 * /proc/aipc/ffs/rmdir write function.
 */
static ssize_t vffs_proc_fs_rmdir_write(struct file *file,
				     const char __user *buffer,
				     size_t count, loff_t *data)
{
	char pathname[256];
	int rval, i;

	if (count > sizeof(pathname))
		return -EINVAL;

	memset(&pathname, 0x0, sizeof(pathname));
	if (copy_from_user(pathname, buffer, count))
		return -EFAULT;

	/* Strip trailing \r or \n */
	for (i = count - 1 ; i >= 0; i--) {
		if (pathname[i] == '\r' || pathname[i] == '\n')
			pathname[i] = '\0';
		else
			break;
	}

	/* Try to obtain a file descriptor */
	rval = vffs_rpmsg_rmdir(pathname);
	if (rval) {
		DEBUG_MSG("rmdir_write: %s %d\n", pathname, rval);
		return -rval;
	}

	return count;
}


static struct file_operations proc_bs_fops = {
		.owner = THIS_MODULE,
		.open = vffs_proc_fs_bs_open,
		.read  = seq_read,
		.llseek = seq_lseek,
		.write = vffs_proc_fs_bs_write,
		.release = single_release,
};

static struct file_operations proc_map_fops = {
		.owner = THIS_MODULE,
		.open = vffs_proc_fs_map_open,
		.read  = seq_read,
		.llseek = seq_lseek,
		.write = vffs_proc_fs_map_write,
		.release = single_release,
};

static struct file_operations proc_unmap_fops = {
		.owner = THIS_MODULE,
		.open = vffs_proc_fs_unmap_open,
		.read  = seq_read,
		.llseek = seq_lseek,
		.write = vffs_proc_fs_unmap_write,
		.release = single_release,
};

static struct file_operations proc_fstat_bin_fops = {
		.owner = THIS_MODULE,
		.read  = vffs_proc_fs_fstat_bin_read,
		.write = vffs_proc_fs_fstat_write,
};

static struct file_operations proc_remove_fops = {
		.owner = THIS_MODULE,
		.write = vffs_proc_fs_remove_write,
};

static struct file_operations proc_fsfirst_bin_fops = {
		.owner = THIS_MODULE,
		.read  = vffs_proc_fs_fsfirst_bin_read,
		.write = vffs_proc_fs_fsfirst_write,
};

static struct file_operations proc_fsnext_bin_fops = {
		.owner = THIS_MODULE,
		.read  = vffs_proc_fs_fsnext_bin_read,
};

static struct file_operations proc_chmod_fops = {
		.owner = THIS_MODULE,
		.write  = vffs_proc_fs_chmod_write,
};

static struct file_operations proc_getdev_fops = {
		.owner	= THIS_MODULE,
		.read	= vffs_proc_fs_getdev_bin_read,
		.write  = vffs_proc_fs_getdev_write,
};

static struct file_operations proc_rename_fops = {
		.owner	= THIS_MODULE,
		.write  = vffs_proc_fs_rename_write,
};

static struct file_operations proc_mkdir_fops = {
		.owner	= THIS_MODULE,
		.write  = vffs_proc_fs_mkdir_write,
};

static struct file_operations proc_rmdir_fops = {
		.owner	= THIS_MODULE,
		.write  = vffs_proc_fs_rmdir_write,
};


/*
 * Install /procfs/aipc/vffs
 */
static void vffs_procfs_init(void)
{
	struct proc_dir_entry *proc;

	proc_ffs = proc_mkdir("ffs", get_ambarella_proc_dir());
	if (proc_ffs == NULL) {
		pr_err("create vffs dir failed!\n");
		return;
	}

	mutex_init(&g_mlock);

	proc = proc_create_data("map", S_IRUGO | S_IWUSR, proc_ffs, &proc_map_fops, NULL);
	if (proc == NULL) {
		printk("create map proc failed\n");
	}

	proc = proc_create_data("unmap", S_IRUGO | S_IWUSR, proc_ffs, &proc_unmap_fops, NULL);
	if (proc == NULL) {
		printk("create unmap proc failed\n");
	}

	proc = proc_create_data("remove", S_IRUGO | S_IWUSR, proc_ffs, &proc_remove_fops, NULL);
	if (proc == NULL) {
		printk("create remove proc failed\n");
	}

	proc = proc_create_data("fsfirst_bin", S_IRUGO | S_IWUSR, proc_ffs, &proc_fsfirst_bin_fops, NULL);
	if (proc == NULL) {
		printk("create fsfirst_bin proc failed\n");
	}

	proc = proc_create_data("fsnext_bin", S_IRUGO | S_IWUSR, proc_ffs, &proc_fsnext_bin_fops, NULL);
	if (proc == NULL) {
		printk("create fsnext_bin proc failed\n");
	}

	proc = proc_create_data("chmod", S_IRUGO | S_IWUSR, proc_ffs, &proc_chmod_fops, NULL);
	if (proc == NULL) {
		printk("create chmod proc failed\n");
	}

	proc = proc_create_data("getdev_bin", S_IRUGO | S_IWUSR, proc_ffs, &proc_getdev_fops, NULL);
	if (proc == NULL) {
		printk("create getdev_bin proc failed\n");
	}

	proc = proc_create_data("rename", S_IRUGO | S_IWUSR, proc_ffs, &proc_rename_fops, NULL);
	if (proc == NULL) {
		printk("create rename proc failed\n");
	}

	proc = proc_create_data("mkdir", S_IRUGO | S_IWUSR, proc_ffs, &proc_mkdir_fops, NULL);
	if (proc == NULL) {
		printk("create mkdir proc failed\n");
	}

	proc = proc_create_data("rmdir", S_IRUGO | S_IWUSR, proc_ffs, &proc_rmdir_fops, NULL);
	if (proc == NULL) {
		printk("create rmdir proc failed\n");
	}

	proc = proc_create_data("bs", S_IRUGO | S_IWUSR, proc_ffs, &proc_bs_fops, NULL);
	if (proc == NULL) {
		printk("create bs proc failed\n");
	}

	proc = proc_create_data("fstat_bin", S_IRUGO | S_IWUSR, proc_ffs, &proc_fstat_bin_fops, NULL);
	if (proc == NULL) {
		printk("create fstat_bin proc failed\n");
	}

}

/*
 * Uninstall /proc/aipc/vffs
 */
static void vffs_procfs_cleanup(void)
{
	struct proc_ffs_list *pl;
	struct list_head *pos, *q;

	remove_proc_entry("bs", proc_ffs);
	remove_proc_entry("map", proc_ffs);
	remove_proc_entry("unmap", proc_ffs);
	remove_proc_entry("list", proc_ffs);
	remove_proc_entry("create", proc_ffs);
	remove_proc_entry("remove", proc_ffs);
	remove_proc_entry("mkdir", proc_ffs);
	remove_proc_entry("rmdir", proc_ffs);
	remove_proc_entry("fstat_bin", proc_ffs);
	remove_proc_entry("getdev_bin", proc_ffs);
	remove_proc_entry("fsfirst_bin", proc_ffs);
	remove_proc_entry("fsnext_bin", proc_ffs);
	remove_proc_entry("chmod", proc_ffs);
	remove_proc_entry("rename", proc_ffs);

	list_for_each_safe(pos, q, &pl_head) {
		pl = list_entry(pos, struct proc_ffs_list, list);
		vffs_rpmsg_fclose(pl->fp);
		remove_proc_entry(pl->name, proc_ffs);
		list_del(pos);
		kfree(pl);
	}

	remove_proc_entry("vffs", proc_ffs);
}

/*
 * RPMSG channel callback for incoming rpmsg
 */
static int rpmsg_vffs_recv(struct rpmsg_device *rpdev, void *data, int len,
		void *priv, u32 src)
{
	struct vffs_msg *msg = (struct vffs_msg*) data;
	struct vffs_xfr *xfr = (struct vffs_xfr*) msg->xfr;

	if (xfr) {
		xfr->cb(xfr->priv, msg, len);
		xfr_put(xfr);
	}

	return 0;
}

static int rpmsg_vffs_probe(struct rpmsg_device *rpdev)
{
	int ret = 0;
	struct rpmsg_channel_info chinfo;

#if defined(CONFIG_PROC_FS)
	vffs_procfs_init();
#endif
	if (!strcmp(rpdev->id.name, rpdev_name))
		rpdev_vffs = rpdev;

	strncpy(chinfo.name, rpdev->id.name, sizeof(chinfo.name));
	chinfo.src = RPMSG_ADDR_ANY;
	chinfo.dst = rpdev->dst;

	rpmsg_send(rpdev->ept, &chinfo, sizeof(chinfo));

	return ret;
}

static void rpmsg_vffs_remove(struct rpmsg_device *rpdev)
{
}

static struct rpmsg_device_id rpmsg_vffs_id_table[] = {
	{ .name	= rpdev_name, },
	{ },
};
MODULE_DEVICE_TABLE(rpmsg, rpmsg_vffs_id_table);

static struct rpmsg_driver rpmsg_vffs_driver = {
	.drv.name	= KBUILD_MODNAME,
	.drv.owner	= THIS_MODULE,
	.id_table	= rpmsg_vffs_id_table,
	.probe		= rpmsg_vffs_probe,
	.callback	= rpmsg_vffs_recv,
	.remove		= rpmsg_vffs_remove,
};

int __init vffs_rpmsg_init(void)
{
	int i;

	sema_init(&xfr_sem, XFR_ARRAY_SIZE);
	for (i = 0; i < XFR_ARRAY_SIZE; i++) {
		xfr_slot[i].refcnt = 0;
		init_completion(&xfr_slot[i].comp);
	}

	return register_rpmsg_driver(&rpmsg_vffs_driver);
}

void __exit vffs_rpmsg_exit(void)
{
#if defined(CONFIG_PROC_FS)
	vffs_procfs_cleanup();
#endif
	unregister_rpmsg_driver(&rpmsg_vffs_driver);
}

module_init(vffs_rpmsg_init);
module_exit(vffs_rpmsg_exit);

MODULE_DESCRIPTION("RPMSG VFFS Server");
