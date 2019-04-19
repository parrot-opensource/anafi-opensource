/*
 * Copyright (C) 2016 Parrot S.A.
 *     Author: Christophe Puga <christophe.puga@parrot.com>
 *             Aurelien Lefebvre <aurelien.lefebvre@parrot.com>
 *             Alexandre Dilly <alexandre.dilly@parrot.com>
 */

#ifndef _SPIT_DEFS_H_
#define _SPIT_DEFS_H_

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#endif

enum spit_stream {
	SPIT_STREAM_RECORDING = 0,
	SPIT_STREAM_STREAMING,
	SPIT_STREAM_VCAM,
	SPIT_STREAM_PHOTO,

	SPIT_STREAM_COUNT,
	SPIT_STREAM_FORCE_ENUM = 0xffffffff,
};

enum spit_control {
	SPIT_CONTROL_FRONT_CAMERA = 0,
	SPIT_CONTROL_VERTICAL_CAMERA,

	SPIT_CONTROL_COUNT,
	SPIT_CONTROL_NONE,
	SPIT_CONTROL_FORCE_ENUM = 0xffffffff,
};

/* Stream type
 * This enum is used for Spit controls streams:
 *  - for controls, it is used to differentiate sensor settings which will be
 *    used for all associated streams,
 *  - for streams, it is used to differentiate if stream can do video capture
 *    such as H264 and JPEG as snapshot / thumbnail and if stream can do photo
 *    capture such as JPEG of full frame / thumbnail. For each, the sensor mode
 *    can be different.
*/
enum spit_stream_type {
	SPIT_STREAM_TYPE_VIDEO = 0,
	SPIT_STREAM_TYPE_PHOTO,

	SPIT_STREAM_TYPE_COUNT,
	SPIT_STREAM_TYPE_FORCE_ENUM = 0xffffffff,
};

enum spit_frame_type {
	SPIT_FRAME_TYPE_NONE = 0,
	SPIT_FRAME_TYPE_H264,
	SPIT_FRAME_TYPE_RAW,
	SPIT_FRAME_TYPE_JPEG,
	SPIT_FRAME_TYPE_THUMB,

	SPIT_FRAME_TYPE_COUNT,
	SPIT_FRAME_TYPE_FORCE_ENUM = 0xffffffff,
};

enum spit_frame_types {
	SPIT_FRAME_TYPES_NONE = 0,
	SPIT_FRAME_TYPES_H264 = (1 << SPIT_FRAME_TYPE_H264),
	SPIT_FRAME_TYPES_RAW = (1 << SPIT_FRAME_TYPE_RAW),
	SPIT_FRAME_TYPES_JPEG = (1 << SPIT_FRAME_TYPE_JPEG),
	SPIT_FRAME_TYPES_THUMB = (1 << SPIT_FRAME_TYPE_THUMB),

	SPIT_FRAME_TYPES_ALL = 0xffffffff,
};
#define SPIT_FRAME_TYPE_TO_TYPES(type) (1 << type)
#define SPIT_FRAME_TYPES_HAS_TYPE(types, type) \
	(SPIT_FRAME_TYPE_TO_TYPES(type) & types)

enum spit_dsp_mode {
	SPIT_DSP_MODE_MOVIE_RECORD = 0, /*!< Video recording + streaming */
	SPIT_DSP_MODE_STILL_CAPTURE, /*!< Photo capture + preview streaming */
	SPIT_DSP_MODE_TRANSCODE, /*!< No camera, transcoding only */

	SPIT_DSP_MODE_COUNT,
	SPIT_DSP_MODE_CURRENT = 0xffffffff,
};

enum spit_raw_format {
	SPIT_RAW_FORMAT_NONE = 0,

