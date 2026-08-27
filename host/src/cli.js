#!/usr/bin/env node
// prism-browser CLI: installs the `prism` bindings on globalThis, then hands
// the vendored harness's runMain() the agent's stdin script.
//
// Transport selection:
//   1. kernel — the fork's agent socket (default
//      ~/Library/Application Support/Prism/agent.sock, PRISM_AGENT_SOCKET
//      override, "off" disables). One process = one socket connection = one
//      isolated DevTools session. If the socket is absent and
//      PRISM_BROWSER_PATH points at a fork binary, the CLI launches the
//      browser itself (detached, no pipe) and waits for the listener.
//   2. daemon — fallback: the prism-host daemon multiplexes one
//      --remote-debugging-pipe connection (stock Chromium path).
//
// Usage:
//   prism-browser <<'JS' ... JS      run a script against the browser
//   prism-browser --doctor           check browser/daemon health
//   prism-browser --reload           restart the browser connection
//   prism-browser --version          print CLI/host versions
//
// Env:
//   PRISM_BROWSER_PATH    explicit browser binary
//   PRISM_BROWSER_PROFILE dev profile dir (never the user's real profile)
//   PRISM_AGENT_SOCKET    kernel socket path override ("off" = daemon only)
//   PRISM_HOST_SOCKET     daemon socket override
//   PRISM_HARNESS_BUNDLE  path to the harness bundle (dist/out/index.js)

import fs from "node:fs";
import path from "node:path";
import { spawn } from "node:child_process";
import { fileURLToPath, pathToFileURL } from "node:url";

import { buildPrismBindings, connectToDaemon } from "./client.js";
import { SOCKET_PATH } from "./daemon.js";
import { defaultProfileDir, spawnBrowser } from "./chrome.js";
import {
  buildKernelPrismBindings,
  connectToKernel,
  kernelSocketCandidates,
  kernelTransportEnabled,
} from "./kernel.js";

const HERE = path.dirname(fileURLToPath(import.meta.url));
// Harness bundle resolution: the app bundle ships it at
// Prism.app/Contents/Resources/prism/harness/index.js next to this file's
// Resources/prism/cli/ home; the repo layout is the fallback (development).
const BUNDLED_BUNDLE = path.join(HERE, "..", "harness", "index.js");
const DEFAULT_BUNDLE = fs.existsSync(BUNDLED_BUNDLE)
  ? BUNDLED_BUNDLE
  : path.join(
  HERE,
  "..",
  "..",
  "package",
  "prism-browser",
  "dist",
  "out",
  "index.js",
);

async function ensureDaemon() {
  try {
    const client = await connectToDaemon(SOCKET_PATH);
    await client.call("host.ping");
    return client;
  } catch {
    // fall through and spawn
  }
  try {
    fs.unlinkSync(SOCKET_PATH);
  } catch {}
  const daemonPath = path.join(HERE, "daemon.js");
  const logPath = process.env.PRISM_HOST_LOG;
  const stdio = logPath
    ? ["ignore", fs.openSync(logPath, "a"), fs.openSync(logPath, "a")]
    : "ignore";
  const child = spawn(process.execPath, [daemonPath], {
    detached: true,
    stdio,
  });
  child.unref();
  const deadline = Date.now() + 30_000;
  let lastError;
  while (Date.now() < deadline) {
    await new Promise((resolve) => setTimeout(resolve, 150));
    try {
      const client = await connectToDaemon(SOCKET_PATH);
      await client.call("host.ping");
      return client;
    } catch (error) {
      lastError = error;
    }
  }
  throw new Error(
    `prism-host daemon did not become ready within 30s (${lastError?.message ?? "no detail"})`,
  );
}

async function tryKernelTransport() {
  if (!kernelTransportEnabled()) return null;
  try {
    return await connectToKernel();
  } catch {
    // No listener yet.
  }
  // Who may we launch? An explicit fork binary, or — when this CLI runs from
  // inside Prism.app (Resources/prism/cli/) — the owning app itself (the
  // real product path: the app keeps its default profile and its global
  // agent socket).
  const bundledBrowser = path.join(HERE, "..", "..", "..", "MacOS", "Prism");
  const browserPath = process.env.PRISM_BROWSER_PATH ||
    (fs.existsSync(bundledBrowser) ? bundledBrowser : null);
  // Only an explicit fork binary or the bundled app may be auto-launched here
  // — a stock build has no agent socket, so otherwise we fall through to the
  // daemon, which resolves and launches a browser on its own.
  if (!browserPath || !fs.existsSync(browserPath)) return null;
  const ownApp = browserPath === bundledBrowser;
  spawnBrowser({
    browserPath,
    profileDir: defaultProfileDir(),
    usePipe: false,
    // The bundled app launch keeps its real default profile (no
    // --user-data-dir): that's where the global agent socket lives.
    useDefaultProfile: ownApp,
    extraArgs: ownApp ? ["--no-default-browser-check"] : [],
  });
  const deadline = Date.now() + 30_000;
  while (Date.now() < deadline) {
    await new Promise((resolve) => setTimeout(resolve, 150));
    try {
      return await connectToKernel();
    } catch {
      // listener not up yet
    }
  }
  return null;
}

async function main() {
  const kernel = await tryKernelTransport();
  const client = kernel ? null : await ensureDaemon();
  globalThis.prism = kernel
    ? buildKernelPrismBindings(kernel)
    : buildPrismBindings(client);

  const bundle = process.env.PRISM_HARNESS_BUNDLE || DEFAULT_BUNDLE;
  if (!fs.existsSync(bundle)) {
    console.error(
      `harness bundle not found at ${bundle}\n` +
        "build it first: cd package/prism-browser && npm install && npm run build",
    );
    process.exitCode = 1;
    return;
  }
  const harness = await import(pathToFileURL(bundle).href);

  const services = {
    runDoctor: async (stream) => {
      try {
        if (kernel) {
          const version = await kernel.callPrism("getBrowserVersion");
          stream.write(`kernel agent socket: ok\n`);
          stream.write(`browser: ${version.currentVersion}\n`);
          stream.write(`socket: ${kernelSocketCandidates().join(", ")}\n`);
          return 0;
        }
        const ping = await client.call("host.ping");
        const version = await client.call("getBrowserVersion");
        stream.write(`prism-host daemon: ok (${ping.clients} client(s))\n`);
        stream.write(`browser: ${version.currentVersion}\n`);
        stream.write(`socket: ${SOCKET_PATH}\n`);
        return 0;
      } catch (error) {
        stream.write(`prism-host doctor failed: ${error.message}\n`);
        return 1;
      }
    },
    resetConnection: async () => {
      // Kernel connections are per-process: the next CLI run is already a
      // fresh session, so there is nothing to reset.
      if (kernel) return;
      await client.call("host.reset");
    },
    printUpdateBanner: () => {},
  };

  try {
    process.exitCode = await harness.runMain({ services });
  } finally {
    (kernel ?? client).close();
  }
}

main().catch((error) => {
  console.error(error?.stack || error?.message || String(error));
  process.exitCode = 1;
});
