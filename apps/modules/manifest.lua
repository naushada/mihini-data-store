-- The registry of managed modules. Each entry is the Lua module path of a
-- module's config schema (apps.modules.<name>.config). The supervisor loads each
-- one and drives its `enabled` knob -> systemd start/stop.
return {
    "apps.modules.openvpn.client.config",
    "apps.modules.openvpn.server.config",
    "apps.modules.lwm2m.client.config",
    "apps.modules.lwm2m.server.config",
    "apps.modules.cellular.config",
}
