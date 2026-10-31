-- OpenVPN client module config (YANG-like schema).
--
-- Conventions (see apps/modules/supervisor.lua):
--   config.enabled  boolean start/stop knob, persisted in flash
--   state.running   boolean actual status, written by the supervisor via systemd
return {
    namespace = "openvpn.client",
    unit = "openvpn-client.service",
    store = "flash",

    config = {
        enabled = { type = "boolean", default = false },          -- start/stop knob
        config  = { type = "string",  default = "/etc/openvpn/client.ovpn" },
        remote  = { type = "string",  default = "" },
        port    = { type = "integer", default = 1194, range = { 1, 65535 } },
        proto   = { type = "string",  default = "udp", enum = { "udp", "tcp" } },
    },

    state = {
        running = { type = "boolean", default = false },          -- actual, by supervisor
    },
}
