// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "ui/base/metadata/metadata_impl_macros.h"
#include "chrome/browser/ui/views/prism/prism_spaces_mode_host.h"

#include "chrome/browser/prism/prism_space_window_delegate.h"
#include "chrome/browser/prism/prism_spaces_ui_constants.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/layout/fill_layout.h"
#include "url/gurl.h"

namespace prism {

PrismSpacesModeHost::PrismSpacesModeHost(
    Profile* profile,
    BrowserWindowInterface* browser_window_interface,
    base::RepeatingClosure exit_to_chrome)
    : profile_(profile),
      browser_window_interface_(browser_window_interface),
      exit_to_chrome_(std::move(exit_to_chrome)) {
  SetLayoutManager(std::make_unique<views::FillLayout>());
  web_view_ = AddChildView(std::make_unique<views::WebView>());
}

PrismSpacesModeHost::~PrismSpacesModeHost() {
  if (registered_) {
    GetPrismSpaceWindowDelegate()->UnregisterSpacesMode(contents_.get());
  }
}

void PrismSpacesModeHost::ShowSpacesWall() {
  if (!contents_) {
    content::WebContents::CreateParams params(profile_.get());
    contents_ = content::WebContents::Create(params);
    // ?window=1: the page strips its in-page header (the native top row —
    // caption + corner trigger — replaces it) and plays the mode's enter
    // animation on visibilitychange.
    contents_->GetController().LoadURL(
        GURL(std::string(kPrismSpacesURL) + "?window=1"), content::Referrer(),
        ui::PAGE_TRANSITION_AUTO_TOPLEVEL, std::string());
    web_view_->SetWebContents(contents_.get());
  }
  if (!registered_) {
    registered_ = true;
    GetPrismSpaceWindowDelegate()->RegisterSpacesMode(
        browser_window_interface_, contents_.get(),
        prism::PrismSpaceWindowDelegate::SpacesModeExitCallback(
            exit_to_chrome_));
  }
  SetVisible(true);
  contents_->WasShown();
}

void PrismSpacesModeHost::HideSpacesWall() {
  if (registered_) {
    registered_ = false;
    GetPrismSpaceWindowDelegate()->UnregisterSpacesMode(contents_.get());
  }
  if (contents_) {
    contents_->WasHidden();
  }
  SetVisible(false);
}

BEGIN_METADATA(PrismSpacesModeHost)
END_METADATA

}  // namespace prism
