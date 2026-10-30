# Eclipse Mihini — Data Store

This document explains the **data-store subsystem** of [Eclipse Mihini](https://wiki.eclipse.org/Mihini), the
M2M/IoT embedded runtime contributed to the Eclipse Foundation by Sierra Wireless around 2012 and later
archived. Mihini is a lightweight Linux runtime that exposes a high-level **Lua** API for building
machine-to-machine applications (alongside Eclipse **Koneki** for Lua tooling and **Paho** for messaging).

In Mihini the generic "data store" is not one class but a small family of cooperating modules:

| Concern | Module(s) | Backing |
|---|---|---|
| **Hierarchical data tree** (read / write / monitor) | `agent.treemgr.*` (the Tree Manager) | CDB mapping DB + pluggable *handlers* |
| **Persisted device configuration** | `agent.config` (the Config Store) + `agent.treemgr.handlers.agentconfig` | `persist.table` on flash/RW filesystem |
| **Store-and-forward of app data to a server** | `agent.asscon.datamanager` + `stagedb` | row-oriented tables, per-policy flushing |

The Tree Manager is the heart of the system. The Config Store is a *handler* mounted into that tree, and the
Data Manager is a sibling subsystem that consumes the same idea of "paths → data" but optimizes for batching and
sending records over the wire.

---

## 1. The Tree Manager (`treemgr`)

### Purpose

Present a device's data as a set of **variables organized in a hierarchical tree**, which can be **read**,
**written**, and **monitored for changes** by user applications (and, through the agent, by a remote server).

The design is split into three parts:

1. **A logical tree** — user-friendly, portable, hierarchical; this is what applications see.
2. **Handler trees** — device-specific code that actually manipulates the data. Their internal layout is an
   implementation detail and does not have to match the logical tree.
3. **A mapping** — translates logical paths ↔ handler paths. It is compiled into **CDB** (Constant DataBase)
   files so that a large mapping does not have to fit entirely in RAM.

```
              Logical tree (what apps see)
                    ┌────────────────────┐
                    │  system.position.  │
   treemgr.get() ──▶│    latitude        │◀── treemgr.register(hook)
   treemgr.set() ──▶│  system.time       │
                    │  config.*          │
                    └─────────┬──────────┘
                              │  mapping (CDB: l2h / h2l / l2c / l2m)
              ┌───────────────┼────────────────┐
              ▼               ▼                ▼
        ┌──────────┐   ┌────────────┐   ┌──────────────┐
        │  aleos   │   │   time     │   │ agentconfig  │   handler trees
        │ handler  │   │  handler   │   │  handler     │   (get/set/register)
        └──────────┘   └────────────┘   └──────────────┘
```

### Key concepts

- A **path** is a sequence of identifiers separated by dots (`"system.position.latitude"`).
- **Leaf nodes** carry a value and have no children. **Non-leaf nodes** have children but no value.

```
-a
  +-b
  | +-c = 123      <- leaf, value 123
  +-d = 234        <- leaf, value 234
```

- A **mountpoint** associates a path in the logical tree with `(handler, handler-path)`. Mountpoints may map
  leaf *or* non-leaf nodes, and non-root handler nodes can be mounted. Mapping `config:<root>=agentconfig`
  mounts the whole handler tree recursively, while mapping `system.position.latitude=aleos:GPS_LATITUDE` maps a
  single variable.

### Naming conventions (used throughout the code)

| Abbrev. | Meaning |
|---|---|
| `hpath` | path relative to a handler (leaf or non-leaf) |
| `hlpath` | a handler's *leaf* node |
| `lpath` | absolute path in the logical tree (leaf or non-leaf) |
| `llpath` | absolute path to a logical *leaf* node |
| `nlnpath` | non-leaf logical path |
| `hmap` | `hpath → value` table |
| `lmap` | `lpath → value` table |

---

## 2. Sub-modules

| Sub-module | Role |
|---|---|
| `agent.treemgr` | Core engine: coordinates handlers with the logical view; `get`/`set`/`register`/`notify`. |
| `agent.treemgr.db` | Interface to the compiled CDB databases describing the mapping. |
| `agent.treemgr.build` | Compiles `*.map` files into `*.cdb` databases. |
| `agent.treemgr.table` | Proxy tables exposing the tree as nested Lua tables (`__index`/`__newindex`/`__pairs`). |
| `agent.treemgr.handlers.*` | The actual handler implementations (see §5). |

---

## 3. Public API

### `treemgr.get(lpath)` / `treemgr.get(lpath_list)`

- Leaf node → returns its value.
- Non-leaf node → returns `nil` + the list of direct children.
- Batch (list of paths) → returns an `lmap` of values and/or child paths.

```lua
treemgr.get("a.d")                 --> 234
treemgr.get("a")                   --> nil, { "a.b", "a.d" }
treemgr.get{ "a.d", "a.b" }        --> { ["a.d"]=234 }, { "a.b.c" }
```

### `treemgr.set(...)`

Multiple equivalent forms, all writing leaf values:

```lua
set{ ['a.b.c'] = 123 }
set('',  { ['a.b.c'] = 123 })
set('a', { ['b.c']  = 123 })
set('a.b', { c = 123 })
set('a.b.c', 123)
```

`set` sorts the incoming paths by handler and performs **at most one `handler:set` call per handler**, so a
single logical write stays a single handler write.

### `treemgr.register(monitored_lpath_list, hook, associated_lpath_list)`

Registers `hook` to be called every time one of the *monitored* paths changes. The hook receives an
`llpath → value` map that is the union of: every monitored variable that changed, **plus** every *associated*
variable (whether it changed or not). A variable whose value is needed even when unchanged must be listed in
both lists.

### `treemgr.unregister(hook)`

Cancels a hook, and only de-registers from a handler when no other hook still needs that handler path.

### `treemgr.notify(handler_name, hmap)` — called *by handlers*

A handler signals that a set of its variables changed. The engine converts `hpath`s into `lpath`s, finds the
relevant hooks, adds any missing associated variables, and invokes each hook with the resulting map.

---

## 4. Mapping, CDB, and handler loading

- The mapping is compiled from `*.map` files into **CDB** (Constant DataBase, based on
  [D. J. Bernstein's cdb](http://cr.yp.to/cdb.html)) so path translation is constant-memory and constant-time.
- **Four dictionaries** are kept:

| DB file | Key → value | Used by |
|---|---|---|
| `treemgr.l2h.cdb` | `lpath → handler_name:hpath` | `get` / `set` |
| `treemgr.h2l.cdb` | `handler_name:hpath → lpath` | `notify` |
| `treemgr.l2c.cdb` | `lpath → direct children lpaths` | `get` on non-leaf nodes |
| `treemgr.l2m.cdb` | `lpath → handlers mounted below` | `register` |

- **Map file format** (one handler per file): the first line is `treemgr <module_name>`; every following line is
  `<logical_path>=<handler_path>`; `#` starts a comment and blank lines are ignored. Example
  (`agentconfig.map`):

```
treemgr agent.treemgr.handlers.agentconfig
config=
```

- `treemgr.build` compiles all `*.map` files in the resources directory into `persist/treemgr/treemgr.*.cdb`,
  and also emits generated `load.lua` (loads every referenced handler) and `rebuild.lua` (rebuilds the DB).
- On startup, `treemgr.db.init()` **recompiles the DB automatically** if any `.map` file is newer than the
  compiled DB, then opens the four `.cdb` files.
- **Handlers are loaded lazily** via Lua's `require`: the handler's name is a Lua module name, and loading the
  module must return the handler object.

---

## 5. Handlers

A handler is any Lua object implementing the handler interface:

- `handler:get(hpath)` → value (leaf), or `nil` + error string, or `nil` + children set
  (`{ x=true, y=true }`, keys are child names, not values).
- `handler:set(hmap)` → writes a map of `hlpath → value` pairs.
- `handler:register(hpath)` / `handler:unregister(hpath)` → subscribe/unsubscribe change notifications.
- The handler calls `treemgr.notify(handler_name, hmap)` when a registered variable changes.

The handlers shipped with the agent:

| Handler | Backing | Notes |
|---|---|---|
| `ramstore` | RAM `values`/`nlnodes` tables | Reference example; full get/set/register/notify. Mounted at `ram`. |
| `agentconfig` | `agent.config` (persisted) | Presents the persisted agent configuration; handles notification via a config signal. Mounted at `config`. |
| `table` | a plain nested Lua table | Generator; read/write only, **no notification**. |
| `functable` | a flat table of per-leaf `get`/`set` functions | Generator; optional auto-notify on set. |
| `constant` | a read-only table | `set` always errors; used e.g. when `update` is disabled. |
| `time` | computed values | Exposes `system.date` (→ `os.date()`) and `system.timestamp` (→ `os.time()`); read-only. |
| `appcon` | `agent.appcon` daemon | Application-container attributes, cached for ~3 s; adds a writable `Started` flag. |
| `cellular` | AT-command interface (`at`) | Live cellular/radio status via `AT` commands. |
| `update` | `agent.update.common` | Software-update status tree (`swlist`, `currentupdate`). |
| `monitoring` | monitoring module | Mounted at `monitoring`. |
| `dummystatusreport` | static table (built on `table`) | Sample device status report. |
| `extvars` | native C extension (`.so`) | Device-specific variables exposed from C (e.g. `aleos`, `treehdlsample`). |

Example — `ramstore.lua` `:set()` demonstrates the notify contract: it walks each path's prefixes, maintains the
parent→children relationship, and if any changed prefix is registered, calls
`treemgr.notify(self.name, hmap)`.

---

## 6. The Config Store (`agent.config`)

The persistent agent configuration is the most important *data* in the tree, and it is exposed two ways:

- **Inside the agent** via `require 'agent.config'`, which returns a proxy over the `config` subtree of
  `agent.treemgr.table` (nested-table syntax).
- **Remotely** via M3DA (`ReadNode` / data write), path-prefixed by `@sys.config`; and via the REST API under
  `GET/PUT /devicetree/config/...`.

### Persistence

- `agent.config` stores values in a `persist.table` named `"ConfigStore"` — a flat key/value store keyed by
  fully-qualified paths, persisted **synchronously** on every `set()`.
- On Linux-like targets the persisted config lives in `./persist/ConfigStore` (a custom database format on a
  read/write filesystem, often flash).
- The default configuration template is `agent/defaultconfig.lua`. On first run (or after a `make clean`), the
  agent copies the default config to the persisted store. Changing `defaultconfig` afterwards does **not** change
  the running config — `agent.config.default()` reloads it.

### Config API

```lua
agent.config.get(path)       -- get a value / subtree
agent.config.set(path, value)-- set a value (persisted synchronously)
agent.config.default(path)   -- reload subtree from defaultconfig.lua
agent.config.diff(path)      -- list items differing from the default config
agent.config.pdiff(path)     -- pretty-print that diff
```

The `agentconfig` treemgr handler simply delegates `:get`/`:set` to `agent.config` and forwards change
notifications via the `agent.config` `notify` signal.

---

## 7. How the tree is reached (protocols)

The `treemgr` API itself is **in-process Lua**. Applications and the server reach the tree over the wire through
three different transports — and EMP is the one used by on-device asset applications.

### EMP — asset applications ⇄ agent

Asset applications (via the `devicetree` / AirVantage client library) talk to the agent's Asset Connector
(`agent.asscon`) using the **Embedded Micro Protocol (EMP)**. `agent/devman/init.lua` registers thin EMP wrappers
over `treemgr`:

| EMP command | Direction | `treemgr` call |
|---|---|---|
| `GetVariable` | App → Agent | `treemgr.get(path)` |
| `SetVariable` | App → Agent | `treemgr.set(path, value)` |
| `RegisterVariable` | App → Agent | `treemgr.register(regvars, hook, passivevars)` → returns a registration id |
| `NotifyVariable` | Agent → App | change notification delivered to the registered hook |
| `UnregisterVariable` | App → Agent | `treemgr.unregister(id)` (the doc names it `DeRegisterVariable`) |

So the full **set / get / register / unregister / notify** cycle runs over EMP: `RegisterVariable` registers a
hook that, on change, re-sends the changed-variable map back to the app as a `NotifyVariable` EMP message
(`agent/devman/init.lua`).

### M3DA — server ⇄ agent

The remote server does **not** speak EMP. It reads the tree with the **M3DA** `ReadNode` command
(`agent/devman/cmds/ReadNode.lua`, which walks `treemgr.get` and pushes data back) and writes values with a data
write (`treemgr.set`). Persisted configuration is addressed under `@sys.config`.

### REST — `GET` / `PUT /devicetree/...`

When `rest.activate` is set, `agent/devman/init.lua` additionally mirrors `get`/`set` over HTTP at
`/devicetree/<path>`.

> There is no separate "delete" command: un-registering change notifications is `UnregisterVariable`, and
> clearing a stored value is done by setting it to `nil` (`niltoken`). The Data Manager (§8) uses a *different*
> set of EMP messages (`PData`, `PFlush`, `TableNew`, …) for store-and-forward records.

## 8. Data Manager (`datamanager`) — store and forward

Complements the tree store for data that must reach a server. `agent.asscon.datamanager` receives EMP
(Embedded Micro Protocol) messages from asset applications (`PData`, `PFlush`, `PAcknowledge`, `TableNew`,
`TableRow`, `TableSetMaxRows`, `ConsoNew`, `ConsoTrigger`, `SendTrigger`, `TableReset`), and:

- stores records in **row-oriented tables** built on `stagedb` (RAM- or flash-backed),
- supports **consolidation** (merging many source rows into a destination table),
- flushes tables to the server through `srvcon`, and
- sends acknowledgements (RAM or persisted).

Data is grouped under named **policies** (e.g. `default`, `now`, `never`, `on_boot`), each with triggers such as
`manual`, `latency`, `period`, `cron`, or `onboot`, configured under `data.policy.*` in the config store.

---

## 9. Examples

The APIs split cleanly by *where* they run: `devicetree` and `airvantage` run **in an asset application**
(speaking EMP to the agent), while `treemgr` and `agent.config` run **inside the agent** (in-process Lua).

### 9.1 From an asset application — `devicetree` (over EMP)

Every call below is an EMP message to the agent (`GetVariable`/`SetVariable`/`RegisterVariable`/
`UnregisterVariable`), and change notifications arrive back as `NotifyVariable`.

```lua
local dt = require 'devicetree'
dt.init()

-- Read a leaf value
local fw = dt.get("system.sw_info.fw_ver")            -- e.g. "4.2.5"

-- Read a subtree: a non-leaf node returns nil + its direct children
local value, children = dt.get("system.sw_info")      -- value=nil,
                                                      -- children={"system.sw_info.fw_ver", ...}

-- Write a single value
dt.set("config.monitoring.activate", true)

-- Write a whole subtree in one call (value can be a key/value table)
dt.set("ram", { counter = 0, label = "boot" })

-- Register a change notification
local id = dt.register(
    "system.gps.latitude",                            -- variable(s) to monitor
    function(changes) print("GPS changed:", changes) end,  -- callback gets a path->value map
    "system.gps.longitude"                            -- passive var: reported even if unchanged
)

-- Unsubscribe later
dt.unregister(id)
```

### 9.2 Inside the agent — `treemgr` (in-process Lua)

Same semantics as `devicetree`, but called directly inside the agent process (no wire protocol):

```lua
local treemgr = require 'agent.treemgr'

treemgr.set("ram.counter", 42)
local v = treemgr.get("ram.counter")                  -- 42

-- Set several values at once (prefix + relative keys, or absolute paths)
treemgr.set("ram", { counter = 43, label = "hello" })
treemgr.set{ ["ram.counter"] = 44, ["ram.label"] = "world" }

-- Register a change hook, then unregister it
local hook = treemgr.register(
    "ram.counter",
    function(lmap) print("counter is now", lmap["ram.counter"]) end
)
treemgr.unregister(hook)
```

### 9.3 Agent configuration — `agent.config`

The persisted Config Store is exposed as nested Lua tables (a proxy over the `config` subtree) plus an
explicit API:

```lua
local config = require 'agent.config'

-- Nested-table access (reads/writes go through treemgr)
print(config.mediation.timeout)
config.mediation.activate = false

-- Explicit API
config.set("network.activate", true)
local v = config.get("network.bearerpriority")
config.default("mediation")    -- reload the 'mediation' subtree from defaultconfig.lua
config.pdiff("")               -- pretty-print values that differ from the defaults
```

### 9.4 Push data to the server — `airvantage` (store-and-forward)

Separate from the tree: `asset:pushData` buffers records (EMP `PData`), and a policy flush sends them:

```lua
local airvantage = require 'airvantage'
airvantage.init()

local asset = airvantage.newAsset("sensor1")
asset:start()

asset:pushData("temperature", { value = 23.5, unit = "C" }, "now")
-- → sends <assetroot>.temperature.value and <assetroot>.temperature.unit

airvantage.triggerPolicy("now")   -- flush the 'now' policy immediately
```

## 10. Source map

```
agent/agent/
├── treemgr/
│   ├── init.lua          # core engine: get/set/register/unregister/notify
│   ├── db.lua            # CDB access + auto-rebuild on startup
│   ├── build.lua         # *.map  →  *.cdb compiler
│   ├── table.lua         # treemgr-as-nested-tables proxy
│   ├── maps/*.map        # logical↔handler mappings (one handler per file)
│   └── handlers/*.lua    # handler implementations (ramstore, agentconfig, …)
├── config.lua            # Config Store (agent.config), persist.table-backed
└── asscon/datamanager.lua# data queues / store-and-forward (stagedb-backed)

libs/keystore/            # C key-value store (separate, lower-level)
luafwk/log/log_store.*    # C log storage
```

---

## 11. License & provenance

- **License:** Eclipse Public License v1.0 (`http://www.eclipse.org/legal/epl-v10.html`).
- **Copyright:** © 2012 Sierra Wireless and others.
- **Original contributors:** Fabien Fleutot (treemgr), Laurent Barthelemny, Cuero Bugot, David Francois
  (config), Julien Desgats (datamanager), among others.
- The original source lived at `git://git.eclipse.org/gitroot/mihini/org.eclipse.mihini.git` (now archived/offline).
  The code reviewed for this document is the mirror `nim65s/mihini-repo` (a pure clone of the official repo).

## References

- [Eclipse Mihini project](https://wiki.eclipse.org/Mihini)
- [Mihini — Lua Application Framework project proposal (Eclipse m2m-iwg)](https://www.eclipse.org/lists/m2m-iwg/msg00199.html)
- [Mihini REST API](https://wiki.eclipse.org/Mihini/Rest)
- [Mihini source mirror used here](https://github.com/nim65s/mihini-repo)
- [cdb — constant database](http://cr.yp.to/cdb.html)
