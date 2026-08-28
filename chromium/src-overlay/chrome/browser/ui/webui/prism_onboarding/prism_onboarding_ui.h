// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_UI_WEBUI_PRISM_ONBOARDING_PRISM_ONBOARDING_UI_H_
#define CHROME_BROWSER_UI_WEBUI_PRISM_ONBOARDING_PRISM_ONBOARDING_UI_H_

#include <memory>
#include <string>

#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/webui_config.h"

class ImporterList;

namespace prism {

class PrismOnboardingUI;

// chrome://prism-onboarding — 4-step first-run flow (splash, pitch, import
// wizard, finish). Shown once via the first-run tab hook; completion is
// recorded in Local State (prism::kOnboardingCompletedPref).
class PrismOnboardingUIConfig
    : public content::DefaultWebUIConfig<PrismOnboardingUI> {
 public:
  PrismOnboardingUIConfig();
  ~PrismOnboardingUIConfig() override;
};

class PrismOnboardingUI : public content::WebUIController {
 public:
  explicit PrismOnboardingUI(content::WebUI* web_ui);
  ~PrismOnboardingUI() override;

 private:
  // chrome.send("onboardingGetState") — async; replies via
  // prismOnboarding.onState({chromeFound, chromeLabel, otherBrowsers}).
  void OnGetState(const base::ListValue& args);
  void OnSourceProfilesDetected();
  void MaybeSendState();

  // chrome.send("onboardingImport", [importChrome, otherImporterIndices])
  void OnImport(const base::ListValue& args);
  void ReportImportStatus(const std::string& status);

  // chrome.send("onboardingFinish", [setDefault, addToDock, crashReports,
  //                                  restart])
  void OnFinish(const base::ListValue& args);

  std::unique_ptr<ImporterList> importer_list_;
  bool chrome_found_ = false;
  bool state_detection_done_ = false;
  bool staged_for_restart_ = false;
  base::WeakPtrFactory<PrismOnboardingUI> weak_factory_{this};
};

}  // namespace prism

#endif  // CHROME_BROWSER_UI_WEBUI_PRISM_ONBOARDING_PRISM_ONBOARDING_UI_H_
