/*
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rpmsg.h>
#include <linux/err.h>
#include <linux/remoteproc.h>
#include <linux/kobject.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/spit.h>

#define MODULE_NAME "spit_ctrl"

#define SPIT_CTRL_WAIT_TIMEOUT (1*HZ)

struct spit_ctrl_devdata {
	struct device		*dev;
	struct mutex		 lock;
	enum spit_control	 control;
	/* ThreadX rpmsg sync */
	wait_queue_head_t	 ctrl_wait;
	unsigned long		 wait_flags;
	struct spit_rpmsg_control resp[SPIT_RPMSG_CONTROL_TYPE_RESP_COUNT];
	struct mutex		 ctrl_lock[SPIT_RPMSG_CONTROL_TYPE_RESP_COUNT];
	/* File entry */
	struct miscdevice	 misc_dev;
	char			 devname[32];
	atomic_t		 opened;
};

static struct spit_ctrl_devdata *spit_ctrl_devs;
static struct rpmsg_device *rp_device;

static const char *spit_error_to_string(enum spit_error err)
{
	static DECLARE_SPIT_ERROR_STRING(error_str);
	int i;

	for (i = 0; i < ARRAY_SIZE(error_str); i++)
		if (error_str[i].err == err)
			return error_str[i].str;

	return "unknown error";
}

static struct spit_ctrl_devdata *spit_ctrl_get_devdata(int control)
{
	if (control < 0 || control >= SPIT_CONTROL_COUNT)
		return NULL;

	return &spit_ctrl_devs[control];
}

static struct spit_ctrl_devdata *spit_ctrl_get_devdata_by_minor(int minor)
{
	int i;

	for (i = 0; i < SPIT_CONTROL_COUNT; i++) {
		if (spit_ctrl_devs[i].misc_dev.minor == minor)
			return &spit_ctrl_devs[i];
	}

	return NULL;
}

static int spit_ctrl_rpmsg_cb(struct rpmsg_device *rpdev, void *data, int len,
			      void *priv, u32 src)
{
	struct spit_ctrl_devdata *spit_ctrl_devdata;
	struct spit_rpmsg_control *msg = data;
	unsigned long flag;
	int idx;

	if (len != sizeof(struct spit_rpmsg_control)) {
		dev_err(&rpdev->dev, "received message with invalid length\n");
		return -EINVAL;
	}

	/* Get spit control data device data */
	spit_ctrl_devdata = spit_ctrl_get_devdata(msg->control);
	if (!spit_ctrl_devdata)
		return -ENODEV;

	if (SPIT_RPMSG_CONTROL_IS_RESP(msg->type)) {
		/* Get response index (from 0 to N) */
		idx = SPIT_RPMSG_CONTROL_RESP_IDX(msg->type);
		flag = (1 << idx);

		/* Copy response and clear wait flag */
		spit_ctrl_devdata->resp[idx] = *msg;
		mutex_lock(&spit_ctrl_devdata->lock);
		spit_ctrl_devdata->wait_flags &= ~flag;
		mutex_unlock(&spit_ctrl_devdata->lock);

		/* Wake up IOCTL */
		wake_up(&spit_ctrl_devdata->ctrl_wait);
	} else {
		dev_warn(&rpdev->dev, "Received unknown message");
	}

	return 0;
}

static int spit_ctrl_rpmsg_probe(struct rpmsg_device *rpdev)
{
	struct rpmsg_channel_info chinfo;

	rp_device = rpdev;

	strncpy(chinfo.name, rpdev->id.name, sizeof(chinfo.name));
	chinfo.name[sizeof(chinfo.name)-1] = 0;
	chinfo.src = RPMSG_ADDR_ANY;
	chinfo.dst = rpdev->dst;

	rpmsg_send(rpdev->ept, &chinfo, sizeof(chinfo));

	return 0;
}

static void spit_ctrl_rpmsg_remove(struct rpmsg_device *rpdev)
{
}

static struct rpmsg_device_id spit_ctrl_rpmsg_id_table[] = {
	{ .name	= SPIT_RPMSG_CONTROL_CHANNEL, },
};
MODULE_DEVICE_TABLE(rpmsg, spit_ctrl_rpmsg_id_table);

static struct rpmsg_driver spit_ctrl_rpmsg_driver = {
	.drv	= {
		.name	= KBUILD_MODNAME,
		.owner	= THIS_MODULE,
	},
	.id_table	= spit_ctrl_rpmsg_id_table,
	.probe		= spit_ctrl_rpmsg_probe,
	.callback	= spit_ctrl_rpmsg_cb,
	.remove		= spit_ctrl_rpmsg_remove,
};