	/* Bayer format */
	SPIT_RAW_FORMAT_RGGB8 = (1 << 0),
	SPIT_RAW_FORMAT_BGGR8 = (1 << 1),
	SPIT_RAW_FORMAT_GRBG8 = (1 << 2),
	SPIT_RAW_FORMAT_GBRG8 = (1 << 3),
	SPIT_RAW_FORMAT_RGGB10 = (1 << 4),
	SPIT_RAW_FORMAT_BGGR10 = (1 << 5),
	SPIT_RAW_FORMAT_GRBG10 = (1 << 6),
	SPIT_RAW_FORMAT_GBRG10 = (1 << 7),
	SPIT_RAW_FORMAT_RGGB12 = (1 << 8),
	SPIT_RAW_FORMAT_BGGR12 = (1 << 9),
	SPIT_RAW_FORMAT_GRBG12 = (1 << 10),
	SPIT_RAW_FORMAT_GBRG12 = (1 << 11),
	SPIT_RAW_FORMAT_RGGB14 = (1 << 12),
	SPIT_RAW_FORMAT_BGGR14 = (1 << 13),
	SPIT_RAW_FORMAT_GRBG14 = (1 << 14),
	SPIT_RAW_FORMAT_GBRG14 = (1 << 15),

	/*Gray format */
	SPIT_RAW_FORMAT_GRAY8 = (1 << 16),
	SPIT_RAW_FORMAT_GRAY10 = (1 << 17),
	SPIT_RAW_FORMAT_GRAY12 = (1 << 18),
	SPIT_RAW_FORMAT_GRAY14 = (1 << 19),

	/* YUV format */
	SPIT_RAW_FORMAT_NV12 = (1 << 20),
	SPIT_RAW_FORMAT_NV16 = (1 << 21),

	SPIT_RAW_FORMAT_ALL = 0xffffffff,
};

struct spit_stream_caps {
	char name[20];
	enum spit_control control;
	enum spit_stream_type stream_type;
	enum spit_frame_types frame_types;
	enum spit_raw_format raw_formats;
	enum spit_frame_types input_frame_types;
	enum spit_raw_format input_raw_formats;
	uint32_t modes_count;
};

enum spit_control_features {
	SPIT_CONTROL_FEATURES_NONE = 0,
	SPIT_CONTROL_FEATURES_DSP = (1 << 0),
	SPIT_CONTROL_FEATURES_AE = (1 << 1),
	SPIT_CONTROL_FEATURES_AWB = (1 << 2),
	SPIT_CONTROL_FEATURES_FLICKER = (1 << 3),
	SPIT_CONTROL_FEATURES_DEWARP = (1 << 4),
	SPIT_CONTROL_FEATURES_BRACKETING = (1 << 5),
	SPIT_CONTROL_FEATURES_BURST = (1 << 6),
	SPIT_CONTROL_FEATURES_IMG_STYLE = (1 << 7),

	SPIT_CONTROL_FEATURES_ALL = 0xffffffff
};

struct spit_control_caps {
	char name[20];
	uint32_t modes_count;
	enum spit_control_features features;
};

enum spit_h264_frame_type {
	SPIT_H264_FRAME_TYPE_NONE = 0,
	SPIT_H264_FRAME_TYPE_IDR,
	SPIT_H264_FRAME_TYPE_I,
	SPIT_H264_FRAME_TYPE_P,
	SPIT_H264_FRAME_TYPE_B,
	SPIT_H264_FRAME_TYPE_FORCE_ENUM = 0xffffffff,
};

struct spit_frame_desc {
	uint32_t seqnum;
	uint64_t timestamp; /* in ns */
	uint32_t latency; /* in us */
	enum spit_frame_type type;
	uint64_t data;
	uint32_t size;
	uint32_t checksum;
	/* H264 specific */
	uint64_t h264_timestamp;
	enum spit_h264_frame_type h264_frame_type;
};

enum spit_buffer_fifo_flags {
	SPIT_BUFFER_FIFO_FLAGS_NONE = 0,
	SPIT_BUFFER_FIFO_FLAGS_TRUNC = (1 << 0), /*!< Set when a frame can be
						  *   truncated in the FIFO ring
						  *   buffer
						  */
	SPIT_BUFFER_FIFO_FLAGS_CACHED = (1 << 1), /*!< Set when a FIFO buffer is
						   *   cached on Threadx and an
						   *   invalidation is done on
						   *   on frame in spit task (if
						   *   set, it is not necessary
						   *   to invalidate the cache
						   */
	SPIT_BUFFER_FIFO_FLAGS_INPUT = (1 << 2), /*!< Request buffer FIFO info
						  *   for input frame feeding.
						  *   This flag must be set by
						  *   client to request for
						  *   FIFO buffer for input
						  *   frame instead of output.
						  */

