// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/prism/prism_space_window_delegate.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "build/build_config.h"
#include "chrome/browser/infobars/confirm_infobar_creator.h"
#include "chrome/browser/prism/prism_dock_badge.h"
#include "chrome/browser/prism/prism_spaces_ui_constants.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/infobars/content/content_infobar_manager.h"
#include "components/infobars/core/confirm_infobar_delegate.h"
#include "components/infobars/core/infobar.h"
#include "components/infobars/core/infobar_manager.h"
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

// The "Agent is in control" banner shown on a space window's active tab
// while SpaceManager says the space is agent-owned. OK = hand off to the
// user (kAgentDelegatedToUser); Cancel = stop the agent for good (kUser —
// the agent can only re-enter by claiming the space anew).
class AgentBannerDelegate : public ConfirmInfoBarDelegate {
 public:
  AgentBannerDelegate(int space_id,
                      std::string space_name,
                      base::RepeatingCallback<void(int)> on_dismissed)
      : space_id_(space_id),
        space_name_(std::move(space_name)),
        on_dismissed_(std::move(on_dismissed)) {}

  // ConfirmInfoBarDelegate:
  InfoBarIdentifier GetIdentifier() const override {
    return PRISM_AGENT_BANNER_INFOBAR_DELEGATE;
  }
  std::u16string GetMessageText() const override {
    return base::UTF8ToUTF16(space_name_ + " \xc2\xb7 Agent is in control");
  }
  int GetButtons() const override { return BUTTON_OK | BUTTON_CANCEL; }
  std::u16string GetButtonLabel(InfoBarButton button) const override {
    return button == BUTTON_OK ? u"Take over" : u"Stop agent";
  }
  bool Accept() override {
    SpaceManager::GetInstance()->HandOff(space_id_);
    return true;
  }
  bool Cancel() override {
    SpaceManager::GetInstance()->StopAgent(space_id_);
    return true;
  }
  void InfoBarDismissed() override { on_dismissed_.Run(space_id_); }

 private:
  const int space_id_;
  const std::string space_name_;
  base::RepeatingCallback<void(int)> on_dismissed_;
};

// True while `wc` is still hosted in some browser window's tab strip — the
// liveness check used before dereferencing tracked banner pointers.
bool WebContentsHostedAnywhere(content::WebContents* wc) {
  bool hosted = false;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* candidate) {
        auto* tabs = candidate->GetTabStripModel();
        for (int i = 0; i < tabs->count(); ++i) {
          if (tabs->GetWebContentsAt(i) == wc) {
            hosted = true;
            return false;
          }
        }
        return true;
      });
  return hosted;
}

}  // namespace

PrismSpaceWindowDelegate::PrismSpaceWindowDelegate() {
  agent_surface_timer_.Start(
      FROM_HERE, base::Seconds(1),
      base::BindRepeating(&PrismSpaceWindowDelegate::SyncAgentSurfaces,
                          base::Unretained(this)));
}
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

bool PrismSpaceWindowDelegate::AppendTabToSpaceWindow(
    int space_id, std::unique_ptr<content::WebContents> tab) {
  BrowserWindowInterface* browser = FindSpaceWindow(space_id);
  if (!browser) {
    return false;
  }
  browser->GetTabStripModel()->AppendWebContents(std::move(tab),
                                                 /*foreground=*/false);
  return true;
}

void PrismSpaceWindowDelegate::OpenSpacesOverview(Browser* browser) {
  if (!browser) {
    return;
  }
  content::OpenURLParams params(GURL(kPrismSpacesURL), content::Referrer(),
                                WindowOpenDisposition::NEW_FOREGROUND_TAB,
                                ui::PAGE_TRANSITION_AUTO_BOOKMARK, false);
  browser->OpenURL(params, /*navigation_handle_callback=*/{});
}

void PrismSpaceWindowDelegate::OpenSpacesOverview(
    BrowserWindowInterface* browser) {
  if (!browser) {
    return;
  }
  content::OpenURLParams params(GURL(kPrismSpacesURL), content::Referrer(),
                                WindowOpenDisposition::NEW_FOREGROUND_TAB,
                                ui::PAGE_TRANSITION_AUTO_BOOKMARK, false);
  browser->OpenURL(params, /*navigation_handle_callback=*/{});
}

