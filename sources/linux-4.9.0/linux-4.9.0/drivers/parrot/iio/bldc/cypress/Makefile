#
# Makefile for BLDC cypress IIO driver.
#
ccflags-y += -I$(srctree)/drivers/parrot

obj-$(CONFIG_IIO_PARROT_BLDC_CYPRESS) += bldc-cypress.o
bldc-cypress-objs := bldc_cypress_core.o bldc_cypress_ring.o bldc_host_bootloader_routines.o bldc_host_bootloader.o

obj-$(CONFIG_IIO_PARROT_BLDC_CYPRESS_I2C) += bldc_cypress_i2c.o
