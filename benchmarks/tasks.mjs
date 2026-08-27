// Benchmark task scripts: each runs as the prism-browser CLI's stdin script.
// Instrumentation: the prism binding object is monkey-wrapped to count
// binding+CDP calls; the script prints one __BENCH__ line the runner parses.

export const BENCH_INSTRUMENT_PREAMBLE = `
// --- benchmark instrumentation (injected by benchmarks/run.mjs) ---
const __benchStart = Date.now();
const __bench = { cliCalls: 0, snapshotBytes: 0 };
{
  const prism = globalThis.prism;
  if (prism) {
    for (const key of Object.keys(prism)) {
      const fn = prism[key];
      if (typeof fn !== "function") continue;
      prism[key] = (...args) => {
        __bench.cliCalls++;
        return fn(...args);
      };
    }
  }
}
const __benchDone = (extra = {}) => {
  console.log("__BENCH__ " + JSON.stringify({
    wallMs: Date.now() - __benchStart,
    cliCalls: __bench.cliCalls,
    snapshotBytes: __bench.snapshotBytes,
    ...extra,
  }));
};
// --- end instrumentation ---
`;

// 1. Ticket rush: a compacted version of the e2e damai-rush flow.
export function ticketRush(base) {
  return `${BENCH_INSTRUMENT_PREAMBLE}
const task = await taskSpaces.useOrCreate("bench-ticket-rush");
const tab = await browser.openOrReuseTab(${JSON.stringify(base)} + "/e2e/damai-rush/", { wait: true, timeout: 10000 });
await browser.switchTab(tab.targetId);
await page.locator('#reset-form input[name="capacity"]').fill("2");
await page.locator('#reset-form input[name="saleDelay"]').fill("0");
await page.getByRole("button", { name: "重置场景" }).evaluate((el) => el.click());
await page.waitForFunction(() => document.querySelector("[data-testid=remaining]")?.textContent === "2", { timeout: 10000 });
await page.locator('input[value="shanghai-sunday"]').check();
await page.locator('input[value="tier-980"]').check();
await page.locator('input[value="attendee-guest"]').check();
await page.getByLabel("下单账号").fill("Prism Bench");
const snap = await page.snapshot();
__bench.snapshotBytes += snap.length;
await page.getByRole("button", { name: "提交抢票" }).click();
await page.waitForFunction(() => document.body.innerText.includes("订单") || document.querySelector("[data-testid=order-result]"), { timeout: 10000 });
__benchDone({ task: "ticket-rush" });
await taskSpaces.complete(task.id, { keep: false });
`;
}

// 2. Multi-page form flow: fill → submit → confirm → done.
export function formFlow(base) {
  return `${BENCH_INSTRUMENT_PREAMBLE}
const task = await taskSpaces.useOrCreate("bench-form-flow");
await browser.openOrReuseTab(${JSON.stringify(base)} + "/form", { wait: true });
await page.getByLabel("Name").fill("Ada Lovelace");
await page.getByLabel("Email").fill("ada@example.com");
await page.locator('select[name="plan"]').selectOption("pro");
const snap = await page.snapshot();
__bench.snapshotBytes += snap.length;
await page.getByRole("button", { name: "Continue" }).click();
await page.waitForLoadState("load", { timeout: 10000 });
await page.getByRole("link", { name: "Finish" }).click();
await page.waitForLoadState("load", { timeout: 10000 });
const ok = (await page.getByRole("heading", { name: "Order confirmed" }).count()) === 1;
__benchDone({ task: "form-flow", ok });
await taskSpaces.complete(task.id, { keep: false });
`;
}

// 3. List scrape: paginate 4 pages, collect 32 items.
export function listScrape(base) {
  return `${BENCH_INSTRUMENT_PREAMBLE}
const task = await taskSpaces.useOrCreate("bench-list-scrape");
await browser.openOrReuseTab(${JSON.stringify(base)} + "/list", { wait: true });
const items = [];
for (let p = 1; p <= 4; p++) {
  const snap = await page.snapshot();
  __bench.snapshotBytes += snap.length;
  items.push(...(await page.locator("li.item").evaluateAll((els) => els.map((e) => e.textContent))));
  if (p < 4) {
    await page.locator("a.next").click();
    await page.waitForLoadState("load", { timeout: 10000 });
  }
}
__benchDone({ task: "list-scrape", items: items.length });
await taskSpaces.complete(task.id, { keep: false });
`;
}
