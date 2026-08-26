# Chromium fork infrastructure

Kernel-side assets of Prism.app, managed as **patch series on a pinned revision** (ADR-001):

- `DEPS.pin` — the pinned Chromium stable version (currently 151.0.7922.174).
- `patches/` — quilt-style patches against existing upstream files (one patch, one purpose; motivation in the header).
- `src-overlay/` — new source tree, copied into the Chromium `src/` at build time (mainly `//prism/`: the `Prism.*` DevTools domain, the snapshot composer, Space window-set management).
- `args/` — gn args (`args-arm64.gn` / `args-x64.gn`; release/branding args land with the packaging step).
- `branding/` — app name, icons, bundle id, theme colors.
- `scripts/` — see below.

## Scripts

- `fetch.sh` — depot_tools + fetch + sync to the pin + hooks. **Resumable**: every step is idempotent and retry-looped (`PRISM_FETCH_MAX_ATTEMPTS`, default 50); on any interruption re-run the same script and it picks up where it stopped. Honest limit: git discards a half-downloaded pack, so an interrupted *single repo* restarts its own transfer — everything already completed is kept. Progress log: `~/chromium/prism/fetch.log`.
- `apply.sh` — rsync `src-overlay/` into the tree + apply `patches/*.patch` (idempotent).
- `build.sh [arm64|x64]` — gn gen + autoninja chrome.
- `package.sh` — (Phase 2 packaging step) sign + notarize + dmg.

## Checkout location

The Chromium checkout **does not live in this repo**: default `~/chromium/prism/src`, overridable via `PRISM_CHROMIUM_SRC` (root via `PRISM_CHROMIUM_ROOT`). This repo's path contains spaces, which Chromium's toolchain does not tolerate.

## Receiving an offline copy (build machine only)

When ops delivers a pre-fetched tree (`chromium/offline-bootstrap.md` is their
runbook):

```bash
# restore to the standard layout
mkdir -p ~/chromium && cd ~/chromium
tar -xf /path/to/chromium-prism.tar.zst   # or rsync back from the drive
export PATH="$HOME/chromium/depot_tools:$PATH"

# DO NOT run `gclient sync` / `fetch` — the tree is complete and
# self-contained; syncing would hit the network.
chromium/scripts/apply.sh   # from the Prism repo
chromium/scripts/build.sh   # gn gen + autoninja
```

Build outputs (~50GB+) are produced locally and are never part of the
transfer.

## src-overlay/prism status

- `pdl/prism.pdl` — full `Prism.*` domain definition (design-complete).
- `browser/spaces/space_manager.{h,cc}` + unittest — the ownership state machine plus per-space tab bookkeeping (pure logic, no content/ deps).
- `browser/devtools/prism_domain_handler.{h,cc}` — compiled into content/browser via the patch series. Task-space lifecycle, `createTab`/`listTabs` (windowless agent WebContents owned by the session) and `getBrowserVersion` are live; `snapshot` is a Phase 3 stub.
- Verified end to end by `host/scripts/probe-domain.mjs` (pipe-direct kernel probe).
