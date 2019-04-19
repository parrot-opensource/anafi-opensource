LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := iio_generic_buffer
LOCAL_DESCRIPTION :=
LOCAL_CATEGORY_PATH := utils/iio
LOCAL_CFLAGS += -D_GNU_SOURCE

LOCAL_SRC_FILES := generic_buffer.c iio_utils.c

include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)

LOCAL_MODULE := lsiio
LOCAL_DESCRIPTION :=
LOCAL_CATEGORY_PATH := utils/iio
LOCAL_CFLAGS += -D_GNU_SOURCE

LOCAL_SRC_FILES := lsiio.c iio_utils.c

include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)

LOCAL_MODULE := iio_event_monitor
LOCAL_DESCRIPTION :=
LOCAL_CATEGORY_PATH := utils/iio
LOCAL_CFLAGS += -D_GNU_SOURCE

LOCAL_SRC_FILES := iio_event_monitor.c iio_utils.c

LOCAL_LIBRARIES := linux

include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)

LOCAL_MODULE := iio_dump_sensors
LOCAL_DESCRIPTION := Log all sensors to files
LOCAL_CATEGORY_PATH := utils/iio
LOCAL_COPY_FILES := iio_dump_sensors.sh:usr/bin/iio_dump_sensors
include $(BUILD_CUSTOM)
