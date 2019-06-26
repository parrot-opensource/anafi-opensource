/*
 * Remote processor messaging transport
 *
 * Copyright (C) 2016 Ambarella, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/remoteproc.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/virtio_ids.h>
#include <linux/rpmsg.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/irqdomain.h>

#include <plat/remoteproc.h>
#include <plat/ambalink_cfg.h>

#include "remoteproc_internal.h"

#ifdef RPMSG_DEBUG
static AMBA_RPMSG_STATISTIC_s *rpmsg_stat;
extern struct ambalink_shared_memory_layout ambalink_shm_layout;
#endif

struct resource_table	*table;
static int table_size;

static struct resource_table *gen_rsc_table_cortex(int *tablesz)
{
	struct fw_rsc_hdr	        *hdr;
	struct fw_rsc_vdev	        *vdev;
	struct fw_rsc_vdev_vring        *vring;

	*tablesz 		= table_size;

	table->ver              = 1;
	table->num              = 1;
	table->offset[0]        = sizeof(*table) + sizeof(u32) * table->num;

	hdr                     = (void*)table + table->offset[0];
	hdr->type               = RSC_VDEV;

        vdev                    = (void*)&hdr->data;
	vdev->id                = VIRTIO_ID_RPMSG;
	vdev->notifyid          = 124;
	vdev->dfeatures         = 1;
	vdev->config_len	= 0;
	vdev->num_of_vrings     = 2;

	vring                   = (void*)&vdev->vring[0];
	vring->da               = ambalink_shm_layout.vring_c0_to_c1;
	vring->align		= PAGE_SIZE;
	vring->num              = MAX_RPMSG_NUM_BUFS >> 1;
	vring->notifyid		= 111;

	vring                   = (void*)&vdev->vring[1];
	vring->da               = ambalink_shm_layout.vring_c1_to_c0;
	vring->align            = PAGE_SIZE;
	vring->num              = MAX_RPMSG_NUM_BUFS >> 1;
	vring->notifyid		= 112;

	return table;
}

static int ambarella_rproc_dummy_request_firmware(const struct firmware **firmware_p,
		        const char *name, struct device *device)
{
	*firmware_p = kzalloc(sizeof(**firmware_p), GFP_KERNEL);

	return 0;
}

static int ambarella_rproc_dummy_request_firmware_nowait(struct module *module,
                        bool uevent, const char *name, struct device *device,
                        gfp_t gfp, void *context,
                        void (*cont)(const struct firmware *fw, void *context))
{
	cont(NULL, context);

	return 0;
}

static void ambarella_rproc_dummy_release_firmware(const struct firmware *fw)
{
	kfree(fw);
}

static int ambarella_rproc_dummy_load_segments(struct rproc *rproc,
				     const struct firmware *fw)
{
	return 0;
}

static struct resource_table *
ambarella_rproc_dummy_find_rsc_table(struct rproc *rproc,
                                const struct firmware *fw,
                                int *tablesz)
{
	struct ambarella_rproc_pdata *pdata = rproc->priv;

	return pdata->gen_rsc_table(tablesz);
}

static struct resource_table *
ambarella_rproc_dummy_find_loaded_rsc_table(struct rproc *rproc, const struct firmware *fw)
{
        int tablesz;
	struct ambarella_rproc_pdata *pdata = rproc->priv;

	return pdata->gen_rsc_table(&tablesz);
}

const struct rproc_fw_ops rproc_dummy_fw_ops = {
	.request_firmware		= ambarella_rproc_dummy_request_firmware,
	.request_firmware_nowait	= ambarella_rproc_dummy_request_firmware_nowait,
	.release_firmware		= ambarella_rproc_dummy_release_firmware,
	.load				= ambarella_rproc_dummy_load_segments,
	.find_rsc_table			= ambarella_rproc_dummy_find_rsc_table,
	.find_loaded_rsc_table          = ambarella_rproc_dummy_find_loaded_rsc_table,
	.sanity_check			= NULL,
	.get_boot_addr			= NULL
};

static void rpmsg_send_irq(struct regmap *reg_ahb_scr, unsigned long irq)
{
        regmap_write_bits(reg_ahb_scr, AHB_SP_SWI_SET_OFFSET,
                        0x1 << (irq - AXI_SOFT_IRQ(0)),
                        0x1 << (irq - AXI_SOFT_IRQ(0)));
}

static void rpmsg_ack_irq(struct regmap *reg_ahb_scr, unsigned long irq)
{
        regmap_write(reg_ahb_scr, AHB_SP_SWI_CLEAR_OFFSET,
                        0x1 << (irq - AXI_SOFT_IRQ(0)));
}

static void ambarella_rproc_kick(struct rproc *rproc, int vqid)
{
	struct ambarella_rproc_pdata *pdata = rproc->priv;

	if (vqid == 0) {
		rpmsg_send_irq(pdata->reg_ahb_scr, pdata->rvq_tx_irq);
	} else {
		rpmsg_send_irq(pdata->reg_ahb_scr, pdata->svq_tx_irq);
	}
}

static void rproc_svq_worker(struct work_struct *work)
{
	struct ambarella_rproc_pdata *pdata;
	struct rproc *rproc;

	pdata = container_of(work, struct ambarella_rproc_pdata, svq_work);
	rproc = pdata->rproc;

	rproc_vq_interrupt(rproc, 1);
}

static void rproc_rvq_worker(struct work_struct *work)
{
	struct ambarella_rproc_pdata *pdata;
	struct rproc *rproc;
	struct rproc_vring *rvring;

	pdata = container_of(work, struct ambarella_rproc_pdata, rvq_work);
	rproc = pdata->rproc;
	rvring = idr_find(&pdata->rproc->notifyids, 0);

	while (1) {

		if (rproc_vq_interrupt(rproc, 0) == IRQ_HANDLED)
			continue;

		virtqueue_enable_cb(rvring->vq);

		if (rproc_vq_interrupt(rproc, 0) == IRQ_HANDLED) {

			virtqueue_disable_cb(rvring->vq);
			continue;
		}

		break;
	}
}

static irqreturn_t rproc_ambarella_isr(int irq, void *dev_id)
{
	struct ambarella_rproc_pdata *pdata = dev_id;
	struct rproc_vring *rvring;
        struct irq_data *idata = NULL;

	if (irq == pdata->rvq_rx_irq) {
#ifdef RPMSG_DEBUG
		rpmsg_stat->LxRvqIsrCount++;
#endif
                idata = irq_get_irq_data(pdata->rvq_rx_irq);
		rvring = idr_find(&pdata->rproc->notifyids, 0);
		virtqueue_disable_cb(rvring->vq);
		schedule_work(&pdata->rvq_work);
	} else if (irq == pdata->svq_rx_irq) {
	        idata = irq_get_irq_data(pdata->svq_rx_irq);
		schedule_work(&pdata->svq_work);
	} else {
                BUG();
	}

	rpmsg_ack_irq(pdata->reg_ahb_scr, idata->hwirq);

	return IRQ_HANDLED;
}

static int ambarella_rproc_start(struct rproc *rproc)
{
	return 0;
}

static int ambarella_rproc_stop(struct rproc *rproc)
{
	return 0;
}

static struct rproc_ops ambarella_rproc_ops = {
	.start		= ambarella_rproc_start,
	.stop		= ambarella_rproc_stop,
	.kick		= ambarella_rproc_kick,
};

static const struct of_device_id ambarella_rproc_of_match[] = {
	{ .compatible = "ambarella,rproc", },
	{},
};

static struct ambarella_rproc_pdata pdata_cortex = {
	.name           = "c0_and_c1",
	.firmware       = "dummy",
	.ops            = NULL,
	.gen_rsc_table  = gen_rsc_table_cortex,
};

static int ambarella_rproc_probe(struct platform_device *pdev)
{
	struct ambarella_rproc_pdata *pdata;
	struct rproc *rproc;
	int ret;

	/* generate a shared rsc table. */
	table_size = sizeof(struct resource_table) + sizeof(u32) * 1
		     + sizeof(struct fw_rsc_hdr) + sizeof(struct fw_rsc_vdev)
		     + sizeof(struct fw_rsc_vdev_vring) * 2;

	table = kzalloc(table_size, GFP_KERNEL);

	ret = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(64));
	if (ret) {
		dev_err(pdev->dev.parent, "dma_set_coherent_mask: %d\n", ret);
		return ret;
	}

        pdev->dev.platform_data = &pdata_cortex;
        pdata = pdev->dev.platform_data;

        pdata->svq_tx_irq       = VRING_IRQ_C1_TO_C0_KICK;
        pdata->rvq_tx_irq       = VRING_IRQ_C1_TO_C0_ACK;
        pdata->buf_addr_pa      = ambalink_shm_layout.vring_c0_and_c1_buf;

	rproc = rproc_alloc(&pdev->dev, "ambalink_rproc", &ambarella_rproc_ops,
			    pdata->firmware, sizeof(*rproc));
	if (!rproc) {
		ret = -ENOMEM;
		goto free_rproc;
	}

        pdata->svq_rx_irq = platform_get_irq(pdev, 1);
        if (pdata->svq_rx_irq < 0) {
                dev_err(&pdev->dev, "no irq for svq_rx_irq!\n");
                ret = -ENODEV;
                goto free_rproc;
        }