	SPIT_BUFFER_FIFO_FLAGS_ALL = 0xffffffff,
};

struct spit_buffer_fifo_info {
	enum spit_frame_type type;
	uint64_t buf_base;
	uint64_t buf_phys;
	uint32_t buf_size;
	enum spit_buffer_fifo_flags flags;
};

struct spit_params_h264 {
	uint8_t bitstream_format; /*!< H264 bitstream format:
				   *   see SPIT_H264_BITSTREAM_FORMAT_
				   */
	uint8_t profile_idc; /*!< H264 profile: see SPIT_H264_PROFILE_ */
	uint8_t level_idc; /*!< H264 level: see SPIT_H264_LEVEL_ */
	uint8_t entropy_coding; /*!< Entropy encoding mode: see
				 *   SPIT_H264_ENTROPY_CODING_MODE_
				 */
	uint8_t gop_m; /*!< Group of picture M (I/P frame interval) */
	uint16_t gop_n; /*!< Group of picture N (I frame interval) */
	uint8_t gop_struct; /*!< Group of pictures structure: see
			     *   SPIT_H264_GOP_STRUCTURE_
			     */
	uint8_t num_p_ref; /*!< P reference picture count */
	uint8_t num_b_ref; /*!< B reference picture count */
	uint32_t quality_level; /*!< Quality level */
	uint32_t bitrate_mode; /*!< Bitrate mode: see SPIT_H264_BITRATE_MODE_ */
	uint8_t vbr_complex_level; /*!< VBR complexity level */
	uint8_t vbr_percent; /*!< The percentage of average rate that will be
			      *   devoted to VBR (0 ~ 99). The rest is for CBR.
			      */
	uint16_t vbr_min_ratio; /*!< The minimum rate that VBR will not dip
				 *   below (0 ~ 100).
				 */
	uint16_t vbr_max_ratio; /*!< The maximum rate the vbr will not go above
				 * (100 ~ ).
				 */
	uint32_t idr_interval; /*!< IDR frame interval in GOP count */
	uint32_t bitrate; /*!< Target bitrate in bits/s */
	uint8_t num_slice; /*!< Number of slices per frame */
	uint8_t ir_period; /*!< Intra-refresh period */
	uint8_t ir_mode; /*!< Intra-refresh mode: see SPIT_H264_IR_MODE_ */
	uint8_t ir_length; /*!< Intra refresh length */
	int8_t ir_qp_adj; /*!< QP adjustment for the intra refresh zones
			   *   Range is +-51
			   */
	uint8_t poc_type; /*!< Picture order count type (0 default) */
	uint8_t ps_insert_mode; /*!< SPS/PPS insertion mode: see
				 *   SPIT_H264_PS_INSERTION_MODE_
				 */
	uint8_t aud_insert_mode; /*!< AUD insertion mode: see
				  *   SPIT_H264_AUD_INSERTION_MODE_
				  */
	uint8_t aqp; /*!< Adaptive QP */
	uint8_t qp_reduce_i_frame; /*!< QP reducer for I frame */
	uint32_t zmv_threshold; /*!< ZMV threshold */
	uint32_t cpb_size; /*!< User-defined CPB size when > 0 */
	uint8_t ref_frame_interval; /*!< Reference frame interval, frames
				     *   in between will be non-ref frames
				     */
	uint8_t disable_i4x4_pred; /*!< Disable intra 4x4 predictions */
	uint32_t timelapse_factor; /*!< Timelapse factor (used to increase /
				    *   decrease the frame rate reference used
				    *   by the encoder to fit the requested
				    *   bitrate). If the factor is greater than
				    *   1, the frame rate reference will be
				    *   higher and bitrate target will be
				    *   reached with more frames.
				    *   The value is x1000: for a factor of
				    *   x1.0, the value will be 1000.
				    */

	uint8_t blend_enable; /*!< Enable vertical camera stream blending into
			       *   encoder stream.
			       */
	uint16_t blend_offset_x; /*!< X offset for blending */
	uint16_t blend_offset_y; /*!< Y offset for blending */
};

struct spit_params_raw {
	enum spit_raw_format format;
};

