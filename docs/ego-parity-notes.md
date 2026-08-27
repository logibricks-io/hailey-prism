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

1. Snapshot text density/role curation (see above).
2. Spaces overview UI + ⌥S quick switcher + in-window space switching.
3. Agent-in-control banner with Take over/Stop in the space view.
4. Full Chrome data import (cookies/passwords/extensions) with first-run
   user-present keychain authorization.
5. Signed/notarized distribution + auto-update channel.
6. x64 build. 7. Bare-Meta input isolation. 8. Channel publishing.
