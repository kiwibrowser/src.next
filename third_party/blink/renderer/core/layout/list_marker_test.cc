// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/layout/list_marker.h"

#include "third_party/blink/renderer/core/layout/ng/list/layout_ng_list_item.h"
#include "third_party/blink/renderer/core/layout/ng/ng_layout_test.h"

namespace blink {

// We don't test legacy layout because it's deprecated, and we don't want to
// complicate the test with the legacy LayoutListMarker here.
class ListMarkerTest : public NGLayoutTest {
 protected:
  LayoutObject* GetMarker(const char* list_item_id) {
    LayoutNGListItem* list_item =
        To<LayoutNGListItem>(GetLayoutObjectByElementId(list_item_id));
    return list_item->Marker();
  }

  LayoutObject* GetMarker(TreeScope& scope, const char* list_item_id) {
    Element* list_item = scope.getElementById(list_item_id);
    return To<LayoutNGListItem>(list_item->GetLayoutObject())->Marker();
  }

  String GetMarkerText(TreeScope& scope, const char* list_item_id) {
    return To<LayoutText>(GetMarker(scope, list_item_id)->SlowFirstChild())
        ->GetText();
  }

  String GetMarkerText(const char* list_item_id) {
    return GetMarkerText(GetDocument(), list_item_id);
  }

  void AddCounterStyle(const AtomicString& name, const String& descriptors) {
    StringBuilder declaration;
    declaration.Append("@counter-style ");
    declaration.Append(name);
    declaration.Append("{");
    declaration.Append(descriptors);
    declaration.Append("}");
    Element* sheet = GetDocument().CreateElementForBinding("style");
    sheet->setInnerHTML(declaration.ToString());
    GetDocument().body()->appendChild(sheet);
  }
};

TEST_F(ListMarkerTest, AddCounterStyle) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      @counter-style foo {
        system: fixed;
        symbols: W X Y Z;
      }
    </style>
    <ol>
      <li id="decimal" style="list-style-type: decimal"></li>
      <li id="foo" style="list-style-type: foo"></li>
      <li id="bar" style="list-style-type: bar"></li>
    </ol>
  )HTML");
  UpdateAllLifecyclePhasesForTest();

  EXPECT_EQ("1. ", GetMarkerText("decimal"));
  EXPECT_EQ("X. ", GetMarkerText("foo"));
  EXPECT_EQ("3. ", GetMarkerText("bar"));

  // Add @counter-style 'bar'. Should not affect 'decimal' and 'foo'.
  AddCounterStyle("bar", "system: fixed; symbols: A B C;");
  GetDocument().UpdateStyleAndLayoutTree();

  EXPECT_FALSE(GetMarker("decimal")->NeedsLayout());
  EXPECT_FALSE(GetMarker("foo")->NeedsLayout());
  EXPECT_TRUE(GetMarker("bar")->NeedsLayout());

  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ("1. ", GetMarkerText("decimal"));
  EXPECT_EQ("X. ", GetMarkerText("foo"));
  EXPECT_EQ("C. ", GetMarkerText("bar"));
}

TEST_F(ListMarkerTest, RemoveCounterStyle) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style id="foo-sheet">
      @counter-style foo {
        system: fixed;
        symbols: W X Y Z;
      }
    </style>
    <ol>
      <li id="decimal" style="list-style-type: decimal"></li>
      <li id="foo" style="list-style-type: foo"></li>
    </ol>
  )HTML");
  UpdateAllLifecyclePhasesForTest();

  EXPECT_EQ("1. ", GetMarkerText("decimal"));
  EXPECT_EQ("X. ", GetMarkerText("foo"));

  // Remove @counter-style 'foo'. Should not affect 'decimal'.
  GetElementById("foo-sheet")->remove();
  GetDocument().UpdateStyleAndLayoutTree();

  EXPECT_FALSE(GetMarker("decimal")->NeedsLayout());
  EXPECT_TRUE(GetMarker("foo")->NeedsLayout());

  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ("1. ", GetMarkerText("decimal"));
  EXPECT_EQ("2. ", GetMarkerText("foo"));
}

TEST_F(ListMarkerTest, OverridePredefinedCounterStyle) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <ol>
      <li id="decimal" style="list-style-type: decimal"></li>
      <li id="upper-roman" style="list-style-type: upper-roman"></li>
    </ol>
  )HTML");
  UpdateAllLifecyclePhasesForTest();

  EXPECT_EQ("1. ", GetMarkerText("decimal"));
  EXPECT_EQ("II. ", GetMarkerText("upper-roman"));

  // Override 'upper-roman'. Should not affect 'decimal'.
  AddCounterStyle("upper-roman", "system: fixed; symbols: A B C;");
  GetDocument().UpdateStyleAndLayoutTree();

  EXPECT_FALSE(GetMarker("decimal")->NeedsLayout());
  EXPECT_TRUE(GetMarker("upper-roman")->NeedsLayout());

  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ("1. ", GetMarkerText("decimal"));
  EXPECT_EQ("B. ", GetMarkerText("upper-roman"));
}

