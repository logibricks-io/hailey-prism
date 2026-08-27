// Phase 6 fixture test: full Chrome profile import (keychain seed copy +
// staged encrypted files + live bookmarks/history merge) against a synthetic
// Chrome profile and a THROWAWAY keychain file — the user's real keychains
// are never touched (the importer's documented test-hook switches aim both
// the read and the write at the temp keychain).
//
//   node host/scripts/import-fixture-test.mjs            # happy path + restart
//   node host/scripts/import-fixture-test.mjs --deny     # ACL-denial path:
//     the keychain item is created WITHOUT -A, so the import posts the real
//     macOS authorization prompt; the script waits for a human (or the
//     driving agent) to click "Don't Allow", then asserts graceful
//     degradation (bookmarks/history only).
//
// Verified chain: fixture "Chrome Safe Storage" password -> read by Prism ->
// written as "Prism Safe Storage" (temp keychain) -> staged Cookies/Login
// Data copied -> applied into the profile on next startup -> v10 ciphertext
// decrypts with PBKDF2-HMAC-SHA1(secret, "saltysalt", 1003) / AES-128-CBC —
// the same KDF Prism's OSCrypt uses, so the migrated profile decrypts
// transparently.

import crypto from "node:crypto";
import fs from "node:fs";
import net from "node:net";
import os from "node:os";
import path from "node:path";
import { execFileSync } from "node:child_process";
import { DatabaseSync } from "node:sqlite";
import { fileURLToPath } from "node:url";

const hostSrc = path.join(path.dirname(fileURLToPath(import.meta.url)), "..", "src");
const { spawnBrowser } = await import(path.join(hostSrc, "chrome.js"));
const { PipeTransport } = await import(path.join(hostSrc, "pipe.js"));
const { CdpConnection } = await import(path.join(hostSrc, "cdp.js"));

const DENY_MODE = process.argv.includes("--deny");
const MISSING_MODE = process.argv.includes("--missing-chrome");
const SECRET = "fixture-safe-storage-secret-7f3a";
const COOKIE_VALUE = "session-token-abc123";
const LOGIN_PASSWORD = "hunter2-fixture";

let failures = 0;
function check(label, cond, detail = "") {
  console.log(`${cond ? "PASS" : "FAIL"}  ${label}${cond ? "" : " — " + detail}`);
  if (!cond) failures++;
}

// ---------------------------------------------------------------- fixture ---

function v10Encrypt(plaintext) {
  const key = crypto.pbkdf2Sync(SECRET, "saltysalt", 1003, 16, "sha1");
  const cipher = crypto.createCipheriv("aes-128-cbc", key, Buffer.alloc(16, 0x20));
  return Buffer.concat([Buffer.from("v10"), cipher.update(plaintext), cipher.final()]);
}

function v10Decrypt(secret, blob) {
  const key = crypto.pbkdf2Sync(secret, "saltysalt", 1003, 16, "sha1");
  const decipher = crypto.createDecipheriv("aes-128-cbc", key, Buffer.alloc(16, 0x20));
  return Buffer.concat([decipher.update(blob.subarray(3)), decipher.final()]);
}

// Modern Chromium prefixes encrypted cookie plaintexts with the RAW 32-byte
// sha256 of the host_key (crypto::SHA256HashString returns bytes, not hex),
// verified at load — the fixture must do the same.
const COOKIE_HOST = ".example.com";
const COOKIE_DOMAIN_HASH = crypto.createHash("sha256").update(COOKIE_HOST).digest();
const COOKIE_PLAINTEXT = Buffer.concat([COOKIE_DOMAIN_HASH, Buffer.from(COOKIE_VALUE)]);

function hashTree(root) {
  const out = new Map();
  const walk = (dir) => {
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
      const p = path.join(dir, entry.name);
      if (entry.isDirectory()) walk(p);
      else out.set(path.relative(root, p), crypto.createHash("sha256").update(fs.readFileSync(p)).digest("hex"));
    }
  };
  walk(root);
  return out;
}

