
# Linux, h22 cpu
ifeq ("$(TARGET_OS)","linux")

LOCAL_PATH := $(call my-dir)

ifeq ("$(TARGET_CPU)","h22")

ifndef BUILD_LINUX
$(error update alchemy to version 1.0.5 or more)
endif

# Force 64-bit compilation of kernel (userspace can be 32-bit or 64-bit)
TARGET_LINUX_ARCH := aarch64
ifndef TARGET_LINUX_CROSS
  TARGET_LINUX_CROSS := /opt/gcc-linaro-6.3-2017.02-aarch64-linux-gnu/bin/aarch64-linux-gnu-
endif


include $(CLEAR_VARS)

LOCAL_MODULE := linux
LOCAL_CATEGORY_PATH := system

LINUX_EXPORTED_HEADERS := \
	$(LOCAL_PATH)/include/linux/aipc_msg.h \
	$(LOCAL_PATH)/include/linux/AmbaIPC_Rpc_Def.h \
	$(LOCAL_PATH)/include/linux/pgimbal_interface.h \
	$(LOCAL_PATH)/include/linux/pshm_interface.h \
	$(LOCAL_PATH)/include/linux/iio/bldc/servo_messages.h \
	$(LOCAL_PATH)/include/linux/spit.h \
	$(LOCAL_PATH)/include/linux/spit_defs.h \
	$(LOCAL_PATH)/include/linux/parrot/amba_stepper.h


# Linux configuration file
LINUX_DEFAULT_CONFIG_TARGET := ambarella_h22_ambalink_defconfig

include $(BUILD_LINUX)

include $(CLEAR_VARS)

LOCAL_MODULE := perf
LOCAL_CATEGORY_PATH := devel

LOCAL_COPY_FILES := \
	parrot/perfconfig:etc/perfconfig

include $(BUILD_LINUX)

endif

# when building for native target,
# some headers from linux are required by other modules
ifeq ("$(TARGET_OS_FLAVOUR:-chroot=)","native")

include $(CLEAR_VARS)

LOCAL_MODULE := linux-headers-h22
LOCAL_INSTALL_HEADERS := include/linux/pgimbal_interface.h:usr/include/linux/

include $(BUILD_PREBUILT)

endif
endif
