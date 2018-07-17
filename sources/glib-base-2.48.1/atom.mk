
LOCAL_PATH := $(call my-dir)

###############################################################################
###############################################################################
include $(CLEAR_VARS)

LOCAL_MODULE := glib-base
LOCAL_DESCRIPTION := GLib is the low-level core library
LOCAL_CATEGORY_PATH := libs

LOCAL_EXPORT_C_INCLUDES := \
	$(TARGET_OUT_STAGING)/$(TARGET_DEFAULT_LIB_DESTDIR)/glib-2.0/include \
	$(TARGET_OUT_STAGING)/usr/include/glib-2.0 \
	$(TARGET_OUT_STAGING)/usr/include/gio-unix-2.0

LOCAL_EXPORT_LDLIBS := \
	-lglib-2.0 \
	-lgio-2.0 \
	-lgobject-2.0 \
	-lgmodule-2.0 \
	-lgthread-2.0

LOCAL_LIBRARIES := libffi zlib libpcre

# Use libcorkscrew only if available
LOCAL_CONDITIONAL_LIBRARIES := OPTIONAL:libcorkscrew OPTIONAL:libulog

# The host version will provides us glib-genmarshal and some other tools
LOCAL_DEPENDS_HOST_MODULES := host.glib

LOCAL_AUTOTOOLS_CONFIGURE_ARGS := \
	--disable-maintainer-mode \
	--disable-selinux \
	--disable-fam \
	--disable-gtk-doc-html \
	--disable-man \
	--disable-dtrace \
	--disable-systemtap \
	--disable-modular-tests \
	--with-pcre=system \
	glib_cv_stack_grows=no \
	glib_cv_uscore=no \
	ac_cv_func_posix_getpwuid_r=yes \
	ac_cv_func_posix_getgrgid_r=yes

# Remove some development tools not needed on target
define LOCAL_AUTOTOOLS_CMD_POST_INSTALL
	$(Q) rm -rf $(TARGET_OUT_STAGING)/usr/share/glib-2.0/gettext
	$(Q) rm -f $(TARGET_OUT_STAGING)/usr/bin/glib-compile-resources
	$(Q) rm -f $(TARGET_OUT_STAGING)/usr/bin/glib-compile-schemas
	$(Q) rm -f $(TARGET_OUT_STAGING)/usr/bin/glib-genmarshal
	$(Q) rm -f $(TARGET_OUT_STAGING)/usr/bin/glib-gettextize
	$(Q) rm -f $(TARGET_OUT_STAGING)/usr/bin/glib-mkenums
	$(Q) rm -f $(TARGET_OUT_STAGING)/usr/bin/gobject-query
	$(Q) rm -f $(TARGET_OUT_STAGING)/usr/bin/gtester
	$(Q) rm -f $(TARGET_OUT_STAGING)/usr/bin/gtester-report
endef

include $(BUILD_AUTOTOOLS)

###############################################################################
###############################################################################
include $(CLEAR_VARS)

LOCAL_MODULE := glib
LOCAL_DESCRIPTION := GLib meta package with optional gobject-introspection dependency
LOCAL_CATEGORY_PATH := libs

LOCAL_LIBRARIES := glib-base

# Export glib's libraries here too to make life of module which depends on
# glib easier as meta-package rule doesn't export its dependency LDLIBS list.
LOCAL_EXPORT_LDLIBS := \
	-lglib-2.0 \
	-lgio-2.0 \
	-lgobject-2.0 \
	-lgmodule-2.0 \
	-lgthread-2.0

# Garantee that optional gobject-introspection is built before all packages
# requiring glib.
LOCAL_CONDITIONAL_LIBRARIES := OPTIONAL:gobject-introspection

include $(BUILD_META_PACKAGE)

###############################################################################
# Host
###############################################################################
include $(CLEAR_VARS)

LOCAL_HOST_MODULE := glib

LOCAL_EXPORT_C_INCLUDES := \
	$(HOST_OUT_STAGING)/usr/lib/glib-2.0/include \
	$(HOST_OUT_STAGING)/usr/include/glib-2.0

LOCAL_EXPORT_LDLIBS := \
	-lglib-2.0 \
	-lgio-2.0 \
	-lgobject-2.0 \
	-lgmodule-2.0 \
	-lgthread-2.0

LOCAL_LIBRARIES := host.libffi host.zlib

# Use internal version of pcre because we don't need it somewhere else
LOCAL_AUTOTOOLS_CONFIGURE_ARGS := \
	--disable-maintainer-mode \
	--disable-selinux \
	--disable-fam \
	--disable-gtk-doc-html \
	--disable-man \
	--disable-dtrace \
	--disable-systemtap \
	--disable-modular-tests \
	--with-pcre=internal \
	glib_cv_stack_grows=no \
	glib_cv_uscore=no \
	ac_cv_func_posix_getpwuid_r=yes \
	ac_cv_func_posix_getgrgid_r=yes

include $(BUILD_AUTOTOOLS)


###############################################################################
# Special rule to compile gsettings schema between post-build and pre-final
# Only done if glib is checked in config.
###############################################################################
include $(CLEAR_VARS)

ifneq ("$(call is-module-in-build-config,glib)","")

.PHONY: compile-gsettings-schema
compile-gsettings-schema: post-build
	@if [ -f $(HOST_OUT_STAGING)/usr/bin/glib-compile-schemas ]; then \
		echo "Compiling gsettings schemas"; \
		mkdir -p $(dir $@); \
		$(HOST_OUT_STAGING)/usr/bin/glib-compile-schemas --strict \
			--targetdir=$(TARGET_OUT_STAGING)/usr/share/glib-2.0/schemas \
			$(TARGET_OUT_STAGING)/usr/share/glib-2.0/schemas; \
	else \
		echo "GSettings compiler not found, skip schemas compilation"; \
	fi;

pre-final: compile-gsettings-schema

endif
