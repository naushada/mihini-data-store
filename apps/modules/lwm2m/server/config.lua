-- LwM2M server module config (YANG-like schema).
--
-- Mirrors the iot repo's LwM2M server (lwm2m-dm + bootstrap): CoAPs over
-- DTLS-PSK via Eclipse tinydtls. `dm` is the device-management listener
-- (clients register here over coaps), `bs` the bootstrap listener. Both default
-- to security.mode "PSK"; "None" skips the DTLS handshake for dev bring-up.
return {
    namespace = "lwm2m.server",
    unit = "lwm2m-server.service",
    store = "flash",
    mirror = true,

    config = {
        enabled = { type = "boolean", default = false },          -- start/stop knob

        dm = {
            uri      = { type = "string",  default = "coaps://0.0.0.0:5683" },
            lifetime = { type = "integer", default = 90, range = { 0, 2592000 } },
            binding  = { type = "string",  default = "U", enum = { "U", "T", "N", "S" } },
            psk = {
                id  = { type = "string", default = "iot-dm-client" },
                key = { type = "string", default = "" },          -- DTLS-PSK secret (hex/raw)
            },
        },

        bs = {
            uri = { type = "string", default = "coaps://0.0.0.0:5684" },
            security = {
                mode = { type = "string", default = "PSK", enum = { "PSK", "None" } },
            },
            psk = {
                id  = { type = "string", default = "iot-client" },
                key = { type = "string", default = "" },          -- DTLS-PSK secret (hex/raw)
            },
        },

        lwm2m_version = { type = "string", default = "1.1" },

        db = { type = "string", default = "/var/lib/lwm2m-server" }, -- Wakaama server data dir
    },

    state = {
        running           = { type = "boolean", default = false },
        pid               = { type = "integer", default = 0 },
        started_timestamp = { type = "integer", default = 0 },
        last_error        = { type = "string",  default = "" },
    },
}