int PrismSpaceWindowDelegate::SpaceIdForWebContents(content::WebContents* wc) {
  if (!wc) {
    return 0;
  }
  for (const auto& [space_id, unused] : windows_) {
    BrowserWindowInterface* window = FindSpaceWindow(space_id);
    if (!window) {
      continue;
    }
    auto* tabs = window->GetTabStripModel();
    for (int i = 0; i < tabs->count(); ++i) {
      if (tabs->GetWebContentsAt(i) == wc) {
        return space_id;
      }
    }
  }
  return 0;
}

int PrismSpaceWindowDelegate::SpaceIdForWindow(
    const BrowserWindowInterface* window) const {
  for (const auto& [space_id, candidate] : windows_) {
    if (candidate == window) {
      return space_id;
    }
  }
  return 0;
}

BrowserWindowInterface* PrismSpaceWindowDelegate::DefaultSpaceWindow() const {
  BrowserWindowInterface* found = nullptr;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* candidate) {
        if (candidate->GetType() != BrowserWindowInterface::Type::TYPE_NORMAL) {
          return true;
        }
        if (SpaceIdForWindow(candidate) != 0) {
          return true;  // a task space's window, keep looking
        }
        found = candidate;
        return false;
      });
  return found;
}

content::WebContents* PrismSpaceWindowDelegate::ActiveTabForDefaultSpace()
    const {
  BrowserWindowInterface* window = DefaultSpaceWindow();
  return window ? window->GetTabStripModel()->GetActiveWebContents() : nullptr;
}

void PrismSpaceWindowDelegate::RegisterSpacesMode(
    BrowserWindowInterface* window,
    content::WebContents* wall_wc,
    SpacesModeExitCallback exit_cb) {
  if (!wall_wc) {
    return;
  }
  spaces_modes_[wall_wc] = {window, std::move(exit_cb), base::Time::Now()};
}

void PrismSpaceWindowDelegate::UnregisterSpacesMode(
    content::WebContents* wall_wc) {
  spaces_modes_.erase(wall_wc);
}

bool PrismSpaceWindowDelegate::IsSpacesModeWebContents(
    const content::WebContents* wc) const {
  return wc && spaces_modes_.contains(const_cast<content::WebContents*>(wc));
}

base::Time PrismSpaceWindowDelegate::SpacesModeShownAt(
    const content::WebContents* wc) const {
  auto it = spaces_modes_.find(const_cast<content::WebContents*>(wc));
  return it == spaces_modes_.end() ? base::Time() : it->second.shown_at;
}

int PrismSpaceWindowDelegate::SpaceIdForModeWebContents(
    content::WebContents* wc) {
  auto it = spaces_modes_.find(wc);
  if (it == spaces_modes_.end()) {
    return 0;
  }
  return SpaceIdForWindow(it->second.window);
}

void PrismSpaceWindowDelegate::ExitSpacesMode(
    content::WebContents* wall_wc,
    std::optional<int> open_space_id) {
  auto it = spaces_modes_.find(wall_wc);
  if (it == spaces_modes_.end()) {
    return;
  }
  // Copy out: the callback unregisters itself (restores normal chrome).
  SpacesModeExitCallback exit = it->second.exit;
  exit.Run();
  if (open_space_id.has_value()) {
    SpaceManager* manager = SpaceManager::GetInstance();
    manager->set_focused_space_id(*open_space_id);
    ShowTaskSpace(*open_space_id, {});
    manager->SetWindowShown(*open_space_id, true);
  }
}

void PrismSpaceWindowDelegate::FocusSpaceWindow(int space_id) {
  BrowserWindowInterface* window = FindSpaceWindow(space_id);
  if (!window) {
    return;  // space not currently hosted in a visible window
  }
  window->GetWindow()->Show();
  window->GetWindow()->Activate();
}

