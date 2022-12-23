// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_HISTORY_CORE_BROWSER_FEATURES_H_
#define COMPONENTS_HISTORY_CORE_BROWSER_FEATURES_H_

#include "base/feature_list.h"
#include "base/metrics/field_trial_params.h"

namespace history {

// Organic Repeatable Queries
extern const base::Feature kOrganicRepeatableQueries;
extern const base::FeatureParam<int> kMaxNumRepeatableQueries;
extern const base::FeatureParam<bool> kScaleRepeatableQueriesScores;
extern const base::FeatureParam<bool> kPrivilegeRepeatableQueries;

}  // namespace history

#endif  // COMPONENTS_HISTORY_CORE_BROWSER_FEATURES_H_
