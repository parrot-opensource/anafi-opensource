#include <linux/clk.h>
#include <linux/netdevice.h>
#include <linux/can.h>
#include <linux/can/dev.h>
#include <linux/can/error.h>
#include <linux/can/led.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>

#define DRV_NAME				"ambacan"
#define CAN_CTRL				0x000
#define CAN_TT_CTRL				0x004
#define CAN_RESET				0x008
#define CAN_TQ					0x010
#define CAN_TQ_FD				0x014
#define CAN_TT_TIMER_ENABLE			0x01C
#define CAN_ERR_STATUS				0x020
#define CAN_INT_CTRL				0x028
#define CAN_INT_STATUS				0x02C
#define CAN_INT_RAW				0x030
#define CAN_INT_MASK				0x034
#define CAN_ENABLE				0x038
#define CAN_BUF_CFG_DONE			0x080
#define CAN_MSG_REQUEST				0x084
#define CAN_MSG_BUF_DONE			0x090
#define CAN_MSG_BUG_TYPE			0x09C
#define CAN_MSG_BUF				0x200
#define CAN_MSG_BUF_DATA			0x800

#define CAN_CTRL_LOOPBACK			0x01
#define CAN_CTRL_LISTEN				BIT(2)
#define CAN_CTRL_AUTO_RESPONSE			BIT(3)
#define CAN_CTRL_FD				BIT(31)

#define CAN_STATUS_BUS_OFF			BIT(0)
#define CAN_STATUS_ERR_PASSIVE			BIT(1)
#define CAN_STATUS_ACK_ERR			BIT(2)
#define CAN_STATUS_FORM_ERR			BIT(3)
#define CAN_STATUS_CRC_ERR			BIT(4)
#define CAN_STATUS_STUFF_ERR			BIT(5)
#define CAN_STATUS_BIT_ERR			BIT(6)
#define CAN_STATUS_TIMEOUT			BIT(7)
#define CAN_STATUS_RX_OVERFLOW			BIT(8)
#define CAN_STATUS_TRX_DONE			BIT(9)
#define CAN_STATUS_TIMER_WRAP			BIT(10)
#define CAN_STATUS_WAKE_UP			BIT(11)
#define CAN_STATUS_REPLAY_FAIL			BIT(12)

#define CAN_BUS_ERR				CAN_STATUS_ACK_ERR | CAN_STATUS_FORM_ERR | \
						CAN_STATUS_CRC_ERR | CAN_STATUS_STUFF_ERR | \
						CAN_STATUS_BIT_ERR

#define CAN_ERR_STATUS_ERROR_STATE(x)		(((x) & 0x6000) << 25)
#define CAN_ERR_STATUS_RX_ERR_CNT(x)		(((x) & 0x1f00) << 16)
#define CAN_ERR_STATUS_TX_ERR_CNT(x)		((x) & 0x1f)

#define CAN_ERR_STATUS_IDLE			0
#define CAN_ERR_STATUS_ACTIVE			1
#define CAN_ERR_STATUS_PASSIVE			2
#define CAN_ERR_STATUS_BUF_OFF			3

#define CAN_TQ_TSEG1(x)				(((x) & 0x1f) << 4)
#define CAN_TQ_TSEG2(x)				((x) & 0xf)
#define CAN_TQ_SJW(x)				(((x) & 0xf) << 9)
#define CAN_TQ_PRE(x)				(((x) & 0xff) << 13)
#define CAN_TQ_SYNC				BIT(21)
#define CAN_TQ_DEALY(x)				(((x) & 0xff) << 22)
#define CAN_TQ_3_SAMPLE				BIT(30)

#define CAN_INT_CTRL_SET_INT_THRESHOLD(x)	((x & 0xf))
#define CAN_INT_CTRL_SET_INT_TIMEOUT(x)		(((x) & 0x7ffff) << 8)

#define CAN_MSG_EXT				BIT(29)
#define CAN_MSG_RTR				BIT(7)
#define CAN_MSG_EDL				BIT(6)
#define CAN_MSG_BRS				BIT(5)
#define CAN_MSG_ESI				BIT(4)
#define CAN_MSG_LEN(x)				((x) & 0xf)

