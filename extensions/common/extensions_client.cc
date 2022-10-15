// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/common/extensions_client.h"

#include "base/check.h"
#include "base/metrics/histogram_macros.h"
#include "base/notreached.h"
#include "base/timer/elapsed_timer.h"
#include "extensions/common/extension_icon_set.h"
#include "extensions/common/extensions_api_provider.h"
#include "extensions/common/features/feature_provider.h"
#include "extensions/common/features/json_feature_provider_source.h"
#include "extensions/common/manifest_handler.h"
#include "extensions/common/manifest_handlers/icons_handler.h"
#include "extensions/common/permissions/permissions_info.h"

namespace extensions {

namespace {

ExtensionsClient* g_client = nullptr;

}  // namespace

ExtensionsClient* ExtensionsClient::Get() {
  DCHECK(g_client);
  return g_client;
}

void ExtensionsClient::Set(ExtensionsClient* client) {
  // This can happen in unit tests, where the utility thread runs in-process.
  if (g_client)
    return;
  g_client = client;
  g_client->DoInitialize();
}

ExtensionsClient::ExtensionsClient() = default;
ExtensionsClient::~ExtensionsClient() = default;

std::unique_ptr<FeatureProvider> ExtensionsClient::CreateFeatureProvider(
    const std::string& name) const {
  auto feature_provider = std::make_unique<FeatureProvider>();
  using ProviderMethod = void (ExtensionsAPIProvider::*)(FeatureProvider*);
  ProviderMethod method = nullptr;
  if (name == "api") {
    method = &ExtensionsAPIProvider::AddAPIFeatures;
  } else if (name == "manifest") {
    method = &ExtensionsAPIProvider::AddManifestFeatures;
  } else if (name == "permission") {
    method = &ExtensionsAPIProvider::AddPermissionFeatures;
  } else if (name == "behavior") {
    method = &ExtensionsAPIProvider::AddBehaviorFeatures;
  } else {
    NOTREACHED();
  }
  for (const auto& api_provider : api_providers_)
    ((*api_provider).*method)(feature_provider.get());

  return feature_provider;
}

std::unique_ptr<JSONFeatureProviderSource>
ExtensionsClient::CreateAPIFeatureSource() const {
  auto source = std::make_unique<JSONFeatureProviderSource>("api");
  for (const auto& api_provider : api_providers_)
    api_provider->AddAPIJSONSources(source.get());
  return source;
}

bool ExtensionsClient::IsAPISchemaGenerated(const std::string& name) const {
  for (const auto& provider : api_providers_) {
    if (provider->IsAPISchemaGenerated(name))
      return true;
  }
  return false;
}

base::StringPiece ExtensionsClient::GetAPISchema(
    const std::string& name) const {
  for (const auto& provider : api_providers_) {
    base::StringPiece api = provider->GetAPISchema(name);
    if (!api.empty())
      return api;
  }
  return base::StringPiece();
}

void ExtensionsClient::AddAPIProvider(
    std::unique_ptr<ExtensionsAPIProvider> provider) {
  DCHECK(!initialize_called_)
      << "APIProviders can only be added before client initialization.";
  api_providers_.push_back(std::move(provider));
}

std::set<base::FilePath> ExtensionsClient::GetBrowserImagePaths(
    const Extension* extension) {
  std::set<base::FilePath> paths;
  IconsInfo::GetIcons(extension).GetPaths(&paths);
  return paths;
}

void ExtensionsClient::AddOriginAccessPermissions(
    const Extension& extension,
    bool is_extension_active,
    std::vector<network::mojom::CorsOriginPatternPtr>* origin_patterns) const {}

absl::optional<int> ExtensionsClient::GetExtensionExtendedErrorCode() const {
  return absl::nullopt;
}

void ExtensionsClient::DoInitialize() {
  initialize_called_ = true;

  DCHECK(!ManifestHandler::IsRegistrationFinalized());
  PermissionsInfo* permissions_info = PermissionsInfo::GetInstance();
  const base::ElapsedTimer timer;
  for (const auto& provider : api_providers_) {
    provider->RegisterManifestHandlers();
    provider->RegisterPermissions(permissions_info);
  }
  ManifestHandler::FinalizeRegistration();

  Initialize();

  UMA_HISTOGRAM_CUSTOM_MICROSECONDS_TIMES(
      "Extensions.ChromeExtensionsClientInitTime2", timer.Elapsed(),
      base::Microseconds(1), base::Seconds(10), 50);
}

}  // namespace extensions
