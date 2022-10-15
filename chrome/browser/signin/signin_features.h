// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SIGNIN_SIGNIN_FEATURES_H_
#define CHROME_BROWSER_SIGNIN_SIGNIN_FEATURES_H_

#include "base/feature_list.h"

#if BUILDFLAG(IS_ANDROID)
extern const base::Feature kEnableFamilyInfoFeedback;
#endif

#if !BUILDFLAG(IS_CHROMEOS_ASH) && !BUILDFLAG(IS_ANDROID)
extern const base::Feature kForYouFre;
#endif

extern const base::Feature kDelayConsentLevelUpgrade;

extern const base::Feature kProcessGaiaRemoveLocalAccountHeader;

extern const base::Feature kSyncPromoAfterSigninIntercept;

extern const base::Feature kSigninInterceptBubbleV2;

extern const base::Feature kShowEnterpriseDialogForAllManagedAccountsSignin;

#endif  // CHROME_BROWSER_SIGNIN_SIGNIN_FEATURES_H_