static int spit_ctrl_open(struct inode *inode, struct file *filp)
{
	struct spit_ctrl_devdata *spit_ctrl_devdata;
	int minor;

	minor = iminor(inode);

	spit_ctrl_devdata = spit_ctrl_get_devdata_by_minor(minor);
	if (!spit_ctrl_devdata)
		return -ENOENT;

	filp->private_data = (void *) spit_ctrl_devdata;
	atomic_inc(&spit_ctrl_devdata->opened);

	return 0;
}

static int spit_ctrl_release(struct inode *inode, struct file *filp)
{
	struct spit_ctrl_devdata *spit_ctrl_devdata =
		(struct spit_ctrl_devdata *) filp->private_data;

	atomic_dec(&spit_ctrl_devdata->opened);

	return 0;
}

static int spit_send_request(struct spit_ctrl_devdata *spit_ctrl_devdata,
			     unsigned long data, size_t size,
			     enum spit_rpmsg_control_type type,
			     struct spit_rpmsg_control **resp)
{
	struct spit_rpmsg_control rpmsg;
	int idx = SPIT_RPMSG_CONTROL_REQ_IDX(type);
	unsigned long flag = (1 << idx);
	int ret;

	/* Setup RPMSG */
	rpmsg.control = spit_ctrl_devdata->control;
	rpmsg.type = type;

	/* Copy data */
	if ((data && !size) || (!data && size))
		return -EINVAL;
	if (data && copy_from_user(&rpmsg.data, (void __user *)data, size))
		return -EFAULT;


	mutex_lock(&spit_ctrl_devdata->ctrl_lock[idx]);

	/* Setup wait flag */
	mutex_lock(&spit_ctrl_devdata->lock);
	spit_ctrl_devdata->wait_flags |= flag;
	mutex_unlock(&spit_ctrl_devdata->lock);

	/* Send request to ThreadX */
	rpmsg_send(rp_device->ept, &rpmsg, sizeof(struct spit_rpmsg_control));

	/* Wait for response from ThreadX */
	ret = wait_event_interruptible_timeout(spit_ctrl_devdata->ctrl_wait,
					!(spit_ctrl_devdata->wait_flags & flag),
					SPIT_CTRL_WAIT_TIMEOUT);

	/* Timeout or error */
	if (ret <= 0) {
		/* Clear waiting flag */
		mutex_lock(&spit_ctrl_devdata->lock);
		spit_ctrl_devdata->wait_flags &= ~flag;
		mutex_unlock(&spit_ctrl_devdata->lock);
		dev_err(spit_ctrl_devdata->dev, "request 0x%04x failed: %s\n",
			type, ret ? "rpmsg control channel error" : "timeout");

		mutex_unlock(&spit_ctrl_devdata->ctrl_lock[idx]);
		return ret == 0 ? -ETIMEDOUT : -ERESTARTSYS;
	}

	/* Check error status */
	ret = spit_ctrl_devdata->resp[idx].error;
	if (ret != SPIT_ERROR_NONE) {
		dev_err(spit_ctrl_devdata->dev, "request 0x%04x failed: %s\n",
			type, spit_error_to_string(ret));
		mutex_unlock(&spit_ctrl_devdata->ctrl_lock[idx]);
		return ret > 0 ? -ret : ret;
	}

	/* Set response */
	*resp = &spit_ctrl_devdata->resp[idx];

	mutex_unlock(&spit_ctrl_devdata->ctrl_lock[idx]);
	return 0;
}

