-- LwM2M client module config (YANG-like schema).
--
-- Transport is CoAPs = CoAP over DTLS-PSK, exactly as the iot repo's LwM2M
-- stack: Eclipse tinydtls (1.2) linked statically into the client. `security.mode`
-- "PSK" (default) runs the DTLS handshake with psk.id/psk.key; "None" skips DTLS
-- for bring-up only (dev). `dm` is the device-management server the client
-- registers to; `bs` is the optional bootstrap server (empty = bootstrapping
-- disabled, the device is pre-provisioned with DM credentials).
return {
    namespace = "lwm2m.client",
    unit = "lwm2m-client.service",
    store = "flash",
    mirror = true,

    config = {
        enabled  = { type = "boolean", default = false },         -- start/stop knob
        endpoint = { type = "string",  default = "rpi3b-0001" },  -- client endpoint name

        dm = {
            uri      = { type = "string",  default = "coaps://leshan.eclipseprojects.io:5684" },
            lifetime = { type = "integer", default = 300, range = { 0, 86400 } },
            binding  = { type = "string",  default = "U", enum = { "U", "T", "N", "S" } },
        },

        bs = {
            uri = { type = "string", default = "" },              -- empty => bootstrap off
        },

        security = {
            mode = { type = "string", default = "PSK", enum = { "PSK", "None" } },
        },

        psk = {
            id  = { type = "string", default = "iot-client" },    -- DTLS-PSK identity
            key = { type = "string", default = "" },              -- DTLS-PSK secret (hex/raw)
        },

        lwm2m_version = { type = "string", default = "1.1" },
    },

    state = {
        running           = { type = "boolean", default = false },
        pid               = { type = "integer", default = 0 },
        started_timestamp = { type = "integer", default = 0 },
        last_error        = { type = "string",  default = "" },
    },
}
