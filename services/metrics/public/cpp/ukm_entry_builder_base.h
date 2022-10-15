// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_METRICS_PUBLIC_CPP_UKM_ENTRY_BUILDER_BASE_H_
#define SERVICES_METRICS_PUBLIC_CPP_UKM_ENTRY_BUILDER_BASE_H_

#include "services/metrics/public/cpp/metrics_export.h"
#include "services/metrics/public/cpp/ukm_recorder.h"
#include "services/metrics/public/cpp/ukm_source_id.h"
#include "services/metrics/public/mojom/ukm_interface.mojom.h"

namespace ukm {

namespace internal {

// A base class for generated UkmEntry builder objects.
// This class should not be used directly.
class METRICS_EXPORT UkmEntryBuilderBase {
 public:
  UkmEntryBuilderBase(const UkmEntryBuilderBase&) = delete;
  UkmEntryBuilderBase& operator=(const UkmEntryBuilderBase&) = delete;

  virtual ~UkmEntryBuilderBase();

  // Records the complete entry into the recorder. If recorder is null, the
  // entry is simply discarded. The |entry_| is used up by this call so
  // further calls to this or TakeEntry() will do nothing.
  void Record(UkmRecorder* recorder);

  // Extracts the created UkmEntryPtr. Record() cannot be called after this.
  mojom::UkmEntryPtr TakeEntry();

 protected:
  UkmEntryBuilderBase(ukm::SourceIdObj source_id, uint64_t event_hash);
  // TODO(crbug/873866): Remove this version once callers are migrated.
  UkmEntryBuilderBase(SourceId source_id, uint64_t event_hash);

  // Add metric to the entry. A metric contains a metric hash and value.
  void SetMetricInternal(uint64_t metric_hash, int64_t value);

 private:
  mojom::UkmEntryPtr entry_;
};

}  // namespace internal

}  // namespace ukm

#endif  // SERVICES_METRICS_PUBLIC_CPP_UKM_ENTRY_BUILDER_BASE_H_
