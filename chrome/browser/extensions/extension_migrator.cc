// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_migrator.h"

#include <utility>

#include "base/values.h"
#include "chrome/browser/defaults.h"
#include "chrome/browser/extensions/external_provider_impl.h"
#include "chrome/browser/profiles/profile.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/extension_urls.h"

namespace extensions {

ExtensionMigrator::ExtensionMigrator(Profile* profile,
                                     const std::string& old_id,
                                     const std::string& new_id)
    : profile_(profile), old_id_(old_id), new_id_(new_id) {}

ExtensionMigrator::~ExtensionMigrator() {
}

void ExtensionMigrator::StartLoading() {
  auto prefs = std::make_unique<base::DictionaryValue>();

  const bool should_have_extension =
      IsAppPresent(old_id_) || IsAppPresent(new_id_);
  if (should_have_extension) {
    std::unique_ptr<base::DictionaryValue> entry(new base::DictionaryValue);
    entry->SetKey(ExternalProviderImpl::kExternalUpdateUrl,
                  base::Value(extension_urls::GetWebstoreUpdateUrl().spec()));

    prefs->SetKey(new_id_, base::Value::FromUniquePtrValue(std::move(entry)));
  }

  LoadFinished(std::move(prefs));
}

bool ExtensionMigrator::IsAppPresent(const std::string& app_id) {
  return !!ExtensionRegistry::Get(profile_)->GetInstalledExtension(app_id);
}

}  // namespace extensions
