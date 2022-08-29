/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "third_party/blink/renderer/core/layout/overflow_model.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/renderer/platform/geometry/layout_rect.h"

namespace blink {
namespace {

LayoutRect InitialLayoutOverflow() {
  return LayoutRect(10, 10, 80, 80);
}

LayoutRect InitialVisualOverflow() {
  return LayoutRect(0, 0, 100, 100);
}

class SimpleOverflowModelTest : public testing::Test {
 protected:
  SimpleOverflowModelTest()
      : layout_overflow_(InitialLayoutOverflow()),
        visual_overflow_(InitialVisualOverflow()) {}
  SimpleLayoutOverflowModel layout_overflow_;
  SimpleVisualOverflowModel visual_overflow_;
};

TEST_F(SimpleOverflowModelTest, InitialOverflowRects) {
  EXPECT_EQ(InitialLayoutOverflow(), layout_overflow_.LayoutOverflowRect());
  EXPECT_EQ(InitialVisualOverflow(), visual_overflow_.VisualOverflowRect());
}

TEST_F(SimpleOverflowModelTest, AddLayoutOverflowOutsideExpandsRect) {
  layout_overflow_.AddLayoutOverflow(LayoutRect(0, 10, 30, 10));
  EXPECT_EQ(LayoutRect(0, 10, 90, 80), layout_overflow_.LayoutOverflowRect());
}

TEST_F(SimpleOverflowModelTest, AddLayoutOverflowInsideDoesNotAffectRect) {
  layout_overflow_.AddLayoutOverflow(LayoutRect(50, 50, 10, 20));
  EXPECT_EQ(InitialLayoutOverflow(), layout_overflow_.LayoutOverflowRect());
}

TEST_F(SimpleOverflowModelTest, AddLayoutOverflowEmpty) {
  // This test documents the existing behavior so that we are aware when/if
  // it changes. It would also be reasonable for addLayoutOverflow to be
  // a no-op in this situation.
  layout_overflow_.AddLayoutOverflow(LayoutRect(200, 200, 0, 0));
  EXPECT_EQ(LayoutRect(10, 10, 190, 190),
            layout_overflow_.LayoutOverflowRect());
}

TEST_F(SimpleOverflowModelTest, AddVisualOverflowOutsideExpandsRect) {
  visual_overflow_.AddVisualOverflow(LayoutRect(150, -50, 10, 10));
  EXPECT_EQ(LayoutRect(0, -50, 160, 150),
            visual_overflow_.VisualOverflowRect());
}

TEST_F(SimpleOverflowModelTest, AddVisualOverflowInsideDoesNotAffectRect) {
  visual_overflow_.AddVisualOverflow(LayoutRect(0, 10, 90, 90));
  EXPECT_EQ(InitialVisualOverflow(), visual_overflow_.VisualOverflowRect());
}

TEST_F(SimpleOverflowModelTest, AddVisualOverflowEmpty) {
  visual_overflow_.SetVisualOverflow(LayoutRect(0, 0, 600, 0));
  visual_overflow_.AddVisualOverflow(LayoutRect(100, -50, 100, 100));
  visual_overflow_.AddVisualOverflow(LayoutRect(300, 300, 0, 10000));
  EXPECT_EQ(LayoutRect(100, -50, 100, 100),
            visual_overflow_.VisualOverflowRect());
}

TEST_F(SimpleOverflowModelTest, MoveAffectsLayoutOverflow) {
  layout_overflow_.Move(LayoutUnit(500), LayoutUnit(100));
  EXPECT_EQ(LayoutRect(510, 110, 80, 80),
            layout_overflow_.LayoutOverflowRect());
}

class BoxOverflowModelTest : public testing::Test {
 protected:
  BoxOverflowModelTest()
      : layout_overflow_(InitialLayoutOverflow()),
        visual_overflow_(InitialVisualOverflow()) {}
  BoxLayoutOverflowModel layout_overflow_;
  BoxVisualOverflowModel visual_overflow_;
};

TEST_F(BoxOverflowModelTest, InitialOverflowRects) {
  EXPECT_EQ(InitialLayoutOverflow(), layout_overflow_.LayoutOverflowRect());
  EXPECT_EQ(InitialVisualOverflow(), visual_overflow_.SelfVisualOverflowRect());
  EXPECT_TRUE(visual_overflow_.ContentsVisualOverflowRect().IsEmpty());
}

TEST_F(BoxOverflowModelTest, AddLayoutOverflowOutsideExpandsRect) {
  layout_overflow_.AddLayoutOverflow(LayoutRect(0, 10, 30, 10));
  EXPECT_EQ(LayoutRect(0, 10, 90, 80), layout_overflow_.LayoutOverflowRect());
}

TEST_F(BoxOverflowModelTest, AddLayoutOverflowInsideDoesNotAffectRect) {
  layout_overflow_.AddLayoutOverflow(LayoutRect(50, 50, 10, 20));
  EXPECT_EQ(InitialLayoutOverflow(), layout_overflow_.LayoutOverflowRect());
}

TEST_F(BoxOverflowModelTest, AddLayoutOverflowEmpty) {
  // This test documents the existing behavior so that we are aware when/if
  // it changes. It would also be reasonable for addLayoutOverflow to be
  // a no-op in this situation.
  layout_overflow_.AddLayoutOverflow(LayoutRect(200, 200, 0, 0));
  EXPECT_EQ(LayoutRect(10, 10, 190, 190),
            layout_overflow_.LayoutOverflowRect());
}

TEST_F(BoxOverflowModelTest, AddSelfVisualOverflowOutsideExpandsRect) {
  visual_overflow_.AddSelfVisualOverflow(LayoutRect(150, -50, 10, 10));
  EXPECT_EQ(LayoutRect(0, -50, 160, 150),
            visual_overflow_.SelfVisualOverflowRect());
}

TEST_F(BoxOverflowModelTest, AddSelfVisualOverflowInsideDoesNotAffectRect) {
  visual_overflow_.AddSelfVisualOverflow(LayoutRect(0, 10, 90, 90));
  EXPECT_EQ(InitialVisualOverflow(), visual_overflow_.SelfVisualOverflowRect());
}

TEST_F(BoxOverflowModelTest, AddSelfVisualOverflowEmpty) {
  BoxVisualOverflowModel visual_overflow(LayoutRect(0, 0, 600, 0));
  visual_overflow.AddSelfVisualOverflow(LayoutRect(100, -50, 100, 100));
  visual_overflow.AddSelfVisualOverflow(LayoutRect(300, 300, 0, 10000));
  EXPECT_EQ(LayoutRect(100, -50, 100, 100),
            visual_overflow.SelfVisualOverflowRect());
}

TEST_F(BoxOverflowModelTest,
       AddSelfVisualOverflowDoesNotAffectContentsVisualOverflow) {
  visual_overflow_.AddSelfVisualOverflow(LayoutRect(300, 300, 300, 300));
  EXPECT_TRUE(visual_overflow_.ContentsVisualOverflowRect().IsEmpty());
}

TEST_F(BoxOverflowModelTest, AddContentsVisualOverflowFirstCall) {
  visual_overflow_.AddContentsVisualOverflow(LayoutRect(0, 0, 10, 10));
  EXPECT_EQ(LayoutRect(0, 0, 10, 10),
            visual_overflow_.ContentsVisualOverflowRect());
}

TEST_F(BoxOverflowModelTest, AddContentsVisualOverflowUnitesRects) {
  visual_overflow_.AddContentsVisualOverflow(LayoutRect(0, 0, 10, 10));
  visual_overflow_.AddContentsVisualOverflow(LayoutRect(80, 80, 10, 10));
  EXPECT_EQ(LayoutRect(0, 0, 90, 90),
            visual_overflow_.ContentsVisualOverflowRect());
}

TEST_F(BoxOverflowModelTest, AddContentsVisualOverflowRectWithinRect) {
  visual_overflow_.AddContentsVisualOverflow(LayoutRect(0, 0, 10, 10));
  visual_overflow_.AddContentsVisualOverflow(LayoutRect(2, 2, 5, 5));
  EXPECT_EQ(LayoutRect(0, 0, 10, 10),
            visual_overflow_.ContentsVisualOverflowRect());
}

TEST_F(BoxOverflowModelTest, AddContentsVisualOverflowEmpty) {
  visual_overflow_.AddContentsVisualOverflow(LayoutRect(0, 0, 10, 10));
  visual_overflow_.AddContentsVisualOverflow(LayoutRect(20, 20, 0, 0));
  EXPECT_EQ(LayoutRect(0, 0, 10, 10),
            visual_overflow_.ContentsVisualOverflowRect());
}

TEST_F(BoxOverflowModelTest, MoveAffectsLayoutOverflow) {
  layout_overflow_.Move(LayoutUnit(500), LayoutUnit(100));
  EXPECT_EQ(LayoutRect(510, 110, 80, 80),
            layout_overflow_.LayoutOverflowRect());
}

TEST_F(BoxOverflowModelTest, MoveAffectsSelfVisualOverflow) {
  visual_overflow_.Move(LayoutUnit(500), LayoutUnit(100));
  EXPECT_EQ(LayoutRect(500, 100, 100, 100),
            visual_overflow_.SelfVisualOverflowRect());
}

TEST_F(BoxOverflowModelTest, MoveAffectsContentsVisualOverflow) {
  visual_overflow_.AddContentsVisualOverflow(LayoutRect(0, 0, 10, 10));
  visual_overflow_.Move(LayoutUnit(500), LayoutUnit(100));
  EXPECT_EQ(LayoutRect(500, 100, 10, 10),
            visual_overflow_.ContentsVisualOverflowRect());
}

}  // anonymous namespace
}  // namespace blink
