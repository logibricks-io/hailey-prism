# Prism binding contract

**Status: this document is the single source of truth for Prism browser-side development (fork / adapter). Changes require an ADR first.**
Derived from a behavioral audit of ego-lite's open-source harness (MIT). Error codes and protocol shapes are wire contracts between processes; Prism rebrands them as `prism.*` / `PRISM_*` (the upstream `EGO_*` mapping lives in compatibility.md).

The harness expects the host to inject a **`prism`** object on `globalThis` (upstream calls it `ego`). The host can be:
- the `prism-host` adapter (Node, attached to stock Chromium or the fork via the CDP pipe), or
- any future equivalent host.

Detection logic (harness side): `typeof globalThis.prism.sendCDPMessage === "function"` means a browser runtime is present.

---

## 1. The CDP transport trio

### 1.1 `prism.sendCDPMessage(payload: string): void`

- **Synchronous** call; the payload is a JSON string: `{id:number, method:string, params:object, sessionId?:string}`.
- May throw synchronously (treated as a local send failure).
- The harness's default response timeout is **15s**; timeout text: `CDP request timed out: <method>`.

### 1.2 `prism.onCDPMessage(message: string): void` (a callback the harness assigns)

The host returns JSON strings in two shapes:

- **Response**: `{id, result}` or `{id, error:{message:string}|string}`
- **Event**: `{method, params, sessionId?}` — enters the harness's buffered event queue (10k cap, oldest dropped on overflow).

Special event semantics the host must honor:

| Event | Harness behavior |
|---|---|
| `Target.detachedFromTarget` / `Target.targetDestroyed` | invalidates the session, triggers auto re-attach |
| `Page.javascriptDialogOpening` / `Page.javascriptDialogClosed` | pending-dialog tracking (while a dialog exists, screenshot degrades to raw mode and `page.info()` returns `{dialog}`) |

### 1.3 `prism.onSendCDPMessageError(message: string, error_code: string): void` (a callback the harness assigns)

- The host calls this on local send failures (task inactive / user-controlled / not selected / host gone).
- **Carries no request id**: the harness rejects **all** currently pending requests with `buildPrismError({error, error_code})`.

### 1.4 Session management (existing harness-side logic the host should know)

- `ensureSession()`: picks preferred → active → last tab via `prism.listTabs()`, then `Target.attachToTarget {flatten:true}` for a sessionId; cached with a **2s TTL**.
- On session loss (error matching `/Session (with given id )?not found|Target closed|No session/i`) it re-attaches **once**.
- Requests with an explicit sessionId and `Target.*` / `Browser.*` browser-level methods are **not** retried.

---

## 2. Binding method surface

### 2.1 Tab management

| Method | Signature & return | Notes |
|---|---|---|
| `prism.listTabs()` | `Promise<{tabs:[{targetId,title,url,active,index?}]} \| {error,error_code}>`; `ensureSession` also accepts `{targetInfos}` | filtered by the selected Space in fork mode |
| `prism.createTab(url)` | `Promise<{targetId} \| {error,error_code}>` | on success the harness marks it the preferred target |

### 2.2 Snapshot

| Method | Signature & return |
|---|---|
| `prism.snapshot(options)` | `Promise<{content:string, refs:[{backendNodeId:number, role:string, name:string}]}>` |

- **Failures only reject** (an `Error` carrying `.error_code`), never resolve `{error}`. The harness relies on this distinction (its control probe uses the rejection to detect user control).
- Options and output contract: §4.

### 2.3 Task Space management