#define CAN_BUF_ID(x)				(CAN_MSG_BUF + x*16)
#define CAN_BUF_CTRL(x)				(CAN_MSG_BUF + x*16 + 0x4)
#define CAN_BUF_DATA_FIELD(x, y)		(CAN_MSG_BUF_DATA + x*64 + y*4)

#define TX_BUF_NUM				1
#define TOTAL_BUF_NUM				16

//#define ENABLE_AMBACAN_DEBUG_MSG		1
#ifdef ENABLE_AMBACAN_DEBUG_MSG
#define AMBACAN_DMSG(...)			printk(__VA_ARGS__)
#else
#define AMBACAN_DMSG(...)
#endif

static DEFINE_SPINLOCK(can_lock);
static int msg_buf_occupy[TX_BUF_NUM];

struct ambacan_priv {
	struct can_priv can;	/* must be the first member */
	struct napi_struct napi;
	struct net_device *dev;
	struct device *device;
	void __iomem *regs;
	u32 irqstatus;
	struct clk *clk;
};

static const struct can_bittiming_const ambacan_bittiming_const = {
	.name = DRV_NAME,
	.tseg1_min = 1,
	.tseg1_max = 16,
	.tseg2_min = 1,
	.tseg2_max = 8,
	.sjw_max = 4,
	.brp_min = 1,
	.brp_max = 1024,
	.brp_inc = 1,
};

static const struct can_bittiming_const ambacan_data_bittiming_const = {
	.name = KBUILD_MODNAME,
	.tseg1_min = 2,		/* Time segment 1 = prop_seg + phase_seg1 */
	.tseg1_max = 16,
	.tseg2_min = 1,		/* Time segment 2 = phase_seg2 */
	.tseg2_max = 8,
	.sjw_max = 4,
	.brp_min = 1,
	.brp_max = 32,
	.brp_inc = 1,
};

static int get_avail_buf(void)
{
	int i, found = 0;
	unsigned long flags;

	spin_lock_irqsave(&can_lock, flags);
	for (i=0; i<TX_BUF_NUM; i++) {
		if (msg_buf_occupy[i] == 0) {
			found = 1;
			msg_buf_occupy[i] = 1;
			break;
		}
	}
	spin_unlock_irqrestore(&can_lock, flags);

	/* can't find available buffer. */
	if (found == 0) {
		i = -1;
	}

	return i;
}

static void set_avail_buf(int id)
{
	msg_buf_occupy[id] = 0;
}

static int ambacan_read_frame(struct net_device *dev, int buf_id)
{
	struct net_device_stats *stats = &dev->stats;
	const struct ambacan_priv *priv = netdev_priv(dev);
	/*
	 * We support can 2.0 & fd, but we use struct canfd_frame to handel all the cases.
	 */
	struct canfd_frame *cf;
	struct sk_buff *skb;
	u32 reg_id, ctrl, data;
	int data_size = 0, i, j = 0, ret;

	ctrl = readl_relaxed(priv->regs + CAN_BUF_CTRL(buf_id));
	if (ctrl & CAN_MSG_EDL) {
		/* CAN FD frame */
		skb = alloc_canfd_skb(dev, &cf);
		cf->len = can_dlc2len(ctrl & 0xf);
	} else {
		/* CAN 2.0 frame */
		skb = alloc_can_skb(dev, (struct can_frame **)&cf);
		cf->len = get_can_dlc(ctrl & 0xf);
	}

	if (unlikely(!skb)) {
		stats->rx_dropped++;
		return 0;
	}

	reg_id = readl_relaxed(priv->regs + CAN_BUF_ID(buf_id));
	if (reg_id & CAN_MSG_EXT) {
		cf->can_id = ((reg_id >> 0) & CAN_EFF_MASK) | CAN_EFF_FLAG;
	} else {
		cf->can_id = (reg_id >> 18) & CAN_SFF_MASK;
	}

	if (ctrl & CAN_MSG_EDL) {
		/* CAN FD frame */
		if (ctrl & CAN_MSG_ESI) {
			cf->flags |= CANFD_ESI;
		}
		if (ctrl & CAN_MSG_BRS) {
			cf->flags |= CANFD_BRS;
		}

	} else {
		/* CAN 2.0 frame */
		if (ctrl & CAN_MSG_RTR) {
			cf->can_id |= CAN_RTR_FLAG;
		}
	}

	while (data_size < cf->len) {
		data = readl_relaxed(priv->regs + CAN_BUF_DATA_FIELD(buf_id, j));
		for (i=0; i<4; i++) {
			if (data_size < cf->len) {
				cf->data[data_size] = (u8) ((data >> (i*8)) & 0xff);
				data_size++;
			}
		}
		j++;
	}
	// recv buf done
	writel_relaxed(BIT(buf_id), priv->regs + CAN_BUF_CFG_DONE);

	stats->rx_packets++;
	stats->rx_bytes += cf->len;
	ret = netif_receive_skb(skb);
	if (ret == NET_RX_DROP)
		AMBACAN_DMSG("%s packet is dropped\n", __func__);
	return 1;
}

