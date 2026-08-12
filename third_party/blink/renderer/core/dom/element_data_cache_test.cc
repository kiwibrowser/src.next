// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/dom/element_data_cache.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/renderer/core/dom/attribute.h"
#include "third_party/blink/renderer/core/dom/element_data.h"
#include "third_party/blink/renderer/core/html_names.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/heap/heap_test_utilities.h"
#include "third_party/blink/renderer/platform/heap/persistent.h"

namespace blink {

namespace {

class ElementDataCacheTest : public TestSupportingGC {};

TEST_F(ElementDataCacheTest, SentinelKeyWithFixDoesNotCauseUAF) {
  Persistent<ElementDataCache> cache = MakeGarbageCollected<ElementDataCache>();

  String tag_name = "div";
  Vector<Attribute, kAttributePrealloc> attributes;
  attributes.push_back(Attribute(html_names::kIdAttr, AtomicString("foo")));

  // Call the method with a sentinel hash. We expect it to be XOR'd.
  unsigned sentinel_hash = 0xFFFFFFFFu;

  WeakPersistent<ShareableElementData> witness;

  {
    ShareableElementData* data = cache->CachedElementDataWithHashForTesting(
        tag_name.Impl(), attributes, sentinel_hash);
    ASSERT_NE(nullptr, data);
    witness = data;
  }

  // The object should be traced and survive this GC.
  PreciselyCollectGarbage();

  ASSERT_NE(nullptr, witness.Get())
      << "ShareableElementData was swept; trace skipped the bucket despite fix";
}

}  // namespace
}  // namespace blink
