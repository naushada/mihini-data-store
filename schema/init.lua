-- schema.lua -- a YANG-flavoured declarative config model + the "config -> state"
-- mirror rule, running over the Mihini tree manager.
--
-- The *model* is a pure-data Lua file (see appconfig.lua): a tree of containers and
-- typed leafs, with defaults and constraints, in the spirit of a YANG module.
-- This module turns that model into behaviour against a treemgr-compatible interface:
--
--     get(path)                 -> value | nil  (nil for an absent leaf)
--     set(lmap | path, value)   -> write one or many leaf values
--     register(monitored_list, hook [, associated_list]) -> registration handle
--     unregister(handle)
--
-- Usage inside the agent:
--
--     local schema = require 'schema'
--     local model  = schema.load(require 'schema.appconfig')
--     model:apply_defaults()        -- write defaults for any missing config leaf
--     local mirror = model:start_mirror()   -- mirror state=true leafs into state.*
--     -- ... later:
--     mirror:stop()
--
-- A leaf is any model node that carries a `type`; everything else that is a table is
-- a container. Leaf fields:
--     type      string   "boolean" | "integer" | "float" | "string" | "list:<type>"
--     default   *        written on first boot when the leaf is absent
--     range     {min,max} integer only
--     pattern   string   Lua pattern, string only
--     enum      list     allowed values, string only (YANG-like enumeration)
--     state     bool|str  (on a config leaf) reference just this leaf into the state
--                         container, or an explicit string path; see `mirror` below
--
-- Model fields (YANG / OpenConfig shape):
--     config     required  the intended-config tree (config true leafs)
--     state      optional  the operational-state tree (config false leafs). These
--                          have no config counterpart and are written by an external
--                          actor (e.g. a supervisor reporting `running`, `pid`).
--     mirror     bool      reference EVERY config leaf into the state container
--                          (state.<namespace>.<rel>), OpenConfig-style. Default false;
--                          when true you usually don't need per-leaf `state`.
--     namespace  string    dot-path prefix applied to every config/state leaf path
--     store      "flash"|"ram"  backing for intended config (default "flash")

local M = {}

-- ---------------------------------------------------------------------------
-- path + model-walk helpers (self-contained; no dependency on utils.path)
-- ---------------------------------------------------------------------------

