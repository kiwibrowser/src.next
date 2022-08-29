// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/layout/layout_view.h"

#include "build/build_config.h"
#include "third_party/blink/public/mojom/webpreferences/web_preferences.mojom-blink.h"
#include "third_party/blink/renderer/core/dom/node_computed_style.h"
#include "third_party/blink/renderer/core/editing/position_with_affinity.h"
#include "third_party/blink/renderer/core/editing/text_affinity.h"
#include "third_party/blink/renderer/core/html/html_iframe_element.h"
#include "third_party/blink/renderer/core/page/named_pages_mapper.h"
#include "third_party/blink/renderer/core/page/print_context.h"
#include "third_party/blink/renderer/core/testing/core_unit_test_helper.h"
#include "third_party/blink/renderer/platform/testing/runtime_enabled_features_test_helpers.h"

namespace blink {

class LayoutViewTest : public RenderingTest {
 public:
  LayoutViewTest()
      : RenderingTest(MakeGarbageCollected<SingleChildLocalFrameClient>()) {}
};

TEST_F(LayoutViewTest, UpdateCountersLayout) {
  SetBodyInnerHTML(R"HTML(
    <style>
      div.incX { counter-increment: x }
      div.incY { counter-increment: y }
      div::before { content: counter(y) }
    </style>
    <div id=inc></div>
  )HTML");

  UpdateAllLifecyclePhasesForTest();
  Element* inc = GetDocument().getElementById("inc");

  inc->setAttribute("class", "incX");
  GetDocument().UpdateStyleAndLayoutTree();
  EXPECT_FALSE(GetDocument().View()->NeedsLayout());

  UpdateAllLifecyclePhasesForTest();
  inc->setAttribute("class", "incY");
  GetDocument().UpdateStyleAndLayoutTree();
  EXPECT_TRUE(GetDocument().View()->NeedsLayout());
}

TEST_F(LayoutViewTest, DisplayNoneFrame) {
  SetBodyInnerHTML(R"HTML(
    <iframe id="iframe" style="display:none"></iframe>
  )HTML");

  auto* iframe = To<HTMLIFrameElement>(GetDocument().getElementById("iframe"));
  Document* frame_doc = iframe->contentDocument();
  ASSERT_TRUE(frame_doc);
  frame_doc->OverrideIsInitialEmptyDocument();
  frame_doc->View()->BeginLifecycleUpdates();
  UpdateAllLifecyclePhasesForTest();

  LayoutObject* view = frame_doc->GetLayoutView();
  ASSERT_TRUE(view);
  EXPECT_FALSE(view->CanHaveChildren());
  EXPECT_FALSE(frame_doc->documentElement()->GetComputedStyle());

  frame_doc->body()->setInnerHTML(R"HTML(
    <div id="div"></div>
  )HTML");

  EXPECT_FALSE(frame_doc->NeedsLayoutTreeUpdate());
}

TEST_F(LayoutViewTest, NamedPages) {
  ScopedNamedPagesForTest named_pages_enabler(true);

  SetBodyInnerHTML(R"HTML(
    <!DOCTYPE html>
    <style>
      div:empty { height:10px; }
    </style>
    <!-- First page: -->
    <div></div>
    <!-- Second page: -->
    <div style="break-before:page;"></div>
    <!-- Third page: -->
    <div style="page:yksi;"></div>
    <!-- Fourth page: -->
    <div style="page:yksi;">
      <div style="page:yksi; break-before:page;"></div>
      <!-- Fifth page: -->
      <div style="page:yksi; break-before:page;"></div>
    </div>
    <!-- Sixth page: -->
    <div style="page:kaksi;"></div>
    <!-- Seventh page: -->
    <div style="page:maksitaksi;"></div>
    <!-- Eighth page: -->
    <div></div>
    <!-- Ninth page: -->
    <div style="page:yksi;"></div>
  )HTML");

  UpdateAllLifecyclePhasesForTest();
  const LayoutView* view = GetDocument().GetLayoutView();
  ASSERT_TRUE(view);

  ScopedPrintContext print_context(&GetDocument().View()->GetFrame());
  print_context->BeginPrintMode(500, 500);
  const NamedPagesMapper* mapper = view->GetNamedPagesMapper();
  ASSERT_TRUE(mapper);

  EXPECT_EQ(mapper->NamedPageAtIndex(0), AtomicString());
  EXPECT_EQ(mapper->NamedPageAtIndex(1), AtomicString());
  EXPECT_EQ(mapper->NamedPageAtIndex(2), "yksi");
  EXPECT_EQ(mapper->NamedPageAtIndex(3), "yksi");
  EXPECT_EQ(mapper->NamedPageAtIndex(4), "yksi");
  EXPECT_EQ(mapper->NamedPageAtIndex(5), "kaksi");
  EXPECT_EQ(mapper->NamedPageAtIndex(6), "maksitaksi");
  EXPECT_EQ(mapper->NamedPageAtIndex(7), AtomicString());
  EXPECT_EQ(mapper->NamedPageAtIndex(8), "yksi");
  EXPECT_EQ(mapper->NamedPageAtIndex(9), "yksi");
  EXPECT_EQ(mapper->NamedPageAtIndex(100), "yksi");
  EXPECT_EQ(mapper->LastPageName(), "yksi");
}

struct HitTestConfig {
  bool layout_ng;
  mojom::EditingBehavior editing_behavior;
};

class LayoutViewHitTestTest : public testing::WithParamInterface<HitTestConfig>,
                              private ScopedLayoutNGForTest,
                              public RenderingTest {
 public:
  LayoutViewHitTestTest()
      : ScopedLayoutNGForTest(GetParam().layout_ng),
        RenderingTest(MakeGarbageCollected<SingleChildLocalFrameClient>()) {}

 protected:
  bool LayoutNG() { return RuntimeEnabledFeatures::LayoutNGEnabled(); }
  bool IsAndroidOrWindowsEditingBehavior() {
    return GetParam().editing_behavior ==
               mojom::EditingBehavior::kEditingAndroidBehavior ||
           GetParam().editing_behavior ==
               mojom::EditingBehavior::kEditingWindowsBehavior;
  }

  void SetUp() override {
    RenderingTest::SetUp();
    GetFrame().GetSettings()->SetEditingBehaviorType(
        GetParam().editing_behavior);
  }

  PositionWithAffinity HitTest(int left, int top) {
    const HitTestRequest hit_request(HitTestRequest::kActive);
    const HitTestLocation hit_location(PhysicalOffset(left, top));
    HitTestResult hit_result(hit_request, hit_location);
    if (!GetLayoutView().HitTest(hit_location, hit_result))
      return PositionWithAffinity();
    return hit_result.GetPosition();
  }
};

INSTANTIATE_TEST_SUITE_P(
    All,
    LayoutViewHitTestTest,
    ::testing::Values(
        // Legacy 0 to 4
        HitTestConfig{false, mojom::EditingBehavior::kEditingMacBehavior},
        HitTestConfig{false, mojom::EditingBehavior::kEditingWindowsBehavior},
        HitTestConfig{false, mojom::EditingBehavior::kEditingUnixBehavior},
        HitTestConfig{false, mojom::EditingBehavior::kEditingAndroidBehavior},
        HitTestConfig{false, mojom::EditingBehavior::kEditingChromeOSBehavior},
        // LayoutNG 5 to 9
        HitTestConfig{true, mojom::EditingBehavior::kEditingMacBehavior},
        HitTestConfig{true, mojom::EditingBehavior::kEditingWindowsBehavior},
        HitTestConfig{true, mojom::EditingBehavior::kEditingUnixBehavior},
        HitTestConfig{true, mojom::EditingBehavior::kEditingAndroidBehavior},
        HitTestConfig{true, mojom::EditingBehavior::kEditingChromeOSBehavior}));

// See editing/selection/click-after-nested-block.html
TEST_P(LayoutViewHitTestTest, BlockInInlineBelowBottom) {
  LoadAhem();
  InsertStyleElement("body { margin: 0px; font: 10px/15px Ahem; }");
  SetBodyInnerHTML(
      "<div id=target>"
      "<div id=line1>ab</div>"
      "<div><span><div id=line2>cd</div></span></div>"
      "</div>");
  const auto& line2 = *GetElementById("line2");
  const auto& cd = *To<Text>(line2.firstChild());
  const auto& cd_0 = PositionWithAffinity(Position(cd, 0));
  const auto& cd_1 = PositionWithAffinity(
      Position(cd, 1),
      LayoutNG() ? TextAffinity::kDownstream : TextAffinity::kUpstream);
  const auto& cd_2 =
      PositionWithAffinity(Position(cd, 2), TextAffinity::kUpstream);
  const auto& kEndOfLine = PositionWithAffinity(
      LayoutNG() ? Position::AfterNode(line2) : Position(cd, 2));

  // hit test on line 2
  EXPECT_EQ(cd_0, HitTest(0, 20));
  EXPECT_EQ(cd_0, HitTest(5, 20));
  EXPECT_EQ(cd_1, HitTest(10, 20));
  EXPECT_EQ(cd_1, HitTest(15, 20));
  EXPECT_EQ(cd_2, HitTest(20, 20));
  EXPECT_EQ(cd_2, HitTest(25, 20));

  // hit test below line 2
  if (IsAndroidOrWindowsEditingBehavior()) {
    EXPECT_EQ(cd_0, HitTest(0, 50));
    EXPECT_EQ(cd_0, HitTest(5, 50));
    EXPECT_EQ(cd_1, HitTest(10, 50));
    EXPECT_EQ(cd_1, HitTest(15, 50));
    EXPECT_EQ(cd_2, HitTest(20, 50));
    EXPECT_EQ(cd_2, HitTest(25, 50));
  } else {
    // ShouldMoveCaretToHorizontalBoundaryWhenPastTopOrBottom behavior is
    // in effect.
    EXPECT_EQ(kEndOfLine, HitTest(0, 50));
    EXPECT_EQ(kEndOfLine, HitTest(5, 50));
    EXPECT_EQ(kEndOfLine, HitTest(10, 50));
    EXPECT_EQ(kEndOfLine, HitTest(15, 50));
    EXPECT_EQ(kEndOfLine, HitTest(25, 50));
  }
}

// See editing/pasteboard/drag-drop-list.html
TEST_P(LayoutViewHitTestTest, BlockInInlineWithListItem) {
  LoadAhem();
  InsertStyleElement("body { margin: 0px; font: 10px/15px Ahem; }");
  SetBodyInnerHTML("<li id=target><span><div id=inner>abc</div></span>");
  const auto& target = *GetElementById("target");
  const auto& span = *target.firstChild();
  const auto& inner = *GetElementById("inner");
  const auto& abc = *To<Text>(inner.firstChild());

  // Note: span@0 comes from |LayoutObject::FindPosition()| via
  // |LayoutObject::CreatePositionWithAffinity()| for anonymous block
  // containing list marker.
  // LayoutNGBlockFlow (anonymous)
  //    LayoutNGInsideListMarker {::marker}
  //      LayoutText (anonymous)
  //      LayoutInline {SPAN}
  EXPECT_EQ(PositionWithAffinity(Position(span, 0)), HitTest(0, 5));
  EXPECT_EQ(PositionWithAffinity(Position(span, 0)), HitTest(0, 10));
  if (RuntimeEnabledFeatures::LayoutNGBlockInInlineEnabled()) {
    if (IsAndroidOrWindowsEditingBehavior()) {
      EXPECT_EQ(PositionWithAffinity(Position(abc, 1)), HitTest(10, 5));
      EXPECT_EQ(PositionWithAffinity(Position(abc, 1)), HitTest(10, 10));
      EXPECT_EQ(PositionWithAffinity(Position(abc, 3), TextAffinity::kUpstream),
                HitTest(100, 5));
      EXPECT_EQ(PositionWithAffinity(Position(abc, 3), TextAffinity::kUpstream),
                HitTest(100, 10));
    } else {
      EXPECT_EQ(PositionWithAffinity(Position::BeforeNode(inner)),
                HitTest(10, 5));
      EXPECT_EQ(PositionWithAffinity(Position::BeforeNode(inner)),
                HitTest(10, 10));
      EXPECT_EQ(PositionWithAffinity(Position::BeforeNode(inner)),
                HitTest(100, 5));
      EXPECT_EQ(PositionWithAffinity(Position::BeforeNode(inner)),
                HitTest(100, 10));
    }
  } else {
    EXPECT_EQ(PositionWithAffinity(Position(span, 0)), HitTest(10, 5));
    EXPECT_EQ(PositionWithAffinity(Position(span, 0)), HitTest(10, 10));
    EXPECT_EQ(PositionWithAffinity(Position(span, 0)), HitTest(100, 5));
    EXPECT_EQ(PositionWithAffinity(Position(span, 0)), HitTest(100, 10));
  }
  if (LayoutNG() || IsAndroidOrWindowsEditingBehavior()) {
    EXPECT_EQ(PositionWithAffinity(Position(abc, 3), TextAffinity::kUpstream),
              HitTest(100, 15));
  } else {
    EXPECT_EQ(PositionWithAffinity(Position(abc, 0)), HitTest(100, 15));
  }
  EXPECT_EQ(PositionWithAffinity(Position(abc, 3), TextAffinity::kUpstream),
            HitTest(100, 20));
  EXPECT_EQ(PositionWithAffinity(Position(abc, 3), TextAffinity::kUpstream),
            HitTest(100, 25));
}

TEST_P(LayoutViewHitTestTest, EmptySpan) {
  LoadAhem();
  InsertStyleElement(
      "body { margin: 0px; font: 10px/10px Ahem; }"
      "#target { width: 50px; }"
      "b { border: solid 5px green; }");
  SetBodyInnerHTML("<div id=target>AB<b></b></div>");
  auto& target = *GetElementById("target");
  auto& ab = *To<Text>(target.firstChild());
  const auto after_ab =
      PositionWithAffinity(Position(ab, 2), TextAffinity::kUpstream);

  EXPECT_EQ(PositionWithAffinity(Position(ab, 0)), HitTest(0, 5));
  EXPECT_EQ(PositionWithAffinity(Position(ab, 0)), HitTest(5, 5));
  EXPECT_EQ(PositionWithAffinity(Position(ab, 1),
                                 LayoutNG() ? TextAffinity::kDownstream
                                            : TextAffinity::kUpstream),
            HitTest(10, 5));
  EXPECT_EQ(PositionWithAffinity(Position(ab, 1),
                                 LayoutNG() ? TextAffinity::kDownstream
                                            : TextAffinity::kUpstream),
            HitTest(15, 5));
  EXPECT_EQ(after_ab, HitTest(20, 5));
  EXPECT_EQ(after_ab, HitTest(25, 5));
  EXPECT_EQ(after_ab, HitTest(30, 5));
  EXPECT_EQ(after_ab, HitTest(35, 5));
  EXPECT_EQ(after_ab, HitTest(40, 5));
  EXPECT_EQ(after_ab, HitTest(45, 5));
  EXPECT_EQ(after_ab, HitTest(50, 5));
  EXPECT_EQ(after_ab, HitTest(55, 5));
}

// http://crbug.com/1233862
TEST_P(LayoutViewHitTestTest, FlexBlockChildren) {
  LoadAhem();
  InsertStyleElement(
      "body { margin: 0px; font: 10px/10px Ahem; }"
      "#t { display: flex; }");
  SetBodyInnerHTML("<div id=t><div id=ab>ab</div><div id=xy>XY</div></div>");

  const auto& ab = *To<Text>(GetElementById("ab")->firstChild());
  const auto& xy = *To<Text>(GetElementById("xy")->firstChild());

  EXPECT_EQ(PositionWithAffinity(Position(ab, 0)), HitTest(0, 5));
  EXPECT_EQ(PositionWithAffinity(Position(ab, 0)), HitTest(5, 5));
  EXPECT_EQ(PositionWithAffinity(Position(ab, 1),
                                 LayoutNG() ? TextAffinity::kDownstream
                                            : TextAffinity::kUpstream),
            HitTest(10, 5));
  EXPECT_EQ(PositionWithAffinity(Position(ab, 1),
                                 LayoutNG() ? TextAffinity::kDownstream
                                            : TextAffinity::kUpstream),
            HitTest(15, 5));
  EXPECT_EQ(PositionWithAffinity(Position(xy, 0)), HitTest(20, 5));
  EXPECT_EQ(PositionWithAffinity(Position(xy, 0)), HitTest(25, 5));
  EXPECT_EQ(PositionWithAffinity(Position(xy, 1),
                                 LayoutNG() ? TextAffinity::kDownstream
                                            : TextAffinity::kUpstream),
            HitTest(30, 5));
  EXPECT_EQ(PositionWithAffinity(Position(xy, 1),
                                 LayoutNG() ? TextAffinity::kDownstream
                                            : TextAffinity::kUpstream),
            HitTest(35, 5));
  EXPECT_EQ(PositionWithAffinity(Position(xy, 2), TextAffinity::kUpstream),
            HitTest(40, 5));
  EXPECT_EQ(PositionWithAffinity(Position(xy, 2), TextAffinity::kUpstream),
            HitTest(45, 5));
}

// http://crbug.com/1171070
// See also, FloatLeft*, DOM order of "float" should not affect hit testing.
TEST_P(LayoutViewHitTestTest, FloatLeftLeft) {
  LoadAhem();
  InsertStyleElement(
      "body { margin: 0px; font: 10px/10px Ahem; }"
      "#target { width: 70px; }"
      ".float { float: left; margin-right: 10px; }");
  SetBodyInnerHTML("<div id=target><div class=float>ab</div>xy</div>");
  // NGFragmentItem
  //   [0] kLine (30,0)x(20,10)
  //   [1] kBox/Floating (0,0)x(20,10)
  //   [2] kText "xy" (30,0)x(20,10)
  auto& target = *GetElementById("target");
  auto& ab = *To<Text>(target.firstChild()->firstChild());
  auto& xy = *To<Text>(target.lastChild());

  EXPECT_EQ(PositionWithAffinity(Position(ab, 0)), HitTest(0, 5));
  EXPECT_EQ(PositionWithAffinity(Position(ab, 0)), HitTest(5, 5));
  EXPECT_EQ(PositionWithAffinity(Position(ab, 1),
                                 LayoutNG() ? TextAffinity::kDownstream
                                            : TextAffinity::kUpstream),
            HitTest(15, 5));
  EXPECT_EQ(PositionWithAffinity(Position(xy, 0)), HitTest(20, 5));
  EXPECT_EQ(PositionWithAffinity(Position(xy, 0)), HitTest(25, 5));
  EXPECT_EQ(PositionWithAffinity(Position(xy, 0)), HitTest(30, 5));
  EXPECT_EQ(PositionWithAffinity(Position(xy, 0)), HitTest(35, 5));
  EXPECT_EQ(PositionWithAffinity(Position(xy, 1),
                                 LayoutNG() ? TextAffinity::kDownstream
                                            : TextAffinity::kUpstream),
            HitTest(40, 5));
  EXPECT_EQ(PositionWithAffinity(Position(xy, 1),
                                 LayoutNG() ? TextAffinity::kDownstream
                                            : TextAffinity::kUpstream),
            HitTest(45, 5));
  EXPECT_EQ(PositionWithAffinity(Position(xy, 2), TextAffinity::kUpstream),
            HitTest(50, 5));
  EXPECT_EQ(PositionWithAffinity(Position(xy, 2), TextAffinity::kUpstream),
            HitTest(55, 5));
}

// http://crbug.com/1171070
// See also, FloatLeft*, DOM order of "float" should not affect hit testing.
TEST_P(LayoutViewHitTestTest, FloatLeftMiddle) {
  LoadAhem();
  InsertStyleElement(
      "body { margin: 0px; font: 10px/10px Ahem; }"
      "#target { width: 70px; }"
      ".float { float: left; margin-right: 10px; }");
  SetBodyInnerHTML("<div id=target>x<div class=float>ab</div>y</div>");
  // NGFragmentItem
  //   [0] kLine (30,0)x(20,10)
  //   [1] kText "x" (30,0)x(10,10)
  //   [1] kBox/Floating (0,0)x(20,10)
  //   [2] kText "y" (40,0)x(10,10)
  auto& target = *GetElementById("target");
  auto& ab = *To<Text>(target.firstChild()->nextSibling()->firstChild());
  auto& x = *To<Text>(target.firstChild());
  auto& y = *To<Text>(target.lastChild());

  EXPECT_EQ(PositionWithAffinity(Position(ab, 0)), HitTest(0, 5));
  EXPECT_EQ(PositionWithAffinity(Position(ab, 0)), HitTest(5, 5));
  EXPECT_EQ(PositionWithAffinity(Position(ab, 1),
                                 LayoutNG() ? TextAffinity::kDownstream
                                            : TextAffinity::kUpstream),
            HitTest(15, 5));
  EXPECT_EQ(PositionWithAffinity(Position(x, 0)), HitTest(20, 5));
  EXPECT_EQ(PositionWithAffinity(Position(x, 0)), HitTest(25, 5));
  EXPECT_EQ(PositionWithAffinity(Position(x, 0)), HitTest(30, 5));
  EXPECT_EQ(PositionWithAffinity(Position(x, 0)), HitTest(35, 5));
  EXPECT_EQ(PositionWithAffinity(Position(y, 0)), HitTest(40, 5));
  EXPECT_EQ(PositionWithAffinity(Position(y, 0)), HitTest(45, 5));
  EXPECT_EQ(PositionWithAffinity(Position(y, 1), TextAffinity::kUpstream),
            HitTest(50, 5));
  EXPECT_EQ(PositionWithAffinity(Position(y, 1), TextAffinity::kUpstream),
            HitTest(55, 5));
}

// http://crbug.com/1171070
// See also, FloatLeft*, DOM order of "float" should not affect hit testing.
TEST_P(LayoutViewHitTestTest, FloatLeftRight) {
  LoadAhem();
  InsertStyleElement(
      "body { margin: 0px; font: 10px/10px Ahem; }"
      "#target { width: 70px; }"
      ".float { float: left; margin-right: 10px; }");
  SetBodyInnerHTML("<div id=target>xy<div class=float>ab</div></div>");
  // NGFragmentItem
  //   [0] kLine (30,0)x(20,10)
  //   [1] kText "xy" (30,0)x(20,10)
  //   [2] kBox/Floating (0,0)x(20,10)
  auto& target = *GetElementById("target");
  auto& ab = *To<Text>(target.lastChild()->firstChild());
  auto& xy = *To<Text>(target.firstChild());

  EXPECT_EQ(PositionWithAffinity(Position(ab, 0)), HitTest(0, 5));
  EXPECT_EQ(PositionWithAffinity(Position(ab, 0)), HitTest(5, 5));
  EXPECT_EQ(PositionWithAffinity(Position(ab, 1),
                                 LayoutNG() ? TextAffinity::kDownstream
                                            : TextAffinity::kUpstream),
            HitTest(15, 5));
  EXPECT_EQ(PositionWithAffinity(Position(xy, 0)), HitTest(20, 5));
  EXPECT_EQ(PositionWithAffinity(Position(xy, 0)), HitTest(25, 5));
  EXPECT_EQ(PositionWithAffinity(Position(xy, 0)), HitTest(30, 5));
  EXPECT_EQ(PositionWithAffinity(Position(xy, 0)), HitTest(35, 5));
  EXPECT_EQ(PositionWithAffinity(Position(xy, 1),
                                 LayoutNG() ? TextAffinity::kDownstream
                                            : TextAffinity::kUpstream),
            HitTest(40, 5));
  EXPECT_EQ(PositionWithAffinity(Position(xy, 1),
                                 LayoutNG() ? TextAffinity::kDownstream
                                            : TextAffinity::kUpstream),
            HitTest(45, 5));
  EXPECT_EQ(PositionWithAffinity(Position(xy, 2), TextAffinity::kUpstream),
            HitTest(50, 5));
  EXPECT_EQ(PositionWithAffinity(Position(xy, 2), TextAffinity::kUpstream),
            HitTest(55, 5));
}

// http://crbug.com/1171070
// See also, FloatRight*, DOM order of "float" should not affect hit testing.
TEST_P(LayoutViewHitTestTest, FloatRightLeft) {
  LoadAhem();
  InsertStyleElement(
      "body { margin: 0px; font: 10px/10px Ahem; }"
      "#target { width: 50px; }"
      ".float { float: right; }");
  SetBodyInnerHTML("<div id=target>xy<div class=float>ab</div></div>");
  // NGFragmentItem
  //   [0] kLine (0,0)x(20,10)
  //   [1] kBox/Floating (30,0)x(20,10)
  auto& target = *GetElementById("target");
  auto& ab = *To<Text>(target.lastChild()->firstChild());
  auto& xy = *To<Text>(target.firstChild());

  EXPECT_EQ(PositionWithAffinity(Position(xy, 0)), HitTest(0, 5));
  EXPECT_EQ(PositionWithAffinity(Position(xy, 0)), HitTest(5, 5));
  EXPECT_EQ(PositionWithAffinity(Position(xy, 1),
                                 LayoutNG() ? TextAffinity::kDownstream
                                            : TextAffinity::kUpstream),
            HitTest(15, 5));
  EXPECT_EQ(PositionWithAffinity(Position(xy, 2), TextAffinity::kUpstream),
            HitTest(20, 5))
      << "at right of 'xy'";
  EXPECT_EQ(PositionWithAffinity(Position(xy, 2), TextAffinity::kUpstream),
            HitTest(25, 5))
      << "right of 'xy'";
  EXPECT_EQ(PositionWithAffinity(Position(ab, 0)), HitTest(30, 5))
      << "inside float";
  EXPECT_EQ(PositionWithAffinity(Position(ab, 0)), HitTest(35, 5))
      << "inside float";
  EXPECT_EQ(PositionWithAffinity(Position(ab, 1),
                                 LayoutNG() ? TextAffinity::kDownstream
                                            : TextAffinity::kUpstream),
            HitTest(40, 5))
      << "inside float";
  EXPECT_EQ(PositionWithAffinity(Position(ab, 1),
                                 LayoutNG() ? TextAffinity::kDownstream
                                            : TextAffinity::kUpstream),
            HitTest(45, 5))
      << "inside float";

  // |HitTestResult| holds <body>.
  EXPECT_EQ(PositionWithAffinity(Position(xy, 2), TextAffinity::kUpstream),
            HitTest(50, 5))
      << "at right side of float";
  EXPECT_EQ(PositionWithAffinity(Position(xy, 2), TextAffinity::kUpstream),
            HitTest(55, 5))
      << "right of float";
}

// http://crbug.com/1171070
// See also, FloatRight*, DOM order of "float" should not affect hit testing.
TEST_P(LayoutViewHitTestTest, FloatRightMiddle) {
  LoadAhem();
  InsertStyleElement(
      "body { margin: 0px; font: 10px/10px Ahem; }"
      "#target { width: 50px; }"
      ".float { float: right; }");
  SetBodyInnerHTML("<div id=target>x<div class=float>ab</div>y</div>");
  // NGFragmentItem
  //   [0] kLine (0,0)x(20,10)
  //   [1] kText "x" (0,0)x(10,10)
  //   [2] kBox/Floating (30,0)x(20,10)
  //   [3] kText "y" (10,0)x(10,10)
  auto& target = *GetElementById("target");
  auto& ab = *To<Text>(target.firstChild()->nextSibling()->firstChild());
  auto& x = *To<Text>(target.firstChild());
  auto& y = *To<Text>(target.lastChild());

  EXPECT_EQ(PositionWithAffinity(Position(x, 0)), HitTest(0, 5));
  EXPECT_EQ(PositionWithAffinity(Position(x, 0)), HitTest(5, 5));
  EXPECT_EQ(PositionWithAffinity(Position(y, 0)), HitTest(15, 5));
  EXPECT_EQ(PositionWithAffinity(Position(y, 1), TextAffinity::kUpstream),
            HitTest(20, 5));
  EXPECT_EQ(PositionWithAffinity(Position(y, 1), TextAffinity::kUpstream),
            HitTest(25, 5));
  EXPECT_EQ(PositionWithAffinity(Position(ab, 0)), HitTest(30, 5));
  EXPECT_EQ(PositionWithAffinity(Position(ab, 0)), HitTest(35, 5));
  EXPECT_EQ(PositionWithAffinity(Position(ab, 1),
                                 LayoutNG() ? TextAffinity::kDownstream
                                            : TextAffinity::kUpstream),
            HitTest(40, 5));
  EXPECT_EQ(PositionWithAffinity(Position(ab, 1),
                                 LayoutNG() ? TextAffinity::kDownstream
                                            : TextAffinity::kUpstream),
            HitTest(45, 5));
  EXPECT_EQ(PositionWithAffinity(Position(y, 1), TextAffinity::kUpstream),
            HitTest(50, 5));
  EXPECT_EQ(PositionWithAffinity(Position(y, 1), TextAffinity::kUpstream),
            HitTest(55, 5));
}

// http://crbug.com/1171070
// See also, FloatRight*, DOM order of "float" should not affect hit testing.
TEST_P(LayoutViewHitTestTest, FloatRightRight) {
  LoadAhem();
  InsertStyleElement(
      "body { margin: 0px; font: 10px/10px Ahem; }"
      "#target { width: 50px; }"
      ".float { float: right; }");
  SetBodyInnerHTML("<div id=target><div class=float>ab</div>xy</div>");
  //   [0] kLine (0,0)x(20,10)
  //   [1] kBox/Floating (30,0)x(20,10)
  //   [2] kText "xy" (0,0)x(20,10)
  auto& target = *GetElementById("target");
  auto& ab = *To<Text>(target.firstChild()->firstChild());
  auto& xy = *To<Text>(target.lastChild());

  EXPECT_EQ(PositionWithAffinity(Position(xy, 0)), HitTest(0, 5));
  EXPECT_EQ(PositionWithAffinity(Position(xy, 0)), HitTest(5, 5));
  EXPECT_EQ(PositionWithAffinity(Position(xy, 1),
                                 LayoutNG() ? TextAffinity::kDownstream
                                            : TextAffinity::kUpstream),
            HitTest(15, 5));
  EXPECT_EQ(PositionWithAffinity(Position(xy, 2), TextAffinity::kUpstream),
            HitTest(20, 5));
  EXPECT_EQ(PositionWithAffinity(Position(xy, 2), TextAffinity::kUpstream),
            HitTest(25, 5));
  EXPECT_EQ(PositionWithAffinity(Position(ab, 0)), HitTest(30, 5));
  EXPECT_EQ(PositionWithAffinity(Position(ab, 0)), HitTest(35, 5));
  EXPECT_EQ(PositionWithAffinity(Position(ab, 1),
                                 LayoutNG() ? TextAffinity::kDownstream
                                            : TextAffinity::kUpstream),
            HitTest(40, 5));
  EXPECT_EQ(PositionWithAffinity(Position(ab, 1),
                                 LayoutNG() ? TextAffinity::kDownstream
                                            : TextAffinity::kUpstream),
            HitTest(45, 5));
  EXPECT_EQ(PositionWithAffinity(Position(xy, 2), TextAffinity::kUpstream),
            HitTest(50, 5));
  EXPECT_EQ(PositionWithAffinity(Position(xy, 2), TextAffinity::kUpstream),
            HitTest(55, 5));
}

TEST_P(LayoutViewHitTestTest, PositionAbsolute) {
  LoadAhem();
  InsertStyleElement(
      "body { margin: 0px; font: 10px/10px Ahem; }"
      "#target { width: 70px; }"
      ".abspos { position: absolute; left: 40px; top: 0px; }");
  SetBodyInnerHTML("<div id=target><div class=abspos>ab</div>xy</div>");
  // NGFragmentItem
  //   [0] kLine (0,0)x(20,10)
  //   [2] kText "xy" (30,0)x(20,10)
  // Note: position:absolute isn't in NGFragmentItems of #target.
  auto& target = *GetElementById("target");
  auto& ab = *To<Text>(target.firstChild()->firstChild());
  auto& xy = *To<Text>(target.lastChild());

  EXPECT_EQ(PositionWithAffinity(Position(xy, 0)), HitTest(0, 5));
  EXPECT_EQ(PositionWithAffinity(Position(xy, 0)), HitTest(5, 5));
  EXPECT_EQ(PositionWithAffinity(Position(xy, 1),
                                 LayoutNG() ? TextAffinity::kDownstream
                                            : TextAffinity::kUpstream),
            HitTest(15, 5));
  EXPECT_EQ(PositionWithAffinity(Position(xy, 2), TextAffinity::kUpstream),
            HitTest(20, 5));
  EXPECT_EQ(PositionWithAffinity(Position(xy, 2), TextAffinity::kUpstream),
            HitTest(25, 5));
  EXPECT_EQ(PositionWithAffinity(Position(xy, 2), TextAffinity::kUpstream),
            HitTest(30, 5));
  EXPECT_EQ(PositionWithAffinity(Position(xy, 2), TextAffinity::kUpstream),
            HitTest(35, 5));
  EXPECT_EQ(PositionWithAffinity(Position(ab, 0)), HitTest(40, 5));
  EXPECT_EQ(PositionWithAffinity(Position(ab, 0)), HitTest(45, 5));
  EXPECT_EQ(PositionWithAffinity(Position(ab, 1),
                                 LayoutNG() ? TextAffinity::kDownstream
                                            : TextAffinity::kUpstream),
            HitTest(50, 5));
  EXPECT_EQ(PositionWithAffinity(Position(ab, 1),
                                 LayoutNG() ? TextAffinity::kDownstream
                                            : TextAffinity::kUpstream),
            HitTest(55, 5));
  EXPECT_EQ(PositionWithAffinity(Position(xy, 2), TextAffinity::kUpstream),
            HitTest(60, 5));
  EXPECT_EQ(PositionWithAffinity(Position(xy, 2), TextAffinity::kUpstream),
            HitTest(65, 5));
}

TEST_P(LayoutViewHitTestTest, HitTestHorizontal) {
  LoadAhem();
  SetBodyInnerHTML(R"HTML(
    <style>body { margin: 0 }</style>
    <div id="div" style="position: relative; font: 10px/10px Ahem;
        top: 100px; left: 50px; width: 200px; height: 80px">
      <span id="span1">ABCDE</span><span id="span2"
          style="position: relative; top: 30px">XYZ</span>
    </div>
  )HTML");

  // (50, 100)         (250, 100)
  //   |------------------|
  //   |ABCDE             |
  //   |                  |
  //   |                  |
  //   |     XYZ          |
  //   |                  |
  //   |                  |
  //   |------------------|
  // (50, 180)         (250, 180)
  auto* div = GetDocument().getElementById("div");
  auto* text1 = GetDocument().getElementById("span1")->firstChild();
  auto* text2 = GetDocument().getElementById("span2")->firstChild();

  HitTestResult result;
  // In body, but not in any descendants.
  GetLayoutView().HitTest(HitTestLocation(PhysicalOffset(1, 1)), result);
  EXPECT_EQ(GetDocument().body(), result.InnerNode());
  EXPECT_EQ(PhysicalOffset(1, 1), result.LocalPoint());
  EXPECT_EQ(PositionWithAffinity(Position(text1, 0), TextAffinity::kDownstream),
            result.GetPosition());

  // Top-left corner of div and span1.
  result = HitTestResult();
  GetLayoutView().HitTest(HitTestLocation(PhysicalOffset(51, 101)), result);
  EXPECT_EQ(text1, result.InnerNode());
  EXPECT_EQ(PhysicalOffset(1, 1), result.LocalPoint());
  EXPECT_EQ(PositionWithAffinity(Position(text1, 0), TextAffinity::kDownstream),
            result.GetPosition());

  // Top-right corner (outside) of div.
  result = HitTestResult();
  GetLayoutView().HitTest(HitTestLocation(PhysicalOffset(251, 101)), result);
  EXPECT_EQ(GetDocument().documentElement(), result.InnerNode());
  EXPECT_EQ(PhysicalOffset(251, 101), result.LocalPoint());
  EXPECT_EQ(PositionWithAffinity(Position(text2, 3), TextAffinity::kUpstream),
            result.GetPosition());

  // Top-right corner (inside) of div.
  result = HitTestResult();
  GetLayoutView().HitTest(HitTestLocation(PhysicalOffset(249, 101)), result);
  EXPECT_EQ(div, result.InnerNode());
  EXPECT_EQ(PhysicalOffset(199, 1), result.LocalPoint());
  EXPECT_EQ(PositionWithAffinity(Position(text2, 3), TextAffinity::kUpstream),
            result.GetPosition());

  // Top-right corner (inside) of span1.
  result = HitTestResult();
  GetLayoutView().HitTest(HitTestLocation(PhysicalOffset(99, 101)), result);
  EXPECT_EQ(text1, result.InnerNode());
  EXPECT_EQ(PhysicalOffset(49, 1), result.LocalPoint());
  EXPECT_EQ(PositionWithAffinity(Position(text1, 5), TextAffinity::kUpstream),
            result.GetPosition());

  // Top-right corner (outside) of span1.
  result = HitTestResult();
  GetLayoutView().HitTest(HitTestLocation(PhysicalOffset(101, 101)), result);
  EXPECT_EQ(div, result.InnerNode());
  EXPECT_EQ(PhysicalOffset(51, 1), result.LocalPoint());
  EXPECT_EQ(PositionWithAffinity(Position(text2, 0), TextAffinity::kDownstream),
            result.GetPosition());

  // Bottom-left corner (outside) of div.
  result = HitTestResult();
  GetLayoutView().HitTest(HitTestLocation(PhysicalOffset(51, 181)), result);
  EXPECT_EQ(GetDocument().documentElement(), result.InnerNode());
  EXPECT_EQ(PhysicalOffset(51, 181), result.LocalPoint());
  EXPECT_EQ(
      IsAndroidOrWindowsEditingBehavior()
          ? PositionWithAffinity(Position(text1, 0), TextAffinity::kDownstream)
          : PositionWithAffinity(Position(text2, 3), TextAffinity::kDownstream),
      result.GetPosition());

  // Bottom-left corner (inside) of div.
  result = HitTestResult();
  GetLayoutView().HitTest(HitTestLocation(PhysicalOffset(51, 179)), result);
  EXPECT_EQ(div, result.InnerNode());
  EXPECT_EQ(PhysicalOffset(1, 79), result.LocalPoint());
  EXPECT_EQ(
      IsAndroidOrWindowsEditingBehavior()
          ? PositionWithAffinity(Position(text1, 0), TextAffinity::kDownstream)
          : PositionWithAffinity(Position(text2, 3), TextAffinity::kDownstream),
      result.GetPosition());

  // Bottom-left corner (outside) of span1.
  result = HitTestResult();
  GetLayoutView().HitTest(HitTestLocation(PhysicalOffset(51, 111)), result);
  EXPECT_EQ(div, result.InnerNode());
  EXPECT_EQ(PhysicalOffset(1, 11), result.LocalPoint());
  EXPECT_EQ(
      IsAndroidOrWindowsEditingBehavior()
          ? PositionWithAffinity(Position(text1, 0), TextAffinity::kDownstream)
          : PositionWithAffinity(Position(text2, 3), TextAffinity::kDownstream),
      result.GetPosition());

  // Top-left corner of span2.
  result = HitTestResult();
  GetLayoutView().HitTest(HitTestLocation(PhysicalOffset(101, 131)), result);
  EXPECT_EQ(text2, result.InnerNode());
  if (RuntimeEnabledFeatures::LayoutNGEnabled())
    EXPECT_EQ(PhysicalOffset(51, 31), result.LocalPoint());
  else
    EXPECT_EQ(PhysicalOffset(51, 1), result.LocalPoint());
  EXPECT_EQ(PositionWithAffinity(Position(text2, 0), TextAffinity::kDownstream),
            result.GetPosition());
}

TEST_P(LayoutViewHitTestTest, HitTestVerticalLR) {
  LoadAhem();
  SetBodyInnerHTML(R"HTML(
    <style>body { margin: 0 }</style>
    <div id="div" style="position: relative; font: 10px/10px Ahem;
        top: 100px; left: 50px; width: 200px; height: 80px;
        writing-mode: vertical-lr">
      <span id="span1">ABCDE</span><span id="span2"
          style="position: relative; left: 30px">XYZ</span>
    </div>
  )HTML");

  // (50, 100)         (250, 100)
  //   |------------------|
  //   |A                 |
  //   |B                 |
  //   |C                 |
  //   |D                 |
  //   |E                 |
  //   |   X              |
  //   |   Y              |
  //   |   Z              |
  //   |------------------|
  // (50, 180)         (250, 180)
  auto* div = GetDocument().getElementById("div");
  auto* text1 = GetDocument().getElementById("span1")->firstChild();
  auto* text2 = GetDocument().getElementById("span2")->firstChild();

  HitTestResult result;
  // In body, but not in any descendants.
  GetLayoutView().HitTest(HitTestLocation(PhysicalOffset(1, 1)), result);
  EXPECT_EQ(GetDocument().body(), result.InnerNode());
  EXPECT_EQ(PhysicalOffset(1, 1), result.LocalPoint());
  EXPECT_EQ(PositionWithAffinity(Position(text1, 0), TextAffinity::kDownstream),
            result.GetPosition());

  // Top-left corner of div and span1.
  result = HitTestResult();
  GetLayoutView().HitTest(HitTestLocation(PhysicalOffset(51, 101)), result);
  EXPECT_EQ(text1, result.InnerNode());
  EXPECT_EQ(PhysicalOffset(1, 1), result.LocalPoint());
  EXPECT_EQ(PositionWithAffinity(Position(text1, 0), TextAffinity::kDownstream),
            result.GetPosition());

  // Top-right corner (outside) of div.
  result = HitTestResult();
  GetLayoutView().HitTest(HitTestLocation(PhysicalOffset(251, 101)), result);
  EXPECT_EQ(GetDocument().documentElement(), result.InnerNode());
  EXPECT_EQ(PhysicalOffset(251, 101), result.LocalPoint());
  EXPECT_EQ(
      IsAndroidOrWindowsEditingBehavior()
          ? PositionWithAffinity(Position(text1, 0), TextAffinity::kDownstream)
          : PositionWithAffinity(Position(text2, 3), TextAffinity::kDownstream),
      result.GetPosition());

  // Top-right corner (inside) of div.
  result = HitTestResult();
  GetLayoutView().HitTest(HitTestLocation(PhysicalOffset(249, 101)), result);
  EXPECT_EQ(div, result.InnerNode());
  EXPECT_EQ(PhysicalOffset(199, 1), result.LocalPoint());
  EXPECT_EQ(
      IsAndroidOrWindowsEditingBehavior()
          ? PositionWithAffinity(Position(text1, 0), TextAffinity::kDownstream)
          : PositionWithAffinity(Position(text2, 3), TextAffinity::kDownstream),
      result.GetPosition());

  // Top-right corner (inside) of span1.
  result = HitTestResult();
  GetLayoutView().HitTest(HitTestLocation(PhysicalOffset(59, 101)), result);
  EXPECT_EQ(text1, result.InnerNode());
  EXPECT_EQ(PhysicalOffset(9, 1), result.LocalPoint());
  EXPECT_EQ(PositionWithAffinity(Position(text1, 0), TextAffinity::kDownstream),
            result.GetPosition());

  // Top-right corner (outside) of span1.
  result = HitTestResult();
  GetLayoutView().HitTest(HitTestLocation(PhysicalOffset(61, 101)), result);
  EXPECT_EQ(div, result.InnerNode());
  EXPECT_EQ(PhysicalOffset(11, 1), result.LocalPoint());
  EXPECT_EQ(
      IsAndroidOrWindowsEditingBehavior()
          ? PositionWithAffinity(Position(text1, 0), TextAffinity::kDownstream)
          : PositionWithAffinity(Position(text2, 3), TextAffinity::kDownstream),
      result.GetPosition());

  // Bottom-left corner (outside) of span1.
  result = HitTestResult();
  GetLayoutView().HitTest(HitTestLocation(PhysicalOffset(51, 181)), result);
  EXPECT_EQ(GetDocument().documentElement(), result.InnerNode());
  EXPECT_EQ(PhysicalOffset(51, 181), result.LocalPoint());
  EXPECT_EQ(PositionWithAffinity(Position(text2, 3), TextAffinity::kUpstream),
            result.GetPosition());

  // Bottom-left corner (inside) of span1.
  result = HitTestResult();
  GetLayoutView().HitTest(HitTestLocation(PhysicalOffset(51, 179)), result);
  EXPECT_EQ(div, result.InnerNode());
  EXPECT_EQ(PhysicalOffset(1, 79), result.LocalPoint());
  EXPECT_EQ(PositionWithAffinity(Position(text2, 3), TextAffinity::kUpstream),
            result.GetPosition());

  // Top-left corner of span2.
  result = HitTestResult();
  GetLayoutView().HitTest(HitTestLocation(PhysicalOffset(81, 151)), result);
  EXPECT_EQ(text2, result.InnerNode());
  if (RuntimeEnabledFeatures::LayoutNGEnabled())
    EXPECT_EQ(PhysicalOffset(31, 51), result.LocalPoint());
  else
    EXPECT_EQ(PhysicalOffset(1, 51), result.LocalPoint());
  EXPECT_EQ(PositionWithAffinity(Position(text2, 0), TextAffinity::kDownstream),
            result.GetPosition());
}

TEST_P(LayoutViewHitTestTest, HitTestVerticalRL) {
  LoadAhem();
  SetBodyInnerHTML(R"HTML(
    <style>body { margin: 0 }</style>
    <div id="div" style="position: relative; font: 10px/10px Ahem;
        top: 100px; left: 50px; width: 200px; height: 80px;
        writing-mode: vertical-rl">
      <span id="span1">ABCDE</span><span id="span2"
          style="position: relative; left: -30px">XYZ</span>
    </div>
  )HTML");

  // (50, 100)         (250, 100)
  //   |------------------|
  //   |                 A|
  //   |                 B|
  //   |                 C|
  //   |                 D|
  //   |                 E|
  //   |              X   |
  //   |              Y   |
  //   |              Z   |
  //   |------------------|
  // (50, 180)         (250, 180)
  auto* div = GetDocument().getElementById("div");
  auto* text1 = GetDocument().getElementById("span1")->firstChild();
  auto* text2 = GetDocument().getElementById("span2")->firstChild();

  HitTestResult result;
  // In body, but not in any descendants.
  // XXX1
  GetLayoutView().HitTest(HitTestLocation(PhysicalOffset(1, 1)), result);
  EXPECT_EQ(GetDocument().body(), result.InnerNode());
  EXPECT_EQ(PhysicalOffset(1, 1), result.LocalPoint());
  EXPECT_EQ(
      IsAndroidOrWindowsEditingBehavior()
          ? PositionWithAffinity(Position(text1, 0), TextAffinity::kDownstream)
          : PositionWithAffinity(Position(text2, 3), TextAffinity::kDownstream),
      result.GetPosition());

  // Top-left corner of div.
  result = HitTestResult();
  GetLayoutView().HitTest(HitTestLocation(PhysicalOffset(51, 101)), result);
  EXPECT_EQ(div, result.InnerNode());
  EXPECT_EQ(PhysicalOffset(1, 1), result.LocalPoint());
  EXPECT_EQ(
      IsAndroidOrWindowsEditingBehavior()
          ? PositionWithAffinity(Position(text1, 0), TextAffinity::kDownstream)
          : PositionWithAffinity(Position(text2, 3), TextAffinity::kDownstream),
      result.GetPosition());

  // Top-right corner (outside) of div.
  result = HitTestResult();
  GetLayoutView().HitTest(HitTestLocation(PhysicalOffset(251, 101)), result);
  EXPECT_EQ(GetDocument().documentElement(), result.InnerNode());
  EXPECT_EQ(PhysicalOffset(251, 101), result.LocalPoint());
  EXPECT_EQ(PositionWithAffinity(Position(text1, 0), TextAffinity::kDownstream),
            result.GetPosition());

  // Top-right corner (inside) of div and span1.
  result = HitTestResult();
  GetLayoutView().HitTest(HitTestLocation(PhysicalOffset(249, 101)), result);
  EXPECT_EQ(text1, result.InnerNode());
  EXPECT_EQ(PhysicalOffset(199, 1), result.LocalPoint());
  EXPECT_EQ(PositionWithAffinity(Position(text1, 0), TextAffinity::kDownstream),
            result.GetPosition());

  // Bottom-right corner (inside) of span1.
  result = HitTestResult();
  GetLayoutView().HitTest(HitTestLocation(PhysicalOffset(249, 149)), result);
  EXPECT_EQ(text1, result.InnerNode());
  EXPECT_EQ(PhysicalOffset(199, 49), result.LocalPoint());
  EXPECT_EQ(PositionWithAffinity(Position(text1, 5), TextAffinity::kUpstream),
            result.GetPosition());

  // Bottom-right corner (outside) of span1 but inside of div.
  result = HitTestResult();
  GetLayoutView().HitTest(HitTestLocation(PhysicalOffset(249, 151)), result);
  EXPECT_EQ(div, result.InnerNode());
  EXPECT_EQ(PhysicalOffset(199, 51), result.LocalPoint());
  EXPECT_EQ(PositionWithAffinity(Position(text2, 0), TextAffinity::kDownstream),
            result.GetPosition());

  // Bottom-left corner (outside) of div.
  // XXX2
  result = HitTestResult();
  GetLayoutView().HitTest(HitTestLocation(PhysicalOffset(51, 181)), result);
  EXPECT_EQ(GetDocument().documentElement(), result.InnerNode());
  EXPECT_EQ(PhysicalOffset(51, 181), result.LocalPoint());
  EXPECT_EQ(
      IsAndroidOrWindowsEditingBehavior()
          ? PositionWithAffinity(Position(text2, 3), TextAffinity::kUpstream)
          : PositionWithAffinity(Position(text2, 3), TextAffinity::kDownstream),
      result.GetPosition());

  // Bottom-left corner (inside) of div.
  result = HitTestResult();
  GetLayoutView().HitTest(HitTestLocation(PhysicalOffset(51, 179)), result);
  EXPECT_EQ(div, result.InnerNode());
  EXPECT_EQ(PhysicalOffset(1, 79), result.LocalPoint());
  EXPECT_EQ(
      IsAndroidOrWindowsEditingBehavior()
          ? PositionWithAffinity(Position(text2, 3), TextAffinity::kUpstream)
          : PositionWithAffinity(Position(text2, 3), TextAffinity::kDownstream),
      result.GetPosition());

  // Bottom-left corner (outside) of span1.
  result = HitTestResult();
  GetLayoutView().HitTest(HitTestLocation(PhysicalOffset(241, 151)), result);
  EXPECT_EQ(div, result.InnerNode());
  EXPECT_EQ(PhysicalOffset(191, 51), result.LocalPoint());
  EXPECT_EQ(PositionWithAffinity(Position(text2, 0), TextAffinity::kDownstream),
            result.GetPosition());

  // Top-right corner (inside) of span2.
  result = HitTestResult();
  GetLayoutView().HitTest(HitTestLocation(PhysicalOffset(219, 151)), result);
  EXPECT_EQ(text2, result.InnerNode());
  if (RuntimeEnabledFeatures::LayoutNGEnabled())
    EXPECT_EQ(PhysicalOffset(169, 51), result.LocalPoint());
  else
    EXPECT_EQ(PhysicalOffset(199, 51), result.LocalPoint());
  EXPECT_EQ(PositionWithAffinity(Position(text2, 0), TextAffinity::kDownstream),
            result.GetPosition());
}

TEST_P(LayoutViewHitTestTest, HitTestVerticalRLRoot) {
  LoadAhem();
  SetBodyInnerHTML(R"HTML(
    <style>
      html { writing-mode: vertical-rl; }
      body { margin: 0 }
    </style>
    <div id="div" style="font: 10px/10px Ahem; width: 200px; height: 80px">
      <span id="span">ABCDE</span>
    </div>
  )HTML");

  // (0,0)     (600, 0)         (800, 0)
  // +----...----+---------------+
  // |           |              A|
  // |           |              B|
  // |           |              C|
  // |           |     (div)    D|
  // | (screen)  |              E|
  // |           |               |
  // |           |               |
  // |           +---------------+ (800, 80)
  // |       (600, 80)           |
  // .                           .
  // +----...--------------------+ (800, 600)

  auto* div = GetDocument().getElementById("div");
  auto* text = GetDocument().getElementById("span")->firstChild();
  HitTestResult result;
  // Not in any element. Should fallback to documentElement.
  GetLayoutView().HitTest(HitTestLocation(PhysicalOffset(1, 1)), result);
  EXPECT_EQ(GetDocument().documentElement(), result.InnerNode());
  EXPECT_EQ(PhysicalOffset(-599, 1), result.LocalPoint());
  EXPECT_EQ(
      IsAndroidOrWindowsEditingBehavior()
          ? PositionWithAffinity(Position(text, 0), TextAffinity::kDownstream)
          : PositionWithAffinity(Position(text, 5), TextAffinity::kDownstream),
      result.GetPosition());

  // Top-left corner (inside) of div.
  result = HitTestResult();
  GetLayoutView().HitTest(HitTestLocation(PhysicalOffset(601, 1)), result);
  EXPECT_EQ(div, result.InnerNode());
  EXPECT_EQ(PhysicalOffset(1, 1), result.LocalPoint());
  EXPECT_EQ(
      IsAndroidOrWindowsEditingBehavior()
          ? PositionWithAffinity(Position(text, 0), TextAffinity::kDownstream)
          : PositionWithAffinity(Position(text, 5), TextAffinity::kDownstream),
      result.GetPosition());

  // Top-right corner (outside) of div. Should fallback to documentElement.
  result = HitTestResult();
  GetLayoutView().HitTest(HitTestLocation(PhysicalOffset(801, 1)), result);
  EXPECT_EQ(GetDocument().documentElement(), result.InnerNode());
  EXPECT_EQ(PhysicalOffset(201, 1), result.LocalPoint());
  EXPECT_EQ(
      IsAndroidOrWindowsEditingBehavior()
          ? PositionWithAffinity(Position(text, 0), TextAffinity::kDownstream)
          : PositionWithAffinity(Position(text, 0), TextAffinity::kDownstream),
      result.GetPosition());

  // Top-right corner (inside) of div and span.
  result = HitTestResult();
  GetLayoutView().HitTest(HitTestLocation(PhysicalOffset(799, 1)), result);
  EXPECT_EQ(text, result.InnerNode());
  EXPECT_EQ(PhysicalOffset(199, 1), result.LocalPoint());
  EXPECT_EQ(PositionWithAffinity(Position(text, 0), TextAffinity::kDownstream),
            result.GetPosition());

  // Bottom-right corner (outside) of span1 but inside of div.
  result = HitTestResult();
  GetLayoutView().HitTest(HitTestLocation(PhysicalOffset(799, 51)), result);
  EXPECT_EQ(div, result.InnerNode());
  EXPECT_EQ(PhysicalOffset(199, 51), result.LocalPoint());
  EXPECT_EQ(PositionWithAffinity(Position(text, 5), TextAffinity::kUpstream),
            result.GetPosition());

  // Bottom-left corner (outside) of div.
  result = HitTestResult();
  GetLayoutView().HitTest(HitTestLocation(PhysicalOffset(599, 81)), result);
  EXPECT_EQ(GetDocument().documentElement(), result.InnerNode());
  EXPECT_EQ(PhysicalOffset(-1, 81), result.LocalPoint());
  EXPECT_EQ(
      IsAndroidOrWindowsEditingBehavior()
          ? PositionWithAffinity(Position(text, 5), TextAffinity::kUpstream)
          : PositionWithAffinity(Position(text, 5), TextAffinity::kDownstream),
      result.GetPosition());
}

// http://crbug.com/1164974
TEST_P(LayoutViewHitTestTest, PseudoElementAfterBlock) {
  LoadAhem();
  InsertStyleElement(
      "body { margin: 0px; font: 10px/15px Ahem; }"
      "p::after { content: 'XY' }");
  SetBodyInnerHTML("<div><p id=target>ab</p></div>");
  const auto& text_ab = *To<Text>(GetElementById("target")->firstChild());
  // In legacy layout, this position comes from |LayoutBlock::PositionBox()|
  // for mac/unix, or |LayoutObject::FindPosition()| on android/windows.
  const auto expected = PositionWithAffinity(
      IsAndroidOrWindowsEditingBehavior() ? Position(text_ab, 2)
                                          : Position(text_ab, 0),
      LayoutNG() && IsAndroidOrWindowsEditingBehavior()
          ? TextAffinity::kUpstream
          : TextAffinity::kDownstream);

  EXPECT_EQ(expected, HitTest(20, 5)) << "after ab";
  EXPECT_EQ(expected, HitTest(25, 5)) << "at X";
  EXPECT_EQ(expected, HitTest(35, 5)) << "at Y";
  EXPECT_EQ(expected, HitTest(40, 5)) << "after Y";
  EXPECT_EQ(expected, HitTest(50, 5)) << "after XY";
}

// http://crbug.com/1043471
TEST_P(LayoutViewHitTestTest, PseudoElementAfterInline) {
  LoadAhem();
  InsertStyleElement(
      "body { margin: 0px; font: 10px/10px Ahem; }"
      "#cd::after { content: 'XYZ'; margin-left: 100px; }");
  SetBodyInnerHTML("<div id=ab>ab<span id=cd>cd</span></div>");
  const auto& text_ab = *To<Text>(GetElementById("ab")->firstChild());
  const auto& text_cd = *To<Text>(GetElementById("cd")->lastChild());

  EXPECT_EQ(PositionWithAffinity(Position(text_ab, 0)), HitTest(5, 5));
  // Because of hit testing at "b", position should be |kDownstream|.
  EXPECT_EQ(PositionWithAffinity(Position(text_ab, 1),
                                 LayoutNG() ? TextAffinity::kDownstream
                                            : TextAffinity::kUpstream),
            HitTest(15, 5));
  EXPECT_EQ(PositionWithAffinity(Position(text_cd, 0)), HitTest(25, 5));
  // Because of hit testing at "d", position should be |kDownstream|.
  EXPECT_EQ(PositionWithAffinity(Position(text_cd, 1),
                                 LayoutNG() ? TextAffinity::kDownstream
                                            : TextAffinity::kUpstream),
            HitTest(35, 5));
  // Because of hit testing at right of <span cd>, result position should be
  // |kUpstream|.
  EXPECT_EQ(PositionWithAffinity(Position(text_cd, 2),
                                 LayoutNG() ? TextAffinity::kUpstream
                                            : TextAffinity::kDownstream),
            HitTest(45, 5));
  EXPECT_EQ(PositionWithAffinity(Position(text_cd, 2),
                                 LayoutNG() ? TextAffinity::kUpstream
                                            : TextAffinity::kDownstream),
            HitTest(55, 5));
  EXPECT_EQ(PositionWithAffinity(Position(text_cd, 2),
                                 LayoutNG() ? TextAffinity::kUpstream
                                            : TextAffinity::kDownstream),
            HitTest(65, 5));
}

TEST_P(LayoutViewHitTestTest, PseudoElementAfterBlockWithMargin) {
  LoadAhem();
  InsertStyleElement(
      "body { margin: 0px; font: 10px/15px Ahem; }"
      "p::after { content: 'XY'; margin-left: 10px;}");
  SetBodyInnerHTML("<div><p id=target>ab</p></div>");
  const auto& text_ab = *To<Text>(GetElementById("target")->firstChild());
  // In legacy layout, this position comes from |LayoutBlock::PositionBox()|
  // for mac/unix, or |LayoutObject::FindPosition()| on android/windows.
  const auto expected = PositionWithAffinity(
      IsAndroidOrWindowsEditingBehavior() ? Position(text_ab, 2)
                                          : Position(text_ab, 0),
      LayoutNG() && IsAndroidOrWindowsEditingBehavior()
          ? TextAffinity::kUpstream
          : TextAffinity::kDownstream);

  EXPECT_EQ(expected, HitTest(20, 5)) << "after ab";
  EXPECT_EQ(expected, HitTest(25, 5)) << "at margin-left";
  EXPECT_EQ(expected, HitTest(30, 5)) << "before X";
  EXPECT_EQ(expected, HitTest(35, 5)) << "at X";
  EXPECT_EQ(expected, HitTest(45, 5)) << "at Y";
  EXPECT_EQ(expected, HitTest(50, 5)) << "after Y";
  EXPECT_EQ(expected, HitTest(55, 5)) << "after XY";
}

// http://crbug.com/1268782
TEST_P(LayoutViewHitTestTest, ScrolledBlockChildren) {
  LoadAhem();
  InsertStyleElement(R"CSS(
    body {
      margin: 0px;
      font: 10px/15px Ahem;
    }
    #sample { height: 60px; overflow: scroll; }
  )CSS");
  SetBodyInnerHTML(
      "<div id=sample>"
      "0<br>1<br>2<br><b id=four>4</b><br><b id=five>5</b><br>"
      "<div id=x>X</div>"
      "<b id=six>6</b><br>7<br>8<br>9<br>"
      "</div>");

  Element& sample = *GetElementById("sample");
  sample.scrollTo(0, 45);

  const auto& text_4 = *To<Text>(GetElementById("four")->firstChild());
  const auto& text_5 = *To<Text>(GetElementById("five")->firstChild());
  const auto& text_6 = *To<Text>(GetElementById("six")->firstChild());
  const auto& text_x = *To<Text>(GetElementById("x")->firstChild());

  EXPECT_EQ(PositionWithAffinity(Position(text_4, 1), TextAffinity::kUpstream),
            HitTest(15, 5));
  EXPECT_EQ(PositionWithAffinity(Position(text_5, 1), TextAffinity::kUpstream),
            HitTest(15, 15));
  EXPECT_EQ(PositionWithAffinity(Position(text_5, 1), TextAffinity::kUpstream),
            HitTest(15, 25));
  EXPECT_EQ(PositionWithAffinity(Position(text_x, 1), TextAffinity::kUpstream),
            HitTest(15, 35));
  if (LayoutNG()) {
    EXPECT_EQ(
        PositionWithAffinity(Position(text_6, 1), TextAffinity::kUpstream),
        HitTest(15, 45));
  } else if (IsAndroidOrWindowsEditingBehavior()) {
    EXPECT_EQ(
        PositionWithAffinity(Position(text_6, 1), TextAffinity::kUpstream),
        HitTest(15, 45));
  } else {
    EXPECT_EQ(
        PositionWithAffinity(Position(text_6, 0), TextAffinity::kDownstream),
        HitTest(15, 45));
  }
  EXPECT_EQ(PositionWithAffinity(Position(text_6, 1), TextAffinity::kUpstream),
            HitTest(15, 55));
}

// See also "editing/selection/click-in-padding-with-multiple-line-boxes.html"
TEST_P(LayoutViewHitTestTest, ScrolledInlineChildren) {
  LoadAhem();
  InsertStyleElement(R"CSS(
    body {
      margin: 0px;
      font: 10px/15px Ahem;
    }
    #sample {
      overflow: scroll;
      padding-bottom: 10px;
      padding-top: 10px;
      white-space: nowrap;
      width: 60px;
    }
  )CSS");
  SetBodyInnerHTML("<div id=sample>012345678</div>");

