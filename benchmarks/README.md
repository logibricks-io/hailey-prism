# Prism benchmarks

Repeatable, deterministic performance tasks for the prism-browser stack. No
network: every task runs against local fixture servers
(`benchmarks/fixtures.mjs` — the e2e damai-rush fixture plus a multi-page form
and a paginated list).

## Running

```bash
node benchmarks/run.mjs
```

What it does per task (`benchmarks/tasks.mjs`):

1. starts the local fixture server (random 127.0.0.1 port);
2. spawns the prism-browser CLI (`host/src/cli.js`) with the task script on
   stdin — kernel transport against the fork when `PRISM_BROWSER_PATH` is set
   (default: the local build output);
3. the task script monkey-wraps `globalThis.prism` and counts binding/CDP
   calls, sums snapshot bytes, and prints one `__BENCH__` JSON marker;
4. the runner records outer wall time (spawn → exit) plus the in-script
   numbers and writes `benchmarks/results/<timestamp>.json` and refreshes
   `benchmarks/results/summary.md`.

Metrics per task: wall time, CLI round trips (binding + CDP calls),
snapshot bytes.

## External tools

`BENCH_TOOL=agent-browser` / `BENCH_TOOL=ego-browser` switches the spawned CLI.
The task scripts use prism-browser facades, so a genuinely comparable run
needs the other tool's equivalent scripts — until those exist the runner
**skips** uninstalled tools explicitly (no fabricated comparison data). To
enable: install the tool on PATH, then e.g.
`BENCH_TOOL=agent-browser node benchmarks/run.mjs`.
