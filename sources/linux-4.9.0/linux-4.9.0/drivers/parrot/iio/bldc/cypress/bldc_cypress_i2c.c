/**
 ************************************************
 * @file bldc_cypress_core.c
 * @brief BLDC cypress IIO driver
 *
 * Copyright (C) 2015 Parrot S.A.
 *
 * @author Karl Leplat <karl.leplat@parrot.com>
 * @date 2015-06-22
 *************************************************
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/sysfs.h>
#include <linux/jiffies.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/kfifo.h>
#include <linux/spinlock.h>
#include <linux/iio/iio.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/firmware.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

#include "parrot_bldc_iio.h"
#include "parrot_bldc_cypress_iio.h"
#include "bldc_host_bootloader.h"

#define BLDC_CYPRESS_DRV_NAME "mpsoc-i2c"

#define C_BLDC_FLASHED_MD5_FILE "/data/.bldc_flashed.md5"

static int bldc_i2c_read_multiple_byte(struct device *dev,
		u8 reg_addr, int len, u8 *data, int with_dummy_byte)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct i2c_msg msgs[2];
	u8 tx_buf[2];
	int ret;
	int tx_len = 1;

	tx_buf[0] = reg_addr;
	tx_buf[1] = 0xFF;
	if (with_dummy_byte)
		tx_len = 2;

	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = tx_len;
	msgs[0].buf = tx_buf;

	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = data;

	ret = i2c_transfer(client->adapter, msgs, 2);
	if (ret != 2) {
		dev_err(&client->dev, "i2c transfer failed\n");
		return -EIO;
	}

	return 0;
}

static int bldc_i2c_write_multiple_byte(struct device *dev,
		u8 reg_addr, int len, u8 *data)
{
	return i2c_smbus_write_i2c_block_data(to_i2c_client(dev),
			reg_addr, len, data);
}

static const struct bldc_transfer_function bldc_tf_i2c = {
	.write_multiple_byte = bldc_i2c_write_multiple_byte,
	.read_multiple_byte = bldc_i2c_read_multiple_byte,
};

static void bldc_i2c_configure(struct iio_dev *indio_dev,
		struct i2c_client *client, struct bldc_state *st)
{
	i2c_set_clientdata(client, indio_dev);

	indio_dev->dev.parent = &client->dev;
	indio_dev->name = client->name;

	st->dev = &client->dev;
	st->tf = &bldc_tf_i2c;
}

static void md5_to_str(const u8 *sha1, char *str)
{
	int i;
	static const char hexval[16] = {
		'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b',
		'c', 'd', 'e', 'f'
	};

	for (i = 0; i < MD5_DIGEST_SIZE; i++) {
		str[i*2] = hexval[((sha1[i] >> 4) & 0xf)];
		str[(i*2) + 1] = hexval[(sha1[i]) & 0x0f];
	}

	str[2*MD5_DIGEST_SIZE] = '\0';
}

static int bldc_has_fw(struct host_bootloader *const bootloader)
{
	uint8_t invalid_fw_md5_digest[MD5_DIGEST_SIZE];

	memset(invalid_fw_md5_digest, 0, MD5_DIGEST_SIZE);

	/* bldc has no app firmware if its md5 is zero  */
	if (memcmp(bootloader->bldc_md5_digest,
			invalid_fw_md5_digest, MD5_DIGEST_SIZE) == 0)
		return 0;
	else
		return 1;
}


static int is_firmware_already_flashed(struct i2c_client *client,
		struct host_bootloader *const bootloader)
{
	int ret;
	char bldc_md5_str[2*MD5_DIGEST_SIZE + 1];
	char local_md5_str[2*MD5_DIGEST_SIZE + 1];

	md5_to_str(bootloader->bldc_md5_digest, bldc_md5_str);
	md5_to_str(bootloader->cyacd_md5_digest, local_md5_str);

	dev_info(&client->dev, "bldc  md5 fw: %s\n", bldc_md5_str);
	dev_info(&client->dev, "local md5 fw: %s\n", local_md5_str);

	/* compare bldc md5sum with our md5 local firmware */
	if (!memcmp(bootloader->cyacd_md5_digest,
			bootloader->bldc_md5_digest, MD5_DIGEST_SIZE))
		ret = 1;
	else
		ret = 0;

	return ret;
}

