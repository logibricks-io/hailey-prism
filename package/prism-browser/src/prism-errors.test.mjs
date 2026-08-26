import test from "node:test";
import assert from "node:assert/strict";

import {
  assertNoPrismError,
  prismErrorCode,
  isPrismErrorCode,
  isPrismUserControlError,
  resolvePrismError,
} from "../dist/src/prism-errors.js";

test("prismErrorCode extracts the code from every error shape", () => {
  // resolved { error, error_code } object
  assert.equal(
    prismErrorCode({ error: "nope", error_code: "PRISM_BROWSER_UNAVAILABLE" }),
    "PRISM_BROWSER_UNAVAILABLE",
  );
  // rejected / thrown Error carrying .error_code
  const err = Object.assign(new Error("boom"), {
    error_code: "PRISM_SNAPSHOT_FAILED",
  });
  assert.equal(prismErrorCode(err), "PRISM_SNAPSHOT_FAILED");
  // bare known code string (e.g. onSendCDPMessageError second arg)
  assert.equal(
    prismErrorCode("PRISM_TASK_SPACE_USER_IN_CONTROL"),
    "PRISM_TASK_SPACE_USER_IN_CONTROL",
  );
  // future code this build does not know about is still returned
  assert.equal(
    prismErrorCode({ error_code: "PRISM_FUTURE_CODE" }),
    "PRISM_FUTURE_CODE",
  );
  // no code present
  assert.equal(prismErrorCode({ error: "plain message" }), undefined);
  assert.equal(prismErrorCode("plain message"), undefined);
});

test("isPrismErrorCode narrows to known codes only", () => {
  assert.equal(isPrismErrorCode("PRISM_TASK_SPACE_NOT_FOUND"), true);
  assert.equal(isPrismErrorCode("PRISM_FUTURE_CODE"), false);
  assert.equal(isPrismErrorCode(undefined), false);
});

test("resolvePrismError overrides the native error message with the owned wording for an owned code", () => {
  const { code, message } = resolvePrismError({
    error: "Task space 7 is not assigned to an agent.",
    error_code: "PRISM_TASK_SPACE_INACTIVE",
  });
  assert.equal(code, "PRISM_TASK_SPACE_INACTIVE");
  // Owned id-less guidance replaces the native "Task space 7 ..." text.
  assert.match(message, /taskSpaces\.claim\(id\)/);
  assert.doesNotMatch(message, /\b7\b/);
});

test("resolvePrismError keeps the native error message for an unknown future code", () => {
  assert.deepEqual(
    resolvePrismError({
      error: "Some build-specific detail",
      error_code: "PRISM_FUTURE_CODE",
    }),
    {
      code: "PRISM_FUTURE_CODE",
      message: "Some build-specific detail",
    },
  );
});

test("resolvePrismError defers to the native error message for a code prism-browser does not own", () => {
  // PRISM_OPERATION_FAILED is not owned: the client wording (e.g. which operation
  // failed) is more specific than any static line.
  assert.deepEqual(
    resolvePrismError({
      error: "Failed to create task space",
      error_code: "PRISM_OPERATION_FAILED",
    }),
    {
      code: "PRISM_OPERATION_FAILED",
      message: "Failed to create task space",
    },
  );
});

test("resolvePrismError falls back to the raw code for a bare non-owned code", () => {
  // prism-browser does not own NOT_SELECTED and a bare code carries no native error message,
  // so the stable code itself is the most specific thing to surface.
  assert.deepEqual(resolvePrismError("PRISM_TASK_SPACE_NOT_SELECTED"), {
    code: "PRISM_TASK_SPACE_NOT_SELECTED",
    message: "PRISM_TASK_SPACE_NOT_SELECTED",
  });
});

test("resolvePrismError uses the id-less guidance block for a bare user-control code", () => {
  const { code, message } = resolvePrismError("PRISM_TASK_SPACE_USER_IN_CONTROL");
  assert.equal(code, "PRISM_TASK_SPACE_USER_IN_CONTROL");
  assert.match(message, /taken control of this task space/);
  assert.match(message, /taskSpaces\.takeOver\(\)/);
  assert.doesNotMatch(message, /<id>/);
});

test("resolvePrismError falls back to the raw code, then a generic message", () => {
  assert.deepEqual(resolvePrismError({ error_code: "PRISM_FUTURE_CODE" }), {
    code: "PRISM_FUTURE_CODE",
    message: "PRISM_FUTURE_CODE",
  });
  assert.deepEqual(resolvePrismError({}), {
    code: undefined,
    message: "Unknown prism error",
  });
});

test("isPrismUserControlError keys on the stable code, not wording", () => {
  assert.equal(
    isPrismUserControlError(
      Object.assign(new Error("anything at all"), {
        error_code: "PRISM_TASK_SPACE_USER_IN_CONTROL",
      }),
    ),
    true,
  );
  // wording that mentions user control but lacks the code is not a match
  assert.equal(
    isPrismUserControlError(new Error("the user is controlling this")),
    false,
  );
  assert.equal(
    isPrismUserControlError({ error_code: "PRISM_SNAPSHOT_FAILED" }),
    false,
  );
});

test("assertNoPrismError resolves the message via the code and attaches error_code", () => {
  try {
    assertNoPrismError(
      {
        error: "Task space not selected",
        error_code: "PRISM_TASK_SPACE_NOT_SELECTED",
      },
      "listTabs",
    );
    assert.fail("expected assertNoPrismError to throw");
  } catch (err) {
    assert.equal(err.message, "listTabs: Task space not selected");
    assert.equal(err.error_code, "PRISM_TASK_SPACE_NOT_SELECTED");
  }
});

test("assertNoPrismError omits the prefix when no op is given", () => {
  try {
    assertNoPrismError({
      error: "The task space is inactive: 10",
      error_code: "PRISM_TASK_SPACE_INACTIVE",
    });
    assert.fail("expected assertNoPrismError to throw");
  } catch (err) {
    // No op given, so no "<op>: " prefix — the owned block starts the message.
    assert.match(err.message, /^The user has taken control/);
    assert.match(err.message, /taskSpaces\.claim\(id\)/);
    assert.doesNotMatch(err.message, /\b10\b/);
    assert.equal(err.error_code, "PRISM_TASK_SPACE_INACTIVE");
  }
});

test("assertNoPrismError passes through results with no error", () => {
  const ok = { tabs: [] };
  assert.equal(assertNoPrismError(ok, "listTabs"), ok);
});
