# Chromium fork infrastructure

Kernel-side assets of Prism.app, managed as **patch series on a pinned revision** (ADR-001):

- `DEPS.pin` — the pinned Chromium stable version (filled in Phase 2).
- `patches/` — quilt-style patches against existing upstream files (one patch, one purpose; motivation in the header).
- `src-overlay/` — new source tree, copied into the Chromium `src/` at build time (mainly `//prism/`: the `Prism.*` DevTools domain, the snapshot composer, Space window-set management).
- `args/` — gn args (`args-arm64.gn` / `args-x64.gn`, official build + branding).
- `branding/` — app name, icons, bundle id, theme colors.
- `scripts/` — `fetch.sh` (depot_tools + fetch; 100GB-scale, one-time), `apply.sh` (patches + overlay), `build.sh`, `package.sh` (sign + notarize + dmg).

## Checkout location

The Chromium checkout **does not live in this repo**: default `~/chromium/prism/src`, overridable via `PRISM_CHROMIUM_SRC`. This repo's path contains spaces, which Chromium's toolchain does not tolerate.

> This directory activates in Phase 2; Phase 1 harness/adapter development only needs stock Chromium/Chrome (see host/README.md).
