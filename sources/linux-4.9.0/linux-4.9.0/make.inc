##
## kernel/linux/make.inc
##
## History:
##    2012/06/01 - [Cao Rongrong] Created file
##
## Copyright 2008-2015 Ambarella Inc.  All Rights Reserved.
##
## This program is free software; you can redistribute it and/or
## modify it under the terms of the GNU General Public License
## as published by the Free Software Foundation; either version 2
## of the License, or (at your option) any later version.
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##

ifeq ($(CONFIG_LINUX_KERNEL_VERSION),"4.9")

LOCAL_PATH:=$(call my-dir)

#####

.PHONY: linux

linux: $(DOT_CONFIG)
	@mkdir -p $(LINUX_OUT_DIR)
	@if [ -f $(LINUX_OUT_DIR)/.config ]; then \
		echo "Build Linux-$(CONFIG_LINUX_KERNEL_VERSION) with previous configuration ..."; \
	else \
		$(MAKE) $(AMBA_MAKE_PARA) ARCH=$(CPU_ARCH) defconfig_public_linux; \
	fi
	$(AMBA_MAKEFILE_V)$(MAKE) $(AMBA_MAKE_PARA) ARCH=$(CPU_ARCH) -C $(LINUX_SRC_DIR) O=$(LINUX_OUT_DIR) all
	@if [ -n "$(KERNEL_INSTALL_PATH)" ]; then \
		echo "Install Linux modules to $(KERNEL_INSTALL_PATH) ..."; \
		mkdir -p $(KERNEL_INSTALL_PATH); \
		$(MAKE) $(AMBA_MAKE_PARA) ARCH=$(CPU_ARCH) -C $(LINUX_SRC_DIR) O=$(LINUX_OUT_DIR) \
			$(LINUX_INSTALL_FLAG) modules_install; \
		find $(KERNEL_INSTALL_PATH)/lib/modules/ -name build | xargs -l1 rm -rf; \
		find $(KERNEL_INSTALL_PATH)/lib/modules/ -name source | xargs -l1 rm -rf; \
	fi
	@cp -dpRf $(LINUX_OUT_DIR)/arch/$(CPU_ARCH)/boot/Image $(KERNEL_OUT_DIR)/Image
	@if [ -f $(LINUX_OUT_DIR)/arch/$(CPU_ARCH)/boot/zImage ]; then \
		cp -dpRf $(LINUX_OUT_DIR)/arch/$(CPU_ARCH)/boot/zImage $(KERNEL_OUT_DIR)/zImage; \
	fi
	@echo "Build $@ Done."

$(call add-target-into-build, linux)

###

.PHONY: menuconfig_public_linux

menuconfig_public_linux: $(DOT_CONFIG)
	@mkdir -p $(LINUX_OUT_DIR)
	@$(MAKE) $(AMBA_MAKE_PARA) ARCH=$(CPU_ARCH) -C $(LINUX_SRC_DIR) O=$(LINUX_OUT_DIR) menuconfig

###

.PHONY: defconfig_public_linux

defconfig_public_linux: $(DOT_CONFIG)
	@echo "Build Linux-$(CONFIG_LINUX_KERNEL_VERSION) with $(KERNEL_DEFCONFIG) ..."
	@mkdir -p $(LINUX_OUT_DIR)
	@if [ -f $(AMB_BOARD_DIR)/config/kernel/$(KERNEL_DEFCONFIG) ]; then \
		cp -dpRf $(AMB_BOARD_DIR)/config/kernel/$(KERNEL_DEFCONFIG) $(LINUX_OUT_DIR)/.config; \
	else \
		$(MAKE) $(AMBA_MAKE_PARA) ARCH=$(CPU_ARCH) -C $(LINUX_SRC_DIR) \
					O=$(LINUX_OUT_DIR) $(KERNEL_DEFCONFIG); \
	fi

#####

endif

