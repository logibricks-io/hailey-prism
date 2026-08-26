#!/usr/bin/env node
// prism-browser CLI: connects to the prism-host daemon, installs the `prism`
// bindings on globalThis, then hands the vendored harness's runMain() the
// agent's stdin script.
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
//   PRISM_HOST_SOCKET     daemon socket override
//   PRISM_HARNESS_BUNDLE  path to the harness bundle (dist/out/index.js)

import { spawn } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

import { buildPrismBindings, connectToDaemon } from "./client.js";
import { SOCKET_PATH } from "./daemon.js";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const DEFAULT_BUNDLE = path.join(
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

async function main() {
  const client = await ensureDaemon();
  globalThis.prism = buildPrismBindings(client);

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
      await client.call("host.reset");
    },
    printUpdateBanner: () => {},
  };

  try {
    process.exitCode = await harness.runMain({ services });
  } finally {
    client.close();
  }
}

main().catch((error) => {
  console.error(error?.stack || error?.message || String(error));
  process.exitCode = 1;
});