| Method | Signature & return | Semantics |
|---|---|---|
| `listTaskSpaces()` | `Promise<{taskSpaces:[Space]} \| {error,error_code}>` | `Space = {taskId, id:number, name, createdBy?, ownership?, recentTabTitles?:string[]}`; `ownership ∈ {"agent","user","agentDelegatedToUser"}` |
| `useTaskSpace(id:number)` | `Promise<any \| {error,error_code}>` | selects a Space; all later tab enumeration/snapshot on this connection applies to it |
| `createTaskSpace(name)` | `Promise<Space \| {error,error_code}>` | must return a numeric `id` |
| `claimTaskSpace(id:number, name?)` | `Promise<Space \| {error,error_code}>` | converts a user-owned space to agent-owned and selects it |
| `completeTaskSpace()` | `Promise<any \| {error,error_code}>` | completes the selected Space (keeps the scene) |
| `closeTaskSpace()` | `Promise<any \| {error,error_code}>` | closes the selected Space |
| `handOffTaskSpace()` | `Promise<any \| {error,error_code}>` | hands control to the user |
| `takeOverTaskSpace()` | `Promise<any \| {error,error_code}>` | takes control back from the user (no ownership check) |

Harness-side ownership policy (host behavior must cooperate):

- `useOrCreateTaskSpace` does not reuse user-owned spaces; a user-owned space can only be *selected*, and later operations hit `PRISM_TASK_SPACE_USER_IN_CONTROL`.
- `switchTaskSpace` requires agent-owned / agentDelegatedToUser.
- In `completeTaskSpace(_, {keep})` the `keep` flag is mandatory; with `keep:false` a user-owned space is claimed first, then closed.

### 2.4 Version & optional UI hooks

| Method | Signature & return | Notes |
|---|---|---|
| `getBrowserVersion()` | `Promise<{currentVersion:string, updateAvailable:boolean, latestVersion?, mandatory?} \| null>` | may be absent on older hosts |
| `animationHighlightMouseToPosition(x, y)` | optional; called on every pointer action | visual feedback for the user |
| `setAgentTaskState(label)` | optional; agent status label | feeds the Space indicator |

Upgrades do not go through the bindings: they use the CLI's `prism-browser upgrade` subcommand.

---

## 3. Error conventions

### 3.1 Two channels

- Business methods: **resolve** `{error: string, error_code: string}`.
- snapshot: **reject** an `Error` with `.error_code`.
- A bare code string is also recognized by the harness.

**Codes are the stable contract; wording may drift.** New codes are forward-compatible (the harness passes unknown codes through).

### 3.2 Error code table (Prism uses the `PRISM_` prefix; upstream equivalents share the same words with `EGO_`)

| Code | Meaning | Trigger |
|---|---|---|
| `PRISM_BROWSER_UNAVAILABLE` | browser unavailable | host not running / crashed |
| `PRISM_CDP_CHANNEL_UNAVAILABLE` | CDP channel unavailable | pipe broken |
| `PRISM_CDP_SEND_FAILED` | CDP send failed | local write failed |
| `PRISM_INVALID_ARGUMENT` | invalid argument | binding input validation |
| `PRISM_INVALID_RESULT_PAYLOAD` | invalid result payload | host returned a malformed shape |
| `PRISM_OPERATION_FAILED` | generic operation failure | — |
| `PRISM_RESULT_CONVERSION_FAILED` | result conversion failed | — |
| `PRISM_SNAPSHOT_FAILED` | snapshot failed | kernel composition error |
| `PRISM_TASK_HOST_DISCONNECTED` | task host disconnected | the Space's window set died |
| `PRISM_TASK_SPACE_INACTIVE` | Space inactive | ⚑ hard stop |
| `PRISM_TASK_SPACE_NOT_FOUND` | Space not found | no id/name match |
| `PRISM_TASK_SPACE_NOT_SELECTED` | no Space selected | precondition of business operations |
| `PRISM_TASK_SPACE_UNAVAILABLE` | Space unavailable | — |
| `PRISM_TASK_SPACE_USER_IN_CONTROL` | user in control | ⚑ hard stop |
| `PRISM_WEB_CONTENTS_UNAVAILABLE` | WebContents unavailable | page crash etc. |

