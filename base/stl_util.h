// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Derived from google3/util/gtl/stl_util.h

#ifndef BASE_STL_UTIL_H_
#define BASE_STL_UTIL_H_

#include <algorithm>
#include <forward_list>
#include <iterator>
#include <type_traits>

#include "base/check.h"
#include "base/ranges/algorithm.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace base {

namespace internal {

template <typename Iter>
constexpr bool IsRandomAccessIter =
    std::is_same<typename std::iterator_traits<Iter>::iterator_category,
                 std::random_access_iterator_tag>::value;

}  // namespace internal

// Implementation of C++23's std::to_underlying.
//
// Note: This has an additional `std::is_enum<EnumT>` requirement to be SFINAE
// friendly prior to C++20.
//
// Reference: https://en.cppreference.com/w/cpp/utility/to_underlying
template <typename EnumT, typename = std::enable_if_t<std::is_enum<EnumT>{}>>
constexpr std::underlying_type_t<EnumT> to_underlying(EnumT e) noexcept {
  return static_cast<std::underlying_type_t<EnumT>>(e);
}

// Returns a const reference to the underlying container of a container adapter.
// Works for std::priority_queue, std::queue, and std::stack.
template <class A>
const typename A::container_type& GetUnderlyingContainer(const A& adapter) {
  struct ExposedAdapter : A {
    using A::c;
  };
  return adapter.*&ExposedAdapter::c;
}

// Clears internal memory of an STL object.
// STL clear()/reserve(0) does not always free internal memory allocated
// This function uses swap/destructor to ensure the internal memory is freed.
template<class T>
void STLClearObject(T* obj) {
  T tmp;
  tmp.swap(*obj);
  // Sometimes "T tmp" allocates objects with memory (arena implementation?).
  // Hence using additional reserve(0) even if it doesn't always work.
  obj->reserve(0);
}

// Counts the number of instances of val in a container.
template <typename Container, typename T>
typename std::iterator_traits<
    typename Container::const_iterator>::difference_type
STLCount(const Container& container, const T& val) {
  return std::count(container.begin(), container.end(), val);
}

// O(1) implementation of const casting an iterator for any sequence,
// associative or unordered associative container in the STL.
//
// Reference: https://stackoverflow.com/a/10669041
template <typename Container,
          typename ConstIter,
          std::enable_if_t<!internal::IsRandomAccessIter<ConstIter>>* = nullptr>
constexpr auto ConstCastIterator(Container& c, ConstIter it) {
  return c.erase(it, it);
}

// Explicit overload for std::forward_list where erase() is named erase_after().
template <typename T, typename Allocator>
constexpr auto ConstCastIterator(
    std::forward_list<T, Allocator>& c,
    typename std::forward_list<T, Allocator>::const_iterator it) {
// The erase_after(it, it) trick used below does not work for libstdc++ [1],
// thus we need a different way.
// TODO(crbug.com/972541): Remove this workaround once libstdc++ is fixed on all
// platforms.
//
// [1] https://gcc.gnu.org/bugzilla/show_bug.cgi?id=90857
#if defined(__GLIBCXX__)
  return c.insert_after(it, {});
#else
  return c.erase_after(it, it);
#endif
}

// Specialized O(1) const casting for random access iterators. This is
// necessary, because erase() is either not available (e.g. array-like
// containers), or has O(n) complexity (e.g. std::deque or std::vector).
template <typename Container,
          typename ConstIter,
          std::enable_if_t<internal::IsRandomAccessIter<ConstIter>>* = nullptr>
constexpr auto ConstCastIterator(Container& c, ConstIter it) {
  using std::begin;
  using std::cbegin;
  return begin(c) + (it - cbegin(c));
}

// Returns a new ResultType containing the difference of two sorted containers.
template <typename ResultType, typename Arg1, typename Arg2>
ResultType STLSetDifference(const Arg1& a1, const Arg2& a2) {
  DCHECK(ranges::is_sorted(a1));
  DCHECK(ranges::is_sorted(a2));
  ResultType difference;
  std::set_difference(a1.begin(), a1.end(),
                      a2.begin(), a2.end(),
                      std::inserter(difference, difference.end()));
  return difference;
}

// Returns a new ResultType containing the union of two sorted containers.
template <typename ResultType, typename Arg1, typename Arg2>
ResultType STLSetUnion(const Arg1& a1, const Arg2& a2) {
  DCHECK(ranges::is_sorted(a1));
  DCHECK(ranges::is_sorted(a2));
  ResultType result;
  std::set_union(a1.begin(), a1.end(),
                 a2.begin(), a2.end(),
                 std::inserter(result, result.end()));
  return result;
}

// Returns a new ResultType containing the intersection of two sorted
// containers.
template <typename ResultType, typename Arg1, typename Arg2>
ResultType STLSetIntersection(const Arg1& a1, const Arg2& a2) {
  DCHECK(ranges::is_sorted(a1));
  DCHECK(ranges::is_sorted(a2));
  ResultType result;
  std::set_intersection(a1.begin(), a1.end(),
                        a2.begin(), a2.end(),
                        std::inserter(result, result.end()));
  return result;
}

// A helper class to be used as the predicate with |EraseIf| to implement
// in-place set intersection. Helps implement the algorithm of going through
// each container an element at a time, erasing elements from the first
// container if they aren't in the second container. Requires each container be
// sorted. Note that the logic below appears inverted since it is returning
// whether an element should be erased.
template <class Collection>
class IsNotIn {
 public:
  explicit IsNotIn(const Collection& collection)
      : i_(collection.begin()), end_(collection.end()) {}

  bool operator()(const typename Collection::value_type& x) {
    while (i_ != end_ && *i_ < x)
      ++i_;
    if (i_ == end_)
      return true;
    if (*i_ == x) {
      ++i_;
      return false;
    }
    return true;
  }

 private:
  typename Collection::const_iterator i_;
  const typename Collection::const_iterator end_;
};

// Helper for returning the optional value's address, or nullptr.
template <class T>
T* OptionalOrNullptr(absl::optional<T>& optional) {
  return optional.has_value() ? &optional.value() : nullptr;
}

template <class T>
const T* OptionalOrNullptr(const absl::optional<T>& optional) {
  return optional.has_value() ? &optional.value() : nullptr;
}

// Helper for creating an optional<T> from a potentially nullptr T*.
template <class T>
absl::optional<T> OptionalFromPtr(const T* value) {
  if (value)
    return absl::optional<T>(*value);
  return absl::nullopt;
}

}  // namespace base

#endif  // BASE_STL_UTIL_H_
