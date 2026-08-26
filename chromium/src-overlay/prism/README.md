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

## Build wiring (done via chromium/patches/)

`chromium/patches/0001-prism-devtools-domain.patch` (verified with
`git apply --check` against pin 151.0.7922.174):
1. adds `//prism/pdl/prism.pdl` to the browser protocol concatenation in
   `content/browser/devtools/BUILD.gn` (plus the generated `protocol/prism.*`
   outputs),
2. compiles `prism_domain_handler.cc` as part of the content/browser target
   and adds the `//prism:prism` dep in `content/browser/BUILD.gn`,
3. registers `PrismDomainHandler` in
   `content/browser/devtools/browser_devtools_agent_host.cc` (`AttachSession`,
   browser-target sessions only).

The handler keeps per-DevToolsSession state (selected Space id) — the pipe
connection is the session, which is what the adapter's "connection = state"
model relies on (ADR-002).

## Implementation status

- `space_manager.{h,cc}` + unittest — complete ownership state machine.
- `prism_domain_handler.{h,cc}` — all task-space commands + getBrowserVersion
  implemented; `listTabs`/`createTab`/`snapshot` are honest stubs until the
  window-group wiring (Phase 2/4) and the kernel snapshot (Phase 3) land.
- Error convention: `Response::ServerError("<PRISM_* code>: <detail>")`; the
  adapter maps the message prefix to the contract's error codes.
