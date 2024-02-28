// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_HISTORY_CORE_BROWSER_FEATURES_H_
#define COMPONENTS_HISTORY_CORE_BROWSER_FEATURES_H_

#include "base/feature_list.h"
#include "base/metrics/field_trial_params.h"

namespace history {

// Organic Repeatable Queries
BASE_DECLARE_FEATURE(kOrganicRepeatableQueries);
extern const base::FeatureParam<int> kMaxNumRepeatableQueries;
extern const base::FeatureParam<bool> kScaleRepeatableQueriesScores;
extern const base::FeatureParam<bool> kPrivilegeRepeatableQueries;
extern const base::FeatureParam<bool> kRepeatableQueriesIgnoreDuplicateVisits;
extern const base::FeatureParam<int> kRepeatableQueriesMaxAgeDays;
extern const base::FeatureParam<int> kRepeatableQueriesMinVisitCount;
extern const base::FeatureParam<int> kMaxNumNewTabPageDisplays;

// When enabled, this feature flag begins populating the VisitedLinkDatabase
// with data.
BASE_DECLARE_FEATURE(kPopulateVisitedLinkDatabase);

// Synced Segments Data
// NOTE: Use `IsSyncSegmentsDataEnabled()` below to check if `kSyncSegmentsData`
// is enabled; do not check `kSyncSegmentsData` directly.
BASE_DECLARE_FEATURE(kSyncSegmentsData);

// Kill switch for use of the new SQL recovery module in `TopSitesDatabase`.
BASE_DECLARE_FEATURE(kTopSitesDatabaseUseBuiltInRecoveryIfSupported);

// Returns true when both full history sync and synced segments data are
// enabled.
bool IsSyncSegmentsDataEnabled();

}  // namespace history

#endif  // COMPONENTS_HISTORY_CORE_BROWSER_FEATURES_H_