TEST_F(ListMarkerTest, RemoveOverrideOfPredefinedCounterStyle) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style id="to-remove">
      @counter-style upper-roman {
        system: fixed;
        symbols: A B C;
      }
    </style>
    <ol>
      <li id="decimal" style="list-style-type: decimal"></li>
      <li id="upper-roman" style="list-style-type: upper-roman"></li>
    </ol>
  )HTML");
  UpdateAllLifecyclePhasesForTest();

  EXPECT_EQ("1. ", GetMarkerText("decimal"));
  EXPECT_EQ("B. ", GetMarkerText("upper-roman"));

  // Remove override of 'upper-roman'. Should not affect 'decimal'.
  GetElementById("to-remove")->remove();
  GetDocument().UpdateStyleAndLayoutTree();

  EXPECT_FALSE(GetMarker("decimal")->NeedsLayout());
  EXPECT_TRUE(GetMarker("upper-roman")->NeedsLayout());

  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ("1. ", GetMarkerText("decimal"));
  EXPECT_EQ("II. ", GetMarkerText("upper-roman"));
}

TEST_F(ListMarkerTest, OverrideSameScopeCounterStyle) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      @counter-style foo {
        system: fixed;
        symbols: W X Y Z;
      }
    </style>
    <ol>
      <li id="decimal" style="list-style-type: decimal"></li>
      <li id="foo" style="list-style-type: foo"></li>
    </ol>
  )HTML");
  UpdateAllLifecyclePhasesForTest();

  EXPECT_EQ("1. ", GetMarkerText("decimal"));
  EXPECT_EQ("X. ", GetMarkerText("foo"));

  // Override 'foo'. Should not affect 'decimal'.
  AddCounterStyle("foo", "system: fixed; symbols: A B C;");
  GetDocument().UpdateStyleAndLayoutTree();

  EXPECT_FALSE(GetMarker("decimal")->NeedsLayout());
  EXPECT_TRUE(GetMarker("foo")->NeedsLayout());

  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ("1. ", GetMarkerText("decimal"));
  EXPECT_EQ("B. ", GetMarkerText("foo"));
}

TEST_F(ListMarkerTest, RemoveOverrideOfSameScopeCounterStyle) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      @counter-style foo {
        system: fixed;
        symbols: W X Y Z;
      }
    </style>
    <style id="to-remove">
      @counter-style foo {
        system: fixed;
        symbols: A B C;
      }
    </style>
    <ol>
      <li id="decimal" style="list-style-type: decimal"></li>
      <li id="foo" style="list-style-type: foo"></li>
    </ol>
  )HTML");
  UpdateAllLifecyclePhasesForTest();

  EXPECT_EQ("1. ", GetMarkerText("decimal"));
  EXPECT_EQ("B. ", GetMarkerText("foo"));

  // Remove the override of 'foo'. Should not affect 'decimal'.
  GetElementById("to-remove")->remove();
  GetDocument().UpdateStyleAndLayoutTree();

  EXPECT_FALSE(GetMarker("decimal")->NeedsLayout());
  EXPECT_TRUE(GetMarker("foo")->NeedsLayout());

  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ("1. ", GetMarkerText("decimal"));
  EXPECT_EQ("X. ", GetMarkerText("foo"));
}