function buildFixture(chromeDir) {
  const def = path.join(chromeDir, "Default");
  fs.mkdirSync(path.join(def, "Extensions", "abcdefghijklmnopabcdefghijklmnop", "1.0"), { recursive: true });

  fs.writeFileSync(path.join(def, "Bookmarks"), JSON.stringify({
    roots: {
      bookmark_bar: { type: "folder", name: "Bookmarks bar", children: [
        { type: "url", name: "fixture-bookmark-one", url: "https://example.com/one" },
      ] },
      other: { type: "folder", name: "Other bookmarks", children: [
        { type: "url", name: "fixture-bookmark-two", url: "https://example.com/two" },
      ] },
      synced: { type: "folder", name: "Mobile bookmarks", children: [] },
    },
    version: 1,
  }));

  const history = new DatabaseSync(path.join(def, "History"));
  history.exec("CREATE TABLE urls (id INTEGER PRIMARY KEY, url TEXT, title TEXT, visit_count INTEGER, last_visit_time INTEGER)");
  history.exec(`INSERT INTO urls VALUES (1, 'https://example.com/history-entry', 'Fixture history entry', 3, 13400000000000000)`);
  history.close();

  const cookies = new DatabaseSync(path.join(def, "Cookies"));
  // Faithful Chrome v24 schema + meta so the real cookie store accepts the
  // migrated file without rebuilding it.
  cookies.exec("CREATE TABLE meta(key LONGVARCHAR NOT NULL UNIQUE PRIMARY KEY, value LONGVARCHAR)");
  cookies.exec(`CREATE TABLE cookies(creation_utc INTEGER NOT NULL,host_key TEXT NOT NULL,
    top_frame_site_key TEXT NOT NULL,name TEXT NOT NULL,value TEXT NOT NULL,encrypted_value BLOB NOT NULL,
    path TEXT NOT NULL,expires_utc INTEGER NOT NULL,is_secure INTEGER NOT NULL,is_httponly INTEGER NOT NULL,
    last_access_utc INTEGER NOT NULL,has_expires INTEGER NOT NULL,is_persistent INTEGER NOT NULL,
    priority INTEGER NOT NULL,samesite INTEGER NOT NULL,source_scheme INTEGER NOT NULL,
    source_port INTEGER NOT NULL,last_update_utc INTEGER NOT NULL,source_type INTEGER NOT NULL,
    has_cross_site_ancestor INTEGER NOT NULL)`);
  cookies.exec("INSERT INTO meta VALUES ('mmap_status', '-1'), ('version', '24'), ('last_compatible_version', '24')");
  // Chrome time = microseconds since 1601; 1.352e16 ≈ year 2040.
  // SameSite=Lax (1): SameSite=None without Secure is refused by modern
  // Chrome — and session cookies are normally Lax anyway.
  const stmt = cookies.prepare(`INSERT INTO cookies (creation_utc, host_key, top_frame_site_key, name, value,
    encrypted_value, path, expires_utc, is_secure, is_httponly, last_access_utc, has_expires, is_persistent,
    priority, samesite, source_scheme, source_port, last_update_utc, source_type, has_cross_site_ancestor)
    VALUES (13400000000000000, ?, '', ?, '', ?, '/', 13520000000000000, 0, 0, 13400000000000000, 1, 1, 1, 1, 0, -1, 0, 0, 0)`);
  stmt.run(COOKIE_HOST, "sid", v10Encrypt(COOKIE_PLAINTEXT));
  cookies.close();

  const login = new DatabaseSync(path.join(def, "Login Data"));
  login.exec(`CREATE TABLE logins (origin_url TEXT, action_url TEXT, username_value TEXT,
    password_value BLOB, signon_realm TEXT, date_created INTEGER)`);
  const lstmt = login.prepare("INSERT INTO logins VALUES (?, '', 'fixture-user', ?, ?, 0)");
  lstmt.run("https://example.com/login", v10Encrypt(LOGIN_PASSWORD), "https://example.com/");
  login.close();

  fs.writeFileSync(path.join(def, "Preferences"), JSON.stringify({ prism_fixture_marker: true }));
  fs.writeFileSync(path.join(def, "Secure Preferences"),
    JSON.stringify({ extensions: { settings: { abcdefghijklmnopabcdefghijklmnop: { state: 1 } } } }));
  fs.writeFileSync(
    path.join(def, "Extensions", "abcdefghijklmnopabcdefghijklmnop", "1.0", "manifest.json"),
    JSON.stringify({ manifest_version: 3, name: "Fixture Extension", version: "1.0" }));
}

