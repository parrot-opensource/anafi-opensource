/*
 * Copyright (C) 2018 Parrot Drones SAS
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rpmsg.h>
#include <linux/fs.h>
#include <linux/sysfs.h>
#include <linux/string.h>

#include <linux/slab.h>
#include <linux/pgimbal_interface.h>

enum pgimbal_state {
	PGIMBAL_STATE_IDLE = 0,		/* Waiting for request */
	PGIMBAL_STATE_REQUESTED,	/* A request is in progress */
	PGIMBAL_STATE_ANSWERED,		/* An answer has been received */
	PGIMBAL_STATE_COUNT,
};

struct pgimbal_drvdata {
	struct rpmsg_device		*rp_device;
	struct device			*dev;
	struct mutex			lock;
	wait_queue_head_t		wait_rpmsg;
	enum pgimbal_state		state;
	uint8_t				curr_alerts;
	int				offsets_update_in_progress;
	struct pgimbal_offset_info	curr_offsets[PGIMBAL_AXIS_COUNT];
};

#ifdef CONFIG_SYSFS
static ssize_t calibrate_store(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t count)
{
	struct rpmsg_device *rpdev = container_of(dev, struct rpmsg_device,
						  dev);
	struct pgimbal_drvdata *pgimbal_data = dev_get_drvdata(&rpdev->dev);
	struct pgimbal_rpmsg rpmsg;

	mutex_lock(&pgimbal_data->lock);

	rpmsg.type = PGIMBAL_RPMSG_TYPE_CALIBRATION_REQUEST;
	rpmsg_send(rpdev->ept, &rpmsg, sizeof(struct pgimbal_rpmsg));

	mutex_unlock(&pgimbal_data->lock);

	dev_dbg(dev, "calibration request sent\n");

	return count;
}

static ssize_t alerts_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct rpmsg_device *rpdev = container_of(dev, struct rpmsg_device,
						  dev);
	struct pgimbal_drvdata *pgimbal_data = dev_get_drvdata(&rpdev->dev);

	return sprintf(buf, "%u\n", pgimbal_data->curr_alerts);
}

static ssize_t offset_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count,
			    enum pgimbal_axis axis)
{
	struct rpmsg_device *rpdev = container_of(dev, struct rpmsg_device,
						  dev);
	struct pgimbal_drvdata *pgimbal_data = dev_get_drvdata(&rpdev->dev);
	struct pgimbal_rpmsg rpmsg;
	int ret;

	if (!pgimbal_data->offsets_update_in_progress)
		return -EACCES;

	if (count == 0)
		return 0;

	rpmsg.offset_info.id = axis;
	ret = sscanf(buf, "%d\n", &rpmsg.offset_info.offset);
	if (ret != 1)
		return -EINVAL;

	mutex_lock(&pgimbal_data->lock);

	pgimbal_data->state = PGIMBAL_STATE_REQUESTED;
	rpmsg.type = PGIMBAL_RPMSG_TYPE_OFFSET;
	rpmsg_send(rpdev->ept, &rpmsg, sizeof(struct pgimbal_rpmsg));

	mutex_unlock(&pgimbal_data->lock);

	ret = wait_event_interruptible_timeout(pgimbal_data->wait_rpmsg,
		pgimbal_data->state == PGIMBAL_STATE_ANSWERED, 5000);
	if (ret < 0)
		return -ERESTARTSYS;

	dev_dbg(dev, "offset sent: axis %d value %d\n",
		rpmsg.offset_info.id, rpmsg.offset_info.offset);

	return count;
}

static ssize_t offset_x_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct rpmsg_device *rpdev = container_of(dev, struct rpmsg_device,
						  dev);
	struct pgimbal_drvdata *pgimbal_data = dev_get_drvdata(&rpdev->dev);

	return sprintf(buf, "%d\n",
		       pgimbal_data->curr_offsets[PGIMBAL_AXIS_X].offset);
}

static ssize_t offset_x_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	return offset_store(dev, attr, buf, count, PGIMBAL_AXIS_X);
}

