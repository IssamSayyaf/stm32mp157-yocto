FILESEXTRAPATHS:prepend := "${THISDIR}/${BPN}:"

SRC_URI +="\
         file://0001-ADD-SPI-SUPPORT-DEVICE-TREE.patch \
         file://defconfig \
         "

KERNEL_DEFCONFIG=""
KERNEL_EXTERNAL_DEFCONFIG="defconfig"
