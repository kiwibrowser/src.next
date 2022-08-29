// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/signin/investigator_dependency_provider.h"

InvestigatorDependencyProvider::InvestigatorDependencyProvider(Profile* profile)
    : profile_(profile) {}

InvestigatorDependencyProvider::~InvestigatorDependencyProvider() {}

PrefService* InvestigatorDependencyProvider::GetPrefs() {
  return profile_->GetPrefs();
}
