// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/signin/signin_error_controller_factory.h"

#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/signin/account_consistency_mode_manager.h"
#include "chrome/browser/signin/identity_manager_factory.h"

SigninErrorControllerFactory::SigninErrorControllerFactory()
    : ProfileKeyedServiceFactory("SigninErrorController") {
  DependsOn(IdentityManagerFactory::GetInstance());
}

SigninErrorControllerFactory::~SigninErrorControllerFactory() {}

// static
SigninErrorController* SigninErrorControllerFactory::GetForProfile(
    Profile* profile) {
  return static_cast<SigninErrorController*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

// static
SigninErrorControllerFactory* SigninErrorControllerFactory::GetInstance() {
  return base::Singleton<SigninErrorControllerFactory>::get();
}

KeyedService* SigninErrorControllerFactory::BuildServiceInstanceFor(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);
  SigninErrorController::AccountMode account_mode =
#if BUILDFLAG(IS_CHROMEOS_ASH)
      SigninErrorController::AccountMode::ANY_ACCOUNT;
#else
      AccountConsistencyModeManager::IsMirrorEnabledForProfile(profile)
          ? SigninErrorController::AccountMode::ANY_ACCOUNT
          : SigninErrorController::AccountMode::PRIMARY_ACCOUNT;
#endif
  return new SigninErrorController(
      account_mode, IdentityManagerFactory::GetForProfile(profile));
}