  Element& sample = *GetElementById("sample");
  sample.scrollTo(20, 0);

  const auto& text = *To<Text>(sample.firstChild());

  if (LayoutNG()) {
    if (IsAndroidOrWindowsEditingBehavior()) {
      EXPECT_EQ(PositionWithAffinity(Position(text, 2)), HitTest(5, 5));
      EXPECT_EQ(PositionWithAffinity(Position(text, 2)), HitTest(5, 15));
      EXPECT_EQ(PositionWithAffinity(Position(text, 2)), HitTest(5, 25));

      EXPECT_EQ(PositionWithAffinity(Position(text, 3)), HitTest(15, 5));
      EXPECT_EQ(PositionWithAffinity(Position(text, 3)), HitTest(15, 15));
      EXPECT_EQ(PositionWithAffinity(Position(text, 3)), HitTest(15, 25));

      EXPECT_EQ(PositionWithAffinity(Position(text, 4)), HitTest(25, 5));
      EXPECT_EQ(PositionWithAffinity(Position(text, 4)), HitTest(25, 15));
      EXPECT_EQ(PositionWithAffinity(Position(text, 4)), HitTest(25, 25));

      EXPECT_EQ(PositionWithAffinity(Position(text, 5)), HitTest(35, 5));
      EXPECT_EQ(PositionWithAffinity(Position(text, 5)), HitTest(35, 15));
      EXPECT_EQ(PositionWithAffinity(Position(text, 5)), HitTest(35, 25));

      EXPECT_EQ(PositionWithAffinity(Position(text, 6)), HitTest(45, 5));
      EXPECT_EQ(PositionWithAffinity(Position(text, 6)), HitTest(45, 15));
      EXPECT_EQ(PositionWithAffinity(Position(text, 6)), HitTest(45, 25));

      EXPECT_EQ(PositionWithAffinity(Position(text, 7)), HitTest(55, 5));
      EXPECT_EQ(PositionWithAffinity(Position(text, 7)), HitTest(55, 15));
      EXPECT_EQ(PositionWithAffinity(Position(text, 7)), HitTest(55, 25));
    } else {
      EXPECT_EQ(PositionWithAffinity(Position(text, 0)), HitTest(5, 5));
      EXPECT_EQ(PositionWithAffinity(Position(text, 2)), HitTest(5, 15));
      EXPECT_EQ(PositionWithAffinity(Position(text, 9)), HitTest(5, 25));

      EXPECT_EQ(PositionWithAffinity(Position(text, 0)), HitTest(15, 5));
      EXPECT_EQ(PositionWithAffinity(Position(text, 3)), HitTest(15, 15));
      EXPECT_EQ(PositionWithAffinity(Position(text, 9)), HitTest(15, 25));

      EXPECT_EQ(PositionWithAffinity(Position(text, 0)), HitTest(25, 5));
      EXPECT_EQ(PositionWithAffinity(Position(text, 4)), HitTest(25, 15));
      EXPECT_EQ(PositionWithAffinity(Position(text, 9)), HitTest(25, 25));

      EXPECT_EQ(PositionWithAffinity(Position(text, 0)), HitTest(35, 5));
      EXPECT_EQ(PositionWithAffinity(Position(text, 5)), HitTest(35, 15));
      EXPECT_EQ(PositionWithAffinity(Position(text, 9)), HitTest(35, 25));

      EXPECT_EQ(PositionWithAffinity(Position(text, 0)), HitTest(45, 5));
      EXPECT_EQ(PositionWithAffinity(Position(text, 6)), HitTest(45, 15));
      EXPECT_EQ(PositionWithAffinity(Position(text, 9)), HitTest(45, 25));

      EXPECT_EQ(PositionWithAffinity(Position(text, 0)), HitTest(55, 5));
      EXPECT_EQ(PositionWithAffinity(Position(text, 7)), HitTest(55, 15));
      EXPECT_EQ(PositionWithAffinity(Position(text, 9)), HitTest(55, 25));
    }
  } else {
    if (IsAndroidOrWindowsEditingBehavior()) {
      EXPECT_EQ(
          PositionWithAffinity(Position(text, 2), TextAffinity::kUpstream),
          HitTest(5, 5));
      EXPECT_EQ(
          PositionWithAffinity(Position(text, 2), TextAffinity::kUpstream),
          HitTest(5, 15));
      EXPECT_EQ(
          PositionWithAffinity(Position(text, 2), TextAffinity::kUpstream),
          HitTest(5, 25));

      EXPECT_EQ(
          PositionWithAffinity(Position(text, 3), TextAffinity::kUpstream),
          HitTest(15, 5));
      EXPECT_EQ(
          PositionWithAffinity(Position(text, 3), TextAffinity::kUpstream),
          HitTest(15, 15));
      EXPECT_EQ(
          PositionWithAffinity(Position(text, 3), TextAffinity::kUpstream),
          HitTest(15, 25));

      EXPECT_EQ(
          PositionWithAffinity(Position(text, 4), TextAffinity::kUpstream),
          HitTest(25, 5));
      EXPECT_EQ(
          PositionWithAffinity(Position(text, 4), TextAffinity::kUpstream),
          HitTest(25, 15));
      EXPECT_EQ(
          PositionWithAffinity(Position(text, 4), TextAffinity::kUpstream),
          HitTest(25, 25));

      EXPECT_EQ(
          PositionWithAffinity(Position(text, 5), TextAffinity::kUpstream),
          HitTest(35, 5));
      EXPECT_EQ(
          PositionWithAffinity(Position(text, 5), TextAffinity::kUpstream),
          HitTest(35, 15));
      EXPECT_EQ(
          PositionWithAffinity(Position(text, 5), TextAffinity::kUpstream),
          HitTest(35, 25));

      EXPECT_EQ(
          PositionWithAffinity(Position(text, 6), TextAffinity::kUpstream),
          HitTest(45, 5));
      EXPECT_EQ(
          PositionWithAffinity(Position(text, 6), TextAffinity::kUpstream),
          HitTest(45, 15));
      EXPECT_EQ(
          PositionWithAffinity(Position(text, 6), TextAffinity::kUpstream),
          HitTest(45, 25));

      EXPECT_EQ(
          PositionWithAffinity(Position(text, 7), TextAffinity::kUpstream),
          HitTest(55, 5));
      EXPECT_EQ(
          PositionWithAffinity(Position(text, 7), TextAffinity::kUpstream),
          HitTest(55, 15));
      EXPECT_EQ(
          PositionWithAffinity(Position(text, 7), TextAffinity::kUpstream),
          HitTest(55, 25));
    } else {
      EXPECT_EQ(PositionWithAffinity(Position(text, 0)), HitTest(5, 5));
      EXPECT_EQ(
          PositionWithAffinity(Position(text, 2), TextAffinity::kUpstream),
          HitTest(5, 15));
      EXPECT_EQ(PositionWithAffinity(Position(text, 9)), HitTest(5, 25));

      EXPECT_EQ(PositionWithAffinity(Position(text, 0)), HitTest(15, 5));
      EXPECT_EQ(
          PositionWithAffinity(Position(text, 3), TextAffinity::kUpstream),
          HitTest(15, 15));
      EXPECT_EQ(PositionWithAffinity(Position(text, 9)), HitTest(15, 25));

      EXPECT_EQ(PositionWithAffinity(Position(text, 0)), HitTest(25, 5));
      EXPECT_EQ(
          PositionWithAffinity(Position(text, 4), TextAffinity::kUpstream),
          HitTest(25, 15));
      EXPECT_EQ(PositionWithAffinity(Position(text, 9)), HitTest(25, 25));

      EXPECT_EQ(PositionWithAffinity(Position(text, 0)), HitTest(35, 5));
      EXPECT_EQ(
          PositionWithAffinity(Position(text, 5), TextAffinity::kUpstream),
          HitTest(35, 15));
      EXPECT_EQ(PositionWithAffinity(Position(text, 9)), HitTest(35, 25));

      EXPECT_EQ(PositionWithAffinity(Position(text, 0)), HitTest(45, 5));
      EXPECT_EQ(
          PositionWithAffinity(Position(text, 6), TextAffinity::kUpstream),
          HitTest(45, 15));
      EXPECT_EQ(PositionWithAffinity(Position(text, 9)), HitTest(45, 25));

      EXPECT_EQ(PositionWithAffinity(Position(text, 0)), HitTest(55, 5));
      EXPECT_EQ(
          PositionWithAffinity(Position(text, 7), TextAffinity::kUpstream),
          HitTest(55, 15));
      EXPECT_EQ(PositionWithAffinity(Position(text, 9)), HitTest(55, 25));
    }
  }
}

