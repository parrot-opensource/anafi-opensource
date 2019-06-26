/*
 * drivers/drivers/video/ambarella/ambarella_fb.c
 *
 *	2008/07/22 - [linnsong] Create
 *	2009/03/03 - [Anthony Ginger] Port to 2.6.28
 *	2009/12/15 - [Zhenwu Xue] Change fb_setcmap
 *	2016/07/28 - [Cao Rongrong] Re-write
 *
 * Copyright (C) 2004-2009, Ambarella, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/fb.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <drm/drm_fourcc.h>
#include <linux/dma-mapping.h>
#include <plat/fb.h>

static char *mode = NULL;
module_param(mode, charp, 0444);
MODULE_PARM_DESC(mode, "Initial video mode in characters.");

static char *resolution = NULL;
module_param(resolution, charp, 0444);
MODULE_PARM_DESC(resolution, "Initial video resolution in characters.");

#define MIN_XRES	16
#define MIN_YRES	16
#define MAX_XRES	2048
#define MAX_YRES	2048

struct ambfb_format {
	const char *name;
	u32 bits_per_pixel;
	struct fb_bitfield red;
	struct fb_bitfield green;
	struct fb_bitfield blue;
	struct fb_bitfield transp;
	u32 fourcc;
};

struct ambfb_par {
	u32 vout_id; /* must be put at first */
	struct device *dev;
	atomic_t use_count;
	u32 max_width;
	u32 max_height;
	struct ambfb_format *format;
};

static struct ambfb_format ambfb_formats[] = {
	{ "rgb565", 16, {11, 5}, {5, 6}, {0, 5}, {0, 0}, DRM_FORMAT_RGB565 },
	{ "vyu565", 16, {11, 5}, {5, 6}, {0, 5}, {0, 0}, fourcc_code('v', 'y', '1', '6') },
	{ "bgr565", 16, {0, 5}, {5, 6}, {11, 5}, {0, 0}, DRM_FORMAT_BGR565 },
	{ "uyv565", 16, {0, 5}, {5, 6}, {11, 5}, {0, 0}, fourcc_code('u', 'y', '1', '6') },
	{ "ayuv4444", 16, {0, 4}, {8, 4}, {4, 4}, {12, 4}, fourcc_code('a', 'y', 'u', 'v') },
	{ "rgba4444", 16, {12, 4}, {8, 4}, {4, 4}, {0, 4}, DRM_FORMAT_RGBA4444 },
	{ "bgra4444", 16, {4, 4}, {8, 4}, {12, 4}, {0, 4}, DRM_FORMAT_BGRA4444 },
	{ "abgr4444", 16, {0, 4}, {4, 4}, {8, 4}, {12, 4}, DRM_FORMAT_ABGR4444 },
	{ "argb4444", 16, {8, 4}, {4, 4}, {0, 4}, {12, 4}, DRM_FORMAT_ARGB4444 },
	{ "ayuv1555", 16, {0, 5}, {10, 5}, {5, 5}, {15, 1}, fourcc_code('a', 'y', '1', '5') },
	{ "rgba5551", 16, {11, 5}, {6, 5}, {1, 5}, {0, 1}, DRM_FORMAT_RGBA5551 },
	{ "bgra5551", 16, {1, 5}, {6, 5}, {11, 5}, {0, 1}, DRM_FORMAT_BGRA5551 },
	{ "abgr1555", 16, {0, 5}, {5, 5}, {10, 5}, {15, 1}, DRM_FORMAT_ABGR1555 },
	{ "argb1555", 16, {10, 5}, {5, 5}, {0, 5}, {15, 1}, DRM_FORMAT_ARGB1555 },
	{ "ayuv8888", 32, {0, 8}, {16, 8}, {8, 8}, {24, 8}, DRM_FORMAT_AYUV },
	{ "rgba8888", 32, {24, 8}, {16, 8}, {8, 8}, {0, 8}, DRM_FORMAT_RGBA8888 },
	{ "bgra8888", 32, {8, 8}, {16, 8}, {24, 8}, {0, 8}, DRM_FORMAT_BGRA8888 },
	{ "abgr8888", 32, {0, 8}, {8, 8}, {16, 8}, {24, 8}, DRM_FORMAT_ABGR8888 },
	{ "argb8888", 32, {16, 8}, {8, 8}, {0, 8}, {24, 8}, DRM_FORMAT_ARGB8888 },
};

static int ambfb_notifier_call_chain(struct fb_info *info, unsigned long evt, void *data)
{
	struct fb_event event;

	event.info = info;
	event.data = data;
	return fb_notifier_call_chain(evt, &event);
}

