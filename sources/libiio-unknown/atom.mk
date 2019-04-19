LOCAL_PATH := $(call my-dir)

COMMON_VERSIONS_CFLAGS := \
	-DLIBIIO_VERSION_GIT=\"$(shell cd $(LOCAL_PATH); git describe --always --dirty)\" \
	-DLIBIIO_VERSION_MAJOR=0 \
	-DLIBIIO_VERSION_MINOR=8 \

COMMON_IIO_CFLAGS := \
	-DLOG_LEVEL=Info_L \
	-DWITH_LOCAL_BACKEND=1 \
	-DWITH_XML_BACKEND=1 \
	-DWITH_NETWORK_BACKEND=1 \
	-DWITH_NETWORK_EVENTFD=1 \
	-DWITH_HAS_PIPE2=1 \
	-DHAS_STRDUP=1 \
	-DHAS_STRERROR_R=1 \
	-DHAVE_IPV6=1 \
	-DLIBIIO_EXPORTS=1 \
	-D_GNU_SOURCE=1 \
	-D_POSIX_C_SOURCE=200809L \
	-fvisibility=hidden


###############################################################################
# libiio
###############################################################################

include $(CLEAR_VARS)

LOCAL_MODULE := libiio
LOCAL_DESCRIPTION := Library for Linux Industrial I/O (IIO) devices
LOCAL_CATEGORY_PATH := libs/libiio

LOCAL_LIBRARIES := libxml2

LOCAL_SRC_FILES := \
	backend.c \
	buffer.c \
	channel.c \
	context.c \
	device.c \
	iiod-client.c \
	local.c \
	lock.c \
	network.c \
	plugins.c \
	scan.c \
	utilities.c \
	xml.c

LOCAL_CFLAGS := $(COMMON_VERSIONS_CFLAGS) $(COMMON_IIO_CFLAGS)
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)
LOCAL_EXPORT_CFLAGS := -DHAVE_LIBIIO \
		       $(COMMON_VERSIONS_CFLAGS)
LOCAL_LDLIBS := -ldl

include $(BUILD_LIBRARY)

###############################################################################
# iiod
###############################################################################

include $(CLEAR_VARS)

LOCAL_MODULE := iiod
LOCAL_DESCRIPTION := Linux Industrial I/O (IIO) devices network daemon
LOCAL_CATEGORY_PATH := libs/libiio

LOCAL_LIBRARIES := libiio

IIOD_BUILD_DIR := $(call local-get-build-dir)

LOCAL_PREREQUISITES += \
	$(IIOD_BUILD_DIR)/parser.h

$(IIOD_BUILD_DIR)/lexer.c: $(LOCAL_PATH)/iiod/lexer.l
	$(Q) flex -o $@  $<

$(IIOD_BUILD_DIR)/parser.c \
$(IIOD_BUILD_DIR)/parser.h: $(LOCAL_PATH)/iiod/parser.y
	$(Q) $(BISON_BIN) -d -o $(IIOD_BUILD_DIR)/parser.c $<

LOCAL_SRC_FILES := \
	iiod/iiod.c \
	iiod/ops.c \
	iiod/thread-pool.c

LOCAL_GENERATED_SRC_FILES := \
	parser.c \
	lexer.c

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/iiod \
	$(IIOD_BUILD_DIR)

LOCAL_CFLAGS := $(COMMON_VERSIONS_CFLAGS) $(COMMON_IIO_CFLAGS)

include $(BUILD_EXECUTABLE)

###############################################################################
# libiio-plugins-private
###############################################################################

include $(CLEAR_VARS)

LOCAL_MODULE := libiio-plugins-private
LOCAL_DESCRIPTION := Export of libiio headers for plugin implementors
LOCAL_CATEGORY_PATH := libs/libiio

LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)

include $(BUILD_CUSTOM)

###############################################################################
# iio_info
###############################################################################

include $(CLEAR_VARS)