struct spit_stream_conf {
	uint16_t width;
	uint16_t height;
	uint32_t fps_n;
	uint32_t fps_d;
	enum spit_frame_types frame_types;
	uint8_t input_conf;
	struct {
		struct spit_params_h264 h264;
		struct spit_params_raw raw;
	} params;
};

enum spit_input_state {
	SPIT_INPUT_STATE_STOPPED = 0,
	SPIT_INPUT_STATE_RUNNING,
	SPIT_INPUT_STATE_WAITING,
	SPIT_INPUT_STATE_ERROR,

	SPIT_INPUT_STATE_COUNT,
	SPIT_INPUT_STATE_FORCE_ENUM = 0xffffffff,
};

struct spit_input_status {
	enum spit_input_state state; /*!< State of current transcoding. The
				      *   stream must be stopped when an error
				      *   has occurred.
				      */
	uint32_t free_size; /*!< Free size available in the input FIFO buffer */
};

struct spit_resolution {
	uint16_t width;
	uint16_t height;
	uint32_t fps_n;
	uint32_t fps_d;
	uint8_t hdr;
};

struct spit_control_conf {
	enum spit_dsp_mode dsp_mode;
	struct spit_resolution input[SPIT_STREAM_TYPE_COUNT]; /*!< Resolution
		 * used for sensor output setup and used for DSP input (same as
		 * output when no DSP or scaler is available)
		 */
	struct spit_resolution output[SPIT_STREAM_TYPE_COUNT]; /*!< Resolution
		 * used for DSP output (same as input when no DSP or scaler is
		 * available)
		 */
};

struct spit_fps {
	uint32_t num;
	uint32_t den;
};

enum spit_ar {
	SPIT_AR_4X3 = (4 << 16) / 3,
	SPIT_AR_16X9 = (16 << 16) / 9,

	SPIT_AR_FORCE_ENUM = 0xffffffff,
};

#define SPIT_MODE_NO_HDR ((uint32_t) ~0)

struct spit_mode_fps {
	uint32_t mode_idx;
	uint32_t idx;
	uint32_t fps_n;
	uint32_t fps_d;
	uint32_t mode;
	uint32_t hdr_mode;
};

struct spit_mode {
	uint32_t idx;
	uint16_t offset_x;
	uint16_t offset_y;
	uint16_t width;
	uint16_t height;
	union {
		enum spit_ar ar;
		uint64_t align_ar;
	};
	uint32_t fps_count;
	union {
		struct spit_mode_fps *fps;
		uint64_t align;
	};
};

struct spit_sensor_settings {
	uint16_t width; /*!< Width of input crop on sensor array */
	uint16_t height; /*!< Height of input crop on sensor array */
	uint16_t top; /*!< Row count skipped before crop rectangle */
	uint16_t left; /*!< Column count skipped before crop rectangle */
	uint8_t binning_x; /*!< Binning mode used on X (1 for no binning) */
	uint8_t binning_y; /*!< Binning mode used on Y (1 for no binning) */
	uint8_t hdr_mode; /*!< HDR mode (0 if disabled) */
	uint16_t out_width; /*!< Width of sensor output */
	uint16_t out_height; /*!< Height of sensor output */
	uint16_t out_top; /*!< Row count skipped before output crop rectangle */
	uint16_t out_left; /*!< Column count skipped before output crop
			    *   rectangle
			    */
	uint64_t readout_time; /*!< Time (in ns) of line readout: time between
				*   read of two consecutive pixels on a colums.
				*/
	uint32_t fps_n; /*!< Numerator of frame rate */
	uint32_t fps_d; /*!< Denominator of frame rate */
};

struct spit_bitrate_change {
	uint32_t bitrate;
};

struct spit_ae_ev {
	int32_t ev_bias; /*!< Current Exposure value applied (x100) */
};

enum spit_ae_mode {
	SPIT_AE_MODE_AUTOMATIC_PREFER_ISO = 0,
	SPIT_AE_MODE_AUTOMATIC_PREFER_SHUTTER,
	SPIT_AE_MODE_MANUAL_ISO,
	SPIT_AE_MODE_MANUAL_SHUTTER,
	SPIT_AE_MODE_MANUAL,

	SPIT_AE_MODE_COUNT,
	SPIT_AE_MODE_FORCE_ENUM = 0xffffffff,
};

