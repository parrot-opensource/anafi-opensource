LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := hwtoolbox
LOCAL_DESCRIPTION := Parrot hardware toolbox
LOCAL_CATEGORY_PATH := devel

LOCAL_ARCHIVE_VERSION := 4.2.2
LOCAL_ARCHIVE := memtester-$(LOCAL_ARCHIVE_VERSION).tar.gz
LOCAL_ARCHIVE_SUBDIR := memtester-$(LOCAL_ARCHIVE_VERSION)

# Patch needed memtester sources
define LOCAL_ARCHIVE_CMD_POST_UNPACK
	$(Q) sed -i 's/main/memtester_main/g' \
		$(PRIVATE_ARCHIVE_UNPACK_DIR)/$(PRIVATE_ARCHIVE_SUBDIR)/memtester.c
	$(Q) sed -i 's/exit/return/g' \
		$(PRIVATE_ARCHIVE_UNPACK_DIR)/$(PRIVATE_ARCHIVE_SUBDIR)/memtester.c
	$(Q) sed -i 's/.. doesn.t return ../return 0\;/g' \
		$(PRIVATE_ARCHIVE_UNPACK_DIR)/$(PRIVATE_ARCHIVE_SUBDIR)/memtester.c
	$(Q) sed -i 's/void usage/int usage/g' \
		$(PRIVATE_ARCHIVE_UNPACK_DIR)/$(PRIVATE_ARCHIVE_SUBDIR)/memtester.c
	$(Q) patch -p1 -d $(PRIVATE_ARCHIVE_UNPACK_DIR)/$(PRIVATE_ARCHIVE_SUBDIR) < \
		$(PRIVATE_PATH)/memtester_md5sum.diff
endef

LOCAL_C_INCLUDES := $(LOCAL_PATH)/mxt_src

LOCAL_CFLAGS := \
	-DMXT_VERSION=\"1\" \
	-Wno-missing-prototypes \
	-Wno-unused-variable \
	-Wno-logical-op \
	-Wno-missing-field-initializers \
	-Wno-sign-compare \
	-Wno-type-limits \
	-Wno-discarded-qualifiers \
	-Wno-strict-aliasing \
	-Wno-pointer-arith \
	-Wno-format-security \
	-Wno-unused-result \
	-Wno-format-nonliteral \
	-Wno-format

LOCAL_SRC_FILES := \
	hwtoolbox.c \
	hwtoolbox_gpio.c \
	hwtoolbox_pwm.c \
	hwtoolbox_i2c.c \
	hwtoolbox_audio.c \
	hwtoolbox_endurance.c \
	hwtoolbox_powersucker.c \
	hwtoolbox_touch.c \
	hwtoolbox_valid_fb.c \
	logo-parrot-france.c \
	hwtoolbox_chip.c \
	hwtoolbox_menu.c \
	hwtoolbox_utils.c \
	hwtoolbox_ethernet.c \
	hwtoolbox_o3.c \
	hwtoolbox_crc_utils.c \
	hwtoolbox_uart.c \
	mxt_src/mxt-app/mxt_app.c \
	mxt_src/mxt-app/touch_app.c \
	mxt_src/mxt-app/bootloader.c \
	mxt_src/mxt-app/serial_data.c \
	mxt_src/mxt-app/signal.c \
	mxt_src/mxt-app/gr.c \
	mxt_src/mxt-app/buffer.c \
	mxt_src/mxt-app/self_test.c \
	mxt_src/mxt-app/diagnostic_data.c \
	mxt_src/mxt-app/self_cap.c \
	mxt_src/mxt-app/menu.c \
	mxt_src/libmaxtouch/utilfuncs.c \
	mxt_src/libmaxtouch/libmaxtouch.c \
	mxt_src/libmaxtouch/sysfs/sysfs_device.c \
	mxt_src/libmaxtouch/log.c \
	mxt_src/libmaxtouch/info_block.c \
	mxt_src/libmaxtouch/i2c_dev/i2c_dev_device.c \
	mxt_src/libmaxtouch/sysfs/dmesg.c \
	mxt_src/libmaxtouch/hidraw/hidraw_device.c \
	mxt_src/libmaxtouch/msg.c \
	mxt_src/libmaxtouch/config.c \
	mxt_src/libmaxtouch/sysfs/sysinfo.c

ifeq ("$(TARGET_CPU_HAS_NEON)","1")
  LOCAL_CFLAGS += -DTARGET_CPU_HAS_NEON

  ifeq ("$(TARGET_ARCH)","arm")
    LOCAL_SRC_FILES += burnCortexA9.s
  else ifeq ("$(TARGET_ARCH)","aarch64")
    LOCAL_SRC_FILES += cpuburn-a53.S
  endif

endif

LOCAL_GENERATED_SRC_FILES := $(addprefix $(LOCAL_ARCHIVE_SUBDIR)/, \
	memtester.c \
	tests.c \
)

LOCAL_LDFLAGS := -static

LOCAL_CONDITIONAL_LIBRARIES := OPTIONAL:alsa-lib-static

include $(BUILD_EXECUTABLE)
