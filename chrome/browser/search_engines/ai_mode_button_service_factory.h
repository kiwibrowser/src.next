// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SEARCH_ENGINES_AI_MODE_BUTTON_SERVICE_FACTORY_H_
#define CHROME_BROWSER_SEARCH_ENGINES_AI_MODE_BUTTON_SERVICE_FACTORY_H_

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

class Profile;
class AiModeButtonService;

namespace content {
class BrowserContext;
}

class AiModeButtonServiceFactory : public ProfileKeyedServiceFactory {
 public:
  static AiModeButtonService* GetForProfile(Profile* profile);
  static AiModeButtonServiceFactory* GetInstance();

 private:
  friend base::NoDestructor<AiModeButtonServiceFactory>;
  AiModeButtonServiceFactory();
  ~AiModeButtonServiceFactory() override;

  // BrowserContextKeyedServiceFactory:
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
};

#endif  // CHROME_BROWSER_SEARCH_ENGINES_AI_MODE_BUTTON_SERVICE_FACTORY_H_
