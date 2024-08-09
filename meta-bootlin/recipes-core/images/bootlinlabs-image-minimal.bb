inherit core-image

DESCRIPTION = "A small image to boot a device, created for the Bootlin lab "

IMAGE_INSTALL = "\
                 packagegroup-core-boot \
                 ${CORE_IMAGE_EXTRA_INSTALL} \
                 dropbear \
                 packagegroup-bootlinlabs-games \
                 libgpiod-dev \
                 libgpiod \
                 libgpiod-tools \
                 "


