# ADR-002: No Node embedded in the browser process; bindings via CDP pipe + a custom DevTools domain

## Status

Accepted (2026-08-26, project kickoff decision)

## Context

ego-lite's shape is an app embedding an `ego-browser` binary (with an embedded Node runtime) that injects `globalThis.ego`. Three options for the replication:

1. Embed Node in the browser process the same way (libnode).
2. The CLI is a standalone Node process talking to the browser over `--remote-debugging-pipe`; the binding semantics live in the kernel as a **custom DevTools domain** (`Prism.*`).
3. The CLI connects over a WebSocket port (`--remote-debugging-port`).

## Decision

Adopt **option 2**: `prism-browser` CLI = Node single-file bundle + the `prism-host` adapter; the fork registers a `Prism.*` DevTools domain in the kernel to carry task-space / snapshot / listTabs semantics; standard CDP domains (Target/Input/Page/Network/Accessibility…) pass through verbatim.

Option 3 (port) survives only as a development fallback (stock Chromium offers no per-connection customization beyond pipes).

## Rationale

- **The harness stays nearly unmodified**: `prism.sendCDPMessage` is literally one JSON line written to the pipe, `onCDPMessage` is pipe-read dispatch — maximizing the value of the vendor strategy.
- **Security**: a pipe is bound to process file descriptors, unlike a DevTools port, which any local process can hit (a known local attack surface).
- **Connection = state**: each CLI process gets its own pipe connection, so Space selection is naturally per-connection (contract §6) with no extra session tokens.
- Skipping embedded Node removes an entire class of problems: libnode integration, V8 context isolation, an enlarged crash surface.

## Consequences

- Custom domain methods need registration in the fork's DevTools protocol handler (self-contained implementation under `//prism/`).
- The stock-Chromium adapter (Phase 1) must simulate task-space semantics — that simulation code becomes "passthrough preferred, simulation fallback" once the fork lands.
- We do not replicate ego-lite's "app-embedded binary" form; the external equivalent is the `~/.local/bin/prism-browser` CLI.
