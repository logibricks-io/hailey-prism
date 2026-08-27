#!/usr/bin/env node
// Benchmark runner: runs deterministic local-fixture tasks through the
// prism-browser CLI and records wall time, CLI round trips, and snapshot
// bytes per task. Results land in benchmarks/results/ (JSON + markdown).
//
//   node benchmarks/run.mjs
//
// Env:
//   PRISM_BROWSER_PATH   fork binary (defaults to out/Prism-arm64)
//   BENCH_TOOL           "prism" (default) | "agent-browser" | "ego-browser"
//                        — external tools run only when installed; otherwise
//                        they are skipped and the report says how to enable.

import { spawn } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

import { startFixtures } from "./fixtures.mjs";
import { ticketRush, formFlow, listScrape } from "./tasks.mjs";

const REPO_ROOT = path.join(path.dirname(fileURLToPath(import.meta.url)), "..");
const CLI = path.join(REPO_ROOT, "host", "src", "cli.js");
const RESULTS_DIR = path.join(REPO_ROOT, "benchmarks", "results");
const TOOL = process.env.BENCH_TOOL || "prism";

const browserPath =
  process.env.PRISM_BROWSER_PATH ||
  path.join(os.homedir(),
    "chromium/prism/src/out/Prism-arm64/Prism.app/Contents/MacOS/Prism");

const TASKS = [
  { name: "ticket-rush", script: ticketRush },
  { name: "form-flow", script: formFlow },
  { name: "list-scrape", script: listScrape },
];

function haveCommand(name) {
  for (const dir of (process.env.PATH || "").split(path.delimiter)) {
    if (dir && fs.existsSync(path.join(dir, name))) return true;
  }
  return false;
}

function runTask(cliArgs, script) {
  return new Promise((resolve, reject) => {
    const child = spawn("node", cliArgs, { stdio: ["pipe", "pipe", "inherit"] });
    let out = "";
    child.stdout.on("data", (c) => (out += c));
    child.on("error", reject);
    child.on("close", (code) => {
      const marker = out.split("\n").find((l) => l.startsWith("__BENCH__ "));
      if (!marker) return reject(new Error(`no __BENCH__ marker (exit ${code})`));
      resolve(JSON.parse(marker.slice("__BENCH__ ".length)));
    });
    child.stdin.write(script);
    child.stdin.end();
  });
}

function cliInvocation() {
  if (TOOL === "prism") return [CLI];
  if (TOOL === "agent-browser" || TOOL === "ego-browser") return [TOOL];
  throw new Error(`unknown BENCH_TOOL: ${TOOL}`);
}

async function main() {
  if (TOOL !== "prism" && !haveCommand(TOOL)) {
    console.log(`SKIP: ${TOOL} not installed; benchmark skipped for it.`);
    console.log("Enable with: BENCH_TOOL=agent-browser node benchmarks/run.mjs");
    console.log("           BENCH_TOOL=ego-browser node benchmarks/run.mjs");
    console.log("after installing the tool on PATH.");
    return;
  }

  const fixtures = await startFixtures();
  const results = [];
  try {
    for (const task of TASKS) {
      const env = { ...process.env, PRISM_BROWSER_PATH: browserPath };
      const wallStart = Date.now();
      const child = spawn("node", cliInvocation(), { stdio: ["pipe", "pipe", "inherit"], env });
      let out = "";
      child.stdout.on("data", (c) => (out += c));
      child.stdin.write(task.script(fixtures.base));
      child.stdin.end();
      const bench = await new Promise((resolve, reject) => {
        child.on("error", reject);
        child.on("close", (code) => {
          const marker = out.split("\n").find((l) => l.startsWith("__BENCH__ "));
          if (!marker) return reject(new Error(`${task.name}: no __BENCH__ marker (exit ${code})`));
          resolve(JSON.parse(marker.slice("__BENCH__ ".length)));
        });
      });
      results.push({
        task: task.name,
        tool: TOOL,
        wallMs: Date.now() - wallStart,
        cliWallMs: bench.wallMs,
        cliCalls: bench.cliCalls,
        snapshotBytes: bench.snapshotBytes,
        ok: bench.ok ?? true,
      });
      console.log(
        `${task.name.padEnd(14)} wall=${results.at(-1).wallMs}ms cli=${bench.cliCalls} snap=${bench.snapshotBytes}B`,
      );
    }
  } finally {
    fixtures.close();
  }

  fs.mkdirSync(RESULTS_DIR, { recursive: true });
  const stamp = new Date().toISOString().replace(/[:.]/g, "-");
  const jsonPath = path.join(RESULTS_DIR, `${stamp}.json`);
  fs.writeFileSync(jsonPath, JSON.stringify({ results }, null, 2) + "\n");

  const summaryPath = path.join(RESULTS_DIR, "summary.md");
  const lines = [
    "# Prism benchmark summary",
    "",
    `run: ${stamp} · tool: ${TOOL} · browser: ${browserPath}`,
    "",
    "| task | wall (ms) | CLI round trips | snapshot bytes |",
    "|---|---|---|---|",
    ...results.map((r) =>
      `| ${r.task} | ${r.wallMs} | ${r.cliCalls} | ${r.snapshotBytes} |`,
    ),
    "",
  ];
  fs.writeFileSync(summaryPath, lines.join("\n"));
  console.log(`results: ${jsonPath}`);
  console.log(`summary: ${summaryPath}`);
}

main().catch((error) => {
  console.error(error?.stack || error?.message || String(error));
  process.exitCode = 1;
});