struct spit_ae_info {
	enum spit_stream_type type; /*!< Apply settings for all associated to
				     *   the control with this stream type
				     */
	enum spit_ae_mode mode; /*!< AE mode */
	uint32_t iso; /*!< Current ISO applied (Read-only) */
	uint32_t analog_gain; /*!< Current Analog gain applied
			       *   The value is x1000: for a gain if x1.0, the
			       *   the value will be 1000.
			       */
	uint32_t digital_gain; /*!< Current Digital gain applied */
	uint32_t shutter_time_us; /*!< Current shutter time applied (us) */
	uint32_t hdr_gain_ratio; /*!< Current gain ratio for HDR applied
				  *   The value is x1000: for a ratio of 1.0,
				  *   the value will be 1000. If value is 0,
				  *   the current ratio is not updated.
				  */
	uint32_t hdr_shutter_ratio; /*!< Current shutter rato for HDR applied
				     *   The value is x1000: for a ratio of 1.0,
				     *   the value will be 1000. If value is 0,
				     *   the current ratio is not updated.
				     */
};

enum spit_awb_mode {
	SPIT_AWB_MODE_AUTOMATIC = 0,
	SPIT_AWB_MODE_INCANDESCENT,
	SPIT_AWB_MODE_D4000,
	SPIT_AWB_MODE_D5000,
	SPIT_AWB_MODE_SUNNY,
	SPIT_AWB_MODE_CLOUDY,
	SPIT_AWB_MODE_D9000,
	SPIT_AWB_MODE_D10000,
	SPIT_AWB_MODE_FLASH,
	SPIT_AWB_MODE_FLUORESCENT,
	SPIT_AWB_MODE_FLUORESCENT_2,
	SPIT_AWB_MODE_FLUORESCENT_3,
	SPIT_AWB_MODE_FLUORESCENT_4,
	SPIT_AWB_MODE_WATER,
	SPIT_AWB_MODE_OUTDOOR,

	SPIT_AWB_MODE_COUNT,
	SPIT_AWB_MODE_FORCE_ENUM = 0xffffffff,
};

struct spit_awb_info {
	enum spit_awb_mode mode; /*!< AWB mode applied (only in auto mode) */
	uint8_t manual_mode; /*!< Enable / disable AWB manual mode) */
	uint32_t temperature; /*!< Temperature in Kelvin applied (only in manual) */
};

enum spit_img_style {
	SPIT_IMG_STYLE_STANDARD = 0,
	SPIT_IMG_STYLE_PLOG,

	SPIT_IMG_STYLE_COUNT,
	SPIT_IMG_STYLE_FORCE_ENUM = 0xffffffff,
};

struct spit_img_settings {
	uint32_t saturation; /*!< Saturate / desaturate image
			      * From 0 to 256 (default at 64) */
	float sharpness; /*!< Increase / decrease sharpness
			     * From 0.5 to 2 (default at 1) */
	uint32_t contrast; /*!< Increase / decrease contrast
			    * From 0 to 256 (default at 64) */
};

enum spit_flicker_mode {
	SPIT_FLICKER_MODE_AUTO = 0,
	SPIT_FLICKER_MODE_60_HZ,
	SPIT_FLICKER_MODE_50_HZ,
	SPIT_FLICKER_MODE_OFF,

	SPIT_FLICKER_MODE_FORCE_ENUM = 0xffffffff,
};

enum spit_dewarp_mode {
	SPIT_DEWARP_MODE_NONE = 0, /*!< Disable dewarp (none of anti-wobble or
				    * stabilization are available) */
	SPIT_DEWARP_MODE_FISHEYE, /*!< Use Fisheye dewarp preset */
	SPIT_DEWARP_MODE_RECTILINEAR, /*!< Use rectilinear dewarp preset */

	SPIT_DEWARP_MODE_COUNT,
	SPIT_DEWARP_MODE_FORCE_ENUM = 0xffffffff,
};

struct spit_dewarp_cfg {
	enum spit_dewarp_mode mode; /*!< Select Dewarp mode / preset */
	uint8_t anti_wobble_enable; /*!< Enable / disable anti-wobble feature */
	uint8_t stabilization_enable; /*!< Enable / disable stabilization feature */
	uint32_t fov; /*!< Field Of View to apply (in milli-degrees) */
};

