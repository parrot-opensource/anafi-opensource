LOCAL_PATH := $(call my-dir)

###############################################################################
#  lz4
###############################################################################
include $(CLEAR_VARS)
LOCAL_MODULE := liblz4
LOCAL_DESCRIPTION := Extremely Fast Compression algorithm
LOCAL_CATEGORY_PATH := libs
LOCAL_ARCHIVE_VERSION := r131
LOCAL_ARCHIVE := lz4-$(LOCAL_ARCHIVE_VERSION).tar.gz
LOCAL_ARCHIVE_SUBDIR := lz4-$(LOCAL_ARCHIVE_VERSION)
LOCAL_ARCHIVE_PATCHES := cmake.patch
LOCAL_EXPORT_LDLIBS := -llz4
# lz4 command line tools are actually GPL
define LOCAL_ARCHIVE_CMD_POST_UNPACK
	$(Q) cp -af $(PRIVATE_PATH)/.MODULE_LICENSE_GPL.programs \
		$(PRIVATE_SRC_DIR)/programs/.MODULE_LICENSE_GPL
endef

include $(BUILD_CMAKE)
