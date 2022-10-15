// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/browser/content_verifier_io_data.h"

#include <utility>

#include "content/public/browser/browser_thread.h"

namespace extensions {

ContentVerifierIOData::ExtensionData::ExtensionData(
    std::unique_ptr<std::set<CanonicalRelativePath>>
        canonical_browser_image_paths,
    std::unique_ptr<std::set<CanonicalRelativePath>>
        canonical_background_or_content_paths,
    std::unique_ptr<std::set<CanonicalRelativePath>>
        canonical_indexed_ruleset_paths,
    const base::Version& version,
    ContentVerifierDelegate::VerifierSourceType source_type)
    : canonical_browser_image_paths(std::move(canonical_browser_image_paths)),
      canonical_background_or_content_paths(
          std::move(canonical_background_or_content_paths)),
      canonical_indexed_ruleset_paths(
          std::move(canonical_indexed_ruleset_paths)),
      version(version),
      source_type(source_type) {}

ContentVerifierIOData::ContentVerifierIOData() {
}

ContentVerifierIOData::ExtensionData::~ExtensionData() {
}

ContentVerifierIOData::~ContentVerifierIOData() {
}

void ContentVerifierIOData::AddData(const std::string& extension_id,
                                    std::unique_ptr<ExtensionData> data) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  CHECK(data->canonical_browser_image_paths.get());
  data_map_[extension_id] = std::move(data);
}

void ContentVerifierIOData::RemoveData(const std::string& extension_id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  data_map_.erase(extension_id);
}

void ContentVerifierIOData::Clear() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  data_map_.clear();
}

const ContentVerifierIOData::ExtensionData* ContentVerifierIOData::GetData(
    const std::string& extension_id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  auto found = data_map_.find(extension_id);
  if (found != data_map_.end())
    return found->second.get();
  else
    return nullptr;
}

}  // namespace extensions