static int ambfb_open(struct fb_info *info, int user)
{
	struct ambfb_par *par = info->par;
	int rval = 0;

	if (atomic_inc_and_test(&par->use_count)) {
		rval = ambfb_notifier_call_chain(info, AMBFB_EVENT_OPEN, &info->var);
		if (rval == NOTIFY_DONE)
			rval = -ENODEV;
		else if (notifier_to_errno(rval) < 0)
			rval = notifier_to_errno(rval);
		else
			rval = 0;
	}

	if (rval < 0)
		atomic_dec(&par->use_count);

	return rval;
}

static int ambfb_release(struct fb_info *info, int user)
{
	struct ambfb_par *par = info->par;
	int rval = 0;

	if (atomic_dec_return(&par->use_count) == -1) {
		rval = ambfb_notifier_call_chain(info, AMBFB_EVENT_RELEASE, NULL);
		if (rval == NOTIFY_DONE)
			rval = -ENODEV;
		else if (notifier_to_errno(rval) < 0)
			rval = notifier_to_errno(rval);
		else
			rval = 0;
	}

	if (rval < 0)
		atomic_inc(&par->use_count);

	return 0;
}

static int ambfb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	u32 smem_len, i, size = sizeof(struct fb_bitfield);
	int rval;

	var->xres_virtual = var->xres;
	var->yres_virtual = var->yres * 2;

	/* Basic geometry sanity checks. */
	if (var->xres < MIN_XRES)
		var->xres = MIN_XRES;
	if (var->yres < MIN_YRES)
		var->yres = MIN_YRES;
	if (var->xres > MAX_XRES)
		return -EINVAL;
	if (var->yres > MAX_YRES)
		return -EINVAL;
	if (var->xoffset + var->xres > var->xres_virtual)
		return -EINVAL;
	if (var->yoffset + var->yres > var->yres_virtual)
		return -EINVAL;

	/* Check size of framebuffer. */
	smem_len = var->xres_virtual * var->yres_virtual * var->bits_per_pixel;
	if (DIV_ROUND_UP(smem_len, 8) > info->fix.smem_len)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(ambfb_formats); i++) {
		if (var->grayscale == ambfb_formats[i].fourcc ||
			(!memcmp(&var->red, &ambfb_formats[i].red, size) &&
			 !memcmp(&var->green, &ambfb_formats[i].green, size) &&
			 !memcmp(&var->blue, &ambfb_formats[i].blue, size) &&
			 !memcmp(&var->transp, &ambfb_formats[i].transp, size)))
			break;
	}

	if (i >= ARRAY_SIZE(ambfb_formats))
		return -EINVAL;

	var->bits_per_pixel = ambfb_formats[i].bits_per_pixel;
	var->grayscale = ambfb_formats[i].fourcc;
	var->red = ambfb_formats[i].red;
	var->green = ambfb_formats[i].green;
	var->blue = ambfb_formats[i].blue;
	var->transp = ambfb_formats[i].transp;

	var->nonstd = 0;
	var->height = -1;
	var->width = -1;

	rval = ambfb_notifier_call_chain(info, AMBFB_EVENT_CHECK_PAR, var);

	return notifier_to_errno(rval);
}

static int ambfb_set_par(struct fb_info *info)
{

	info->fix.line_length = DIV_ROUND_UP(info->var.xres_virtual *
						info->var.bits_per_pixel, 8);
	ambfb_notifier_call_chain(info, AMBFB_EVENT_SET_PAR, NULL);

	return 0;
}

static int ambfb_pan_display(struct fb_var_screeninfo *var, struct fb_info *info)
{
	/* No support for X panning! */
	if (!var || var->xoffset != 0)
		return -EINVAL;

	ambfb_notifier_call_chain(info, AMBFB_EVENT_PAN_DISPLAY, var);

	return 0;
}

static int ambfb_setcmap(struct fb_cmap *cmap, struct fb_info *info)
{
	return 0;
}

static int ambfb_blank(int blank_mode, struct fb_info *info)
{
	return 0;
}

static struct fb_ops ambfb_ops = {
	.owner          = THIS_MODULE,
	.fb_open	= ambfb_open,
	.fb_release	= ambfb_release,
	.fb_check_var	= ambfb_check_var,
	.fb_set_par	= ambfb_set_par,
	.fb_pan_display	= ambfb_pan_display,
	.fb_fillrect	= sys_fillrect,
	.fb_copyarea	= sys_copyarea,
	.fb_imageblit	= sys_imageblit,
	.fb_setcmap	= ambfb_setcmap,
	.fb_blank	= ambfb_blank,
};

