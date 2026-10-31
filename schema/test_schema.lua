-- test_schema.lua -- self-contained unit test for the schema engine, using a
-- stub treemgr. Runs under any Lua 5.1+ (or LuaJIT) with no Mihini agent:
--
--     luajit schema/test_schema.lua
--
-- Exercises: default application, validation, the initial mirror, and live
-- mirror-on-change (the config -> state rule).

-- Make `require 'schema'` / `require 'schema.appconfig'` resolve from any CWD by
-- pointing the module path at this repository's root.
local this_dir = arg[0]:match("^(.*)/[^/]+$") or "."
package.path = (this_dir .. "/../?.lua;" .. this_dir .. "/../?/init.lua;") .. package.path

local schema = require 'schema'

-- ---------------------------------------------------------------------------
-- stub treemgr: a flat dot-path map with get/set/register/unregister
-- ---------------------------------------------------------------------------

-- The stub mirrors the real `agent.treemgr` API, which is a set of plain
-- dot-call functions (get / set / register / unregister), not colon methods.
local function new_stub()
    local values, regs = {}, {}
    local stub = {}

    function stub.get(path)
        return values[path]
    end

    function stub.set(path_or_map, value)
        local changes = {}
        if type(path_or_map) == "table" then
            for p, v in pairs(path_or_map) do
                values[p] = v
                changes[p] = v
            end
        else
            values[path_or_map] = value
            changes[path_or_map] = value
        end
        -- fire any hook that monitors a changed (or descendant) path
        for _, reg in ipairs(regs) do
            local hit = false
            for p in pairs(changes) do
                for _, mon in ipairs(reg.monitored) do
                    if p == mon or p:sub(1, #mon + 1) == mon .. "." then
                        hit = true
                        break
                    end
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
            if reg == handle then
                table.remove(regs, i)
                return
            end
        end
    end

    return stub
end

-- ---------------------------------------------------------------------------
-- assertions
-- ---------------------------------------------------------------------------

local failures = 0
local function check(cond, msg)
    if cond then
        print("PASS  " .. msg)
    else
        failures = failures + 1
        print("FAIL  " .. msg)
    end
end

local function eq(a, b)
    if type(a) ~= "table" then return a == b end
    if type(b) ~= "table" or #a ~= #b then return false end
    for i = 1, #a do if a[i] ~= b[i] then return false end end
    return true
end

-- ---------------------------------------------------------------------------
-- the tests
-- ---------------------------------------------------------------------------

local tm = new_stub()
local model = schema.load(require 'schema.appconfig')

-- 1. defaults are written for missing config leafs
model:apply_defaults(tm)
check(tm.get("config.network.activate") == false, "default applied: config.network.activate == false")
check(eq(tm.get("config.network.bearerpriority"), { "ETH", "GPRS" }),
      "default applied: list:string bearerpriority")
check(tm.get("config.server.retrytimes") == 10, "default applied: config.server.retrytimes == 10")

-- 2. defaults are NOT applied to already-present config
tm.set("config.network.retry", 7)
model:apply_defaults(tm)
check(tm.get("config.network.retry") == 7, "existing value preserved (config.network.retry == 7)")

-- 3. validation rejects out-of-range on existing values
tm.set("config.network.retry", 9999)
local ok = pcall(model.apply_defaults, model, tm)
check(not ok, "out-of-range existing value raises")
tm.set("config.network.retry", 7) -- restore

-- 4. the mirror rule: initial mirror + live sync
local mirror = model:start_mirror(tm)
check(tm.get("state.network.activate") == false, "initial mirror: state.network.activate")
check(tm.get("state.network.apn") == "", "initial mirror: state.network.apn")
check(tm.get("state.server.url") == "tcp://m2m.example.org:44900", "initial mirror: state.server.url")
check(tm.get("state.network.retry") == nil, "non-state leaf NOT mirrored (state.network.retry absent)")

-- 5. live mirror on config change
tm.set("config.network.activate", true)
check(tm.get("state.network.activate") == true, "live mirror: state tracks config.network.activate")

tm.set("config.server.url", "tcp://new-host:80")
check(tm.get("state.server.url") == "tcp://new-host:80", "live mirror: state tracks config.server.url")

-- 6. stop() unregisters the hook: further config changes no longer mirror
mirror:stop()
tm.set("config.network.activate", false)
check(tm.get("state.network.activate") == true, "mirror:stop() unregisters (state unchanged)")

-- ---------------------------------------------------------------------------

if failures == 0 then
    print("\nAll tests passed.")
    os.exit(0)
else
    print(("\n%d test(s) FAILED."):format(failures))
    os.exit(1)
end
