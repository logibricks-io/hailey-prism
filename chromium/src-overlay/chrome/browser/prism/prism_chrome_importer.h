// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_PRISM_PRISM_CHROME_IMPORTER_H_
#define CHROME_BROWSER_PRISM_PRISM_CHROME_IMPORTER_H_

#include "base/files/file_path.h"
#include "base/values.h"

namespace prism {

// Full Chrome profile import (Phase 6): inherits Chrome's login state by
// copying the "Chrome Safe Storage" keychain seed into Prism's own item
// ("Prism Safe Storage" — one interactive ACL prompt, user-present by
// design) and staging Chrome's profile files (Cookies, Login Data,
// Preferences, Secure Preferences, Extensions/) for the next startup.
//
// Why staging: the running browser owns those files and would rewrite them
// on shutdown, clobbering an in-place copy. StageChromeImport() copies into
// <user-data-dir>/PrismChromeImport.staging/; ApplyStagedChromeImport() runs
// from PreMainMessageLoopRunImpl (before any profile service opens the
// files) and moves them into place. Bookmarks/history are imported live via
// ProfileWriter instead (they merge without a restart) and stay in the
// welcome page.
//
// Test hooks (documented in docs/chrome-import.md):
//   --prism-chrome-profile-dir=<path>     source profile dir override
//   --prism-import-keychain-read-from=<path>   search only this keychain
//                                              for the Chrome item
//   --prism-import-keychain-write-to=<path>    write the Prism item into
//                                              this keychain (no stomping
//                                              the user's real item)

// Resolves the Chrome profile dir to import from (honors the
// --prism-chrome-profile-dir test override). Exported so the welcome page's
// live bookmarks/history import reads the same source as the staged import.
base::FilePath ChromeSourceProfileDir();

// Copies Chrome's Safe Storage password into Prism's keychain item and
// stages Chrome's profile files. Runs blocking IO — call off the UI thread.
// The report maps item names ("cookies", "passwords", "preferences",
// "securePreferences", "extensions", "keychain") to {status, detail}:
//   ok / missing / failed:<why> / skipped:keychain-denied
// plus "chromeFound" (bool) and "stagedForRestart" (bool).
base::DictValue StageChromeImport(const base::FilePath& dest_profile_dir);

// Startup hook: lands a staged import before profile services open those
// files. No-op (one PathExists) when nothing is staged.
void ApplyStagedChromeImport(const base::FilePath& user_data_dir);

}  // namespace prism

#endif  // CHROME_BROWSER_PRISM_PRISM_CHROME_IMPORTER_H_
