/*
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <plat/remoteproc.h>
#include <plat/remoteproc_cfg.h>

#ifdef RPMSG_DEBUG
extern AMBA_RPMSG_PROFILE_s *svq_profile, *rvq_profile;
extern AMBA_RPMSG_STATISTIC_s *rpmsg_stat;

struct aipc_timer_dev {
	void __iomem *reg;
};

static struct aipc_timer_dev timer;
#endif

unsigned int read_aipc_timer(void)
{
#ifdef RPMSG_DEBUG
	return readl_relaxed(timer.reg);
#else
	return 0;
#endif
}

#ifdef RPMSG_DEBUG
/* The counter is decreasing, so start will large than end normally.*/
static unsigned int calc_timer_diff(unsigned int start, unsigned int end)
{
	unsigned int diff;
	if(end <= start) {
		diff = start - end;
	}
	else{
		diff = 0xFFFFFFFF - end + 1 + start;
	}
	return diff;
}
#endif

unsigned int to_get_svq_buf_profile(void)
{
	unsigned int to_get_buf = 0;
#ifdef RPMSG_DEBUG
	unsigned int diff;
	to_get_buf = read_aipc_timer();
	/* calculate rpmsg injection rate */
	if( rpmsg_stat->LxLastInjectTime != 0 ){
		diff = calc_timer_diff(rpmsg_stat->LxLastInjectTime, to_get_buf);
	}
	else{
		diff = 0;
	}
	rpmsg_stat->LxTotalInjectTime += diff;
	rpmsg_stat->LxLastInjectTime = to_get_buf;
#endif
	return to_get_buf;
}

void get_svq_buf_done_profile(unsigned int to_get_buf, int idx)
{
#ifdef RPMSG_DEBUG
	unsigned int get_buf;
#endif
	if(idx < 0) {
		return;
	}

#ifdef RPMSG_DEBUG
	get_buf = read_aipc_timer();
	svq_profile[idx].ToGetSvqBuffer = to_get_buf;
	svq_profile[idx].GetSvqBuffer = get_buf;
//	printk("idx %d to_get_svq_buf %u\n", idx, svq_profile[idx].ToGetSvqBuffer);
#endif
}

void lnx_response_profile(unsigned int to_get_buf, int idx)
{
#ifdef RPMSG_DEBUG
	unsigned int diff;
#endif
	if(idx < 0) {
		return;
	}
#ifdef RPMSG_DEBUG

	diff = calc_timer_diff(rvq_profile[idx].SvqToSendInterrupt, to_get_buf);

	rpmsg_stat->LxResponseTime += diff;
	if(diff > rpmsg_stat->MaxLxResponseTime){
		rpmsg_stat->MaxLxResponseTime = diff;
	}
#endif
}

unsigned int finish_rpmsg_profile(unsigned int to_get_buf, unsigned int to_recv_data, int idx)
{
	unsigned int ret = 0;
#ifdef RPMSG_DEBUG
	unsigned int recv_data_done;
	unsigned int diff;

	recv_data_done = read_aipc_timer();
	diff = calc_timer_diff(to_recv_data, recv_data_done);
	rpmsg_stat->LxRecvCallBackTime += diff;
	if(diff > rpmsg_stat->MaxLxRecvCBTime){
		rpmsg_stat->MaxLxRecvCBTime = diff;
	}
	if(diff < rpmsg_stat->MinLxRecvCBTime){
		rpmsg_stat->MinLxRecvCBTime = diff;
	}

	diff = calc_timer_diff(rvq_profile[idx].ToGetSvqBuffer, rvq_profile[idx].SvqToSendInterrupt);
	rpmsg_stat->TxSendRpmsgTime += diff;

	diff = calc_timer_diff(to_get_buf, to_recv_data);
	rpmsg_stat->LxRecvRpmsgTime += diff;

	diff = calc_timer_diff(rvq_profile[idx].ToGetSvqBuffer, to_recv_data);
	rpmsg_stat->TxToLxRpmsgTime += diff;
	if(diff > rpmsg_stat->MaxTxToLxRpmsgTime){
		rpmsg_stat->MaxTxToLxRpmsgTime = diff;
	}
	if(diff < rpmsg_stat->MinTxToLxRpmsgTime){
		rpmsg_stat->MinTxToLxRpmsgTime = diff;
	}
	ret = read_aipc_timer();
#endif
	return ret;
}

#ifdef RPMSG_DEBUG
static const struct of_device_id aipc_timer_dt_ids[] = {
	{.compatible = "ambarella,timer19", },
	{},
};
MODULE_DEVICE_TABLE(of, aipc_profile_dt_ids);

static void __init aipc_timer_init(struct device_node *np)
{
	timer.reg = of_iomap(np, 0);
}
OF_DECLARE_1(aipc_timer, ambarella_ipc_timer, "ambarella,timer19", aipc_timer_init);
#endif
