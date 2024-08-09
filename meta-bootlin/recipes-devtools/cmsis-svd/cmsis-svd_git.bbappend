SRC_URI = "git://github.com/posborne/cmsis-svd.git;protocol=https;branch=main"
SRCREV = "f487b5ca7c132b8f09d11514c509372f83a6cb75"

PV = "0.4+git${SRCPV}"

S = "${WORKDIR}/git"

BBCLASSEXTEND += "native nativesdk"
