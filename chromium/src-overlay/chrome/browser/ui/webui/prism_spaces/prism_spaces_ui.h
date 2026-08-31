// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_UI_WEBUI_PRISM_SPACES_PRISM_SPACES_UI_H_
#define CHROME_BROWSER_UI_WEBUI_PRISM_SPACES_PRISM_SPACES_UI_H_

#include <string>

#include "base/values.h"
#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/webui_config.h"

namespace content {
class WebContents;
}

namespace prism {

class PrismSpacesUI;

// chrome://prism-spaces — the Space management page (Phase 4).
class PrismSpacesUIConfig
    : public content::DefaultWebUIConfig<PrismSpacesUI> {
 public:
  PrismSpacesUIConfig();
  ~PrismSpacesUIConfig() override;
};

class PrismSpacesUI : public content::WebUIController {
 public:
  explicit PrismSpacesUI(content::WebUI* web_ui);
  ~PrismSpacesUI() override;

  // Prompts the wall hosted in `wc` (a spaces-mode wall) to replay the enter
  // motion (recon §8). Routed through the live controller because its
  // CallJavascriptFunctionUnsafe channel is the proven-working one.
  static void NotifyModeShown(content::WebContents* wc);

 private:
  // chrome.send("spaceAction", [id, action]) — view/takeover/handoff/close.
  void OnAction(const base::ListValue& args);
  // chrome.send("querySpaces") — pushes the spaces JSON via
  // prismSpaces.onData(...).
  void OnQuerySpaces(const base::ListValue& args);
};

}  // namespace prism

#endif  // CHROME_BROWSER_UI_WEBUI_PRISM_SPACES_PRISM_SPACES_UI_H_
