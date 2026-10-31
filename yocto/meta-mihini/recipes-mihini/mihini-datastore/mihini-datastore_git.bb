SUMMARY = "C++ static library and CLI bridging the Mihini device tree"
DESCRIPTION = "libmihini-datastore (std::variant Value + DataStore facade with \
in-memory and EMP backends) and the datastore-cli example app."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "git://github.com/naushada/mihini-data-store.git;branch=main;protocol=https"
SRCREV = "${AUTOREV}"

inherit cmake

S = "${WORKDIR}/git"

# Offline (in-memory) backend by default. To link the EMP backend against the
# agent's swi_* libraries on-target, set -DMIHINI_DATSTORE_WITH_SWILIBS=ON plus
# MIHINI_SWILIBS_INCLUDE_DIRS / MIHINI_SWILIBS_LIBRARY_DIRS / MIHINI_SWILIBS_LIBRARIES
# once the mihini-agent recipe produces those libraries.
EXTRA_OECMAKE += "-DMIHINI_DATSTORE_WITH_SWILIBS=OFF"

# The tree has no install() rules yet, so install the artifacts explicitly.
do_install() {
    install -d ${D}${libdir}
    install -m 0644 ${B}/libs/mihini-datastore/libmihini-datastore.a ${D}${libdir}/

    install -d ${D}${includedir}/mihini
    install -m 0644 ${S}/libs/mihini-datastore/include/mihini/*.hpp ${D}${includedir}/mihini/

    install -d ${D}${bindir}
    install -m 0755 ${B}/apps/datastore-cli/datastore-cli ${D}${bindir}/
}

FILES:${PN} = "${bindir}/datastore-cli"
FILES:${PN}-dev = "${includedir}/mihini"
FILES:${PN}-staticdev = "${libdir}/libmihini-datastore.a"