struct spit_bracketing_cfg {
	uint8_t count; /*!< Number of frame to be captured (0 or 1: disabled)
			* Only 3, 5 or 7 are allowed.
			*/
	int32_t ev_bias[7]; /*!< Exposure value to apply (x100) */
};

struct spit_burst_cfg {
	uint8_t count; /*!< Number of frame to be captured (0: disabled) */
};

/*
 * Helpers
 */
static inline void spit_set_ptr(uint64_t *addr, unsigned char *ptr)
{
#if __SIZEOF_POINTER__ == 8
	*addr = (uint64_t) ptr;
#else
	*addr = (uint64_t) (uint32_t) ptr;
#endif
}

static inline unsigned char *spit_get_ptr(uint64_t addr)
{
#if __SIZEOF_POINTER__ == 8
	return (unsigned char *) addr;
#else
	return (unsigned char *) (uint32_t) addr;
#endif
}

static inline void spit_set_buffer_fifo_base(struct spit_buffer_fifo_info *info,
					     unsigned char *addr)
{
	spit_set_ptr(&info->buf_base, addr);
}

static inline void spit_set_buffer_fifo_phys(struct spit_buffer_fifo_info *info,
					     unsigned char *addr)
{
	spit_set_ptr(&info->buf_phys, addr);
}

static inline unsigned char *spit_get_buffer_fifo_base(
					     struct spit_buffer_fifo_info *info)
{
	return spit_get_ptr(info->buf_base);
}

static inline unsigned char *spit_get_buffer_fifo_phys(
					     struct spit_buffer_fifo_info *info)
{
	return spit_get_ptr(info->buf_phys);
}

static inline void spit_set_frame_data(struct spit_frame_desc *frame,
				       unsigned char *data)
{
	spit_set_ptr(&frame->data, data);
}

static inline unsigned char *spit_get_frame_data(struct spit_frame_desc *frame)
{
	return spit_get_ptr(frame->data);
}

static inline uint32_t spit_compute_frame_checksum(uint64_t fifo_base_addr,
			uint32_t fifo_size, uint64_t frame_base_addr,
			uint32_t frame_size)
{
	int i, j;
	uint32_t checksum, step, word_data, n_words = 32;
	uint64_t word_addr, fifo_buf_end = fifo_base_addr + fifo_size - 1;

	/* step 32bits aligned */
	step = frame_size / n_words;
	step &= ~3;

	/* checksum is 32bit xor done on 32 words in frame */
	checksum = 0;
	for (i = 0; i < n_words; i++) {
		word_addr = frame_base_addr + i * step;
		word_data = 0;
		for (j = 0; j < 4; j++) {
			/* take frame buffer overlap into account */
			if (word_addr > fifo_buf_end)
				word_addr -= fifo_size;

			word_data |= (*spit_get_ptr(word_addr)) << (8*j);
			word_addr++;
		}

		checksum ^= word_data;
	}

	return checksum;
}

/*
 * Error handling
 */
enum spit_error {
	SPIT_ERROR_NONE = 0,
	SPIT_ERROR_UNKNOWN = 0x800,
	SPIT_ERROR_NOT_AVAILABLE,
	SPIT_ERROR_BAD_FRAME_TYPE,
	SPIT_ERROR_BAD_FRAME_TYPES,
	SPIT_ERROR_NO_DSP_AVAILABLE,
	SPIT_ERROR_SET_DSP_MODE,
	SPIT_ERROR_STREAM_STARTED,
	SPIT_ERROR_STREAM_NOT_STARTED,
	SPIT_ERROR_STREAMS_STARTED,
	SPIT_ERROR_INVALID_MODE,
	SPIT_ERROR_UNAVAILABLE_MODE,
	SPIT_ERROR_PIPELINE_RESTART,
	SPIT_ERROR_START_VIDEO_ENCODER,
	SPIT_ERROR_STOP_VIDEO_ENCODER,
	SPIT_ERROR_BAD_H264_PARAMS,
	SPIT_ERROR_BAD_STREAM_TYPE,
	SPIT_ERROR_MODE_NOT_FOUND,
	SPIT_ERROR_MODE_FPS_NOT_FOUND,
	SPIT_ERROR_OLD_FRAME,
	SPIT_ERROR_BAD_FPS,
	SPIT_ERROR_QUEUE_FULL,
	SPIT_ERROR_INVALID_FRAME,

