// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_FEATURES_H_
#define BASE_FEATURES_H_

#include "base/base_export.h"
#include "base/feature_list.h"
#include "base/metrics/field_trial_params.h"

namespace base::features {

// All features in alphabetical order. The features should be documented
// alongside the definition of their values in the .cc file.

// Alphabetical:
BASE_EXPORT BASE_DECLARE_FEATURE(kEnforceNoExecutableFileHandles);

BASE_EXPORT BASE_DECLARE_FEATURE(kNotReachedIsFatal);

BASE_EXPORT BASE_DECLARE_FEATURE(kOptimizeDataUrls);

BASE_EXPORT BASE_DECLARE_FEATURE(kUseRustJsonParser);

BASE_EXPORT BASE_DECLARE_FEATURE(kJsonNegativeZero);

#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_CHROMEOS)
BASE_EXPORT BASE_DECLARE_FEATURE(kPartialLowEndModeOn3GbDevices);
BASE_EXPORT BASE_DECLARE_FEATURE(kPartialLowEndModeOnMidRangeDevices);
#endif

#if BUILDFLAG(IS_ANDROID)
BASE_EXPORT BASE_DECLARE_FEATURE(kCollectAndroidFrameTimelineMetrics);
#endif

}  // namespace base::features

#endif  // BASE_FEATURES_H_
