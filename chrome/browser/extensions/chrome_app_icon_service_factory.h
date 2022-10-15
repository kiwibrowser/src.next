// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_CHROME_APP_ICON_SERVICE_FACTORY_H_
#define CHROME_BROWSER_EXTENSIONS_CHROME_APP_ICON_SERVICE_FACTORY_H_

#include "base/memory/singleton.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

namespace extensions {

class ChromeAppIconService;

// Factory to create ChromeAppIconService. Use helper
// ChromeAppIconService::Get(context) to access the service.
class ChromeAppIconServiceFactory : public ProfileKeyedServiceFactory {
 public:
  ChromeAppIconServiceFactory(const ChromeAppIconServiceFactory&) = delete;
  ChromeAppIconServiceFactory& operator=(const ChromeAppIconServiceFactory&) =
      delete;

  static ChromeAppIconService* GetForBrowserContext(
      content::BrowserContext* context);

  static ChromeAppIconServiceFactory* GetInstance();

 private:
  friend struct base::DefaultSingletonTraits<ChromeAppIconServiceFactory>;

  ChromeAppIconServiceFactory();
  ~ChromeAppIconServiceFactory() override;

  KeyedService* BuildServiceInstanceFor(
      content::BrowserContext* context) const override;
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_CHROME_APP_ICON_SERVICE_FACTORY_H_