static long spit_ctrl_ioctl(struct file *filp, unsigned int req,
			    unsigned long arg)
{
	struct spit_ctrl_devdata *spit_ctrl_devdata =
		(struct spit_ctrl_devdata *) filp->private_data;
	struct spit_rpmsg_control *resp;
	int ret;

	if (!rp_device)
		return -EAGAIN;

	switch (req) {
	case SPIT_CTRL_GET_CAPABILITIES:
		/* Send request and wait capabilities */
		ret = spit_send_request(spit_ctrl_devdata, 0, 0,
				   SPIT_RPMSG_CONTROL_TYPE_REQ_GET_CAPABILITIES,
				   &resp);
		if (ret)
			return ret;

		/* Copy capabilities */
		if (copy_to_user((void __user *)arg, &resp->caps,
				   sizeof(struct spit_control_caps))) {
			return -EFAULT;
		}
		break;

	case SPIT_CTRL_GET_MODE:
		/* Send request and wait mode */
		ret = spit_send_request(spit_ctrl_devdata, arg,
					sizeof(struct spit_mode),
					SPIT_RPMSG_CONTROL_TYPE_REQ_GET_MODE,
					&resp);
		if (ret)
			return ret;

		/* Copy mode data */
		if (copy_to_user((void __user *)arg, &resp->mode,
				   sizeof(struct spit_mode))) {
			return -EFAULT;
		}
		break;

	case SPIT_CTRL_GET_MODE_FPS:
		/* Send request and wait fps mode */
		ret = spit_send_request(spit_ctrl_devdata, arg,
				       sizeof(struct spit_mode_fps),
				       SPIT_RPMSG_CONTROL_TYPE_REQ_GET_MODE_FPS,
				       &resp);
		if (ret)
			return ret;

		/* Copy fps mode data */
		if (copy_to_user((void __user *)arg, &resp->mode_fps,
				   sizeof(struct spit_mode_fps))) {
			return -EFAULT;
		}
		break;

	case SPIT_CTRL_GET_DSP_MODE:
	case SPIT_CTRL_SET_DSP_MODE:
		/* Send request and wait dsp mode switch */
		ret = spit_send_request(spit_ctrl_devdata, arg,
				      sizeof(enum spit_dsp_mode),
				      req == SPIT_CTRL_GET_DSP_MODE ?
				      SPIT_RPMSG_CONTROL_TYPE_REQ_GET_DSP_MODE :
				      SPIT_RPMSG_CONTROL_TYPE_REQ_SET_DSP_MODE,
				      &resp);
		if (ret)
			return ret;

		/* Copy applied DSP mode */
		if (copy_to_user((void __user *)arg,
				 &resp->dsp_mode, sizeof(enum spit_dsp_mode))) {
			return -EFAULT;
		}
		break;

	case SPIT_CTRL_GET_CONFIGURATION:
	case SPIT_CTRL_SET_CONFIGURATION:
		/* Send request and wait new configuration */
		ret = spit_send_request(spit_ctrl_devdata, arg,
				 sizeof(struct spit_control_conf),
				 req == SPIT_CTRL_GET_CONFIGURATION ?
				 SPIT_RPMSG_CONTROL_TYPE_REQ_GET_CONFIGURATION :
				 SPIT_RPMSG_CONTROL_TYPE_REQ_SET_CONFIGURATION,
				 &resp);
		if (ret)
			return ret;

		/* Copy applied configuration */
		if (copy_to_user((void __user *)arg,
				 &resp->conf,
				 sizeof(struct spit_control_conf))) {
			return -EFAULT;
		}
		break;

	case SPIT_CTRL_GET_AE_INFO:
	case SPIT_CTRL_SET_AE_INFO:
		/* Send AE info request and wait data */
		ret = spit_send_request(spit_ctrl_devdata, arg,
				       sizeof(struct spit_ae_info),
				       req == SPIT_CTRL_GET_AE_INFO ?
				       SPIT_RPMSG_CONTROL_TYPE_REQ_GET_AE_INFO :
				       SPIT_RPMSG_CONTROL_TYPE_REQ_SET_AE_INFO,
				       &resp);
		if (ret)
			return ret;

		/* Copy AE info */
		if (copy_to_user((void __user *)arg,
				 &resp->ae_info, sizeof(struct spit_ae_info))) {
			return -EFAULT;
		}
		break;

	case SPIT_CTRL_GET_AE_EV:
	case SPIT_CTRL_SET_AE_EV:
		/* Send AE exposure value request and wait data */
		ret = spit_send_request(spit_ctrl_devdata, arg,
					sizeof(struct spit_ae_ev),
					req == SPIT_CTRL_GET_AE_EV ?
					SPIT_RPMSG_CONTROL_TYPE_REQ_GET_AE_EV :
					SPIT_RPMSG_CONTROL_TYPE_REQ_SET_AE_EV,
					&resp);
		if (ret)
			return ret;

		/* Copy AE exposure value */
		if (copy_to_user((void __user *)arg,
				 &resp->ae_ev, sizeof(struct spit_ae_ev))) {
			return -EFAULT;
		}
		break;

	case SPIT_CTRL_GET_AWB_INFO:
	case SPIT_CTRL_SET_AWB_INFO:
		/* Send AWB info request and wait data */
		ret = spit_send_request(spit_ctrl_devdata, arg,
				      sizeof(struct spit_awb_info),
				      req == SPIT_CTRL_GET_AWB_INFO ?
				      SPIT_RPMSG_CONTROL_TYPE_REQ_GET_AWB_INFO :
				      SPIT_RPMSG_CONTROL_TYPE_REQ_SET_AWB_INFO,
				      &resp);
		if (ret)
			return ret;

		/* Copy AWB info */
		if (copy_to_user((void __user *)arg,
				 &resp->awb_info,
				 sizeof(struct spit_awb_info))) {
			return -EFAULT;
		}
		break;

	case SPIT_CTRL_GET_IMG_SETTINGS:
	case SPIT_CTRL_SET_IMG_SETTINGS:
		/* Send image settings request and wait data */
		ret = spit_send_request(spit_ctrl_devdata, arg,
				sizeof(struct spit_img_settings),
				req == SPIT_CTRL_GET_IMG_SETTINGS ?
				SPIT_RPMSG_CONTROL_TYPE_REQ_GET_IMAGE_SETTINGS :
				SPIT_RPMSG_CONTROL_TYPE_REQ_SET_IMAGE_SETTINGS,
				&resp);
		if (ret)
			return ret;

		/* Copy image settings */
		if (copy_to_user((void __user *)arg,
				 &resp->img_settings,
				 sizeof(struct spit_img_settings))) {
			return -EFAULT;
		}
		break;

	case SPIT_CTRL_GET_FLICKER_MODE:
	case SPIT_CTRL_SET_FLICKER_MODE:
		/* Send flicker mode request and wait data */
		ret = spit_send_request(spit_ctrl_devdata, arg,
				  sizeof(enum spit_flicker_mode),
				  req == SPIT_CTRL_GET_FLICKER_MODE ?
				  SPIT_RPMSG_CONTROL_TYPE_REQ_GET_FLICKER_MODE :
				  SPIT_RPMSG_CONTROL_TYPE_REQ_SET_FLICKER_MODE,
				  &resp);
		if (ret)
			return ret;

		/* Copy flicker mode */
		if (copy_to_user((void __user *)arg,
				 &resp->flicker_mode,
				 sizeof(enum spit_flicker_mode))) {
			return -EFAULT;
		}
		break;

	case SPIT_CTRL_GET_DEWARP_CONFIG:
	case SPIT_CTRL_SET_DEWARP_CONFIG:
		/* Send dewarp config request and wait data */
		ret = spit_send_request(spit_ctrl_devdata, arg,
				 sizeof(struct spit_dewarp_cfg),
				 req == SPIT_CTRL_GET_DEWARP_CONFIG ?
				 SPIT_RPMSG_CONTROL_TYPE_REQ_GET_DEWARP_CONFIG :
				 SPIT_RPMSG_CONTROL_TYPE_REQ_SET_DEWARP_CONFIG,
				 &resp);
		if (ret)
			return ret;

		/* Copy dewarp configuration */
		if (copy_to_user((void __user *)arg,
				 &resp->dewarp_cfg,
				 sizeof(struct spit_dewarp_cfg))) {
			return -EFAULT;
		}
		break;

	case SPIT_CTRL_GET_DEWARP_FOV:
	case SPIT_CTRL_SET_DEWARP_FOV:
		/* Send dewarp FOV request and wait data */
		ret = spit_send_request(spit_ctrl_devdata, arg,
				    sizeof(uint32_t),
				    req == SPIT_CTRL_GET_DEWARP_FOV ?
				    SPIT_RPMSG_CONTROL_TYPE_REQ_GET_DEWARP_FOV :
				    SPIT_RPMSG_CONTROL_TYPE_REQ_SET_DEWARP_FOV,
				    &resp);
		if (ret)
			return ret;

		/* Copy dewarp FOV */
		if (copy_to_user((void __user *)arg,
				 &resp->dewarp_fov,
				 sizeof(uint32_t))) {
			return -EFAULT;
		}
		break;

	case SPIT_CTRL_GET_BRACKETING_CONFIG:
	case SPIT_CTRL_SET_BRACKETING_CONFIG:
		/* Send bracketing config request and wait data */
		ret = spit_send_request(spit_ctrl_devdata, arg,
			     sizeof(struct spit_bracketing_cfg),
			     req == SPIT_CTRL_GET_BRACKETING_CONFIG ?
			     SPIT_RPMSG_CONTROL_TYPE_REQ_GET_BRACKETING_CONFIG :
			     SPIT_RPMSG_CONTROL_TYPE_REQ_SET_BRACKETING_CONFIG,
			     &resp);
		if (ret)
			return ret;

		/* Copy bracketing configuration */
		if (copy_to_user((void __user *)arg,
				 &resp->bracketing_cfg,
				 sizeof(struct spit_bracketing_cfg))) {
			return -EFAULT;
		}
		break;

	case SPIT_CTRL_GET_BURST_CONFIG:
	case SPIT_CTRL_SET_BURST_CONFIG:
		/* Send burst config request and wait data */
		ret = spit_send_request(spit_ctrl_devdata, arg,
				  sizeof(struct spit_burst_cfg),
				  req == SPIT_CTRL_GET_BURST_CONFIG ?
				  SPIT_RPMSG_CONTROL_TYPE_REQ_GET_BURST_CONFIG :
				  SPIT_RPMSG_CONTROL_TYPE_REQ_SET_BURST_CONFIG,
				  &resp);
		if (ret)
			return ret;

		/* Copy burst configuration */
		if (copy_to_user((void __user *)arg,
				 &resp->burst_cfg,
				 sizeof(struct spit_burst_cfg))) {
			return -EFAULT;
		}
		break;

	case SPIT_CTRL_GET_IMG_STYLE:
	case SPIT_CTRL_SET_IMG_STYLE:
		/* Send image style request and wait data */
		ret = spit_send_request(spit_ctrl_devdata, arg,
				sizeof(enum spit_img_style),
				req == SPIT_CTRL_GET_IMG_STYLE ?
				SPIT_RPMSG_CONTROL_TYPE_REQ_GET_IMAGE_STYLE :
				SPIT_RPMSG_CONTROL_TYPE_REQ_SET_IMAGE_STYLE,
				&resp);
		if (ret)
			return ret;

		/* Copy image style */
		if (copy_to_user((void __user *)arg,
				 &resp->img_style,
				 sizeof(enum spit_img_style))) {
			return -EFAULT;
		}
		break;

	default:
		dev_err(spit_ctrl_devdata->dev, "unknown control command\n");
		return -EINVAL;
	}

	return 0;
}

