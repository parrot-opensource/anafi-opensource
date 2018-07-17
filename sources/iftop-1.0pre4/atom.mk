
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := iftop
LOCAL_DESCRIPTION := displays bandwidth usage information on an network \
  interface
LOCAL_CATEGORY_PATH := network/tools

LOCAL_AUTOTOOLS_VERSION := 1.0pre4
LOCAL_AUTOTOOLS_ARCHIVE := $(LOCAL_MODULE)_1.0~pre4.orig.tar.gz
LOCAL_AUTOTOOLS_SUBDIR := $(LOCAL_MODULE)-$(LOCAL_AUTOTOOLS_VERSION)

LOCAL_LIBRARIES := \
  ncurses libpcap

include $(BUILD_AUTOTOOLS)

