-- supervisor.lua -- turns each module's `enabled` config knob into systemd
-- start/stop, and reports the actual status back into the state container.
--
-- A module config schema (see openvpn/client/config.lua for the canonical shape)
-- must declare:
--     config.enabled            boolean  the start/stop knob (persisted in flash)
--     state.running             boolean  actual status, written by this supervisor
--     state.started_timestamp   integer  last successful start (unix epoch; 0 = down)
-- (plus any other operational state leafs, e.g. pid / last_error) and a top-level
-- `unit` (systemd unit name) + `namespace` (dot path under config/state).
--
-- The supervisor runs inside the agent. For each module it calls model:apply(),
-- which applies config defaults, initialises the operational state leafs, and
-- starts the config -> state reference (`mirror = true`, so every config leaf is
-- also visible under state.<namespace>.*). It then watches the `enabled` knob:
-- setting it true runs `systemctl start <unit>`, false runs `systemctl stop`, and
-- the result is mirrored into `state.<namespace>.running` / `.last_error`. A knob
-- persisted true is re-applied at boot, giving autostart across reboots.

local schema = require 'schema'

local M = {}

local function run_cmd(cmd)
    -- os.execute returns 0 on success under Lua 5.1 / LuaJIT (true under 5.2+).
    local r = os.execute(cmd .. " >/dev/null 2>&1")
    return r == 0 or r == true
end

local function load_modules()
    local out = {}
    for _, mod in ipairs(require 'apps.modules.manifest') do
        out[#out + 1] = schema.load(require(mod))
    end
    return out
end

-- Start supervising every module. `models` and `treemgr` are injectable for
-- testing; by default they are loaded from the manifest and the real agent.
-- Returns a handle with :stop().
function M.start(models, treemgr)
    models = models or load_modules()
    treemgr = treemgr or require 'agent.treemgr'

    local stops = {}

    for _, model in ipairs(models) do
        local unit    = model.unit or ((model.namespace or "module") .. ".service")
        local knob    = model:config_path("enabled")
        local run     = model:state_path("running")
        local started = model:state_path("started_timestamp")
        local err     = model:state_path("last_error")

        -- defaults + operational-state init + the config -> state reference
        local mirror = model:apply(treemgr)
        stops[#stops + 1] = function() mirror:stop() end

        local function apply(enabled)
            local what = "systemctl " .. (enabled and "start" or "stop") .. " " .. unit
            local ok = run_cmd(what)
            local running = ok and enabled or false
            treemgr.set(run, running)
            treemgr.set(started, running and os.time() or 0)
            treemgr.set(err, ok and "" or (what .. " failed"))
        end

        -- honour a persisted knob at boot (autostart across reboots)
        apply(treemgr.get(knob) == true)

        local hook = treemgr.register(knob, function(lmap)
            local enabled = lmap[knob]
            if enabled == nil then enabled = treemgr.get(knob) end
            apply(enabled == true)
        end)
        stops[#stops + 1] = function() treemgr.unregister(hook) end
    end

    local handle = {}
    function handle:stop()
        for _, stop in ipairs(stops) do stop() end
    end
    return handle
end

return M