static enum can_state ambacan_get_state(int cnt)
{
	enum can_state state;

	if (cnt < 96) {
		state = CAN_STATE_ERROR_ACTIVE;
	} else if (cnt < 128) {
		state = CAN_STATE_ERROR_WARNING;
	} else if (cnt < 256) {
		state = CAN_STATE_ERROR_PASSIVE;
	} else if (cnt >= 256) {
		state = CAN_STATE_BUS_OFF;
	}
	return state;
}

static void ambacan_get_bus_err(struct net_device *dev,
		       struct can_frame *cf)
{
	struct ambacan_priv *priv = netdev_priv(dev);
	int rx_errors = 0, tx_errors = 0;
	u32 reg_esr;

	reg_esr = priv->irqstatus;

	cf->can_id |= CAN_ERR_PROT | CAN_ERR_BUSERROR;

	/*
	 * We don't divid bit errors into dominant bit and recessive bit.
	 * We only have one kind of bit error.
	 */
	if (reg_esr & CAN_STATUS_BIT_ERR) {
		netdev_dbg(dev, "BIT_ERR irq\n");
		cf->data[2] |= CAN_ERR_PROT_BIT0 | CAN_ERR_PROT_BIT1;
		tx_errors = 1;
	}

	if (reg_esr & CAN_STATUS_ACK_ERR) {
		netdev_dbg(dev, "ACK_ERR irq\n");
		cf->can_id |= CAN_ERR_ACK;
		cf->data[3] = CAN_ERR_PROT_LOC_ACK;
		tx_errors = 1;
	}
	if (reg_esr & CAN_STATUS_CRC_ERR) {
		netdev_dbg(dev, "CRC_ERR irq\n");
		cf->data[2] |= CAN_ERR_PROT_BIT;
		cf->data[3] = CAN_ERR_PROT_LOC_CRC_SEQ;
		rx_errors = 1;
	}
	if (reg_esr & CAN_STATUS_FORM_ERR) {
		netdev_dbg(dev, "FRM_ERR irq\n");
		cf->data[2] |= CAN_ERR_PROT_FORM;
		rx_errors = 1;
	}
	if (reg_esr & CAN_STATUS_STUFF_ERR) {
		netdev_dbg(dev, "STF_ERR irq\n");
		cf->data[2] |= CAN_ERR_PROT_STUFF;
		rx_errors = 1;
	}

	priv->can.can_stats.bus_error++;
	if (rx_errors)
		dev->stats.rx_errors++;
	if (tx_errors)
		dev->stats.tx_errors++;
}

static int ambacan_poll_bus_err(struct net_device *dev)
{
	struct sk_buff *skb;
	struct can_frame *cf;

	skb = alloc_can_err_skb(dev, &cf);
	if (unlikely(!skb))
		return 0;

	ambacan_get_bus_err(dev, cf);

	dev->stats.rx_packets++;
	dev->stats.rx_bytes += cf->can_dlc;
	netif_receive_skb(skb);

	return 1;
}

