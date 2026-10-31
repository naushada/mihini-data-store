-- OpenVPN server module config (YANG-like schema).
return {
    namespace = "openvpn.server",
    unit = "openvpn-server.service",
    store = "flash",
    mirror = true,

    config = {
        enabled = { type = "boolean", default = false },          -- start/stop knob
        config  = { type = "string",  default = "/etc/openvpn/server.conf" },
        port    = { type = "integer", default = 1194, range = { 1, 65535 } },
        proto   = { type = "string",  default = "udp", enum = { "udp", "tcp" } },
        dev     = { type = "string",  default = "tun", enum = { "tun", "tap" } },
        subnet  = { type = "string",  default = "10.8.0.0" },
    },

    state = {
        running    = { type = "boolean", default = false },
        pid        = { type = "integer", default = 0 },
        last_error = { type = "string",  default = "" },
    },
}
