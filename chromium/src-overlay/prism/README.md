# //prism — Prism's kernel-side source tree

Injected into the Chromium checkout by `chromium/scripts/apply.sh` (rsync).
Upstream files are never edited here — those edits live in
`chromium/patches/` as small patches.

## Layout

- `pdl/prism.pdl` — the `Prism.*` DevTools domain definition (the kernel-side
  binding contract; mirror of docs/binding-contract.md).
- `browser/spaces/` — the Space registry and ownership state machine
  (pure logic; `space_manager` is unit-testable without a browser).
- `browser/devtools/` — the DevTools domain handler that bridges the
  `Prism.*` commands to the space manager / snapshot composer.
- `browser/snapshot/` — (Phase 3) the kernel snapshot composer: frame-tree
  AXTree walk with AXTreeID routing for cross-process iframes, DOM
  backendNodeId association.

## Build wiring (done via chromium/patches/, verified against the pinned tree)

TODO(after fetch): a patch must
1. add `//prism/pdl/prism.pdl` to the DevTools protocol build inputs
   (`//content/browser/devtools/BUILD.gn` / protocol pdl list),
2. register `PrismDomainHandler` in the DevTools session handler list for
   browser targets (`//content/browser/devtools/devtools_session.cc`),
3. add `//prism:prism` as a dependency of the browser target.

The handler keeps per-DevToolsSession state (selected Space id) — the pipe
connection is the session, which is what the adapter's "connection = state"
model relies on (ADR-002).