local function split_path(p)
    local t = {}
    for s in (p or ""):gmatch("[^%.]+") do t[#t + 1] = s end
    return t
end

local function is_leaf(node)
    return type(node) == "table" and type(node.type) == "string"
end

-- visit(relpath, leafspec) for every leaf under `config` (containers are walked).
local function each_leaf(config, prefix, visit)
    for name, node in pairs(config) do
        local rel = prefix and (prefix .. "." .. name) or name
        if is_leaf(node) then
            visit(rel, node)
        elseif type(node) == "table" then
            each_leaf(node, rel, visit)
        end
    end
end

-- ---------------------------------------------------------------------------
-- validation (YANG-like: type, range, pattern)
-- ---------------------------------------------------------------------------

local function validate(value, spec)
    local t = spec.type
    local base = t:match("^list:(.+)$")
    if base then
        if type(value) ~= "table" then return false, t .. " expects a table" end
        for _, v in ipairs(value) do
            local ok, err = validate(v, { type = base })
            if not ok then return false, err end
        end
        return true
    end
    if t == "boolean" then
        return type(value) == "boolean", "expected boolean"
    elseif t == "integer" then
        if type(value) ~= "number" or value % 1 ~= 0 then return false, "expected integer" end
        if spec.range and (value < spec.range[1] or value > spec.range[2]) then
            return false, ("value %s out of range [%s,%s]"):format(value, spec.range[1], spec.range[2])
        end
        return true
    elseif t == "float" then
        return type(value) == "number", "expected float"
    elseif t == "string" then
        if type(value) ~= "string" then return false, "expected string" end
        if spec.pattern and not value:match(spec.pattern) then
            return false, "value does not match pattern " .. spec.pattern
        end
        if spec.enum then
            for _, e in ipairs(spec.enum) do
                if value == e then return true end
            end
            return false, "value must be one of: " .. table.concat(spec.enum, ", ")
        end
        return true
    end
    return false, "unknown type " .. tostring(t)
end

-- ---------------------------------------------------------------------------
-- the model object
-- ---------------------------------------------------------------------------

local Model = {}
Model.__index = Model

function Model:_treemgr()
    -- The real treemgr is injected lazily so this module also loads (and can be
    -- unit-tested) outside a running agent.
    if not self._tm then
        self._tm = require 'agent.treemgr'
    end
    return self._tm
end

function Model:_prefix()
    return self.namespace and (self.namespace .. ".") or ""
end

-- Absolute logical path of a config / state leaf under this model's root.
function Model:config_path(rel)
    return self.config_root .. "." .. self:_prefix() .. rel
end

function Model:state_path(rel)
    return self.state_root .. "." .. self:_prefix() .. rel
end

-- visit(relpath, leafspec) for every operational-state leaf. Unlike the
-- config->state mirror, these are written by an external actor (e.g. a module
-- supervisor reporting what systemd actually started), not derived from config.
function Model:each_state_leaf(visit)
    each_leaf(self.state, nil, visit)
end

-- Write defaults for any config leaf that is absent, and validate whatever is
-- already present. Raises on an invalid existing value (fail fast at boot).
function Model:apply_defaults(treemgr)
    treemgr = treemgr or self:_treemgr()
    local writes = {}
    each_leaf(self.config, nil, function(rel, spec)
        local cpath = self:config_path(rel)
        local cur = treemgr.get(cpath)
        if cur == nil then
            if spec.default ~= nil then
                local ok, err = validate(spec.default, spec)
                assert(ok, "invalid default for " .. cpath .. ": " .. tostring(err))
                writes[cpath] = spec.default
            end
        else
            local ok, err = validate(cur, spec)
            if not ok then
                error(("invalid config %s: %s"):format(cpath, err), 2)
            end
        end
    end)
    if next(writes) then treemgr.set(writes) end
    return self
end

-- Write defaults for any operational-state leaf that is absent. Unlike config
-- leafs, existing state values are NOT validated here -- they are written by
-- external actors (supervisors, drivers), not by the config model.
function Model:apply_state_defaults(treemgr)
    treemgr = treemgr or self:_treemgr()
    local writes = {}
    self:each_state_leaf(function(rel, spec)
        local spath = self:state_path(rel)
        if treemgr.get(spath) == nil and spec.default ~= nil then
            local ok, err = validate(spec.default, spec)
            assert(ok, "invalid default for " .. spath .. ": " .. tostring(err))
            writes[spath] = spec.default
        end
    end)
    if next(writes) then treemgr.set(writes) end
    return self
end

-- Subscribe to every state=true config leaf and mirror its value into the state
-- container. Performs an initial mirror pass, then keeps the state container in
-- sync on every change. Returns a handle with :stop().
function Model:start_mirror(treemgr)
    treemgr = treemgr or self:_treemgr()

    local monitored, statemap = {}, {}
    each_leaf(self.config, nil, function(rel, spec)
        if self.mirror or spec.state then
            local cpath = self:config_path(rel)
            local spath = type(spec.state) == "string" and spec.state or self:state_path(rel)
            statemap[cpath] = spath
            monitored[#monitored + 1] = cpath
        end
    end)

    -- initial mirror: copy current config values into the state container.
    local init = {}
    for cpath, spath in pairs(statemap) do
        local v = treemgr.get(cpath)
        if v ~= nil then init[spath] = v end
    end
    if next(init) then treemgr.set(init) end

    -- live mirror: whenever a monitored config leaf changes, rewrite its state twin.
    local hook = treemgr.register(monitored, function(lmap)
        local writes = {}
        for cpath, v in pairs(lmap) do
            local spath = statemap[cpath]
            if spath then writes[spath] = v end
        end
        if next(writes) then treemgr.set(writes) end
    end)

    local handle = {}
    function handle:stop()
        treemgr.unregister(hook)
    end
    return handle
end

-- Convenience: config defaults + state defaults + the config->state mirror in one
-- call, returning the mirror handle.
function Model:apply(treemgr)
    treemgr = treemgr or self:_treemgr()
    self:apply_defaults(treemgr)
    self:apply_state_defaults(treemgr)
    return self:start_mirror(treemgr)
end

-- Load and check a model table. Fields:
--     config       (required) the config tree of containers/leafs
--     state        optional operational-state tree (written by an external actor)
--     namespace    optional dot-path prefix applied to every config/state leaf
--                  path (e.g. "openvpn.client" -> config.openvpn.client.enabled)
--     store        "flash" (default, persisted config.*) or "ram" (volatile ram.*)
--     config_root  override the logical root where intended config lives
--     state_root   override the logical root of the state container (default "state")
function M.load(model)
    assert(type(model) == "table", "schema model must be a table")
    assert(model.config ~= nil, "schema model needs a `config` subtree")

    local o = setmetatable({}, Model)
    o.config = model.config
    o.state = model.state or {}
    o.namespace = model.namespace
    o.unit = model.unit
    o.mirror = model.mirror
    o.store = model.store or "flash"
    o.config_root = model.config_root or (o.store == "ram" and "ram" or "config")
    o.state_root = model.state_root or "state"
    return o
end

return M
