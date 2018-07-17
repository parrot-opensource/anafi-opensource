LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := htop
LOCAL_DESCRIPTION := Interactive process viewer for Unix
LOCAL_CATEGORY_PATH := tools

LOCAL_AUTOTOOLS_VERSION := 2.0.1
LOCAL_AUTOTOOLS_ARCHIVE := $(LOCAL_MODULE)-$(LOCAL_AUTOTOOLS_VERSION).tar.gz
LOCAL_AUTOTOOLS_SUBDIR := $(LOCAL_MODULE)-$(LOCAL_AUTOTOOLS_VERSION)

LOCAL_AUTOTOOLS_CONFIGURE_ARGS := \
	--disable-unicode \
	--enable-shared \
	--enable-linux-affinity

LOCAL_AUTOTOOLS_PATCHES := \
	configure-ncurses.patch

LOCAL_LIBRARIES := ncurses

include $(BUILD_AUTOTOOLS)
