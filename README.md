# Prism

**Prism** is a browser designed for humans and AI agents to share (a Chromium fork on macOS), together with its agent-automation harness and skill package. Agents run browser tasks in parallel, isolated Spaces without touching the user's tabs, while inheriting the user's real login state.

This repository holds all of Prism's own assets:

| Directory | Contents |
|---|---|
| `package/prism-browser/` | Agent-side harness (CLI + SDK, TypeScript, Node 22+), vendored from ego-lite (MIT) — see `package/prism-browser/NOTICE` |
| `skills/prism-browser/` | Agent skill package (SKILL.md, install scripts, per-site learnings) |
| `host/` | `prism-host` adapter: bridges the `prism.*` bindings to CDP / the custom `Prism.*` DevTools domain |
| `chromium/` | Chromium fork infrastructure: pin, patches, src-overlay, gn args, branding, build/sign/notarize scripts |
| `e2e/` | Real-browser acceptance suite (ported from ego-lite's e2e cases) |
| `docs/` | Architecture, the binding contract spec, ADRs, compatibility notes |

## Key documents

- [docs/architecture.md](docs/architecture.md) — overall architecture and data flow
- [docs/binding-contract.md](docs/binding-contract.md) — **the authoritative contract the browser side must implement**
- [docs/compatibility.md](docs/compatibility.md) — differences from ego-lite and the naming map
- [docs/adr/](docs/adr/) — architecture decision records

## Quick start (development)

```bash
# Harness regression suite (build + typecheck + node --test)
cd package/prism-browser && npm install && npm test
```

Chromium fork fetching and building is documented in [chromium/README.md](chromium/README.md) (a 100GB-scale checkout; first-time setup is a separate effort).

## Language policy

All repository content — code, comments, docs, commit messages — is **English**. Prism is an international project.

## Status

Phase 1: harness vendored, agent workflow verified end-to-end against stock Chromium. See docs/architecture.md for the full roadmap.
