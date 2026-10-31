SUMMARY = "tinydtls 0.8.6 — lightweight DTLS library"
DESCRIPTION = "Eclipse tinydtls is a minimal DTLS 1.2 implementation for \
constrained devices. mihini-lwm2m links it statically for CoAPs (CoAP over \
DTLS-PSK) transport. Built from the source vendored in the mihini-data-store \
repo (libs/mihini-lwm2m/3rdparty/tinydtls) with the iot repo's local patches \
already applied, so this recipe and the library can never drift."
HOMEPAGE = "https://github.com/legatoproject/tinydtls"
LICENSE = "EPL-1.0 | EDL-1.0"
LIC_FILES_CHKSUM = "file://libs/mihini-lwm2m/3rdparty/tinydtls/LICENSE;md5=ffb073dbb36e7ec5e091047332f302c5"
SECTION = "libs"

SRC_URI = "git://github.com/naushada/mihini-data-store.git;branch=main;protocol=https"
SRCREV = "${AUTOREV}"

S = "${WORKDIR}/git"

inherit cmake

# Build the vendored tinydtls CMake project (a self-contained add_library),
# not the repo root. Its CMakeLists.txt declares project(tinydtls C) so it
# configures standalone.
OECMAKE_SOURCEPATH = "${S}/libs/mihini-lwm2m/3rdparty/tinydtls"

# tinydtls is static-only and has no install() rules; install the artifacts
# explicitly so dependent recipes can pick them up from the sysroot.
do_install() {
    install -d ${D}${libdir}
    install -m 0644 ${B}/libtinydtls.a ${D}${libdir}/libtinydtls.a

    install -d ${D}${includedir}/tinydtls
    install -m 0644 ${S}/libs/mihini-lwm2m/3rdparty/tinydtls/*.h ${D}${includedir}/tinydtls/

    # session.h includes <platform/inet.h> + <platform/types.h>; crypto.h
    # pulls aes/rijndael.h and sha2/sha2.h. Install the subdirs in place so a
    # consumer can compile against tinydtls without the full source tree.
    for d in platform aes ecc sha2; do
        install -d ${D}${includedir}/tinydtls/${d}
        install -m 0644 ${S}/libs/mihini-lwm2m/3rdparty/tinydtls/${d}/*.h ${D}${includedir}/tinydtls/${d}/
    done
}

# Static-library-only recipe: keep ${PN} resolvable even though it ships no
# runtime files.
ALLOW_EMPTY:${PN} = "1"

FILES:${PN}-staticdev = "${libdir}/libtinydtls.a"
FILES:${PN}-dev = "${includedir}/tinydtls"
