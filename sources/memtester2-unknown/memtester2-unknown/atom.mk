LOCAL_PATH := $(call my-dir)

###############################################################################
# memtester
###############################################################################

include $(CLEAR_VARS)

LOCAL_MODULE := memtester2
LOCAL_DESCRIPTION := A userspace utility for testing the memory subsystem for \
	faults (with parrot modification)
LOCAL_CATEGORY_PATH := devel

LOCAL_CFLAGS := \
	-Wno-pointer-arith \
	-Wno-sign-compare

LOCAL_SRC_FILES := \
	memtester.c \
	tests.c

include $(BUILD_EXECUTABLE)
