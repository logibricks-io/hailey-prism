/**
 * Shared handling for prism-binding errors.
 *
 * Browser-side failures expose two signals (see the PrismBindings JS API):
 *   - human-readable text (`error` on resolved results, `message` on rejected
 *     Errors), and
 *   - a stable `error_code` such as PRISM_TASK_SPACE_USER_IN_CONTROL.
 *
 * The code is the durable contract; the wording can drift between builds. Branch
 * on the code (isPrismUserControlError), not on the message. PRISM_ERROR_MESSAGES is
 * where prism-browser owns its wording for the few codes an agent must act on; every
 * other code (and any unknown future code) defers to the native error message.
 *
 * Single source of truth — error handling was previously duplicated across
 * helpers.ts and driver/nav.ts.
 */

import { markHardStop } from "./output-sink.js";

/** Stable error codes emitted by the native prism bindings. */
export const PRISM_ERROR_CODES = [
  "PRISM_BROWSER_UNAVAILABLE",
  "PRISM_CDP_CHANNEL_UNAVAILABLE",
  "PRISM_CDP_SEND_FAILED",
  "PRISM_INVALID_ARGUMENT",
  "PRISM_INVALID_RESULT_PAYLOAD",
  "PRISM_OPERATION_FAILED",
  "PRISM_RESULT_CONVERSION_FAILED",
  "PRISM_SNAPSHOT_FAILED",
  "PRISM_TASK_HOST_DISCONNECTED",
  "PRISM_TASK_SPACE_INACTIVE",
  "PRISM_TASK_SPACE_NOT_FOUND",
  "PRISM_TASK_SPACE_NOT_SELECTED",
  "PRISM_TASK_SPACE_UNAVAILABLE",
  "PRISM_TASK_SPACE_USER_IN_CONTROL",
  "PRISM_WEB_CONTENTS_UNAVAILABLE",
] as const;

export type PrismErrorCode = (typeof PRISM_ERROR_CODES)[number];

/**
 * Codes whose wording prism-browser owns. A listed code returns this static, id-less
 * message instead of the native error message — reserved for the two business signals
 * an agent must react to, not just report. Every other code is absent here and defers
 * to the native error message (and any unknown future code does too), which is more
 * specific than any static line.
 */
const PRISM_ERROR_MESSAGES: Partial<Record<PrismErrorCode, string>> = {
  PRISM_TASK_SPACE_INACTIVE: [
    "The user has taken control of this task space and ended the task, so it is no longer assigned to the agent and browser commands are paused.",
    "This is a hard stop, not an obstacle to route around — do not retry and do not take ownership back on your own.",
    "Wait until the user explicitly asks you to continue, then claim the space and resume:",
    "  await taskSpaces.claim(id)",
    "",
    `Offer the user choices like "Continue" or "Finish task" if your harness supports it; otherwise tell them: "You now control this task space. Reply 'continue' when ready and I will resume."`,
  ].join("\n"),
  PRISM_TASK_SPACE_USER_IN_CONTROL: [
    "The user has taken control of this task space, so browser commands are paused.",
    "This is a hard stop, not an obstacle to route around — do not retry and do not take control back on your own.",
    "Wait until the user explicitly asks you to continue, then take control back and resume:",
    "  await taskSpaces.takeOver()",
    "",
    `Offer the user choices like "Continue" or "Finish task" if your harness supports it; otherwise tell them: "You now control this task space. Reply 'continue' when ready and I will resume."`,
  ].join("\n"),
};

/** Type guard for codes this build knows about. */
export function isPrismErrorCode(value: unknown): value is PrismErrorCode {
  return (
    typeof value === "string" &&
    (PRISM_ERROR_CODES as readonly string[]).includes(value)
  );
}

/**
 * Pull the stable error_code out of any prism error shape: resolved
 * `{ error, error_code }` objects, rejected/thrown Errors carrying `.error_code`,
 * or a bare known code string. Returns the raw code (which may be one this build
 * does not know about yet) or undefined when none is present.
 */
