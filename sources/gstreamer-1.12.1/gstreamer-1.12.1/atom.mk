
LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := gstreamer
LOCAL_DESCRIPTION := GStreamer streaming media framework runtime
LOCAL_CATEGORY_PATH := multimedia/gstreamer
LOCAL_LIBRARIES := glib

LOCAL_CONDITIONAL_LIBRARIES := OPTIONAL:libunwind

LOCAL_CONFIG_FILES := aconfig.in
$(call load-config)

LOCAL_EXPORT_C_INCLUDES := \
	$(TARGET_OUT_STAGING)/usr/include/gstreamer-1.0 \
	$(TARGET_OUT_STAGING)/$(TARGET_DEFAULT_LIB_DESTDIR)/gstreamer-1.0/include

LOCAL_EXPORT_LDLIBS := \
	-lgstreamer-1.0 \
	-lgstbase-1.0 \
	-lgstcontroller-1.0 \
	-lgstnet-1.0 \
	-lgstparrot-1.0

ifndef CONFIG_GSTREAMER_DISABLE_CHECK
LOCAL_EXPORT_LDLIBS += -lgstcheck-1.0
endif

# --disable-maintainer-mode is to avoid regenerating configure/Makefile.in
# that we store in git repo
LOCAL_AUTOTOOLS_CONFIGURE_ARGS := \
	--disable-maintainer-mode \
	--disable-nls \
	--disable-benchmarks \
	--disable-examples \
	--disable-tests \
	--disable-failing-tests \
	--disable-gtk-doc-html \
	--disable-introspection

# Get all config options from aconfig.in file and associate each option
# with its configure option. Each output lines look like:
# GSTREAMER_DISABLE_FOO:--disable-foo
#
# To avoid useless processing, we first exclude all commented lines and those
# that don't start with config.
gstreamer_configs := $(shell sed -e '/^\s*config/!d' \
	-e 's/^\s*config\s\+\(GSTREAMER_\(.*\)\)/\1/' \
	-e 'h' \
	-e 's/GSTREAMER_\(.*\)/--\L\1/' \
	-e 's/_/-/g' \
	-e 'H' \
	-e 'g' \
	-e 's/\n/:/' $(LOCAL_PATH)/aconfig.in)

# Update LOCAL_AUTOTOOLS_CONFIGURE_ARGS with config state
$(foreach _config,$(gstreamer_configs), \
	$(eval _var := $(word 1,$(subst $(colon),$(space),$(_config)))) \
	$(eval _opt := $(word 2,$(subst $(colon),$(space),$(_config)))) \
	$(if $(CONFIG_$(_var)), \
		$(eval LOCAL_AUTOTOOLS_CONFIGURE_ARGS += $(_opt)) \
	) \
)

include $(BUILD_AUTOTOOLS)

