// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/layout/layout_table.h"
#include "third_party/blink/renderer/core/layout/layout_table_section.h"

#include "third_party/blink/renderer/core/testing/core_unit_test_helper.h"

namespace blink {

namespace {

class LayoutTableTest : public RenderingTest {
 protected:
  LayoutBlock* GetTableByElementId(const char* id) {
    return To<LayoutBlock>(GetLayoutObjectByElementId(id));
  }
  LayoutNGTableInterface* GetTableInterfaceByElementId(const char* id) {
    return ToInterface<LayoutNGTableInterface>(GetLayoutObjectByElementId(id));
  }
};

TEST_F(LayoutTableTest, OverflowViaOutline) {
  SetBodyInnerHTML(R"HTML(
    <style>
      div { display: table; width: 100px; height: 200px; }
    </style>
    <div id=target>
      <div id=child></div>
    </div>
  )HTML");
  auto* target = GetTableByElementId("target");
  EXPECT_EQ(LayoutRect(0, 0, 100, 200), target->SelfVisualOverflowRect());
  To<Element>(target->GetNode())
      ->setAttribute(html_names::kStyleAttr, "outline: 2px solid black");

  auto* child = GetTableByElementId("child");
  To<Element>(child->GetNode())
      ->setAttribute(html_names::kStyleAttr, "outline: 2px solid black");

  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(LayoutRect(-2, -2, 104, 204), target->SelfVisualOverflowRect());

  EXPECT_EQ(LayoutRect(-2, -2, 104, 204), child->SelfVisualOverflowRect());
}

TEST_F(LayoutTableTest, OverflowWithCollapsedBorders) {
  SetBodyInnerHTML(R"HTML(
    <style>
      table { border-collapse: collapse }
      td { border: 0px solid blue; padding: 0; width: 100px; height: 100px }
    </style>
    <table id='table'>
      <tr>
        <td style='border-top-width: 2px; border-left-width: 2px;
            outline: 6px solid blue'></td>
        <td style='border-top-width: 4px; border-right-width: 10px'></td>
      </tr>
      <tr style='outline: 8px solid green'>
        <td style='border-left-width: 20px'></td>
        <td style='border-right-width: 20px'></td>
      </tr>
    </table>
  )HTML");

  auto* table = GetTableByElementId("table");

  auto expected_border_box_rect = table->PhysicalContentBoxRect();
  if (RuntimeEnabledFeatures::LayoutNGEnabled()) {
    expected_border_box_rect.ExpandEdges(LayoutUnit(2), LayoutUnit(10),
                                         LayoutUnit(0), LayoutUnit(10));
  } else {
    // The table's border box rect covers all collapsed borders of the first
    // row, and bottom collapsed borders of the last row.
    expected_border_box_rect.ExpandEdges(LayoutUnit(2), LayoutUnit(5),
                                         LayoutUnit(0), LayoutUnit(1));
  }
  EXPECT_EQ(expected_border_box_rect, table->PhysicalBorderBoxRect());

  // The table's self visual overflow rect covers all collapsed borders, but
  // not visual overflows (outlines) from descendants.
  auto expected_self_visual_overflow = table->PhysicalContentBoxRect();
  expected_self_visual_overflow.ExpandEdges(LayoutUnit(2), LayoutUnit(10),
                                            LayoutUnit(0), LayoutUnit(10));
  EXPECT_EQ(expected_self_visual_overflow,
            table->PhysicalSelfVisualOverflowRect());
  EXPECT_EQ(expected_self_visual_overflow, table->PhysicalLayoutOverflowRect());
  // The table's visual overflow covers self visual overflow and content visual
  // overflows.
  auto expected_visual_overflow = table->PhysicalContentBoxRect();
  expected_visual_overflow.ExpandEdges(LayoutUnit(6), LayoutUnit(10),
                                       LayoutUnit(8), LayoutUnit(10));
  EXPECT_EQ(expected_visual_overflow, table->PhysicalVisualOverflowRect());
}

TEST_F(LayoutTableTest, CollapsedBorders) {
  SetBodyInnerHTML(
      "<style>table { border-collapse: collapse }</style>"
      "<table id='table1'"
      "    style='border-top: hidden; border-bottom: 8px solid;"
      "           border-left: hidden; border-right: 10px solid'>"
      "  <tr><td>A</td><td>B</td></tr>"
      "</table>"
      "<table id='table2' style='border: 10px solid'>"
      "  <tr>"
      "    <td style='border: hidden'>C</td>"
      "    <td style='border: hidden'>D</td>"
      "  </tr>"
      "</table>"
      "<table id='table3' style='border: 10px solid'>"
      "  <tr>"
      "    <td style='border-top: 15px solid;"
      "               border-left: 21px solid'>E</td>"
      "    <td style='border-right: 25px solid'>F</td>"
      "  </tr>"
      // The second row won't affect start and end borders of the table.
      "  <tr>"
      "    <td style='border: 30px solid'>G</td>"
      "    <td style='border: 40px solid'>H</td>"
      "  </tr>"
      "</table>");

  auto* table1 = GetTableByElementId("table1");
  EXPECT_EQ(0, table1->BorderBefore());
  EXPECT_EQ(4, table1->BorderAfter());
  EXPECT_EQ(0, table1->BorderStart());
  EXPECT_EQ(5, table1->BorderEnd());

  // All cells have hidden border.
  auto* table2 = GetTableByElementId("table2");
  EXPECT_EQ(0, table2->BorderBefore());
  EXPECT_EQ(0, table2->BorderAfter());
  EXPECT_EQ(0, table2->BorderStart());
  EXPECT_EQ(0, table2->BorderEnd());

  // Cells have wider borders.
  auto* table3 = GetTableByElementId("table3");
  if (RuntimeEnabledFeatures::LayoutNGEnabled()) {
    // Cell E's border-top won.
    EXPECT_EQ(LayoutUnit(7.5), table3->BorderBefore());
    // Cell H's border-bottom won.
    EXPECT_EQ(20, table3->BorderAfter());
    // Cell G's border-left won.
    EXPECT_EQ(LayoutUnit(15), table3->BorderStart());
    // Cell H's border-right won.
    EXPECT_EQ(LayoutUnit(20), table3->BorderEnd());
  } else {
    // Cell E's border-top won.
    EXPECT_EQ(7, table3->BorderBefore());
    // Cell H's border-bottom won.
    EXPECT_EQ(20, table3->BorderAfter());
    // Cell E's border-left won.
    EXPECT_EQ(10, table3->BorderStart());
    // Cell F's border-bottom won.
    EXPECT_EQ(13, table3->BorderEnd());
  }
}

TEST_F(LayoutTableTest, CollapsedBordersWithCol) {
  SetBodyInnerHTML(R"HTML(
    <style>table { border-collapse: collapse }</style>
    <table id='table1' style='border: hidden'>
      <colgroup>
        <col span='2000' style='border: 10px solid'>
        <col span='2000' style='border: 20px solid'>
      </colgroup>
      <tr>
        <td colspan='2000'>A</td>
        <td colspan='2000'>B</td>
      </tr>
    </table>
    <table id='table2' style='border: 10px solid'>
      <colgroup>
        <col span='2000' style='border: 10px solid'>
        <col span='2000' style='border: 20px solid'>
      </colgroup>
      <tr>
        <td colspan='2000' style='border: hidden'>C</td>
        <td colspan='2000' style='border: hidden'>D</td>
      </tr>
    </table>
    <table id='table3'>
      <colgroup>
        <col span='2000' style='border: 10px solid'>
        <col span='2000' style='border: 20px solid'>
      </colgroup>
      <tr>
        <td colspan='2000' style='border: 12px solid'>E</td>
        <td colspan='2000' style='border: 16px solid'>F</td>
      </tr>
    </table>
  )HTML");

  // Table has hidden border.
  auto* table1 = GetTableByElementId("table1");
  EXPECT_EQ(0, table1->BorderBefore());
  EXPECT_EQ(0, table1->BorderAfter());
  EXPECT_EQ(0, table1->BorderStart());
  EXPECT_EQ(0, table1->BorderEnd());

  // All cells have hidden border.
  auto* table2 = GetTableByElementId("table2");
  EXPECT_EQ(0, table2->BorderBefore());
  EXPECT_EQ(0, table2->BorderAfter());
  EXPECT_EQ(0, table2->BorderStart());
  EXPECT_EQ(0, table2->BorderEnd());

  // Combined cell and col borders.
  auto* table3 = GetTableByElementId("table3");
  // The second col's border-top won.
  EXPECT_EQ(10, table3->BorderBefore());
  // The second col's border-bottom won.
  EXPECT_EQ(10, table3->BorderAfter());
  // Cell E's border-left won.
  EXPECT_EQ(6, table3->BorderStart());
  // The second col's border-right won.
  EXPECT_EQ(10, table3->BorderEnd());
}

TEST_F(LayoutTableTest, WidthPercentagesExceedHundred) {
  SetBodyInnerHTML(R"HTML(
    <style>#outer { width: 2000000px; }
    table { border-collapse: collapse; }</style>
    <div id='outer'>
    <table id='onlyTable'>
      <tr>
        <td width='100%'>
          <div></div>
        </td>
        <td width='60%'>
          <div width='10px;'></div>
        </td>
      </tr>
    </table>
    </div>
  )HTML");

  // Table width should be TableLayoutAlgorithm::kMaxTableWidth
  auto* table = GetTableByElementId("onlyTable");
  EXPECT_EQ(1000000, table->OffsetWidth());
}

TEST_F(LayoutTableTest, CloseToMaxWidth) {
  SetBodyInnerHTML(R"HTML(
    <style>#outer { width: 2000000px; }
    table { border-collapse: collapse; }</style>
    <div id='outer'>
    <table id='onlyTable' width='999999px;'>
      <tr>
        <td>
          <div></div>
        </td>
      </tr>
    </table>
    </div>
  )HTML");

  // Table width should be 999999
  auto* table = GetTableByElementId("onlyTable");
  EXPECT_EQ(999999, table->OffsetWidth());
}

TEST_F(LayoutTableTest, PaddingWithCollapsedBorder) {
  SetBodyInnerHTML(R"HTML(
    <table id='table' style='padding: 20px; border-collapse: collapse'>
      <tr><td>TD</td</tr>
    </table>
  )HTML");

  auto* table = GetTableByElementId("table");
  EXPECT_EQ(0, table->PaddingLeft());
  EXPECT_EQ(0, table->PaddingRight());
  EXPECT_EQ(0, table->PaddingTop());
  EXPECT_EQ(0, table->PaddingBottom());
  EXPECT_EQ(0, table->PaddingStart());
  EXPECT_EQ(0, table->PaddingEnd());
  EXPECT_EQ(0, table->PaddingBefore());
  EXPECT_EQ(0, table->PaddingAfter());
  EXPECT_EQ(0, table->PaddingOver());
  EXPECT_EQ(0, table->PaddingUnder());
}

TEST_F(LayoutTableTest, OutOfOrderHeadAndBody) {
  SetBodyInnerHTML(R"HTML(
    <table id='table' style='border-collapse: collapse'>
      <tbody id='body'><tr><td>Body</td></tr></tbody>
      <thead id='head'></thead>
    <table>
  )HTML");
  auto* table = GetTableInterfaceByElementId("table");
  auto* head_section = ToInterface<LayoutNGTableSectionInterface>(
      GetLayoutObjectByElementId("head"));
  auto* body_section = ToInterface<LayoutNGTableSectionInterface>(
      GetLayoutObjectByElementId("body"));
  ASSERT_TRUE(table);
  ASSERT_TRUE(head_section);
  ASSERT_TRUE(body_section);

  EXPECT_EQ(head_section, table->FirstSectionInterface());
  EXPECT_EQ(body_section, table->LastSectionInterface());

  EXPECT_EQ(body_section,
            table->NextSectionInterface(head_section, kDoNotSkipEmptySections));
  EXPECT_EQ(nullptr,
            table->NextSectionInterface(body_section, kDoNotSkipEmptySections));

  EXPECT_EQ(body_section, table->FirstNonEmptySectionInterface());
  EXPECT_EQ(body_section, table->LastNonEmptySectionInterface());

  if (RuntimeEnabledFeatures::LayoutNGEnabled()) {
    // Legacy does not implement this API. It is only used by TablesNG.
    EXPECT_EQ(nullptr, table->PreviousSectionInterface(head_section,
                                                       kSkipEmptySections));
    EXPECT_EQ(nullptr, table->PreviousSectionInterface(body_section,
                                                       kSkipEmptySections));
    EXPECT_EQ(head_section, table->PreviousSectionInterface(
                                body_section, kDoNotSkipEmptySections));
  }
}

TEST_F(LayoutTableTest, OutOfOrderFootAndBody) {
  SetBodyInnerHTML(R"HTML(
    <table id='table'>
      <tfoot id='foot'></tfoot>
      <tbody id='body'><tr><td>Body</td></tr></tbody>
    <table>
  )HTML");
  auto* table = GetTableInterfaceByElementId("table");
  auto* body_section = ToInterface<LayoutNGTableSectionInterface>(
      GetLayoutObjectByElementId("body"));
  auto* foot_section = ToInterface<LayoutNGTableSectionInterface>(
      GetLayoutObjectByElementId("foot"));
  ASSERT_TRUE(table);
  ASSERT_TRUE(body_section);
  ASSERT_TRUE(foot_section);

  EXPECT_EQ(body_section, table->FirstSectionInterface());
  EXPECT_EQ(foot_section, table->LastSectionInterface());

  EXPECT_EQ(nullptr,
            table->NextSectionInterface(body_section, kSkipEmptySections));
  EXPECT_EQ(foot_section,
            table->NextSectionInterface(body_section, kDoNotSkipEmptySections));
  EXPECT_EQ(nullptr,
            table->NextSectionInterface(foot_section, kDoNotSkipEmptySections));

  EXPECT_EQ(body_section, table->FirstNonEmptySectionInterface());
  EXPECT_EQ(body_section, table->LastNonEmptySectionInterface());

  if (RuntimeEnabledFeatures::LayoutNGEnabled()) {
    // Legacy does not implement this API. It is only used by TablesNG.
    EXPECT_EQ(body_section, table->PreviousSectionInterface(
                                foot_section, kSkipEmptySections));
    EXPECT_EQ(nullptr, table->PreviousSectionInterface(body_section,
                                                       kSkipEmptySections));
  }
}

TEST_F(LayoutTableTest, OutOfOrderHeadFootAndBody) {
  SetBodyInnerHTML(R"HTML(
    <table id='table' style='border-collapse: collapse'>
      <tfoot id='foot'><tr><td>foot</td></tr></tfoot>
      <thead id='head'><tr><td>head</td></tr></thead>
      <tbody id='body'><tr><td>Body</td></tr></tbody>
    <table>
  )HTML");
  auto* table = GetTableInterfaceByElementId("table");
  auto* head_section = ToInterface<LayoutNGTableSectionInterface>(
      GetLayoutObjectByElementId("head"));
  auto* body_section = ToInterface<LayoutNGTableSectionInterface>(
      GetLayoutObjectByElementId("body"));
  auto* foot_section = ToInterface<LayoutNGTableSectionInterface>(
      GetLayoutObjectByElementId("foot"));
  ASSERT_TRUE(table);
  ASSERT_TRUE(head_section);
  ASSERT_TRUE(body_section);
  ASSERT_TRUE(foot_section);

  EXPECT_EQ(head_section, table->FirstSectionInterface());
  EXPECT_EQ(foot_section, table->LastSectionInterface());

  EXPECT_EQ(body_section,
            table->NextSectionInterface(head_section, kSkipEmptySections));
  EXPECT_EQ(foot_section,
            table->NextSectionInterface(body_section, kSkipEmptySections));

  EXPECT_EQ(head_section, table->FirstNonEmptySectionInterface());
  EXPECT_EQ(foot_section, table->LastNonEmptySectionInterface());

  if (RuntimeEnabledFeatures::LayoutNGEnabled()) {
    // Legacy does not implement this API. It is only used by TablesNG.
    EXPECT_EQ(body_section, table->PreviousSectionInterface(
                                foot_section, kSkipEmptySections));
    EXPECT_EQ(head_section, table->PreviousSectionInterface(
                                body_section, kSkipEmptySections));
  }
}

TEST_F(LayoutTableTest, VisualOverflowCleared) {
  SetBodyInnerHTML(R"HTML(
    <style>
      #table {
        width: 50px; height: 50px; box-shadow: 5px 5px 5px black;
      }
    </style>
    <table id='table' style='width: 50px; height: 50px'></table>
  )HTML");
  auto* table = GetTableByElementId("table");
  EXPECT_EQ(LayoutRect(-3, -3, 66, 66), table->SelfVisualOverflowRect());
  To<Element>(table->GetNode())
      ->setAttribute(html_names::kStyleAttr, "box-shadow: initial");
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(LayoutRect(0, 0, 50, 50), table->SelfVisualOverflowRect());
}

TEST_F(LayoutTableTest, HasNonCollapsedBorderDecoration) {
  // TablesNG does not support DirtiedRowsAndEffectiveColumns.
  if (RuntimeEnabledFeatures::LayoutNGEnabled())
    return;

  SetBodyInnerHTML("<table id='table'></table>");
  auto* table = GetTableByElementId("table");
  EXPECT_FALSE(table->HasNonCollapsedBorderDecoration());

  To<Element>(table->GetNode())
      ->setAttribute(html_names::kStyleAttr, "border: 1px solid black");
  GetDocument().View()->UpdateAllLifecyclePhasesExceptPaint(
      DocumentUpdateReason::kTest);
  EXPECT_TRUE(table->HasNonCollapsedBorderDecoration());

  To<Element>(table->GetNode())
      ->setAttribute(html_names::kStyleAttr,
                     "border: 1px solid black; border-collapse: collapse");
  GetDocument().View()->UpdateAllLifecyclePhasesExceptPaint(
      DocumentUpdateReason::kTest);
  EXPECT_FALSE(table->HasNonCollapsedBorderDecoration());
}

}  // anonymous namespace

}  // namespace blink
