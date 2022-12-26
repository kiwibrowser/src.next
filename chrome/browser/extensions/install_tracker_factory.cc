// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/install_tracker_factory.h"

#include "base/memory/singleton.h"
#include "chrome/browser/extensions/install_tracker.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_prefs_factory.h"
#include "extensions/browser/extension_system_provider.h"
#include "extensions/browser/extensions_browser_client.h"

namespace extensions {

// static
InstallTracker* InstallTrackerFactory::GetForBrowserContext(
    content::BrowserContext* context) {
  return static_cast<InstallTracker*>(
      GetInstance()->GetServiceForBrowserContext(context, true));
}

InstallTrackerFactory* InstallTrackerFactory::GetInstance() {
  return base::Singleton<InstallTrackerFactory>::get();
}

InstallTrackerFactory::InstallTrackerFactory()
    : BrowserContextKeyedServiceFactory(
        "InstallTracker",
        BrowserContextDependencyManager::GetInstance()) {
  DependsOn(ExtensionsBrowserClient::Get()->GetExtensionSystemFactory());
  DependsOn(ExtensionPrefsFactory::GetInstance());
}

InstallTrackerFactory::~InstallTrackerFactory() {
}

KeyedService* InstallTrackerFactory::BuildServiceInstanceFor(
    content::BrowserContext* context) const {
  return new InstallTracker(context, ExtensionPrefs::Get(context));
}

content::BrowserContext* InstallTrackerFactory::GetBrowserContextToUse(
    content::BrowserContext* context) const {
  // The installs themselves are routed to the non-incognito profile and so
  // should the install progress.
  return ExtensionsBrowserClient::Get()->GetOriginalContext(context);
}

}  // namespace extensions
