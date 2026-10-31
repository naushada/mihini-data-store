SUMMARY = "Mihini managed modules: Lua config schemas, supervisor, systemd units"
DESCRIPTION = "Per-module YANG-like Lua configs (openvpn client/server, lwm2m \
client/server, cellular) plus the module supervisor that maps each module's \
`enabled` knob to systemd start/stop, and the corresponding systemd units. The \
units are installed but never enabled at boot -- they are started on demand."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

# Builds from the repository. Pin SRCREV for reproducible images.
SRC_URI = "git://github.com/naushada/mihini-data-store.git;branch=main;protocol=https \
           file://openvpn-client.service \
           file://openvpn-server.service \
           file://lwm2m-client.service \
           file://lwm2m-server.service \
           file://cellular.service \
"
SRCREV = "${AUTOREV}"

S = "${WORKDIR}/git"

# Runtime layout must match the agent's Lua search path (see mihini-schema).
MISC_PREFIX = "${datadir}/mihini"

inherit systemd

do_install() {
    # Lua module configs + manifest + supervisor, under the agent's Lua root so
    # `require 'apps.modules.<name>.config'` / `require 'apps.modules.supervisor'`
    # resolve. (The unit-test file is deliberately not installed.)
    MOD=${D}${MISC_PREFIX}/apps/modules
    install -d ${MOD}/openvpn/client ${MOD}/openvpn/server
    install -m 0644 ${S}/apps/modules/openvpn/client/config.lua ${MOD}/openvpn/client/
    install -m 0644 ${S}/apps/modules/openvpn/server/config.lua ${MOD}/openvpn/server/
    install -d ${MOD}/lwm2m/client ${MOD}/lwm2m/server
    install -m 0644 ${S}/apps/modules/lwm2m/client/config.lua ${MOD}/lwm2m/client/
    install -m 0644 ${S}/apps/modules/lwm2m/server/config.lua ${MOD}/lwm2m/server/
    install -d ${MOD}/cellular
    install -m 0644 ${S}/apps/modules/cellular/config.lua ${MOD}/cellular/
    install -m 0644 ${S}/apps/modules/manifest.lua   ${MOD}/
    install -m 0644 ${S}/apps/modules/supervisor.lua ${MOD}/

    # systemd units, installed but NOT enabled (no SYSTEMD_SERVICE): the
    # supervisor runs `systemctl start/stop` on demand from each enabled knob.
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/openvpn-client.service ${D}${systemd_system_unitdir}/
    install -m 0644 ${WORKDIR}/openvpn-server.service ${D}${systemd_system_unitdir}/
    install -m 0644 ${WORKDIR}/lwm2m-client.service   ${D}${systemd_system_unitdir}/
    install -m 0644 ${WORKDIR}/lwm2m-server.service   ${D}${systemd_system_unitdir}/
    install -m 0644 ${WORKDIR}/cellular.service       ${D}${systemd_system_unitdir}/
}

FILES:${PN} = "${MISC_PREFIX}/apps ${systemd_system_unitdir}"

# The units' ExecStart binaries come from other packages (openvpn, the iot LwM2M
# stack linking tinydtls, ModemManager). Add them as RDEPENDS here once those
# recipes are present.
