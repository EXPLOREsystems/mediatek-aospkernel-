include $(srctree)/drivers/misc/mediatek/Makefile.custom

# Linux driver folder
ccflags-y += -I$(srctree)/arch/arm/mach-$(MTK_PLATFORM)/$(ARCH_MTK_PROJECT)/touchpanel/MXT224/
ccflags-y += -I$(srctree)/drivers/input/touchscreen/mediatek/MXT224/
ccflags-y += -I$(srctree)/drivers/input/touchscreen/mediatek/

obj-y	+=	atmel_mxt_ts.o
