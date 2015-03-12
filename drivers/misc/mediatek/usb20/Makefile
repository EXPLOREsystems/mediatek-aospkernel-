#
# for USB OTG silicon based on Mentor Graphics INVENTRA designs
#
include $(srctree)/drivers/misc/mediatek/Makefile.custom

obj-y += $(subst ",,$(CONFIG_MTK_PLATFORM))/

musb_hdrc-y := musb_core.o

musb_hdrc-y					+= musb_gadget_ep0.o musb_gadget.o
musb_hdrc-y					+= musb_virthub.o musb_host.o
musb_hdrc-y					+= musbhsdma.o 
