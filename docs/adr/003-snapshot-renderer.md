# ADR-003: Self-built snapshot renderer (kernel AXTree composition + contract-level compatibility)

## Status

Accepted (2026-08-26, project kickoff decision)

## Context

ego-lite claims its snapshot quality comes from "kernel-level customization", but the text layout is produced closed-source; the open repo only documents the `[ref=N, loc=..., url=...]` annotation style plus loose e2e assertions. Byte-exact replication is impossible; the decision is how Prism implements snapshots.

Options:

1. Pure JS composition: `Accessibility.getFullAXTree` + the DOM domain assembled in the adapter (the browser-use / agent-browser route).
2. Kernel composition: the fork's browser process composes the AXTree across the frame tree, routing cross-process iframes via AXTreeIDs, and associates nodes with backendNodeIds.
3. Hybrid: JS for v0, kernel for v1.

## Decision

Adopt **option 3**:

- **Phase 1 (simulation mode)**: the adapter composes from the stock `Accessibility.getFullAXTree`, satisfying the contract shape `{content, refs}` and the ref rules first so the harness and e2e can run.
- **Phase 3 (kernel mode)**: a custom `Prism.snapshot` domain implemented in the kernel, handling the known JS-route blind spots (deeply nested iframes, shadow DOM).

The text format is our own, honoring these **invariants** (contract §4, pinned by tests):

- Returns `{content:string, refs:[{backendNodeId:number, role:string, name:string}]}`.
- Node lines carry `[ref=N, loc=..., url=...]` annotations; ref = CDP backendNodeId; **the same element keeps the same number across calls**.
- Supported options: `scope("only_within_viewport"|"full_page")`, `includeActionMarks`, `includeStableLocator`, `maxResultLength`.
- Failures only reject (carrying codes such as `PRISM_SNAPSHOT_FAILED`), never resolve `{error}`.

## Rationale

- The JS route cannot compose AXTrees across cross-process iframes (DevTools isolates per target) — precisely the differentiation ego claims, and the core reason the fork route was chosen.
- The hybrid route keeps snapshot off the critical path of Phases 1-2, scheduling the hardest kernel work after the infrastructure is ready.

## Consequences

- We build our own format renderer plus a fixture site (deeply nested iframes, shadow DOM, canvas) for information-completeness evaluation.
- Snapshot text will not match upstream byte-for-byte — documented in compatibility.md; agent code must depend only on the ref/loc annotations, never the layout.
- Once the kernel implementation lands, the adapter flips to "passthrough preferred"; the JS composition path stays as a diagnostic reference.
