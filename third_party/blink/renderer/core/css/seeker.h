#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_CSS_SEEKER_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_CSS_SEEKER_H_

#include <iterator>
#include "base/memory/stack_allocated.h"
#include "third_party/blink/renderer/core/css/rule_set.h"

namespace blink {

// Sequentially scans a sorted list of RuleSet::Interval<T> and seeks
// for the value for a rule (given by its position). Seek() must be called
// with non-decreasing rule positions, so that we only need to go
// through the layer list at most once for all Seek() calls.
template <class T>
class Seeker {
  STACK_ALLOCATED();

 public:
  explicit Seeker(const HeapVector<RuleSet::Interval<T>>& intervals)
      : intervals_(intervals), iter_(intervals_.begin()) {}

  const T* Seek(unsigned rule_position) {
#if DCHECK_IS_ON()
    DCHECK_GE(rule_position, last_rule_position_);
    last_rule_position_ = rule_position;
#endif

    while (iter_ != intervals_.end() &&
           iter_->start_position <= rule_position) {
      ++iter_;
    }
    if (iter_ == intervals_.begin()) {
      return nullptr;
    }
    return std::prev(iter_)->value.Get();
  }

 private:
  const HeapVector<RuleSet::Interval<T>>& intervals_;
  const RuleSet::Interval<T>* iter_;
#if DCHECK_IS_ON()
  unsigned last_rule_position_ = 0;
#endif
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_CSS_SEEKER_H_
