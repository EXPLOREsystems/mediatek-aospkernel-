#
# Makefile for the touchscreen drivers.
#

include $(srctree)/drivers/misc/mediatek/Makefile.custom

# Linux driver folder
ccflags-y += -I$(srctree)/arch/arm/mach-$(MTK_PLATFORM)/$(ARCH_MTK_PROJECT)/touchpanel/TMA445A/
ccflags-y += -I$(srctree)/drivers/input/touchscreen/mediatek/TMA445A/
ccflags-y += -I$(srctree)/drivers/input/touchscreen/mediatek/

obj-y	+= cyttsp5.o
cyttsp5-y := cyttsp5_core.o cyttsp5_mt_common.o
obj-y	+= cyttsp5_i2c.o
obj-y += cyttsp5_platform.o
obj-y += cyttsp5_mta.o

