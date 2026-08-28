// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/prism/prism_onboarding_actions.h"

#include "base/apple/bundle_locations.h"
#include "chrome/browser/mac/dock.h"
#include "chrome/browser/shell_integration.h"
#include "chrome/browser/browser_process.h"
#include "components/metrics/metrics_pref_names.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"

namespace prism {

void RegisterLocalStatePrefs(PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref(kOnboardingCompletedPref, false);
}

bool OnboardingCompleted() {
  return g_browser_process->local_state()->GetBoolean(kOnboardingCompletedPref);
}

void MarkOnboardingCompleted() {
  g_browser_process->local_state()->SetBoolean(kOnboardingCompletedPref, true);
  g_browser_process->local_state()->CommitPendingWrite();
}

void SetPrismAsDefaultBrowser() {
  // macOS: posts the system's interactive default-browser prompt.
  shell_integration::SetAsDefaultBrowser();
}

bool AddPrismToDock() {
  return dock::AddIcon(base::apple::OuterBundle().bundlePath, nil) !=
         dock::IconAddFailure;
}

void SetCrashReportsEnabled(bool enabled) {
  // Crash uploads on macOS ride on the metrics-reporting consent pref.
  g_browser_process->local_state()->SetBoolean(
      metrics::prefs::kMetricsReportingEnabled, enabled);
  g_browser_process->local_state()->CommitPendingWrite();
}

}  // namespace prism
