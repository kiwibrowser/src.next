// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/warning_badge_service_factory.h"

#include "chrome/browser/extensions/warning_badge_service.h"
#include "chrome/browser/profiles/profile.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "extensions/browser/extensions_browser_client.h"
#include "extensions/browser/warning_service_factory.h"

using content::BrowserContext;

namespace extensions {

// static
WarningBadgeService* WarningBadgeServiceFactory::GetForBrowserContext(
    BrowserContext* context) {
  return static_cast<WarningBadgeService*>(
      GetInstance()->GetServiceForBrowserContext(context, true));
}

// static
WarningBadgeServiceFactory* WarningBadgeServiceFactory::GetInstance() {
  return base::Singleton<WarningBadgeServiceFactory>::get();
}

WarningBadgeServiceFactory::WarningBadgeServiceFactory()
    : BrowserContextKeyedServiceFactory(
          "WarningBadgeService",
          BrowserContextDependencyManager::GetInstance()) {
  DependsOn(WarningServiceFactory::GetInstance());
}

WarningBadgeServiceFactory::~WarningBadgeServiceFactory() {
}

KeyedService* WarningBadgeServiceFactory::BuildServiceInstanceFor(
    BrowserContext* context) const {
  return new WarningBadgeService(static_cast<Profile*>(context));
}

BrowserContext* WarningBadgeServiceFactory::GetBrowserContextToUse(
    BrowserContext* context) const {
  // Redirected in incognito.
  return ExtensionsBrowserClient::Get()->GetOriginalContext(context);
}

bool WarningBadgeServiceFactory::ServiceIsCreatedWithBrowserContext() const {
  return true;
}

}  // namespace extensions
