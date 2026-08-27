# ADR-004: Auto-update — self-hosted CLI upgrade path first, Sparkle as the production hook

## Status

Accepted (2026-08-27). Skeleton landed without a real release server; the
endpoint is parameterized (`PRISM_UPDATE_URL`) and the default points at
GitHub Releases (`logibricks-io/hailey-prism`).

## Context

ego-lite ships `ego-browser upgrade` plus a launch-time update hint. Agents
drive Prism through a CLI, so update awareness must exist where agents read
it: the harness notice trailer (`emitUpdateNotice`) and the CLI itself.

Two real options:

1. **Self-hosted CLI path (built now)**: `host/src/update.js` polls a
   `releases/latest`-shaped JSON endpoint with a 6h tmpdir cache and
   write-behind discipline (banners read only the cache; the network never
   blocks a command). `prism-browser upgrade [--dry-run]`
   (`host/src/upgrade.js`) downloads the arch-keyed dmg asset
   (`Prism-mac-<arch>.dmg`, the stable name `package.sh` already emits),
   verifies the mounted app's version against the manifest, quits the running
   app gracefully, swaps `/Applications/Prism.app` atomically
   (rename-to-backup, rename-in, restore-on-failure), and reopens it.
2. **Sparkle**: the standard macOS in-app updater — signed appcast, delta
   updates, phased rollout, its own UI, EdDSA signature verification.

## Decision

Option 1 now, option 2 stays the production hook. Rationale:

- Prism is unsigned today (`PRISM_SIGNING_IDENTITY` unset). Sparkle's value is
  its signature-verified replacement pipeline; without a Developer ID
  certificate it degenerates to exactly the tarball/dmg swap we wrote
  ourselves, plus a vendored framework and an appcast to maintain.
- Our primary operator is an agent shell, not a human at a keyboard: a
  composable CLI verb (`upgrade --dry-run` printing the plan) beats an in-app
  modal for that audience, and the SDK notice line already routes there.
- The CLI path's weak spots are documented, not hidden: the swap is not
  signature-verified (the dmg is trusted because it came from the configured
  endpoint over TLS), there is no delta download, no phased rollout, and no
  rollback channel beyond the timestamped backup kept during the swap.

## Production hooks (what adopting Sparkle later requires)

- `PRISM_SIGNING_IDENTITY` (Developer ID Application cert) in
  `chromium/scripts/package.sh`, plus notarization (`xcrun notarytool`) — the
  dmg is currently unsigned and Gatekeeper-blocked elsewhere.
- An appcast feed (`appcast.xml`) served next to the release assets; the
  `SUFeedURL` key in `app-Info.plist` (patch 0003 area) and Sparkle.framework
  embedded into `Prism.app/Contents/Frameworks`.
- Sparkle's EdDSA signing keypair (separate from the Developer ID cert); the
  private half signs appcast items at release time.
- The CLI banner/upgrade endpoint then just points at the appcast instead of
  the GitHub API — `PRISM_UPDATE_URL` already abstracts the endpoint shape.

## Consequences

- `getBrowserVersion` keeps its kernel meaning; update status is composed in
  the CLI layer (`mergeUpdateStatus`) so the kernel domain stays untouched.
- Both banner paths (SDK trailer via `emitUpdateNotice`, CLI via
  `services.printUpdateBanner`) share one line format (`composeNotice`,
  exported from the harness package for exactly this).
- Failures are silent-by-design for banners (no update on 404/timeout/
  offline) and explicit for the verb (upgrade prints why).