TEST_P(LayoutViewHitTestTest, TextAndInputsWithRtlDirection) {
  LoadAhem();
  InsertStyleElement(R"CSS(
    body {
      margin: 0 auto 0 0;
      direction: rtl;
      width: 200px;
      font: 50px/1 Ahem;
    }
    input {
      width: 100px;
      height: 50px;
      box-sizing: border-box;
      vertical-align: top;
    }
  )CSS");
  SetBodyInnerHTML("ab<input><input>cd");

  Element* body = GetDocument().body();
  Node* text_ab = body->firstChild();
  Node* input_1 = text_ab->nextSibling();
  Node* input_2 = input_1->nextSibling();
  Node* text_cd = input_2->nextSibling();
  Node* shadow_div_1 = input_1->GetShadowRoot()->firstChild();
  Node* shadow_div_2 = input_2->GetShadowRoot()->firstChild();
  TextAffinity downstream_if_ng =
      LayoutNG() ? TextAffinity::kDownstream : TextAffinity::kUpstream;

  // Note: This is a crash test. The expectations only reflect the current
  // behavior, which may change.
  for (int y : {0, 25, 49}) {
    for (int x : {0, 25}) {
      EXPECT_EQ(PositionWithAffinity(Position::AfterNode(*input_1),
                                     LayoutNG() ? TextAffinity::kUpstream
                                                : TextAffinity::kDownstream),
                HitTest(x, y));
    }
    for (int x : {26, 50, 75}) {
      EXPECT_EQ(PositionWithAffinity(Position(text_ab, 1), downstream_if_ng),
                HitTest(x, y));
    }
    for (int x : {76, 99}) {
      EXPECT_EQ(
          PositionWithAffinity(Position(text_ab, 2), TextAffinity::kUpstream),
          HitTest(x, y));
    }
    for (int x : {100, 125, 150, 175, 199}) {
      EXPECT_EQ(PositionWithAffinity(Position(shadow_div_1, 0)), HitTest(x, y));
    }
    EXPECT_EQ(PositionWithAffinity(Position::AfterNode(*input_1)),
              HitTest(200, y));
  }
  for (int y : {50, 75, 99}) {
    for (int x : {0, 25, 50, 75, 99}) {
      EXPECT_EQ(PositionWithAffinity(Position(shadow_div_2, 0)), HitTest(x, y));
    }
    for (int x : {100, 125}) {
      EXPECT_EQ(PositionWithAffinity(Position(text_cd, 0)), HitTest(x, y));
    }
    for (int x : {126, 150, 175}) {
      EXPECT_EQ(PositionWithAffinity(Position(text_cd, 1), downstream_if_ng),
                HitTest(x, y));
    }
    for (int x : {176, 200}) {
      EXPECT_EQ(PositionWithAffinity(LayoutNG() ? Position::BeforeNode(*input_2)
                                                : Position(input_2, 0)),
                HitTest(x, y));
    }
  }
  if (IsAndroidOrWindowsEditingBehavior()) {
    for (int x : {0, 25, 50, 75, 99}) {
      EXPECT_EQ(PositionWithAffinity(Position::AfterNode(*input_2)),
                HitTest(x, 100));
    }
    for (int x : {100, 125}) {
      EXPECT_EQ(PositionWithAffinity(Position(text_cd, 0)), HitTest(x, 100));
    }
    for (int x : {126, 150, 175}) {
      EXPECT_EQ(PositionWithAffinity(Position(text_cd, 1), downstream_if_ng),
                HitTest(x, 100));
    }
    for (int x : {176, 200}) {
      EXPECT_EQ(PositionWithAffinity(LayoutNG() ? Position::BeforeNode(*input_2)
                                                : Position(input_2, 0)),
                HitTest(x, 100));
    }
  } else {
    for (int x : {0, 25, 50, 75, 100, 125, 150, 175, 200}) {
      EXPECT_EQ(PositionWithAffinity(LayoutNG() ? Position::AfterNode(*input_2)
                                                : Position(text_cd, 2)),
                HitTest(x, 100));
    }
  }
}