LOCAL_MODULE := iio_info
LOCAL_CATEGORY_PATH := libs/libiio
LOCAL_DESCRIPTION := get info on iio devices

LOCAL_CFLAGS := $(COMMON_VERSIONS_CFLAGS) $(COMMON_IIO_CFLAGS)

LOCAL_SRC_FILES := \
	tests/$(LOCAL_MODULE).c

LOCAL_LIBRARIES := \
	libiio

include $(BUILD_EXECUTABLE)

###############################################################################
# iio_readdev
###############################################################################

include $(CLEAR_VARS)

LOCAL_MODULE := iio_readdev
LOCAL_CATEGORY_PATH := libs/libiio
LOCAL_DESCRIPTION := read samples from an iio device

LOCAL_CFLAGS := $(COMMON_VERSIONS_CFLAGS) $(COMMON_IIO_CFLAGS)

LOCAL_SRC_FILES := \
	tests/$(LOCAL_MODULE).c

LOCAL_LIBRARIES := \
	libiio

include $(BUILD_EXECUTABLE)

###############################################################################
# iio_reg
###############################################################################

include $(CLEAR_VARS)

LOCAL_MODULE := iio_reg
LOCAL_CATEGORY_PATH := libs/libiio
LOCAL_DESCRIPTION := read from / write to iio device hardware registers

LOCAL_CFLAGS := $(COMMON_VERSIONS_CFLAGS) $(COMMON_IIO_CFLAGS)

LOCAL_SRC_FILES := \
	tests/$(LOCAL_MODULE).c

LOCAL_LIBRARIES := \
	libiio

include $(BUILD_EXECUTABLE)

###############################################################################
# iio_genxml
###############################################################################

include $(CLEAR_VARS)

LOCAL_MODULE := iio_genxml
LOCAL_CATEGORY_PATH := libs/libiio
LOCAL_DESCRIPTION := get the iio devices tree xml description

LOCAL_CFLAGS := $(COMMON_VERSIONS_CFLAGS) $(COMMON_IIO_CFLAGS)

LOCAL_SRC_FILES := \
	tests/$(LOCAL_MODULE).c

LOCAL_LIBRARIES := \
	libiio

include $(BUILD_EXECUTABLE)

###############################################################################
# libiio-test-plugin
###############################################################################

include $(CLEAR_VARS)

LOCAL_MODULE := libiio-test-plugin
LOCAL_CATEGORY_PATH := libs/libiio
LOCAL_DESCRIPTION := libiio plugin implementing a backend exposing a false iio \
	device tree, for testing purpose

LOCAL_DESTDIR := usr/lib/libiio-plugins

LOCAL_SRC_FILES := \
	tests/iio_test_plugin.c

LOCAL_LIBRARIES := \
	libiio \
	libiio-plugins-private

include $(BUILD_SHARED_LIBRARY)

###############################################################################
# libiio_test_plugin_main
###############################################################################

include $(CLEAR_VARS)

LOCAL_MODULE := iio_test
LOCAL_CATEGORY_PATH := libs/libiio
LOCAL_DESCRIPTION := program running automated tests by mean of the \
	iio_test_plugin

LOCAL_SRC_FILES := \
	tests/iio_test_plugin_main.c

LOCAL_LIBRARIES := \
	libiio

LOCAL_REQUIRED_MODULES := \
	libiio_test_plugin

include $(BUILD_EXECUTABLE)


###############################################################################
# copy of python wrapper
###############################################################################

include $(CLEAR_VARS)
LOCAL_MODULE := libiio-python-wrapper
LOCAL_DESCRIPTION := just copy python wrapper
LOCAL_CATEGORY_PATH := libs/libiio
$(foreach __f,$(call all-files-under,bindings/python,.py), \
	$(eval LOCAL_COPY_FILES += $(__f):usr/lib/python/iio/))
include $(BUILD_CUSTOM)