static int ambacan_poll_state(struct net_device *dev)
{
	struct ambacan_priv *priv = netdev_priv(dev);
	struct sk_buff *skb;
	struct can_frame *cf;
	enum can_state new_state = 0, rx_state = 0, tx_state = 0;
	u32 err_status, tx_err_cnt, rx_err_cnt;

	err_status = priv->irqstatus;
	if (err_status & (CAN_BUS_ERR)) {
		rx_err_cnt = CAN_ERR_STATUS_RX_ERR_CNT(readl_relaxed(priv->regs + CAN_ERR_STATUS));
		tx_err_cnt = CAN_ERR_STATUS_TX_ERR_CNT(readl_relaxed(priv->regs + CAN_ERR_STATUS));

		rx_state = ambacan_get_state(rx_err_cnt);
		tx_state = ambacan_get_state(tx_err_cnt);
		new_state = max(rx_state, tx_state);

		if (new_state == priv->can.state)
			return 0;

		skb = alloc_can_err_skb(dev, &cf);
		if (unlikely(!skb))
			return 0;

		can_change_state(dev, cf, tx_state, rx_state);

		if (unlikely(new_state == CAN_STATE_BUS_OFF))
			can_bus_off(dev);

		dev->stats.rx_packets++;
		dev->stats.rx_bytes += cf->can_dlc;
		netif_receive_skb(skb);
	}

	return 1;

}

static inline int ambacan_has_and_handle_berr(const struct ambacan_priv *priv)
{
	return (priv->can.ctrlmode & CAN_CTRLMODE_BERR_REPORTING) &&
		(priv->irqstatus & (CAN_BUS_ERR));
}


static int ambacan_poll(struct napi_struct *napi, int quota)
{
	struct net_device *dev = napi->dev;
	const struct ambacan_priv *priv = netdev_priv(dev);
	int work_done = 0, i;
	u32 rx_status;

	rx_status = readl_relaxed(priv->regs + CAN_MSG_BUF_DONE);
	rx_status = rx_status >> 16;

	work_done += ambacan_poll_state(dev);

	for (i=0; i<TOTAL_BUF_NUM; i++) {
		if (rx_status > 0) {
			if (rx_status & 1) {
				work_done += ambacan_read_frame(dev, i);
			}
			rx_status = rx_status >> 1;
		}
	}

	/* report bus errors */
	if (ambacan_has_and_handle_berr(priv) && work_done < quota)
		work_done += ambacan_poll_bus_err(dev);


	if (work_done < quota) {
		napi_complete(napi);
	} else{
		AMBACAN_DMSG("%s recv pkt %d exceeds the quota\n", __func__, work_done);
	}

	return work_done;
}

static irqreturn_t ambacan_irq(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct net_device_stats *stats = &dev->stats;
	struct ambacan_priv *priv = netdev_priv(dev);
	u32 status, rx_status = 0, tx_status = 0;
	int i;

	status = readl_relaxed(priv->regs + CAN_INT_STATUS);
	priv->irqstatus = status;
	if (status) {
		/*
		 * schedule NAPI for:
		 * - receive pkt
		 * - state change
		 * - bus error IRQ and bus error reporting is enabled
		 */
		if (status & CAN_STATUS_TRX_DONE) {
			status = readl_relaxed(priv->regs + CAN_MSG_BUF_DONE);
			rx_status = status >> 16;
			tx_status = status & 0xFFFF;
		}

		if ((rx_status > 0) || (priv->irqstatus & (CAN_BUS_ERR)))
			napi_schedule(&priv->napi);

		/* transmission complete interrupt */
		if (tx_status > 0) {
			for (i=0; i<TX_BUF_NUM; i++) {
				if (tx_status & 1) {
					set_avail_buf(i);
					stats->tx_bytes += can_get_echo_skb(dev, i);
					stats->tx_packets++;
					netif_wake_queue(dev);
				}
				tx_status = tx_status >> 1;
			}
		}
	}

	/* FIFO overflow */
	if (priv->irqstatus & CAN_STATUS_RX_OVERFLOW) {
		dev->stats.rx_over_errors++;
		dev->stats.rx_errors++;
		AMBACAN_DMSG("%s overflow\n", __func__);
	}

	// clear interrupt
	writel_relaxed(0x1fff, priv->regs + CAN_INT_RAW);
	return IRQ_HANDLED;
}

