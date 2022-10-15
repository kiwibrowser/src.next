// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/css/css_test_helpers.h"
#include "third_party/blink/renderer/core/css/parser/css_parser.h"
#include "third_party/blink/renderer/core/css/parser/css_parser_context.h"
#include "third_party/blink/renderer/core/css/rule_set.h"

#include "testing/gtest/include/gtest/gtest.h"

#include <iostream>

namespace blink {

namespace {

unsigned Specificity(const String& selector_text) {
  CSSSelectorList selector_list =
      css_test_helpers::ParseSelectorList(selector_text);
  return selector_list.First()->Specificity();
}

bool HasLinkOrVisited(const String& selector_text) {
  CSSSelectorList selector_list =
      css_test_helpers::ParseSelectorList(selector_text);
  return selector_list.First()->HasLinkOrVisited();
}

}  // namespace

TEST(CSSSelector, Representations) {
  css_test_helpers::TestStyleSheet sheet;

  const char* css_rules =
      "summary::-webkit-details-marker { }"
      "* {}"
      "div {}"
      "#id {}"
      ".class {}"
      "[attr] {}"
      "div:hover {}"
      "div:nth-child(2){}"
      ".class#id { }"
      "#id.class { }"
      "[attr]#id { }"
      "div[attr]#id { }"
      "div::first-line { }"
      ".a.b.c { }"
      "div:not(.a) { }"        // without class a
      "div:not(:visited) { }"  // without the visited pseudo class

      "[attr=\"value\"] { }"   // Exact equality
      "[attr~=\"value\"] { }"  // One of a space-separated list
      "[attr^=\"value\"] { }"  // Begins with
      "[attr$=\"value\"] { }"  // Ends with
      "[attr*=\"value\"] { }"  // Substring equal to
      "[attr|=\"value\"] { }"  // One of a hyphen-separated list

      ".a .b { }"    // .b is a descendant of .a
      ".a > .b { }"  // .b is a direct descendant of .a
      ".a ~ .b { }"  // .a precedes .b in sibling order
      ".a + .b { }"  // .a element immediately precedes .b in sibling order
      ".a, .b { }"   // matches .a or .b

      ".a.b .c {}";

  sheet.AddCSSRules(css_rules);
  EXPECT_EQ(29u,
            sheet.GetRuleSet().RuleCount());  // .a, .b counts as two rules.
#ifndef NDEBUG
  sheet.GetRuleSet().Show();
#endif
}

TEST(CSSSelector, OverflowRareDataMatchNth) {
  int max_int = std::numeric_limits<int>::max();
  int min_int = std::numeric_limits<int>::min();
  CSSSelector selector;

  // Overflow count - b (max_int - -1 = max_int + 1)
  selector.SetNth(1, -1);
  EXPECT_FALSE(selector.MatchNth(max_int));
  // 0 - (min_int) = max_int + 1
  selector.SetNth(1, min_int);
  EXPECT_FALSE(selector.MatchNth(0));

  // min_int - 1
  selector.SetNth(-1, min_int);
  EXPECT_FALSE(selector.MatchNth(1));

  // a shouldn't negate to itself (and min_int negates to itself).
  // Note: This test can only fail when using ubsan.
  selector.SetNth(min_int, 10);
  EXPECT_FALSE(selector.MatchNth(2));
}

TEST(CSSSelector, Specificity_Is) {
  EXPECT_EQ(Specificity(".a :is(.b, div.c)"), Specificity(".a div.c"));
  EXPECT_EQ(Specificity(".a :is(.c#d, .e)"), Specificity(".a .c#d"));
  EXPECT_EQ(Specificity(":is(.e+.f, .g>.b, .h)"), Specificity(".e+.f"));
  EXPECT_EQ(Specificity(".a :is(.e+.f, .g>.b, .h#i)"), Specificity(".a .h#i"));
  EXPECT_EQ(Specificity(".a+:is(.b+span.f, :is(.c>.e, .g))"),
            Specificity(".a+.b+span.f"));
  EXPECT_EQ(Specificity("div > :is(div:where(span:where(.b ~ .c)))"),
            Specificity("div > div"));
  EXPECT_EQ(Specificity(":is(.c + .c + .c, .b + .c:not(span), .b + .c + .e)"),
            Specificity(".c + .c + .c"));
}

TEST(CSSSelector, Specificity_Where) {
  EXPECT_EQ(Specificity(".a :where(.b, div.c)"), Specificity(".a"));
  EXPECT_EQ(Specificity(".a :where(.c#d, .e)"), Specificity(".a"));
  EXPECT_EQ(Specificity(":where(.e+.f, .g>.b, .h)"), Specificity("*"));
  EXPECT_EQ(Specificity(".a :where(.e+.f, .g>.b, .h#i)"), Specificity(".a"));
  EXPECT_EQ(Specificity("div > :where(.b+span.f, :where(.c>.e, .g))"),
            Specificity("div"));
  EXPECT_EQ(Specificity("div > :where(div:is(span:is(.b ~ .c)))"),
            Specificity("div"));
  EXPECT_EQ(
      Specificity(":where(.c + .c + .c, .b + .c:not(span), .b + .c + .e)"),
      Specificity("*"));
}

TEST(CSSSelector, Specificity_Slotted) {
  EXPECT_EQ(Specificity("::slotted(.a)"), Specificity(".a::first-line"));
  EXPECT_EQ(Specificity("::slotted(*)"), Specificity("::first-line"));
}

TEST(CSSSelector, Specificity_Host) {
  EXPECT_EQ(Specificity(":host"), Specificity(".host"));
  EXPECT_EQ(Specificity(":host(.a)"), Specificity(".host .a"));
  EXPECT_EQ(Specificity(":host(div#a.b)"), Specificity(".host div#a.b"));
}

TEST(CSSSelector, Specificity_HostContext) {
  EXPECT_EQ(Specificity(":host-context(.a)"), Specificity(".host-context .a"));
  EXPECT_EQ(Specificity(":host-context(div#a.b)"),
            Specificity(".host-context div#a.b"));
}

TEST(CSSSelector, Specificity_Not) {
  EXPECT_EQ(Specificity(":not(div)"), Specificity(":is(div)"));
  EXPECT_EQ(Specificity(":not(.a)"), Specificity(":is(.a)"));
  EXPECT_EQ(Specificity(":not(div.a)"), Specificity(":is(div.a)"));
  EXPECT_EQ(Specificity(".a :not(.b, div.c)"),
            Specificity(".a :is(.b, div.c)"));
  EXPECT_EQ(Specificity(".a :not(.c#d, .e)"), Specificity(".a :is(.c#d, .e)"));
  EXPECT_EQ(Specificity(".a :not(.e+.f, .g>.b, .h#i)"),
            Specificity(".a :is(.e+.f, .g>.b, .h#i)"));
  EXPECT_EQ(Specificity(":not(.c + .c + .c, .b + .c:not(span), .b + .c + .e)"),
            Specificity(":is(.c + .c + .c, .b + .c:not(span), .b + .c + .e)"));
}

TEST(CSSSelector, Specificity_Has) {
  EXPECT_EQ(Specificity(":has(div)"), Specificity("div"));
  EXPECT_EQ(Specificity(":has(div)"), Specificity("* div"));
  EXPECT_EQ(Specificity(":has(~ div)"), Specificity("* ~ div"));
  EXPECT_EQ(Specificity(":has(> .a)"), Specificity("* > .a"));
  EXPECT_EQ(Specificity(":has(+ div.a)"), Specificity("* + div.a"));
  EXPECT_EQ(Specificity(".a :has(.b, div.c)"), Specificity(".a div.c"));
  EXPECT_EQ(Specificity(".a :has(.c#d, .e)"), Specificity(".a .c#d"));
  EXPECT_EQ(Specificity(":has(.e+.f, .g>.b, .h)"), Specificity(".e+.f"));
  EXPECT_EQ(Specificity(".a :has(.e+.f, .g>.b, .h#i)"), Specificity(".a .h#i"));
  EXPECT_EQ(Specificity(".a+:has(.b+span.f, :has(.c>.e, .g))"),
            Specificity(".a+.b+span.f"));
  EXPECT_EQ(Specificity("div > :has(div, div:where(span:where(.b ~ .c)))"),
            Specificity("div > div"));
  EXPECT_EQ(Specificity(":has(.c + .c + .c, .b + .c:not(span), .b + .c + .e)"),
            Specificity(".c + .c + .c"));
}

TEST(CSSSelector, HasLinkOrVisited) {
  EXPECT_FALSE(HasLinkOrVisited("tag"));
  EXPECT_FALSE(HasLinkOrVisited("visited"));
  EXPECT_FALSE(HasLinkOrVisited("link"));
  EXPECT_FALSE(HasLinkOrVisited(".a"));
  EXPECT_FALSE(HasLinkOrVisited("#a:is(visited)"));
  EXPECT_FALSE(HasLinkOrVisited(":not(link):hover"));
  EXPECT_FALSE(HasLinkOrVisited(":hover"));
  EXPECT_FALSE(HasLinkOrVisited(":is(:hover)"));
  EXPECT_FALSE(HasLinkOrVisited(":not(:is(:hover))"));

  EXPECT_TRUE(HasLinkOrVisited(":visited"));
  EXPECT_TRUE(HasLinkOrVisited(":link"));
  EXPECT_TRUE(HasLinkOrVisited(":visited:link"));
  EXPECT_TRUE(HasLinkOrVisited(":not(:visited)"));
  EXPECT_TRUE(HasLinkOrVisited(":not(:link)"));
  EXPECT_TRUE(HasLinkOrVisited(":not(:is(:link))"));
  EXPECT_TRUE(HasLinkOrVisited(":is(:link)"));
  EXPECT_TRUE(HasLinkOrVisited(":is(.a, .b, :is(:visited))"));
  EXPECT_TRUE(HasLinkOrVisited("::cue(:visited)"));
  EXPECT_TRUE(HasLinkOrVisited("::cue(:link)"));
  EXPECT_TRUE(HasLinkOrVisited(":host(:link)"));
  EXPECT_TRUE(HasLinkOrVisited(":host-context(:link)"));
}

TEST(CSSSelector, CueDefaultNamespace) {
  css_test_helpers::TestStyleSheet sheet;

  sheet.AddCSSRules(R"HTML(
    @namespace "http://www.w3.org/1999/xhtml";
    video::cue(b) {}
  )HTML");

  const CSSSelector& cue_selector =
      sheet.GetRuleSet().CuePseudoRules()[0].Selector();
  EXPECT_EQ(cue_selector.GetPseudoType(), CSSSelector::kPseudoCue);

  const CSSSelectorList* cue_arguments = cue_selector.SelectorList();
  ASSERT_TRUE(cue_arguments);
  const CSSSelector* vtt_type_selector = cue_arguments->First();
  ASSERT_TRUE(vtt_type_selector);
  EXPECT_EQ(vtt_type_selector->TagQName().LocalName(), "b");
  // Default namespace should not affect VTT node type selector.
  EXPECT_EQ(vtt_type_selector->TagQName().NamespaceURI(), g_star_atom);
}

TEST(CSSSelector, CopyInvalidList) {
  CSSSelectorList list;
  EXPECT_FALSE(list.IsValid());
  EXPECT_FALSE(list.Copy().IsValid());
}

TEST(CSSSelector, CopyValidList) {
  CSSSelectorList list = css_test_helpers::ParseSelectorList(".a");
  EXPECT_TRUE(list.IsValid());
  EXPECT_TRUE(list.Copy().IsValid());
}

TEST(CSSSelector, FirstInInvalidList) {
  CSSSelectorList list;
  EXPECT_FALSE(list.IsValid());
  EXPECT_FALSE(list.First());
}

}  // namespace blink
