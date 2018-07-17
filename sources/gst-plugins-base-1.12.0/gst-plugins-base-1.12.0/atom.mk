
LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := gst-plugins-base
LOCAL_DESCRIPTION := GStreamer streaming media framework base plug-ins
LOCAL_CATEGORY_PATH := multimedia/gstreamer

LOCAL_CONFIG_FILES := aconfig.in
$(call load-config)

LOCAL_LIBRARIES := gstreamer

LOCAL_CONDITIONAL_LIBRARIES := OPTIONAL:orc
LOCAL_CONDITIONAL_LIBRARIES += OPTIONAL:zlib
LOCAL_CONDITIONAL_LIBRARIES += OPTIONAL:libogg
LOCAL_CONDITIONAL_LIBRARIES += OPTIONAL:libvorbis
LOCAL_CONDITIONAL_LIBRARIES += OPTIONAL:alsa-lib
LOCAL_CONDITIONAL_LIBRARIES += OPTIONAL:pango
LOCAL_CONDITIONAL_LIBRARIES += OPTIONAL:vorbis

LOCAL_EXPORT_LDLIBS := \
	-lgstallocators-1.0 \
	-lgstapp-1.0 \
	-lgstaudio-1.0 \
	-lgstfft-1.0 \
	-lgstpbutils-1.0 \
	-lgstriff-1.0 \
	-lgstrtp-1.0 \
	-lgstrtsp-1.0 \
	-lgstsdp-1.0 \
	-lgsttag-1.0 \
	-lgstvideo-1.0

# --disable-maintainer-mode is to avoid regenerating configure/Makefile.in
# that we store in git repo
LOCAL_AUTOTOOLS_CONFIGURE_ARGS := \
	--disable-maintainer-mode \
	--disable-nls \
	--disable-examples \
	--disable-gtk-doc-html \
	--with-audioresample-format=int \
	--disable-introspection

# Explicitly disable libraries that we don't depend on.
# It ensures consistency in build.
# If you want to enable one, put it also in LOCAL_CONDITIONAL_LIBRARIES
LOCAL_AUTOTOOLS_CONFIGURE_ARGS += \
	--disable-x \
	--disable-xvideo \
	--disable-xshm \
	--disable-cdparanoia \
	--disable-ivorbis \
	--disable-libvisual \
	--disable-oggtest \
	--disable-theora \
	--disable-vorbistest \
	--disable-freetypetest

# Get all config options from aconfig.in file and associate each option
# with its configure option. Each output lines look like:
# GST_PLUGINS_BASE_DISABLE_FOO:--disable-foo
#
# To avoid useless processing, we first exclude all commented lines and those
# that don't start with config.
gst_plugins_base_configs := $(shell sed -e '/^\s*config/!d' \
	-e 's/^\s*config\s\+\(GST_PLUGINS_BASE_\(.*\)\)/\1/' \
	-e 'h' \
	-e 's/GST_PLUGINS_BASE_\(.*\)/--\L\1/' \
	-e 's/_/-/g' \
	-e 'H' \
	-e 'g' \
	-e 's/\n/:/' $(LOCAL_PATH)/aconfig.in)

# Update LOCAL_AUTOTOOLS_CONFIGURE_ARGS with config state
$(foreach _config,$(gst_plugins_base_configs), \
	$(eval _var := $(word 1,$(subst $(colon),$(space),$(_config)))) \
	$(eval _opt := $(word 2,$(subst $(colon),$(space),$(_config)))) \
	$(if $(CONFIG_$(_var)), \
		$(eval LOCAL_AUTOTOOLS_CONFIGURE_ARGS += $(_opt)) \
	) \
)

include $(BUILD_AUTOTOOLS)
