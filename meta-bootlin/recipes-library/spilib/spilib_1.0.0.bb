DESCRIPTION = "Example shared library"
LICENSE = "CLOSED"
SRC_URI = "file://spi_lib.c;sha256=f0d584d69f7cd6e43cd333b521708da34e9c272e321d8ed5d7d15173d3ec73b6 \
           file://spi_lib.h;sha256=c6c582dbc97a26a45df62aa859f61a0d1395eed12c7485e718f4fb733c963792 \
           file://CMakeLists.txt;sha256=31a50b8b2ba0665e4dea38edaf6be5709183a3579bbcbca499939dbadf6052bb"


S = "${WORKDIR}"

inherit cmake

DEPENDS = "libgpiod"

# Define version variables
LIBRARY_VERSION_MAJOR = "1"
LIBRARY_VERSION_MINOR = "0"
LIBRARY_VERSION_PATCH = "0"
LIBRARY_VERSION = "${LIBRARY_VERSION_MAJOR}.${LIBRARY_VERSION_MINOR}.${LIBRARY_VERSION_PATCH}"



do_install() {
    install -d ${D}${libdir}
    install -d ${D}${includedir}

    # Use the correct way to reference the LIBRARY_VERSION variable
    library_version="${@d.getVar('LIBRARY_VERSION')}"

    install -m 0755 ${B}/libspi_lib.so.${library_version} ${D}${libdir}/
    ln -sf libspi_lib.so.${library_version} ${D}${libdir}/libspi_lib.so

    install -m 0644 ${S}/spi_lib.h ${D}${includedir}/
}
