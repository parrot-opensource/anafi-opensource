LOCAL_PATH := $(call my-dir)

###############################################################################
# mtd
###############################################################################

include $(CLEAR_VARS)

LOCAL_MODULE := mtd
LOCAL_DESCRIPTION := MTD, UBI and UBIFS userland tools
LOCAL_CATEGORY_PATH := fs

LOCAL_LIBRARIES := zlib util-linux-ng

LOCAL_AUTOTOOLS_VERSION := 2.0.0
LOCAL_AUTOTOOLS_ARCHIVE := mtd-utils-$(LOCAL_AUTOTOOLS_VERSION).tar.gz
LOCAL_AUTOTOOLS_SUBDIR := mtd-utils-$(LOCAL_AUTOTOOLS_VERSION)

LOCAL_AUTOTOOLS_CONFIGURE_ARGS := \
	--without-lzo

define LOCAL_CMD_BOOTSTRAP
	$(Q) cd $(PRIVATE_SRC_DIR) && \
		./autogen.sh
endef

include $(BUILD_AUTOTOOLS)
