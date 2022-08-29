// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/install_verifier_factory.h"

#include "chrome/browser/extensions/install_verifier.h"
#include "chrome/browser/profiles/profile.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_prefs_factory.h"
#include "extensions/browser/extension_registry_factory.h"
#include "extensions/browser/extensions_browser_client.h"

using content::BrowserContext;

namespace extensions {

// static
InstallVerifier* InstallVerifierFactory::GetForBrowserContext(
    BrowserContext* context) {
  return static_cast<InstallVerifier*>(
      GetInstance()->GetServiceForBrowserContext(context, true));
}

// static
InstallVerifierFactory* InstallVerifierFactory::GetInstance() {
  return base::Singleton<InstallVerifierFactory>::get();
}

InstallVerifierFactory::InstallVerifierFactory()
    : BrowserContextKeyedServiceFactory(
          "InstallVerifier",
          BrowserContextDependencyManager::GetInstance()) {
  DependsOn(ExtensionPrefsFactory::GetInstance());
  DependsOn(ExtensionRegistryFactory::GetInstance());
}

InstallVerifierFactory::~InstallVerifierFactory() {
}

KeyedService* InstallVerifierFactory::BuildServiceInstanceFor(
    BrowserContext* context) const {
  return new InstallVerifier(ExtensionPrefs::Get(context), context);
}

BrowserContext* InstallVerifierFactory::GetBrowserContextToUse(
    BrowserContext* context) const {
  // Redirected in incognito.
  return ExtensionsBrowserClient::Get()->GetOriginalContext(context);
}

}  // namespace extensions
