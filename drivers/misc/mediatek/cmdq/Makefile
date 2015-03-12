# common driver code for CMDQ (ROME, K2~)
# 72/82/92 does not use this common code

ccflags-y += -I$(srctree)/drivers/misc/mediatek/cmdq/mt6595 \
             -I$(srctree)/drivers/misc/mediatek/dispsys/mt6595 \
             -I$(srctree)/drivers/misc/mediatek/smi/mt6595

# Enable MET
ccflags-y += -DMET_USER_EVENT_SUPPORT

# driver module
obj-y += cmdq_record.o
obj-y += cmdq_core.o
obj-y += cmdq_driver.o
obj-y += cmdq_mmp.o
obj-y += cmdq_test.o
obj-y += cmdq_mdp_common.o
obj-y += cmdq_test.o

obj-y += $(subst ",,$(CONFIG_MTK_PLATFORM))/