TEST_F(ListMarkerTest, ModifyShadowDOMWithOwnCounterStyles) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      @counter-style foo {
        system: fixed;
        symbols: W X Y Z;
      }
    </style>
    <ol>
      <li id="decimal" style="list-style-type: decimal"></li>
      <li id="foo" style="list-style-type: foo"></li>
    </ol>
    <div id="host1"></div>
    <div id="host2"></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();

  EXPECT_EQ("1. ", GetMarkerText("decimal"));
  EXPECT_EQ("X. ", GetMarkerText("foo"));

  // Attach a shadow tree with counter styles. Shouldn't affect anything outside
  ShadowRoot& shadow1 =
      GetElementById("host1")->AttachShadowRootInternal(ShadowRootType::kOpen);
  shadow1.setInnerHTML(R"HTML(
    <style>
      @counter-style foo {
        system: fixed;
        symbols: A B C;
      }
    </style>
    <ol>
      <li id="shadow-foo" style="list-style-type: foo"></li>
    </ol>
  )HTML");
  GetDocument().UpdateStyleAndLayoutTree();
  EXPECT_FALSE(GetMarker("decimal")->NeedsLayout());
  EXPECT_FALSE(GetMarker("foo")->NeedsLayout());
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ("1. ", GetMarkerText("decimal"));
  EXPECT_EQ("X. ", GetMarkerText("foo"));
  EXPECT_EQ("A. ", GetMarkerText(shadow1, "shadow-foo"));

  // Attach another shadow tree with counter styles. Shouldn't affect anything
  // outside.
  ShadowRoot& shadow2 =
      GetElementById("host2")->AttachShadowRootInternal(ShadowRootType::kOpen);
  shadow2.setInnerHTML(R"HTML(
    <style>
      @counter-style foo {
        system: fixed;
        symbols: D E F;
      }
    </style>
    <ol>
      <li id="shadow-foo" style="list-style-type: foo"></li>
    </ol>
  )HTML");
  GetDocument().UpdateStyleAndLayoutTree();
  EXPECT_FALSE(GetMarker("decimal")->NeedsLayout());
  EXPECT_FALSE(GetMarker("foo")->NeedsLayout());
  EXPECT_FALSE(GetMarker(shadow1, "shadow-foo")->NeedsLayout());
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ("1. ", GetMarkerText("decimal"));
  EXPECT_EQ("X. ", GetMarkerText("foo"));
  EXPECT_EQ("A. ", GetMarkerText(shadow1, "shadow-foo"));
  EXPECT_EQ("D. ", GetMarkerText(shadow2, "shadow-foo"));

  // Remove one of the shadow trees. Shouldn't affect anything outside.
  GetElementById("host1")->remove();
  GetDocument().UpdateStyleAndLayoutTree();
  EXPECT_FALSE(GetMarker("decimal")->NeedsLayout());
  EXPECT_FALSE(GetMarker("foo")->NeedsLayout());
  EXPECT_FALSE(GetMarker(shadow2, "shadow-foo")->NeedsLayout());
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ("1. ", GetMarkerText("decimal"));
  EXPECT_EQ("X. ", GetMarkerText("foo"));
  EXPECT_EQ("D. ", GetMarkerText(shadow2, "shadow-foo"));
}

TEST_F(ListMarkerTest, WidthOfSymbolForFontSizeZero) {
  InsertStyleElement("li { font-size: 0px; }");
  SetBodyInnerHTML("<li id=target>a</li>");
  const auto& target = *GetElementById("target");
  const auto& target_layout_object = *target.GetLayoutObject();

  EXPECT_EQ(LayoutUnit(),
            ListMarker::WidthOfSymbol(target_layout_object.StyleRef()));
}

// crbug.com/1310599
TEST_F(ListMarkerTest, InlineMarginsForOutside) {
  GetDocument().body()->setInnerHTML(
      R"HTML(<details open><summary id="target" style="
  font-size: 536870912px;
  zoom: 65536;
  list-style-position: outside;
  ">foo</summary></details>)HTML",
      ASSERT_NO_EXCEPTION);
  GetDocument().UpdateStyleAndLayoutTree();
  auto* item_object = GetLayoutObjectByElementId("target");
  auto* marker_object = ListMarker::MarkerFromListItem(item_object);
  auto [start, end] = ListMarker::InlineMarginsForOutside(
      GetDocument(), marker_object->StyleRef(), item_object->StyleRef(),
      LayoutUnit::Max());
  EXPECT_EQ(LayoutUnit::Min(), start);
  EXPECT_EQ(LayoutUnit(), end);
}

class ListMarkerLegacyTest : public RenderingTest {
 private:
  ScopedLayoutNGForTest layout_ng{false};
};

// crbug.com/1336864
TEST_F(ListMarkerLegacyTest, NegativeLetterSpacingByStatic) {
  SetBodyContent(
      R"HTML(<ul><li id="target" style="
  font-size: 100px;
  letter-spacing: -4400000000px;
  list-style-type: 'foo';
  list-style-position: outside;
  ">foo</li></ul>)HTML");

  auto* item_object = GetLayoutObjectByElementId("target");
  auto* marker_object = ListMarker::MarkerFromListItem(item_object);
  // Negative letter-spacing should not make the marker width negative.
  EXPECT_GE(To<LayoutBox>(marker_object)->LogicalWidth(), LayoutUnit());
}

// crbug.com/1336864
TEST_F(ListMarkerLegacyTest, NegativeLetterSpacingBySuffix) {
  // The following marker is a decimal counter, and it has a suffix like ".".
  SetBodyContent(
      R"HTML(<ol><li id="target">foo</li></ol>
      <style>
      #target::marker {
        font-size: 100px;
        letter-spacing: -4400000000px;
      }</style>)HTML");

  auto* item_object = GetLayoutObjectByElementId("target");
  auto* marker_object = ListMarker::MarkerFromListItem(item_object);
  // Negative letter-spacing should not make the marker width negative.
  EXPECT_GE(To<LayoutBox>(marker_object)->LogicalWidth(), LayoutUnit());
}

}  // namespace blink
