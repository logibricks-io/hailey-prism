# Real-browser E2E acceptance

This directory holds the repo-root entry point for the real-browser E2E suite.
The suite itself — cases, runner, fixture server — is vendored inside the
harness package at
[`package/prism-browser/scripts/real-browser-e2e/`](../package/prism-browser/scripts/real-browser-e2e/)
(origin: ego-lite, MIT). **Cases are not copied here; they are the acceptance
standard and should stay untouched.**

Every case runs through Prism's own host stack:

```
node e2e/run.mjs
  └─ runner.mjs spawns, per case:  node host/src/cli.js   (stdin = preamble + case body)
       └─ cli.js auto-launches host/src/daemon.js  (unix socket: $TMPDIR/prism-host-<uid>.sock)
            └─ daemon drives stock Chrome via --remote-debugging-pipe
               (dedicated dev profile, never the user's real profile)
```

`cli.js` loads the harness bundle `package/prism-browser/dist/out/index.js`
(override with `PRISM_HARNESS_BUNDLE`); the runner pins it explicitly.

## Running

```bash
node e2e/run.mjs          # full suite from the repo root
```

Prerequisites: Node 22+, a stock Chrome/Chromium under
`/Applications` (or `PRISM_BROWSER_PATH`), and nothing else — the runner
builds the harness (`npm run build`) and starts the bundled fixture server
itself. The daemon and browser auto-launch on the first case; no manual setup
is needed even from a cold machine state.

The same suite can also be launched from the package with
`cd package/prism-browser && npm run e2e` — identical code path.

## Environment knobs

| Variable | Purpose |
|---|---|
| `PRISM_BROWSER_REAL_E2E_ONLY` | Comma-separated case names; run only these. |
| `PRISM_BROWSER_REAL_E2E_SKIP` | Comma-separated case names to skip (overrides the default skip list when set). |
| `PRISM_BROWSER_REAL_E2E_SKIP_REASONS` | JSON object `{ "case name": "reason" }`, shown in output and summary. |
| `PRISM_BROWSER_REAL_E2E_KEEP` | `1` keeps the task space (window) open after a green run. |
| `PRISM_BROWSER_REAL_E2E_VERBOSE_CASE_OUTPUT` | `1` echoes each case's stdout/stderr live. |
| `PRISM_BROWSER_REAL_E2E_VERBOSE_ASSERTIONS` | `1` logs every assertion inside a case. |
| `PRISM_BROWSER_PATH` / `PRISM_BROWSER_PROFILE` / `PRISM_HOST_SOCKET` / `PRISM_HOST_LOG` | Host adapter overrides, see [host/README.md](../host/README.md). |

## Default skip list

`run.mjs` seeds these skips (with reasons printed at startup); each entry is
removed automatically once the underlying capability lands:

| Case | Reason |
|---|---|
| `screencast recording` | The harness pipes `Page.startScreencast` frames into an `ffmpeg` subprocess and verifies the WebM with `ffprobe`. Skipped only when neither binary is found (`PRISM_BROWSER_FFMPEG_PATH` / `PRISM_BROWSER_FFPROBE_PATH` or `PATH`). |
| `macOS bare Meta input isolation` | Bare-Meta input isolation is a browser-shell capability (upstream fixed it in ego-lite's app layer). Under stock Chrome the synthetic Cmd keyDown/keyUp reaches the macOS shortcut layer and launches System Information when the dev window is frontmost (focus-dependent; probe-verified). The CDP adapter cannot isolate OS-level shortcuts without dropping legitimate `Input.dispatchKeyEvent` traffic — every Cmd chord starts with a Meta keyDown. Fork-era fix. |

Other capability-dependent cases run for real: the ticket-rush fixture is
served by the suite's own fixture server, and downloads work because the
adapter scopes `Browser.setDownloadBehavior` to the selected Space's browser
context and translates the browser-level `Browser.downloadWillBegin` /
`Browser.downloadProgress` events to the legacy `Page.download*` names the
harness listens for. If a case starts failing on the adapter, prefer fixing
the adapter; add a skip only with a recorded reason.

## Known limitations (simulation mode on stock Chromium)

- A Space is a **visible** browser window (browser-context simulation); stock
  Chromium cannot create hidden/background windows.
- Snapshot lines carry `ref=`/`loc=` annotations but no `url=` annotation
  (kernel renderer, Phase 3); `scope: "only_within_viewport"` currently
  returns the full-page tree.
- The dev profile has no real login state; Spaces (browser contexts) have
  isolated storage.
- Iframes do not surface as separate targets, so `browser.iframeTarget`
  resolves `null` and iframe-scoped checks degrade to logged warnings.
