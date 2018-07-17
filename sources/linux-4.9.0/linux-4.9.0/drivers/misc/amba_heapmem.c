/*
 * amba_heapmem.c
 *
 * History:
 *	2012/04/07 - [Keny Huang] created file
 *
 * Copyright (C) 2007-2012, Ambarella, Inc.
 *
 * All rights reserved. No Part of this file may be reproduced, stored
 * in a retrieval system, or transmitted, in any form, or by any means,
 * electronic, mechanical, photocopying, recording, or otherwise,
 * without the prior consent of Ambarella, Inc.
 *
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <misc/amba_heapmem.h>
#include <plat/ambalink_cfg.h>

#include <linux/types.h>
#include <linux/errno.h>
// #include <linux/ioport.h>
#include <linux/of.h>
// #include <linux/of_platform.h>

//=============================================================
// macro
#if 0 // debug
    #define dbg_trace(str, args...)     printk("@@ %s[#%d]: " str, __func__, __LINE__, ## args)
#else
    #define dbg_trace(str, args...)
#endif

#ifndef pgprot_noncached
#define pgprot_noncached(prot) \
       __pgprot_modify(prot, L_PTE_MT_MASK, L_PTE_MT_UNCACHED)
#endif
//=============================================================
// misc
extern int rpmsg_linkctrl_cmd_get_mem_info(u8 type, void **base, void **phys, u32 *size);

static void *heap_baseaddr = NULL;
static void *heap_physaddr = NULL;
static unsigned int heap_size = 0;

struct amba_heapmem_dev {
    struct miscdevice   *misc_dev;
};

static struct amba_heapmem_dev  amba_heap_dev = {NULL};
static DEFINE_MUTEX(amba_heap_mutex);

//=============================================================
// file_operations
static long amba_heap_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    long ret = 0;
    struct AMBA_HEAPMEM_INFO_s minfo;

    dbg_trace("\n");

    mutex_lock(&amba_heap_mutex);
    switch (cmd) {
        case AMBA_HEAPMEM_GET_INFO:
            if(rpmsg_linkctrl_cmd_get_mem_info(0, &heap_baseaddr, &heap_physaddr, &heap_size) < 0) {
                printk("%s: rpmsg_linkctrl_cmd_get_mem_info() fail !\n", __func__);
                ret = -EINVAL;
                break;
            }
            minfo.base = (unsigned long long)heap_baseaddr;
            minfo.phys = (unsigned long long)heap_physaddr;
            minfo.size = heap_size;

            dbg_trace("x%llx, x%llx, x%x\n", minfo.base, minfo.phys, minfo.size);
            if(copy_to_user((void **)arg, &minfo, sizeof(struct AMBA_HEAPMEM_INFO_s))) {
                ret = -EFAULT;
            }
            break;
        case AMBA_HEAPMEM_ACCESS_TEST:
            {
                unsigned long long test_addr;
                unsigned long long *tptr;

                ret = copy_from_user(&test_addr, (void *)arg, sizeof(unsigned long long));
                if (ret<0) {
                    ret = -EFAULT;
                    break;
                } else {
                    ret = 0;
                }
                tptr = (unsigned long long *)test_addr;
                //printk("%s: write %p as 0xfeedda0b\n",__FUNCTION__, tptr);
                *tptr = 0xfeedda0bULL;
            }
            break;
        default:
            printk("%s: unknown command 0x%08x", __func__, cmd);
            ret = -EINVAL;
            break;
    }
    mutex_unlock(&amba_heap_mutex);
    dbg_trace("\n");
    return ret;
}


static pgprot_t amba_phys_mem_access_prot(struct file *file, unsigned long pfn,
        unsigned long size, pgprot_t vma_prot)
{
    /* Do not need to set as noncached for one CPU! */
#if !defined(CONFIG_AMBALINK_SINGLE_CORE)
    if (file->f_flags & O_DSYNC) {
        printk("amba_phys_mem_access_prot: set as noncached\n");
        return pgprot_noncached(vma_prot);
    }
#endif

    return vma_prot;
}