TEST_P(LayoutViewHitTestTest, TextCombineOneTextNode) {
  ScopedLayoutNGForTest enable_layout_ng(true);
  LoadAhem();
  InsertStyleElement(
      "body { margin: 0px; font: 100px/110px Ahem; }"
      "c { text-combine-upright: all; }"
      "div { writing-mode: vertical-rl; }");
  SetBodyInnerHTML("<div>a<c id=target>01234</c>b</div>");
  //  LayoutNGBlockFlow {HTML} at (0,0) size 800x600
  //    LayoutNGBlockFlow {BODY} at (0,0) size 800x600
  //      LayoutNGBlockFlow {DIV} at (0,0) size 110x300
  //        LayoutText {#text} at (5,0) size 100x100
  //          text run at (5,0) width 100: "a"
  //        LayoutInline {C} at (5,100) size 100x100
  //          LayoutNGTextCombine (anonymous) at (5,100) size 100x100
  //            LayoutText {#text} at (-5,0) size 110x100
  //              text run at (0,0) width 500: "01234"
  //        LayoutText {#text} at (5,200) size 100x100
  //          text run at (5,200) width 100: "b"
  const auto& target = *GetElementById("target");
  const auto& text_01234 = *To<Text>(target.firstChild());
  const auto& text_a = *To<Text>(target.previousSibling());
  const auto& text_b = *To<Text>(target.nextSibling());

  if (LayoutNG()) {
    EXPECT_EQ(PositionWithAffinity(Position(text_01234, 0)), HitTest(0, 150));
    EXPECT_EQ(PositionWithAffinity(Position(text_01234, 0)), HitTest(10, 150));
    EXPECT_EQ(PositionWithAffinity(Position(text_01234, 1)), HitTest(20, 150));
    EXPECT_EQ(PositionWithAffinity(Position(text_01234, 1)), HitTest(30, 150));
    EXPECT_EQ(PositionWithAffinity(Position(text_01234, 2)), HitTest(40, 150));
    EXPECT_EQ(PositionWithAffinity(Position(text_01234, 2)), HitTest(50, 150));
    EXPECT_EQ(PositionWithAffinity(Position(text_01234, 3)), HitTest(60, 150));
    EXPECT_EQ(PositionWithAffinity(Position(text_01234, 3)), HitTest(70, 150));
    EXPECT_EQ(PositionWithAffinity(Position(text_01234, 4)), HitTest(80, 150));
    EXPECT_EQ(PositionWithAffinity(Position(text_01234, 4)), HitTest(90, 150));
    EXPECT_EQ(
        PositionWithAffinity(Position(text_01234, 5), TextAffinity::kUpstream),
        HitTest(100, 150));
    // TODO(yosin): should be text_01234@5
    if (IsAndroidOrWindowsEditingBehavior()) {
      EXPECT_EQ(PositionWithAffinity(Position(text_b, 0)), HitTest(110, 150));
      EXPECT_EQ(PositionWithAffinity(Position(text_b, 0)), HitTest(120, 150));
    } else {
      EXPECT_EQ(PositionWithAffinity(Position(text_a, 0)), HitTest(110, 150));
      EXPECT_EQ(PositionWithAffinity(Position(text_a, 0)), HitTest(120, 150));
    }
  } else {
    // Note: Hit test for legacy layout is broken. This is just record of
    // current behavior.
    if (IsAndroidOrWindowsEditingBehavior())
      EXPECT_EQ(PositionWithAffinity(Position(text_01234, 0)), HitTest(0, 150));
    else
      EXPECT_EQ(PositionWithAffinity(Position(text_b, 1)), HitTest(0, 150));
    EXPECT_EQ(PositionWithAffinity(Position(text_01234, 0)), HitTest(10, 150));
    EXPECT_EQ(PositionWithAffinity(Position(text_01234, 0)), HitTest(20, 150));
    EXPECT_EQ(PositionWithAffinity(Position(text_01234, 0)), HitTest(30, 150));
    EXPECT_EQ(PositionWithAffinity(Position(text_01234, 0)), HitTest(40, 150));
    EXPECT_EQ(PositionWithAffinity(Position(text_01234, 0)), HitTest(50, 150));
    EXPECT_EQ(PositionWithAffinity(Position(text_01234, 0)), HitTest(60, 150));
    EXPECT_EQ(PositionWithAffinity(Position(text_01234, 0)), HitTest(70, 150));
    EXPECT_EQ(PositionWithAffinity(Position(text_01234, 0)), HitTest(80, 150));
    EXPECT_EQ(PositionWithAffinity(Position(text_01234, 0)), HitTest(90, 150));
    EXPECT_EQ(PositionWithAffinity(Position(text_01234, 0)), HitTest(100, 150));
    if (IsAndroidOrWindowsEditingBehavior()) {
      EXPECT_EQ(PositionWithAffinity(Position(text_01234, 0)),
                HitTest(110, 150));
      EXPECT_EQ(PositionWithAffinity(Position(text_01234, 0)),
                HitTest(120, 150));
    } else {
      EXPECT_EQ(PositionWithAffinity(Position(text_a, 0)), HitTest(110, 150));
      EXPECT_EQ(PositionWithAffinity(Position(text_a, 0)), HitTest(120, 150));
    }
  }
}

