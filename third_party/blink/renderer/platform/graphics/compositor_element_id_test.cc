// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/platform/graphics/compositor_element_id.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace blink {

class CompositorElementIdTest : public testing::Test {};

uint64_t IdFromCompositorElementId(CompositorElementId element_id) {
  return element_id.GetInternalValue() >> kCompositorNamespaceBitCount;
}

TEST_F(CompositorElementIdTest, EncodeDecode) {
  auto element_id = CompositorElementIdFromUniqueObjectId(1);
  EXPECT_EQ(1u, IdFromCompositorElementId(element_id));
  EXPECT_EQ(CompositorElementIdNamespace::kUniqueObjectId,
            NamespaceFromCompositorElementId(element_id));

  static_assert(static_cast<uint64_t>(
                    CompositorElementIdNamespace::kUniqueObjectId) != 0);
  static_assert(static_cast<uint64_t>(CompositorElementIdNamespace::kScroll) !=
                0);
  element_id = CompositorElementIdWithNamespace(
      element_id, CompositorElementIdNamespace::kScroll);
  EXPECT_EQ(1u, IdFromCompositorElementId(element_id));
  EXPECT_EQ(CompositorElementIdNamespace::kScroll,
            NamespaceFromCompositorElementId(element_id));

  element_id = CompositorElementIdFromUniqueObjectId(
      1, CompositorElementIdNamespace::kPrimary);
  EXPECT_EQ(1u, IdFromCompositorElementId(element_id));
  EXPECT_EQ(CompositorElementIdNamespace::kPrimary,
            NamespaceFromCompositorElementId(element_id));
}

TEST_F(CompositorElementIdTest, FromDOMNodeId) {
  auto element_id = CompositorElementIdFromDOMNodeId(1);
  EXPECT_EQ(1u, IdFromCompositorElementId(element_id));
  EXPECT_EQ(CompositorElementIdNamespace::kDOMNodeId,
            NamespaceFromCompositorElementId(element_id));
}

TEST_F(CompositorElementIdTest, ToDOMNodeId) {
  auto element_id = CompositorElementIdFromUniqueObjectId(
      1, CompositorElementIdNamespace::kDOMNodeId);
  EXPECT_EQ(CompositorElementIdNamespace::kDOMNodeId,
            NamespaceFromCompositorElementId(element_id));
  EXPECT_EQ(1, DOMNodeIdFromCompositorElementId(element_id));
}

TEST_F(CompositorElementIdTest, EncodeDecodeDOMNodeId) {
  auto element_id = CompositorElementIdFromDOMNodeId(1);
  EXPECT_EQ(CompositorElementIdNamespace::kDOMNodeId,
            NamespaceFromCompositorElementId(element_id));
  EXPECT_EQ(1, DOMNodeIdFromCompositorElementId(element_id));
}

}  // namespace blink