static int amba_heap_mmap(struct file *filp, struct vm_area_struct *vma)
{
    int rval;
    unsigned long size;
    u64 baseaddr;

    dbg_trace("\n");
    mutex_lock(&amba_heap_mutex);
    if(rpmsg_linkctrl_cmd_get_mem_info(0, &heap_baseaddr, &heap_physaddr, &heap_size) < 0) {
        printk("%s: rpmsg_linkctrl_cmd_get_mem_info() fail !\n", __func__);
        rval = -EINVAL;
        goto Done;
    }

    size = vma->vm_end - vma->vm_start;
    if(size == heap_size) {
        dbg_trace("heap_base= %p, heap_phy_addr= %p, heap_size= x%x\n",
            heap_baseaddr, heap_physaddr, heap_size);

        /* For MMAP, it needs to use physical address directly! */
        baseaddr = (u64)heap_physaddr;
    } else {
        printk("%s: wrong size(%lx)! heap_size= x%x\n",__func__, size, heap_size);
        rval = -EINVAL;
        goto Done;
    }

    vma->vm_page_prot = amba_phys_mem_access_prot(filp, vma->vm_pgoff,
                                                  size, vma->vm_page_prot);

    vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
    vma->vm_pgoff = (baseaddr) >> PAGE_SHIFT;
    if ((rval = remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
                                size, vma->vm_page_prot)) < 0) {
        goto Done;
    }
    rval = 0;

Done:
    mutex_unlock(&amba_heap_mutex);
    dbg_trace("\n");
    return rval;
}

static int amba_heap_open(struct inode *inode, struct file *filp)
{
    dbg_trace("\n");
    return 0;
}

static int amba_heap_release(struct inode *inode, struct file *filp)
{
    dbg_trace("\n");
    return 0;
}

static struct file_operations       amba_heap_fops = {
    .owner          = THIS_MODULE,
    .unlocked_ioctl = amba_heap_ioctl,
    .mmap           = amba_heap_mmap,
    .open           = amba_heap_open,
    .release        = amba_heap_release,
};

//=============================================================
// platform_driver
static struct miscdevice        amba_heapmem_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = "amba_heapmem",
    .fops  = &amba_heap_fops,
};

static int amba_heap_probe(struct platform_device *pdev)
{
    int err = 0;

    dbg_trace("\n");

    if(amba_heap_dev.misc_dev != NULL) {
        dev_err(&pdev->dev, "amba_heapmem already exists. Skip operation!\n");
        return 0;
    }

    platform_set_drvdata(pdev, &amba_heap_dev);

    amba_heap_dev.misc_dev = &amba_heapmem_device;
    err = misc_register(amba_heap_dev.misc_dev);
    if (err) {
        dev_err(&pdev->dev, "failed to misc_register amba_heapmem.\n");
        goto err_fail;
    }

    dbg_trace("Probe %s ok\n", amba_heap_dev.misc_dev->name);
    printk(KERN_INFO "Probe %s ok\n", amba_heap_dev.misc_dev->name);

    return 0;

err_fail:
    misc_deregister(amba_heap_dev.misc_dev);
    amba_heap_dev.misc_dev = NULL;
    return err;
}


static int amba_heap_remove(struct platform_device *pdev)
{
    misc_deregister(amba_heap_dev.misc_dev);
    amba_heap_dev.misc_dev = NULL;
    dbg_trace("\n");
    return 0;
}

#ifdef CONFIG_PM
static int amba_heap_suspend(struct platform_device *pdev, pm_message_t state)
{
    int errorCode = 0;

    dev_dbg(&pdev->dev, "%s exit with %d @ %d\n",
            __func__, errorCode, state.event);
    return errorCode;
}

static int amba_heap_resume(struct platform_device *pdev)
{
    int errorCode = 0;

    dev_dbg(&pdev->dev, "%s exit with %d\n", __func__, errorCode);
    return errorCode;
}
#else
#define amba_dspmem_suspend         NULL
#define amba_dspmem_resume          NULL
#endif

static const struct of_device_id    amba_heapmem_of_match[] = {
    {.compatible = "ambarella,amba_heapmem", },
    {},
};
MODULE_DEVICE_TABLE(of, amba_heapmem_of_match);

static struct platform_driver       amba_heapmem_driver = {
    .probe      = amba_heap_probe,
    .remove     = amba_heap_remove,
    .suspend    = amba_heap_suspend,
    .resume     = amba_heap_resume,
    .driver     = {
        .name	= "amba_heapmem",
        .of_match_table = of_match_ptr(amba_heapmem_of_match),
    },
};

static int __init __amba_heap_init(void)
{
    dbg_trace("\n");
    return platform_driver_register(&amba_heapmem_driver);
}

static void __exit __amba_heap_exit(void)
{
    platform_driver_unregister(&amba_heapmem_driver);
    dbg_trace("\n");
}

module_init(__amba_heap_init);
module_exit(__amba_heap_exit);

MODULE_AUTHOR("Keny Huang <skhuang@ambarella.com>");
MODULE_DESCRIPTION("Ambarella heapmem driver");
MODULE_LICENSE("GPL");

