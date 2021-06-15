# - Build kernel module
#
# launch SDK script first
# like "source /opt/fsl-imx-wayland/5.10-gatesgarth/environment-setup-cortexa53-crypto-poky-linux"


KERNEL=/home/max/nxp/kernel

obj-m += dmacpy_drv.o

all:
	make M=${PWD} modules -C ${KERNEL}

clean:
	make M=${PWD} clean -C ${KERNEL}
