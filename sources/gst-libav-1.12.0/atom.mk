LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := gst-libav
LOCAL_LIBRARIES := gstreamer gst-plugins-base ffmpeg-libav
LOCAL_CATEGORY_PATH := multimedia/gstreamer

LOCAL_DESCRIPTION := GStreamer Streaming-media framework plug-in using libav (FFmpeg).

# --disable-maintainer-mode is to avoid regenerating configure/Makefile.in
# that we store in git repo
LOCAL_AUTOTOOLS_CONFIGURE_ARGS := \
	--disable-maintainer-mode \
	--disable-orc \
	--disable-gtk-doc-html \
	--enable-lgpl \
	--with-system-libav

include $(BUILD_AUTOTOOLS)
