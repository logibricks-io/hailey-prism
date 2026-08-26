import test from "node:test";
import assert from "node:assert/strict";

import { installPrismSdk } from "../dist/src/index.js";

test("installPrismSdk keeps page.locator chainable while wrapping locator methods", () => {
  const originalLog = console.log;
  const target = { click() {} };
  try {
    installPrismSdk(target, { cliLog() {} });
    assert.equal(typeof target.click, "undefined");
    assert.equal(typeof target.page.locator, "function");
    assert.equal(typeof target.page.getByText("Allow").click, "function");
    assert.equal(typeof target.page.getByLabel("Email").fill, "function");
    assert.equal(
      typeof target.page.getByPlaceholder("Search").fill,
      "function",
    );
    assert.equal(typeof target.page.getByAltText("Logo").click, "function");
    assert.equal(typeof target.page.getByTitle("More").click, "function");
    const locator = target.page.locator("#target");
    assert.equal(locator && typeof locator, "object");
    assert.equal(typeof locator.click, "function");
    assert.equal(typeof locator.evaluateAll, "function");
    assert.equal(typeof locator.extractAll, "undefined");
    assert.equal(typeof locator.nth(1).click, "function");
    assert.equal(typeof locator.first().click, "function");
    assert.equal(typeof locator.last().click, "function");
    assert.equal(typeof locator.getByText("Save").click, "function");
    assert.equal(
      typeof locator.getByRole("button", { name: "Save" }).click,
      "function",
    );
    assert.equal(typeof locator.locator(".child").fill, "function");
    assert.equal(typeof locator.filter({ hasText: "Ready" }).click, "function");
    assert.equal(
      typeof target.page.getByText("Row").filter({ hasText: "Ready" }).nth(0)
        .click,
      "function",
    );
  } finally {
    console.log = originalLog;
  }
});

test("installPrismSdk preserves the asynchronous page.url contract", async () => {
  const originalLog = console.log;
  const target = {};
  try {
    installPrismSdk(target, {
      cliLog() {},
      context: {
        page: {
          url: async () => "https://example.com/current",
        },
      },
    });
    const value = target.page.url();
    assert.equal(typeof value.then, "function");
    assert.equal(await value, "https://example.com/current");
  } finally {
    console.log = originalLog;
  }
});

test("installPrismSdk exposes the site facade under prism.learnings", () => {
  const originalLog = console.log;
  const target = { prism: {} };
  try {
    installPrismSdk(target, { cliLog() {} });
    assert.equal(target.prism.learnings, target.prism.helpers.site);
    assert.equal(typeof target.prism.learnings.skills, "function");
    assert.equal(typeof target.prism.learnings.skillsForUrl, "function");
    assert.equal(typeof target.prism.learnings.runTool, "function");
    assert.equal(typeof target.prism.learnings.runBrowserTool, "function");
    assert.equal(typeof target.prism.learnings.learnContext, "function");
  } finally {
    console.log = originalLog;
  }
});
