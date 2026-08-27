# ego lite first-hand parity notes

Findings from hands-on exploration of the installed ego lite app (GUI + its own
`ego-browser` CLI), 2026-08-27. Complements docs/compatibility.md — that file
covers the repo-derived contract; this one covers the shipping product's UX and
behavior that cannot be seen from the repo.

## Snapshot format (the "kernel-level" claim, made concrete)

Real `snapshotText()` output on example.com (their build, Chromium 150):

```
root
  heading
    text "Example Domain"
  paragraph
    text "This domain is for use in documentation examples without needing permission. Avoid use in operations."
  paragraph
    anchor [ref=16, loc=href:https://iana.org/domains/example, url=https://iana.org/domains/example]
      text "Learn more"
```

Differences from our current renderer (both JS and kernel paths):

- **Curated semantic roles, lowercase, HTML-flavored**: `anchor`, `heading`,
  `paragraph`, `text` — not raw AX roles (`link`, `RootWebArea`, `StaticText`).
- **Text folded into standalone `text "..."` child lines**, and no
  `StaticText`/`InlineTextBox` duplication noise. Same page: theirs 9 lines,
  ours 12 (with two noise lines).
- Annotations `[ref=N, loc=href:..., url=...]`; links use `loc=href:` (we emit
  `loc=role:`); `url=` present on links (we added this in Phase 3 — good).
- ref = backendNodeId, same convention as ours.

**Action**: evolve our composer toward this density — lowercase semantic role
names, fold text runs, drop decorative/duplicated nodes, add `loc=href:` for
links. Theirs is measurably denser (LLM-token-friendlier). This is now the
format to beat; ours still wins on OOPIF composition (theirs untested here,
their claim is nested-iframe robustness).

**Status (2026-08-27): ALIGNED.** The kernel composer
(`prism/browser/snapshot/snapshot_composer.{h,cc}`) and the JS fallback
(`host/src/snapshot.js`) both emit the curated format; the example.com output
matches the sample above line for line (only the ref number differs, which is
allowed by contract — backendNodeIds are process-assigned):

```
root
  heading
    text "Example Domain"
  paragraph
    text "This domain is for use in documentation examples without needing permission. Avoid use in operations."
  paragraph
    anchor [ref=13, loc=href:https://iana.org/domains/example, url=https://iana.org/domains/example]
      text "Learn more"
```

Format deltas we keep deliberately: no dash prefixes on lines (matches the
sample), refs are actionable-only, `textbox` lines may include the current
`value` when set (form state). Density on a real page
(news.ycombinator.com): 1603 → 969 lines, 92.2KB → 57.2KB (-40%/-38%),
refs 1118 → 230 (actionable-only). OOPIF expansion is preserved — nested
iframes still compose inline (probe fixture: 3 levels, cross-process, green).

## Spaces product model (GUI recon)

- **A Space is an in-window tab set** (Arc/Safari-style): switching spaces
  swaps the whole tab strip inside the same window. Agent spaces run in the
  background tab set without interrupting the user — exactly the background
  execution model Prism targets. (Ours: windowless WebContents + optional
  window per space + separate management page — functionally equivalent for
  background work, different peek UX.)
- **Mission-Control-style Spaces overview**: full-window card wall with live
  thumbnails per space, a state chip (`Running`) + task name under agent cards,
  a "+" create card, "N Spaces" dropdown (contains "Delete all Space"), and a
  top-right numeric activity badge.
- **Quick switcher**: hold ⌥ and tap S repeatedly to cycle spaces (with the
  hint line rendered in the overview).
- **Agent-in-control banner**: when the user views a space an agent controls,
  a bottom bar shows "<task> · Agent is in control" with **Take over** /
  **Stop** buttons — their hard-stop UX has a native face.
- The watermark text on cards is the *profile name* (user's profile is named
  "LogiBricks.AI Agents EGO"), not an ego feature.
- Toolbar "ego" menu = rebranded Chrome overflow menu + "Set as default
  browser" entry. Tab hover shows memory usage (e.g. "Memory usage - 78.3 MB").
- Base version observed: Chrome/150.0.0.0 UA (we pin 151 — one milestone newer).

