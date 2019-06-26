LOCAL_PATH := $(call my-dir)

###############################################################################
# ffmpeg libav
###############################################################################

include $(CLEAR_VARS)

LOCAL_MODULE := ffmpeg-libav
LOCAL_CATEGORY_PATH := multimedia/ffmpeg

LOCAL_DESCRIPTION := cross-platform tools and libraries to convert, manipulate and stream a wide range of multimedia formats and protocols

LOCAL_CONFIG_FILES := aconfig.in
$(call load-config)

ifeq ("$(TARGET_ARCH)","x64")
  LOCAL_AUTOTOOLS_CONFIGURE_ARGS += --arch="x86_64"
else
  LOCAL_AUTOTOOLS_CONFIGURE_ARGS += --arch="$(TARGET_ARCH)"
endif

# Main compilation options
LOCAL_AUTOTOOLS_CONFIGURE_ARGS += \
	--enable-shared \
	--enable-cross-compile \
	--enable-optimizations \
	--target-os="linux" \
	--cross-prefix="$(TARGET_CROSS)"

# Components options
LOCAL_AUTOTOOLS_CONFIGURE_ARGS += \
	--disable-orc \
	--disable-avconv \
	--disable-avplay \
	--disable-avprobe \
	--disable-avserver \
	--disable-avdevice \
	--disable-avresample \
	--disable-filters \
	--disable-network \
	--disable-yasm \
	--disable-bzlib

# Lisencing options
LOCAL_AUTOTOOLS_CONFIGURE_ARGS += \
	--disable-gpl \
	--disable-version3 \
	--disable-nonfree

# User selected components
#
# By default all decoder/encoders/parser/mux/demux are disabled to avoid
# compiling extra stuff.
# When a user need a particular comportment it shall add it as new configuration
# in "aconfig.in" and add a entry in this section
LOCAL_AUTOTOOLS_CONFIGURE_ARGS += --disable-everything

ifdef CONFIG_FFMPEG_HEVC_DECODING
LOCAL_AUTOTOOLS_CONFIGURE_ARGS += \
	--enable-avcodec \
	--enable-decoder=hevc
endif

ifdef CONFIG_FFMPEG_AVC_DECODING
LOCAL_AUTOTOOLS_CONFIGURE_ARGS += \
	--enable-avcodec \
	--enable-decoder=h264
endif

ifdef CONFIG_FFMPEG_AAC_ENCODING
LOCAL_AUTOTOOLS_CONFIGURE_ARGS += \
	--enable-avcodec \
	--enable-encoder=aac
endif

ifdef CONFIG_FFMPEG_MOV_FORMAT
LOCAL_AUTOTOOLS_CONFIGURE_ARGS += \
	--enable-demuxer=mov \
	--enable-muxer=mov
endif

ifdef CONFIG_FFMPEG_PROGRAMS
LOCAL_AUTOTOOLS_CONFIGURE_ARGS += \
	--enable-protocol=file
else
LOCAL_AUTOTOOLS_CONFIGURE_ARGS += \
	--disable-programs
endif

ifdef CONFIG_FFMPEG_ENABLE_CUVID
# WARNING: non-free software is enabled in this configuration,
# the software must not be distributed with cuvid enabled.

LOCAL_AUTOTOOLS_CONFIGURE_ARGS += \
	--enable-decoder=h264_cuvid \
	--enable-decoder=hevc_cuvid \
	--enable-nvenc \
	--enable-cuda \
	--enable-cuvid \
	--enable-nonfree \
	--extra-cflags=-I/usr/local/cuda/include \
	--extra-ldflags=-L/usr/local/cuda/lib64
endif

ifdef CONFIG_FFMPEG_ENABLE_VDPAU
LOCAL_AUTOTOOLS_CONFIGURE_ARGS += \
	--enable-decoder=h264_vdpau \
	--enable-vdpau
endif

# licence check (shall be the last rule)
ifneq (,$(filter --enable-nonfree --enable-version3 --enable-nonfree, \
 	$(LOCAL_AUTOTOOLS_CONFIGURE_ARGS)))
$(warning some options: "$(filter --enable-nonfree --enable-version3 \
 	--enable-nonfree, $(LOCAL_AUTOTOOLS_CONFIGURE_ARGS))" \
	are not compatible with a release)
endif

# Export libraries
LOCAL_EXPORT_LDLIBS = \
	-lavcodec \
	-lavutil \
	-lavformat \
	-lavfilter \
	-lswresample \
	-lswscale

LOCAL_LIBRARIES := zlib


include $(BUILD_AUTOTOOLS)

