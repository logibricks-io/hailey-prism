# Install Prism

Read this file only when Prism isn't installed yet, or when the user asks to
install it. For day-to-day browser work, go back to `SKILL.md`.

The prism-browser skill depends on the Prism browser: the `prism-browser`
command drives the app over its kernel control channel. Once Prism is
installed and the CLI link is on the PATH, the environment is ready.

## Install steps (macOS only)

The install script lives at `scripts/install.sh` in this skill and supports
macOS only. It will:

- Download the Prism dmg for your CPU architecture (arm64 / x64) from the
  GitHub Releases "latest" channel of the Prism repository
  (`github.com/logibricks-io/hailey-prism`).
- Install `Prism.app` to `/Applications` (falling back to `~/Applications`
  without prompting for sudo when `/Applications` is not writable).
- Strip the quarantine attribute to keep Gatekeeper from blocking the first
  launch (internal builds are unsigned; signed+notarized builds don't need it).
- Register the `prism-browser` CLI into `~/.local/bin` (a symlink to the
  bundle-embedded copy at `Prism.app/Contents/Resources/prism-browser`;
  until the CLI is bundled into the dmg, this step is skipped with a printed
  note — see the packaging TODO in `chromium/scripts/package.sh`).
- Launch Prism.

Run the script (use the script's actual path under this skill's directory):

```bash
sh skills/prism-browser/scripts/install.sh
```

Print the steps without executing anything (no network access):

```bash
PRISM_INSTALL_DRY_RUN=1 sh skills/prism-browser/scripts/install.sh
```

If Prism is already installed, the script skips the download and opens the app
directly.

## First run

On the very first launch, Prism shows the welcome page
(`chrome://prism-welcome`), which offers importing bookmarks and history from
the system Chrome (or from Safari / Firefox), or skipping. Skipping has no
side effects — the page can be revisited at any time at that URL.

## After installing: confirm `prism-browser` is available

```bash
command -v prism-browser
```

If it reports that the command isn't found, `~/.local/bin` is most likely not
on the current PATH. Fix it temporarily and retry:

```bash
export PATH="$HOME/.local/bin:$PATH"
command -v prism-browser
```

Once the command exists, verify the runtime with a minimal heredoc:

```bash
prism-browser <<'EOF'
console.log('prism-browser ready')
EOF
```

Printing `prism-browser ready` means the environment is ready.

## After that, return to the original task

Once the environment is ready, return to the user's original task and continue
with the task space flow in `SKILL.md` — start from
`taskSpaces.useOrCreate(name)` and proceed as usual.

## Troubleshooting

- **Not macOS**: the script supports macOS only (`uname -s` is `Darwin`).
- **Download failed**: the script retries 3 times automatically; if it still
  fails, check the network and that the latest release has the
  `Prism-mac-<arch>.dmg` asset.
- **Gatekeeper still blocks it**: the script already tries to strip
  quarantine; if the first launch is still blocked, allow Prism manually under
  System Settings → Privacy & Security.
- **`prism-browser` command missing**: the dmg may predate the bundled CLI —
  the script prints a note and skips the symlink in that case; the CLI also
  runs straight from the repository (`node host/src/cli.js`).
- **Command still unavailable after installing**: confirm `~/.local/bin` is on
  the PATH (see above).
