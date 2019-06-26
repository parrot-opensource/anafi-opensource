/*
 * Demonstrate a trivial filesystem using libfs.
 *
 * Copyright 2002, 2003 Jonathan Corbet <corbet@lwn.net>
 * This file may be redistributed under the terms of the GNU GPL.
 *
 * Chances are that this code will crash your system, delete your
 * nethack high scores, and set your disk drives on fire.  You have
 * been warned.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/statfs.h>
#include <linux/backing-dev.h>

#include "ambafs.h"

#define AMBAFS_MAGIC 0x414D4241



static int ambafs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	int tmp[128];
	struct ambafs_msg  *msg  = (struct ambafs_msg  *)tmp;

	buf->f_type = dentry->d_sb->s_magic;
	buf->f_namelen = NAME_MAX;

	msg->cmd = AMBAFS_CMD_VOLSIZE;
	strcpy((char*)msg->parameter, dentry->d_sb->s_fs_info);
	ambafs_rpmsg_exec(msg, strlen((char*)msg->parameter)+1);

	if (msg->flag == 0) {
		buf->f_bsize = msg->parameter[2];
		buf->f_blocks = msg->parameter[0];
		buf->f_bavail = buf->f_bfree = msg->parameter[1];
	} else {
		buf->f_bsize = buf->f_bavail = buf->f_bfree = buf->f_blocks = 0;
	}

	return 0;
}

static struct super_operations ambafs_super_ops = {
	.statfs		= ambafs_statfs,
	.drop_inode	= generic_delete_inode,
};

struct backing_dev_info ambafs_bdi = {
	.name           = "ambafs_bdi",
	.ra_pages       = 32,
	.capabilities   = 0,
};

/*
 * fill a superblock for mounting
 */
static int ambafs_fill_super (struct super_block *sb, void *data, int silent)
{
	struct inode *root;
	struct dentry *d_root;

	sb->s_blocksize = PAGE_SIZE;
	sb->s_blocksize_bits = PAGE_SHIFT;
	sb->s_magic = AMBAFS_MAGIC;
	sb->s_op = &ambafs_super_ops;
	sb->s_d_op = &ambafs_dentry_ops;
	sb->s_bdi = &ambafs_bdi;

	/* make root inode and dentry */
	root = new_inode(sb);
	if (! root)
		goto out;
	root->i_mode = S_IFDIR | 0755;
	root->i_uid = GLOBAL_ROOT_UID;
	root->i_gid = GLOBAL_ROOT_GID;
	root->i_blocks = 0;
	root->i_atime = root->i_mtime = root->i_ctime = CURRENT_TIME;
	root->i_ino = iunique(sb, AMBAFS_INO_MAX_RESERVED);
	root->i_op = &ambafs_dir_inode_ops;
	root->i_fop = &ambafs_dir_ops;
	root->i_sb->s_maxbytes = 0;

	d_root = d_make_root(root);
	if (! d_root)
		goto out_iput;
	sb->s_root = d_root;
	root->i_private = d_root;

	return 0;

out_iput:
	iput(root);
out:
	return -ENOMEM;
}

/*
 * mount the ambafs against @devname on remote core
 */
static struct dentry *ambafs_mount(struct file_system_type *fst,
		int flags, const char *devname, void *data)
{
	int buf[128];
	struct ambafs_msg  *msg  = (struct ambafs_msg  *)buf;
	struct dentry *d_root;
	struct super_block *sb;
	char *path;
	int len = strlen(devname);

	path = kmalloc(len+2, GFP_KERNEL);
	strcpy(path, devname);
	if (len == 1) {
		strcat(path, ":");
	} else if (path[len-1] == '/') {
		path[len-1] = 0;
	}

	msg->cmd = AMBAFS_CMD_MOUNT;
	strcpy((char*)msg->parameter, path);
	strcat((char*)msg->parameter, "/*");
	ambafs_rpmsg_exec(msg, strlen((char*)msg->parameter)+1);
	if (msg->flag == 0) {
		kfree(path);
		return ERR_PTR(-ENODEV);
	}

	d_root = mount_nodev(fst, flags, data, ambafs_fill_super);
	sb = d_root->d_sb;
	sb->s_fs_info = path;

	AMBAFS_DMSG("%s %s\n", __FUNCTION__, path);

	return d_root;
}

/*
 * umount the ambafs
 */
static void ambafs_umount(struct super_block *sb)
{
	void *fs_info = sb->s_fs_info;

	AMBAFS_DMSG("%s\n", __FUNCTION__);

	generic_shutdown_super(sb);
	kfree(fs_info);
}

static struct file_system_type ambafs_type = {
	.owner 		= THIS_MODULE,
	.name		= "ambafs",
	.mount		= ambafs_mount,
	.kill_sb	= ambafs_umount,
};

static int __init ambafs_init(void)
{
	int err;

	err = bdi_init(&ambafs_bdi);
	if (!err)
		err = bdi_register(&ambafs_bdi, NULL, "%s", ambafs_bdi.name);

	if (err) {
		bdi_destroy(&ambafs_bdi);

		return err;
	}

	ambafs_rpmsg_init();

	err = register_filesystem(&ambafs_type);

	return err;
}

static void __exit ambafs_exit(void)
{
	bdi_destroy(&ambafs_bdi);

	ambafs_rpmsg_exit();
	unregister_filesystem(&ambafs_type);
}

module_init(ambafs_init);
module_exit(ambafs_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Joey Li");
