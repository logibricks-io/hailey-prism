# Chrome data import (chrome://prism-welcome → "Import from Chrome")

Phase 6: full Chrome profile migration so agents inherit the user's login
state ("say yes once, and the agent works with your existing logins").

## What is imported

| Data | Source (Chrome Default profile) | How |
|---|---|---|
| Bookmarks | `Bookmarks` (JSON) | live merge via `ProfileWriter` ("Imported from Chrome" folder) |
| History | `History` (SQLite) | copied to a temp dir first, read, merged via `ProfileWriter` |
| Cookies (login sessions) | `Cookies` (+ `Network/Cookies` when present) | staged file copy |
| Passwords | `Login Data`, `Login Data For Account` | staged file copy |
| Preferences | `Preferences` | staged file copy |
| Secure Preferences | `Secure Preferences` | staged file copy |
| Extensions | `Extensions/` payloads | staged file copy, best-effort load |

## Security boundary

Cookies and passwords on macOS are v10 AES-128-CBC ciphertexts keyed by
PBKDF2-HMAC-SHA1(password, salt="saltysalt", 1003 iterations) where the
password lives in the keychain item "Chrome Safe Storage" (service) /
"Chrome" (account). Prism's own OSCrypt uses "Prism Safe Storage" / "Prism"
(patch 0004).

The migration copies the **seed password**, not the plaintext data:

1. `SecItemCopyMatching` reads the Chrome item. This is the one and only
   authorization point: macOS posts the standard ACL prompt ("Prism wants to
   access confidential information stored in 'Chrome Safe Storage'…") with
   Allow / Always Allow / Don't Allow. It is interactive by design — the user
   is present, having just clicked the import button.
2. The seed is written as "Prism Safe Storage" / "Prism" (create-or-update,
   `kSecAttrAccessibleWhenUnlocked`), indistinguishable from an item OSCrypt
   itself would create.
3. The encrypted profile files are staged unchanged. Because both apps derive
   the same AES key from the same seed bytes, the copied ciphertexts decrypt
   transparently after the restart — no per-entry decrypt/re-encrypt pass,
   so plaintext cookies/passwords never touch disk or memory outside the
   browser's own crypto path.

Explicitly NOT done:

- Chrome's directory is opened read-only; the fixture test asserts the source
  tree is byte-identical afterwards (no writes, no WAL checkpoints on the
  original).
- `Local State` is not copied (on macOS it holds no os_crypt material — that
  is the Windows app-bound scheme — and copying machine-scoped state would be
  wrong).
- Extension settings/storages beyond the `Extensions/` payloads and
  (Secure) Preferences entries (e.g. `Local Extension Settings` leveldb) are
  not migrated in v1.
- Only Chrome's `Default` profile is read (no multi-profile selection yet).
- Safari/Firefox passwords/cookies remain behind the stock importer's own
  limits.

## Overwrite semantics (honest sharp edges)

- The import **replaces** the Prism keychain item's value with Chrome's seed.
  Anything Prism encrypted before the import becomes undecryptable. The
  import is designed for first-run fresh profiles; the welcome page says so.
- Between the keychain write and the restart, the running browser still holds
  the old derived key in memory; any data encrypted in that window would also
  be orphaned (seconds-long window on a fresh profile).
- Preferences/Secure Preferences per-pref MACs verify after the seed copy
  (pref hashing is OSCrypt-derived, same machine). If the keychain step is
  denied, they are skipped entirely rather than risking a settings reset.

## Staging + restart (why)

The running browser owns `Cookies`/`Login Data`/`Preferences` and would
rewrite them on shutdown, clobbering an in-place copy. The importer therefore
writes `<user-data-dir>/PrismChromeImport.staging/` (marker file `pending`
written last, so a partial stage never applies) and
`prism::ApplyStagedChromeImport()` — called from
`ChromeBrowserMainParts::PreMainMessageLoopRunImpl` before any profile
service opens those files — moves the staged files into `Default/`. The
welcome page's "Restart Prism to finish" button drives `chrome::AttemptRestart()`.

## Graceful degradation

- Chrome profile absent → report `chromeFound: false`, nothing is staged or
  merged.
- Keychain prompt denied (`errSecUserCanceled`/`errSecAuthFailed`) → the
  encrypted items are reported `skipped:keychain-denied`; only bookmarks and
  history import. The user is told plainly what stayed behind.

## Test hooks (switches, used by the fixture test)

- `--prism-chrome-profile-dir=<path>` — source profile dir override.
- `--prism-import-keychain-read-from=<path>` — read the Chrome item only from
  this keychain file.
- `--prism-import-keychain-write-to=<path>` — write the Prism item into this
  keychain file instead of the default keychain.

Note (measured on macOS 26): `SecItemCopyMatching` does NOT traverse extra
file keychains added to the session search list — an item written into a
temporary keychain file is invisible to OSCrypt's default query. That is why
the end-to-end leg of the fixture test writes the real login-keychain item
(and deletes it afterwards, restoring the pre-test state; the test refuses to
run if a real "Prism Safe Storage" item already exists).

## Fixture test

`host/scripts/import-fixture-test.mjs` (run from the repo root):

- happy path: synthetic Chrome profile (v10 cookie with the raw-SHA256
  domain-hash prefix Chromium verifies at load, v10 login entry, bookmarks,
  history, prefs, one extension) + throwaway keychain holding a known seed.
  Asserts the report, the login-keychain write, offline decryptability of the
  staged ciphertexts, the startup apply (files land in `Default/`), and —
  end to end — that the restarted browser sends the migrated cookie
  (`Network.getCookies` + `document.cookie`) and keeps the row through the
  cookie store's load-time crypto verification. Also asserts the fixture tree
  is byte-identical afterwards (read-only import).
- `--deny`: the Chrome item is created without `-A`, so the read resolves to
  an authorization failure; asserts the `denied` report, skipped encrypted
  items, live bookmarks/history still merged, nothing staged.
- `--missing-chrome`: source dir override points nowhere; asserts
  `chromeFound: false` and zero side effects.

Known fixture caveat: modern cookie rows must encrypt
`sha256(host_key)` (raw 32 bytes) + value — hex-encoding the hash or omitting
it gets the row rejected at load with no visible error.
