SUMMARY = "Mihini YANG-like config schema and the config -> state mirror rule"
DESCRIPTION = "Pure-data Lua model (appconfig.lua) plus the schema engine that \
applies defaults, validates, and mirrors every state=true leaf into a state \
container. Installed into the agent's Lua module path and treemgr maps directory."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

# Builds from the repository. Pin SRCREV for reproducible images.
SRC_URI = "git://github.com/naushada/mihini-data-store.git;branch=main;protocol=https"
SRCREV = "${AUTOREV}"

S = "${WORKDIR}/git"

# Runtime layout (must match the agent's Lua search path + treemgr maps dir; see
# mihini-agent once its recipe is ported).
MISC_PREFIX = "${datadir}/mihini"

do_install() {
    install -d ${D}${MISC_PREFIX}/schema
    install -m 0644 ${S}/schema/init.lua      ${D}${MISC_PREFIX}/schema/
    install -m 0644 ${S}/schema/appconfig.lua  ${D}${MISC_PREFIX}/schema/

    install -d ${D}${MISC_PREFIX}/treemgr/maps
    install -m 0644 ${S}/schema/state.map      ${D}${MISC_PREFIX}/treemgr/maps/
}

FILES:${PN} = "${MISC_PREFIX}"