static int upload_firmware(struct i2c_client *client,
		struct host_bootloader *bootloader,
		const uint8_t *filename)
{
	const struct firmware *fw_entry;
	uint8_t *fw_data;
	int fw_len, ret = 0;

	/* request local fw */
	ret = request_firmware(&fw_entry, filename, &client->dev);
	if (ret) {
		dev_warn(&client->dev, "firmware (%s) not found.\n", filename);
		goto out_download_firmware;
	}

	fw_len = fw_entry->size;
	fw_data = (uint8_t *) fw_entry->data;

	if (fw_len > MAX_CYACD_SIZE) {
		dev_err(&client->dev, "firmware too large %d\n", fw_len);
		goto rel_fw;
	}

	/* compute local fw md5sum */
	ret = bldc_open_cyacd_file(bootloader, fw_data, fw_len);
	if (ret != C_BTLDR_SUCCESS)
		goto rel_fw;

	/* Check if the file is already flashed and not corrupted */
	ret = bldc_verify_crc_appli(client, bootloader);
	if ((ret == C_BTLDR_SUCCESS) &&
	    is_firmware_already_flashed(client, bootloader)) {
		dev_info(&client->dev, "bldc fw already up-to-date\n");
		goto end_bootloader;
	}

	/* Try to flash the new BLDC firmware. */
	ret = bldc_program(client, bootloader, fw_data, fw_len);
	if (ret != C_BTLDR_SUCCESS) {
		/* Failure during the flash process.
		 * Do not restore firmware with backup:
		 * flash has not been programmed
		 * If flash failed because of incompatible device
		 * do not overwrite error code */
		if (ret != C_BTLDR_ERR_DEVICE)
			ret = C_BTLDR_FAILURE_FIRMWARE_NOT_FLASHED;

		dev_err(&client->dev, "bldc fw flashed failed: %d\n", ret);
		goto end_bootloader;
	}

	/* The fw has been correctly flashed, write new md5sum into bldc
	 * internal flaash */
	dev_info(&client->dev, "bldc fw flashed succeed\n");
	ret = bldc_write_md5(client, bootloader, bootloader->cyacd_md5_digest,
			sizeof(bootloader->cyacd_md5_digest));
	if (ret != C_BTLDR_SUCCESS) {
		dev_err(&client->dev, "bldc fw update md5 failed %d\n", ret);
		/* ignore md5 write error */
		ret = 0;
	}

end_bootloader:
	bldc_end_bootloader(client, bootloader);
rel_fw:
	release_firmware(fw_entry);
out_download_firmware:

	return ret;
}

static int bldc_i2c_get_esc_infos(struct bldc_state *st)
{
	u8 esc_info[PARROT_BLDC_GET_INFO_LENGTH];
	int rc = bldc_i2c_read_multiple_byte(st->dev,
						PARROT_BLDC_REG_INFO,
						PARROT_BLDC_GET_INFO_LENGTH,
						esc_info, 0);

	if (rc) {
		dev_err(st->dev, "failed to retrieve ESC infos\n");
		memset(esc_info, 0, sizeof(esc_info));
	}
	snprintf(
		st->fw_version,
		sizeof(st->fw_version),
		"%d.%d.%c.%d",
		esc_info[0], esc_info[1], esc_info[2], esc_info[3]);
	st->hw_version = esc_info[4];

	return rc;
}

static inline struct bldc_state* bldc_dev_to_st(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct bldc_state *st = iio_priv(indio_dev);

	return st;
}

static ssize_t show_fw_version(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct bldc_state *st = bldc_dev_to_st(dev);

	return sprintf(buf, "%s\n", st->fw_version);
}

static ssize_t show_hw_version(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct bldc_state *st = bldc_dev_to_st(dev);

	return sprintf(buf, "%d\n", st->hw_version);
}

static DEVICE_ATTR(fw_version, S_IRUSR, show_fw_version, NULL);
static DEVICE_ATTR(hw_version, S_IRUSR, show_hw_version, NULL);

static struct attribute *bldc_attrs[] = {
	&dev_attr_fw_version.attr,
	&dev_attr_hw_version.attr,
	NULL,
};

static struct attribute_group bldc_attr_group = {
	.attrs = bldc_attrs,
};

static int bldc_create_sysfs(struct device *dev)
{
	return sysfs_create_group(&dev->kobj, &bldc_attr_group);
}

static void bldc_remove_sysfs(struct device *dev)
{
	sysfs_remove_group(&dev->kobj, &bldc_attr_group);
}

