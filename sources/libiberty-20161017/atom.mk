
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := libiberty
LOCAL_DESCRIPTION := Utility library used by various GNU programs
LOCAL_CATEGORY_PATH := libs

LOCAL_AUTOTOOLS_VERSION := 20161017
LOCAL_AUTOTOOLS_ARCHIVE := libiberty_$(LOCAL_AUTOTOOLS_VERSION).tar.bz2
LOCAL_AUTOTOOLS_SUBDIR := libiberty-$(LOCAL_AUTOTOOLS_VERSION)/libiberty

LOCAL_AUTOTOOLS_CONFIGURE_ARGS := \
	--enable-install-libiberty

include $(BUILD_AUTOTOOLS)

