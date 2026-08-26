# Prism architecture

## Overview

```
┌──────────────────────────── Prism.app (Chromium fork, macOS) ───────────────────────────┐
│  chrome UI: Spaces management (WebUI chrome://prism-spaces + native indicators)          │
│  ┌──────────────┐   ┌──────────────┐                                                     │
│  │ user windows  │   │ agent Spaces  │  … same profile, shared login state               │
│  │ (foreground)  │   │ (background   │                                                     │
│  │               │   │  window sets) │                                                     │
│  └──────────────┘   └──────────────┘                                                     │
│         ▲ custom DevTools domain: Prism.*                                                 │
│         │   Prism.listTaskSpaces / useTaskSpace / claimTaskSpace / ...                    │
│         │   Prism.snapshot (kernel AXTree composition, nested iframes included)           │
│         │   Prism.listTabs (filtered by Space) / Prism.createTab                          │
└─────────┼────────────────────────────────────────────────────────────────────────────────┘
          │ CDP over --remote-debugging-pipe (one connection per CLI process; the
          │ connection itself carries the Space selection state)
┌─────────┴──────────────── prism-browser CLI (Node 22, single-file bundle) ───────────────┐
│  prism-host adapter: implements the prism.* bindings as CDP calls (Prism.* domain        │
│  plus standard Target/Input/...); vendored harness (MIT, from ego-lite):                 │
│  page / browser / taskSpaces / site / fetch / cdp / help, learnings subsystem,           │
│  JSDoc-embedded help(), hard-stop output collapse                                        │
└─────────┼────────────────────────────────────────────────────────────────────────────────┘
          │ stdin heredoc JavaScript
     any agent CLI / our other agent products (spawn the CLI, or import the SDK)
```

## Data flow

1. The agent runs `prism-browser <<'JS' ... JS` via Bash (or embeds the SDK in-process).
2. The harness injects helpers into an AsyncFunction; `console.log` is rewired to a buffered sink (a hard stop discards the whole buffer and prints a single guidance line).
3. helper → `prism.sendCDPMessage(jsonString)` → prism-host → CDP pipe → Chromium.
4. Responses/events come back via `prism.onCDPMessage`; events enter a 10k-cap buffered queue.
5. Snapshots go through the custom `Prism.snapshot` domain (kernel composition) and return `{content, refs}`.

## Key design decisions

- **No embedded Node (ADR-002)**: we own the fork, so every binding semantic can be expressed as "custom DevTools domain + standard CDP"; the vendored harness works almost unmodified.
- **Space = hidden Browser window set + ownership state machine** (kernel side); one shared profile gives login inheritance for free. In adapter simulation mode a Space maps to a Target browser context.
- **Self-built snapshot renderer (ADR-003)**: the browser process composes the AXTree across the frame tree (AXTreeID routing for cross-process iframes) and associates nodes with DOM backendNodeIds.
- **Patch-series on a pinned upstream revision (ADR-001)**: new code concentrates in `//prism/`; upstream modifications are small, self-contained patches.

## Roadmap

| Phase | Scope | Exit criteria |
|---|---|---|
| 0 | Scaffolding & specs | spec docs landed, skeleton builds |
| 1 | Harness vendored + end-to-end on stock Chromium | upstream tests + core e2e green on stock Chromium |
| 2 | Fork bring-up + packaging/signing pipeline + first `Prism.*` methods | dmg installs; e2e green on the fork (passthrough mode) |
| 3 | Kernel-level snapshot | kernel path live; nested-iframe fixtures information-complete |
| 4 | Spaces UX & control handoff | manual acceptance scenarios all pass |
| 5 | Productization & distribution (migration, auto-update, skill channels, embedding SDK docs) | clean Mac runs first agent task within 5 minutes |
| 6 | Polish & benchmarking (benchmarks, stability, compatibility wrap-up) | comparison data published, CI gates complete |

## Embedding in our other agent products

Two consumption modes:
1. **Spawn the CLI** — identical to any third-party agent CLI (stdin heredoc).
2. **SDK import** — `import { installPrismSdk } from "prism-browser"` with a custom transport (direct in-process calls, skipping the CLI round trip).
