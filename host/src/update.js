// Update check + upgrade mechanics for Prism (Phase 7 / queue 5).
//
// Endpoint: GitHub Releases "latest" API by default, overridable with
// PRISM_UPDATE_URL (the loopback mock uses that; the real publishing channel
// is a separate queue item). No release server exists yet — every failure
// mode (404, timeout, offline, malformed JSON) must degrade to "no update"
// without surfacing errors to agents.
//
// Two disciplines:
//   - startUpdateCheck()/readCachedUpdate() — non-blocking pair used by the
//     getBrowserVersion wiring and the CLI banner: read the cache instantly,
//     refresh it in the background when stale (write-behind). The banner never
//     waits on the network.
//   - checkForUpdate() — the patient path for `prism-browser upgrade`: a real
//     fetch with a timeout, allowed to take seconds because the user asked.
//
// Cache: one JSON file per endpoint in the OS tmpdir, TTL 6h, validated
// against the current version (an upgrade invalidates a stale "available"
// answer). Only successful checks are cached; failures stay silent.

import crypto from "node:crypto";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";

export const DEFAULT_UPDATE_URL =
  "https://api.github.com/repos/logibricks-io/hailey-prism/releases/latest";
export const UPDATE_CHECK_TTL_MS = 6 * 60 * 60 * 1000;
export const UPDATE_FETCH_TIMEOUT_MS = 4000;

// Capture the real fetch at module load: the harness bundle's installPrismSdk
// replaces globalThis.fetch with the agent facade ({server, browser}) when
// cli.js imports it, so a lookup at call time finds the wrong thing (observed
// as "globalThis.fetch is not a function" inside the CLI process).
const nativeFetch =
  typeof globalThis.fetch === "function" ? globalThis.fetch.bind(globalThis) : null;

// Release assets are published under a stable name (see chromium/scripts/package.sh).
export function releaseAssetName(arch = process.arch) {
  return `Prism-mac-${arch}.dmg`;
}

// Semver-lite: the numeric run found in the string, so both
// "Prism/151.0.7922.174" and "v151.0.7922.174" parse. Chromium versions are
// 4-component (MAJOR.MINOR.BUILD.PATCH); a prerelease suffix sorts below the
// bare release. Unparseable values fall back to plain string inequality
// (updateAvailable = different).
function parseVersion(value) {
  const match = /(\d+(?:\.\d+){2,3})((?:[-+]).*)?/.exec(String(value ?? ""));
  if (!match) return null;
  return {
    parts: match[1].split(".").map(Number),
    suffix: match[2] || "",
  };
}

// >0: a newer; <0: a older; 0: same.
export function compareVersions(a, b) {
  const pa = parseVersion(a);
  const pb = parseVersion(b);
  if (!pa || !pb) {
    return String(a) === String(b) ? 0 : String(a) < String(b) ? -1 : 1;
  }
  for (let i = 0; i < Math.max(pa.parts.length, pb.parts.length); i++) {
    const x = pa.parts[i] ?? 0;
    const y = pb.parts[i] ?? 0;
    if (x !== y) return x < y ? -1 : 1;
  }
  if (pa.suffix === pb.suffix) return 0;
  if (!pa.suffix) return 1; // "1.2.3" > "1.2.3-beta"
  if (!pb.suffix) return -1;
  return pa.suffix < pb.suffix ? -1 : 1;
}

// GitHub releases/latest JSON -> { latestVersion, downloadUrl } | null.
export function parseLatestRelease(json, arch = process.arch) {
  if (!json || typeof json !== "object") return null;
  const tag = json.tag_name;
  if (typeof tag !== "string" || !parseVersion(tag)) return null;
  const want = releaseAssetName(arch);
  const assets = Array.isArray(json.assets) ? json.assets : [];
  const asset = assets.find((a) => a && a.name === want);
  return {
    latestVersion: tag.replace(/^v/i, ""),
    downloadUrl:
      asset && typeof asset.browser_download_url === "string"
        ? asset.browser_download_url
        : null,
  };
}

function updateUrl(env) {
  return (env.PRISM_UPDATE_URL || "").trim() || DEFAULT_UPDATE_URL;
}

function cachePath(env, tmpdir = os.tmpdir()) {
  const key = crypto
    .createHash("sha256")
    .update(updateUrl(env))
    .digest("hex")
    .slice(0, 16);
  return path.join(tmpdir, `prism-update-${key}.json`);
}

/** Exported for tests/cleanup: the cache file a given endpoint writes. */
export function updateCachePath(env, tmpdir) {
  return cachePath(env, tmpdir);
}

