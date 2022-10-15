// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_INSTALL_VERIFIER_FACTORY_H_
#define CHROME_BROWSER_EXTENSIONS_INSTALL_VERIFIER_FACTORY_H_

#include "base/memory/singleton.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

namespace extensions {

class InstallVerifier;

class InstallVerifierFactory : public ProfileKeyedServiceFactory {
 public:
  InstallVerifierFactory(const InstallVerifierFactory&) = delete;
  InstallVerifierFactory& operator=(const InstallVerifierFactory&) = delete;

  static InstallVerifier* GetForBrowserContext(
      content::BrowserContext* context);
  static InstallVerifierFactory* GetInstance();

 private:
  friend struct base::DefaultSingletonTraits<InstallVerifierFactory>;

  InstallVerifierFactory();
  ~InstallVerifierFactory() override;

  // BrowserContextKeyedServiceFactory implementation
  KeyedService* BuildServiceInstanceFor(
      content::BrowserContext* context) const override;
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_INSTALL_VERIFIER_FACTORY_H_
