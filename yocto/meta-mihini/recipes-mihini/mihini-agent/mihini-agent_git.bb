SUMMARY = "Eclipse Mihini agent (treemgr, config store, EMP/devicetree daemon)"
DESCRIPTION = "Vendored Mihini runtime. *** SKELETON / NOT YET BUILDABLE *** -- the \
2012-era C (cfwk: swi_devicetree/swi_dset/Emp) and Lua must be ported to the \
Scarthgap toolchain before this recipe produces a usable agent. Left as a \
placeholder so the layer structure and image wiring are in place."

# Placeholder license until the vendored EPL-1.0 source is ported; then switch to
#   LICENSE = "EPL-1.0"
#   LIC_FILES_CHKSUM = "file://...;md5=..."
LICENSE = "CLOSED"

SRC_URI = "git://github.com/naushada/mihini-data-store.git;branch=main;protocol=https"
SRCREV = "${AUTOREV}"

S = "${WORKDIR}/git/mihini"

# TODO: inherit cmake + DEPENDS on the real prerequisites once the vendored build
# is ported. Expected deps (best guess, to confirm): lua5.1, yajl, openssl.
# The vendored tree builds cfwk (swi_devicetree/swi_dset/Emp), luafwk, and the agent.

do_compile() {
    # placeholder -- port the vendored mihini/CMakeLists.txt here
    :
}

do_install() {
    # placeholder -- install agent binaries, Lua modules, and treemgr maps here
    :
}
