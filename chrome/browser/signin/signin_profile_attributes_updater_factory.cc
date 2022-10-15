// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/signin/signin_profile_attributes_updater_factory.h"

#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/signin/identity_manager_factory.h"
#include "chrome/browser/signin/signin_profile_attributes_updater.h"

// static
SigninProfileAttributesUpdater*
SigninProfileAttributesUpdaterFactory::GetForProfile(Profile* profile) {
  return static_cast<SigninProfileAttributesUpdater*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

// static
SigninProfileAttributesUpdaterFactory*
SigninProfileAttributesUpdaterFactory::GetInstance() {
  return base::Singleton<SigninProfileAttributesUpdaterFactory>::get();
}

SigninProfileAttributesUpdaterFactory::SigninProfileAttributesUpdaterFactory()
    : ProfileKeyedServiceFactory("SigninProfileAttributesUpdater") {
  DependsOn(IdentityManagerFactory::GetInstance());
}

SigninProfileAttributesUpdaterFactory::
    ~SigninProfileAttributesUpdaterFactory() {}

KeyedService* SigninProfileAttributesUpdaterFactory::BuildServiceInstanceFor(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);

  // Some tests don't have a ProfileManager, disable this service.
  if (!g_browser_process->profile_manager()) {
    return nullptr;
  }

  return new SigninProfileAttributesUpdater(
      IdentityManagerFactory::GetForProfile(profile),
      &g_browser_process->profile_manager()->GetProfileAttributesStorage(),
      profile->GetPath(), profile->GetPrefs());
}

bool SigninProfileAttributesUpdaterFactory::ServiceIsCreatedWithBrowserContext()
    const {
  return true;
}
