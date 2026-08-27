// Benchmark fixtures: deterministic local servers (no network).
//
// - damai-rush: the e2e ticket-rush fixture (imported from the harness's
//   real-browser-e2e module — single source of truth).
// - form: a multi-page form flow (details → payment → confirm) with a server
//   echo endpoint.
// - list: a paginated product list (4 pages × 8 items) to scrape.

import { createServer } from "node:http";
import path from "node:path";
import { fileURLToPath } from "node:url";

const e2eFixture = path.join(
  path.dirname(fileURLToPath(import.meta.url)),
  "..", "package", "prism-browser", "scripts", "real-browser-e2e", "damai-rush",
  "fixture-routes.mjs",
);
const { createDamaiRushRoutes } = await import(e2eFixture);

const page = (title, body) =>
  `<!doctype html><html><head><meta charset="utf-8"><title>${title}</title></head><body>${body}</body></html>`;

export async function startFixtures() {
  const rush = createDamaiRushRoutes();
  const server = createServer(async (req, res) => {
    const url = new URL(req.url || "/", "http://127.0.0.1");
    if (await rush(req, res, url)) return;

    if (url.pathname === "/form") {
      res.end(page("bench form", `
        <form method="post" action="/form/submit">
          <label>Name <input name="name"></label>
          <label>Email <input name="email"></label>
          <label>Plan <select name="plan"><option>free</option><option>pro</option></select></label>
          <button type="submit">Continue</button>
        </form>`));
    } else if (url.pathname === "/form/submit" && req.method === "POST") {
      let body = "";
      req.on("data", (c) => (body += c));
      req.on("end", () => {
        const params = new URLSearchParams(body);
        res.end(page("bench form confirm", `
          <p id="echo">Hello ${params.get("name") ?? ""} (${params.get("plan") ?? ""})</p>
          <a href="/form/done">Finish</a>`));
      });
      return;
    } else if (url.pathname === "/form/done") {
      res.end(page("bench form done", `<h1>Order confirmed</h1>`));
    } else if (url.pathname === "/list") {
      const pageNum = Number(url.searchParams.get("p") || "1");
      const items = Array.from({ length: 8 }, (_, i) => {
        const n = (pageNum - 1) * 8 + i + 1;
        return `<li class="item" data-id="${n}">Item ${n} — $${n * 3}.00</li>`;
      }).join("");
      const next = pageNum < 4
        ? `<a class="next" href="/list?p=${pageNum + 1}">Next</a>` : "";
      res.end(page(`bench list p${pageNum}`,
        `<ul>${items}</ul><nav>${next}</nav><p id="pageinfo">page ${pageNum} of 4</p>`));
    } else {
      res.writeHead(404).end("not found");
    }
  });
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  const port = server.address().port;
  return {
    base: `http://127.0.0.1:${port}`,
    close: () => server.close(),
  };
}
