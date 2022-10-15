// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/browser/lazy_background_task_queue_factory.h"

#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "extensions/browser/extension_host_registry.h"
#include "extensions/browser/extension_registry_factory.h"
#include "extensions/browser/extensions_browser_client.h"
#include "extensions/browser/lazy_background_task_queue.h"

using content::BrowserContext;

namespace extensions {

// static
LazyBackgroundTaskQueue* LazyBackgroundTaskQueueFactory::GetForBrowserContext(
    BrowserContext* context) {
  return static_cast<LazyBackgroundTaskQueue*>(
      GetInstance()->GetServiceForBrowserContext(context, true));
}

// static
LazyBackgroundTaskQueueFactory* LazyBackgroundTaskQueueFactory::GetInstance() {
  return base::Singleton<LazyBackgroundTaskQueueFactory>::get();
}

LazyBackgroundTaskQueueFactory::LazyBackgroundTaskQueueFactory()
    : BrowserContextKeyedServiceFactory(
          "LazyBackgroundTaskQueue",
          BrowserContextDependencyManager::GetInstance()) {
  DependsOn(ExtensionRegistryFactory::GetInstance());
  DependsOn(ExtensionHostRegistry::GetFactory());
}

LazyBackgroundTaskQueueFactory::~LazyBackgroundTaskQueueFactory() {
}

KeyedService* LazyBackgroundTaskQueueFactory::BuildServiceInstanceFor(
    BrowserContext* context) const {
  return new LazyBackgroundTaskQueue(context);
}

BrowserContext* LazyBackgroundTaskQueueFactory::GetBrowserContextToUse(
    BrowserContext* context) const {
  // Redirected in incognito.
  return ExtensionsBrowserClient::Get()->GetOriginalContext(context);
}

}  // namespace extensions
