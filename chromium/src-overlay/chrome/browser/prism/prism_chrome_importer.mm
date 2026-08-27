// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/prism/prism_chrome_importer.h"

#import <Foundation/Foundation.h>
#import <Security/Security.h>

#include <string>

#include "base/apple/bridging.h"
#include "base/apple/scoped_cftyperef.h"
#include "base/command_line.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "sql/database.h"
#include "sql/statement.h"

namespace prism {

namespace {

// Chrome's Safe Storage item (Google-Chrome branding) vs ours (patch 0004).
constexpr char kChromeService[] = "Chrome Safe Storage";
constexpr char kChromeAccount[] = "Chrome";
constexpr char kPrismService[] = "Prism Safe Storage";
constexpr char kPrismAccount[] = "Prism";

// Test hooks (see docs/chrome-import.md). All three keep the production path
// untouched when absent.
constexpr char kChromeProfileDirSwitch[] = "prism-chrome-profile-dir";
constexpr char kKeychainReadFromSwitch[] = "prism-import-keychain-read-from";
constexpr char kKeychainWriteToSwitch[] = "prism-import-keychain-write-to";

constexpr char kStagingDirName[] = "PrismChromeImport.staging";
constexpr char kStagingMarker[] = "pending";

base::FilePath ChromeProfileDir() {
  auto* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(kChromeProfileDirSwitch)) {
    return command_line->GetSwitchValuePath(kChromeProfileDirSwitch);
  }
  base::FilePath home;
  if (!base::PathService::Get(base::DIR_HOME, &home)) {
    return {};
  }
  return home.Append("Library")
      .Append("Application Support")
      .Append("Google")
      .Append("Chrome")
      .Append("Default");
}

// Opens a specific keychain file for the test-hook switches. The null ref
// means "the default keychain search list" (production behavior).
base::apple::ScopedCFTypeRef<SecKeychainRef> OpenKeychainForSwitch(
    const char* switch_name) {
  auto* command_line = base::CommandLine::ForCurrentProcess();
  if (!command_line->HasSwitch(switch_name)) {
    return base::apple::ScopedCFTypeRef<SecKeychainRef>();
  }
  const base::FilePath path = command_line->GetSwitchValuePath(switch_name);
  SecKeychainRef keychain = nullptr;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  // SecKeychainOpen is the only way to aim the SecItem* calls at a specific
  // keychain file; used solely for the documented test-hook switches.
  const OSStatus open_status = SecKeychainOpen(path.value().c_str(), &keychain);
#pragma clang diagnostic pop
  if (open_status != noErr) {
    return base::apple::ScopedCFTypeRef<SecKeychainRef>();
  }
  return base::apple::ScopedCFTypeRef<SecKeychainRef>(keychain);
}

struct KeychainRead {
  OSStatus status;  // noErr | errSecItemNotFound | errSecUserCanceled | ...
  std::string value;
};

KeychainRead ReadGenericPassword(SecKeychainRef keychain,
                                 const char* service,
                                 const char* account) {
  NSMutableDictionary* query = [@{
    (__bridge id)kSecClass : (__bridge id)kSecClassGenericPassword,
    (__bridge id)kSecAttrService : @(service),
    (__bridge id)kSecAttrAccount : @(account),
    (__bridge id)kSecReturnData : @YES,
    (__bridge id)kSecMatchLimit : (__bridge id)kSecMatchLimitOne,
  } mutableCopy];
  if (keychain) {
    query[(__bridge id)kSecMatchSearchList] = @[ (__bridge id)keychain ];
  }
  base::apple::ScopedCFTypeRef<CFTypeRef> result;
  KeychainRead read{
      SecItemCopyMatching((__bridge CFDictionaryRef)query,
                          result.InitializeInto()),
      {}};
  if (read.status == noErr) {
    CFDataRef data = static_cast<CFDataRef>(result.get());
    read.value.assign(reinterpret_cast<const char*>(CFDataGetBytePtr(data)),
                      CFDataGetLength(data));
  }
  return read;
}

OSStatus WriteGenericPassword(SecKeychainRef keychain,
                              const char* service,
                              const char* account,
                              const std::string& value) {
  // Mirrors crypto::apple::KeychainV2::AddGenericPassword (create-or-update,
  // WhenUnlocked accessibility) so the item is indistinguishable from one
  // OSCrypt itself would create — including its creator-trusting ACL.
  NSMutableDictionary* query = [@{
    (__bridge id)kSecClass : (__bridge id)kSecClassGenericPassword,
    (__bridge id)kSecAttrService : @(service),
    (__bridge id)kSecAttrAccount : @(account),
  } mutableCopy];
  if (keychain) {
    query[(__bridge id)kSecMatchSearchList] = @[ (__bridge id)keychain ];
  }
  const OSStatus found = SecItemCopyMatching((__bridge CFDictionaryRef)query,
                                             /*result=*/nullptr);
  NSData* data = [NSData dataWithBytes:value.data() length:value.size()];
  if (found == noErr) {
    // Replace, not merge: the migration makes Chrome's seed authoritative.
    // Anything Prism encrypted before this point becomes unreadable — the
    // import targets fresh profiles and the page says so.
    if (keychain) {
      query[(__bridge id)kSecUseKeychain] = (__bridge id)keychain;
    }
    return SecItemUpdate((__bridge CFDictionaryRef)query,
                         (__bridge CFDictionaryRef)@{
                           (__bridge id)kSecValueData : data,
                         });
  }
  if (found != errSecItemNotFound) {
    return found;
  }
  if (keychain) {
    query[(__bridge id)kSecUseKeychain] = (__bridge id)keychain;
  }
  query[(__bridge id)kSecValueData] = data;
  query[(__bridge id)kSecAttrAccessible] =
      (__bridge id)kSecAttrAccessibleWhenUnlocked;
  return SecItemAdd((__bridge CFDictionaryRef)query, nullptr);
}

// Copies `name` plus its SQLite sidecars (-wal/-shm/-journal) when present.
// Returns false when the main file is missing.
bool CopyDbWithSidecars(const base::FilePath& dir,
                        const std::string& name,
                        const base::FilePath& staging_dir) {
  const base::FilePath source = dir.Append(name);
  if (!base::PathExists(source)) {
    return false;
  }
  base::CreateDirectory(staging_dir);
  bool copied = base::CopyFile(source, staging_dir.Append(name));
  for (const char* suffix : {"-wal", "-shm", "-journal"}) {
    const base::FilePath sidecar = dir.Append(name + suffix);
    if (base::PathExists(sidecar)) {
      base::CopyFile(sidecar, staging_dir.Append(name + suffix));
    }
  }
  return copied;
}

// Opens the staged copy and runs a 1-row integrity check, catching torn
// copies of a live (running-Chrome) database.
bool CheckSqliteIntegrity(const base::FilePath& db_path) {
  sql::Database db(sql::Database::Tag("PrismChromeImporter"));
  if (!db.Open(db_path)) {
    return false;
  }
  sql::Statement statement(
      db.GetUniqueStatement("PRAGMA integrity_check(1)"));
  const bool ok = statement.Step() && statement.ColumnString(0) == "ok";
  db.Close();
  return ok;
}

void SetItem(base::DictValue& items,
             const char* name,
             const std::string& status,
             const std::string& detail = "") {
  base::DictValue entry;
  entry.Set("status", status);
  if (!detail.empty()) {
    entry.Set("detail", detail);
  }
  items.Set(name, std::move(entry));
}

}  // namespace

