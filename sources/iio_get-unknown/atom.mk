LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := iio_get
LOCAL_DESCRIPTION :=
LOCAL_CATEGORY_PATH := utils/iio

LOCAL_SRC_FILES := $(call all-c-files-under,.)

LOCAL_LIBRARIES := libiio

include $(BUILD_EXECUTABLE)