	SPIT_ERROR_FORCE_ENUM = 0xffffffff,
};

static inline int spit_is_error(int err)
{
	return (err >= SPIT_ERROR_UNKNOWN);
}

#define DECLARE_SPIT_ERROR_STRING(name)\
const struct {\
	enum spit_error err;\
	const char *str;\
} name[] = {\
	{SPIT_ERROR_UNKNOWN, "unknown error"},\
	{SPIT_ERROR_NOT_AVAILABLE, "sensor / stream is not available"},\
	{SPIT_ERROR_BAD_FRAME_TYPE, "bad frame type"},\
	{SPIT_ERROR_BAD_FRAME_TYPES, "bad frame types"},\
	{SPIT_ERROR_NO_DSP_AVAILABLE, "no DSP available"},\
	{SPIT_ERROR_SET_DSP_MODE, "cannot set DSP mode"},\
	{SPIT_ERROR_STREAM_STARTED, "stream is already started"},\
	{SPIT_ERROR_STREAM_NOT_STARTED, "stream is not started"},\
	{SPIT_ERROR_STREAMS_STARTED, "some stream are started"},\
	{SPIT_ERROR_INVALID_MODE, "invalid output mode"},\
	{SPIT_ERROR_UNAVAILABLE_MODE, "unavailable sensor mode"},\
	{SPIT_ERROR_PIPELINE_RESTART, "failed to restart pipeline"},\
	{SPIT_ERROR_START_VIDEO_ENCODER, "failed to start video encoder"},\
	{SPIT_ERROR_STOP_VIDEO_ENCODER, "failed to stop video encoder"},\
	{SPIT_ERROR_BAD_H264_PARAMS, "bad H264 encoder parameters"},\
	{SPIT_ERROR_BAD_STREAM_TYPE, "bad stream type"},\
	{SPIT_ERROR_MODE_NOT_FOUND, "mode not found (bad index)"},\
	{SPIT_ERROR_MODE_FPS_NOT_FOUND, "mode fps not found (bad index)"},\
	{SPIT_ERROR_OLD_FRAME, "old frame cannot be released"},\
	{SPIT_ERROR_BAD_FPS, "bad frame rate"},\
	{SPIT_ERROR_QUEUE_FULL, "queue is full"},\
	{SPIT_ERROR_INVALID_FRAME, "invalid frame"},\
}

/*
 * H264 encoder parameters
 */

/* H264 output format */
#define SPIT_H264_BITSTREAM_FORMAT_BYTE_STREAM	0
#define SPIT_H264_BITSTREAM_FORMAT_AVCC		1

/* H264 profile */
#define SPIT_H264_PROFILE_MAIN			77

/* H264 level
 * see Rec. ITU-T H.264 Annex A, table A-1 */
