// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/menu_manager_factory.h"

#include "base/memory/ptr_util.h"
#include "chrome/browser/extensions/menu_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/extension_system_provider.h"
#include "extensions/browser/extensions_browser_client.h"

namespace extensions {

// static
MenuManager* MenuManagerFactory::GetForBrowserContext(
    content::BrowserContext* context) {
  return static_cast<MenuManager*>(
      GetInstance()->GetServiceForBrowserContext(context, true));
}

// static
MenuManagerFactory* MenuManagerFactory::GetInstance() {
  return base::Singleton<MenuManagerFactory>::get();
}

// static
std::unique_ptr<KeyedService>
MenuManagerFactory::BuildServiceInstanceForTesting(
    content::BrowserContext* context) {
  return base::WrapUnique(GetInstance()->BuildServiceInstanceFor(context));
}

MenuManagerFactory::MenuManagerFactory()
    : BrowserContextKeyedServiceFactory(
        "MenuManager",
        BrowserContextDependencyManager::GetInstance()) {
  DependsOn(ExtensionsBrowserClient::Get()->GetExtensionSystemFactory());
}

MenuManagerFactory::~MenuManagerFactory() {}

KeyedService* MenuManagerFactory::BuildServiceInstanceFor(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);
  return new MenuManager(profile, ExtensionSystem::Get(profile)->state_store());
}

content::BrowserContext* MenuManagerFactory::GetBrowserContextToUse(
    content::BrowserContext* context) const {
  return ExtensionsBrowserClient::Get()->GetOriginalContext(context);
}

bool MenuManagerFactory::ServiceIsCreatedWithBrowserContext() const {
  return true;
}

bool MenuManagerFactory::ServiceIsNULLWhileTesting() const {
  return true;
}

}  // namespace extensions
