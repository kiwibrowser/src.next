// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/signin/dice_web_signin_interceptor_factory.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/signin/dice_web_signin_interceptor.h"
#include "chrome/browser/signin/identity_manager_factory.h"
#include "chrome/browser/ui/signin/dice_web_signin_interceptor_delegate.h"

// static
DiceWebSigninInterceptor* DiceWebSigninInterceptorFactory::GetForProfile(
    Profile* profile) {
  return static_cast<DiceWebSigninInterceptor*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

//  static
DiceWebSigninInterceptorFactory*
DiceWebSigninInterceptorFactory::GetInstance() {
  return base::Singleton<DiceWebSigninInterceptorFactory>::get();
}

DiceWebSigninInterceptorFactory::DiceWebSigninInterceptorFactory()
    : ProfileKeyedServiceFactory("DiceWebSigninInterceptor") {
  DependsOn(IdentityManagerFactory::GetInstance());
}

DiceWebSigninInterceptorFactory::~DiceWebSigninInterceptorFactory() = default;

void DiceWebSigninInterceptorFactory::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  DiceWebSigninInterceptor::RegisterProfilePrefs(registry);
}

KeyedService* DiceWebSigninInterceptorFactory::BuildServiceInstanceFor(
    content::BrowserContext* context) const {
  return new DiceWebSigninInterceptor(
      Profile::FromBrowserContext(context),
      std::make_unique<DiceWebSigninInterceptorDelegate>());
}