static netdev_tx_t ambacan_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	const struct ambacan_priv *priv = netdev_priv(dev);
	struct canfd_frame *cf = (struct canfd_frame *)skb->data;
	int buf_id, ret, count = 0, i = 0;
	u32 request, request_mask, status_mask;
	u32 msg_id = 0, msg_ctrl = 0;
	u32 data = 0;

	if (can_dropped_invalid_skb(dev, skb))
		return NETDEV_TX_OK;

	// get the grant of the message buffer
	buf_id = get_avail_buf();
	if (buf_id == -1) {
		netdev_err(dev, "can't get available tx buf\n");
		return NETDEV_TX_BUSY;
	}

	request_mask = 1 << (16 + buf_id);
	request = readl_relaxed(priv->regs + CAN_MSG_REQUEST);
	request |= request_mask;
	writel_relaxed(request, priv->regs + CAN_MSG_REQUEST);

	// poll the message buffer request
	request = readl_relaxed(priv->regs + CAN_MSG_REQUEST);
	if ((request & request_mask) == 0) {
		status_mask = 1 << buf_id;
		ret = request & status_mask;
	} else {
		netdev_err(dev, "request tx buf failed\n");
		return NETDEV_TX_BUSY;
	}

	if (ret == 0) {
		netdev_err(dev, "tx buf is busy\n");
		return NETDEV_TX_BUSY;
	}

	netif_stop_queue(dev);
	// configure the message buffer
	// configure the id
	if (cf->can_id & CAN_EFF_FLAG) {
		msg_id = cf->can_id & CAN_EFF_MASK;
		msg_id |= CAN_MSG_EXT;
	} else {
		msg_id = (cf->can_id & CAN_SFF_MASK) << 18;
	}
	writel_relaxed(msg_id, priv->regs + CAN_BUF_ID(buf_id));

	// configure the priority

	// configure contrl settings
	if (can_is_canfd_skb(skb)) {
		/* CAN FD frames */
		msg_ctrl |= CAN_MSG_LEN(can_len2dlc(cf->len));
		// CAN_FD should set EDL bit.
		msg_ctrl |= CAN_MSG_EDL;

		if(cf->flags & CANFD_BRS)
			msg_ctrl |= CAN_MSG_BRS;

		if(cf->flags & CANFD_ESI)
			msg_ctrl |= CAN_MSG_ESI;
	} else {
		/* CAN 2.0 frames */
		msg_ctrl |= CAN_MSG_LEN(cf->len);
		if (cf->can_id & CAN_RTR_FLAG)
			msg_ctrl |= CAN_MSG_RTR;
	}

	writel_relaxed(msg_ctrl, priv->regs + CAN_BUF_CTRL(buf_id));

	// fill the payload
	while (count < cf->len) {
		data = 0;
		for (i=0; i<4; i++) {
			if (count < cf->len) {
				data |= (cf->data[count] << (i*8));
				count++;
			} else {
				break;
			}
		}
		if (data) {
			writel_relaxed(data, priv->regs +
				CAN_BUF_DATA_FIELD(buf_id, (count - 1)/4));
		}
	}

	can_put_echo_skb(skb, dev, buf_id);

	// write data done
	writel_relaxed(BIT(buf_id), priv->regs + CAN_BUF_CFG_DONE);
	return NETDEV_TX_OK;
}

static void ambacan_set_bittiming(struct net_device *dev)
{
	const struct ambacan_priv *priv = netdev_priv(dev);
	const struct can_bittiming *bt = &priv->can.bittiming;
	u32 reg_tq = 0;


	reg_tq |= CAN_TQ_PRE(bt->brp - 1) |
		CAN_TQ_TSEG1(bt->phase_seg1 - 1) |
		CAN_TQ_TSEG2(bt->phase_seg2 - 1) |
		CAN_TQ_SJW(bt->sjw - 1)		 |
		((priv->can.ctrlmode & CAN_CTRLMODE_3_SAMPLES) ? CAN_TQ_3_SAMPLE : 0) ;

	writel_relaxed(reg_tq, priv->regs + CAN_TQ);
}

static void ambacan_set_data_bittiming(struct net_device *dev)
{
	const struct ambacan_priv *priv = netdev_priv(dev);
	const struct can_bittiming *bt = &priv->can.data_bittiming;
	u32 reg_tq = 0;

	reg_tq |= CAN_TQ_PRE(bt->brp - 1) |
		CAN_TQ_TSEG1(bt->phase_seg1 - 1) |
		CAN_TQ_TSEG2(bt->phase_seg2 - 1) |
		CAN_TQ_SJW(bt->sjw - 1);

	writel_relaxed(reg_tq, priv->regs + CAN_TQ_FD);
}

