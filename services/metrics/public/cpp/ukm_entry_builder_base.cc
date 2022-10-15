// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/metrics/public/cpp/ukm_entry_builder_base.h"

#include <memory>

#include "services/metrics/public/cpp/ukm_source_id.h"
#include "services/metrics/public/mojom/ukm_interface.mojom.h"

namespace ukm {

namespace internal {

UkmEntryBuilderBase::UkmEntryBuilderBase(ukm::SourceId source_id,
                                         uint64_t event_hash)
    : entry_(mojom::UkmEntry::New()) {
  entry_->source_id = source_id;
  entry_->event_hash = event_hash;
}

UkmEntryBuilderBase::UkmEntryBuilderBase(ukm::SourceIdObj source_id,
                                         uint64_t event_hash)
    : entry_(mojom::UkmEntry::New()) {
  entry_->source_id = source_id.ToInt64();
  entry_->event_hash = event_hash;
}

UkmEntryBuilderBase::~UkmEntryBuilderBase() = default;

void UkmEntryBuilderBase::SetMetricInternal(uint64_t metric_hash,
                                            int64_t value) {
  entry_->metrics.emplace(metric_hash, value);
}

void UkmEntryBuilderBase::Record(UkmRecorder* recorder) {
  if (recorder)
    recorder->AddEntry(std::move(entry_));
  else
    entry_.reset();
}

mojom::UkmEntryPtr UkmEntryBuilderBase::TakeEntry() {
  return std::move(entry_);
}

}  // namespace internal

}  // namespace ukm
