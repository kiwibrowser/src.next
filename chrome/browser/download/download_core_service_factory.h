// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_DOWNLOAD_DOWNLOAD_CORE_SERVICE_FACTORY_H_
#define CHROME_BROWSER_DOWNLOAD_DOWNLOAD_CORE_SERVICE_FACTORY_H_

#include "base/memory/singleton.h"
#include "components/keyed_service/content/browser_context_keyed_service_factory.h"

class DownloadCoreService;

// Singleton that owns all DownloadCoreServices and associates them with
// Profiles. Listens for the Profile's destruction notification and cleans up
// the associated DownloadCoreService.
class DownloadCoreServiceFactory : public BrowserContextKeyedServiceFactory {
 public:
  // Returns the DownloadCoreService for |context|, creating if not yet created.
  static DownloadCoreService* GetForBrowserContext(
      content::BrowserContext* context);

  static DownloadCoreServiceFactory* GetInstance();

 protected:
  // BrowserContextKeyedServiceFactory:
  KeyedService* BuildServiceInstanceFor(
      content::BrowserContext* profile) const override;
  content::BrowserContext* GetBrowserContextToUse(
      content::BrowserContext* context) const override;

 private:
  friend struct base::DefaultSingletonTraits<DownloadCoreServiceFactory>;

  DownloadCoreServiceFactory();
  ~DownloadCoreServiceFactory() override;
};

#endif  // CHROME_BROWSER_DOWNLOAD_DOWNLOAD_CORE_SERVICE_FACTORY_H_
