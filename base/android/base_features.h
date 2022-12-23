// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_ANDROID_BASE_FEATURES_H_
#define BASE_ANDROID_BASE_FEATURES_H_

#include "base/feature_list.h"

namespace base::android::features {

// All features in alphabetical order. The features should be documented
// alongside the definition of their values in the .cc file.

// Alphabetical:
extern const base::Feature kCrashBrowserOnChildMismatchIfBrowserChanged;
extern const base::Feature kCrashBrowserOnAnyChildMismatch;

}  // namespace base::android::features

#endif  // BASE_ANDROID_BASE_FEATURES_H_