void PrismSpaceWindowDelegate::CycleToNextSpace() {
  auto* manager = SpaceManager::GetInstance();
  std::vector<int> order;
  order.push_back(0);  // the implicit default space: main browsing area
  for (const auto& space : manager->List()) {
    order.push_back(space.id);  // List() is id-ordered (std::map backed)
  }

  const int current = manager->focused_space_id();
  const auto it = std::find(order.begin(), order.end(), current);
  const size_t index =
      it == order.end() ? 0 : static_cast<size_t>(it - order.begin());
  const int next = order[(index + 1) % order.size()];
  manager->set_focused_space_id(next);

  if (next != 0) {
    if (BrowserWindowInterface* window = FindSpaceWindow(next)) {
      window->GetWindow()->Show();
      window->GetWindow()->Activate();
    } else {
      // No live window yet: open one hosting just the identity tab. Agent
      // tabs stay windowless until the agent drives showTaskSpace — the
      // chrome layer cannot reach them (per-session handler state).
      ShowTaskSpace(next, {});
      manager->SetWindowShown(next, true);
    }
    return;
  }

  // Back to the default space: raise the first window hosting no space.
  BrowserWindowInterface* target = nullptr;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* candidate) {
        for (const auto& [space_id, window] : windows_) {
          if (window == candidate) {
            return true;  // a space window; keep looking
          }
        }
        target = candidate;
        return false;
      });
  if (target) {
    target->GetWindow()->Show();
    target->GetWindow()->Activate();
  }
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

void PrismSpaceWindowDelegate::SyncAgentSurfaces() {
  auto* manager = SpaceManager::GetInstance();

  int agent_controlled = 0;
  for (const auto& space : manager->List()) {
    if (space.ownership == SpaceManager::Ownership::kAgent) {
      ++agent_controlled;
    }
  }
#if BUILDFLAG(IS_MAC)
  SetDockBadgeCount(agent_controlled);
#endif

  // Snapshot the ids: FindSpaceWindow can prune entries from windows_.
  std::vector<int> ids;
  ids.reserve(windows_.size());
  for (const auto& [id, window] : windows_) {
    ids.push_back(id);
  }

  for (const int id : ids) {
    BrowserWindowInterface* window = FindSpaceWindow(id);
    const auto* space = manager->Find(id);
    const bool agent_in_control =
        space && space->ownership == SpaceManager::Ownership::kAgent;
    if (!agent_in_control) {
      dismissed_banners_.erase(id);  // re-arm for the next agent stint
    }
    content::WebContents* active =
        window ? window->GetTabStripModel()->GetActiveWebContents() : nullptr;

    // Reconcile the tracked banner with reality (it can die with its tab or
    // by user action between ticks).
    auto tracked = banners_.find(id);
    if (tracked != banners_.end()) {
      content::WebContents* tracked_wc = tracked->second.web_contents;
      bool alive = false;
      if (tracked_wc && WebContentsHostedAnywhere(tracked_wc)) {
        const auto& bars =
            infobars::ContentInfoBarManager::FromWebContents(tracked_wc)
                ->infobars();
        alive = std::find(bars.begin(), bars.end(), tracked->second.infobar) !=
                bars.end();
      }
      if (!alive) {
        banners_.erase(tracked);
        tracked = banners_.end();
      }
    }

    if (agent_in_control && active && !dismissed_banners_.count(id) &&
        (tracked == banners_.end() || tracked->second.web_contents != active)) {
      if (tracked != banners_.end()) {
        // The active tab changed under the banner: retire the old one.
        auto* old_manager = infobars::ContentInfoBarManager::FromWebContents(
            tracked->second.web_contents);
        old_manager->RemoveInfoBar(tracked->second.infobar);
        banners_.erase(tracked);
      }
      auto* infobar_manager =
          infobars::ContentInfoBarManager::FromWebContents(active);
      infobars::InfoBar* added = infobar_manager->AddInfoBar(
          CreateConfirmInfoBar(std::make_unique<AgentBannerDelegate>(
              id, space->name,
              base::BindRepeating(
                  &PrismSpaceWindowDelegate::OnBannerDismissed,
                  base::Unretained(this)))));
      banners_[id] = {active, added};
    } else if (!agent_in_control && tracked != banners_.end()) {
      infobars::ContentInfoBarManager::FromWebContents(
          tracked->second.web_contents)
          ->RemoveInfoBar(tracked->second.infobar);
      banners_.erase(tracked);
    }
  }
}

void PrismSpaceWindowDelegate::OnBannerDismissed(int space_id) {
  dismissed_banners_.insert(space_id);
  // The manager is already tearing the bar down; just forget the pointers.
  banners_.erase(space_id);
}

PrismSpaceWindowDelegate* GetPrismSpaceWindowDelegate() {
  // Lives for the process; the framework never unloads.
  static base::NoDestructor<PrismSpaceWindowDelegate> delegate;
  return delegate.get();
}

}  // namespace prism
