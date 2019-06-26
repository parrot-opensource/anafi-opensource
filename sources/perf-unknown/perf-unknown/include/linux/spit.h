/*
 * Copyright (C) 2016 Parrot S.A.
 *     Author: Aurelien Lefebvre <aurelien.lefebvre@parrot.com>
 *             Alexandre Dilly <alexandre.dilly@parrot.com>
 */

#ifndef _SPIT_H_
#define _SPIT_H_

#include "spit_defs.h"

#ifdef __KERNEL__
#include "spit_rpmsg.h"
#endif

struct spit_fifo {
	enum spit_frame_type type;
	off_t offset;
	size_t length;
};

struct spit_cache {
	enum spit_frame_type type;
	unsigned int enable;
};

/* Stream IOCTLs for /dev/spitX */
#define SPIT_IOCTL_NUM '~'
#define SPIT_GET_FRAME _IOR(SPIT_IOCTL_NUM, 1, struct spit_frame_desc)
#define SPIT_START_STREAM _IOW(SPIT_IOCTL_NUM, 2, enum spit_frame_types)
#define SPIT_STOP_STREAM _IOW(SPIT_IOCTL_NUM, 3, enum spit_frame_types)
#define SPIT_CONFIGURE_STREAM _IOW(SPIT_IOCTL_NUM, 4, struct spit_stream_conf)
#define SPIT_GET_FIFO _IOR(SPIT_IOCTL_NUM, 5, struct spit_fifo)
#define SPIT_SET_CACHE _IOW(SPIT_IOCTL_NUM, 6, struct spit_cache)
#define SPIT_SET_INPUT_CACHE _IOW(SPIT_IOCTL_NUM, 7, struct spit_cache)
#define SPIT_CHANGE_BITRATE _IOW(SPIT_IOCTL_NUM, 8, struct spit_bitrate_change)
#define SPIT_GET_CAPABILITIES _IOR(SPIT_IOCTL_NUM, 9, struct spit_stream_caps)
#define SPIT_GET_CONFIGURATION _IOR(SPIT_IOCTL_NUM, 10, struct spit_stream_conf)
#define SPIT_SET_CONFIGURATION _IOW(SPIT_IOCTL_NUM, 11, struct spit_stream_conf)
#define SPIT_GET_MODE _IOR(SPIT_IOCTL_NUM, 12, struct spit_mode)
#define SPIT_GET_MODE_FPS _IOW(SPIT_IOCTL_NUM, 13, struct spit_mode_fps)
#define SPIT_RELEASE_FRAME _IOW(SPIT_IOCTL_NUM, 14, struct spit_frame_desc)
#define SPIT_CHANGE_FRAMERATE _IOW(SPIT_IOCTL_NUM, 15, struct spit_fps)
#define SPIT_GET_FRAME_FIFO_DEPTH _IOR(SPIT_IOCTL_NUM, 16, unsigned int)
#define SPIT_SET_FRAME_FIFO_DEPTH _IOW(SPIT_IOCTL_NUM, 17, unsigned int)
#define SPIT_GET_INPUT_FIFO _IOR(SPIT_IOCTL_NUM, 18, struct spit_fifo)
#define SPIT_FEED_FRAME _IOR(SPIT_IOCTL_NUM, 19, struct spit_frame_desc)
#define SPIT_GET_INPUT_STATUS _IOR(SPIT_IOCTL_NUM, 20, struct spit_input_status)

/* Control IOCTLs for /dev/spit_ctrlX */
#define SPIT_CTRL_IOCTL_NUM SPIT_IOCTL_NUM + 1
#define SPIT_CTRL_GET_CAPABILITIES \
	_IOR(SPIT_CTRL_IOCTL_NUM, 1, struct spit_control_caps)

#define SPIT_CTRL_GET_MODE \
	_IOR(SPIT_CTRL_IOCTL_NUM, 2, struct spit_mode)
#define SPIT_CTRL_GET_MODE_FPS \
	 _IOW(SPIT_CTRL_IOCTL_NUM, 3, struct spit_mode_fps)

#define SPIT_CTRL_GET_DSP_MODE \
	 _IOR(SPIT_CTRL_IOCTL_NUM, 4, enum spit_dsp_mode)
#define SPIT_CTRL_SET_DSP_MODE \
	 _IOW(SPIT_CTRL_IOCTL_NUM, 5, enum spit_dsp_mode)

