// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_UI_WEBUI_PRISM_WELCOME_PRISM_WELCOME_UI_H_
#define CHROME_BROWSER_UI_WEBUI_PRISM_WELCOME_PRISM_WELCOME_UI_H_

#include <memory>
#include <string>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/webui_config.h"

class ImporterList;

namespace prism {

class PrismWelcomeUI;

// chrome://prism-welcome — first-run onboarding (Phase 5): import browsing
// data from the system Chrome via Chromium's importer framework, or skip.
class PrismWelcomeUIConfig
    : public content::DefaultWebUIConfig<PrismWelcomeUI> {
 public:
  PrismWelcomeUIConfig();
  ~PrismWelcomeUIConfig() override;
};

class PrismWelcomeUI : public content::WebUIController {
 public:
  explicit PrismWelcomeUI(content::WebUI* web_ui);
  ~PrismWelcomeUI() override;

 private:
  // chrome.send("importFromChrome")
  void OnImportFromChrome(const base::ListValue& args);
  void OnSourceProfilesDetected();
  void ReportStatus(const std::string& status);

  std::unique_ptr<ImporterList> importer_list_;
  base::WeakPtrFactory<PrismWelcomeUI> weak_factory_{this};
};

}  // namespace prism

#endif  // CHROME_BROWSER_UI_WEBUI_PRISM_WELCOME_PRISM_WELCOME_UI_H_
