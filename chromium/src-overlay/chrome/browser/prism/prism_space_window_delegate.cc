// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/prism/prism_space_window_delegate.h"

#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/prism/prism_spaces_ui_constants.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "prism/browser/spaces/space_manager.h"
#include "cc/paint/paint_flags.h"
#include "ui/base/base_window.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/widget/widget.h"

namespace prism {

namespace {

// A 48dp violet ring shown for a beat where the agent "clicked".
class ClickRingView : public views::View {
 public:
  ClickRingView() = default;

  void OnPaint(gfx::Canvas* canvas) override {
    const gfx::Rect bounds = GetLocalBounds();
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(SkColorSetRGB(124, 77, 255));  // prism violet
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(4.0f);
    const gfx::PointF center(bounds.CenterPoint());
    canvas->DrawCircle(center, bounds.width() / 2.0f - 4.0f, flags);
    flags.setStrokeWidth(2.0f);
    flags.setColor(SkColorSetARGB(140, 124, 77, 255));
    canvas->DrawCircle(center, bounds.width() / 2.0f - 10.0f, flags);
  }
};

}  // namespace

PrismSpaceWindowDelegate::PrismSpaceWindowDelegate() = default;
PrismSpaceWindowDelegate::~PrismSpaceWindowDelegate() = default;

BrowserWindowInterface* PrismSpaceWindowDelegate::FindSpaceWindow(
    int space_id) {
  auto it = windows_.find(space_id);
  if (it == windows_.end()) {
    return nullptr;
  }
  BrowserWindowInterface* found = nullptr;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* candidate) {
        if (candidate == it->second) {
          found = candidate;
          return false;
        }
        return true;
      });
  if (!found) {
    windows_.erase(it);  // the user closed the window
  }
  return found;
}

void PrismSpaceWindowDelegate::ShowTaskSpace(
    int space_id,
    std::vector<std::unique_ptr<content::WebContents>> windowless_tabs) {
  const auto* space = SpaceManager::GetInstance()->Find(space_id);
  if (!space) {
    return;
  }

  BrowserWindowInterface* browser = FindSpaceWindow(space_id);
  if (!browser) {
    Profile* profile = nullptr;
    if (!windowless_tabs.empty()) {
      profile = Profile::FromBrowserContext(
          windowless_tabs.front()->GetBrowserContext());
    }
    if (!profile) {
      profile = ProfileManager::GetLastUsedProfileIfLoaded();
    }
    if (!profile) {
      return;
    }
    browser = Browser::Create(Browser::CreateParams(profile, true));
    windows_[space_id] = browser;

    // Identity tab: the spaces management page, pinned, so the window is
    // recognizable at a glance and self-manageable.
    content::OpenURLParams params(GURL(kPrismSpacesURL), content::Referrer(),
                                  WindowOpenDisposition::NEW_BACKGROUND_TAB,
                                  ui::PAGE_TRANSITION_AUTO_TOPLEVEL, false);
    browser->OpenURL(params, /*navigation_handle_callback=*/{});
    browser->GetTabStripModel()->SetTabPinned(0, true);
  }

  for (auto& tab : windowless_tabs) {
    browser->GetTabStripModel()->AppendWebContents(std::move(tab),
                                                   /*foreground=*/false);
  }
  browser->GetWindow()->Show();
  browser->GetWindow()->Activate();
}

void PrismSpaceWindowDelegate::AnimateClickHighlight(int space_id,
                                                     int x,
                                                     int y) {
  BrowserWindowInterface* browser = FindSpaceWindow(space_id);
  if (!browser) {
    return;  // space not shown: highlight is a no-op (documented v1)
  }
  content::WebContents* active =
      browser->GetTabStripModel()->GetActiveWebContents();
  if (!active) {
    return;
  }
  // Page coordinates -> screen: the contents container origin plus the page
  // offset (scroll offset is not folded in; documented approximation).
  const gfx::Rect container = active->GetContainerBounds();
  const gfx::Point center(container.x() + x, container.y() + y);

  views::Widget::InitParams params(
      views::Widget::InitParams::Ownership::NATIVE_WIDGET_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_WINDOW_FRAMELESS);
  params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
  params.bounds = gfx::Rect(center.x() - 24, center.y() - 24, 48, 48);
  params.z_order = ui::ZOrderLevel::kFloatingWindow;
  auto* widget = new views::Widget();
  widget->Init(std::move(params));
  widget->SetContentsView(std::make_unique<ClickRingView>());
  widget->Show();
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce([](base::WeakPtr<views::Widget> w) {
        if (w) {
          w->Close();
        }
      }, widget->GetWeakPtr()),
      base::Milliseconds(450));
}

PrismSpaceWindowDelegate* GetPrismSpaceWindowDelegate() {
  // Lives for the process; the framework never unloads.
  static base::NoDestructor<PrismSpaceWindowDelegate> delegate;
  return delegate.get();
}

}  // namespace prism