TEST_P(LayoutViewHitTestTest, TextCombineTwoTextNodes) {
  ScopedLayoutNGForTest enable_layout_ng(true);
  LoadAhem();
  InsertStyleElement(
      "body { margin: 0px; font: 100px/110px Ahem; }"
      "c { text-combine-upright: all; }"
      "div { writing-mode: vertical-rl; }");
  SetBodyInnerHTML("<div>a<c id=target>012<wbr>34</c>b</div>");
  //   LayoutNGBlockFlow {HTML} at (0,0) size 800x600
  //     LayoutNGBlockFlow {BODY} at (0,0) size 800x600
  //       LayoutNGBlockFlow {DIV} at (0,0) size 110x300
  //         LayoutText {#text} at (5,0) size 100x100
  //           text run at (5,0) width 100: "a"
  //         LayoutInline {C} at (5,100) size 100x100
  //           LayoutNGTextCombine (anonymous) at (5,100) size 100x100
  //             LayoutText {#text} at (-5,0) size 66x100
  //               text run at (0,0) width 300: "012"
  //             LayoutWordBreak {WBR} at (61,0) size 0x100
  //               text run at (300,0) width 0: "\x{200B}"
  //             LayoutText {#text} at (61,0) size 44x100
  //               text run at (300,0) width 200: "34"
  //         LayoutInline {B} at (5,200) size 100x100
  //           LayoutText {#text} at (5,200) size 100x100
  //             text run at (5,200) width 100: "b"
  //   const auto& target = *GetElementById("target");
  const auto& target = *GetElementById("target");
  const auto& text_012 = *To<Text>(target.firstChild());
  const auto& text_34 = *To<Text>(target.lastChild());
  const auto& text_a = *To<Text>(target.previousSibling());
  const auto& text_b = *To<Text>(target.nextSibling());

  if (LayoutNG()) {
    EXPECT_EQ(PositionWithAffinity(Position(text_012, 0)), HitTest(0, 150));
    EXPECT_EQ(PositionWithAffinity(Position(text_012, 0)), HitTest(10, 150));
    EXPECT_EQ(PositionWithAffinity(Position(text_012, 1)), HitTest(20, 150));
    EXPECT_EQ(PositionWithAffinity(Position(text_012, 1)), HitTest(30, 150));
    EXPECT_EQ(PositionWithAffinity(Position(text_012, 2)), HitTest(40, 150));
    EXPECT_EQ(PositionWithAffinity(Position(text_012, 2)), HitTest(50, 150));
    EXPECT_EQ(
        PositionWithAffinity(Position(text_012, 3), TextAffinity::kUpstream),
        HitTest(60, 150));
    EXPECT_EQ(PositionWithAffinity(Position(text_34, 0)), HitTest(70, 150));
    EXPECT_EQ(PositionWithAffinity(Position(text_34, 1)), HitTest(80, 150));
    EXPECT_EQ(PositionWithAffinity(Position(text_34, 1)), HitTest(90, 150));
    EXPECT_EQ(
        PositionWithAffinity(Position(text_34, 2), TextAffinity::kUpstream),
        HitTest(100, 150));
    // TODO(yosin): should be text_012@5
    if (IsAndroidOrWindowsEditingBehavior()) {
      EXPECT_EQ(PositionWithAffinity(Position(text_b, 0)), HitTest(110, 150));
      EXPECT_EQ(PositionWithAffinity(Position(text_b, 0)), HitTest(120, 150));
    } else {
      EXPECT_EQ(PositionWithAffinity(Position(text_a, 0)), HitTest(110, 150));
      EXPECT_EQ(PositionWithAffinity(Position(text_a, 0)), HitTest(120, 150));
    }
  } else {
    // Note: Hit test for legacy layout is broken. This is just record of
    // current behavior.
    if (IsAndroidOrWindowsEditingBehavior())
      EXPECT_EQ(PositionWithAffinity(Position(text_012, 0)), HitTest(0, 150));
    else
      EXPECT_EQ(PositionWithAffinity(Position(text_b, 1)), HitTest(0, 150));
    EXPECT_EQ(PositionWithAffinity(Position(text_012, 0)), HitTest(10, 150));
    EXPECT_EQ(PositionWithAffinity(Position(text_012, 0)), HitTest(20, 150));
    EXPECT_EQ(PositionWithAffinity(Position(text_012, 0)), HitTest(30, 150));
    EXPECT_EQ(PositionWithAffinity(Position(text_012, 0)), HitTest(40, 150));
    EXPECT_EQ(PositionWithAffinity(Position(text_012, 0)), HitTest(50, 150));
    EXPECT_EQ(PositionWithAffinity(Position(text_012, 0)), HitTest(60, 150));
    EXPECT_EQ(PositionWithAffinity(Position(text_012, 0)), HitTest(70, 150));
    EXPECT_EQ(PositionWithAffinity(Position(text_012, 0)), HitTest(80, 150));
    EXPECT_EQ(PositionWithAffinity(Position(text_012, 0)), HitTest(90, 150));
    EXPECT_EQ(PositionWithAffinity(Position(text_012, 0)), HitTest(100, 150));
    if (IsAndroidOrWindowsEditingBehavior()) {
      EXPECT_EQ(PositionWithAffinity(Position(text_012, 0)), HitTest(110, 150));
      EXPECT_EQ(PositionWithAffinity(Position(text_012, 0)), HitTest(120, 150));
    } else {
      EXPECT_EQ(PositionWithAffinity(Position(text_a, 0)), HitTest(110, 150));
      EXPECT_EQ(PositionWithAffinity(Position(text_a, 0)), HitTest(120, 150));
    }
  }
}

}  // namespace blink
