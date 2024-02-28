// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_CSS_SELECTOR_STATISTICS_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_CSS_SELECTOR_STATISTICS_H_

#include "base/time/time.h"
#include "third_party/blink/renderer/platform/heap/collection_support/heap_vector.h"

namespace blink {

class RuleData;

struct RulePerfDataPerRequest {
  RulePerfDataPerRequest(const RuleData* r, bool f, bool m, base::TimeDelta e)
      : rule(r), fast_reject(f), did_match(m), elapsed(e) {}
  // RuleData is Traceable but not owned here, so there's no need to Trace it
  // here. The RuleData is owned and traced by HeapVectors in RuleSet.
  const RuleData* const rule;
  bool fast_reject;
  bool did_match;
  base::TimeDelta elapsed;

  DISALLOW_NEW();
};

// For a given pass to collect matching rules against a single element (i.e.
// `MatchRequest`, this class will gather information about how a rule's
// selector matched (or didn't) along with the elapsed time. These values are
// then aggregated per-rule, outside of the lifetime of this class.
// For performance reasons (the loop where the tracing is logged is very hot and
// we don't want to affect performance of the non-tracing path), a single
// instance of SelectorStatisticsCollector should be used and
// BeginCollectionForRule/EndCollectionForCurrentRule must be called for each
// rule.
class SelectorStatisticsCollector {
  STACK_ALLOCATED();

 public:
  void ReserveCapacity(wtf_size_t size);

  // NOTE: The rule must live for at least as long as the
  // SelectorStatisticsCollector, as it is returned back in
  // PerRuleStatistics. This is fine, because we throw away
  // the statistics set at the end of CollectMatchingRulesForList
  // to do our aggregation (on selectors), and in that time,
  // we do not modify the rule buckets.
  void BeginCollectionForRule(const RuleData* rule);
  void EndCollectionForCurrentRule();

  void SetWasFastRejected() { fast_reject_ = true; }
  void SetDidMatch() { did_match_ = true; }

  const Vector<RulePerfDataPerRequest>& PerRuleStatistics() const {
    return per_rule_statistics_;
  }

 private:
  // `Vector` is more beneficial here since `RulePerfDataPerRequest` is
  // non-traceable and `SelectorStatisticsCollector` is stack allocated.
  // `HeapVector` could also be used but will be less performant in this case.
  Vector<RulePerfDataPerRequest> per_rule_statistics_;
  // The below values are for the selector currently being matched. These values
  // are pushed into `per_rule_statistics_` when `EndCollectionForCurrentRule`
  // is called.
  const RuleData* rule_{nullptr};
  base::TimeTicks start_;
  bool fast_reject_{false};
  bool did_match_{false};
};

}  // namespace blink

WTF_ALLOW_CLEAR_UNUSED_SLOTS_WITH_MEM_FUNCTIONS(blink::RulePerfDataPerRequest)

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_CSS_SELECTOR_STATISTICS_H_
