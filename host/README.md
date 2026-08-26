# prism-host

The `prism-browser` CLI plus the host daemon: bridges the `globalThis.prism` bindings the harness expects to the browser.

## Architecture (Phase 1 simulation mode)

```
Agent → prism-browser (cli.js)
          │  globalThis.prism = buildPrismBindings(client)
          │  import harness bundle → runMain(stdin JS)
          ▼  NDJSON over a unix socket ($TMPDIR/prism-host-<uid>.sock)
        daemon.js (long-lived; exclusively owns the browser pipe)
          │  --remote-debugging-pipe (fd 3/4, NUL-framed JSON)
          ▼
        Chromium / Chrome (a dedicated dev profile — never the user's real one)
```

- Each CLI process gets its own socket connection; Space selection is per-connection → parallel agents.
- Raw CDP passes through verbatim except `Target.getTargets / createTarget / attachToTarget`, which are intercepted (Space filtering / bookkeeping).
- Once the fork lands, task-space and snapshot switch to the passthrough-first `Prism.*` custom domain (simulation remains as fallback).

## Running

```bash
# Build the harness first (the CLI imports it)
cd ../package/prism-browser && npm install && npm run build

# Run a script (the daemon auto-launches the browser on first call)
cd ../../host
node src/cli.js <<'JS'
const task = await taskSpaces.useOrCreate("demo");
console.log("space", task);
await browser.openOrReuseTab("https://example.com");
await page.waitForLoadState("load", { timeout: 15 });
const snap = await page.snapshot();
console.log(snap.split("\n").slice(0, 5).join("\n"));
await taskSpaces.complete(task.id, { keep: true });
JS

# Diagnostics / connection reset
node src/cli.js --doctor
node src/cli.js --reload
```

Smoke test without the harness bundle: `node host/scripts/smoke.mjs` (from the repo root).

## Environment variables

| Variable | Purpose |
|---|---|
| `PRISM_BROWSER_PATH` | explicit browser binary |
| `PRISM_BROWSER_PROFILE` | dev profile dir (default `~/Library/Application Support/Prism/dev-profile`) |
| `PRISM_HOST_SOCKET` | daemon socket path override |
| `PRISM_HOST_LOG` | daemon log file (silent by default) |
| `PRISM_HOST_DEBUG` | `1` logs daemon internals (browser launches, per-space tab creation decisions) to the daemon log/stderr |
| `PRISM_HARNESS_BUNDLE` | harness bundle path override |

## Known limitations (simulation mode)

- A Space is a visible separate window on the desktop (stock Chromium has no hidden-window capability); the fork backgrounds them.
- The snapshot's `url=` annotation and viewport-only scope are unimplemented (Phase 3 kernel renderer).
- Login state comes from the dev profile, not the user's real Chrome profile (solved by the fork + the migration flow).
- Browser-context storage is isolated per Space in simulation mode; kernel Spaces share the profile.