#ifdef RPMSG_DEBUG
	rpmsg_stat = (AMBA_RPMSG_STATISTIC_s *) \
				phys_to_virt(ambalink_shm_layout.rpmsg_profile_addr);
#endif

	ret = request_irq(pdata->svq_rx_irq, rproc_ambarella_isr,
			IRQF_SHARED | IRQF_TRIGGER_HIGH,
			"rproc-svq_rx", pdata);
	if (ret) {
		dev_err(&pdev->dev, "request_irq error: %d\n", ret);
		goto free_rproc;
	}

        pdata->rvq_rx_irq = platform_get_irq(pdev, 0);
        if (pdata->rvq_rx_irq < 0) {
                dev_err(&pdev->dev, "no irq for rvq_rx_irq!\n");
                ret = -ENODEV;
                goto free_rproc;
        }

	ret = request_irq(pdata->rvq_rx_irq, rproc_ambarella_isr,
			IRQF_SHARED | IRQF_TRIGGER_HIGH,
			"rproc-rvq_rx", pdata);
	if (ret) {
		dev_err(&pdev->dev, "request_irq error: %d\n", ret);
		goto free_rproc;
	}

        pdata->reg_ahb_scr = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
                                                        "amb,scr-regmap");
        if (IS_ERR(pdata->reg_ahb_scr)) {
                printk(KERN_ERR "no scr regmap!\n");
                ret = PTR_ERR(pdata->reg_ahb_scr);
        }

	INIT_WORK(&pdata->svq_work, rproc_svq_worker);
	INIT_WORK(&pdata->rvq_work, rproc_rvq_worker);

	mutex_init(&rproc->lock);

	rproc->priv = pdata;
	pdata->rproc = rproc;
	rproc->has_iommu = false;

	platform_set_drvdata(pdev, rproc);

	rproc->fw_ops = &rproc_dummy_fw_ops;
	ret = rproc_add(rproc);
	if (ret)
		goto free_rproc;

	return 0;

free_rproc:
	rproc_put(rproc);
	return ret;
}

static int ambarella_rproc_remove(struct platform_device *pdev)
{
	struct rproc *rproc = platform_get_drvdata(pdev);

	rproc_del(rproc);
	rproc_put(rproc);
	kfree(table);
	return 0;
}

static struct platform_driver ambarella_rproc_driver = {
	.probe = ambarella_rproc_probe,
	.remove = ambarella_rproc_remove,
	.driver = {
	        .name = "ambarella-rproc",
                .of_match_table = ambarella_rproc_of_match,
	},
};

int __init ambarella_rproc_init(void)
{
	return platform_driver_register(&ambarella_rproc_driver);
}

void __exit ambarella_rproc_exit(void)
{
	platform_driver_unregister(&ambarella_rproc_driver);
}

subsys_initcall(ambarella_rproc_init);
module_exit(ambarella_rproc_exit);

//module_platform_driver(ambarella_rproc_driver);

MODULE_DESCRIPTION("Ambarella Remote Processor control driver");
