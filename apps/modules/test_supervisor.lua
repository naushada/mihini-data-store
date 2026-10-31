-- test_supervisor.lua -- unit test for the module supervisor, using a stub
-- treemgr and a stubbed os.execute (no real systemd on the host). Run:
--
--     luajit apps/modules/test_supervisor.lua
--
-- Exercises: default application over namespaced/nested configs, the YANG-style
-- config -> state reference (mirror = true) plus the extra operational state
-- leafs, state.running initialisation, the boot-time autostart pass, and live
-- knob -> systemctl start/stop on change.

local this_dir = arg[0]:match("^(.*)/[^/]+$") or "."
package.path = (this_dir .. "/../../?.lua;" .. this_dir .. "/../../?/init.lua;") .. package.path

local schema     = require 'schema'
local supervisor = require 'apps.modules.supervisor'

-- ---------------------------------------------------------------------------
-- stub treemgr (same dot-call shape as the real agent.treemgr)
-- ---------------------------------------------------------------------------

local function new_stub()
    local values, regs = {}, {}
    local stub = {}

    function stub.get(path) return values[path] end

    function stub.set(path_or_map, value)
        local changes = {}
        if type(path_or_map) == "table" then
            for p, v in pairs(path_or_map) do values[p] = v; changes[p] = v end
        else
            values[path_or_map] = value; changes[path_or_map] = value
        end
        for _, reg in ipairs(regs) do
            local hit = false
            for p in pairs(changes) do
                for _, mon in ipairs(reg.monitored) do
                    if p == mon or p:sub(1, #mon + 1) == mon .. "." then hit = true; break end
                end
                if hit then break end
            end
            if hit then reg.hook(changes) end
        end
    end

    function stub.register(monitored, hook, associated)
        local mon = type(monitored) == "string" and { monitored } or monitored
        local reg = { monitored = mon, hook = hook }
        table.insert(regs, reg)
        return reg
    end

    function stub.unregister(handle)
        for i, reg in ipairs(regs) do
            if reg == handle then table.remove(regs, i); return end
        end
    end

    return stub
end

-- ---------------------------------------------------------------------------
-- stub os.execute: capture systemctl commands, report success
-- ---------------------------------------------------------------------------

local executed = {}
os.execute = function(cmd)
    executed[#executed + 1] = cmd
    return 0
end

local function contains(needle)
    for _, cmd in ipairs(executed) do
        if cmd:find(needle, 1, true) then return true end
    end
    return false
end

-- ---------------------------------------------------------------------------
-- assertions
-- ---------------------------------------------------------------------------

local failures = 0
local function check(cond, msg)
    if cond then print("PASS  " .. msg) else failures = failures + 1; print("FAIL  " .. msg) end
end

-- ---------------------------------------------------------------------------
-- the tests
-- ---------------------------------------------------------------------------

-- load every module from the manifest and check the models parse
local models = {}
for _, mod in ipairs(require 'apps.modules.manifest') do
    models[#models + 1] = schema.load(require(mod))
end
check(#models == 5, "manifest yields 5 module models")

local tm = new_stub()
local h = supervisor.start(models, tm)

-- defaults + namespace prefixing (config.<namespace>.<leaf>)
check(tm.get("config.openvpn.client.enabled") == false, "default: openvpn.client.enabled == false")
check(tm.get("config.cellular.enabled") == false, "default: cellular.enabled == false")

-- nested container rel paths, LwM2M DTLS/PSK (iot-repo conventions)
check(tm.get("config.lwm2m.client.dm.uri") == "coaps://leshan.eclipseprojects.io:5684",
      "default: lwm2m.client.dm.uri is coaps (DTLS)")
check(tm.get("config.lwm2m.client.security.mode") == "PSK", "default: lwm2m.client security.mode == PSK")
check(tm.get("config.lwm2m.client.psk.id") == "iot-client", "default: lwm2m.client psk.id")
check(tm.get("config.lwm2m.server.bs.security.mode") == "PSK", "default: lwm2m.server bs.security.mode == PSK")
check(tm.get("config.lwm2m.server.dm.psk.id") == "iot-dm-client", "default: lwm2m.server dm.psk.id")

-- YANG reference: every config leaf is mirrored into state.* (mirror = true)
check(tm.get("state.openvpn.client.enabled") == false, "reference: state.openvpn.client.enabled == config")
check(tm.get("state.lwm2m.client.dm.uri") == "coaps://leshan.eclipseprojects.io:5684",
      "reference: state.lwm2m.client.dm.uri mirrors config")
check(tm.get("state.openvpn.server.subnet") == "10.8.0.0",
      "reference: state.openvpn.server.subnet mirrors config")

-- operational-only state leafs (no config counterpart), initialised from defaults
check(tm.get("state.openvpn.client.running") == false, "init: state.openvpn.client.running == false")
check(tm.get("state.openvpn.client.pid") == 0,           "init: state.openvpn.client.pid == 0")
check(tm.get("state.openvpn.client.last_error") == "",   "init: state.openvpn.client.last_error == ''")
check(tm.get("state.lwm2m.server.running") == false,     "init: state.lwm2m.server.running == false")

-- boot pass: default enabled=false -> systemctl stop each unit (harmless no-op)
check(contains("systemctl stop openvpn-client.service"), "boot pass stops openvpn-client")
check(contains("systemctl stop lwm2m-client.service"),   "boot pass stops lwm2m-client")
check(contains("systemctl stop cellular.service"),       "boot pass stops cellular")

-- live knob -> start (mirror tracks config.enabled, supervisor writes running)
tm.set("config.openvpn.client.enabled", true)
check(contains("systemctl start openvpn-client.service"), "knob on -> systemctl start openvpn-client")
check(tm.get("state.openvpn.client.running") == true,     "knob on -> state.openvpn.client.running == true")
check(tm.get("state.openvpn.client.enabled") == true,     "knob on -> state.enabled mirrors config.enabled")
check(tm.get("state.openvpn.client.last_error") == "",    "knob on -> last_error cleared")

-- live knob -> stop
tm.set("config.openvpn.client.enabled", false)
check(tm.get("state.openvpn.client.running") == false,    "knob off -> state.openvpn.client.running == false")
check(contains("systemctl stop openvpn-client.service"),  "knob off -> systemctl stop openvpn-client")

-- failed start -> running stays false, last_error records it
os.execute = function(cmd) executed[#executed + 1] = cmd; return 1 end
tm.set("config.openvpn.client.enabled", true)
check(tm.get("state.openvpn.client.running") == false,    "failed start -> running stays false")
check(tm.get("state.openvpn.client.last_error") ~= "",    "failed start -> last_error populated")
os.execute = function(cmd) executed[#executed + 1] = cmd; return 0 end

-- stop() unregisters cleanly (no error) and later changes are inert
h:stop()
tm.set("config.cellular.enabled", true)
check(not contains("systemctl start cellular.service"), "after stop(): cellular knob no longer drives systemd")

print(("\n%d failure(s)"):format(failures))
os.exit(failures == 0 and 0 or 1)