static int ambacan_chip_start(struct net_device *dev)
{
	u32 config, mask, int_ctrl;
	struct ambacan_priv *priv = netdev_priv(dev);

	/* Reset CAN */
	writel_relaxed(1, priv->regs + CAN_RESET);
	writel_relaxed(0, priv->regs + CAN_RESET);

	/* Configuration */
	config = readl_relaxed(priv->regs + CAN_CTRL);
	if (priv->can.ctrlmode & CAN_CTRLMODE_LOOPBACK)
		config |= CAN_CTRL_LOOPBACK; // configure loopback mode
	if (priv->can.ctrlmode & CAN_CTRLMODE_LISTENONLY)
		config |= CAN_CTRL_LISTEN; 	// configure listen mode
	if (priv->can.ctrlmode & CAN_CTRLMODE_FD_NON_ISO)
		config |= CAN_CTRL_FD;		// configure FD mode

	if (priv->can.ctrlmode & CAN_CTRLMODE_FD)
		config |= CAN_CTRL_FD;		// configure FD mode

	writel_relaxed(config, priv->regs + CAN_CTRL);

	/* Configure the number of tx buf */
	writel_relaxed(BIT(0), priv->regs + CAN_MSG_BUG_TYPE);
	writel_relaxed(BIT(0)^0xFFFF, priv->regs + CAN_BUF_CFG_DONE);


	priv->can.state = CAN_STATE_ERROR_ACTIVE;

	/* Configure interrupt settings */
	mask = readl_relaxed(priv->regs + CAN_INT_MASK);
	mask |= CAN_STATUS_BUS_OFF | CAN_STATUS_TRX_DONE | CAN_BUS_ERR | CAN_STATUS_RX_OVERFLOW;
	writel_relaxed(mask, priv->regs + CAN_INT_MASK);

	int_ctrl = readl_relaxed(priv->regs + CAN_INT_CTRL);
	/* set bit 0~3 to 0 to assert interrupt pin every time an interrupt event occurs. */
	/*
	 * set bit 8~31 (itr_timer_th) to 0.
	 * If the interrupt event counter is non-zero,
	 * and interrupt timer > itr_timer_th, assert interrupt pin regardless of acc_itr_num_th
	 */
	int_ctrl = int_ctrl & CAN_INT_CTRL_SET_INT_THRESHOLD(0) & CAN_INT_CTRL_SET_INT_TIMEOUT(0);
	writel_relaxed(int_ctrl, priv->regs + CAN_INT_CTRL);

	ambacan_set_bittiming(dev);
	/* set data bitrate only when FD is enabled. */
	if (priv->can.ctrlmode & CAN_CTRLMODE_FD)
		ambacan_set_data_bittiming(dev);

	// Enable CAN
	writel_relaxed(BIT(0), priv->regs + CAN_ENABLE);
	// disable TT-CAN
	writel_relaxed(0, priv->regs + CAN_TT_TIMER_ENABLE);
	writel_relaxed(0, priv->regs + CAN_TT_CTRL);

	return 0;
}

static int ambacan_open(struct net_device *dev)
{
	struct ambacan_priv *priv = netdev_priv(dev);
	int err;

	err = clk_prepare_enable(priv->clk);
	if (err) {
		netdev_err(dev, "failed to prepare clk err (%d)\n", err);
		goto out_close;
	}

	err = open_candev(dev);
	if (err) {
		netdev_err(dev, "failed to open can device err (%d)\n", err);
		goto out_close;
	}

	err = request_irq(dev->irq, ambacan_irq, IRQF_SHARED, dev->name, dev);
	if (err) {
		netdev_err(dev, "failed to request irq err (%d)\n", err);
		goto out_close;
	}

	/* start chip and queuing */
	err = ambacan_chip_start(dev);
	if (err)
		goto out_free_irq;

	napi_enable(&priv->napi);
	netif_start_queue(dev);

	return 0;

out_free_irq:
	free_irq(dev->irq, dev);
out_close:
	close_candev(dev);

	return err;

}

