// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_PRISM_PRISM_ONBOARDING_ACTIONS_H_
#define CHROME_BROWSER_PRISM_PRISM_ONBOARDING_ACTIONS_H_

class PrefRegistrySimple;

namespace prism {

// Local State pref: set when the first-run onboarding flow is completed (any
// exit from the last step). Registered from RegisterLocalStatePrefs below.
inline constexpr char kOnboardingCompletedPref[] = "prism.onboarding_completed";

// Registers Prism's Local State prefs; called from
// chrome/browser/prefs/browser_prefs.cc RegisterLocalState.
void RegisterLocalStatePrefs(PrefRegistrySimple* registry);

// true once the first-run onboarding flow has been completed.
bool OnboardingCompleted();

// Marks onboarding completed (idempotent).
void MarkOnboardingCompleted();

// Applies the finish-step choices. Each is a no-op when its flag is false.
void SetPrismAsDefaultBrowser();
bool AddPrismToDock();           // false if the add failed
void SetCrashReportsEnabled(bool enabled);

}  // namespace prism

#endif  // CHROME_BROWSER_PRISM_PRISM_ONBOARDING_ACTIONS_H_