### 3.3 Hard-stop semantics (the two ⚑ codes)

- Never auto-retried.
- `buildPrismError` calls `markHardStop(message)` at construction; when the run ends, the output sink **discards the entire buffered output and prints the guidance line exactly once**.
- The harness owns the guidance text for both codes: INACTIVE → points to `taskSpaces.claim(id)`; USER_IN_CONTROL → points to `taskSpaces.takeOver()`.

### 3.4 Thrown shape

An `Error` whose message is `"<op>: <message>"`, with `.error_code` attached.

---

## 4. Snapshot contract

### 4.1 Options

```ts
{
  scope?: "only_within_viewport" | "full_page";  // the harness forces full_page
  includeActionMarks?: boolean;                  // default true
  includeStableLocator?: boolean;                // default true
  maxResultLength?: number;                      // result truncation
}
```

The harness exposes two entries: `page.snapshot()` (forces the defaults above) and `page.snapshotRaw()` (passes options through verbatim).

### 4.2 Return

```ts
{ content: string, refs: Array<{ backendNodeId: number, role: string, name: string }> }
```

- `content`: the full-page semantic tree text; node lines are annotated `[ref=N, loc=..., url=...]` (Prism's own renderer, see ADR-003; byte-for-byte parity with upstream is not guaranteed).
- `refs`: feeds the harness RefMap as `String(backendNodeId) → {backendNodeId, role, name}`.

### 4.3 ref lifecycle (hard rules)

- A ref is a CDP `backendNodeId` (`@21`, not `@e21`).
- **The same element keeps the same number across calls** (repeated snapshots of one DOM node must yield the same backendNodeId).
- Every snapshot rebuilds the RefMap; refs are short-lived — re-snapshot after navigation or DOM changes; using a ref while the map is empty triggers an automatic re-snapshot.
- Long-lived references use the snapshot's `loc=...` (the `includeStableLocator` output).

---

## 5. Harness-side reverse augmentation of the host object (`installPrismSdk`)

After SDK installation (upstream `installEgoSdk` semantics preserved):

1. Legacy globals are deleted (`snapshotText/fillInput/cliLog/...` and friends).
2. The helper surface lands on `prism.helpers`, and `prism.learnings` (an alias of `helpers.site`).
3. `createTab / useTaskSpace / closeTaskSpace / createTaskSpace / claimTaskSpace` are wrapped (session invalidation, preferred-target tracking), guarded by `Symbol.for("prismBrowser.sdkWrapped")`.
4. Every function on `prism` except the wrapped ones above is **exposed bound as a top-level global** (heredoc scripts can call `prism.xxx()` directly).

The host must not overwrite `helpers/learnings`, and the five wrapped methods must be own properties (otherwise the wrapping silently no-ops).

---

## 6. Connection topology (Prism's implementation choice)

- Each CLI process gets its **own** CDP pipe connection (`--remote-debugging-pipe`); the Space selection is per-connection — `useTaskSpace` only affects its own connection.
- The adapter runs in two modes:
  - **Simulation mode** (stock Chromium): Space semantics implemented inside the adapter (Space ↔ browser context), for Phase 1.
  - **Passthrough mode** (fork): when the `Prism.*` domain is detected, task-space / listTabs / snapshot forward straight to the kernel.
- `Browser.grantPermissions` / `Browser.setPermission` are not exposed to agent scripts (same as upstream).

---

## 7. Executable contract test requirements

Every hard rule gets a test:

1. The trigger path of each of the 15 error codes (resolve form + reject form).
2. Both hard-stop codes → output collapses to a single guidance line, no buffered leakage.
3. snapshot only rejects, never resolve-errors; the ref same-number rule; automatic re-snapshot on an empty refMap.
4. `onSendCDPMessageError` broadcast → all pending requests reject.
5. Session 2s TTL and the re-attach-once rule (including the no-retry exception list).