## CLI packaging

`/Applications/ego lite.app/Contents/Resources/` contains `ego-browser` (a
compiled arm64 Mach-O, likely a Node single-executable) and `ego-skills/`.
Our layout (Resources/prism-browser launcher + bundled prism-node + harness +
skills) is structurally equivalent; theirs is one static binary.

## Updated gap list (supersedes earlier versions where they conflict)

1. ~~Snapshot text density/role curation~~ — **landed** (see Status above).
2. ~~Spaces overview UI + ⌥S quick switcher + in-window space switching~~ —
   **landed** (Phase 5): chrome://prism-spaces is now a Mission-Control card
   wall — live tab thumbnails (captured on demand via an internal DevTools
   session, 1s cache), ownership/state chips, a "+" create card, and a
   Delete-all action. Switching: View → "Show Next Space" (⌥S) cycles
   [main browsing area] + spaces by id; View → "Spaces Overview" (⌘⇧S) opens
   the wall. ego's toolbar activity badge maps to a macOS Dock badge counting
   agent-controlled spaces (no toolbar button — the pragmatic equivalent).
   Screenshot: assets/phase5-spaces-overview.png (card wall). The banner was
   verified on-screen via the OS-level accessibility tree (CDP screenshots
   exclude native chrome; this shell lacks Screen Recording permission, so
   no durable banner capture is kept).
3. ~~Agent-in-control banner~~ — **landed**: a space window whose space is
   agent-owned shows an infobar "<name> · Agent is in control" with
   **Take over** (handoff: kAgentDelegatedToUser) and **Stop agent**
   (ownership → kUser; the agent can only re-enter by claiming). A 1s sync in
   the space-window delegate reconciles the banner and Dock badge with
   SpaceManager state, so CDP-driven handoffs surface without polling.
4. ~~Full Chrome data import (cookies/passwords/extensions) with first-run
   user-present keychain authorization~~ — **landed** (Phase 6): one click on
   chrome://prism-welcome's "Import from Chrome" copies the "Chrome Safe
   Storage" keychain seed into "Prism Safe Storage" (one macOS ACL prompt,
   user-present) and stages Cookies/Login Data/Preferences/Secure
   Preferences/Extensions for the next startup, so the copied v10
   ciphertexts decrypt transparently — no decrypt/re-encrypt pass. Coverage
   vs ego's advertised "logins, cookies, extensions, bookmarks": we match all
   four and additionally import history (live-merged) and
   Preferences/Secure Preferences. Not done in v1: multi-profile selection
   (Default only), `Local Extension Settings`, Local State. Denial/missing
   paths degrade to bookmarks+history with an honest per-item report. Full
   security-boundary writeup: docs/chrome-import.md.
5. Signed/notarized distribution + auto-update channel.
6. x64 build. 8. Channel publishing.
7. ~~Bare-Meta input isolation~~ — **landed** (Phase 7): the leak path is the
   shell's keyboard redispatch — a renderer-unhandled key event is re-injected
   by `BrowserNativeWidgetMac::HandleKeyboardEvent` →
   `CommandDispatcher::redispatchKeyEvent:` → `[NSApp sendEvent:]` (gated on
   the window being key), which is where a synthetic bare Cmd escaped to the
   macOS shortcut layer. Patch 0013 swallows the event when it is both
   DevTools-synthetic (`kFromDebugger` — real user keys never carry it) and
   modifier-only (`dom_code` is MetaLeft/MetaRight). Instrumented proof on the
   fork: bare Meta reaches HandleKeyboardEvent and is swallowed (no
   redispatch), while a synthetic Cmd+W chord still redispatches and closes
   the tab (window key, no collateral damage). Caveat measured on macOS 26:
   the original symptom (bare Cmd launching System Information) does not
   reproduce there even on stock Chrome 152 frontmost — the e2e case is a
   guard, not a live repro on this OS version. ego lite passes the same case
   (they fixed it in their shell); stock Chrome passes it vacuously here.
   Harness: `taskSpaces.show(nameOrId)` (kernel-only) fronts the space window
   so the case is deterministic instead of focus-luck; the case is unskipped
   in e2e/run.mjs.
