// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/paint/object_paint_invalidator.h"

#include "testing/gmock/include/gmock/gmock-matchers.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/renderer/core/editing/frame_selection.h"
#include "third_party/blink/renderer/core/layout/layout_object.h"
#include "third_party/blink/renderer/core/layout/ng/inline/ng_inline_cursor.h"
#include "third_party/blink/renderer/core/paint/paint_and_raster_invalidation_test.h"
#include "third_party/blink/renderer/core/paint/paint_layer.h"
#include "third_party/blink/renderer/core/testing/core_unit_test_helper.h"
#include "third_party/blink/renderer/platform/json/json_values.h"
#include "third_party/blink/renderer/platform/testing/runtime_enabled_features_test_helpers.h"

namespace blink {

class ObjectPaintInvalidatorTest : public RenderingTest {
 protected:
  void SetUp() override {
    EnableCompositing();
    RenderingTest::SetUp();
  }

  static void ValidateDisplayItemClient(const DisplayItemClient* client) {
    client->Validate();
  }

  static bool IsValidDisplayItemClient(const DisplayItemClient* client) {
    return client->IsValid();
  }
};

using ::testing::ElementsAre;

TEST_F(ObjectPaintInvalidatorTest, Selection) {
  SetBodyInnerHTML("<img id='target' style='width: 100px; height: 100px'>");
  auto* target = GetLayoutObjectByElementId("target");

  // Add selection.
  GetDocument().View()->SetTracksRasterInvalidations(true);
  GetDocument().GetFrame()->Selection().SelectAll();
  UpdateAllLifecyclePhasesForTest();
  const auto* invalidations =
      &GetRasterInvalidationTracking(*GetDocument().View())->Invalidations();
  ASSERT_EQ(1u, invalidations->size());
  EXPECT_EQ(gfx::Rect(8, 8, 100, 100), (*invalidations)[0].rect);
  EXPECT_EQ(PaintInvalidationReason::kSelection, (*invalidations)[0].reason);
  GetDocument().View()->SetTracksRasterInvalidations(false);

  // Simulate a change without full invalidation or selection change.
  GetDocument().View()->SetTracksRasterInvalidations(true);
  target->SetShouldCheckForPaintInvalidation();
  UpdateAllLifecyclePhasesForTest();
  EXPECT_TRUE(GetRasterInvalidationTracking(*GetDocument().View())
                  ->Invalidations()
                  .IsEmpty());
  GetDocument().View()->SetTracksRasterInvalidations(false);

  // Remove selection.
  GetDocument().View()->SetTracksRasterInvalidations(true);
  GetDocument().GetFrame()->Selection().Clear();
  UpdateAllLifecyclePhasesForTest();
  invalidations =
      &GetRasterInvalidationTracking(*GetDocument().View())->Invalidations();
  ASSERT_EQ(1u, invalidations->size());
  EXPECT_EQ(gfx::Rect(8, 8, 100, 100), (*invalidations)[0].rect);
  EXPECT_EQ(PaintInvalidationReason::kSelection, (*invalidations)[0].reason);
  GetDocument().View()->SetTracksRasterInvalidations(false);
}

// Passes if it does not crash.
TEST_F(ObjectPaintInvalidatorTest, ZeroWidthForeignObject) {
  SetBodyInnerHTML(R"HTML(
    <svg style="backface-visibility: hidden;">
      <foreignObject width=0 height=50>
        <div style="position: relative">test</div>
      </foreignObject>
    </svg>
  )HTML");
}

TEST_F(ObjectPaintInvalidatorTest, VisibilityHidden) {
  SetBodyInnerHTML(R"HTML(
    <style>
      #target {
        visibility: hidden;
        width: 100px;
        height: 100px;
        background: blue;
      }
    </style>
    <div id="target"></div>
  )HTML");

  auto* target_element = GetDocument().getElementById("target");
  const auto* target = target_element->GetLayoutObject();
  ValidateDisplayItemClient(target);
  EXPECT_TRUE(IsValidDisplayItemClient(target));

  target_element->setAttribute(html_names::kStyleAttr, "width: 200px");
  GetDocument().View()->UpdateAllLifecyclePhasesExceptPaint(
      DocumentUpdateReason::kTest);
  EXPECT_TRUE(IsValidDisplayItemClient(target));
  UpdateAllLifecyclePhasesForTest();

  target_element->setAttribute(html_names::kStyleAttr,
                               "width: 200px; visibility: visible");
  GetDocument().View()->UpdateAllLifecyclePhasesExceptPaint(
      DocumentUpdateReason::kTest);
  EXPECT_FALSE(IsValidDisplayItemClient(target));
  UpdateAllLifecyclePhasesForTest();
  EXPECT_TRUE(IsValidDisplayItemClient(target));

  target_element->setAttribute(html_names::kStyleAttr,
                               "width: 200px; visibility: hidden");
  GetDocument().View()->UpdateAllLifecyclePhasesExceptPaint(
      DocumentUpdateReason::kTest);
  EXPECT_FALSE(IsValidDisplayItemClient(target));
  UpdateAllLifecyclePhasesForTest();
  // |target| is not validated because it didn't paint anything.
  EXPECT_FALSE(IsValidDisplayItemClient(target));
}

}  // namespace blink