static ssize_t offset_y_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct rpmsg_device *rpdev = container_of(dev, struct rpmsg_device,
						  dev);
	struct pgimbal_drvdata *pgimbal_data = dev_get_drvdata(&rpdev->dev);

	return sprintf(buf, "%d\n",
		       pgimbal_data->curr_offsets[PGIMBAL_AXIS_Y].offset);
}

static ssize_t offset_y_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	return offset_store(dev, attr, buf, count, PGIMBAL_AXIS_Y);
}

static ssize_t offset_z_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct rpmsg_device *rpdev = container_of(dev, struct rpmsg_device,
						  dev);
	struct pgimbal_drvdata *pgimbal_data = dev_get_drvdata(&rpdev->dev);

	return sprintf(buf, "%d\n",
		       pgimbal_data->curr_offsets[PGIMBAL_AXIS_Z].offset);
}

static ssize_t offset_z_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	return offset_store(dev, attr, buf, count, PGIMBAL_AXIS_Z);
}

static ssize_t offsets_update_trigger_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct rpmsg_device *rpdev = container_of(dev, struct rpmsg_device,
						  dev);
	struct pgimbal_drvdata *pgimbal_data = dev_get_drvdata(&rpdev->dev);

	return sprintf(buf, "%d\n", pgimbal_data->offsets_update_in_progress);
}

static ssize_t offsets_update_trigger_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct rpmsg_device *rpdev = container_of(dev, struct rpmsg_device,
						  dev);
	struct pgimbal_drvdata *pgimbal_data = dev_get_drvdata(&rpdev->dev);
	struct pgimbal_rpmsg rpmsg;
	int started;
	int ret;

	if (count == 0)
		return 0;

	ret = sscanf(buf, "%d\n", &started);
	if (ret != 1)
		return -EINVAL;

	if (started != 0 && started != 1)
		return -EINVAL;

	pgimbal_data->offsets_update_in_progress = started;

	mutex_lock(&pgimbal_data->lock);

	rpmsg.type = started ? PGIMBAL_RPMSG_TYPE_OFFSETS_UPDATE_STARTED :
			PGIMBAL_RPMSG_TYPE_OFFSETS_UPDATE_STOPPED;
	rpmsg_send(rpdev->ept, &rpmsg, sizeof(struct pgimbal_rpmsg));

	mutex_unlock(&pgimbal_data->lock);

	return count;
}
static struct device_attribute pgimbal_sysfs_attrs[] = {
	__ATTR(calibrate, S_IWUSR , NULL, calibrate_store),
	__ATTR(alerts, S_IRUGO , alerts_show, NULL),
	__ATTR(offset_x, S_IWUSR | S_IRUGO, offset_x_show, offset_x_store),
	__ATTR(offset_y, S_IWUSR | S_IRUGO, offset_y_show, offset_y_store),
	__ATTR(offset_z, S_IWUSR | S_IRUGO, offset_z_show, offset_z_store),
	__ATTR(offsets_update_trigger, S_IWUSR | S_IRUGO,
	       offsets_update_trigger_show, offsets_update_trigger_store),
};
#endif

