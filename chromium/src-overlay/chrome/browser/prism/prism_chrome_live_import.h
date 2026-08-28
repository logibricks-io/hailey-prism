// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_PRISM_PRISM_CHROME_LIVE_IMPORT_H_
#define CHROME_BROWSER_PRISM_PRISM_CHROME_LIVE_IMPORT_H_

#include <vector>

#include "base/files/file_path.h"
#include "base/values.h"
#include "components/history/core/browser/history_types.h"
#include "components/user_data_importer/common/imported_bookmark_entry.h"

class Profile;

namespace prism {

// Live-mergeable data read from the Chrome profile (bookmarks + history).
struct ChromeImportData {
  std::vector<user_data_importer::ImportedBookmarkEntry> bookmarks;
  history::URLRows history;
  bool chrome_found = false;
};

struct ChromeImportResult {
  base::DictValue report;  // staged import (keychain + encrypted files)
  ChromeImportData live;   // bookmarks + history, merged by the caller
};

// Runs the staged full import (see prism_chrome_importer.h) and reads the
// live-mergeable data. Blocking IO — call off the UI thread.
ChromeImportResult RunFullChromeImport(const base::FilePath& dest_profile_dir);

// Merges result.live into |profile| via ProfileWriter (no restart needed) and
// folds the merged counts into result.report["items"]. Call on the UI thread.
// Returns the final report to hand to the page.
base::DictValue MergeChromeImportLiveData(Profile* profile,
                                          ChromeImportResult& result);

}  // namespace prism

#endif  // CHROME_BROWSER_PRISM_PRISM_CHROME_LIVE_IMPORT_H_