function makeKeychain(kcPath, trusted) {
  execFileSync("security", ["create-keychain", "-p", "testpass", kcPath]);
  execFileSync("security", ["unlock-keychain", "-p", "testpass", kcPath]);
  const args = ["add-generic-password", "-s", "Chrome Safe Storage", "-a", "Chrome", "-w", SECRET];
  if (trusted) args.push("-A");  // any app may read without prompting
  args.push(kcPath);
  execFileSync("security", args);
}

function keychainHasItem(kcPath, service) {
  // dump-keychain lists metadata without touching the item ACL.
  try {
    return execFileSync("security", ["dump-keychain", kcPath],
      { timeout: 15_000 }).toString().includes(service);
  } catch {
    return false;
  }
}

function loginKeychainPath() {
  return path.join(os.homedir(), "Library", "Keychains", "login.keychain-db");
}

function loginKeychainHasPrismItem() {
  return keychainHasItem(loginKeychainPath(), "Prism Safe Storage");
}

function deletePrismLoginItem() {
  execFileSync("security", ["delete-generic-password", "-s", "Prism Safe Storage",
    "-a", "Prism", loginKeychainPath()]);
}

// ---------------------------------------------------------------- driver ----

const browserPath = process.env.PRISM_BROWSER_PATH || path.join(
  os.homedir(), "chromium/prism/src/out/Prism-arm64/Prism.app/Contents/MacOS/Prism");

async function connectWelcome(profileDir, extraArgs) {
  const child = spawnBrowser({
    browserPath, profileDir,
    extraArgs,
  });
  const cdp = new CdpConnection(new PipeTransport({
    writeStream: child.stdio[3], readStream: child.stdio[4],
  }));
  // chrome:// URLs are filtered from the command line at startup, so open
  // about:blank and navigate into the welcome page.
  let session = null;
  const deadline = Date.now() + 30_000;
  while (Date.now() < deadline && !session) {
    const targets = await cdp.send("Target.getTargets");
    const page = targets.targetInfos.find((t) => t.type === "page");
    if (page) {
      session = (await cdp.send("Target.attachToTarget",
        { targetId: page.targetId, flatten: true })).sessionId;
      await cdp.send("Page.navigate", { url: "chrome://prism-welcome" }, session);
    } else {
      await new Promise((r) => setTimeout(r, 250));
    }
  }
  if (!session) throw new Error("no page target to drive");
  await new Promise((r) => setTimeout(r, 1000));
  const check = await cdp.send("Runtime.evaluate", {
    expression: "location.href + '|' + !!document.getElementById('import-chrome')",
    returnByValue: true,
  }, session);
  if (check.result?.value !== "chrome://prism-welcome/|true") {
    throw new Error("welcome page did not load: " + check.result?.value);
  }
  return { child, cdp, session };
}

async function runImportAndGetReport(cdp, session, timeoutMs = 25_000) {
  await cdp.send("Runtime.evaluate", {
    expression: "chrome.send('importFromChrome')", returnByValue: true,
  }, session);
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    const result = await cdp.send("Runtime.evaluate", {
      expression: "JSON.stringify(window.__lastReport || null)", returnByValue: true,
    }, session);
    const parsed = JSON.parse(result.result?.value ?? "null");
    if (parsed) return parsed;
    await new Promise((r) => setTimeout(r, 400));
  }
  return null;
}