function readCache(env, tmpdir, nowMs) {
  try {
    const entry = JSON.parse(fs.readFileSync(cachePath(env, tmpdir), "utf8"));
    if (nowMs - entry.fetchedAt > UPDATE_CHECK_TTL_MS) return null;
    return entry;
  } catch {
    return null;
  }
}

function writeCache(env, tmpdir, entry) {
  try {
    fs.writeFileSync(cachePath(env, tmpdir), JSON.stringify(entry), "utf8");
  } catch {
    // tmpdir is best-effort; never let caching break the check.
  }
}

async function fetchLatestRelease(env, fetchFn) {
  const fetcher = fetchFn || nativeFetch;
  if (!fetcher) return null;
  const controller = new AbortController();
  const dbg = process.env.PRISM_DEBUG_UPGRADE
    ? (m) => process.stderr.write(`[dbg] ${m}\n`)
    : () => {};
  const t0 = Date.now();
  const timer = setTimeout(() => {
    dbg(`abort timer fired at +${Date.now() - t0}ms`);
    controller.abort();
  }, UPDATE_FETCH_TIMEOUT_MS);
  if (typeof timer.unref === "function") timer.unref();
  try {
    const response = await fetcher(updateUrl(env), {
      signal: controller.signal,
      headers: { accept: "application/json" },
    });
    if (!response.ok) return null;
    return await response.json();
  } catch (error) {
    if (process.env.PRISM_DEBUG_UPGRADE) {
      process.stderr.write(`[dbg] fetch failed: ${error?.name} ${error?.message} ${error?.cause?.code || ""}\n`);
    }
    return null;
  } finally {
    clearTimeout(timer);
  }
}

/**
 * Patient check for `prism-browser upgrade`: fresh fetch (bounded), then a
 * version comparison. Caches the outcome for the non-blocking readers.
 * Returns null on any failure — callers treat that as "no update to offer".
 */
export async function checkForUpdate({
  currentVersion,
  env = process.env,
  fetchFn,
  tmpdir,
  nowMs = Date.now(),
}) {
  const json = await fetchLatestRelease(env, fetchFn);
  const release = parseLatestRelease(json);
  if (!release) return null;
  const status = {
    updateAvailable: compareVersions(currentVersion, release.latestVersion) < 0,
    latestVersion: release.latestVersion,
    downloadUrl: release.downloadUrl,
  };
  writeCache(env, tmpdir, {
    fetchedAt: nowMs,
    currentVersion: String(currentVersion),
    endpoint: updateUrl(env),
    ...status,
  });
  return status;
}

/**
 * Non-blocking read for banners/getBrowserVersion: a fresh cache entry for
 * THIS version, or null. Never touches the network.
 */
export function readCachedUpdate({
  currentVersion,
  env = process.env,
  tmpdir,
  nowMs = Date.now(),
}) {
  const entry = readCache(env, tmpdir, nowMs);
  if (!entry || entry.currentVersion !== String(currentVersion)) return null;
  return {
    updateAvailable: entry.updateAvailable === true,
    latestVersion: entry.latestVersion,
    downloadUrl: entry.downloadUrl,
  };
}

let refreshInFlight = false;

/**
 * Merge the cached update status into a getBrowserVersion answer and kick a
 * background refresh for the next caller. The bridge call stays local and
 * fast; the network only ever runs write-behind. Unknown/older-channel
 * answers (missing currentVersion, error shapes) pass through untouched.
 */
export function mergeUpdateStatus(info, { env = process.env, tmpdir } = {}) {
  if (!info || typeof info !== "object" || info.error) return info;
  const currentVersion = info.currentVersion;
  if (typeof currentVersion !== "string" || !currentVersion) return info;
  const cached = readCachedUpdate({ currentVersion, env, tmpdir });
  startUpdateCheck({ currentVersion, env, tmpdir });
  if (cached?.updateAvailable) {
    return {
      ...info,
      updateAvailable: true,
      latestVersion: cached.latestVersion,
    };
  }
  return info;
}

/**
 * Write-behind refresh: if the cache is missing/stale, kick a background
 * check; returns immediately either way. Safe to call on every command.
 */
export function startUpdateCheck({ currentVersion, env = process.env, tmpdir }) {
  if (refreshInFlight) return;
  if (readCache(env, tmpdir, Date.now())) return;
  refreshInFlight = true;
  checkForUpdate({ currentVersion, env, tmpdir })
    .catch(() => {})
    .finally(() => {
      refreshInFlight = false;
    });
}
