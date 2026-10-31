SUMMARY = "C++ LwM2M 1.1 library for the Mihini data store"
DESCRIPTION = "CoAP (RFC 7252), TLV (Core Annex C), SenML JSON (RFC 8428), the \
object/resource model and the Read/Write/Execute + Observe-Notify/Send \
dispatcher, plus a DTLS 1.2 (PSK) transport. The DTLS transport links the \
system tinydtls (see the tinydtls recipe); the other codec/model sources are \
dependency-free."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "git://github.com/naushada/mihini-data-store.git;branch=main;protocol=https"
SRCREV = "${AUTOREV}"

S = "${WORKDIR}/git"

# libmihini-lwm2m.a links tinydtls symbols (dtls.cpp), so the sysroot must carry
# libtinydtls.a + headers for any consumer that links the DTLS transport.
DEPENDS = "tinydtls"

inherit cmake

# Build the vendored mihini-lwm2m CMake project (a self-contained add_library),
# not the repo root. Link the system tinydtls and skip the unit-test executables.
OECMAKE_SOURCEPATH = "${S}/libs/mihini-lwm2m"
EXTRA_OECMAKE += "-DMIHINI_LWM2M_SYSTEM_TINYDTLS=ON -DBUILD_TESTING=OFF"

# No install() rules; install the artifacts explicitly.
do_install() {
    install -d ${D}${libdir}
    install -m 0644 ${B}/libmihini-lwm2m.a ${D}${libdir}/

    install -d ${D}${includedir}/mihini/lwm2m
    install -m 0644 ${S}/libs/mihini-lwm2m/include/mihini/lwm2m/*.hpp ${D}${includedir}/mihini/lwm2m/
}

# Static-library-only recipe: keep ${PN} resolvable even though it ships no
# runtime files.
ALLOW_EMPTY:${PN} = "1"

FILES:${PN}-staticdev = "${libdir}/libmihini-lwm2m.a"
FILES:${PN}-dev = "${includedir}/mihini"

# A static consumer of libmihini-lwm2m.a that pulls in the DTLS transport also
# needs libtinydtls.a and its headers in the sysroot.
RDEPENDS:${PN}-staticdev = "tinydtls-staticdev"
RDEPENDS:${PN}-dev = "tinydtls-dev"