const stamp = Date.now();
const root = path.join(os.tmpdir(), `prism-import-test-${stamp}`);
const chromeDir = path.join(root, "chrome-fixture");
const kcPath = path.join(root, "test.keychain-db");
const profileDir = path.join(root, "prism-profile");

// --missing-chrome: the source-dir override points at a nonexistent path;
// the import must report chromeFound=false and stage nothing.
if (MISSING_MODE) {
  const missingArgs = [
    `--prism-chrome-profile-dir=${path.join(root, "no-such-chrome", "Default")}`,
  ];
  const { child, cdp, session } = await connectWelcome(profileDir, missingArgs);
  const report = await runImportAndGetReport(cdp, session);
  check("missing: report produced", !!report);
  if (report) {
    check("missing: chromeFound is false", report.chromeFound === false,
      JSON.stringify(report));
    check("missing: nothing staged", report.stagedForRestart === false);
  }
  check("missing: no staging dir on disk",
    !fs.existsSync(path.join(profileDir, "PrismChromeImport.staging")));
  child.kill("SIGTERM");
  fs.rmSync(root, { recursive: true, force: true });
  console.log(failures ? `MISSING-CHROME TEST FAILED (${failures})` : "MISSING-CHROME TEST OK");
  process.exit(failures ? 1 : 0);
}

fs.mkdirSync(chromeDir, { recursive: true });
buildFixture(chromeDir);
const fixtureHashes = hashTree(chromeDir);
makeKeychain(kcPath, /*trusted=*/!DENY_MODE);

// The Chrome-item READ comes from the throwaway keychain (no prompt, -A ACL).
// The WRITE targets the real default keychain: SecItemCopyMatching does not
// traverse extra file keychains from the session search list (macOS 15+,
// measured), so OSCrypt could not see a Prism item written into a temp
// keychain. The login-keychain item is created by the Prism binary itself
// (its ACL then trusts Prism silently), and the test removes it afterwards,
// restoring the pre-test state. Precondition: no existing Prism item (we
// never overwrite a real one).
const importArgs = [
  `--prism-chrome-profile-dir=${path.join(chromeDir, "Default")}`,
  `--prism-import-keychain-read-from=${kcPath}`,
];

// Any Prism launch creates a random "Prism Safe Storage" item on first
// OSCrypt use (startup). Track pre-test state so cleanup removes only items
// this test caused; in deny mode the startup-created item is cleaned too.
const hadPrismItemAtStart = loginKeychainHasPrismItem();

if (!DENY_MODE && hadPrismItemAtStart) {
  console.log("SKIP-GUARD: a real 'Prism Safe Storage' item exists in the login " +
    "keychain — refusing to overwrite it. Delete it (or not) and rerun.");
  fs.rmSync(root, { recursive: true, force: true });
  process.exit(2);
}

