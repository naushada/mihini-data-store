# Yocto build for the Mihini data-store (Raspberry Pi 3B)

Containerized Yocto build that produces a bootable image for the **Raspberry Pi 3B**
(`raspberrypi3-64`), packages the Mihini data-store stack, and copies the deploy
artifacts back to a host directory.

## Status (be honest with yourself before assuming this builds)

| Piece | State |
|---|---|
| `mihini-schema` recipe | Written; installs `schema/*.lua` + `state.map`. Correct-by-construction (simple file copy), **not yet run through bitbake**. |
| `mihini-datastore` recipe | Written; cross-compiles the C++ lib + CLI via `cmake`. Correct-by-construction, **not yet run through bitbake**. |
| `mihini-agent` recipe | **Skeleton only.** The vendored 2012-era C/Lua must be ported to the Scarthgap (gcc 12/13) toolchain before the agent builds. This is the main remaining work. |
| Container + `build.sh` | Written; clones poky/meta-openembedded/meta-raspberrypi (Scarthgap), builds in `crops/poky`, copies artifacts to the host. Untested end-to-end. |

The two recipes that matter first (schema + datastore) are solid, but the full
image build has **not been executed** — a first Scarthgap build is tens of GB and
hours long, and the agent port is a real project of its own.

## Layout

```
yocto/
├── build.sh                    # clone layers, build in container, copy artifacts
├── Dockerfile                  # optional custom build image (crops/poky + flash tools)
├── conf/
│   ├── local.conf.sample       # MACHINE=raspberrypi3-64, caches, rpi settings
│   └── bblayers.conf.sample    # layer list (poky, meta-oe, meta-raspberrypi, meta-mihini)
└── meta-mihini/
    ├── conf/layer.conf
    └── recipes-mihini/
        ├── mihini-schema/      # schema/*.lua + state.map -> ${datadir}/mihini
        ├── mihini-datastore/   # libmihini-datastore.a + datastore-cli
        └── mihini-agent/       # SKELETON (port pending)
    └── recipes-core/images/core-image-base.bbappend
```

## Quick start

```sh
# from the repository root
YOCTO_WORKSPACE=$HOME/yocto-rpi3 ./yocto/build.sh
```

What `build.sh` does:

1. Clones `poky`, `meta-openembedded`, `meta-raspberrypi` (Scarthgap branch) into
   the workspace if absent.
2. Creates `downloads/`, `sstate-cache/`, `build/`, `deploy/` in the workspace.
3. Runs `crops/poky:ubuntu-22.04`, mounting the workspace at `/workdir` and this
   repo at `/repo` (so `meta-mihini` and the source are visible to bitbake).
4. Inside the container: writes `local.conf`/`bblayers.conf`, sources
   `oe-init-build-env`, and runs `bitbake core-image-base`.
5. Copies `build/tmp/deploy/images/raspberrypi3-64/` to `deploy/` on the host.

Artifacts on the host: `$YOCTO_WORKSPACE/deploy/raspberrypi3-64/`
(`*.wic.bz2` for flashing, `*.tar.bz2` rootfs, packages, kernel, etc.).

Flashing the SD card (example):

```sh
bmaptool copy \
  $YOCTO_WORKSPACE/deploy/raspberrypi3-64/core-image-base-raspberrypi3-64.rootfs.wic.bz2 \
  /dev/sdX
```

## What gets into the image today

- `mihini-schema` — `schema/init.lua`, `schema/appconfig.lua`, and `schema/state.map`
  installed under `${datadir}/mihini`.
- `mihini-datastore` — `libmihini-datastore.a` (headers + static lib) and the
  `datastore-cli` binary.

The `mihini-agent` package is commented out in the image bbappend until its recipe
is ported.

## Notes / gotchas

- **Reproducibility:** recipes use `SRCREV = "${AUTOREV}"`. Pin the commit once the
  repository stabilizes.
- **Commit first:** the recipes fetch from GitHub
  (`naushada/mihini-data-store`, branch `main`), so push the schema changes before
  building, or the schema recipe will find no `schema/` directory.
- **Agent port:** the vendored Mihini under `mihini/` builds `cfwk`
  (`swi_devicetree` / `swi_dset` / `Emp`), `luafwk` (Lua 5.1 + modules), and the
  agent. It was written for gcc 4.x-era toolchains; expect build fixes for a
  Scarthgap (gcc 12/13) target.
- **Runtime paths:** `mihini-schema` installs to `${datadir}/mihini`; the exact
  runtime Lua search path and treemgr maps directory must match whatever the
  (ported) agent uses.
