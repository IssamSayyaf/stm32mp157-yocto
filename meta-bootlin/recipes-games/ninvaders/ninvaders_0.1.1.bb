DESCRIPTION = "Space invaders clone based on nCurses"
HOMEPAGE = "http://ninvaders.sourceforge.net/"
SECTION = "examples"

SRC_URI = "${SOURCEFORGE_MIRROR}/ninvaders/${BP}.tar.gz \
			file://joystick-support.patch \
			"

SRC_URI[sha256sum] = "bfbc5c378704d9cf5e7fed288dac88859149bee5ed0850175759d310b61fd30b"

LICENSE = "GPL-2.0-or-later"
LIC_FILES_CHKSUM = "file://gpl.txt;md5=393a5ca445f6965873eca0259a17f833"

DEPENDS = "ncurses"

CFLAGS += "-fcommon"
EXTRA_OEMAKE = "-e"

do_install() {
	install -d ${D}${bindir}
	install -m 755 nInvaders ${D}${bindir}/ninvaders
}