static int bldc_cypress_i2c_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	int rc;
	struct bldc_state *st;
	struct device_node *node = client->dev.of_node;
	struct iio_dev *indio_dev;
	struct host_bootloader bootloader;
	const char *fw_name;
	char propname[50];

	memset(&bootloader, 0, sizeof(bootloader));

	if (!node)
		return -ENODEV;

	if (!i2c_check_functionality(client->adapter,
		I2C_FUNC_SMBUS_I2C_BLOCK))
		return -ENOSYS;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);

	rc = of_property_read_u32_array(node, "lut", st->pdata.lut, 4);
	if (rc) {
		dev_err(&client->dev, "failed to parse lut property\n");
		return -EINVAL;
	}

	rc = of_property_read_string(node, "firmware-name", &fw_name);
	if (rc) {
		dev_err(&client->dev,
			"failed to parse firmware-name property\n");
		return -EINVAL;
	}

	st->pdata.gpio_reset = of_get_named_gpio(node, "reset-gpio", 0);

	if (gpio_is_valid(st->pdata.gpio_reset)) {
		rc = devm_gpio_request(&client->dev, st->pdata.gpio_reset,
					"bldc_reset");
		if (rc < 0) {
			dev_err(&client->dev,
				"%s: bldc_reset gpio_request failed %d\n",
				__func__, rc);
			return rc;
		}

		rc = gpio_direction_output(st->pdata.gpio_reset, 1);
		if (rc < 0) {
			dev_err(&client->dev,
			"%s: gimbal_reset gpio_direction_output failed %d\n",
				__func__, rc);
			return rc;
		}

		gpio_set_value(st->pdata.gpio_reset, 1);
		usleep_range(1000, 2000);
		gpio_set_value(st->pdata.gpio_reset, 0);
		msleep(200);
	}

	rc = upload_firmware(client, &bootloader, fw_name);

	/* Accept the device if upload_firmware failed because
	 * of local fw incompatibility. bldc must have an applicative
	 * fw already flashed */
	if (rc == C_BTLDR_ERR_DEVICE && bldc_has_fw(&bootloader))
		goto accept;

	if (rc != C_BTLDR_SUCCESS)
		return -EINVAL;

accept:
	/* read spin_dir corresponding to bldc revision */
	snprintf(propname, sizeof(propname), "spin_dir_%x",
			bootloader.bldc_silicon_id);
	rc = of_property_read_u32(node, propname, &st->pdata.spin_dir);
	if (rc) {
		/* read default spin_dir  */
		rc = of_property_read_u32(node, "spin_dir",
				&st->pdata.spin_dir);
		if (rc) {
			dev_err(&client->dev, "failed to parse spin_dir "
					"property\n");
			return -EINVAL;
		}
	}

	dev_info(&client->dev,
		"lut={%d, %d, %d, %d} spin_dir=0x%X gpio_reset=%d\n",
		st->pdata.lut[0], st->pdata.lut[1], st->pdata.lut[2],
		st->pdata.lut[3], st->pdata.spin_dir,
		st->pdata.gpio_reset);

	bldc_i2c_configure(indio_dev, client, st);

	rc = bldc_i2c_get_esc_infos(st);
	if (rc)
		return rc;

	rc = bldc_create_sysfs(&client->dev);
	if (rc)
		return rc;

	return bldc_cypress_probe(indio_dev);
}

static int bldc_cypress_i2c_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);

	bldc_remove_sysfs(&client->dev);

	bldc_cypress_remove(indio_dev);
	devm_iio_device_free(&client->dev, indio_dev);
	return 0;
}

static const struct i2c_device_id bldc_cypress_i2c_id[] = {
	{"mpsoc-i2c", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, bldc_cypress_i2c_id);

static const struct of_device_id bldc_cypress_i2c_of_match[] = {
	{ .compatible = "parrot,mpsoc-i2c", },
	{}
};
MODULE_DEVICE_TABLE(of, bldc_cypress_i2c_of_match);

static struct i2c_driver bldc_cypress_driver = {
	.probe		=	bldc_cypress_i2c_probe,
	.remove		=	bldc_cypress_i2c_remove,
	.id_table       =       bldc_cypress_i2c_id,
	.driver = {
		.owner	=	THIS_MODULE,
		.name	=	BLDC_CYPRESS_DRV_NAME,
		.of_match_table	= of_match_ptr(bldc_cypress_i2c_of_match),
	},
};

module_i2c_driver(bldc_cypress_driver);

MODULE_AUTHOR("Karl Leplat <karl.leplat@parrot.com>");
MODULE_DESCRIPTION("Parrot BLDC cypress IIO i2c driver");
MODULE_LICENSE("GPL");
