#include <linux/module.h>
#include <linux/init.h>

#include <linux/kernel.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/mm.h>

MODULE_AUTHOR("Joey Li");
MODULE_LICENSE("GPL");

#define DEVICE_NAME "ppm"

static unsigned int     ppm_major;
static struct class*    ppm_class;
static struct device*   ppm_device;

static int ppm_mmap(struct file *filp, struct vm_area_struct *vma)
{
	int rval = 0;

	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	rval = remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff, 
                        vma->vm_end - vma->vm_start, vma->vm_page_prot);
	if (rval) {
		printk("PPM: mmap failed with error %d.\n", rval);
		return rval;
	}
        
        printk("PPM: remap [%X---%X]\n", vma->vm_start, vma->vm_end);
	return rval;
}

static const struct file_operations ppm_fops = {
        .owner = THIS_MODULE,
        .mmap = ppm_mmap,
};

static int  __init ppm_init(void)
{
        ppm_major = register_chrdev(0, DEVICE_NAME, &ppm_fops);
        if (ppm_major < 0) {
                printk("PPM: failed to register device %d.\n", ppm_major);
                return ppm_major;
        }

        ppm_class = class_create(THIS_MODULE, DEVICE_NAME);
        if (IS_ERR(ppm_class)) {
                unregister_chrdev(ppm_major, DEVICE_NAME);
                printk("PPM: failed to create class.\n");
                return PTR_ERR(ppm_class);
        }

        ppm_device = device_create(ppm_class, NULL, MKDEV(ppm_major, 0), 
                                NULL, DEVICE_NAME);
        if (IS_ERR(ppm_device)) {
                class_destroy(ppm_class);
                unregister_chrdev(ppm_major, DEVICE_NAME);
                printk("PPM: falied to create device.\n");
                return PTR_ERR(ppm_device);
        }

        printk("PPM: init with major = %d.\n", ppm_major);
        return 0;
}

static void __exit ppm_exit(void)
{
        device_destroy(ppm_class, MKDEV(ppm_major, 0));
        class_destroy(ppm_class);
        unregister_chrdev(ppm_major, DEVICE_NAME);
        printk("PPM exit\n");
        return;
}

module_init(ppm_init);
module_exit(ppm_exit);
