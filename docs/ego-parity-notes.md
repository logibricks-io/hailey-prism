# ego → Prism UI parity notes

Working notes for the UI parity package (welcome page, first-run onboarding,
native brand chrome, spaces overview). Spec: `docs/ego-welcome-ui-recon.md`.
This file records deliberate deviations, approximations, and what was (not)
verified — read it before assuming something is a bug.

## A. Welcome page (`chrome://prism-welcome`)

Shipped as a 1:1 rebuild (static HTML + external `app.js`; WebUI CSP forbids
inline scripts and runtime innerHTML, so all DOM is built with createElement
and all SVG art is data-URI inlined — the request filter does not sniff SVG
for `<img>`).

Deliberate deviations:

- Branding swaps per recon §6 (LogiBricks mark, "Prism" strings,
  `/prism-browser` prompt, install hint points at
  github.com/logibricks-io/hailey-prism, external links to our README).
- PostHog telemetry omitted by design.
- The Hermes spin icon ships with an explicit white fill: the original relies
  on `currentColor` inheritance, which is unavailable to `<img>`-embedded SVG.
- The "Copied" state is an icon-only swap (matches the original; no text
  label), and the icon rotation is a hard cut at 1.5 s (the original has no
  crossfade).

## B. First-run onboarding (`chrome://prism-onboarding`)

4 steps per recon §3 (splash → pitch → import wizard → finish), gated by the
first-run sentinel plus the Local State pref `prism.onboarding_completed`.
Kulim Park (OFL) is bundled for display type.

Approximations / gaps:

- The pitch illustration is a static CSS composite; the original shows a
  Chrome→ego window morph (no extracted asset for it).
- The import wizard's profile chip reads "Default profile" —
  `StageChromeImport` imports Chrome's default profile only; ego's
  "All profiles (N)" multi-profile picker is not replicated.
- "Share crash reports" maps to the `metrics.metricsReportingEnabled` Local
  State pref (macOS crash uploads ride on the metrics consent).
- Import runs the same staged+live machinery as the welcome page; if files
  were staged, the finish button becomes "Restart & open Prism"
  (`AttemptRestart` applies them on launch).
- Onboarding visuals (gradient, layout) were rebuilt from recon notes; ego's
  onboarding bundle itself was not extracted, so spacing/animation details
  are approximations in the same design language.

## C. Native brand chrome (recon §5)

- Omnibox chip: LogiBricks mark + "Prism" pill, leftmost leading decoration.
  Click opens the agent menu. (ego's chip measures 85×22; ours sizes to
  content, ~64×22.)
- Toolbar button: top-right, mark + dark circular badge counting
  agent-controlled spaces (hidden at 0). The badge is composited into the
  icon image (`Button::OnPaint` is final at this pin) and polls
  `SpaceManager` at 1 s — the same source and cadence as the Dock badge.
- Tab leading icon: `chrome://prism-welcome` and `chrome://prism-onboarding`
  tabs carry the brand mark via a data-URI `<link rel=icon>`.
- **Not done**: agent-driven tabs do not get a favicon override (that would
  require hooking the favicon driver; agent presence is already signaled by
  the in-window banner, the toolbar badge, and the Dock badge).
- **Not visually verified**: the badge at count > 0 (needs a live agent
  space during the screenshot). The count source is the Dock badge's; the
  composite drawing is deterministic.

## D. Spaces overview (`chrome://prism-spaces`)

Done on the tab-hosted card wall: top-center "N Space(s) ⌄" caption with a
space-list dropdown, "Space" label bottom-left on each card, muted
"Your Prism" watermark bottom-right, "+ Create a new Space" dashed card,
blue selection border (accent `#7eb3fe`), first-run "Hold ⌥ and press S…"
hint bar (dismissal persisted in profile localStorage), staggered card
entrance animation, and palette aligned with the welcome/onboarding tokens.

**Architectural gap (documented, not scheduled)**: ego's overview is a native
Mission-Control-style overlay — the whole browser content springs down to a
~0.42× live card over a dimmed backdrop and zooms back on selection. Prism's
overview is a tab-hosted card wall by design
(`prism_space_window_delegate` + WebUI); the native scale-down/backdrop-dim
spring animation is not replicated. ego's 30×30 title-bar "Open Space (⌥S)"
button is likewise not replicated — the ⌥S accelerator, View menu entries,
and the new toolbar Prism button cover the entry points.

## Environment caveats seen during verification

- The import fixture test (`host/scripts/import-fixture-test.mjs`) is
  SKIP-GUARDed on this machine: a real "Prism Safe Storage" keychain item
  already exists (created by normal Prism usage), and the test refuses to
  overwrite it. Delete it manually only if you know the real profile has no
  data encrypted with it.
- System-proxy auto-detection was intermittently broken during testing
  (Clash fake-ip): the probe suite then fails on `example.com` loads.
  Workaround: `PRISM_BROWSER_PATH=<wrapper adding
  --proxy-server=http://127.0.0.1:7897>`.
