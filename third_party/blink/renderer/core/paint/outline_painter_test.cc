// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/paint/outline_painter.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/renderer/core/style/computed_style.h"
#include "third_party/blink/renderer/core/testing/core_unit_test_helper.h"
#include "third_party/skia/include/core/SkPath.h"

namespace blink {

using OutlinePainterTest = RenderingTest;

TEST_F(OutlinePainterTest, FocusRingOutset) {
  auto initial_style = ComputedStyle::CreateInitialStyleSingleton();
  auto style = ComputedStyle::Clone(*initial_style);
  style->SetOutlineStyle(EBorderStyle::kSolid);
  style->SetOutlineStyleIsAuto(true);
  LayoutObject::OutlineInfo info =
      LayoutObject::OutlineInfo::GetFromStyle(*style);
  EXPECT_EQ(2, OutlinePainter::OutlineOutsetExtent(*style, info));
  style->SetEffectiveZoom(4.75);
  EXPECT_EQ(10, OutlinePainter::OutlineOutsetExtent(*style, info));
  style->SetEffectiveZoom(10);
  EXPECT_EQ(20, OutlinePainter::OutlineOutsetExtent(*style, info));
}

TEST_F(OutlinePainterTest, HugeOutlineWidthOffset) {
  SetBodyInnerHTML(R"HTML(
    <div id=target
         style="outline: 900000000px solid black; outline-offset: 900000000px">
    </div>
  )HTML");
  LayoutObject::OutlineInfo info;
  GetLayoutObjectByElementId("target")->OutlineRects(
      &info, PhysicalOffset(), NGOutlineType::kDontIncludeBlockVisualOverflow);
  const auto& style = GetLayoutObjectByElementId("target")->StyleRef();
  EXPECT_TRUE(style.HasOutline());
  EXPECT_EQ(LayoutUnit::Max().ToInt() * 2,
            OutlinePainter::OutlineOutsetExtent(style, info));
}

// Actually this is not a test for OutlinePainter itself, but it ensures
// that the style logic OutlinePainter depending on is correct.
TEST_F(OutlinePainterTest, OutlineWidthLessThanOne) {
  SetBodyInnerHTML("<div id=target style='outline: 0.2px solid black'></div>");
  const auto& style = GetLayoutObjectByElementId("target")->StyleRef();
  EXPECT_TRUE(style.HasOutline());
  EXPECT_EQ(LayoutUnit(1), style.OutlineWidth());
  LayoutObject::OutlineInfo info =
      LayoutObject::OutlineInfo::GetFromStyle(style);
  EXPECT_EQ(1, OutlinePainter::OutlineOutsetExtent(style, info));
}

TEST_F(OutlinePainterTest, IterateCollapsedPath) {
  SkPath path;
  path.moveTo(8, 12);
  path.lineTo(8, 4);
  path.lineTo(9, 4);
  path.lineTo(9, 0);
  path.lineTo(9, 0);
  path.lineTo(9, 4);
  path.lineTo(8, 4);
  path.close();
  // Collapsed contour should not cause crash and should be ignored.
  OutlinePainter::IterateRightAnglePathForTesting(
      path, base::BindRepeating(
                [](const Vector<OutlinePainter::Line>&) { NOTREACHED(); }));
}

}  // namespace blink
