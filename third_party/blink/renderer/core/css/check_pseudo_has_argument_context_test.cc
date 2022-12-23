// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/css/check_pseudo_has_argument_context.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/renderer/core/css/css_selector_list.h"
#include "third_party/blink/renderer/core/css/css_test_helpers.h"
#include "third_party/blink/renderer/core/css/parser/css_parser.h"
#include "third_party/blink/renderer/core/css/parser/css_parser_context.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/html/html_document.h"
#include "third_party/blink/renderer/core/testing/page_test_base.h"

namespace blink {

class CheckPseudoHasArgumentContextTest : public PageTestBase {
 protected:
  const int kMax = std::numeric_limits<int>::max();

  void TestArgumentContext(
      const String& selector_text,
      CSSSelector::RelationType expected_leftmost_relation,
      int expected_adjacent_distance_limit,
      int expected_depth_limit,
      CheckPseudoHasArgumentTraversalScope expected_traversal_scope) const {
    CSSSelectorList selector_list =
        css_test_helpers::ParseSelectorList(selector_text);
    CheckPseudoHasArgumentContext context(
        selector_list.First()->SelectorList()->First());

    EXPECT_EQ(expected_leftmost_relation, context.LeftmostRelation())
        << "Failed : " << selector_text;
    EXPECT_EQ(expected_adjacent_distance_limit, context.AdjacentDistanceLimit())
        << "Failed : " << selector_text;
    EXPECT_EQ(expected_depth_limit, context.DepthLimit())
        << "Failed : " << selector_text;
    EXPECT_EQ(expected_traversal_scope, context.TraversalScope())
        << "Failed : " << selector_text;
  }

  struct ExpectedTraversalStep {
    const char* element_id;
    int depth;
  };

  void TestTraversalIteratorForEmptyRange(Document* document,
                                          const char* has_anchor_element_id,
                                          const char* selector_text) const {
    Element* has_anchor_element =
        document->getElementById(has_anchor_element_id);
    if (!has_anchor_element) {
      ADD_FAILURE() << "Failed : test iterator on #" << has_anchor_element_id
                    << " (Cannot find element)";
      return;
    }

    unsigned i = 0;
    CSSSelectorList selector_list =
        css_test_helpers::ParseSelectorList(selector_text);
    CheckPseudoHasArgumentContext argument_context(
        selector_list.First()->SelectorList()->First());
    for (CheckPseudoHasArgumentTraversalIterator iterator(*has_anchor_element,
                                                          argument_context);
         !iterator.AtEnd(); ++iterator, ++i) {
      AtomicString current_element_id =
          iterator.CurrentElement()
              ? iterator.CurrentElement()->GetIdAttribute()
              : "nullptr";
      int current_depth = iterator.CurrentDepth();
      ADD_FAILURE() << "Iteration failed : exceeded expected iteration"
                    << " (selector: " << selector_text
                    << ", has_anchor_element: #" << has_anchor_element_id
                    << ", index: " << i
                    << ", current_element: " << current_element_id
                    << ", current_depth: " << current_depth << ")";
    }
  }

