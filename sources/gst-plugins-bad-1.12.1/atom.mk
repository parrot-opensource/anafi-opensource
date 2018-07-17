LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := gst-plugins-bad
LOCAL_DESCRIPTION := GStreamer streaming media framework bad plug-ins
LOCAL_CATEGORY_PATH := multimedia/gstreamer

LOCAL_CONFIG_FILES := aconfig.in
$(call load-config)

LOCAL_LIBRARIES := gstreamer gst-plugins-base

LOCAL_CONDITIONAL_LIBRARIES := OPTIONAL:orc
LOCAL_CONDITIONAL_LIBRARIES += OPTIONAL:opencv
LOCAL_CONDITIONAL_LIBRARIES += OPTIONAL:opus
LOCAL_CONDITIONAL_LIBRARIES += OPTIONAL:sbc
LOCAL_CONDITIONAL_LIBRARIES += OPTIONAL:opengles
LOCAL_CONDITIONAL_LIBRARIES += OPTIONAL:egl

# Only exports libraries which will be build for sure:
# gl and wayland are system-dependent
LOCAL_EXPORT_LDLIBS := \
	-lgstcodecparsers-1.0 \
	-lgstinsertbin-1.0 \
	-lgstmpegts-1.0

# Export GstGL library if opengles and egl are enabled
have_gl := \
	$(and "$(call is-module-in-build-config,opengles)", \
		"$(call is-module-in-build-config,egl)" \
	)

ifneq ($(have_gl),"")
LOCAL_EXPORT_LDLIBS += -lgstgl-1.0
endif

# --disable-maintainer-mode is to avoid regenerating configure/Makefile.in
# that we store in git repo
LOCAL_AUTOTOOLS_CONFIGURE_ARGS := \
	--disable-maintainer-mode \
	--disable-nls \
	--disable-examples \
	--disable-gtk-doc-html \
	--disable-hls \
	--disable-introspection \
	--enable-egl-without-win

# Get all config options from aconfig.in file and associate each option
# with its configure option. Each output lines look like:
# GST_PLUGINS_BAD_DISABLE_FOO:--disable-foo
#
# To avoid useless processing, we first exclude all commented lines.
gst_plugins_bad_configs := $(shell sed -e '/^\s*config/!d' \
	-e 's/^\s*config\s\+\(GST_PLUGINS_BAD_\(.*\)\)/\1/' \
	-e 'h' \
	-e 's/GST_PLUGINS_BAD_\(.*\)/--\L\1/' \
	-e 's/_/-/g' \
	-e 'H' \
	-e 'g' \
	-e 's/\n/:/' $(LOCAL_PATH)/aconfig.in)

# Update LOCAL_AUTOTOOLS_CONFIGURE_ARGS with config state
$(foreach _config,$(gst_plugins_bad_configs), \
	$(eval _var := $(word 1,$(subst $(colon),$(space),$(_config)))) \
	$(eval _opt := $(word 2,$(subst $(colon),$(space),$(_config)))) \
	$(if $(CONFIG_$(_var)), \
		$(eval LOCAL_AUTOTOOLS_CONFIGURE_ARGS += $(_opt)) \
	) \
)

include $(BUILD_AUTOTOOLS)

