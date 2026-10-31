-- OpenVPN client module config (YANG-like schema).
--
-- Conventions (see apps/modules/supervisor.lua):
--   config.enabled  boolean start/stop knob, persisted in flash
--   state.*         operational state: config leafs are referenced in (mirror = true)
--                   plus extra operational-only leafs (running / pid / last_error)
--                   written by the supervisor.
return {
    namespace = "openvpn.client",
    unit = "openvpn-client.service",
    store = "flash",
    mirror = true,          -- reference every config leaf into state.*

    config = {
        enabled = { type = "boolean", default = false },          -- start/stop knob
        config  = { type = "string",  default = "/etc/openvpn/client.ovpn" },
        remote  = { type = "string",  default = "" },
        port    = { type = "integer", default = 1194, range = { 1, 65535 } },
        proto   = { type = "string",  default = "udp", enum = { "udp", "tcp" } },
    },

    state = {                                                     -- operational (config false)
        running    = { type = "boolean", default = false },       -- actual, by supervisor
        pid        = { type = "integer", default = 0 },
        last_error = { type = "string",  default = "" },
    },
}