base::FilePath ChromeSourceProfileDir() {
  return ChromeProfileDir();
}

base::DictValue StageChromeImport(const base::FilePath& dest_profile_dir) {
  base::DictValue report;
  base::DictValue items;

  const base::FilePath chrome_dir = ChromeProfileDir();
  const bool chrome_found =
      !chrome_dir.empty() && base::PathExists(chrome_dir);
  report.Set("chromeFound", chrome_found);
  if (!chrome_found) {
    report.Set("keychain", "skipped:no-chrome");
    report.Set("items", std::move(items));
    report.Set("stagedForRestart", false);
    return report;
  }

  // ---- keychain: Chrome's Safe Storage seed becomes ours ----------------
  // The SecItemCopyMatching below is the one interactive authorization point
  // of the whole flow: macOS posts "Prism wants to access confidential
  // information stored in 'Chrome Safe Storage'" — Allow / Allow Always /
  // Deny. Denial degrades gracefully to bookmarks + history (below).
  auto read_keychain = OpenKeychainForSwitch(kKeychainReadFromSwitch);
  auto write_keychain = OpenKeychainForSwitch(kKeychainWriteToSwitch);
  const KeychainRead seed =
      ReadGenericPassword(read_keychain.get(), kChromeService, kChromeAccount);
  bool keychain_copied = false;
  if (seed.status == noErr) {
    const OSStatus write_status = WriteGenericPassword(
        write_keychain.get(), kPrismService, kPrismAccount, seed.value);
    if (write_status == noErr) {
      keychain_copied = true;
      report.Set("keychain", "copied");
    } else {
      report.Set("keychain", "error:" + base::NumberToString(write_status));
    }
  } else if (seed.status == errSecUserCanceled ||
             seed.status == errSecAuthFailed) {
    report.Set("keychain", "denied");
  } else if (seed.status == errSecItemNotFound) {
    report.Set("keychain", "error:chrome-item-missing");
  } else {
    report.Set("keychain", "error:" + base::NumberToString(seed.status));
  }

  // ---- stage profile files for the next startup --------------------------
  // The running browser owns the destination files and would clobber an
  // in-place copy on shutdown, so everything lands in a staging dir that
  // ApplyStagedChromeImport() moves into place during the next startup,
  // before any profile service opens them.
  bool staged = false;
  if (keychain_copied) {
    const base::FilePath staging_dir = dest_profile_dir.DirName()
                                           .Append(kStagingDirName);
    base::DeletePathRecursively(staging_dir);  // drop any previous attempt

    // Cookies: modern Chrome keeps them under Network/, older at the root.
    // Copy both when both exist.
    bool cookies_ok = false;
    std::string cookies_detail;
    if (CopyDbWithSidecars(chrome_dir, "Cookies", staging_dir)) {
      cookies_ok = CheckSqliteIntegrity(staging_dir.Append("Cookies"));
      cookies_detail = cookies_ok ? "" : "integrity check failed (root)";
    }
    const base::FilePath network_cookies_dir =
        chrome_dir.Append("Network");
    if (base::PathExists(network_cookies_dir.Append("Cookies"))) {
      const base::FilePath staged_network = staging_dir.Append("Network");
      if (CopyDbWithSidecars(chrome_dir.Append("Network"), "Cookies",
                             staged_network)) {
        const bool network_ok =
            CheckSqliteIntegrity(staged_network.Append("Cookies"));
        cookies_ok = cookies_ok || network_ok;
        if (!network_ok) {
          cookies_detail = "integrity check failed (Network/)";
        }
      }
    }
    SetItem(items, "cookies", cookies_ok ? "ok" : "failed", cookies_detail);

    // Passwords (profile + account-scoped stores).
    bool passwords_ok = false;
    std::string passwords_detail;
    for (const char* name : {"Login Data", "Login Data For Account"}) {
      if (CopyDbWithSidecars(chrome_dir, name, staging_dir) &&
          CheckSqliteIntegrity(staging_dir.Append(name))) {
        passwords_ok = true;
      }
    }
    if (!passwords_ok && !base::PathExists(chrome_dir.Append("Login Data"))) {
      SetItem(items, "passwords", "missing");
    } else {
      SetItem(items, "passwords", passwords_ok ? "ok" : "failed",
              passwords_detail);
    }

    // Preferences + Secure Preferences. Their per-pref MACs verify because
    // pref hashing is OSCrypt-derived and we copied the seed (same machine).
    for (const auto& [name, key] :
         {std::pair{"Preferences", "preferences"},
          std::pair{"Secure Preferences", "securePreferences"}}) {
      if (CopyDbWithSidecars(chrome_dir, name, staging_dir)) {
        SetItem(items, key, "ok");
      } else {
        SetItem(items, key, "missing");
      }
    }

    // Extensions: the unpacked payloads; Secure Preferences carries their
    // settings. Loading is best-effort — store-delisted or policy-blocked
    // extensions will simply not load, and the report says so.
    const base::FilePath extensions_dir = chrome_dir.Append("Extensions");
    if (base::DirectoryExists(extensions_dir)) {
      int count = 0;
      bool copy_ok = true;
      const base::FilePath staged_extensions = staging_dir.Append("Extensions");
      base::CreateDirectory(staged_extensions);  // CopyDirectory needs parents
      base::FileEnumerator dirs(extensions_dir, false,
                                base::FileEnumerator::DIRECTORIES);
      for (auto dir = dirs.Next(); !dir.empty(); dir = dirs.Next()) {
        ++count;
        if (!base::CopyDirectory(
                dir, staged_extensions.Append(dir.BaseName()),
                /*recursive=*/true)) {
          copy_ok = false;
        }
      }
      SetItem(items, "extensions", copy_ok ? "ok" : "failed",
              base::NumberToString(count) + " copied; load is best-effort");
    } else {
      SetItem(items, "extensions", "missing");
    }

    // The marker is written last: a partial staging dir never applies.
    if (base::WriteFile(staging_dir.Append(kStagingMarker), "")) {
      staged = true;
    }
  } else {
    // Graceful degradation: without the seed the encrypted files are
    // unreadable and the pref MACs would fail, so only bookmarks + history
    // import (the welcome page does those live).
    for (const char* key :
         {"cookies", "passwords", "preferences", "securePreferences",
          "extensions"}) {
      SetItem(items, key, "skipped:keychain-denied");
    }
  }

  report.Set("items", std::move(items));
  report.Set("stagedForRestart", staged);
  return report;
}

void ApplyStagedChromeImport(const base::FilePath& user_data_dir) {
  const base::FilePath staging_dir = user_data_dir.Append(kStagingDirName);
  if (!base::PathExists(staging_dir.Append(kStagingMarker))) {
    return;
  }
  const base::FilePath dest_dir = user_data_dir.Append("Default");
  base::CreateDirectory(dest_dir);

  base::FileEnumerator files(staging_dir, /*recursive=*/true,
                             base::FileEnumerator::FILES);
  for (auto file = files.Next(); !file.empty(); file = files.Next()) {
    if (file.BaseName().value() == kStagingMarker) {
      continue;
    }
    base::FilePath relative;
    if (!staging_dir.AppendRelativePath(file, &relative)) {
      continue;
    }
    const base::FilePath target = dest_dir.Append(relative);
    base::CreateDirectory(target.DirName());
    // rename(2) replaces existing files atomically on POSIX.
    base::Move(file, target);
  }
  base::DeletePathRecursively(staging_dir);
}

}  // namespace prism