#define SPIT_H264_LEVEL_3_0			30
#define SPIT_H264_LEVEL_3_0_MAX_FS		1620
#define SPIT_H264_LEVEL_3_0_MAX_MBPS		40500
#define SPIT_H264_LEVEL_3_0_MAX_BITRATE		(10 * 1000 * 1000)
#define SPIT_H264_LEVEL_3_1			31
#define SPIT_H264_LEVEL_3_1_MAX_FS		3600
#define SPIT_H264_LEVEL_3_1_MAX_MBPS		108000
#define SPIT_H264_LEVEL_3_1_MAX_BITRATE		(14 * 1000 * 1000)
#define SPIT_H264_LEVEL_3_2			32
#define SPIT_H264_LEVEL_3_2_MAX_FS		5120
#define SPIT_H264_LEVEL_3_2_MAX_MBPS		216000
#define SPIT_H264_LEVEL_3_2_MAX_BITRATE		(20 * 1000 * 1000)
#define SPIT_H264_LEVEL_4_0			40
#define SPIT_H264_LEVEL_4_0_MAX_FS		8192
#define SPIT_H264_LEVEL_4_0_MAX_MBPS		245760
#define SPIT_H264_LEVEL_4_0_MAX_BITRATE		(20 * 1000 * 1000)
#define SPIT_H264_LEVEL_4_1			41
#define SPIT_H264_LEVEL_4_1_MAX_FS		8192
#define SPIT_H264_LEVEL_4_1_MAX_MBPS		245760
#define SPIT_H264_LEVEL_4_1_MAX_BITRATE		(50 * 1000 * 1000)
#define SPIT_H264_LEVEL_4_2			42
#define SPIT_H264_LEVEL_4_2_MAX_FS		8704
#define SPIT_H264_LEVEL_4_2_MAX_MBPS		522240
#define SPIT_H264_LEVEL_4_2_MAX_BITRATE		(50 * 1000 * 1000)
#define SPIT_H264_LEVEL_5_0			50
#define SPIT_H264_LEVEL_5_0_MAX_FS		22080
#define SPIT_H264_LEVEL_5_0_MAX_MBPS		589824
#define SPIT_H264_LEVEL_5_0_MAX_BITRATE		(135 * 1000 * 1000)
#define SPIT_H264_LEVEL_5_1			51
#define SPIT_H264_LEVEL_5_1_MAX_FS		36864
#define SPIT_H264_LEVEL_5_1_MAX_MBPS		983040
#define SPIT_H264_LEVEL_5_1_MAX_BITRATE		(240 * 1000 * 1000)
#define SPIT_H264_LEVEL_5_2			52
#define SPIT_H264_LEVEL_5_2_MAX_FS		36864
#define SPIT_H264_LEVEL_5_2_MAX_MBPS		2073600
#define SPIT_H264_LEVEL_5_2_MAX_BITRATE		(240 * 1000 * 1000)

/* H264 entropy encoding mode */
#define SPIT_H264_ENTROPY_CODING_MODE_CAVLC	0
#define SPIT_H264_ENTROPY_CODING_MODE_CABAC	1

/* H264 Group Of Pictures structure (GOP)
 *  - SIMPLE: simple structure,
 *  - HIERB: dyadic hierarchical B prediction structure.
 */
#define SPIT_H264_GOP_STRUCTURE_SIMPLE		0
#define SPIT_H264_GOP_STRUCTURE_HIERB		1

/* H264 bitrate mode
 *  - CBR: constant bitrate,
 *  - VBR: variable bitrate.
 */
#define SPIT_H264_BITRATE_MODE_CBR		1
#define SPIT_H264_BITRATE_MODE_VBR		3

/* H264 intra-refresh mode
 *  - SPIT_H264_IR_MODE_FIXED: fixed scan
 *  - SPIT_H264_IR_MODE_RANDOM: random scan
 *  - SPIT_H264_IR_MODE_FIRST_FRAMES: IR in the first 'ir_length' frames
 *  - SPIT_H264_IR_MODE_CENTER: start scan from the center
 *  - SPIT_H264_IR_MODE_CENTER_FIRST_FRAMES: start scan from the center,
 *                                           IR in the first 'ir_length' frames
 */
#define SPIT_H264_IR_MODE_FIXED			0
#define SPIT_H264_IR_MODE_RANDOM		1
#define SPIT_H264_IR_MODE_FIRST_FRAMES		2
#define SPIT_H264_IR_MODE_CENTER		3
#define SPIT_H264_IR_MODE_CENTER_FIRST_FRAMES	4

/* H264 SPS/PPS insertion mode
 *  - SPIT_H264_PS_INSERTION_MODE_IDR: only on I/IDR
 *  - SPIT_H264_PS_INSERTION_MODE_AUTO: automatically insert every
 *                'idr_interval' frames (use in intra refresh only)
 */
#define SPIT_H264_PS_INSERTION_MODE_IDR		0
#define SPIT_H264_PS_INSERTION_MODE_AUTO	1

/* H264 AUD insertion mode
 *  - SPIT_H264_AUD_INSERTION_MODE_DISABLED: AUD disabled
 *  - SPIT_H264_AUD_INSERTION_MODE_EXTRA: AUD enabled + extra AUD
 *                                            at the end of frame
 *  - SPIT_H264_AUD_INSERTION_MODE_ENABLED: AUD enabled
 */
#define SPIT_H264_AUD_INSERTION_MODE_DISABLED	0
#define SPIT_H264_AUD_INSERTION_MODE_EXTRA	1
#define SPIT_H264_AUD_INSERTION_MODE_ENABLED	2

#endif /* _SPIT_DEFS_H_ */
