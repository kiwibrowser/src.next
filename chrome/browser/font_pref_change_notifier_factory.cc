// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/font_pref_change_notifier_factory.h"

#include "chrome/browser/font_pref_change_notifier.h"
#include "chrome/browser/profiles/incognito_helpers.h"
#include "chrome/browser/profiles/profile.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"

FontPrefChangeNotifierFactory::FontPrefChangeNotifierFactory()
    : BrowserContextKeyedServiceFactory(
          "FontPrefChangeNotifier",
          BrowserContextDependencyManager::GetInstance()) {}

FontPrefChangeNotifierFactory::~FontPrefChangeNotifierFactory() = default;

// static
FontPrefChangeNotifier* FontPrefChangeNotifierFactory::GetForProfile(
    Profile* profile) {
  return static_cast<FontPrefChangeNotifier*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

// static
FontPrefChangeNotifierFactory* FontPrefChangeNotifierFactory::GetInstance() {
  return base::Singleton<FontPrefChangeNotifierFactory>::get();
}

KeyedService* FontPrefChangeNotifierFactory::BuildServiceInstanceFor(
    content::BrowserContext* context) const {
  return new FontPrefChangeNotifier(
      Profile::FromBrowserContext(context)->GetPrefs());
}

content::BrowserContext* FontPrefChangeNotifierFactory::GetBrowserContextToUse(
    content::BrowserContext* context) const {
  return chrome::GetBrowserContextRedirectedInIncognito(context);
}
