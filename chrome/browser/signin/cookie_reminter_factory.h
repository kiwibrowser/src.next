// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SIGNIN_COOKIE_REMINTER_FACTORY_H_
#define CHROME_BROWSER_SIGNIN_COOKIE_REMINTER_FACTORY_H_

#include "base/memory/singleton.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

class CookieReminter;
class Profile;

class CookieReminterFactory : public ProfileKeyedServiceFactory {
 public:
  static CookieReminter* GetForProfile(Profile* profile);
  static CookieReminterFactory* GetInstance();

 private:
  friend struct base::DefaultSingletonTraits<CookieReminterFactory>;

  CookieReminterFactory();
  ~CookieReminterFactory() override;

  // BrowserContextKeyedServiceFactory:
  KeyedService* BuildServiceInstanceFor(
      content::BrowserContext* profile) const override;
};

#endif  // CHROME_BROWSER_SIGNIN_COOKIE_REMINTER_FACTORY_H_
