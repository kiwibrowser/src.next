// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_AUTOCOMPLETE_IN_MEMORY_URL_INDEX_FACTORY_H_
#define CHROME_BROWSER_AUTOCOMPLETE_IN_MEMORY_URL_INDEX_FACTORY_H_

#include "components/keyed_service/content/browser_context_keyed_service_factory.h"

namespace base {
template <typename T> struct DefaultSingletonTraits;
}

class InMemoryURLIndex;
class Profile;

class InMemoryURLIndexFactory : public BrowserContextKeyedServiceFactory {
 public:
  static InMemoryURLIndex* GetForProfile(Profile* profile);
  static InMemoryURLIndexFactory* GetInstance();

 private:
  friend struct base::DefaultSingletonTraits<InMemoryURLIndexFactory>;

  InMemoryURLIndexFactory();
  ~InMemoryURLIndexFactory() override;

  // BrowserContextKeyedServiceFactory:
  KeyedService* BuildServiceInstanceFor(
      content::BrowserContext* context) const override;
  content::BrowserContext* GetBrowserContextToUse(
      content::BrowserContext* context) const override;
  bool ServiceIsNULLWhileTesting() const override;
};

#endif  // CHROME_BROWSER_AUTOCOMPLETE_IN_MEMORY_URL_INDEX_FACTORY_H_
