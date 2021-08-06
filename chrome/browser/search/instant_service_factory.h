// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SEARCH_INSTANT_SERVICE_FACTORY_H_
#define CHROME_BROWSER_SEARCH_INSTANT_SERVICE_FACTORY_H_

#include "base/macros.h"
#include "base/memory/singleton.h"
#include "build/build_config.h"
#include "components/keyed_service/content/browser_context_keyed_service_factory.h"

#if 0
#if defined(OS_ANDROID)
#error "Instant is only used on desktop";
#endif
#endif

class InstantService;
class Profile;

// Singleton that owns all InstantServices and associates them with Profiles.
class InstantServiceFactory : public BrowserContextKeyedServiceFactory {
 public:
  // Returns the InstantService for |profile|.
  static InstantService* GetForProfile(Profile* profile);

  static InstantServiceFactory* GetInstance();

 private:
  friend struct base::DefaultSingletonTraits<InstantServiceFactory>;

  InstantServiceFactory();
  ~InstantServiceFactory() override;

  // Overridden from BrowserContextKeyedServiceFactory:
  content::BrowserContext* GetBrowserContextToUse(
      content::BrowserContext* context) const override;
  KeyedService* BuildServiceInstanceFor(
      content::BrowserContext* context) const override;

  DISALLOW_COPY_AND_ASSIGN(InstantServiceFactory);
};

#endif  // CHROME_BROWSER_SEARCH_INSTANT_SERVICE_FACTORY_H_
