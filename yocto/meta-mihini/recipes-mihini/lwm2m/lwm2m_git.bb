SUMMARY = "C++ LwM2M 1.1 library for the Mihini data store"
DESCRIPTION = "CoAP (RFC 7252), TLV (Core Annex C), SenML JSON (RFC 8428), the \
object/resource model and the Read/Write/Execute + Observe-Notify/Send \
dispatcher, plus a DTLS 1.2 (PSK) transport. The transport links system \
tinydtls for DTLS and libevent for its I/O loop; the other codec/model sources \
are dependency-free."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "git://github.com/naushada/mihini-data-store.git;branch=main;protocol=https"
SRCREV = "${AUTOREV}"

S = "${WORKDIR}/git"

# liblwm2m.a links tinydtls (dtls.cpp) + libevent (transport I/O loop) symbols,
# so the sysroot must carry both for any consumer that links the DTLS transport.
DEPENDS = "tinydtls libevent"

inherit cmake

# Build the vendored lwm2m CMake project (a self-contained add_library),
# not the repo root. Link the system tinydtls and skip the unit-test executables.
OECMAKE_SOURCEPATH = "${S}/libs/lwm2m"
EXTRA_OECMAKE += "-DLWM2M_SYSTEM_TINYDTLS=ON -DBUILD_TESTING=OFF"

# No install() rules; install the artifacts explicitly.
do_install() {
    install -d ${D}${libdir}
    install -m 0644 ${B}/liblwm2m.a ${D}${libdir}/

    install -d ${D}${includedir}/lwm2m
    install -m 0644 ${S}/libs/lwm2m/include/lwm2m/*.hpp ${D}${includedir}/lwm2m/
}

# Static-library-only recipe: keep ${PN} resolvable even though it ships no
# runtime files.
ALLOW_EMPTY:${PN} = "1"

FILES:${PN}-staticdev = "${libdir}/liblwm2m.a"
FILES:${PN}-dev = "${includedir}/lwm2m"

# A static consumer of liblwm2m.a that pulls in the DTLS transport also needs
# libtinydtls.a, libevent, and their headers in the sysroot.
RDEPENDS:${PN}-staticdev = "tinydtls-staticdev libevent-staticdev"
RDEPENDS:${PN}-dev = "tinydtls-dev libevent-dev"
