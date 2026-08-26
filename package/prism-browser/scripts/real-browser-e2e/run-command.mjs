import { spawn } from "node:child_process";

export function runCommand(command, args, options = {}) {
  return new Promise((resolve, reject) => {
    let stdout = "";
    let stderr = "";
    let settled = false;
    const echo = options.echo !== false;
    const child = spawn(command, args, {
      cwd: options.cwd ?? process.cwd(),
      env: { ...process.env, ...options.env },
      stdio: options.input ? ["pipe", "pipe", "pipe"] : "inherit",
    });
    const timer =
      options.timeoutMs &&
      setTimeout(() => {
        settled = true;
        child.kill("SIGINT");
        reject(
          new Error(
            `${command} ${args.join(" ")} timed out after ${options.timeoutMs}ms`,
          ),
        );
      }, options.timeoutMs);
    child.on("error", (error) => {
      if (settled) return;
      settled = true;
      if (timer) clearTimeout(timer);
      reject(error);
    });
    child.stdout?.on("data", (chunk) => {
      stdout += chunk;
      if (echo) process.stdout.write(chunk);
    });
    child.stderr?.on("data", (chunk) => {
      stderr += chunk;
      if (echo) process.stderr.write(chunk);
    });
    child.on("close", (code) => {
      if (settled) return;
      settled = true;
      if (timer) clearTimeout(timer);
      if (code === 0) {
        resolve({ stdout, stderr });
      } else {
        const error = new Error(
          `${command} ${args.join(" ")} exited with code ${code}`,
        );
        error.stdout = stdout;
        error.stderr = stderr;
        reject(error);
      }
    });
    if (options.input) {
      child.stdin.end(options.input);
    }
  });
}
