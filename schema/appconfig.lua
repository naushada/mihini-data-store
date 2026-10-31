-- appconfig.lua -- an example YANG-flavoured config model for an asset application.
--
-- Pure data: no executable logic. `schema.lua` consumes this table and turns it
-- into behaviour (defaults + validation + the config->state mirror rule).
--
-- Backing is chosen by `store`:
--     store = "flash"  -> intended config lives in `config.*` (persisted, flash-backed)
--     store = "ram"    -> intended config lives in `ram.*`    (volatile, RAM-backed)
-- Either way, every leaf marked `state = true` is mirrored into the state
-- container (`state.*`) so runtime/operational readers only ever touch one place.

return {
    module = "appconfig",
    store = "flash", -- or "ram"

    -- Intended configuration tree (containers + typed leafs).
    config = {
        network = {
            activate       = { type = "boolean",     default = false,            state = true },
            apn            = { type = "string",      default = "",               state = true },
            bearerpriority = { type = "list:string", default = { "ETH", "GPRS" } },
            retry          = { type = "integer",     default = 5,  range = { 0, 255 } },
        },

        server = {
            url        = { type = "string",  default = "tcp://m2m.example.org:44900", state = true },
            retrytimes = { type = "integer", default = 10, range = { 1, 100 } },
        },

        -- A constrained string: one of the allowed log levels.
        logging = {
            level = { type = "string", default = "INFO",
                      enum = { "NONE", "ERROR", "WARNING", "INFO", "DETAIL", "DEBUG", "ALL" } },
        },
    },
}