static const struct file_operations spit_ctrl_fops = {
	.owner		= THIS_MODULE,
	.open		= spit_ctrl_open,
	.release	= spit_ctrl_release,
	.unlocked_ioctl	= spit_ctrl_ioctl,
};

static int __init spit_ctrl_init(void)
{
	int i;
	int ret = 0;
	int miscdev_register = 0;

	spit_ctrl_devs = kzalloc(SPIT_CONTROL_COUNT *
				 sizeof(struct spit_ctrl_devdata), GFP_KERNEL);
	if (!spit_ctrl_devs)
		return -ENOMEM;

	for (i = 0; i < SPIT_CONTROL_COUNT; i++) {
		int j;
		mutex_init(&spit_ctrl_devs[i].lock);

		for (j = 0; j < SPIT_RPMSG_CONTROL_TYPE_RESP_COUNT; j++)
			mutex_init(&spit_ctrl_devs[i].ctrl_lock[j]);

		spit_ctrl_devs[i].control = i;
		atomic_set(&spit_ctrl_devs[i].opened, 0);
		init_waitqueue_head(&spit_ctrl_devs[i].ctrl_wait);

		/* Creating /dev spit device */
		snprintf(spit_ctrl_devs[i].devname,
			 sizeof(spit_ctrl_devs[i].devname),
			 MODULE_NAME "%d", i);
		spit_ctrl_devs[i].misc_dev.minor = MISC_DYNAMIC_MINOR;
		spit_ctrl_devs[i].misc_dev.name = spit_ctrl_devs[i].devname;
		spit_ctrl_devs[i].misc_dev.fops = &spit_ctrl_fops;
		ret = misc_register(&spit_ctrl_devs[i].misc_dev);
		if (ret < 0)
			goto failed;
		miscdev_register++;
		spit_ctrl_devs[i].dev = spit_ctrl_devs[i].misc_dev.this_device;

		dev_info(spit_ctrl_devs[i].dev,
			"device created successfully\n");
	}

	register_rpmsg_driver(&spit_ctrl_rpmsg_driver);

	return 0;

failed:
	for (i = 0; i < miscdev_register; i++)
		misc_deregister(&spit_ctrl_devs[i].misc_dev);
	kfree(spit_ctrl_devs);

	return ret;
}

static void __exit spit_ctrl_exit(void)
{
	int i;

	unregister_rpmsg_driver(&spit_ctrl_rpmsg_driver);

	for (i = 0; i < SPIT_CONTROL_COUNT; i++)
		misc_deregister(&spit_ctrl_devs[i].misc_dev);
	kfree(spit_ctrl_devs);
}

module_init(spit_ctrl_init);
module_exit(spit_ctrl_exit);

MODULE_DESCRIPTION("Parrot Spit Control Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alexandre Dilly <alexandre.dilly@parrot.com>");
