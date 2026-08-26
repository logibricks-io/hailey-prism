# ADR-001: Manage the Chromium fork as patch-series on a pinned revision

## Status

Accepted (2026-08-26, project kickoff decision)

## Context

Prism needs kernel-level customization (a custom DevTools domain, the kernel snapshot, Space window-set management). There are two mainstream ways to maintain a Chromium fork:

1. **Full mirror repository**: fork the entire chromium/src git repo and hack on our own branch.
2. **Patch series**: pin an upstream stable revision; the repo stores a patch series plus an overlay of new files, applied to a pristine upstream checkout at build time.

## Decision

Adopt **patch-series on a pinned revision**:

- `chromium/DEPS.pin` pins the Chromium stable version.
- `chromium/patches/` holds quilt-style patches (small edits to existing upstream files).
- `chromium/src-overlay/` holds **new** files (copied into the `src/` tree at build time; primarily the `//prism/` directory).
- The actual Chromium checkout lives outside this repo (default `~/chromium/prism/src`, overridable via `PRISM_CHROMIUM_SRC`) — this repo's path contains spaces, which Chromium's toolchain does not tolerate.

## Rationale

- Manageable repo size (an upstream checkout is 40GB+, and the git history far more; neither belongs in our repo).
- Rebase cost is explicit: an upgrade = bump the pin + rebase the patch series; the conflict surface equals the patch surface — smaller is stabler.
- New code concentrates in `//prism/` via the overlay and produces almost no rebase conflicts.
- Structurally identical to the proven practice of mature forks (e.g. Brave's brave-core overlay).

## Consequences

- Every developer machine needs depot_tools and a one-time 100GB-scale checkout.
- Day-to-day harness/adapter development uses stock Chromium (Phase 1 decoupling); only kernel developers need the full checkout.
- Patch discipline: no kitchen-sink patches; one patch does one thing and states its motivation in the header.