export function prismErrorCode(err: unknown): string | undefined {
  if (typeof err === "string") {
    return isPrismErrorCode(err) ? err : undefined;
  }
  if (err && typeof err === "object") {
    const code = (err as Record<string, unknown>).error_code;
    if (typeof code === "string" && code) return code;
  }
  return undefined;
}

/**
 * Resolve any prism error into a stable `{ code, message }` pair.
 *
 * For a code prism-browser owns wording for, `message` is that owned wording.
 * Otherwise (a code not owned here, or an unknown future code) it falls back to
 * the native error message the binding returned, then the bare code, then a
 * generic string. `code` is the stable classifier and may be undefined.
 */
export function resolvePrismError(err: unknown): {
  code?: string;
  message: string;
} {
  const code = prismErrorCode(err);
  const message =
    (isPrismErrorCode(code) ? PRISM_ERROR_MESSAGES[code] : undefined) ??
    nativeErrorText(err) ??
    code ??
    "Unknown prism error";
  return { code, message };
}

/** Whether an prism error means the task is currently under user control. */
export function isPrismUserControlError(err: unknown): boolean {
  return prismErrorCode(err) === "PRISM_TASK_SPACE_USER_IN_CONTROL";
}

/**
 * Codes that halt the whole agent task rather than mark a routable obstacle: a task
 * space the user has taken back, or one that is inactive / not assigned to this agent.
 * Both require the user to explicitly hand control back before work can resume.
 */
function isPrismHardStopCode(code: string | undefined): boolean {
  return (
    code === "PRISM_TASK_SPACE_USER_IN_CONTROL" ||
    code === "PRISM_TASK_SPACE_INACTIVE"
  );
}

/** Whether an prism error is a hard stop the agent must not retry or route around. */
export function isPrismHardStopError(err: unknown): boolean {
  return isPrismHardStopCode(prismErrorCode(err));
}

/**
 * Build an Error carrying the resolved message and stable error_code from any prism
 * error shape. `op`, when given, prefixes the message with the failing operation.
 * Shared by assertNoPrismError (which throws it) and the CDP-send failure path (which
 * rejects pending requests with it) so every prism failure surfaces an identical
 * Error shape.
 */
export function buildPrismError(
  err: unknown,
  op?: string,
): Error & { error_code?: string } {
  const { code, message } = resolvePrismError(err);
  if (isPrismHardStopCode(code)) {
    // buildPrismError is the single birthplace of every prism error — assertNoPrismError and
    // the CDP-send failure path both route through it — so recording the hard stop here
    // catches it even when the agent's own try/catch later swallows the thrown Error.
    // The op-less owned message is the one the agent should see, regardless of which
    // operation surfaced it.
    markHardStop(message);
  }
  const error: Error & { error_code?: string } = new Error(
    op ? `${op}: ${message}` : message,
  );
  if (code) error.error_code = code;
  return error;
}

export function assertNoPrismError(result, op?: string) {
  if (
    result &&
    typeof result === "object" &&
    "error" in result &&
    result.error != null
  ) {
    throw buildPrismError(result, op);
  }
  return result;
}

/**
 * The native error message from any prism error shape — the binding's runtime
 * `error`/`message` text (dynamic, may vary across builds). Ignores bare codes.
 */
function nativeErrorText(err: unknown): string | undefined {
  if (typeof err === "string") {
    return isPrismErrorCode(err) ? undefined : err;
  }
  if (err && typeof err === "object") {
    const obj = err as Record<string, unknown>;
    if (obj.error != null) return formatPrismError(obj.error);
    if (typeof obj.message === "string" && obj.message) return obj.message;
  }
  return undefined;
}

export function formatPrismError(err: unknown): string {
  if (err == null) return String(err);
  if (typeof err === "string") return err;
  if (typeof err === "object") {
    const obj = err as Record<string, unknown>;
    if (typeof obj.message === "string") return obj.message;
    try {
      return JSON.stringify(err);
    } catch {
      return String(err);
    }
  }
  return String(err);
}