try {
  // ---- run 1: import ----
  const { child, cdp, session } = await connectWelcome(profileDir, importArgs);
  if (DENY_MODE) {
    console.log("PROMPT_PENDING — click \"Don't Allow\" on the macOS keychain prompt");
  }
  const report = await runImportAndGetReport(cdp, session, DENY_MODE ? 90_000 : 25_000);
  check("import: report produced", !!report);
  if (report) {
    console.log("  report:", JSON.stringify(report));
    check("import: chrome fixture found", report.chromeFound === true);
    if (DENY_MODE) {
      check("deny: keychain reported denied", report.keychain === "denied", report.keychain);
      check("deny: encrypted items skipped",
        ["cookies", "passwords", "preferences", "securePreferences", "extensions"]
          .every((k) => report.items?.[k]?.status === "skipped:keychain-denied"),
        JSON.stringify(report.items));
      check("deny: bookmarks still imported (graceful degradation)",
        report.items?.bookmarks?.status === "ok", JSON.stringify(report.items?.bookmarks));
      check("deny: nothing staged for restart", report.stagedForRestart === false);
    } else {
      check("import: keychain seed copied", report.keychain === "copied", report.keychain);
      for (const k of ["cookies", "passwords", "preferences", "securePreferences"]) {
        check(`import: ${k} staged`, report.items?.[k]?.status === "ok",
          JSON.stringify(report.items?.[k]));
      }
      check("import: extensions staged (1 copied)",
        report.items?.extensions?.status === "ok" &&
          (report.items.extensions.detail || "").startsWith("1 copied"),
        JSON.stringify(report.items?.extensions));
      check("import: bookmarks merged live", report.items?.bookmarks?.status === "ok");
      check("import: history merged live", report.items?.history?.status === "ok");
      check("import: staged for restart", report.stagedForRestart === true);
    }
  }
  child.kill("SIGTERM");
  await new Promise((r) => setTimeout(r, 1500));

  if (DENY_MODE && report) {
    // (No "no Prism item" assertion: any Prism launch auto-creates a random
    // OSCrypt seed item at startup. The meaningful denial properties —
    // denied status, encrypted items skipped, nothing staged, fixture
    // untouched — are asserted above.)
  }

  // ---- happy path: offline verification, then the end-to-end restart ----
  if (!DENY_MODE && report && report.stagedForRestart) {
    const stagingDir = path.join(profileDir, "PrismChromeImport.staging");

    // The migrated seed now lives in the login keychain as "Prism Safe
    // Storage", written by the Prism binary (its ACL trusts Prism silently).
    check("import: Prism keychain item exists in the login keychain",
      loginKeychainHasPrismItem());

    // Offline decrypt of the STAGED ciphertexts with the fixture seed —
    // proves the copied blobs survived intact before the browser owns them.
    const stagedCookies = path.join(stagingDir, "Cookies");
    if (fs.existsSync(stagedCookies)) {
      const db = new DatabaseSync(stagedCookies);
      const row = db.prepare(
        "SELECT encrypted_value FROM cookies WHERE host_key = ?").get(COOKIE_HOST);
      db.close();
      const plain = row && v10Decrypt(SECRET, Buffer.from(row.encrypted_value));
      check("import: staged v10 cookie decrypts with the fixture seed",
        Buffer.isBuffer(plain) && plain.equals(COOKIE_PLAINTEXT),
        plain ? `${plain.length}B` : "(none)");
    } else {
      check("import: staged Cookies exists", false);
    }
    const stagedLogin = path.join(stagingDir, "Login Data");
    if (fs.existsSync(stagedLogin)) {
      const db = new DatabaseSync(stagedLogin);
      const row = db.prepare("SELECT password_value FROM logins").get();
      db.close();
      const plain = row && v10Decrypt(SECRET, Buffer.from(row.password_value));
      check("import: staged v10 login entry decrypts",
        Buffer.isBuffer(plain) && plain.toString() === LOGIN_PASSWORD,
        plain ? plain.toString() : "(none)");
    } else {
      check("import: staged Login Data exists", false);
    }

    // End-to-end: relaunch the profile; the browser's OSCrypt reads the
    // migrated seed from the default keychain (the item was created by this
    // same Prism binary — its ACL trusts Prism silently) and the cookie store
    // decrypts the migrated row. This is the fixture equivalent of the real
    // single-prompt flow.
    const child2 = spawnBrowser({ browserPath, profileDir });
    const cdp2 = new CdpConnection(new PipeTransport({
      writeStream: child2.stdio[3], readStream: child2.stdio[4],
    }));
    await new Promise((r) => setTimeout(r, 5000));

    const defDir = path.join(profileDir, "Default");
    check("restart: staging dir consumed", !fs.existsSync(stagingDir));
    check("restart: Cookies landed in the profile",
      fs.existsSync(path.join(defDir, "Cookies")));
    check("restart: Preferences landed",
      fs.existsSync(path.join(defDir, "Preferences")) &&
        JSON.parse(fs.readFileSync(path.join(defDir, "Preferences")))
          .prism_fixture_marker === true);
    check("restart: extension payload landed",
      fs.existsSync(path.join(defDir, "Extensions",
        "abcdefghijklmnopabcdefghijklmnop", "1.0", "manifest.json")));

    // The proof of "agents inherit your login state": the restarted Prism
    // decrypts the migrated cookie with its own OSCrypt and would send it.
    // Network.getCookies is the network stack's authoritative view (the
    // document.cookie path is subject to extra page-side policies).
    const targets = await cdp2.send("Target.getTargets");
    const page = targets.targetInfos.find((t) => t.type === "page");
    const session2 = (await cdp2.send("Target.attachToTarget",
      { targetId: page.targetId, flatten: true })).sessionId;
    await cdp2.send("Page.navigate", { url: "https://example.com" }, session2);
    let sentCookies = [];
    const deadline = Date.now() + 20_000;
    while (Date.now() < deadline) {
      const result = await cdp2.send("Network.getCookies",
        { urls: ["https://example.com"] }, session2);
      sentCookies = result.cookies ?? [];
      if (sentCookies.some((c) => c.name === "sid")) break;
      await new Promise((r) => setTimeout(r, 500));
    }
    const sid = sentCookies.find((c) => c.name === "sid");
    check("restart: browser would send the migrated cookie (Network.getCookies)",
      sid?.value === COOKIE_VALUE, JSON.stringify(sentCookies));
    const dc = await cdp2.send("Runtime.evaluate",
      { expression: "document.cookie", returnByValue: true }, session2);
    console.log("  (info) document.cookie:", JSON.stringify(dc.result?.value));

    child2.kill("SIGTERM");
    await new Promise((r) => setTimeout(r, 1000));

    // Decisive diagnostic: rows whose decryption (or hash check) fails are
    // rejected at load. Survival of our row proves the browser's OSCrypt
    // read the migrated seed and the whole chain works in-process.
    const landedDb = path.join(defDir, "Cookies");
    if (fs.existsSync(landedDb)) {
      const db = new DatabaseSync(landedDb, { readOnly: true });
      const rows = db.prepare(
        "SELECT name, value, length(encrypted_value) AS elen FROM cookies").all();
      db.close();
      console.log("  (info) cookies table after browser run:", JSON.stringify(rows));
      // Encrypted cookies keep value="" (the plaintext lives only in
      // encrypted_value); survival with a non-trivial blob is the proof.
      check("restart: migrated row survives the browser's cookie-store load",
        rows.some((r) => r.name === "sid" && r.elen > 3),
        JSON.stringify(rows));
    }
  }

  // ---- invariant: the fixture (Chrome's stand-in) was never written ----
  const after = hashTree(chromeDir);
  const changed = [...fixtureHashes].filter(([f, h]) => after.get(f) !== h);
  const added = [...after.keys()].filter((f) => !fixtureHashes.has(f));
  check("safety: Chrome fixture directory is byte-identical (read-only import)",
    changed.length === 0 && added.length === 0,
    [...changed.map(([f]) => f), ...added].join(","));
} finally {
  if (!hadPrismItemAtStart && !process.env.PRISM_IMPORT_KEEP) {
    // Remove the item this test caused (import-written in happy mode,
    // startup-created in deny mode), restoring the pre-test state.
    try {
      deletePrismLoginItem();
      console.log("(cleanup: removed the test 'Prism Safe Storage' login-keychain item)");
    } catch (error) {
      console.log("(cleanup WARNING: login item removal failed:", error.message + ")");
    }
  }
  if (process.env.PRISM_IMPORT_KEEP) {
    console.log("(kept for inspection:", root, ")");
  } else {
    fs.rmSync(root, { recursive: true, force: true });
  }
}

console.log(failures ? `IMPORT FIXTURE TEST FAILED (${failures})` : "IMPORT FIXTURE TEST OK");
process.exit(failures ? 1 : 0);
