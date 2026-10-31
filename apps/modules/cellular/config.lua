-- Cellular modem module config (YANG-like schema).
return {
    namespace = "cellular",
    unit = "cellular.service",
    store = "flash",
    mirror = true,

    config = {
        enabled    = { type = "boolean", default = false },       -- start/stop knob
        apn        = { type = "string",  default = "internet" },
        pin        = { type = "string",  default = "" },
        auth       = { type = "string",  default = "none", enum = { "none", "pap", "chap" } },
        technology = { type = "string",  default = "auto", enum = { "auto", "2g", "3g", "lte" } },
    },

    state = {
        running    = { type = "boolean", default = false },
        pid        = { type = "integer", default = 0 },
        last_error = { type = "string",  default = "" },
    },
}
