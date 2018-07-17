
LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := lttng-tools
LOCAL_DESCRIPTION := The trace control client.
LOCAL_CATEGORY_PATH := devel/lttng

LOCAL_AUTOTOOLS_VERSION := 2.8.2
LOCAL_AUTOTOOLS_ARCHIVE := $(LOCAL_MODULE)-$(LOCAL_AUTOTOOLS_VERSION).tar.bz2
LOCAL_AUTOTOOLS_SUBDIR := $(LOCAL_MODULE)-$(LOCAL_AUTOTOOLS_VERSION)
LOCAL_AUTOTOOLS_CONFIGURE_ARGS += \
	--disable-xmltest \
	--disable-extras \
	--disable-man-pages \
	--with-xml-prefix=$(TARGET_OUT_STAGING)/usr
LOCAL_AUTOTOOLS_PATCHES := configure_remove_tests_and_doc.patch
LOCAL_LIBRARIES := libxml2 popt userspace-rcu lttng-ust

LTTNG_TOOLS_BUILD_DIR := $(call local-get-build-dir)
LTTNG_TOOLS_SRC_DIR := $(LTTNG_TOOLS_BUILD_DIR)/$(LOCAL_MODULE)-$(LOCAL_AUTOTOOLS_VERSION)

# To complete a clean - make cycle, some tools are necessary to generate
# C files from yacc and lex files. Autoconf doesn't check for these tool
# existences, so there may be compile-time errors. The YACC error is quite
# explicit but for LEX it fails without explanation.
# So just be sure to have yacc and lex tools, bison and flex for instance.

# TODO: On POST_INSTALL and POST_CLEAN, manage /var/run/lttng folder in final tree

ifneq ("$(call is-module-in-build-config,$(LOCAL_MODULE))","")
ifeq ("$(TARGET_CROSS)","/opt/arm-2009q1/bin/arm-none-linux-gnueabi-")
$(warning $(LOCAL_MODULE) requires a more recent toolchain (>= 2010q3))
endif
endif

include $(BUILD_AUTOTOOLS)
