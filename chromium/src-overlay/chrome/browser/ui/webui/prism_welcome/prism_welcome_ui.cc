// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/ui/webui/prism_welcome/prism_welcome_ui.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/memory/ref_counted_memory.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/importer/external_process_importer_host.h"
#include "chrome/browser/importer/importer_list.h"
#include "chrome/browser/importer/profile_writer.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/prism/prism_chrome_importer.h"
#include "chrome/browser/profiles/profile.h"
#include "components/history/core/browser/history_types.h"
#include "components/user_data_importer/common/imported_bookmark_entry.h"
#include "components/user_data_importer/common/importer_data_types.h"
#include "components/user_data_importer/common/importer_type.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "content/public/common/url_constants.h"
#include "sql/database.h"
#include "sql/statement.h"

namespace prism {

namespace {

// ------------------------------- the page -----------------------------------

constexpr char kPageHtml[] = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><title>Welcome to Prism</title>
<style>
  :root { color-scheme: dark; }
  body { font-family: -apple-system, system-ui, sans-serif; background: #17141f;
         color: #ece9f4; margin: 0; display: flex; justify-content: center; }
  .card { max-width: 560px; margin: 6vh 24px; background: #211c2e;
          border: 1px solid #352c4a; border-radius: 16px; padding: 36px 40px; }
  h1 { font-size: 24px; font-weight: 650; margin: 0 0 6px; }
  h1 .mark { color: #b69cff; }
  p { color: #cfc8e3; font-size: 14px; line-height: 1.55; }
  .small { font-size: 12px; color: #a79fbd; }
  .actions { display: flex; gap: 10px; margin-top: 22px; flex-wrap: wrap; }
  button { border-radius: 9px; padding: 9px 18px; font-size: 13.5px;
           cursor: pointer; border: 1px solid #453a63; }
  .primary { background: #7c4dff; border-color: #7c4dff; color: white; }
  .primary:hover { background: #8f66ff; }
  .ghost { background: transparent; color: #cfc8e3; }
  #status { margin-top: 18px; font-size: 13px; color: #b69cff; min-height: 1.2em; }
  #report { margin-top: 12px; font-size: 13px; }
  #report .row { display: flex; gap: 8px; padding: 3px 0; align-items: baseline; }
  #report .k { color: #cfc8e3; min-width: 150px; }
  #report .ok { color: #8fd3a5; }
  #report .skip { color: #a79fbd; }
  #report .bad { color: #ff8a9b; }
  #report .detail { color: #a79fbd; font-size: 12px; }
  #restart { display: none; margin-top: 16px; }
  #restart.show { display: inline-block; }
</style></head><body>
<div class="card">
  <h1>Welcome to <span class="mark">Prism</span></h1>
  <p>Prism is a browser for humans and AI agents sharing one browser: agents
     work in isolated task spaces, you stay in control, and you can watch or
     take over at any time from chrome://prism-spaces.</p>
  <p>Bring your Chrome with you — bookmarks, history, cookies, passwords and
     extensions. Cookies and passwords are encrypted by Chrome; migrating them
     asks macOS once for access to "Chrome Safe Storage" in your keychain
     (that's your login sessions coming along). Prism never writes into
     Chrome's profile.</p>
  <div class="actions">
    <button class="primary" id="import-chrome">Import from Chrome</button>
    <button class="ghost" id="import-other">Import from Safari / Firefox</button>
    <button class="ghost" id="skip">Skip</button>
  </div>
  <div id="status"></div>
  <div id="report"></div>
  <button class="primary" id="restart">Restart Prism to finish</button>
  <p class="small">You can always do this later: open chrome://prism-welcome.
     Importing replaces this fresh profile's empty data files; anything Prism
     encrypted before the import becomes unreadable.</p>
</div>
<script src="app.js"></script>
</body></html>)HTML";

constexpr char kAppJs[] = R"JS(function $(id) { return document.getElementById(id); }
const LABELS = {
  bookmarks: "Bookmarks",
  history: "History",
  cookies: "Cookies (login state)",
  passwords: "Passwords",
  preferences: "Preferences",
  securePreferences: "Secure Preferences",
  extensions: "Extensions",
};
function row(kind, label, detail) {
  const el = document.createElement("div");
  el.className = "row";
  const icon = document.createElement("span");
  icon.className = kind;
  icon.textContent = kind === "ok" ? "\u2713" : kind === "bad" ? "\u2717" : "\u2013";
  const name = document.createElement("span");
  name.className = "k";
  name.textContent = label;
  el.appendChild(icon);
  el.appendChild(name);
  if (detail) {
    const d = document.createElement("span");
    d.className = "detail";
    d.textContent = detail;
    el.appendChild(d);
  }
  $("report").appendChild(el);
}
$("import-chrome").addEventListener("click", () => {
  $("report").replaceChildren();
  $("restart").className = "";
  $("status").textContent = "Importing from Chrome\u2026 (macOS may ask once for keychain access)";
  chrome.send("importFromChrome");
});
$("import-other").addEventListener("click", () => {
  $("status").textContent = "Looking for Safari / Firefox profiles\u2026";
  chrome.send("importFromOther");
});
$("skip").addEventListener("click", () => window.close());
$("restart").addEventListener("click", () => chrome.send("restartNow"));
window.prismWelcome = {
  onStatus(text) { $("status").textContent = text; },
  onImportReport(reportJson) {
    const report = JSON.parse(reportJson);
    window.__lastReport = report;
    if (!report.chromeFound) {
      $("status").textContent =
          "No Chrome profile found (~/Library/Application Support/Google/Chrome/Default). Nothing imported.";
      return;
    }
    for (const [key, label] of Object.entries(LABELS)) {
      const item = report.items && report.items[key];
      if (!item) continue;
      let kind = "ok", detail = item.detail || "";
      if (item.status === "ok") { kind = "ok"; }
      else if (item.status === "missing") { kind = "skip"; detail = "not present"; }
      else if (item.status.startsWith("skipped")) { kind = "skip"; detail = "keychain access denied"; }
      else { kind = "bad"; detail = detail || item.status; }
      row(kind, label, detail);
    }
    if (report.keychain === "copied") {
      row("ok", "Keychain seed", "Chrome Safe Storage \u2192 Prism Safe Storage");
    } else if (report.keychain === "denied") {
      row("skip", "Keychain seed", "denied \u2014 cookies/passwords stay with Chrome");
    } else if (report.keychain && !report.keychain.startsWith("skipped")) {
      row("bad", "Keychain seed", report.keychain);
    }
    if (report.stagedForRestart) {
      $("status").textContent = "Import staged. Restart to activate cookies, passwords and extensions.";
      $("restart").className = "show";
    } else if (report.keychain === "denied") {
      $("status").textContent = "Imported bookmarks and history only (keychain access denied).";
    } else {
      $("status").textContent = "Import finished.";
    }
  },
};)JS";

// --------------------------- Chrome profile import --------------------------
//
// The upstream importer framework has no Chrome importer on macOS (it covers
// Safari/Firefox there), so Prism reads Chrome's default profile directly.
// Two lanes:
//  - live merge (bookmarks + history, below) via ProfileWriter — no restart;
//  - staged full import (cookies/passwords/preferences/extensions) via
//    prism::StageChromeImport: Chrome's Safe Storage seed is copied into
//    Prism's keychain item (one interactive ACL prompt) and the encrypted
//    profile files are staged for the next startup (ApplyStagedChromeImport
//    runs before profile services open them — an in-place copy would be
//    clobbered by the running browser on shutdown).

struct ChromeImportData {
  std::vector<user_data_importer::ImportedBookmarkEntry> bookmarks;
  history::URLRows history;
  bool chrome_found = false;
};

struct ChromeImportResult {
  base::DictValue report;  // staged import (keychain + encrypted files)
  ChromeImportData live;   // bookmarks + history, merged below
};

void ReadChromeProfileDataInto(ChromeImportData* out);

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

PrismWelcomeUIConfig::PrismWelcomeUIConfig()
    : content::DefaultWebUIConfig<PrismWelcomeUI>(content::kChromeUIScheme,
                                                  "prism-welcome") {}
PrismWelcomeUIConfig::~PrismWelcomeUIConfig() = default;

PrismWelcomeUI::PrismWelcomeUI(content::WebUI* web_ui)
    : content::WebUIController(web_ui) {
  importer_list_ = std::make_unique<ImporterList>();
  auto* browser_context = web_ui->GetWebContents()->GetBrowserContext();
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      browser_context, "prism-welcome");
  source->SetRequestFilter(
      base::BindRepeating([](const std::string& path) { return true; }),
      base::BindRepeating(
          [](const std::string& path,
             content::WebUIDataSource::GotDataCallback callback) {
            if (path == "app.js") {
              std::move(callback).Run(
                  base::MakeRefCounted<base::RefCountedString>(kAppJs));
              return;
            }
            std::move(callback).Run(
                base::MakeRefCounted<base::RefCountedString>(kPageHtml));
          }));
  web_ui->RegisterMessageCallback(
      "importFromChrome",
      base::BindRepeating(&PrismWelcomeUI::OnImportFromChrome,
                          weak_factory_.GetWeakPtr()));
  web_ui->RegisterMessageCallback(
      "restartNow",
      base::BindRepeating(
          [](const base::ListValue&) {
            // The staged import lands during the next startup, before the
            // profile opens those files; OSCrypt then derives its key from
            // the migrated seed.
            chrome::AttemptRestart();
          }));
  web_ui->RegisterMessageCallback(
      "importFromOther",
      base::BindRepeating(
          [](base::WeakPtr<PrismWelcomeUI> self, const base::ListValue&) {
            if (!self) {
              return;
            }
            self->ReportStatus("Looking for Safari / Firefox profiles…");
            self->importer_list_->DetectSourceProfiles(
                "en-US", /*include_interactive_profiles=*/true,
                base::BindOnce(&PrismWelcomeUI::OnSourceProfilesDetected,
                               self));
          },
          weak_factory_.GetWeakPtr()));
}

PrismWelcomeUI::~PrismWelcomeUI() = default;

void PrismWelcomeUI::ReportStatus(const std::string& status) {
  web_ui()->CallJavascriptFunctionUnsafe("prismWelcome.onStatus",
                                         base::ValueView(status));
}

void PrismWelcomeUI::OnSourceProfilesDetected() {
  if (importer_list_->count() == 0) {
    ReportStatus("No Safari or Firefox profile found on this machine.");
    return;
  }
  const auto& source = importer_list_->GetSourceProfileAt(0);
  Profile* profile =
      Profile::FromBrowserContext(web_ui()->GetWebContents()->GetBrowserContext());
  auto* host = new ExternalProcessImporterHost();
  ReportStatus(std::string("Importing from ") +
               base::UTF16ToUTF8(source.importer_name) + "…");
  // Matches the settings-page call site's ownership idiom.
  host->StartImportSettings(source, profile,
                            user_data_importer::ALL,
                            new ProfileWriter(profile));
  ReportStatus("Import from " + base::UTF16ToUTF8(source.importer_name) +
               " started — a progress notification will follow.");
}

void PrismWelcomeUI::OnImportFromChrome(const base::ListValue& args) {
  Profile* profile =
      Profile::FromBrowserContext(web_ui()->GetWebContents()->GetBrowserContext());
  ReportStatus("Importing from Chrome… (macOS may ask once for keychain access)");
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&RunFullChromeImport, profile->GetPath()),
      base::BindOnce(
          [](base::WeakPtr<PrismWelcomeUI> self, Profile* profile,
             ChromeImportResult result) {
            if (!self) {
              return;
            }
            if (!result.live.chrome_found) {
              std::string json;
              base::JSONWriter::Write(base::Value(std::move(result.report)),
                                      &json);
              self->web_ui()->CallJavascriptFunctionUnsafe(
                  "prismWelcome.onImportReport", base::ValueView(std::move(json)));
              return;
            }
            scoped_refptr<ProfileWriter> writer =
                base::MakeRefCounted<ProfileWriter>(profile);
            size_t bookmarks = 0;
            size_t history_rows = 0;
            if (!result.live.bookmarks.empty()) {
              bookmarks = result.live.bookmarks.size();
              writer->AddBookmarks(result.live.bookmarks,
                                   u"Imported from Chrome");
            }
            if (!result.live.history.empty()) {
              history_rows = result.live.history.size();
              writer->AddHistoryPage(result.live.history,
                                     history::VisitSource::SOURCE_BROWSED);
            }
            // Fold the live-merge outcome into the staged report so the page
            // renders a single item list.
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
            std::string json;
            base::JSONWriter::Write(base::Value(std::move(result.report)),
                                    &json);
            self->web_ui()->CallJavascriptFunctionUnsafe(
                "prismWelcome.onImportReport", base::ValueView(std::move(json)));
          },
          weak_factory_.GetWeakPtr(), profile));
}

}  // namespace prism
