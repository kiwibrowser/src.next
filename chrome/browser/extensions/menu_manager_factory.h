// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_MENU_MANAGER_FACTORY_H_
#define CHROME_BROWSER_EXTENSIONS_MENU_MANAGER_FACTORY_H_

#include <memory>

#include "base/memory/singleton.h"
#include "components/keyed_service/content/browser_context_keyed_service_factory.h"

namespace content {
class BrowserContext;
}

namespace extensions {
class MenuManager;

class MenuManagerFactory : public BrowserContextKeyedServiceFactory {
 public:
  static MenuManager* GetForBrowserContext(content::BrowserContext* context);

  static MenuManagerFactory* GetInstance();

  static std::unique_ptr<KeyedService> BuildServiceInstanceForTesting(
      content::BrowserContext* context);

 private:
  friend struct base::DefaultSingletonTraits<MenuManagerFactory>;

  MenuManagerFactory();
  ~MenuManagerFactory() override;

  KeyedService* BuildServiceInstanceFor(
      content::BrowserContext* context) const override;
  content::BrowserContext* GetBrowserContextToUse(
      content::BrowserContext* context) const override;
  bool ServiceIsCreatedWithBrowserContext() const override;
  bool ServiceIsNULLWhileTesting() const override;
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_MENU_MANAGER_FACTORY_H_
