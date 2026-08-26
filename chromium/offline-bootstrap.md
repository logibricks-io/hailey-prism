# Offline bootstrap runbook (for ops)

How to produce a Prism Chromium checkout on a machine with good Google
connectivity and transfer it to a build machine that cannot reach Google
reliably. Target and build machines must both be macOS.

## What gets downloaded, from where

| Content | Source | Size (approx) |
|---|---|---|
| depot_tools | `https://chromium.googlesource.com/chromium/tools/depot_tools.git` (git) | ~1 GB |
| Chromium source (main pack, shallow) | `https://chromium.googlesource.com/chromium/src.git` (git) | ~50 GB |
| ~100 dependency repos (listed in `src/DEPS`) | mostly `*.googlesource.com` (git) | ~10–20 GB |
| Toolchains: clang, rust, gn, ninja, mac SDK, node | CIPD (`chrome-infra-packages.appspot.com`) + Google Cloud Storage, fetched by `gclient runhooks` | ~5–10 GB |

Everything is Google infrastructure. There is no official tarball mirror; the
git-over-HTTPS + CIPD path above is the only supported route, which is why the
offline copy below exists.

## Produce the copy (ops side)

```bash
# 1. depot_tools
git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git ~/chromium/depot_tools
export PATH="$HOME/chromium/depot_tools:$PATH"

# 2. fetch (resumable; re-run on any interruption)
mkdir -p ~/chromium/prism && cd ~/chromium/prism
fetch --no-history chromium

# 3. pin to Prism's revision (see chromium/DEPS.pin in the repo)
cd ~/chromium/prism/src
git fetch --depth 1 origin refs/tags/151.0.7922.174
gclient sync --no-history --nohooks --revision "src@refs/tags/151.0.7922.174" -D

# 4. toolchains — REQUIRED, otherwise the build machine must go online
gclient runhooks
```

Then pack it up. **Do not copy to exFAT/FAT32 — the tree contains symlinks.**
Either use an APFS/HFS+ formatted drive (replace `PRISMUSB` with the actual
volume name):

```bash
rsync -a ~/chromium/ /Volumes/PRISMUSB/chromium/
```

or a compressed tarball (any drive filesystem):

```bash
tar -cf - -C ~/chromium . | zstd -3 -T0 > /Volumes/PRISMUSB/chromium-prism.tar.zst
```

Optional but speeds up rebuilds: `~/.cipd` (the CIPD client cache).

## Verify before transport

- `cd ~/chromium/prism/src && gclient sync --no-history --nohooks -D` should
  finish quickly with "everything is up-to-date" (requires connectivity; run
  it before leaving the well-connected network).
- Spot-check that the toolchain landed in the tree:
  `third_party/llvm-build/Release+Asserts/bin/clang` must exist.

## Notes

- The pinned revision lives in `chromium/DEPS.pin` of the Prism repo; keep it
  in sync with what was fetched.
- Hand the drive/archive to the build team. What happens on the build machine
  is documented in `chromium/README.md` ("Receiving an offline copy") — not
  part of ops' job.