static int ambfb_parse_dt(struct ambfb_par *par)
{
	struct device_node *np = par->dev->of_node;
	const char *format;
	int i, rval;

	if (resolution != NULL) {
		rval = sscanf(resolution, "%dx%d", &par->max_width, &par->max_height);
		if (rval != 2) {
			dev_err(par->dev, "Invalid resolution parameters\n");
			return -EINVAL;
		}
	} else {
		rval = of_property_read_u32(np, "amb,max-width", &par->max_width);
		if (rval < 0)
			par->max_width = 720;

		rval = of_property_read_u32(np, "amb,max-height", &par->max_height);
		if (rval < 0)
			par->max_height = 480;
	}

	if (mode != NULL) {
		format = mode;
	} else {
		rval = of_property_read_string(np, "amb,format", &format);
		if (rval < 0) {
			dev_err(par->dev, "Can't parse format property\n");
			return rval;
		}
	}

	rval = of_property_read_u32(np, "amb,vout-id", &par->vout_id);
	if (rval < 0) {
		dev_err(par->dev, "Can't parse vout-id property\n");
		return rval;
	}

	for (i = 0; i < ARRAY_SIZE(ambfb_formats); i++) {
		if (!strcmp(format, ambfb_formats[i].name)) {
			par->format = &ambfb_formats[i];
			break;
		}
	}
	if (!par->format) {
		dev_err(par->dev, "Invalid format value\n");
		return -EINVAL;
	}

	return 0;
}

static int ambfb_probe(struct platform_device *pdev)
{
	struct ambfb_par *par;
	struct fb_info *info;
	struct fb_var_screeninfo *var;
	struct fb_fix_screeninfo *fix;
	int rval;

	info = framebuffer_alloc(sizeof(struct ambfb_par), &pdev->dev);
	if (!info)
		return -ENOMEM;

	platform_set_drvdata(pdev, info);

	par = info->par;
	par->dev = &pdev->dev;
	atomic_set(&par->use_count, -1);

	rval = ambfb_parse_dt(par);
	if (rval < 0)
		goto exit0;

	var = &info->var;
	var->height = -1,
	var->width = -1,
	var->activate = FB_ACTIVATE_NOW,
	var->vmode = FB_VMODE_NONINTERLACED,
	var->xres = par->max_width;
	var->yres = par->max_height;
	var->xres_virtual = par->max_width;
	var->yres_virtual = par->max_height * 2;
	var->bits_per_pixel = par->format->bits_per_pixel;
	var->red = par->format->red;
	var->green = par->format->green;
	var->blue = par->format->blue;
	var->transp = par->format->transp;
	var->grayscale = par->format->fourcc;

	fix = &info->fix;
	strcpy(fix->id, "ambfb");
	fix->type = FB_TYPE_PACKED_PIXELS;
	fix->visual = FB_VISUAL_TRUECOLOR;
	fix->accel = FB_ACCEL_NONE;
	fix->ypanstep = 1;
	fix->ywrapstep = 1;
	fix->line_length = DIV_ROUND_UP(var->xres_virtual * var->bits_per_pixel, 8);
	fix->smem_len = fix->line_length * var->yres_virtual;
	info->screen_base = dma_alloc_writecombine(info->device, fix->smem_len,
				(dma_addr_t *)&fix->smem_start, GFP_KERNEL);
	if (!info->screen_base) {
		rval = -ENOMEM;
		goto exit0;
	}
	memset(info->screen_base, 0, info->fix.smem_len);

	info->fbops = &ambfb_ops;
	info->flags = FBINFO_DEFAULT;

	rval = fb_alloc_cmap(&info->cmap, 256, 1);
	if (rval < 0) {
		rval = -ENOMEM;
		goto exit1;
	}

	rval = register_framebuffer(info);
	if (rval < 0) {
		dev_err(&pdev->dev, "register_framebuffer fail!\n");
		rval = -ENOMEM;
		goto exit2;
	}

	dev_info(&pdev->dev, "%dx%d, %s, %d bits per pixel\n", par->max_width,
		par->max_height, par->format->name, par->format->bits_per_pixel);

	return 0;

exit2:
	fb_dealloc_cmap(&info->cmap);
exit1:
	dma_free_writecombine(info->device, fix->smem_len,
				info->screen_base, fix->smem_start);
exit0:
	framebuffer_release(info);
	return rval;
}

static int ambfb_remove(struct platform_device *pdev)
{
	struct fb_info *info = platform_get_drvdata(pdev);

	unregister_framebuffer(info);
	fb_dealloc_cmap(&info->cmap);
	dma_free_writecombine(info->device, info->fix.smem_len,
				info->screen_base, info->fix.smem_start);
	framebuffer_release(info);

	return 0;
}

static const struct of_device_id ambfb_dt_ids[] = {
	{ .compatible = "ambarella,fb" },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, ambfb_dt_ids);

static struct platform_driver ambfb_driver = {
	.probe		= ambfb_probe,
	.remove 	= ambfb_remove,
	.driver = {
		.name	= "ambarella_fb",
		.of_match_table = ambfb_dt_ids,
	},
};

module_platform_driver(ambfb_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Ambarella framebuffer driver");
MODULE_AUTHOR("Cao Rongrong <rrcao@ambarella.com>");

