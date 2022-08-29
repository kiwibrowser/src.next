// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/chrome_app_icon_service_factory.h"

#include "chrome/browser/extensions/chrome_app_icon_service.h"
#include "chrome/browser/profiles/incognito_helpers.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "extensions/browser/extension_registry_factory.h"

namespace extensions {

// static
ChromeAppIconService* ChromeAppIconServiceFactory::GetForBrowserContext(
    content::BrowserContext* context) {
  return static_cast<ChromeAppIconService*>(
      GetInstance()->GetServiceForBrowserContext(context, true));
}

// static
ChromeAppIconServiceFactory* ChromeAppIconServiceFactory::GetInstance() {
  return base::Singleton<ChromeAppIconServiceFactory>::get();
}

ChromeAppIconServiceFactory::ChromeAppIconServiceFactory()
    : BrowserContextKeyedServiceFactory(
          "ChromeAppIconService",
          BrowserContextDependencyManager::GetInstance()) {
  DependsOn(ExtensionRegistryFactory::GetInstance());
}

ChromeAppIconServiceFactory::~ChromeAppIconServiceFactory() = default;

KeyedService* ChromeAppIconServiceFactory::BuildServiceInstanceFor(
    content::BrowserContext* context) const {
  return new ChromeAppIconService(context);
}

content::BrowserContext* ChromeAppIconServiceFactory::GetBrowserContextToUse(
    content::BrowserContext* context) const {
  return chrome::GetBrowserContextRedirectedInIncognito(context);
}

}  // namespace extensions