  template <unsigned length>
  void TestTraversalIteratorSteps(
      Document* document,
      const char* has_anchor_element_id,
      const char* selector_text,
      const ExpectedTraversalStep (&expected_traversal_steps)[length]) const {
    Element* has_anchor_element =
        document->getElementById(has_anchor_element_id);
    if (!has_anchor_element) {
      ADD_FAILURE() << "Failed : test iterator on #" << has_anchor_element_id
                    << " (Cannot find element)";
      return;
    }
    EXPECT_EQ(has_anchor_element->GetIdAttribute(), has_anchor_element_id);

    unsigned i = 0;
    CSSSelectorList selector_list =
        css_test_helpers::ParseSelectorList(selector_text);
    CheckPseudoHasArgumentContext argument_context(
        selector_list.First()->SelectorList()->First());
    for (CheckPseudoHasArgumentTraversalIterator iterator(*has_anchor_element,
                                                          argument_context);
         !iterator.AtEnd(); ++iterator, ++i) {
      AtomicString current_element_id =
          iterator.CurrentElement()
              ? iterator.CurrentElement()->GetIdAttribute()
              : "nullptr";
      int current_depth = iterator.CurrentDepth();
      if (i >= length) {
        ADD_FAILURE() << "Iteration failed : exceeded expected iteration"
                      << " (selector: " << selector_text
                      << ", has_anchor_element: #" << has_anchor_element_id
                      << ", index: " << i
                      << ", current_element: " << current_element_id
                      << ", current_depth: " << current_depth << ")";
        continue;
      }
      EXPECT_EQ(expected_traversal_steps[i].element_id, current_element_id)
          << " (selector: " << selector_text << ", has_anchor_element: #"
          << has_anchor_element_id << ", index: " << i
          << ", expected: " << expected_traversal_steps[i].element_id
          << ", actual: " << current_element_id << ")";
      EXPECT_EQ(expected_traversal_steps[i].depth, current_depth)
          << " (selector: " << selector_text << ", has_anchor_element: #"
          << has_anchor_element_id << ", index: " << i
          << ", expected: " << expected_traversal_steps[i].depth
          << ", actual: " << current_depth << ")";
    }

    for (; i < length; i++) {
      ADD_FAILURE() << "Iteration failed : expected but not traversed"
                    << " (selector: " << selector_text
                    << ", has_anchor_element: #" << has_anchor_element_id
                    << ", index: " << i << ", expected_element: "
                    << expected_traversal_steps[i].element_id << ")";
      EXPECT_NE(
          document->getElementById(expected_traversal_steps[i].element_id),
          nullptr);
    }
  }
};

TEST_F(CheckPseudoHasArgumentContextTest, TestArgumentMatchContext) {
  TestArgumentContext(":has(.a)", CSSSelector::kRelativeDescendant,
                      /* expected_adjacent_distance_limit */ 0,
                      /* expected_depth_limit */ kMax, kSubtree);
  TestArgumentContext(":has(.a ~ .b)", CSSSelector::kRelativeDescendant,
                      /* expected_adjacent_distance_limit */ 0,
                      /* expected_depth_limit */ kMax, kSubtree);
  TestArgumentContext(":has(.a ~ .b > .c)", CSSSelector::kRelativeDescendant,
                      /* expected_adjacent_distance_limit */ 0,
                      /* expected_depth_limit */ kMax, kSubtree);
  TestArgumentContext(":has(.a > .b)", CSSSelector::kRelativeDescendant,
                      /* expected_adjacent_distance_limit */ 0,
                      /* expected_depth_limit */ kMax, kSubtree);
  TestArgumentContext(":has(.a + .b)", CSSSelector::kRelativeDescendant,
                      /* expected_adjacent_distance_limit */ 0,
                      /* expected_depth_limit */ kMax, kSubtree);
  TestArgumentContext(":has(> .a .b)", CSSSelector::kRelativeChild,
                      /* expected_adjacent_distance_limit */ 0,
                      /* expected_depth_limit */ kMax, kSubtree);
  TestArgumentContext(":has(> .a ~ .b .c)", CSSSelector::kRelativeChild,
                      /* expected_adjacent_distance_limit */ 0,
                      /* expected_depth_limit */ kMax, kSubtree);
  TestArgumentContext(":has(> .a + .b .c)", CSSSelector::kRelativeChild,
                      /* expected_adjacent_distance_limit */ 0,
                      /* expected_depth_limit */ kMax, kSubtree);
  TestArgumentContext(":has(> .a)", CSSSelector::kRelativeChild,
                      /* expected_adjacent_distance_limit */ 0,
                      /* expected_depth_limit */ 1, kFixedDepthDescendants);
  TestArgumentContext(":has(> .a > .b)", CSSSelector::kRelativeChild,
                      /* expected_adjacent_distance_limit */ 0,
                      /* expected_depth_limit */ 2, kFixedDepthDescendants);
  TestArgumentContext(":has(> .a + .b)", CSSSelector::kRelativeChild,
                      /* expected_adjacent_distance_limit */ 0,
                      /* expected_depth_limit */ 1, kFixedDepthDescendants);
  TestArgumentContext(":has(> .a ~ .b)", CSSSelector::kRelativeChild,
                      /* expected_adjacent_distance_limit */ 0,
                      /* expected_depth_limit */ 1, kFixedDepthDescendants);
  TestArgumentContext(":has(> .a ~ .b > .c)", CSSSelector::kRelativeChild,
                      /* expected_adjacent_distance_limit */ 0,
                      /* expected_depth_limit */ 2, kFixedDepthDescendants);
  TestArgumentContext(":has(~ .a .b)", CSSSelector::kRelativeIndirectAdjacent,
                      /* expected_adjacent_distance_limit */ kMax,
                      /* expected_depth_limit */ kMax, kAllNextSiblingSubtrees);
  TestArgumentContext(":has(~ .a + .b > .c ~ .d .e)",
                      CSSSelector::kRelativeIndirectAdjacent,
                      /* expected_adjacent_distance_limit */ kMax,
                      /* expected_depth_limit */ kMax, kAllNextSiblingSubtrees);
  TestArgumentContext(":has(~ .a)", CSSSelector::kRelativeIndirectAdjacent,
                      /* expected_adjacent_distance_limit */ kMax,
                      /* expected_depth_limit */ 0, kAllNextSiblings);
  TestArgumentContext(":has(~ .a ~ .b)", CSSSelector::kRelativeIndirectAdjacent,
                      /* expected_adjacent_distance_limit */ kMax,
                      /* expected_depth_limit */ 0, kAllNextSiblings);
  TestArgumentContext(":has(~ .a + .b)", CSSSelector::kRelativeIndirectAdjacent,
                      /* expected_adjacent_distance_limit */ kMax,
                      /* expected_depth_limit */ 0, kAllNextSiblings);
  TestArgumentContext(":has(~ .a + .b ~ .c)",
                      CSSSelector::kRelativeIndirectAdjacent,
                      /* expected_adjacent_distance_limit */ kMax,
                      /* expected_depth_limit */ 0, kAllNextSiblings);
  TestArgumentContext(":has(~ .a > .b)", CSSSelector::kRelativeIndirectAdjacent,
                      /* expected_adjacent_distance_limit */ kMax,
                      /* expected_depth_limit */ 1,
                      kAllNextSiblingsFixedDepthDescendants);
  TestArgumentContext(
      ":has(~ .a + .b > .c ~ .d > .e)", CSSSelector::kRelativeIndirectAdjacent,
      /* expected_adjacent_distance_limit */ kMax,
      /* expected_depth_limit */ 2, kAllNextSiblingsFixedDepthDescendants);
  TestArgumentContext(":has(+ .a ~ .b .c)",
                      CSSSelector::kRelativeDirectAdjacent,
                      /* expected_adjacent_distance_limit */ kMax,
                      /* expected_depth_limit */ kMax, kAllNextSiblingSubtrees);
  TestArgumentContext(":has(+ .a ~ .b > .c + .d .e)",
                      CSSSelector::kRelativeDirectAdjacent,
                      /* expected_adjacent_distance_limit */ kMax,
                      /* expected_depth_limit */ kMax, kAllNextSiblingSubtrees);
  TestArgumentContext(":has(+ .a ~ .b)", CSSSelector::kRelativeDirectAdjacent,
                      /* expected_adjacent_distance_limit */ kMax,
                      /* expected_depth_limit */ 0, kAllNextSiblings);
  TestArgumentContext(":has(+ .a + .b ~ .c)",
                      CSSSelector::kRelativeDirectAdjacent,
                      /* expected_adjacent_distance_limit */ kMax,
                      /* expected_depth_limit */ 0, kAllNextSiblings);
  TestArgumentContext(
      ":has(+ .a ~ .b > .c)", CSSSelector::kRelativeDirectAdjacent,
      /* expected_adjacent_distance_limit */ kMax,
      /* expected_depth_limit */ 1, kAllNextSiblingsFixedDepthDescendants);
  TestArgumentContext(
      ":has(+ .a ~ .b > .c + .d > .e)", CSSSelector::kRelativeDirectAdjacent,
      /* expected_adjacent_distance_limit */ kMax,
      /* expected_depth_limit */ 2, kAllNextSiblingsFixedDepthDescendants);
  TestArgumentContext(":has(+ .a .b)", CSSSelector::kRelativeDirectAdjacent,
                      /* expected_adjacent_distance_limit */ 1,
                      /* expected_depth_limit */ kMax, kOneNextSiblingSubtree);
  TestArgumentContext(":has(+ .a > .b .c)",
                      CSSSelector::kRelativeDirectAdjacent,
                      /* expected_adjacent_distance_limit */ 1,
                      /* expected_depth_limit */ kMax, kOneNextSiblingSubtree);
  TestArgumentContext(":has(+ .a .b > .c)",
                      CSSSelector::kRelativeDirectAdjacent,
                      /* expected_adjacent_distance_limit */ 1,
                      /* expected_depth_limit */ kMax, kOneNextSiblingSubtree);
  TestArgumentContext(":has(+ .a .b ~ .c)",
                      CSSSelector::kRelativeDirectAdjacent,
                      /* expected_adjacent_distance_limit */ 1,
                      /* expected_depth_limit */ kMax, kOneNextSiblingSubtree);
  TestArgumentContext(":has(+ .a + .b .c)",
                      CSSSelector::kRelativeDirectAdjacent,
                      /* expected_adjacent_distance_limit */ 2,
                      /* expected_depth_limit */ kMax, kOneNextSiblingSubtree);
  TestArgumentContext(":has(+ .a > .b + .c .d)",
                      CSSSelector::kRelativeDirectAdjacent,
                      /* expected_adjacent_distance_limit */ 1,
                      /* expected_depth_limit */ kMax, kOneNextSiblingSubtree);
  TestArgumentContext(":has(+ .a + .b > .c .d)",
                      CSSSelector::kRelativeDirectAdjacent,
                      /* expected_adjacent_distance_limit */ 2,
                      /* expected_depth_limit */ kMax, kOneNextSiblingSubtree);
  TestArgumentContext(":has(+ .a)", CSSSelector::kRelativeDirectAdjacent,
                      /* expected_adjacent_distance_limit */ 1,
                      /* expected_depth_limit */ 0, kOneNextSibling);
  TestArgumentContext(":has(+ .a + .b)", CSSSelector::kRelativeDirectAdjacent,
                      /* expected_adjacent_distance_limit */ 2,
                      /* expected_depth_limit */ 0, kOneNextSibling);
  TestArgumentContext(":has(+ .a + .b + .c)",
                      CSSSelector::kRelativeDirectAdjacent,
                      /* expected_adjacent_distance_limit */ 3,
                      /* expected_depth_limit */ 0, kOneNextSibling);
  TestArgumentContext(":has(+ .a > .b)", CSSSelector::kRelativeDirectAdjacent,
                      /* expected_adjacent_distance_limit */ 1,
                      /* expected_depth_limit */ 1,
                      kOneNextSiblingFixedDepthDescendants);
  TestArgumentContext(
      ":has(+ .a > .b ~ .c)", CSSSelector::kRelativeDirectAdjacent,
      /* expected_adjacent_distance_limit */ 1,
      /* expected_depth_limit */ 1, kOneNextSiblingFixedDepthDescendants);
  TestArgumentContext(
      ":has(+ .a + .b > .c ~ .d > .e)", CSSSelector::kRelativeDirectAdjacent,
      /* expected_adjacent_distance_limit */ 2,
      /* expected_depth_limit */ 2, kOneNextSiblingFixedDepthDescendants);
}

TEST_F(CheckPseudoHasArgumentContextTest, TestTraversalIteratorCase1) {
  // CheckPseudoHasArgumentTraversalScope::kSubtree

  auto* document = HTMLDocument::CreateForTest();
  document->write(R"HTML(
    <!DOCTYPE html>
    <main id=main>
      <div id=div1>
        <div id=div11>
          <div id=div111></div>
        </div>
        <div id=div12>
          <div id=div121></div>
          <div id=div122>
            <div id=div1221></div>
            <div id=div1222></div>
            <div id=div1223></div>
          </div>
          <div id=div123></div>
        </div>
        <div id=div13></div>
      </div>
    </main>
  )HTML");

  TestTraversalIteratorSteps(document, "div1", ":has(.a)",
                             {{"div13", /* depth */ 1},
                              {"div123", /* depth */ 2},
                              {"div1223", /* depth */ 3},
                              {"div1222", /* depth */ 3},
                              {"div1221", /* depth */ 3},
                              {"div122", /* depth */ 2},
                              {"div121", /* depth */ 2},
                              {"div12", /* depth */ 1},
                              {"div111", /* depth */ 2},
                              {"div11", /* depth */ 1}});

  TestTraversalIteratorSteps(document, "div12", ":has(.a)",
                             {{"div123", /* depth */ 1},
                              {"div1223", /* depth */ 2},
                              {"div1222", /* depth */ 2},
                              {"div1221", /* depth */ 2},
                              {"div122", /* depth */ 1},
                              {"div121", /* depth */ 1}});

  TestTraversalIteratorSteps(document, "div122", ":has(.a)",
                             {{"div1223", /* depth */ 1},
                              {"div1222", /* depth */ 1},
                              {"div1221", /* depth */ 1}});

  TestTraversalIteratorSteps(document, "div11", ":has(.a)",
                             {{"div111", /* depth */ 1}});

  TestTraversalIteratorForEmptyRange(document, "div111", ":has(.a)");
}

TEST_F(CheckPseudoHasArgumentContextTest, TestTraversalIteratorCase2) {
  // CheckPseudoHasArgumentTraversalScope::kAllNextSiblings

  auto* document = HTMLDocument::CreateForTest();
  document->write(R"HTML(
    <!DOCTYPE html>
    <main id=main>
      <div id=div1>
        <div id=div11></div>
      </div>
      <div id=div2>
        <div id=div21></div>
      </div>
      <div id=div3>
        <div id=div31></div>
      </div>
      <div id=div4>
        <div id=div41></div>
      </div>
    </main>
  )HTML");

  TestTraversalIteratorSteps(document, "div1", ":has(~ .a)",
                             {{"div4", /* depth */ 0},
                              {"div3", /* depth */ 0},
                              {"div2", /* depth */ 0}});

  TestTraversalIteratorSteps(document, "div3", ":has(~ .a)",
                             {{"div4", /* depth */ 0}});

  TestTraversalIteratorForEmptyRange(document, "div4", ":has(~ .a)");
}

TEST_F(CheckPseudoHasArgumentContextTest, TestTraversalIteratorCase3) {
  // CheckPseudoHasArgumentTraversalScope::kOneNextSiblingSubtree

  auto* document = HTMLDocument::CreateForTest();
  document->write(R"HTML(
    <!DOCTYPE html>
    <main id=main>
      <div id=div1>
        <div id=div11></div>
      </div>
      <div id=div2>
        <div id=div21></div>
      </div>
      <div id=div3>
        <div id=div31>
          <div id=div311></div>
        </div>
        <div id=div32>
          <div id=div321></div>
        </div>
        <div id=div33></div>
        <div id=div34>
          <div id=div341>
            <div id=div3411></div>
          </div>
        </div>
      </div>
      <div id=div4>
        <div id=div41></div>
      </div>
    </main>
  )HTML");

  TestTraversalIteratorSteps(document, "div1", ":has(+ .a + .b .c)",
                             {{"div3411", /* depth */ 3},
                              {"div341", /* depth */ 2},
                              {"div34", /* depth */ 1},
                              {"div33", /* depth */ 1},
                              {"div321", /* depth */ 2},
                              {"div32", /* depth */ 1},
                              {"div311", /* depth */ 2},
                              {"div31", /* depth */ 1},
                              {"div3", /* depth */ 0},
                              {"div2", /* depth */ 0}});

  TestTraversalIteratorSteps(document, "div2", ":has(+ .a + .b .c)",
                             {{"div41", /* depth */ 1},
                              {"div4", /* depth */ 0},
                              {"div3", /* depth */ 0}});

  TestTraversalIteratorSteps(document, "div3", ":has(+ .a + .b .c)",
                             {{"div4", /* depth */ 0}});

  TestTraversalIteratorSteps(
      document, "div31", ":has(+ .a + .b .c)",
      {{"div33", /* depth */ 0}, {"div32", /* depth */ 0}});

  TestTraversalIteratorSteps(document, "div32", ":has(+ .a + .b .c)",
                             {{"div3411", /* depth */ 2},
                              {"div341", /* depth */ 1},
                              {"div34", /* depth */ 0},
                              {"div33", /* depth */ 0}});

  TestTraversalIteratorForEmptyRange(document, "div4", ":has(+ .a + .b .c)");
}

TEST_F(CheckPseudoHasArgumentContextTest, TestTraversalIteratorCase4) {
  // CheckPseudoHasArgumentTraversalScope::kAllNextSiblingSubtrees

  auto* document = HTMLDocument::CreateForTest();
  document->write(R"HTML(
    <!DOCTYPE html>
    <main id=main>
      <div id=div1>
        <div id=div11></div>
      </div>
      <div id=div2>
        <div id=div21></div>
      </div>
      <div id=div3>
        <div id=div31>
          <div id=div311></div>
        </div>
        <div id=div32>
          <div id=div321></div>
        </div>
        <div id=div33></div>
        <div id=div34>
          <div id=div341>
            <div id=div3411></div>
          </div>
        </div>
      </div>
      <div id=div4>
        <div id=div41></div>
      </div>
      <div id=div5></div>
    </main>
  )HTML");

  TestTraversalIteratorSteps(document, "div2", ":has(~ .a .b)",
                             {{"div5", /* depth */ 0},
                              {"div41", /* depth */ 1},
                              {"div4", /* depth */ 0},
                              {"div3411", /* depth */ 3},
                              {"div341", /* depth */ 2},
                              {"div34", /* depth */ 1},
                              {"div33", /* depth */ 1},
                              {"div321", /* depth */ 2},
                              {"div32", /* depth */ 1},
                              {"div311", /* depth */ 2},
                              {"div31", /* depth */ 1},
                              {"div3", /* depth */ 0}});

  TestTraversalIteratorSteps(document, "div4", ":has(~ .a .b)",
                             {{"div5", /* depth */ 0}});

  TestTraversalIteratorForEmptyRange(document, "div5", ":has(~ .a .b)");
}

TEST_F(CheckPseudoHasArgumentContextTest, TestTraversalIteratorCase5) {
  // CheckPseudoHasArgumentTraversalScope::kOneNextSibling

  auto* document = HTMLDocument::CreateForTest();
  document->write(R"HTML(
    <!DOCTYPE html>
    <main id=main>
      <div id=div1>
        <div id=div11></div>
      </div>
      <div id=div2>
        <div id=div21></div>
      </div>
      <div id=div3>
        <div id=div31></div>
      </div>
      <div id=div4>
        <div id=div41></div>
      </div>
    </main>
  )HTML");

  TestTraversalIteratorSteps(
      document, "div1", ":has(+ .a + .b)",
      {{"div3", /* depth */ 0}, {"div2", /* depth */ 0}});

  TestTraversalIteratorSteps(
      document, "div2", ":has(+ .a + .b)",
      {{"div4", /* depth */ 0}, {"div3", /* depth */ 0}});

  TestTraversalIteratorSteps(document, "div3", ":has(~ .a)",
                             {{"div4", /* depth */ 0}});

  TestTraversalIteratorForEmptyRange(document, "div4", ":has(~ .a)");
}

TEST_F(CheckPseudoHasArgumentContextTest, TestTraversalIteratorCase6) {
  // CheckPseudoHasArgumentTraversalScope::kFixedDepthDescendants

  auto* document = HTMLDocument::CreateForTest();
  document->write(R"HTML(
    <!DOCTYPE html>
    <main id=main>
      <div id=div1>
        <div id=div11>
          <div id=div111></div>
        </div>
        <div id=div12>
          <div id=div121></div>
          <div id=div122>
            <div id=div1221></div>
            <div id=div1222></div>
            <div id=div1223></div>
          </div>
          <div id=div123></div>
        </div>
        <div id=div13></div>
      </div>
    </main>
  )HTML");

  TestTraversalIteratorSteps(document, "div1", ":has(> .a > .b)",
                             {{"div13", /* depth */ 1},
                              {"div123", /* depth */ 2},
                              {"div122", /* depth */ 2},
                              {"div121", /* depth */ 2},
                              {"div12", /* depth */ 1},
                              {"div111", /* depth */ 2},
                              {"div11", /* depth */ 1}});

  TestTraversalIteratorSteps(document, "div12", ":has(> .a > .b)",
                             {{"div123", /* depth */ 1},
                              {"div1223", /* depth */ 2},
                              {"div1222", /* depth */ 2},
                              {"div1221", /* depth */ 2},
                              {"div122", /* depth */ 1},
                              {"div121", /* depth */ 1}});

  TestTraversalIteratorSteps(document, "div122", ":has(> .a > .b)",
                             {{"div1223", /* depth */ 1},
                              {"div1222", /* depth */ 1},
                              {"div1221", /* depth */ 1}});

  TestTraversalIteratorSteps(document, "div11", ":has(> .a > .b)",
                             {{"div111", /* depth */ 1}});

  TestTraversalIteratorForEmptyRange(document, "div111", ":has(> .a > .b)");
}

TEST_F(CheckPseudoHasArgumentContextTest, TestTraversalIteratorCase7) {
  // CheckPseudoHasArgumentTraversalScope::kOneNextSiblingFixedDepthDescendants

  auto* document = HTMLDocument::CreateForTest();
  document->write(R"HTML(
    <!DOCTYPE html>
    <main id=main>
      <div id=div1>
        <div id=div11></div>
      </div>
      <div id=div2>
        <div id=div21></div>
      </div>
      <div id=div3>
        <div id=div31>
          <div id=div311></div>
        </div>
        <div id=div32>
          <div id=div321></div>
        </div>
        <div id=div33></div>
        <div id=div34>
          <div id=div341>
            <div id=div3411></div>
          </div>
        </div>
      </div>
      <div id=div4>
        <div id=div41></div>
      </div>
    </main>
  )HTML");

  TestTraversalIteratorSteps(document, "div1", ":has(+ .a + .b > .c > .d)",
                             {{"div341", /* depth */ 2},
                              {"div34", /* depth */ 1},
                              {"div33", /* depth */ 1},
                              {"div321", /* depth */ 2},
                              {"div32", /* depth */ 1},
                              {"div311", /* depth */ 2},
                              {"div31", /* depth */ 1},
                              {"div3", /* depth */ 0},
                              {"div2", /* depth */ 0}});

  TestTraversalIteratorSteps(document, "div2", ":has(+ .a + .b > .c > .d)",
                             {{"div41", /* depth */ 1},
                              {"div4", /* depth */ 0},
                              {"div3", /* depth */ 0}});

  TestTraversalIteratorSteps(document, "div3", ":has(+ .a + .b > .c > .d)",
                             {{"div4", /* depth */ 0}});

  TestTraversalIteratorSteps(
      document, "div31", ":has(+ .a + .b > .c > .d)",
      {{"div33", /* depth */ 0}, {"div32", /* depth */ 0}});

  TestTraversalIteratorSteps(document, "div32", ":has(+ .a + .b > .c > .d)",
                             {{"div3411", /* depth */ 2},
                              {"div341", /* depth */ 1},
                              {"div34", /* depth */ 0},
                              {"div33", /* depth */ 0}});

  TestTraversalIteratorForEmptyRange(document, "div4",
                                     ":has(+ .a + .b > .c > .d)");
}

TEST_F(CheckPseudoHasArgumentContextTest, TestTraversalIteratorCase8) {
  // CheckPseudoHasArgumentTraversalScope::kAllNextSiblingsFixedDepthDescendants

  auto* document = HTMLDocument::CreateForTest();
  document->write(R"HTML(
    <!DOCTYPE html>
    <main id=main>
      <div id=div1>
        <div id=div11></div>
      </div>
      <div id=div2>
        <div id=div21></div>
      </div>
      <div id=div3>
        <div id=div31>
          <div id=div311></div>
        </div>
        <div id=div32>
          <div id=div321></div>
        </div>
        <div id=div33></div>
        <div id=div34>
          <div id=div341>
            <div id=div3411></div>
          </div>
        </div>
      </div>
      <div id=div4>
        <div id=div41></div>
      </div>
      <div id=div5></div>
    </main>
  )HTML");

  TestTraversalIteratorSteps(document, "div2", ":has(~ .a > .b > .c)",
                             {{"div5", /* depth */ 0},
                              {"div41", /* depth */ 1},
                              {"div4", /* depth */ 0},
                              {"div341", /* depth */ 2},
                              {"div34", /* depth */ 1},
                              {"div33", /* depth */ 1},
                              {"div321", /* depth */ 2},
                              {"div32", /* depth */ 1},
                              {"div311", /* depth */ 2},
                              {"div31", /* depth */ 1},
                              {"div3", /* depth */ 0}});

  TestTraversalIteratorSteps(document, "div31", ":has(~ .a > .b > .c)",
                             {{"div3411", /* depth */ 2},
                              {"div341", /* depth */ 1},
                              {"div34", /* depth */ 0},
                              {"div33", /* depth */ 0},
                              {"div321", /* depth */ 1},
                              {"div32", /* depth */ 0}});

  TestTraversalIteratorSteps(document, "div4", ":has(~ .a > .b > .c)",
                             {{"div5", /* depth */ 0}});

  TestTraversalIteratorForEmptyRange(document, "div5", ":has(~ .a > .b > .c)");
}

}  // namespace blink
