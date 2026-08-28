// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_UI_VIEWS_PRISM_PRISM_SPACES_MODE_HOST_H_
#define CHROME_BROWSER_UI_VIEWS_PRISM_PRISM_SPACES_MODE_HOST_H_

#include <memory>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/views/view.h"

class BrowserWindowInterface;
class Profile;

namespace content {
class WebContents;
}

namespace views {
class WebView;
}

namespace prism {

// The spaces dashboard presented window-wide (recon §8): owns the wall
// WebContents (chrome://prism-spaces?window=1) inside a views::WebView that
// BrowserView lays out over the whole client area while spaces mode is
// active. Registers itself with the space window delegate so the WebUI can
// resolve its "current" space and request the mode exit (the delegate runs
// `exit_to_chrome_`, which restores the normal browser chrome).
class PrismSpacesModeHost : public views::View {
  METADATA_HEADER(PrismSpacesModeHost, views::View)

 public:
  PrismSpacesModeHost(Profile* profile,
                      BrowserWindowInterface* browser_window_interface,
                      base::RepeatingClosure exit_to_chrome);
  ~PrismSpacesModeHost() override;

  // Shows the wall (lazily creates + navigates the WebContents on first use)
  // and tells the renderer it became visible (drives the page's enter
  // animation via visibilitychange). HideSpacesWall() reverses that.
  void ShowSpacesWall();
  void HideSpacesWall();

  content::WebContents* web_contents() { return contents_.get(); }

 private:
  const raw_ptr<Profile> profile_;
  const raw_ptr<BrowserWindowInterface> browser_window_interface_;
  const base::RepeatingClosure exit_to_chrome_;

  std::unique_ptr<content::WebContents> contents_;
  raw_ptr<views::WebView> web_view_ = nullptr;
  bool registered_ = false;
};

}  // namespace prism

#endif  // CHROME_BROWSER_UI_VIEWS_PRISM_PRISM_SPACES_MODE_HOST_H_