#define SPIT_CTRL_GET_CONFIGURATION \
	 _IOR(SPIT_CTRL_IOCTL_NUM, 6, struct spit_control_conf)
#define SPIT_CTRL_SET_CONFIGURATION \
	 _IOW(SPIT_CTRL_IOCTL_NUM, 7, struct spit_control_conf)

#define SPIT_CTRL_GET_AE_INFO \
	_IOR(SPIT_CTRL_IOCTL_NUM, 8, struct spit_ae_info)
#define SPIT_CTRL_SET_AE_INFO \
	_IOW(SPIT_CTRL_IOCTL_NUM, 9, struct spit_ae_info)
#define SPIT_CTRL_GET_AE_EV \
	_IOR(SPIT_CTRL_IOCTL_NUM, 10, struct spit_ae_ev)
#define SPIT_CTRL_SET_AE_EV \
	_IOW(SPIT_CTRL_IOCTL_NUM, 11, struct spit_ae_ev)

#define SPIT_CTRL_GET_AWB_INFO \
	_IOR(SPIT_CTRL_IOCTL_NUM, 12, struct spit_awb_info)
#define SPIT_CTRL_SET_AWB_INFO \
	_IOW(SPIT_CTRL_IOCTL_NUM, 13, struct spit_awb_info)

#define SPIT_CTRL_GET_IMG_SETTINGS \
	_IOR(SPIT_CTRL_IOCTL_NUM, 14, struct spit_img_settings)
#define SPIT_CTRL_SET_IMG_SETTINGS \
	_IOW(SPIT_CTRL_IOCTL_NUM, 15, struct spit_img_settings)

#define SPIT_CTRL_GET_FLICKER_MODE \
	_IOR(SPIT_CTRL_IOCTL_NUM, 16, enum spit_flicker_mode)
#define SPIT_CTRL_SET_FLICKER_MODE \
	_IOW(SPIT_CTRL_IOCTL_NUM, 17, enum spit_flicker_mode)

#define SPIT_CTRL_GET_DEWARP_CONFIG \
	_IOR(SPIT_CTRL_IOCTL_NUM, 18, struct spit_dewarp_cfg)
#define SPIT_CTRL_SET_DEWARP_CONFIG \
	_IOW(SPIT_CTRL_IOCTL_NUM, 19, struct spit_dewarp_cfg)
#define SPIT_CTRL_GET_DEWARP_FOV \
	_IOR(SPIT_CTRL_IOCTL_NUM, 20, unsigned long)
#define SPIT_CTRL_SET_DEWARP_FOV \
	_IOW(SPIT_CTRL_IOCTL_NUM, 21, unsigned long)

#define SPIT_CTRL_GET_BRACKETING_CONFIG \
	_IOR(SPIT_CTRL_IOCTL_NUM, 22, struct spit_bracketing_cfg)
#define SPIT_CTRL_SET_BRACKETING_CONFIG \
	_IOW(SPIT_CTRL_IOCTL_NUM, 23, struct spit_bracketing_cfg)
#define SPIT_CTRL_GET_BURST_CONFIG \
	_IOR(SPIT_CTRL_IOCTL_NUM, 24, struct spit_burst_cfg)
#define SPIT_CTRL_SET_BURST_CONFIG \
	_IOW(SPIT_CTRL_IOCTL_NUM, 25, struct spit_burst_cfg)

#define SPIT_CTRL_GET_IMG_STYLE \
	_IOR(SPIT_CTRL_IOCTL_NUM, 26, enum spit_img_style)
#define SPIT_CTRL_SET_IMG_STYLE \
	_IOW(SPIT_CTRL_IOCTL_NUM, 27, enum spit_img_style)

#define SPIT_CTRL_GET_LSC_TABLE \
	_IOWR(SPIT_CTRL_IOCTL_NUM, 28, struct spit_lens_shading_maps)

#define SPIT_CTRL_TAKE_DSP_LOCK _IO(SPIT_CTRL_IOCTL_NUM, 29)
#define SPIT_CTRL_RELEASE_DSP_LOCK _IO(SPIT_CTRL_IOCTL_NUM, 30)

#define SPIT_CTRL_GET_STATUS \
	_IOR(SPIT_CTRL_IOCTL_NUM, 31, enum spit_control_status)

#endif /* _SPIT_H_ */
