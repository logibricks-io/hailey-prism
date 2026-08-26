# Compatibility with ego-lite

Prism is a contract-level replication of ego-lite. This document tracks the differences and is written for anyone migrating existing ego scripts or know-how to Prism.

## Naming map

| ego-lite | Prism | Notes |
|---|---|---|
| `globalThis.ego` | `globalThis.prism` | the binding object |
| `ego-browser` CLI | `prism-browser` CLI | identical stdin heredoc usage |
| `EGO_*` error codes (15) | `PRISM_*`, same words | one-to-one semantics, see binding-contract.md §3 |
| `EGO_BROWSER_AGENT_WORKSPACE` | `PRISM_BROWSER_AGENT_WORKSPACE` | agent workspace resolution |
| `EGO_BROWSER_FFMPEG_PATH` | `PRISM_BROWSER_FFMPEG_PATH` | screencast ffmpeg path |
| `EGO_BROWSER_DEBUG_CLICKS` | `PRISM_BROWSER_DEBUG_CLICKS` | click debugging |
| `Symbol.for("egoBrowser.sdkWrapped")` | `Symbol.for("prismBrowser.sdkWrapped")` | SDK wrap guard |
| Task Space | Space (the `taskSpaces` facade keeps its name) | API names unchanged |

**Migration note**: agent scripts that hard-code `err.error_code === "EGO_..."` checks need the mapping above; helper facades (`page/browser/taskSpaces/site/fetch/cdp/help`) and their behavior are unchanged.

## The two naming surfaces

ego-lite ships two helper namings:

- **v2 facades** (what the repo runtime implements): `page.goto` / `page.snapshot` / `browser.openOrReuseTab` / …
- **legacy flat names** (recorded in its SKILL.md): `snapshotText` / `fillInput` / `cliLog` / `gotoUrl` / `waitForElement` / … — these **do not exist** in the v2 runtime, and SDK installation deletes same-named globals explicitly.

**Prism implements the v2 facades only.** The legacy names are archived here purely to keep the skill documentation honest.

## Known intentional differences

1. **Snapshot text layout**: upstream's layout is produced by closed-source kernel code; Prism uses its own renderer (ADR-003). The contract holds (`{content, refs}`, ref = backendNodeId, same element ↔ same number, `[ref=N, loc=..., url=...]` annotations); typographic details may differ byte-for-byte.
2. **`--doctor` / `--reload` / `upgrade`**: upstream ships in-repo stubs (injected by its app binary); Prism implements them for real in its own CLI.
3. **Time units**: upstream's SKILL.md says "seconds" but the v2 code actually uses milliseconds. Prism follows the **code** (milliseconds), except `waitForAgentControl`'s `interval/timeout` and `fetch.*`'s `timeout`, which use seconds.
4. **Brand & UI**: the Spaces management UI, icons, and copy are Prism's own design.

## Protocol invariants (zero drift allowed)

The parts below stay strictly identical to upstream and are pinned by harness tests; changing them breaks compatibility:

- The CDP string-JSON protocol trio (sendCDPMessage / onCDPMessage / the onSendCDPMessageError broadcast semantics).
- snapshot's reject-only failure semantics.
- The output-collapse behavior of the two hard-stop error codes.
- Session 2s TTL, re-attach once on loss, and no retries for explicit-sessionId or `Target.*`/`Browser.*` requests.
- ref = backendNodeId, same element ↔ same number, automatic re-snapshot on an empty refMap.
