// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/glic_util.h"

#include "base/feature_list.h"
#include "chrome/browser/browser_process.h"
#include "components/variations/service/variations_service.h"
#include "extensions/common/extension_features.h"

namespace extensions {

bool IsApiGlicPrivateEnabled() {
  // Limit the feature to US only for launch.
  // Check GetStoredPermanentCountry() or GetLatestCountry() to keep the logic
  // in sync with `glic::GlicGlobalEnabling`.
  auto* feature_list = base::FeatureList::GetInstance();
  if (feature_list && feature_list->IsFeatureOverridden(
                          extensions_features::kApiGlicPrivate.name)) {
    return base::FeatureList::IsEnabled(extensions_features::kApiGlicPrivate);
  }
  auto* variations_service = g_browser_process->variations_service();
  return variations_service &&
         (variations_service->GetStoredPermanentCountry() == "us" ||
          variations_service->GetLatestCountry() == "us");
}

}  // namespace extensions