static int ambacan_close(struct net_device *dev)
{
	struct ambacan_priv *priv = netdev_priv(dev);
	netif_stop_queue(dev);
	napi_disable(&priv->napi);

	free_irq(dev->irq, dev);
	close_candev(dev);

	return 0;
}

static int ambacan_set_mode(struct net_device *dev, enum can_mode mode)
{
	switch (mode) {
	case CAN_MODE_START:
		ambacan_chip_start(dev);
		netif_wake_queue(dev);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static const struct net_device_ops ambacan_netdev_ops = {
	.ndo_open	= ambacan_open,
	.ndo_stop	= ambacan_close,
	.ndo_start_xmit	= ambacan_start_xmit,
	.ndo_change_mtu = can_change_mtu,
};

static int ambarella_can_probe(struct platform_device *pdev)
{
	struct net_device *dev;
	struct ambacan_priv *priv;
	struct resource *mem;
	struct clk *clk;
	void __iomem *regs;
	int err, irq;
	struct pinctrl *pinctrl;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);
	if (!mem || irq <= 0) {
		dev_err(&pdev->dev, "get dev info failed\n");
		return -ENODEV;
	}

	regs = devm_ioremap(&pdev->dev, mem->start, resource_size(mem));
	if (IS_ERR(regs)) {
		dev_err(&pdev->dev, "get register address failed\n");
		return PTR_ERR(regs);
	}

	clk = clk_get(&pdev->dev, "gclk_can");
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "no clock defined\n");
		return -ENODEV;
	}

	pinctrl = devm_pinctrl_get_select_default(&pdev->dev);
	if (IS_ERR(pinctrl)) {
		dev_err(&pdev->dev, "failed to request pinctrl\n");
		return PTR_ERR(pinctrl);
	}

	dev = alloc_candev(sizeof(struct ambacan_priv), TX_BUF_NUM);
	if (!dev) {
		dev_err(&pdev->dev, "allocate can dev failed\n");
		return -ENOMEM;
	}

	dev->netdev_ops = &ambacan_netdev_ops;
	dev->irq = irq;

	priv = netdev_priv(dev);
	priv->can.clock.freq = clk_get_rate(clk);
	priv->regs = regs;
	priv->device = &pdev->dev;
	priv->can.bittiming_const = &ambacan_bittiming_const;
	priv->can.data_bittiming_const = &ambacan_data_bittiming_const;
	priv->can.do_set_mode = ambacan_set_mode;
	priv->can.ctrlmode_supported = CAN_CTRLMODE_LOOPBACK | CAN_CTRLMODE_3_SAMPLES |
		CAN_CTRLMODE_LISTENONLY | CAN_CTRLMODE_BERR_REPORTING |
		CAN_CTRLMODE_FD_NON_ISO | CAN_CTRLMODE_FD;
	priv->clk = clk;

	netif_napi_add(dev, &priv->napi, ambacan_poll, TOTAL_BUF_NUM - TX_BUF_NUM);

	platform_set_drvdata(pdev, dev);
	SET_NETDEV_DEV(dev, &pdev->dev);

	err = register_candev(dev);
	if (err) {
		dev_err(&pdev->dev, "registering failed (err=%d)\n", err);
		goto exit_candev_free;
	}
	dev_info(&pdev->dev, "%s device registered\n", __func__);
	return 0;

exit_candev_free:
	free_candev(dev);
	return err;
}

static int ambarella_can_remove(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	struct ambacan_priv *priv = netdev_priv(dev);

	unregister_candev(dev);
	netif_napi_del(&priv->napi);
	free_candev(dev);

	return 0;
}

static const struct of_device_id ambarella_can_dt_ids[] = {
	{.compatible = "ambarella,can", },
	{},
};
MODULE_DEVICE_TABLE(of, ambarella_can_dt_ids);

static struct platform_driver ambarella_can_driver = {
	.probe = ambarella_can_probe,
	.remove = ambarella_can_remove,
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = of_match_ptr(ambarella_can_dt_ids),
	},
};

module_platform_driver(ambarella_can_driver);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("CAN port driver for ambarella can based chip");
