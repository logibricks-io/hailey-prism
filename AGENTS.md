# Repository Guidelines

## Project overview

Prism is a browser product for "humans + AI agents sharing one browser", built on three pillars:

1. **Prism.app** — a Chromium fork (macOS) providing Space isolation, a kernel-level snapshot, and a custom `Prism.*` DevTools domain.
2. **prism-browser harness** (`package/prism-browser/`) — the agent-side CLI/SDK, vendored from ego-lite (MIT; preserving the copyright header and `NOTICE` is a hard requirement). Agents drive the browser with stdin heredoc JavaScript.
3. **prism-host adapter** (`host/`) — bridges the harness's expected `globalThis.prism` binding object to the CDP pipe / custom DevTools domain. **No Node embedded in the browser process** (ADR-002).

Data flow: `stdin JS` → harness helpers → `prism.sendCDPMessage` → prism-host → CDP pipe → Chromium (standard domains + the `Prism.*` custom domain) → `prism.onCDPMessage` callbacks back.

## Authoritative specs

- **`docs/binding-contract.md` is the single source of truth for browser-side development** (binding methods, the 15 error codes, the snapshot contract, hard-stop semantics). Changing it requires an ADR first.
- The helper surface follows the v2 facades (`page/browser/taskSpaces/site/fetch/cdp/help`); the legacy flat naming recorded in SKILL.md is compatibility reference only — see `docs/compatibility.md`.

## Language policy

All repository content — code, comments, docs, commit messages — is **English**, no exceptions.

## Development commands

- `cd package/prism-browser && npm test` — harness regression suite (build + typecheck + `node --test`); tests import the `dist/src` build output, so a build runs first.
- `cd package/prism-browser && npm run e2e` — real-browser acceptance (requires a local Chromium/Prism plus the CLI).
- `npm run validate:site-skills` — validates `skills/prism-browser/learnings/`.
- Chromium-side commands: see `chromium/README.md`.

## Conventions

- ESM only (`"type": "module"`); Node 22+.
- Public helpers go through `helperContext()` and need JSDoc (`help()` reads it at runtime); keep `skills/prism-browser/SKILL.md` in sync when the helper surface changes.
- Time parameters are milliseconds by default; the few APIs without an `Ms` suffix (`waitForAgentControl`, `fetch.*`) use seconds — this follows the upstream convention, see `docs/compatibility.md`.
- Element-resolution failures use `ElementResolutionError` with an honest `transient/permanent` kind (wait loops depend on it).
- Snapshot refs (`@N`) are short-lived: re-snapshot after navigation or DOM changes; prefer `loc=...` for long-lived references.
- New kernel code lives in Chromium's `//prism/` directory (injected via src-overlay); upstream modifications go through small, self-contained patches in `chromium/patches/`.
- Site learnings must stay site-shaped and verifiable: stable URLs, durable selectors, no pixel coordinates, no secrets.

## Testing & QA

- Harness: Node's built-in runner + `node:assert/strict`; behavior tests inject a FakePrism double (inherited from the upstream FakeEgo pattern) and `__testing.setOverrides`.
- Contract tests: every hard rule in `docs/binding-contract.md` (the 15 error codes, hard-stop output collapse, snapshot reject-only) needs an executable test.
- Acceptance: the same `e2e/` suite runs first against stock Chromium (adapter simulation mode), then against the fork (passthrough mode); both must be green.
- **Silent testing (hard rule)**: this is the user's daily-work machine. Never raise, activate, or focus test windows; launch test instances on a throwaway `--user-data-dir` with `--window-position=3000,3000` (off-main-display) or minimized. No synthetic real-cursor events (CGEvent/cliclick), no full-screen `screencapture` recordings. Verify UI state via CDP (`Page.captureScreenshot` works on occluded windows) or cursor-safe background tooling only.
