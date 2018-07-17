LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := ulogger-kernel
LOCAL_MODULE_FILENAME := ulogger.ko
LOCAL_DESCRIPTION := ulogger kernel module for ulog
LOCAL_CATEGORY_PATH := utils
LOCAL_SRC_FILES := ulogger.c
include $(BUILD_LINUX_MODULE)

