// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/browser/browser_process_context_data.h"

#include <memory>

#include "content/public/browser/isolated_web_apps_policy.h"
#include "content/public/browser/render_process_host.h"

namespace extensions {

std::unique_ptr<ContextData> BrowserProcessContextData::Clone() const {
  return CloneProcessContextData();
}

std::unique_ptr<ProcessContextData>
BrowserProcessContextData::CloneProcessContextData() const {
  return std::make_unique<BrowserProcessContextData>(process_);
}

bool BrowserProcessContextData::IsIsolatedApplication() const {
  return content::IsolatedWebAppsPolicy::AreIsolatedWebAppsEnabled(
             process_->GetBrowserContext()) &&
         process_->GetWebExposedIsolationLevel() >=
             content::WebExposedIsolationLevel::kMaybeIsolatedApplication;
}

}  // namespace extensions
