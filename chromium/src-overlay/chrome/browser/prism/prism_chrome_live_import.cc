// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/prism/prism_chrome_live_import.h"

#include <string>
#include <utility>

#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/importer/profile_writer.h"
#include "chrome/browser/prism/prism_chrome_importer.h"
#include "chrome/browser/profiles/profile.h"
#include "sql/database.h"
#include "sql/statement.h"

namespace prism {

namespace {

void CollectBookmarks(const base::DictValue& node,
                      std::vector<std::u16string> folder_path,
                      bool in_toolbar,
                      ChromeImportData* out) {
  const std::string* type = node.FindString("type");
  if (type && *type == "url") {
    const std::string* url = node.FindString("url");
    const std::string* name = node.FindString("name");
    if (!url || url->empty()) {
      return;
    }
    user_data_importer::ImportedBookmarkEntry entry;
    entry.url = GURL(*url);
    entry.title = base::UTF8ToUTF16(name ? *name : *url);
    entry.in_toolbar = in_toolbar;
    // Chrome stores folder names per node; the entry path is the ancestor
    // chain as a vector (ImportedBookmarkEntry.path semantics).
    entry.path = folder_path;
    out->bookmarks.push_back(std::move(entry));
    return;
  }
  const base::ListValue* children = node.FindList("children");
  if (!children) {
    return;
  }
  const std::string* name = node.FindString("name");
  if (name && !name->empty() && type && *type == "folder") {
    folder_path.push_back(base::UTF8ToUTF16(*name));
  }
  for (const base::Value& child : *children) {
    const base::DictValue* child_dict = child.GetIfDict();
    if (child_dict) {
      CollectBookmarks(*child_dict, folder_path, in_toolbar, out);
    }
  }
}

void ReadChromeProfileDataInto(ChromeImportData* out) {
  const base::FilePath dir = ChromeSourceProfileDir();
  out->chrome_found = !dir.empty() && base::PathExists(dir);

  // Bookmarks (JSON).
  std::string bookmarks_json;
  if (base::ReadFileToString(dir.Append("Bookmarks"), &bookmarks_json)) {
    out->chrome_found = true;
    auto parsed = base::JSONReader::Read(bookmarks_json, base::JSON_PARSE_RFC);
    const base::DictValue* roots =
        parsed ? parsed->GetDict().FindDict("roots") : nullptr;
    if (roots) {
      for (const auto [root_name, root_value] : *roots) {
        const base::DictValue* root = root_value.GetIfDict();
        if (!root) {
          continue;
        }
        const bool toolbar = root_name == "bookmark_bar";
        CollectBookmarks(*root, {}, toolbar, out);
      }
    }
  }

  // History (SQLite): copy first so a running Chrome's lock never matters,
  // then open the copy read-only.
  const base::FilePath history_db = dir.Append("History");
  if (base::PathExists(history_db)) {
    out->chrome_found = true;
    base::FilePath tmp_dir;
    if (base::CreateNewTempDirectory("prism-chrome-history", &tmp_dir)) {
      base::CopyFile(history_db, tmp_dir.Append("History"));
    }
    sql::Database db(sql::Database::Tag("PrismChromeImporter"));
    if (!tmp_dir.empty() && db.Open(tmp_dir.Append("History"))) {
      sql::Statement statement(db.GetUniqueStatement(
          "SELECT url, title, visit_count, last_visit_time FROM urls"));
      while (statement.Step()) {
        history::URLRow row(GURL(statement.ColumnString(0)));
        row.set_title(base::UTF8ToUTF16(statement.ColumnString(1)));
        row.set_visit_count(statement.ColumnInt(2));
        row.set_last_visit(
            base::Time::FromDeltaSinceWindowsEpoch(
                base::Microseconds(statement.ColumnInt64(3))));
        out->history.push_back(std::move(row));
      }
      db.Close();
    }
    if (!tmp_dir.empty()) {
      base::DeletePathRecursively(tmp_dir);
    }
  }
}

}  // namespace

ChromeImportResult RunFullChromeImport(const base::FilePath& dest_profile_dir) {
  ChromeImportResult result;
  result.report = StageChromeImport(dest_profile_dir);
  result.live.chrome_found =
      !ChromeSourceProfileDir().empty() &&
      base::PathExists(ChromeSourceProfileDir());
  if (!result.live.chrome_found) {
    return result;
  }
  ReadChromeProfileDataInto(&result.live);
  return result;
}

base::DictValue MergeChromeImportLiveData(Profile* profile,
                                          ChromeImportResult& result) {
  scoped_refptr<ProfileWriter> writer =
      base::MakeRefCounted<ProfileWriter>(profile);
  size_t bookmarks = 0;
  size_t history_rows = 0;
  if (!result.live.bookmarks.empty()) {
    bookmarks = result.live.bookmarks.size();
    writer->AddBookmarks(result.live.bookmarks, u"Imported from Chrome");
  }
  if (!result.live.history.empty()) {
    history_rows = result.live.history.size();
    writer->AddHistoryPage(result.live.history,
                           history::VisitSource::SOURCE_BROWSED);
  }
  // Fold the live-merge outcome into the staged report so the page renders a
  // single item list.
  base::DictValue* items = result.report.FindDict("items");
  if (items) {
    base::DictValue bm;
    bm.Set("status", bookmarks ? "ok" : "missing");
    bm.Set("detail", base::NumberToString(bookmarks) + " imported");
    items->Set("bookmarks", std::move(bm));
    base::DictValue hi;
    hi.Set("status", history_rows ? "ok" : "missing");
    hi.Set("detail", base::NumberToString(history_rows) + " imported");
    items->Set("history", std::move(hi));
  }
  return std::move(result.report);
}

}  // namespace prism
