// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/css/resolver/match_result.h"

#include "third_party/blink/renderer/core/css/css_property_value_set.h"
#include "third_party/blink/renderer/core/css/css_test_helpers.h"
#include "third_party/blink/renderer/core/dom/shadow_root.h"
#include "third_party/blink/renderer/core/testing/page_test_base.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"

namespace blink {

using css_test_helpers::ParseDeclarationBlock;

class MatchResultTest : public PageTestBase {
 protected:
  void SetUp() override;

  const CSSPropertyValueSet* PropertySet(unsigned index) const {
    return property_sets->at(index).Get();
  }

  size_t LengthOf(const MatchResult& result) const {
    return result.GetMatchedProperties().size();
  }

  CascadeOrigin OriginAt(const MatchResult& result, wtf_size_t index) const {
    DCHECK_LT(index, LengthOf(result));
    return result.GetMatchedProperties()[index].types_.origin;
  }

  const TreeScope& TreeScopeAt(const MatchResult& result, wtf_size_t index) {
    DCHECK_EQ(CascadeOrigin::kAuthor, OriginAt(result, index));
    return result.ScopeFromTreeOrder(
        result.GetMatchedProperties()[index].types_.tree_order);
  }

 private:
  Persistent<HeapVector<Member<MutableCSSPropertyValueSet>, 8>> property_sets;
};

void MatchResultTest::SetUp() {
  PageTestBase::SetUp();
  property_sets =
      MakeGarbageCollected<HeapVector<Member<MutableCSSPropertyValueSet>, 8>>();
  for (unsigned i = 0; i < 8; i++) {
    property_sets->push_back(
        MakeGarbageCollected<MutableCSSPropertyValueSet>(kHTMLQuirksMode));
  }
}

TEST_F(MatchResultTest, CascadeOriginUserAgent) {
  MatchResult result;
  result.AddMatchedProperties(PropertySet(0));
  result.AddMatchedProperties(PropertySet(1));
  result.FinishAddingUARules();
  result.FinishAddingUserRules();
  result.FinishAddingPresentationalHints();
  result.FinishAddingAuthorRulesForTreeScope(GetDocument());

  ASSERT_EQ(LengthOf(result), 2u);
  EXPECT_EQ(OriginAt(result, 0), CascadeOrigin::kUserAgent);
  EXPECT_EQ(OriginAt(result, 1), CascadeOrigin::kUserAgent);
}

TEST_F(MatchResultTest, CascadeOriginUser) {
  MatchResult result;
  result.FinishAddingUARules();
  result.AddMatchedProperties(PropertySet(0));
  result.AddMatchedProperties(PropertySet(1));
  result.FinishAddingUserRules();
  result.FinishAddingPresentationalHints();
  result.FinishAddingAuthorRulesForTreeScope(GetDocument());

  ASSERT_EQ(LengthOf(result), 2u);
  EXPECT_EQ(OriginAt(result, 0), CascadeOrigin::kUser);
  EXPECT_EQ(OriginAt(result, 1), CascadeOrigin::kUser);
}

TEST_F(MatchResultTest, CascadeOriginAuthor) {
  MatchResult result;
  result.FinishAddingUARules();
  result.FinishAddingUserRules();
  result.FinishAddingPresentationalHints();
  result.AddMatchedProperties(PropertySet(0));
  result.AddMatchedProperties(PropertySet(1));
  result.FinishAddingAuthorRulesForTreeScope(GetDocument());

  ASSERT_EQ(LengthOf(result), 2u);
  EXPECT_EQ(OriginAt(result, 0), CascadeOrigin::kAuthor);
  EXPECT_EQ(OriginAt(result, 1), CascadeOrigin::kAuthor);
}

TEST_F(MatchResultTest, CascadeOriginAll) {
  MatchResult result;
  result.AddMatchedProperties(PropertySet(0));
  result.FinishAddingUARules();
  result.AddMatchedProperties(PropertySet(1));
  result.AddMatchedProperties(PropertySet(2));
  result.FinishAddingUserRules();
  result.FinishAddingPresentationalHints();
  result.AddMatchedProperties(PropertySet(3));
  result.AddMatchedProperties(PropertySet(4));
  result.AddMatchedProperties(PropertySet(5));
  result.FinishAddingAuthorRulesForTreeScope(GetDocument());

  ASSERT_EQ(LengthOf(result), 6u);
  EXPECT_EQ(OriginAt(result, 0), CascadeOrigin::kUserAgent);
  EXPECT_EQ(OriginAt(result, 1), CascadeOrigin::kUser);
  EXPECT_EQ(OriginAt(result, 2), CascadeOrigin::kUser);
  EXPECT_EQ(OriginAt(result, 3), CascadeOrigin::kAuthor);
  EXPECT_EQ(OriginAt(result, 4), CascadeOrigin::kAuthor);
  EXPECT_EQ(OriginAt(result, 5), CascadeOrigin::kAuthor);
}

TEST_F(MatchResultTest, CascadeOriginAllExceptUserAgent) {
  MatchResult result;
  result.FinishAddingUARules();
  result.AddMatchedProperties(PropertySet(1));
  result.AddMatchedProperties(PropertySet(2));
  result.FinishAddingUserRules();
  result.FinishAddingPresentationalHints();
  result.AddMatchedProperties(PropertySet(3));
  result.AddMatchedProperties(PropertySet(4));
  result.AddMatchedProperties(PropertySet(5));
  result.FinishAddingAuthorRulesForTreeScope(GetDocument());

  ASSERT_EQ(LengthOf(result), 5u);
  EXPECT_EQ(OriginAt(result, 0), CascadeOrigin::kUser);
  EXPECT_EQ(OriginAt(result, 1), CascadeOrigin::kUser);
  EXPECT_EQ(OriginAt(result, 2), CascadeOrigin::kAuthor);
  EXPECT_EQ(OriginAt(result, 3), CascadeOrigin::kAuthor);
  EXPECT_EQ(OriginAt(result, 4), CascadeOrigin::kAuthor);
}

TEST_F(MatchResultTest, CascadeOriginAllExceptUser) {
  MatchResult result;
  result.AddMatchedProperties(PropertySet(0));
  result.FinishAddingUARules();
  result.FinishAddingUserRules();
  result.FinishAddingPresentationalHints();
  result.AddMatchedProperties(PropertySet(3));
  result.AddMatchedProperties(PropertySet(4));
  result.AddMatchedProperties(PropertySet(5));
  result.FinishAddingAuthorRulesForTreeScope(GetDocument());

  ASSERT_EQ(LengthOf(result), 4u);
  EXPECT_EQ(OriginAt(result, 0), CascadeOrigin::kUserAgent);
  EXPECT_EQ(OriginAt(result, 1), CascadeOrigin::kAuthor);
  EXPECT_EQ(OriginAt(result, 2), CascadeOrigin::kAuthor);
  EXPECT_EQ(OriginAt(result, 3), CascadeOrigin::kAuthor);
}

TEST_F(MatchResultTest, CascadeOriginAllExceptAuthor) {
  MatchResult result;
  result.AddMatchedProperties(PropertySet(0));
  result.FinishAddingUARules();
  result.AddMatchedProperties(PropertySet(1));
  result.AddMatchedProperties(PropertySet(2));
  result.FinishAddingUserRules();
  result.FinishAddingPresentationalHints();
  result.FinishAddingAuthorRulesForTreeScope(GetDocument());

  ASSERT_EQ(LengthOf(result), 3u);
  EXPECT_EQ(OriginAt(result, 0), CascadeOrigin::kUserAgent);
  EXPECT_EQ(OriginAt(result, 1), CascadeOrigin::kUser);
  EXPECT_EQ(OriginAt(result, 2), CascadeOrigin::kUser);
}

TEST_F(MatchResultTest, CascadeOriginTreeScopes) {
  MatchResult result;
  result.AddMatchedProperties(PropertySet(0));
  result.FinishAddingUARules();
  result.AddMatchedProperties(PropertySet(1));
  result.FinishAddingUserRules();
  result.FinishAddingPresentationalHints();
  result.AddMatchedProperties(PropertySet(2));
  result.FinishAddingAuthorRulesForTreeScope(GetDocument());
  result.AddMatchedProperties(PropertySet(3));
  result.AddMatchedProperties(PropertySet(4));
  result.FinishAddingAuthorRulesForTreeScope(GetDocument());
  result.AddMatchedProperties(PropertySet(5));
  result.AddMatchedProperties(PropertySet(6));
  result.AddMatchedProperties(PropertySet(7));
  result.FinishAddingAuthorRulesForTreeScope(GetDocument());

  ASSERT_EQ(LengthOf(result), 8u);
  EXPECT_EQ(OriginAt(result, 0), CascadeOrigin::kUserAgent);
  EXPECT_EQ(OriginAt(result, 1), CascadeOrigin::kUser);
  EXPECT_EQ(OriginAt(result, 2), CascadeOrigin::kAuthor);
  EXPECT_EQ(OriginAt(result, 3), CascadeOrigin::kAuthor);
  EXPECT_EQ(OriginAt(result, 4), CascadeOrigin::kAuthor);
  EXPECT_EQ(OriginAt(result, 5), CascadeOrigin::kAuthor);
  EXPECT_EQ(OriginAt(result, 6), CascadeOrigin::kAuthor);
  EXPECT_EQ(OriginAt(result, 7), CascadeOrigin::kAuthor);
}

TEST_F(MatchResultTest, Reset) {
  MatchResult result;
  result.AddMatchedProperties(PropertySet(0));
  result.FinishAddingUARules();
  result.AddMatchedProperties(PropertySet(1));
  result.FinishAddingUserRules();
  result.FinishAddingPresentationalHints();
  result.AddMatchedProperties(PropertySet(2));
  result.FinishAddingAuthorRulesForTreeScope(GetDocument());
  result.AddMatchedProperties(PropertySet(3));
  result.FinishAddingAuthorRulesForTreeScope(GetDocument());
  result.AddMatchedProperties(PropertySet(4));
  result.FinishAddingAuthorRulesForTreeScope(GetDocument());

  ASSERT_EQ(LengthOf(result), 5u);
  EXPECT_EQ(OriginAt(result, 0), CascadeOrigin::kUserAgent);
  EXPECT_EQ(OriginAt(result, 1), CascadeOrigin::kUser);
  EXPECT_EQ(OriginAt(result, 2), CascadeOrigin::kAuthor);
  EXPECT_EQ(OriginAt(result, 3), CascadeOrigin::kAuthor);
  EXPECT_EQ(OriginAt(result, 4), CascadeOrigin::kAuthor);

  // Check tree_order of last entry.
  EXPECT_TRUE(result.HasMatchedProperties());
  ASSERT_EQ(5u, result.GetMatchedProperties().size());
  EXPECT_EQ(2u, result.GetMatchedProperties()[4].types_.tree_order);

  EXPECT_TRUE(result.IsCacheable());
  result.SetIsCacheable(false);
  EXPECT_FALSE(result.IsCacheable());

  result.Reset();

  EXPECT_TRUE(result.IsCacheable());
  EXPECT_FALSE(result.GetMatchedProperties().size());
  EXPECT_FALSE(result.HasMatchedProperties());

  // Add same declarations again.
  result.AddMatchedProperties(PropertySet(0));
  result.FinishAddingUARules();
  result.AddMatchedProperties(PropertySet(1));
  result.FinishAddingUserRules();
  result.FinishAddingPresentationalHints();
  result.AddMatchedProperties(PropertySet(2));
  result.FinishAddingAuthorRulesForTreeScope(GetDocument());
  result.AddMatchedProperties(PropertySet(3));
  result.FinishAddingAuthorRulesForTreeScope(GetDocument());
  result.AddMatchedProperties(PropertySet(4));
  result.FinishAddingAuthorRulesForTreeScope(GetDocument());

  ASSERT_EQ(LengthOf(result), 5u);
  EXPECT_EQ(OriginAt(result, 0), CascadeOrigin::kUserAgent);
  EXPECT_EQ(OriginAt(result, 1), CascadeOrigin::kUser);
  EXPECT_EQ(OriginAt(result, 2), CascadeOrigin::kAuthor);
  EXPECT_EQ(OriginAt(result, 3), CascadeOrigin::kAuthor);
  EXPECT_EQ(OriginAt(result, 4), CascadeOrigin::kAuthor);

  // Check tree_order of last entry.
  EXPECT_TRUE(result.HasMatchedProperties());
  ASSERT_EQ(5u, result.GetMatchedProperties().size());
  EXPECT_EQ(2u, result.GetMatchedProperties()[4].types_.tree_order);

  EXPECT_TRUE(result.IsCacheable());
}

TEST_F(MatchResultTest, ResetTreeScope) {
  SetBodyInnerHTML("<div id=host1></div><div id=host2></div>");
  Element* host1 = GetElementById("host1");
  Element* host2 = GetElementById("host2");
  ASSERT_TRUE(host1);
  ASSERT_TRUE(host2);
  TreeScope& scope1 = host1->AttachShadowRootInternal(ShadowRootType::kOpen);
  TreeScope& scope2 = host2->AttachShadowRootInternal(ShadowRootType::kOpen);

  MatchResult result;
  result.FinishAddingUARules();
  result.FinishAddingUserRules();
  result.FinishAddingPresentationalHints();
  result.AddMatchedProperties(PropertySet(0));
  result.FinishAddingAuthorRulesForTreeScope(scope1);

  ASSERT_EQ(LengthOf(result), 1u);
  EXPECT_EQ(&TreeScopeAt(result, 0), &scope1);

  result.Reset();

  result.FinishAddingUARules();
  result.FinishAddingUserRules();
  result.FinishAddingPresentationalHints();
  result.AddMatchedProperties(PropertySet(0));
  result.FinishAddingAuthorRulesForTreeScope(scope2);

  ASSERT_EQ(LengthOf(result), 1u);
  EXPECT_EQ(&TreeScopeAt(result, 0), &scope2);
}

}  // namespace blink
