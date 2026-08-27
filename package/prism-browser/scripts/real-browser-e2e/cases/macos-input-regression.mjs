export function macosInputRegressionCase() {
  return `
    assertEqual(process.platform, "darwin", "bare Meta input isolation runs on macOS");

    const { execFile } = await import("node:child_process");
    const { promisify } = await import("node:util");
    const execFileAsync = promisify(execFile);

    async function isSystemInformationRunning() {
      const { stdout } = await execFileAsync("/usr/bin/lsappinfo", ["list"]);
      return /bundleID="com\\.apple\\.SystemProfiler"/.test(stdout);
    }

    const space = await taskSpaces.useOrCreate(taskName);
    await resetHome();

    // The leak this case guards is focus-dependent: the shell redispatches an
    // unhandled key into [NSApp sendEvent:] only while its window is key, so a
    // run with no visible/focused window is a false green. On the fork,
    // taskSpaces.show() (Prism.showTaskSpace) opens a real window for the
    // selected space and activates the app (the delegate's Activate reaches
    // [NSApp activateIgnoringOtherApps:]), making the probe deterministic.
    // Simulated (daemon/stock) spaces have no shell windows; the catch keeps
    // the original behavior there.
    if (space && space.id != null) {
      await taskSpaces.show(space.id).catch(() => {});
      await page.waitForTimeout(500);
    }

    const systemInformationWasRunning = await isSystemInformationRunning();
    assertEqual(
      systemInformationWasRunning,
      false,
      "System Information is closed before the bare Meta input probe"
    );

    let systemInformationLaunched = false;
    try {
      await cdp("Input.dispatchKeyEvent", {
        type: "keyDown",
        key: "Meta",
        code: "MetaLeft",
        windowsVirtualKeyCode: 91,
        nativeVirtualKeyCode: 55,
        metaKey: true,
        modifiers: 4,
      });
      await cdp("Input.dispatchKeyEvent", {
        type: "keyUp",
        key: "Meta",
        code: "MetaLeft",
        windowsVirtualKeyCode: 91,
        nativeVirtualKeyCode: 55,
        metaKey: false,
        modifiers: 0,
      });

      const deadline = Date.now() + 2000;
      while (Date.now() <= deadline) {
        systemInformationLaunched = await isSystemInformationRunning();
        if (systemInformationLaunched) break;
        await page.waitForTimeout(100);
      }
    } finally {
      systemInformationLaunched =
        systemInformationLaunched ||
        (await isSystemInformationRunning().catch(() => false));
      if (!systemInformationWasRunning && systemInformationLaunched) {
        await execFileAsync("/usr/bin/osascript", [
          "-e",
          'tell application id "com.apple.SystemProfiler" to quit',
        ]);
      }
    }

    assertEqual(
      systemInformationLaunched,
      false,
      "raw Meta CDP input does not launch macOS System Information"
    );
  `;
}
