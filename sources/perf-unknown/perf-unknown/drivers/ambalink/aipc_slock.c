/*
 * arch/arm/plat-ambarella/misc/aipc_slock.c
 *
 * Authors:
 *  Joey Li <jli@ambarella.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * Copyright (C) 2013-2015, Ambarella Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/io.h>

#include <asm/uaccess.h>

#include <plat/ambalink_cfg.h>
#include <linux/aipc/ipc_slock.h>

typedef struct {
	unsigned int    lock;
	char            padding[12];
} aspinlock_t;

typedef struct {
	int size;
	aspinlock_t *lock;
} aspinlock_db;

static aspinlock_db lock_set;
static int lock_inited = 0;

#if 0
static int procfs_spinlock_show(struct seq_file *m, void *v)
{
	int len;

	len = seq_printf(m,
	                 "\n"
	                 "usage: echo id [t] > /proc/ambarella/spinlock\n"
	                 "    id is the spinlock id\n"
	                 "    t is the duration(ms) which spinlock is locked. Default is 0\n"
	                 "\n");

	return len;
}

static ssize_t procfs_spinlock_write(struct file *file,
                                 const char __user *buffer, size_t count, loff_t *data)
{
	char str[128];
	int  id = 0, delay = 0;
	unsigned long flags;

	//memset(str, 0, sizeof(str));
	if (copy_from_user(str, buffer, count)) {
		printk(KERN_ERR "copy_from_user failed, aborting\n");
		return -EFAULT;
	}
	sscanf(str, "%d %d", &id, &delay);
	printk("try spinlock %d, holding for %d ms\n", id, delay);

	aipc_spin_lock_irqsave(id, &flags);
	mdelay(delay);
	aipc_spin_unlock_irqrestore(id, flags);

	printk("done\n");
	return count;
}

static int procfs_spinlock_open(struct inode *inode, struct file *file)
{
	return single_open(file, procfs_spinlock_show, PDE_DATA(inode));
}

static const struct file_operations proc_ipc_slock_fops = {
	.open = procfs_spinlock_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.write = procfs_spinlock_write,
	.release = single_release,
};

static void init_procfs(void)
{
	struct proc_dir_entry *aipc_dev;

	aipc_dev = proc_create_data("spinlock", S_IRUGO | S_IWUSR,
	                            get_ambarella_proc_dir(),
	                            &proc_ipc_slock_fops, NULL);
}
#endif

int aipc_spin_lock_setup(unsigned long addr)
{
	if (lock_inited)
		goto done;

	lock_set.lock = (aspinlock_t *) phys_to_virt(addr);
	lock_set.size = AIPC_SLOCK_SIZE / sizeof(aspinlock_t);

        printk(KERN_NOTICE "%s: aipc_slock@0x%016lx\n", __func__, (unsigned long) lock_set.lock);

	/* Reserve one spinlock space for BCH NAND controller workaround. */
	lock_set.size -= 1;
	//memset(lock_set.lock, 0, lock_set.size);

	lock_inited = 1;

	//printk("%s done\n", __func__);

done:
	return 0;
}
EXPORT_SYMBOL(aipc_spin_lock_setup);

static int aipc_spin_lock_init(void)
{
	aipc_spin_lock_setup(ambalink_shm_layout.aipc_slock_addr);

#if 0
	init_procfs();
#endif

	return 0;
}

static void aipc_spin_lock_exit(void)
{
}

void __aipc_spin_lock(aspinlock_t *lock)
{
	unsigned int tmp;

	asm volatile(
	"1:	ldaxr	%w0, %1\n"
	"	cbnz	%w0, 1b\n"
	"	stxr	%w0, %w2, %1\n"
	"	cbnz	%w0, 1b\n"
	"	nop"
	: "=&r" (tmp), "+Q" (*lock)
	: "r" (0x1)
	: "memory");
}

void __aipc_spin_unlock(aspinlock_t *lock)
{
	asm volatile(
	"	stlr	wzr, %0"
	: "=Q" (*lock) :: "memory");
}

void __aipc_spin_lock_irqsave(void *lock, unsigned long *flags)
{
	local_irq_save(*flags);
	preempt_disable();

        __aipc_spin_lock((aspinlock_t *) lock);
}

void __aipc_spin_unlock_irqrestore(void *lock, unsigned long flags)
{
        __aipc_spin_unlock((aspinlock_t *) lock);

	preempt_enable();
	local_irq_restore(flags);
}

void aipc_spin_lock(int id)
{
	if (!lock_inited)
		aipc_spin_lock_setup(ambalink_shm_layout.aipc_slock_addr);

	if (id < 0 || id >= lock_set.size) {
		printk(KERN_ERR "%s: invalid id %d\n", __FUNCTION__, id);
		return;
	}
	__aipc_spin_lock(&lock_set.lock[id]);
}

void aipc_spin_unlock(int id)
{
	if (id < 0 || id >= lock_set.size) {
		printk(KERN_ERR "%s: invalid id %d\n", __FUNCTION__, id);
		return;
	}
	__aipc_spin_unlock(&lock_set.lock[id]);
}

void aipc_spin_lock_irqsave(int id, unsigned long *flags)
{
	if (!lock_inited)
		aipc_spin_lock_setup(ambalink_shm_layout.aipc_slock_addr);

	if (id < 0 || id >= lock_set.size) {
		printk(KERN_ERR "%s: invalid id %d\n", __FUNCTION__, id);
		return;
	}
	__aipc_spin_lock_irqsave((void *) &lock_set.lock[id], flags);
}

void aipc_spin_unlock_irqrestore(int id, unsigned long flags)
{
	if (id < 0 || id >= lock_set.size) {
		printk(KERN_ERR "%s: invalid id %d\n", __FUNCTION__, id);
		return;
	}
	__aipc_spin_unlock_irqrestore((void *) &lock_set.lock[id], flags);
}

subsys_initcall(aipc_spin_lock_init);
module_exit(aipc_spin_lock_exit);
MODULE_DESCRIPTION("Ambarella IPC spinlock");