static int pgimbal_rpmsg_cb(struct rpmsg_device *rpdev, void *data, int len,
			   void *priv, u32 src)
{
	struct pgimbal_drvdata *pgimbal_data = dev_get_drvdata(&rpdev->dev);
	struct pgimbal_rpmsg *rpmsg = data;

	if (len != sizeof(struct pgimbal_rpmsg)) {
		dev_err(&rpdev->dev, "received message with invalid length\n");
		return -EINVAL;
	}

	mutex_lock(&pgimbal_data->lock);

	switch (rpmsg->type) {
	case PGIMBAL_RPMSG_TYPE_OFFSET:
		dev_dbg(pgimbal_data->dev,
			"offset received: axis %d value %d\n",
			rpmsg->offset_info.id, rpmsg->offset_info.offset);

		switch (rpmsg->offset_info.id) {
		case PGIMBAL_AXIS_X:
			memcpy(&pgimbal_data->curr_offsets[PGIMBAL_AXIS_X],
			       &rpmsg->offset_info,
			       sizeof(struct pgimbal_offset_info));
			break;
		case PGIMBAL_AXIS_Y:
			memcpy(&pgimbal_data->curr_offsets[PGIMBAL_AXIS_Y],
			       &rpmsg->offset_info,
			       sizeof(struct pgimbal_offset_info));
			break;
		case PGIMBAL_AXIS_Z:
			memcpy(&pgimbal_data->curr_offsets[PGIMBAL_AXIS_Z],
			       &rpmsg->offset_info,
			       sizeof(struct pgimbal_offset_info));
			break;
		default:
			break;
		};

		pgimbal_data->state = PGIMBAL_STATE_ANSWERED;
		wake_up(&pgimbal_data->wait_rpmsg);
		break;
	case PGIMBAL_RPMSG_TYPE_ALERTS:
		dev_dbg(pgimbal_data->dev, "alerts received: 0x%x\n",
			rpmsg->alerts);
		pgimbal_data->curr_alerts = rpmsg->alerts;
		sysfs_notify(&rpdev->dev.kobj, NULL, "alerts");
		break;
	default:
		break;
	}

	mutex_unlock(&pgimbal_data->lock);

	return 0;
}

static int pgimbal_rpmsg_probe(struct rpmsg_device *rpdev)
{
	struct pgimbal_drvdata *pgimbal_data;
	struct rpmsg_channel_info chinfo;
#ifdef CONFIG_SYSFS
	int i;
#endif

	pgimbal_data = kzalloc(sizeof(struct pgimbal_drvdata), GFP_KERNEL);
	if (!pgimbal_data)
		return -ENOMEM;

	pgimbal_data->rp_device = rpdev;
	pgimbal_data->dev = &rpdev->dev;

	strncpy(chinfo.name, rpdev->id.name, sizeof(chinfo.name));
	chinfo.name[sizeof(chinfo.name)-1] = 0;
	chinfo.src = RPMSG_ADDR_ANY;
	chinfo.dst = rpdev->dst;

	rpmsg_send(rpdev->ept, &chinfo, sizeof(chinfo));

	mutex_init(&pgimbal_data->lock);
	init_waitqueue_head(&pgimbal_data->wait_rpmsg);

	pgimbal_data->state = PGIMBAL_STATE_IDLE;
	pgimbal_data->curr_alerts = 0;
	dev_set_drvdata(&rpdev->dev, pgimbal_data);

#ifdef CONFIG_SYSFS
	for (i = 0; i < ARRAY_SIZE(pgimbal_sysfs_attrs); i++)
		device_create_file(pgimbal_data->dev, &pgimbal_sysfs_attrs[i]);
#endif

	dev_info(pgimbal_data->dev, "device created successfully\n");

	return 0;
}

static void pgimbal_rpmsg_remove(struct rpmsg_device *rpdev)
{
	struct pgimbal_drvdata *pgimbal_data = dev_get_drvdata(&rpdev->dev);
#ifdef CONFIG_SYSFS
	int i;

	for (i = 0; i < ARRAY_SIZE(pgimbal_sysfs_attrs); i++)
		device_remove_file(&rpdev->dev, &pgimbal_sysfs_attrs[i]);
#endif
	kfree(pgimbal_data);
}

static struct rpmsg_device_id pgimbal_rpmsg_id_table[] = {
	{ .name	= PGIMBAL_RPMSG_CHANNEL, },
};
MODULE_DEVICE_TABLE(rpmsg, pgimbal_rpmsg_id_table);

static struct rpmsg_driver pgimbal_rpmsg_driver = {
	.drv	= {
		.name	= KBUILD_MODNAME,
		.owner	= THIS_MODULE,
	},
	.id_table	= pgimbal_rpmsg_id_table,
	.probe		= pgimbal_rpmsg_probe,
	.callback	= pgimbal_rpmsg_cb,
	.remove		= pgimbal_rpmsg_remove,
};
module_rpmsg_driver(pgimbal_rpmsg_driver);

MODULE_DESCRIPTION("Parrot Gimbal Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ronan Chauvin <ronan.chauvin@parrot.com>");
