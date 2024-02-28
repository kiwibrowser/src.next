// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/signin/signin_features.h"

#include "base/feature_list.h"
#include "base/metrics/field_trial_params.h"

#if !BUILDFLAG(IS_CHROMEOS_ASH) && !BUILDFLAG(IS_ANDROID)
// Enables the new style, "For You" First Run Experience
BASE_FEATURE(kForYouFre, "ForYouFre", base::FEATURE_ENABLED_BY_DEFAULT);

#if BUILDFLAG(ENABLE_DICE_SUPPORT)
// Whether the browser should be opened when the user closes the FRE window. If
// false, we just exit Chrome and the user will get straight to the browser on
// the next process launch.
const base::FeatureParam<bool> kForYouFreCloseShouldProceed{
    &kForYouFre, /*name=*/"close_should_proceed", /*default_value=*/true};

constexpr base::FeatureParam<SigninPromoVariant>::Option
    kSignInPromoVariantOptions[] = {
        {SigninPromoVariant::kSignIn, "sign-in"},
        {SigninPromoVariant::kDoMore, "do-more"},
        {SigninPromoVariant::kMakeYourOwn, "make-your-own"},
};

// Indicates the combination of strings to use on the sign-in promo page.
const base::FeatureParam<SigninPromoVariant> kForYouFreSignInPromoVariant{
    &kForYouFre, /*name=*/"signin_promo_variant",
    /*default_value=*/SigninPromoVariant::kSignIn,
    /*options=*/&kSignInPromoVariantOptions};

constexpr base::FeatureParam<WithDefaultBrowserStep>::Option
    kWithDefaultBrowserStepOptions[] = {
        {WithDefaultBrowserStep::kYes, "yes"},
        {WithDefaultBrowserStep::kNo, "no"},
        {WithDefaultBrowserStep::kForced, "forced"},
};

const base::FeatureParam<WithDefaultBrowserStep>
    kForYouFreWithDefaultBrowserStep{
        &kForYouFre, /*name=*/"with_default_browser_step",
        /*default_value=*/WithDefaultBrowserStep::kYes,
        /*options=*/&kWithDefaultBrowserStepOptions};

constexpr base::FeatureParam<DefaultBrowserVariant>::Option
    kDefaultBrowserVariantOptions[] = {
        {DefaultBrowserVariant::kCurrent, "current"},
        {DefaultBrowserVariant::kNew, "new"},
};

const base::FeatureParam<DefaultBrowserVariant> kForYouFreDefaultBrowserVariant{
    &kForYouFre, /*name=*/"default_browser_variant",
    /*default_value=*/DefaultBrowserVariant::kNew,
    /*options=*/&kDefaultBrowserVariantOptions};

// Feature that indicates that we should put the client in a study group
// (provided through `kForYouFreStudyGroup`) to be able to look at metrics in
// the long term. Does not affect the client's behavior by itself, instead this
// is done through the `kForYouFre` feature.
BASE_FEATURE(kForYouFreSyntheticTrialRegistration,
             "ForYouFreSyntheticTrialRegistration",
             base::FEATURE_DISABLED_BY_DEFAULT);

// String that refers to the study group in which this install was enrolled.
// Used to implement the sticky experiment tracking. If the value is an empty
// string, we don't register the client.
const base::FeatureParam<std::string> kForYouFreStudyGroup{
    &kForYouFreSyntheticTrialRegistration, /*name=*/"group_name",
    /*default_value=*/""};
#endif  // BUILDFLAG(ENABLE_DICE_SUPPORT)
#endif  // !BUILDFLAG(IS_CHROMEOS_ASH) && !BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_CHROMEOS_ASH)
// Enables the generation of pseudo-stable per-user per-device device
// identifiers. This identifier can be reset by the user by powerwashing the
// device.
BASE_FEATURE(kStableDeviceId,
             "StableDeviceId",
             base::FEATURE_DISABLED_BY_DEFAULT);
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

// Enables the client-side processing of the HTTP response header
// Google-Accounts-RemoveLocalAccount.
BASE_FEATURE(kProcessGaiaRemoveLocalAccountHeader,
             "ProcessGaiaRemoveLocalAccountHeader",
             base::FEATURE_ENABLED_BY_DEFAULT);

// Enables showing the enterprise dialog after every signin into a managed
// account.
BASE_FEATURE(kShowEnterpriseDialogForAllManagedAccountsSignin,
             "ShowEnterpriseDialogForAllManagedAccountsSignin",
             base::FEATURE_DISABLED_BY_DEFAULT);

// Disables signout for enteprise managed profiles
BASE_FEATURE(kDisallowManagedProfileSignout,
             "DisallowManagedProfileSignout",
             base::FEATURE_ENABLED_BY_DEFAULT);

#if BUILDFLAG(ENABLE_MIRROR)
BASE_FEATURE(kVerifyRequestInitiatorForMirrorHeaders,
             "VerifyRequestInitiatorForMirrorHeaders",
             base::FEATURE_ENABLED_BY_DEFAULT);
#endif  // BUILDFLAG(ENABLE_MIRROR)

BASE_FEATURE(kProfilesReordering,
             "ProfilesReordering",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kForceSigninFlowInProfilePicker,
             "ForceSigninFlowInProfilePicker",
             base::FEATURE_DISABLED_BY_DEFAULT);
extern const base::FeatureParam<bool>
    kForceSigninReauthInProfilePickerUseAddSession{
        &kForceSigninFlowInProfilePicker, /*name=*/"reauth_use_add_session",
        /*default_value=*/false};
