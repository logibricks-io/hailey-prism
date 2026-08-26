// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_PRISM_PRISM_SPACES_UI_CONSTANTS_H_
#define CHROME_BROWSER_PRISM_PRISM_SPACES_UI_CONSTANTS_H_

namespace prism {

// The spaces management page (chrome://prism-spaces). The host constant lives
// here rather than chrome/common/webui_url_constants.h to keep the upstream
// patch surface minimal.
inline constexpr char kPrismSpacesHost[] = "prism-spaces";
inline constexpr char kPrismSpacesURL[] = "chrome://prism-spaces";

}  // namespace prism

#endif  // CHROME_BROWSER_PRISM_PRISM_SPACES_UI_CONSTANTS_H_
