/*
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/kobject.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>

#define MODULE_NAME "test_oops"
#define DRIVER_NAME MODULE_NAME


static int tst_oops_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static int tst_oops_release(struct inode *inode, struct file *filp)
{
	return 0;
}


static ssize_t tst_oops_read(struct file *fp, char __user *ubuf,
				  size_t cnt, loff_t *ppos)
{
	/* generate an oops */
	int *null = NULL;

	*null = 4;

	return 0;
}

static const struct file_operations tst_oops_fops = {
	.owner		= THIS_MODULE,
	.open		= tst_oops_open,
	.release	= tst_oops_release,
	.read		= tst_oops_read,
};

struct miscdevice misc_dev;

static int __init tst_oops_init(void)
{
	int ret;

	misc_dev.minor = MISC_DYNAMIC_MINOR;
	misc_dev.name = MODULE_NAME;
	misc_dev.fops = &tst_oops_fops;

	ret = misc_register(&misc_dev);
	if (ret < 0)
		goto failed;

	return 0;

failed:
	return ret;
}

static void __exit tst_oops_exit(void)
{
	misc_deregister(&misc_dev);
}

module_init(tst_oops_init);
module_exit(tst_oops_exit);

MODULE_DESCRIPTION("Parrot Oops test Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Parrot SA");
