-- supervisor.lua -- turns each module's `enabled` config knob into systemd
-- start/stop, and reports the actual status back into the state container.
--
-- A module config schema (see openvpn/client/config.lua for the canonical shape)
-- must declare:
--     config.enabled   boolean  the start/stop knob (persisted in flash)
--     state.running    boolean  actual status, written by this supervisor
-- and a top-level `unit` (systemd unit name) + `namespace` (dot path under
-- config/state, e.g. "openvpn.client" -> config.openvpn.client.enabled).
--
-- The supervisor runs inside the agent. It applies each module's defaults, then
-- watches the `enabled` knob: setting it true runs `systemctl start <unit>`,
-- setting it false runs `systemctl stop <unit>`, and the result is mirrored into
-- `state.<namespace>.running`. A knob that was persisted true is re-applied at
-- boot, giving autostart across reboots.

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

    local handles = {}

    for _, model in ipairs(models) do
        local unit = model.unit or ((model.namespace or "module") .. ".service")
        local knob = model:config_path("enabled")
        local run  = model:state_path("running")

        model:apply_defaults(treemgr)

        -- initialise operational state to its declared defaults
        local init = {}
        model:each_state_leaf(function(rel, spec)
            if spec.default ~= nil then init[model:state_path(rel)] = spec.default end
        end)
        if next(init) then treemgr.set(init) end

        local function apply(enabled)
            local ok = run_cmd("systemctl " .. (enabled and "start" or "stop") .. " " .. unit)
            treemgr.set(run, ok and enabled or false)
        end

        -- honour a persisted knob at boot (autostart across reboots)
        apply(treemgr.get(knob) == true)

        handles[#handles + 1] = treemgr.register(knob, function(lmap)
            local enabled = lmap[knob]
            if enabled == nil then enabled = treemgr.get(knob) end
            apply(enabled == true)
        end)
    end

    local handle = {}
    function handle:stop()
        for _, h in ipairs(handles) do treemgr.unregister(h) end
    end
    return handle
end

return M
