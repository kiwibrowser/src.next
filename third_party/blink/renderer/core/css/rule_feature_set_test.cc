// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/css/rule_feature_set.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/renderer/core/css/css_property_value_set.h"
#include "third_party/blink/renderer/core/css/css_selector_list.h"
#include "third_party/blink/renderer/core/css/css_test_helpers.h"
#include "third_party/blink/renderer/core/css/invalidation/invalidation_set.h"
#include "third_party/blink/renderer/core/css/parser/css_parser.h"
#include "third_party/blink/renderer/core/css/parser/css_parser_selector.h"
#include "third_party/blink/renderer/core/css/parser/media_query_parser.h"
#include "third_party/blink/renderer/core/css/rule_set.h"
#include "third_party/blink/renderer/core/css/style_rule.h"
#include "third_party/blink/renderer/core/dom/element_traversal.h"
#include "third_party/blink/renderer/core/execution_context/security_context.h"
#include "third_party/blink/renderer/core/html/html_body_element.h"
#include "third_party/blink/renderer/core/html/html_document.h"
#include "third_party/blink/renderer/core/html/html_element.h"
#include "third_party/blink/renderer/core/html/html_html_element.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/testing/runtime_enabled_features_test_helpers.h"
#include "third_party/blink/renderer/platform/wtf/vector.h"

namespace blink {

class RuleFeatureSetTest : public testing::Test {
 public:
  RuleFeatureSetTest() = default;

  void SetUp() override {
    document_ = HTMLDocument::CreateForTest();
    auto* html = MakeGarbageCollected<HTMLHtmlElement>(*document_);
    html->AppendChild(MakeGarbageCollected<HTMLBodyElement>(*document_));
    document_->AppendChild(html);

    document_->body()->setInnerHTML("<b><i></i></b>");
  }

  RuleFeatureSet::SelectorPreMatch CollectFeatures(
      const String& selector_text) {
    return CollectFeaturesTo(selector_text, rule_feature_set_);
  }

  static RuleFeatureSet::SelectorPreMatch CollectFeaturesTo(
      CSSSelectorVector& selector_vector,
      const StyleScope* style_scope,
      RuleFeatureSet& set) {
    if (selector_vector.IsEmpty()) {
      return RuleFeatureSet::SelectorPreMatch::kSelectorNeverMatches;
    }

    auto* style_rule = StyleRule::Create(
        selector_vector,
        MakeGarbageCollected<MutableCSSPropertyValueSet>(kHTMLStandardMode));
    return CollectFeaturesTo(style_rule, style_scope, set);
  }

  static RuleFeatureSet::SelectorPreMatch CollectFeaturesTo(
      StyleRule* style_rule,
      const StyleScope* style_scope,
      RuleFeatureSet& set) {
    Vector<wtf_size_t> indices;
    for (const CSSSelector* s = style_rule->FirstSelector(); s;
         s = CSSSelectorList::Next(*s)) {
      indices.push_back(style_rule->SelectorIndex(*s));
    }

    RuleFeatureSet::SelectorPreMatch result =
        RuleFeatureSet::SelectorPreMatch::kSelectorNeverMatches;
    for (wtf_size_t index : indices) {
      RuleData rule_data(style_rule, index, 0, 0, kRuleHasNoSpecialState);
      if (set.CollectFeaturesFromRuleData(&rule_data, style_scope))
        result = RuleFeatureSet::SelectorPreMatch::kSelectorMayMatch;
    }
    return result;
  }

  static RuleFeatureSet::SelectorPreMatch CollectFeaturesTo(
      const String& selector_text,
      RuleFeatureSet& set) {
    CSSSelectorVector selector_vector = CSSParser::ParseSelector(
        StrictCSSParserContext(SecureContextMode::kInsecureContext), nullptr,
        selector_text);
    return CollectFeaturesTo(selector_vector, nullptr /* style_scope */, set);
  }

  void ClearFeatures() { rule_feature_set_.Clear(); }

  void CollectInvalidationSetsForClass(InvalidationLists& invalidation_lists,
                                       const AtomicString& class_name) const {
    Element* element = Traversal<HTMLElement>::FirstChild(
        *Traversal<HTMLElement>::FirstChild(*document_->body()));
    rule_feature_set_.CollectInvalidationSetsForClass(invalidation_lists,
                                                      *element, class_name);
  }

  void CollectInvalidationSetsForId(InvalidationLists& invalidation_lists,
                                    const AtomicString& id) const {
    Element* element = Traversal<HTMLElement>::FirstChild(
        *Traversal<HTMLElement>::FirstChild(*document_->body()));
    rule_feature_set_.CollectInvalidationSetsForId(invalidation_lists, *element,
                                                   id);
  }

  void CollectInvalidationSetsForAttribute(
      InvalidationLists& invalidation_lists,
      const QualifiedName& attribute_name) const {
    Element* element = Traversal<HTMLElement>::FirstChild(
        *Traversal<HTMLElement>::FirstChild(*document_->body()));
    rule_feature_set_.CollectInvalidationSetsForAttribute(
        invalidation_lists, *element, attribute_name);
  }

  void CollectInvalidationSetsForPseudoClass(
      InvalidationLists& invalidation_lists,
      CSSSelector::PseudoType pseudo) const {
    Element* element = Traversal<HTMLElement>::FirstChild(
        *Traversal<HTMLElement>::FirstChild(*document_->body()));
    rule_feature_set_.CollectInvalidationSetsForPseudoClass(invalidation_lists,
                                                            *element, pseudo);
  }

  void CollectPartInvalidationSet(InvalidationLists& invalidation_lists) const {
    rule_feature_set_.CollectPartInvalidationSet(invalidation_lists);
  }

  void CollectUniversalSiblingInvalidationSet(
      InvalidationLists& invalidation_lists) {
    rule_feature_set_.CollectUniversalSiblingInvalidationSet(invalidation_lists,
                                                             1);
  }

  void CollectNthInvalidationSet(InvalidationLists& invalidation_lists) {
    rule_feature_set_.CollectNthInvalidationSet(invalidation_lists);
  }

  bool NeedsHasInvalidationForClass(const AtomicString& class_name) {
    return rule_feature_set_.NeedsHasInvalidationForClass(class_name);
  }

  void AddTo(RuleFeatureSet& rule_feature_set) {
    rule_feature_set.Add(rule_feature_set_);
  }

  using BackingType = InvalidationSet::BackingType;

  template <BackingType type>
  HashSet<AtomicString> ToHashSet(
      typename InvalidationSet::Backing<type>::Range range) {
    HashSet<AtomicString> hash_set;
    for (auto str : range)
      hash_set.insert(str);
    return hash_set;
  }

  HashSet<AtomicString> ClassSet(const InvalidationSet& invalidation_set) {
    return ToHashSet<BackingType::kClasses>(invalidation_set.Classes());
  }

  HashSet<AtomicString> IdSet(const InvalidationSet& invalidation_set) {
    return ToHashSet<BackingType::kIds>(invalidation_set.Ids());
  }

  HashSet<AtomicString> TagNameSet(const InvalidationSet& invalidation_set) {
    return ToHashSet<BackingType::kTagNames>(invalidation_set.TagNames());
  }

  HashSet<AtomicString> AttributeSet(const InvalidationSet& invalidation_set) {
    return ToHashSet<BackingType::kAttributes>(invalidation_set.Attributes());
  }

  void ExpectNoInvalidation(InvalidationSetVector& invalidation_sets) {
    EXPECT_EQ(0u, invalidation_sets.size());
  }

  void ExpectSelfInvalidation(InvalidationSetVector& invalidation_sets) {
    EXPECT_EQ(1u, invalidation_sets.size());
    EXPECT_TRUE(invalidation_sets[0]->InvalidatesSelf());
  }

  void ExpectNoSelfInvalidation(InvalidationSetVector& invalidation_sets) {
    EXPECT_EQ(1u, invalidation_sets.size());
    EXPECT_FALSE(invalidation_sets[0]->InvalidatesSelf());
  }

  void ExpectSelfInvalidationSet(InvalidationSetVector& invalidation_sets) {
    EXPECT_EQ(1u, invalidation_sets.size());
    EXPECT_TRUE(invalidation_sets[0]->IsSelfInvalidationSet());
  }

  void ExpectNotSelfInvalidationSet(InvalidationSetVector& invalidation_sets) {
    EXPECT_EQ(1u, invalidation_sets.size());
    EXPECT_FALSE(invalidation_sets[0]->IsSelfInvalidationSet());
  }

  void ExpectWholeSubtreeInvalidation(
      InvalidationSetVector& invalidation_sets) {
    EXPECT_EQ(1u, invalidation_sets.size());
    EXPECT_TRUE(invalidation_sets[0]->WholeSubtreeInvalid());
  }

  void ExpectClassInvalidation(const AtomicString& class_name,
                               InvalidationSetVector& invalidation_sets) {
    EXPECT_EQ(1u, invalidation_sets.size());
    HashSet<AtomicString> classes = ClassSet(*invalidation_sets[0]);
    EXPECT_EQ(1u, classes.size());
    EXPECT_TRUE(classes.Contains(class_name));
  }

  void ExpectClassInvalidation(const AtomicString& first_class_name,
                               const AtomicString& second_class_name,
                               InvalidationSetVector& invalidation_sets) {
    EXPECT_EQ(1u, invalidation_sets.size());
    HashSet<AtomicString> classes = ClassSet(*invalidation_sets[0]);
    EXPECT_EQ(2u, classes.size());
    EXPECT_TRUE(classes.Contains(first_class_name));
    EXPECT_TRUE(classes.Contains(second_class_name));
  }

  void ExpectClassInvalidation(const AtomicString& first_class_name,
                               const AtomicString& second_class_name,
                               const AtomicString& third_class_name,
                               InvalidationSetVector& invalidation_sets) {
    EXPECT_EQ(1u, invalidation_sets.size());
    HashSet<AtomicString> classes = ClassSet(*invalidation_sets[0]);
    EXPECT_EQ(3u, classes.size());
    EXPECT_TRUE(classes.Contains(first_class_name));
    EXPECT_TRUE(classes.Contains(second_class_name));
    EXPECT_TRUE(classes.Contains(third_class_name));
  }

  void ExpectSiblingClassInvalidation(
      unsigned max_direct_adjacent_selectors,
      const AtomicString& sibling_name,
      InvalidationSetVector& invalidation_sets) {
    EXPECT_EQ(1u, invalidation_sets.size());
    const auto& sibling_invalidation_set =
        To<SiblingInvalidationSet>(*invalidation_sets[0]);
    HashSet<AtomicString> classes = ClassSet(sibling_invalidation_set);
    EXPECT_EQ(1u, classes.size());
    EXPECT_TRUE(classes.Contains(sibling_name));
    EXPECT_EQ(max_direct_adjacent_selectors,
              sibling_invalidation_set.MaxDirectAdjacentSelectors());
  }

  void ExpectSiblingIdInvalidation(unsigned max_direct_adjacent_selectors,
                                   const AtomicString& sibling_name,
                                   InvalidationSetVector& invalidation_sets) {
    EXPECT_EQ(1u, invalidation_sets.size());
    const auto& sibling_invalidation_set =
        To<SiblingInvalidationSet>(*invalidation_sets[0]);
    HashSet<AtomicString> ids = IdSet(*invalidation_sets[0]);
    EXPECT_EQ(1u, ids.size());
    EXPECT_TRUE(ids.Contains(sibling_name));
    EXPECT_EQ(max_direct_adjacent_selectors,
              sibling_invalidation_set.MaxDirectAdjacentSelectors());
  }

  void ExpectSiblingDescendantInvalidation(
      unsigned max_direct_adjacent_selectors,
      const AtomicString& sibling_name,
      const AtomicString& descendant_name,
      InvalidationSetVector& invalidation_sets) {
    EXPECT_EQ(1u, invalidation_sets.size());
    const auto& sibling_invalidation_set =
        To<SiblingInvalidationSet>(*invalidation_sets[0]);
    HashSet<AtomicString> classes = ClassSet(sibling_invalidation_set);
    EXPECT_EQ(1u, classes.size());
    EXPECT_TRUE(classes.Contains(sibling_name));
    EXPECT_EQ(max_direct_adjacent_selectors,
              sibling_invalidation_set.MaxDirectAdjacentSelectors());

    HashSet<AtomicString> descendant_classes =
        ClassSet(*sibling_invalidation_set.SiblingDescendants());
    EXPECT_EQ(1u, descendant_classes.size());
    EXPECT_TRUE(descendant_classes.Contains(descendant_name));
  }

  void ExpectSiblingDescendantInvalidation(
      unsigned max_direct_adjacent_selectors,
      const AtomicString& descendant_name,
      InvalidationSetVector& invalidation_sets) {
    ASSERT_EQ(1u, invalidation_sets.size());
    const auto& sibling_invalidation_set =
        To<SiblingInvalidationSet>(*invalidation_sets[0]);
    EXPECT_TRUE(sibling_invalidation_set.WholeSubtreeInvalid());
    EXPECT_EQ(max_direct_adjacent_selectors,
              sibling_invalidation_set.MaxDirectAdjacentSelectors());
    ASSERT_TRUE(sibling_invalidation_set.SiblingDescendants());
    HashSet<AtomicString> descendant_classes =
        ClassSet(*sibling_invalidation_set.SiblingDescendants());
    EXPECT_EQ(1u, descendant_classes.size());
    EXPECT_TRUE(descendant_classes.Contains(descendant_name));
  }

  void ExpectSiblingAndSiblingDescendantInvalidationForLogicalCombinationsInHas(
      const AtomicString& sibling_name,
      const AtomicString& sibling_name_for_sibling_descendant,
      const AtomicString& descendant_name,
      InvalidationSetVector& invalidation_sets) {
    EXPECT_EQ(1u, invalidation_sets.size());
    const auto& sibling_invalidation_set =
        To<SiblingInvalidationSet>(*invalidation_sets[0]);
    HashSet<AtomicString> classes = ClassSet(sibling_invalidation_set);
    EXPECT_EQ(2u, classes.size());
    EXPECT_TRUE(classes.Contains(sibling_name));
    EXPECT_TRUE(classes.Contains(sibling_name_for_sibling_descendant));
    EXPECT_EQ(SiblingInvalidationSet::kDirectAdjacentMax,
              sibling_invalidation_set.MaxDirectAdjacentSelectors());

    HashSet<AtomicString> descendant_classes =
        ClassSet(*sibling_invalidation_set.SiblingDescendants());
    EXPECT_EQ(1u, descendant_classes.size());
    EXPECT_TRUE(descendant_classes.Contains(descendant_name));
  }

  void ExpectSiblingNoDescendantInvalidation(
      InvalidationSetVector& invalidation_sets) {
    EXPECT_EQ(1u, invalidation_sets.size());
    const auto& sibling_invalidation_set =
        To<SiblingInvalidationSet>(*invalidation_sets[0]);
    EXPECT_FALSE(sibling_invalidation_set.SiblingDescendants());
  }

  void ExpectSiblingWholeSubtreeInvalidation(
      InvalidationSetVector& invalidation_sets) {
    ASSERT_EQ(1u, invalidation_sets.size());
    const auto& sibling_invalidation_set =
        To<SiblingInvalidationSet>(*invalidation_sets[0]);
    ASSERT_TRUE(sibling_invalidation_set.SiblingDescendants());
    EXPECT_TRUE(
        sibling_invalidation_set.SiblingDescendants()->WholeSubtreeInvalid());
  }

  void ExpectIdInvalidation(const AtomicString& id,
                            InvalidationSetVector& invalidation_sets) {
    EXPECT_EQ(1u, invalidation_sets.size());
    HashSet<AtomicString> ids = IdSet(*invalidation_sets[0]);
    EXPECT_EQ(1u, ids.size());
    EXPECT_TRUE(ids.Contains(id));
  }

  void ExpectIdInvalidation(const AtomicString& first_id,
                            const AtomicString& second_id,
                            InvalidationSetVector& invalidation_sets) {
    EXPECT_EQ(1u, invalidation_sets.size());
    HashSet<AtomicString> ids = IdSet(*invalidation_sets[0]);
    EXPECT_EQ(2u, ids.size());
    EXPECT_TRUE(ids.Contains(first_id));
    EXPECT_TRUE(ids.Contains(second_id));
  }

  void ExpectTagNameInvalidation(const AtomicString& tag_name,
                                 InvalidationSetVector& invalidation_sets) {
    EXPECT_EQ(1u, invalidation_sets.size());
    HashSet<AtomicString> tag_names = TagNameSet(*invalidation_sets[0]);
    EXPECT_EQ(1u, tag_names.size());
    EXPECT_TRUE(tag_names.Contains(tag_name));
  }

  void ExpectTagNameInvalidation(const AtomicString& first_tag_name,
                                 const AtomicString& second_tag_name,
                                 InvalidationSetVector& invalidation_sets) {
    EXPECT_EQ(1u, invalidation_sets.size());
    HashSet<AtomicString> tag_names = TagNameSet(*invalidation_sets[0]);
    EXPECT_EQ(2u, tag_names.size());
    EXPECT_TRUE(tag_names.Contains(first_tag_name));
    EXPECT_TRUE(tag_names.Contains(second_tag_name));
  }

  void ExpectAttributeInvalidation(const AtomicString& attribute,
                                   InvalidationSetVector& invalidation_sets) {
    EXPECT_EQ(1u, invalidation_sets.size());
    HashSet<AtomicString> attributes = AttributeSet(*invalidation_sets[0]);
    EXPECT_EQ(1u, attributes.size());
    EXPECT_TRUE(attributes.Contains(attribute));
  }

  void ExpectFullRecalcForRuleSetInvalidation(bool expected) {
    EXPECT_EQ(expected,
              rule_feature_set_.NeedsFullRecalcForRuleSetInvalidation());
  }

  void ExpectPartsInvalidation(InvalidationSetVector& invalidation_sets) {
    EXPECT_EQ(1u, invalidation_sets.size());
    EXPECT_TRUE(invalidation_sets[0]->InvalidatesParts());
  }

  enum class RefCount { kOne, kMany };

  template <typename MapType, typename KeyType>
  void ExpectRefCountForInvalidationSet(const MapType& map,
                                        const KeyType& key,
                                        RefCount ref_count) {
    auto it = map.find(key);
    ASSERT_NE(map.end(), it);

    if (ref_count == RefCount::kOne) {
      EXPECT_TRUE(it->value->HasOneRef());

      // For SiblingInvalidationSets, we also require that the inner
      // InvalidationSets either don't exist, or have a refcount of 1.
      if (it->value->IsSiblingInvalidationSet()) {
        const auto& sibling_invalidation_set =
            To<SiblingInvalidationSet>(*it->value);
        bool sibling_descendants_has_one_ref =
            !sibling_invalidation_set.SiblingDescendants() ||
            sibling_invalidation_set.SiblingDescendants()->HasOneRef();
        bool descendants_has_one_ref =
            !sibling_invalidation_set.Descendants() ||
            sibling_invalidation_set.Descendants()->HasOneRef();
        EXPECT_TRUE(sibling_descendants_has_one_ref);
        EXPECT_TRUE(descendants_has_one_ref);
      }
    } else {
      EXPECT_FALSE(it->value->HasOneRef());
    }
  }

  void ExpectRefCountForClassInvalidationSet(
      const RuleFeatureSet& rule_feature_set,
      const AtomicString& class_name,
      RefCount ref_count) {
    ExpectRefCountForInvalidationSet(rule_feature_set.class_invalidation_sets_,
                                     class_name, ref_count);
  }

  void ExpectRefCountForAttributeInvalidationSet(
      const RuleFeatureSet& rule_feature_set,
      const AtomicString& attribute,
      RefCount ref_count) {
    ExpectRefCountForInvalidationSet(
        rule_feature_set.attribute_invalidation_sets_, attribute, ref_count);
  }

  void ExpectRefCountForIdInvalidationSet(
      const RuleFeatureSet& rule_feature_set,
      const AtomicString& id,
      RefCount ref_count) {
    ExpectRefCountForInvalidationSet(rule_feature_set.id_invalidation_sets_, id,
                                     ref_count);
  }

  void ExpectRefCountForPseudoInvalidationSet(
      const RuleFeatureSet& rule_feature_set,
      CSSSelector::PseudoType key,
      RefCount ref_count) {
    ExpectRefCountForInvalidationSet(rule_feature_set.pseudo_invalidation_sets_,
                                     key, ref_count);
  }

 private:
  RuleFeatureSet rule_feature_set_;
  Persistent<Document> document_;
};

TEST_F(RuleFeatureSetTest, interleavedDescendantSibling1) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures(".p"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "p");
  ExpectSelfInvalidation(invalidation_lists.descendants);
  ExpectNoInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, interleavedDescendantSibling2) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures(".o + .p"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "o");
  ExpectNoInvalidation(invalidation_lists.descendants);
  ExpectSiblingClassInvalidation(1, "p", invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, interleavedDescendantSibling3) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".m + .n .o + .p"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "n");
  ExpectNoSelfInvalidation(invalidation_lists.descendants);
  ExpectClassInvalidation("p", invalidation_lists.descendants);
  ExpectNoInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, interleavedDescendantSibling4) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".m + .n .o + .p"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "m");
  ExpectNoInvalidation(invalidation_lists.descendants);
  ExpectSiblingDescendantInvalidation(1, "n", "p", invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, interleavedDescendantSibling5) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".l ~ .m + .n .o + .p"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "l");
  ExpectNoInvalidation(invalidation_lists.descendants);
  ExpectSiblingDescendantInvalidation(
      SiblingInvalidationSet::kDirectAdjacentMax, "n", "p",
      invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, interleavedDescendantSibling6) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".k > .l ~ .m + .n .o + .p"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "k");
  ExpectClassInvalidation("p", invalidation_lists.descendants);
  ExpectNoInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, anySibling) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":-webkit-any(.q, .r) ~ .s .t"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "q");
  ExpectNoInvalidation(invalidation_lists.descendants);
  ExpectSiblingDescendantInvalidation(
      SiblingInvalidationSet::kDirectAdjacentMax, "s", "t",
      invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, any) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":-webkit-any(.w, .x)"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "w");
  ExpectSelfInvalidation(invalidation_lists.descendants);
  ExpectNoInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, repeatedAny) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":-webkit-any(.v, .w):-webkit-any(.x, .y, .z)"));

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "v");
    ExpectSelfInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "x");
    ExpectSelfInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }
}

TEST_F(RuleFeatureSetTest, anyIdDescendant) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".a :-webkit-any(#b, #c)"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "a");
  ExpectIdInvalidation("b", "c", invalidation_lists.descendants);
}

TEST_F(RuleFeatureSetTest, repeatedAnyDescendant) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".a :-webkit-any(.v, .w):-webkit-any(.x, .y, .z)"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "a");
  ExpectClassInvalidation("v", "w", invalidation_lists.descendants);
}

TEST_F(RuleFeatureSetTest, anyTagDescendant) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".a :-webkit-any(span, div)"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "a");
  ExpectTagNameInvalidation("span", "div", invalidation_lists.descendants);
}

TEST_F(RuleFeatureSetTest, siblingAny) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".v ~ :-webkit-any(.w, .x)"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "v");
  ExpectNoInvalidation(invalidation_lists.descendants);
  ExpectClassInvalidation("w", "x", invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, descendantSiblingAny) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".u .v ~ :-webkit-any(.w, .x)"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "u");
  ExpectClassInvalidation("w", "x", invalidation_lists.descendants);
  ExpectNoInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, id) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures("#a #b"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForId(invalidation_lists, "a");
  ExpectIdInvalidation("b", invalidation_lists.descendants);
}

TEST_F(RuleFeatureSetTest, attribute) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures("[c] [d]"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForAttribute(invalidation_lists,
                                      QualifiedName("", "c", ""));
  ExpectAttributeInvalidation("d", invalidation_lists.descendants);
}

TEST_F(RuleFeatureSetTest, pseudoClass) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures(":valid"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForPseudoClass(invalidation_lists,
                                        CSSSelector::kPseudoValid);
  ExpectSelfInvalidation(invalidation_lists.descendants);
}

TEST_F(RuleFeatureSetTest, tagName) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures(":valid e"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForPseudoClass(invalidation_lists,
                                        CSSSelector::kPseudoValid);
  ExpectTagNameInvalidation("e", invalidation_lists.descendants);
}

TEST_F(RuleFeatureSetTest, nonMatchingHost) {
  EXPECT_EQ(RuleFeatureSet::kSelectorNeverMatches, CollectFeatures(".a:host"));
  EXPECT_EQ(RuleFeatureSet::kSelectorNeverMatches,
            CollectFeatures("*:host(.a)"));
  EXPECT_EQ(RuleFeatureSet::kSelectorNeverMatches,
            CollectFeatures("*:host .a"));
  EXPECT_EQ(RuleFeatureSet::kSelectorNeverMatches,
            CollectFeatures("div :host .a"));
  EXPECT_EQ(RuleFeatureSet::kSelectorNeverMatches,
            CollectFeatures(":host:hover .a"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "a");
  ExpectNoInvalidation(invalidation_lists.descendants);
}

TEST_F(RuleFeatureSetTest, nonMatchingHostContext) {
  EXPECT_EQ(RuleFeatureSet::kSelectorNeverMatches,
            CollectFeatures(".a:host-context(*)"));
  EXPECT_EQ(RuleFeatureSet::kSelectorNeverMatches,
            CollectFeatures("*:host-context(.a)"));
  EXPECT_EQ(RuleFeatureSet::kSelectorNeverMatches,
            CollectFeatures("*:host-context(*) .a"));
  EXPECT_EQ(RuleFeatureSet::kSelectorNeverMatches,
            CollectFeatures("div :host-context(div) .a"));
  EXPECT_EQ(RuleFeatureSet::kSelectorNeverMatches,
            CollectFeatures(":host-context(div):hover .a"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "a");
  ExpectNoInvalidation(invalidation_lists.descendants);
}

TEST_F(RuleFeatureSetTest, emptyIsWhere) {
  EXPECT_EQ(RuleFeatureSet::kSelectorNeverMatches, CollectFeatures(":is()"));
  EXPECT_EQ(RuleFeatureSet::kSelectorNeverMatches, CollectFeatures(":where()"));

  // We do not support :nonsense, so :is()/:where() end up empty.
  // https://drafts.csswg.org/selectors/#typedef-forgiving-selector-list
  EXPECT_EQ(RuleFeatureSet::kSelectorNeverMatches,
            CollectFeatures(":is(:nonsense)"));
  EXPECT_EQ(RuleFeatureSet::kSelectorNeverMatches,
            CollectFeatures(":where(:nonsense)"));
  EXPECT_EQ(RuleFeatureSet::kSelectorNeverMatches,
            CollectFeatures(".a:is(:nonsense)"));
  EXPECT_EQ(RuleFeatureSet::kSelectorNeverMatches,
            CollectFeatures(".b:where(:nonsense)"));
}

TEST_F(RuleFeatureSetTest, universalSiblingInvalidationDirectAdjacent) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures("* + .a"));

  InvalidationLists invalidation_lists;
  CollectUniversalSiblingInvalidationSet(invalidation_lists);

  ExpectSiblingClassInvalidation(1, "a", invalidation_lists.siblings);
  ExpectSelfInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, universalSiblingInvalidationMultipleDirectAdjacent) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures("* + .a + .b"));

  InvalidationLists invalidation_lists;
  CollectUniversalSiblingInvalidationSet(invalidation_lists);

  ExpectSiblingClassInvalidation(2, "b", invalidation_lists.siblings);
  ExpectSelfInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest,
       universalSiblingInvalidationDirectAdjacentDescendant) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures("* + .a .b"));

  InvalidationLists invalidation_lists;
  CollectUniversalSiblingInvalidationSet(invalidation_lists);

  ExpectSiblingDescendantInvalidation(1, "a", "b", invalidation_lists.siblings);
  ExpectNoSelfInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, universalSiblingInvalidationIndirectAdjacent) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures("* ~ .a"));

  InvalidationLists invalidation_lists;
  CollectUniversalSiblingInvalidationSet(invalidation_lists);

  ExpectSiblingClassInvalidation(SiblingInvalidationSet::kDirectAdjacentMax,
                                 "a", invalidation_lists.siblings);
  ExpectSelfInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest,
       universalSiblingInvalidationMultipleIndirectAdjacent) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures("* ~ .a ~ .b"));

  InvalidationLists invalidation_lists;
  CollectUniversalSiblingInvalidationSet(invalidation_lists);

  ExpectSiblingClassInvalidation(SiblingInvalidationSet::kDirectAdjacentMax,
                                 "b", invalidation_lists.siblings);
  ExpectSelfInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest,
       universalSiblingInvalidationIndirectAdjacentDescendant) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures("* ~ .a .b"));

  InvalidationLists invalidation_lists;
  CollectUniversalSiblingInvalidationSet(invalidation_lists);

  ExpectSiblingDescendantInvalidation(
      SiblingInvalidationSet::kDirectAdjacentMax, "a", "b",
      invalidation_lists.siblings);
  ExpectNoSelfInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, universalSiblingInvalidationNot) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":not(.a) + .b"));

  InvalidationLists invalidation_lists;
  CollectUniversalSiblingInvalidationSet(invalidation_lists);

  ExpectSiblingClassInvalidation(1, "b", invalidation_lists.siblings);
  ExpectSelfInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, nonUniversalSiblingInvalidationNot) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures("#x:not(.a) + .b"));

  InvalidationLists invalidation_lists;
  CollectUniversalSiblingInvalidationSet(invalidation_lists);

  ExpectNoInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, nonUniversalSiblingInvalidationAny) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures("#x:-webkit-any(.a) + .b"));

  InvalidationLists invalidation_lists;
  CollectUniversalSiblingInvalidationSet(invalidation_lists);

  ExpectNoInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, universalSiblingInvalidationType) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures("div + .a"));

  InvalidationLists invalidation_lists;
  CollectUniversalSiblingInvalidationSet(invalidation_lists);

  ExpectSiblingClassInvalidation(1, "a", invalidation_lists.siblings);
  ExpectSelfInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, nonUniversalSiblingInvalidationType) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures("div#x + .a"));

  InvalidationLists invalidation_lists;
  CollectUniversalSiblingInvalidationSet(invalidation_lists);

  ExpectNoInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, universalSiblingInvalidationLink) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures(":link + .a"));

  InvalidationLists invalidation_lists;
  CollectUniversalSiblingInvalidationSet(invalidation_lists);

  ExpectSiblingClassInvalidation(1, "a", invalidation_lists.siblings);
  ExpectSelfInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, nonUniversalSiblingInvalidationLink) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures("#x:link + .a"));

  InvalidationLists invalidation_lists;
  CollectUniversalSiblingInvalidationSet(invalidation_lists);

  ExpectNoInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, nthInvalidationUniversal) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":nth-child(2n)"));

  InvalidationLists invalidation_lists;
  CollectNthInvalidationSet(invalidation_lists);

  ExpectNoInvalidation(invalidation_lists.descendants);
  ExpectSelfInvalidation(invalidation_lists.siblings);
  ExpectWholeSubtreeInvalidation(invalidation_lists.siblings);
  ExpectSiblingNoDescendantInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, nthInvalidationClass) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".a:nth-child(2n)"));

  InvalidationLists invalidation_lists;
  CollectNthInvalidationSet(invalidation_lists);

  ExpectNoInvalidation(invalidation_lists.descendants);
  ExpectSelfInvalidation(invalidation_lists.siblings);
  ExpectSiblingClassInvalidation(SiblingInvalidationSet::kDirectAdjacentMax,
                                 "a", invalidation_lists.siblings);
  ExpectSiblingNoDescendantInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, nthInvalidationUniversalDescendant) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":nth-child(2n) *"));

  InvalidationLists invalidation_lists;
  CollectNthInvalidationSet(invalidation_lists);

  ExpectNoInvalidation(invalidation_lists.descendants);
  ExpectNoSelfInvalidation(invalidation_lists.siblings);
  ExpectWholeSubtreeInvalidation(invalidation_lists.siblings);
  ExpectSiblingWholeSubtreeInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, nthInvalidationDescendant) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":nth-child(2n) .a"));

  InvalidationLists invalidation_lists;
  CollectNthInvalidationSet(invalidation_lists);

  ExpectNoInvalidation(invalidation_lists.descendants);
  ExpectNoSelfInvalidation(invalidation_lists.siblings);
  ExpectWholeSubtreeInvalidation(invalidation_lists.siblings);
  ExpectSiblingDescendantInvalidation(
      SiblingInvalidationSet::kDirectAdjacentMax, "a",
      invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, nthInvalidationSibling) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":nth-child(2n) + .a"));

  InvalidationLists invalidation_lists;
  CollectNthInvalidationSet(invalidation_lists);

  ExpectNoInvalidation(invalidation_lists.descendants);
  ExpectSelfInvalidation(invalidation_lists.siblings);
  ExpectClassInvalidation("a", invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, nthInvalidationSiblingDescendant) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":nth-child(2n) + .a .b"));

  InvalidationLists invalidation_lists;
  CollectNthInvalidationSet(invalidation_lists);

  ExpectNoInvalidation(invalidation_lists.descendants);
  ExpectNoSelfInvalidation(invalidation_lists.siblings);
  ExpectSiblingDescendantInvalidation(
      SiblingInvalidationSet::kDirectAdjacentMax, "a", "b",
      invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, nthInvalidationNot) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":not(:nth-child(2n))"));

  InvalidationLists invalidation_lists;
  CollectNthInvalidationSet(invalidation_lists);

  ExpectNoInvalidation(invalidation_lists.descendants);
  ExpectSelfInvalidation(invalidation_lists.siblings);
  ExpectWholeSubtreeInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, nthInvalidationNotClass) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".a:not(:nth-child(2n))"));

  InvalidationLists invalidation_lists;
  CollectNthInvalidationSet(invalidation_lists);

  ExpectNoInvalidation(invalidation_lists.descendants);
  ExpectSelfInvalidation(invalidation_lists.siblings);
  ExpectSiblingClassInvalidation(SiblingInvalidationSet::kDirectAdjacentMax,
                                 "a", invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, nthInvalidationNotDescendant) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".blah:not(:nth-child(2n)) .a"));

  InvalidationLists invalidation_lists;
  CollectNthInvalidationSet(invalidation_lists);

  ExpectNoInvalidation(invalidation_lists.descendants);
  ExpectNoSelfInvalidation(invalidation_lists.siblings);
  ExpectWholeSubtreeInvalidation(invalidation_lists.siblings);
  ExpectSiblingDescendantInvalidation(
      SiblingInvalidationSet::kDirectAdjacentMax, "a",
      invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, nthInvalidationAny) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":-webkit-any(#nomatch, :nth-child(2n))"));

  InvalidationLists invalidation_lists;
  CollectNthInvalidationSet(invalidation_lists);

  ExpectNoInvalidation(invalidation_lists.descendants);
  ExpectSelfInvalidation(invalidation_lists.siblings);
  ExpectWholeSubtreeInvalidation(invalidation_lists.siblings);
  ExpectSiblingNoDescendantInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, nthInvalidationAnyClass) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".a:-webkit-any(#nomatch, :nth-child(2n))"));

  InvalidationLists invalidation_lists;
  CollectNthInvalidationSet(invalidation_lists);

  ExpectNoInvalidation(invalidation_lists.descendants);
  ExpectSelfInvalidation(invalidation_lists.siblings);
  ExpectClassInvalidation("a", invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, nthInvalidationAnyDescendant) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".blah:-webkit-any(#nomatch, :nth-child(2n)) .a"));

  InvalidationLists invalidation_lists;
  CollectNthInvalidationSet(invalidation_lists);

  ExpectNoInvalidation(invalidation_lists.descendants);
  ExpectNoSelfInvalidation(invalidation_lists.siblings);
  ExpectSiblingDescendantInvalidation(
      SiblingInvalidationSet::kDirectAdjacentMax, "a",
      invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, RuleSetInvalidationTypeSelector) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures("div"));
  ExpectFullRecalcForRuleSetInvalidation(false);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures("* div"));
  ExpectFullRecalcForRuleSetInvalidation(false);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures("body *"));
  ExpectFullRecalcForRuleSetInvalidation(true);
}

TEST_F(RuleFeatureSetTest, RuleSetInvalidationClassIdAttr) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures(".c"));
  ExpectFullRecalcForRuleSetInvalidation(false);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures(".c *"));
  ExpectFullRecalcForRuleSetInvalidation(false);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures("#i"));
  ExpectFullRecalcForRuleSetInvalidation(false);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures("#i *"));
  ExpectFullRecalcForRuleSetInvalidation(false);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures("[attr]"));
  ExpectFullRecalcForRuleSetInvalidation(false);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures("[attr] *"));
  ExpectFullRecalcForRuleSetInvalidation(false);
}

TEST_F(RuleFeatureSetTest, RuleSetInvalidationHoverActiveFocus) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":hover:active:focus"));
  ExpectFullRecalcForRuleSetInvalidation(true);
}

TEST_F(RuleFeatureSetTest, RuleSetInvalidationHostContext) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":host-context(.x)"));
  ExpectFullRecalcForRuleSetInvalidation(true);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":host-context(.x) .y"));
  ExpectFullRecalcForRuleSetInvalidation(false);
}

TEST_F(RuleFeatureSetTest, RuleSetInvalidationHost) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures(":host(.x)"));
  ExpectFullRecalcForRuleSetInvalidation(false);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures(":host(*) .y"));
  ExpectFullRecalcForRuleSetInvalidation(false);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures(":host(.x) .y"));
  ExpectFullRecalcForRuleSetInvalidation(false);
}

TEST_F(RuleFeatureSetTest, RuleSetInvalidationNot) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures(":not(.x)"));
  ExpectFullRecalcForRuleSetInvalidation(true);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":not(.x) :hover"));
  ExpectFullRecalcForRuleSetInvalidation(true);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures(":not(.x) .y"));
  ExpectFullRecalcForRuleSetInvalidation(false);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":not(.x) + .y"));
  ExpectFullRecalcForRuleSetInvalidation(false);
}

TEST_F(RuleFeatureSetTest, RuleSetInvalidationCustomPseudo) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures("::-webkit-slider-thumb"));
  ExpectFullRecalcForRuleSetInvalidation(false);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".x::-webkit-slider-thumb"));
  ExpectFullRecalcForRuleSetInvalidation(false);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".x + ::-webkit-slider-thumb"));
  ExpectFullRecalcForRuleSetInvalidation(false);
}

TEST_F(RuleFeatureSetTest, RuleSetInvalidationSlotted) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures("::slotted(*)"));
  ExpectFullRecalcForRuleSetInvalidation(true);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures("::slotted(.y)"));
  ExpectFullRecalcForRuleSetInvalidation(false);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".x::slotted(.y)"));
  ExpectFullRecalcForRuleSetInvalidation(false);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures("[x] ::slotted(.y)"));
  ExpectFullRecalcForRuleSetInvalidation(false);
}

TEST_F(RuleFeatureSetTest, RuleSetInvalidationAnyPseudo) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":-webkit-any(*, #x)"));
  ExpectFullRecalcForRuleSetInvalidation(true);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".x:-webkit-any(*, #y)"));
  ExpectFullRecalcForRuleSetInvalidation(false);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":-webkit-any(:-webkit-any(.a, .b), #x)"));
  ExpectFullRecalcForRuleSetInvalidation(false);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":-webkit-any(:-webkit-any(.a, *), #x)"));
  ExpectFullRecalcForRuleSetInvalidation(true);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":-webkit-any(*, .a) *"));
  ExpectFullRecalcForRuleSetInvalidation(true);
}

TEST_F(RuleFeatureSetTest, SelfInvalidationSet) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures(".a"));
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures("div .b"));
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures("#c"));
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures("[d]"));
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures(":hover"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "a");
  ExpectSelfInvalidation(invalidation_lists.descendants);
  ExpectSelfInvalidationSet(invalidation_lists.descendants);

  invalidation_lists.descendants.clear();
  CollectInvalidationSetsForClass(invalidation_lists, "b");
  ExpectSelfInvalidation(invalidation_lists.descendants);
  ExpectSelfInvalidationSet(invalidation_lists.descendants);

  invalidation_lists.descendants.clear();
  CollectInvalidationSetsForId(invalidation_lists, "c");
  ExpectSelfInvalidation(invalidation_lists.descendants);
  ExpectSelfInvalidationSet(invalidation_lists.descendants);

  invalidation_lists.descendants.clear();
  CollectInvalidationSetsForAttribute(invalidation_lists,
                                      QualifiedName("", "d", ""));
  ExpectSelfInvalidation(invalidation_lists.descendants);
  ExpectSelfInvalidationSet(invalidation_lists.descendants);

  invalidation_lists.descendants.clear();
  CollectInvalidationSetsForPseudoClass(invalidation_lists,
                                        CSSSelector::kPseudoHover);
  ExpectSelfInvalidation(invalidation_lists.descendants);
  ExpectSelfInvalidationSet(invalidation_lists.descendants);
}

TEST_F(RuleFeatureSetTest, ReplaceSelfInvalidationSet) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures(".a"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "a");
  ExpectSelfInvalidation(invalidation_lists.descendants);
  ExpectSelfInvalidationSet(invalidation_lists.descendants);

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures(".a div"));

  invalidation_lists.descendants.clear();
  CollectInvalidationSetsForClass(invalidation_lists, "a");
  ExpectSelfInvalidation(invalidation_lists.descendants);
  ExpectNotSelfInvalidationSet(invalidation_lists.descendants);
}

TEST_F(RuleFeatureSetTest, pseudoIsSibling) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":is(.q, .r) ~ .s .t"));
  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "q");
    ExpectNoInvalidation(invalidation_lists.descendants);
    ExpectSiblingDescendantInvalidation(
        SiblingInvalidationSet::kDirectAdjacentMax, "s", "t",
        invalidation_lists.siblings);
  }
  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "r");
    ExpectNoInvalidation(invalidation_lists.descendants);
    ExpectSiblingDescendantInvalidation(
        SiblingInvalidationSet::kDirectAdjacentMax, "s", "t",
        invalidation_lists.siblings);
  }
}

TEST_F(RuleFeatureSetTest, pseudoIs) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures(":is(.w, .x)"));

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "w");
    ExpectSelfInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }
  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "x");
    ExpectSelfInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }
}

TEST_F(RuleFeatureSetTest, pseudoIsIdDescendant) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".a :is(#b, #c)"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "a");
  ExpectIdInvalidation("b", "c", invalidation_lists.descendants);
}

TEST_F(RuleFeatureSetTest, pseudoIsTagDescendant) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".a :is(span, div)"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "a");
  ExpectTagNameInvalidation("span", "div", invalidation_lists.descendants);
}

TEST_F(RuleFeatureSetTest, pseudoIsAnySibling) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".v ~ :is(.w, .x)"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "v");
  ExpectNoInvalidation(invalidation_lists.descendants);
  ExpectClassInvalidation("w", "x", invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, pseudoIsDescendantSibling) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".u .v ~ :is(.w, .x)"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "u");
  ExpectClassInvalidation("w", "x", invalidation_lists.descendants);
  ExpectNoInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, pseudoIsWithComplexSelectors) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".a :is(.w+.b, .x>#c)"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "a");
  ExpectClassInvalidation("b", invalidation_lists.descendants);
  ExpectIdInvalidation("c", invalidation_lists.descendants);
  ExpectNoInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, pseudoIsNested) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".a :is(.w+.b, .e+:is(.c, #d))"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "a");
  ExpectClassInvalidation("b", "c", invalidation_lists.descendants);
  ExpectIdInvalidation("d", invalidation_lists.descendants);
  ExpectNoInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, pseudoWhere) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":where(.w, .x)"));

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "w");
    ExpectSelfInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }
  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "x");
    ExpectSelfInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }
}

TEST_F(RuleFeatureSetTest, pseudoWhereSibling) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":where(.q, .r) ~ .s .t"));
  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "q");
    ExpectNoInvalidation(invalidation_lists.descendants);
    ExpectSiblingDescendantInvalidation(
        SiblingInvalidationSet::kDirectAdjacentMax, "s", "t",
        invalidation_lists.siblings);
  }
  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "r");
    ExpectNoInvalidation(invalidation_lists.descendants);
    ExpectSiblingDescendantInvalidation(
        SiblingInvalidationSet::kDirectAdjacentMax, "s", "t",
        invalidation_lists.siblings);
  }
}

TEST_F(RuleFeatureSetTest, pseudoWhereIdDescendant) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".a :where(#b, #c)"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "a");
  ExpectIdInvalidation("b", "c", invalidation_lists.descendants);
}

TEST_F(RuleFeatureSetTest, pseudoWhereTagDescendant) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".a :where(span, div)"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "a");
  ExpectTagNameInvalidation("span", "div", invalidation_lists.descendants);
}

TEST_F(RuleFeatureSetTest, pseudoWhereAnySibling) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".v ~ :where(.w, .x)"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "v");
  ExpectNoInvalidation(invalidation_lists.descendants);
  ExpectClassInvalidation("w", "x", invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, pseudoWhereDescendantSibling) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".u .v ~ :where(.w, .x)"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "u");
  ExpectClassInvalidation("w", "x", invalidation_lists.descendants);
  ExpectNoInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, pseudoWhereWithComplexSelectors) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".a :where(.w+.b, .x>#c)"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "a");
  ExpectClassInvalidation("b", invalidation_lists.descendants);
  ExpectIdInvalidation("c", invalidation_lists.descendants);
  ExpectNoInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, pseudoWhereNested) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".a :where(.w+.b, .e+:where(.c, #d))"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "a");
  ExpectClassInvalidation("b", "c", invalidation_lists.descendants);
  ExpectIdInvalidation("d", invalidation_lists.descendants);
  ExpectNoInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, invalidatesParts) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".a .b::part(partname)"));

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "a");
    EXPECT_EQ(1u, invalidation_lists.descendants.size());
    ExpectNoSelfInvalidation(invalidation_lists.descendants);
    EXPECT_TRUE(invalidation_lists.descendants[0]->TreeBoundaryCrossing());
    EXPECT_TRUE(invalidation_lists.descendants[0]->InvalidatesParts());
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "b");
    EXPECT_EQ(1u, invalidation_lists.descendants.size());
    ExpectPartsInvalidation(invalidation_lists.descendants);
    EXPECT_FALSE(invalidation_lists.descendants[0]->WholeSubtreeInvalid());
    EXPECT_TRUE(invalidation_lists.descendants[0]->TreeBoundaryCrossing());
    EXPECT_TRUE(invalidation_lists.descendants[0]->InvalidatesParts());
  }

  {
    InvalidationLists invalidation_lists;
    CollectPartInvalidationSet(invalidation_lists);
    EXPECT_EQ(1u, invalidation_lists.descendants.size());
    ExpectPartsInvalidation(invalidation_lists.descendants);
    EXPECT_TRUE(invalidation_lists.descendants[0]->TreeBoundaryCrossing());
    EXPECT_TRUE(invalidation_lists.descendants[0]->InvalidatesParts());
  }
}

TEST_F(RuleFeatureSetTest, invalidatesTerminalHas) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".a .b:has(.c)"));

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "a");
    ExpectClassInvalidation("b", invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
    EXPECT_FALSE(NeedsHasInvalidationForClass("a"));
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "b");
    ExpectSelfInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
    EXPECT_FALSE(NeedsHasInvalidationForClass("b"));
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "c");
    ExpectNoInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
    EXPECT_TRUE(NeedsHasInvalidationForClass("c"));
  }
}

TEST_F(RuleFeatureSetTest, invalidatesNonTerminalHas) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".a .b:has(.c) .d"));

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "a");
    ExpectClassInvalidation("d", invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
    EXPECT_FALSE(NeedsHasInvalidationForClass("a"));
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "b");
    ExpectClassInvalidation("d", invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
    EXPECT_FALSE(NeedsHasInvalidationForClass("b"));
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "c");
    ExpectNoInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
    EXPECT_TRUE(NeedsHasInvalidationForClass("c"));
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "d");
    ExpectSelfInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
    EXPECT_FALSE(NeedsHasInvalidationForClass("d"));
  }
}

TEST_F(RuleFeatureSetTest, MediaQueryResultFlagsEquality) {
  RuleFeatureSet empty;

  RuleFeatureSet viewport_dependent;
  viewport_dependent.MutableMediaQueryResultFlags().is_viewport_dependent =
      true;

  RuleFeatureSet device_dependent;
  device_dependent.MutableMediaQueryResultFlags().is_device_dependent = true;

  RuleFeatureSet font_unit;
  font_unit.MutableMediaQueryResultFlags().unit_flags =
      MediaQueryExpValue::kFontRelative;

  RuleFeatureSet dynamic_viewport_unit;
  dynamic_viewport_unit.MutableMediaQueryResultFlags().unit_flags =
      MediaQueryExpValue::kDynamicViewport;

  EXPECT_EQ(empty, empty);
  EXPECT_EQ(viewport_dependent, viewport_dependent);
  EXPECT_EQ(device_dependent, device_dependent);
  EXPECT_EQ(font_unit, font_unit);

  EXPECT_NE(viewport_dependent, device_dependent);
  EXPECT_NE(empty, device_dependent);
  EXPECT_NE(font_unit, viewport_dependent);
  EXPECT_NE(font_unit, dynamic_viewport_unit);
}

struct RefTestData {
  const char* main;
  const char* ref;
};

// The test passes if |main| produces the same RuleFeatureSet as |ref|.
RefTestData ref_equal_test_data[] = {
    // clang-format off
    {".a", ".a"},

    // :is
    {":is(.a)", ".a"},
    {":is(.a .b)", ".a .b"},
    {".a :is(.b .c)", ".a .c, .b .c"},
    {".a + :is(.b .c)", ".a + .c, .b .c"},
    {".a + :is(.b .c)", ".a + .c, .b .c"},
    {"div + :is(.b .c)", "div + .c, .b .c"},
    {":is(.a :is(.b + .c))", ".a .c, .b + .c"},
    {".a + :is(.b) :is(.c)", ".a + .b .c"},
    {":is(#a:nth-child(1))", "#a:nth-child(1)"},
    {":is(#a:nth-child(1), #b:nth-child(1))",
     "#a:nth-child(1), #b:nth-child(1)"},
    {":is(#a, #b):nth-child(1)", "#a:nth-child(1), #b:nth-child(1)"},
    {":is(:nth-child(1))", ":nth-child(1)"},
    {".a :is(.b, .c):nth-child(1)", ".a .b:nth-child(1), .a .c:nth-child(1)"},
    // TODO(andruud): We currently add _all_ rightmost features to the nth-
    // sibling set, so .b is added here, since nth-child is present _somewhere_
    // in the rightmost compound. Hence the unexpected '.b:nth-child(1)'
    // selector in the ref.
    {".a :is(.b, .c:nth-child(1))",
     ".a .b, .a .c:nth-child(1), .b:nth-child(1)"},
    {":is(.a) .b", ".a .b"},
    {":is(.a, .b) .c", ".a .c, .b .c"},
    {":is(.a .b, .c .d) .e", ".a .b .e, .c .d .e"},
    {":is(:is(.a .b, .c) :is(.d, .e .f), .g) .h",
     ".a .b .h, .c .h, .d .h, .e .f .h, .g .h"},
    {":is(.a, .b) :is(.c, .d)", ".a .c, .a .d, .b .c, .b .d"},
    {":is(.a .b, .c .d) :is(.e .f, .g .h)",
     ".a .b .f, .a .b .h, .c .d .f, .c .d .h, .e .f, .g .h"},
    {":is(.a + .b)", ".a + .b"},
    {":is(.a + .b, .c + .d) .e", ".a + .b .e, .c + .d .e"},
    {":is(.a ~ .b, .c + .e + .f) :is(.c .d, .e)",
     ".a ~ .b .d, .a ~ .b .e, .c + .e + .f .d, .c + .e + .f .e, .c .d"},
    {":is(.a) + .b", ".a + .b"},
    {":is(.a, .b) + .c", ".a + .c, .b + .c"},
    {":is(.a + .b, .c + .d) + .e", ".a + .b + .e, .c + .d + .e"},
    {":is(.a + .b, .c + .d) + :is(.e + .f, .g + .h)",
     ".a + .b + .f, .a + .b + .h, .c + .d + .f, .c + .d + .h,"
     ".e + .f, .g + .h"},
    {":is(div)", "div"},
    {":is(div, span)", "div, span"},
    {":is(.a, div)", ".a, div"},
    {":is(.a, :is(div, span))", ".a, div, span"},
    {":is(.a, span) :is(div, .b)", ".a div, .a .b, span div, span .b"},
    {":is(.a, span) + :is(div, .b)",
     ".a + div, .a + .b, span + div, span + .b"},
    {":is(.a, .b)::slotted(.c)", ".a::slotted(.c), .b::slotted(.c)"},
    {".a :is(.b, .c)::slotted(.d)", ".a .b::slotted(.d), .a .c::slotted(.d)"},
    {".a + :is(.b, .c)::slotted(.d)",
     ".a + .b::slotted(.d), .a + .c::slotted(.d)"},
    {".a::slotted(:is(.b, .c))", ".a::slotted(.b), .a::slotted(.c)"},
    {":is(.a, .b)::cue(i)", ".a::cue(i), .b::cue(i)"},
    {".a :is(.b, .c)::cue(i)", ".a .b::cue(i), .a .c::cue(i)"},
    {".a + :is(.b, .c)::cue(i)", ".a + .b::cue(i), .a + .c::cue(i)"},
    {".a::cue(:is(.b, .c))", ".a::cue(.b), .a::cue(.c)"},
    {":is(.a, :host + .b, .c) .d", ".a .d, :host + .b .d, .c .d"},
    {":is(.a, :host(.b) .c, .d) div", ".a div, :host(.b) .c div, .d div"},
    {".a::host(:is(.b, .c))", ".a::host(.b), .a::host(.c)"},
    {".a :is(.b, .c)::part(foo)", ".a .b::part(foo), .a .c::part(foo)"},
    {":is(.a, .b)::part(foo)", ".a::part(foo), .b::part(foo)"},
    {":is(.a, .b) :is(.c, .d)::part(foo)",
     ".a .c::part(foo), .a .d ::part(foo),"
     ".b .c::part(foo), .b .d ::part(foo)"},
    {":is(.a, .b)::first-letter", ".a::first-letter, .b::first-letter"},
    {":is(.a, .b .c)::first-line", ".a::first-line, .b .c::first-line"},
    // TODO(andruud): Here we would normally expect a ref:
    // '.a::first-line, .b + .c::first-line', however the latter selector
    // currently marks the sibling invalidation set for .b as whole subtree
    // invalid, whereas the :is() version does not. This could be improved.
    {":is(.a, .b + .c)::first-line", ".a::first-line, .b + .c, .b + .c *"},
    {":is(.a, .b ~ .c > .d)::first-line",
     ".a::first-line, .b ~ .c > .d::first-line"},
    {":is(.a, :host-context(.b), .c)", ".a, :host-context(.b), .c"},
    {":is(.a, :host-context(.b), .c) .d", ".a .d, :host-context(.b) .d, .c .d"},
    {":is(.a, :host-context(.b), .c) + .d",
     ".a + .d, :host-context(.b) + .d, .c + .d"},
    {":host-context(.a) :is(.b, .c)",
     ":host-context(.a) .b, :host-context(.a) .c"},
    {":host-context(:is(.a))", ":host-context(.a)"},
    {":host-context(:is(.a, .b))", ":host-context(.a), :host-context(.b)"},
    {":is(.a, .b + .c).d", ".a.d, .b + .c.d"},
    {".a :is(.b .c .d).e", ".a .d.e, .b .c .d.e"},
    {":is(*)", "*"},
    {".a :is(*)", ".a *"},
    {":is(*) .a", "* .a"},
    {".a + :is(*)", ".a + *"},
    {":is(*) + .a", "* + .a"},
    {".a + :is(.b, *)", ".a + .b, .a + *"},
    {":is(.a, *) + .b", ".a + .b, * + .b"},
    {".a :is(.b, *)", ".a .b, .a *"},
    {":is(.a, *) .b", ".a .b, * .b"},
    {":is(.a + .b, .c) *", ".a + .b *, .c *"},
    {":is(.a + *, .c) *", ".a + * *, .c *"},
    {".a + .b + .c:is(*)", ".a + .b + .c"},
    {".a :not(.b)", ".a *, .b"},
    {".a :not(.b, .c)", ".a *, .b, .c"},
    {".a :not(.b, .c .d)", ".a *, .b, .c .d"},
    {".a :not(.b, .c + .d)", ".a *, .b, .c + .d"},
    {".a + :not(.b, .c + .d)", ".a + *, .b, .c + .d"},
    {":not(.a .b) .c", ".a .c, .b .c"},
    {":not(.a .b, .c) + .d", "* + .d, .a .b + .d, .c + .d"},
    {":not(.a .b, .c .d) :not(.e + .f, .g + .h)",
     ".a .b *, .c .d *, :not(.e + .f), :not(.g + .h)"},
    {":not(.a, .b)", ":not(.a), :not(.b)"},
    {":not(.a .b, .c)", ":not(.a .b), :not(.c)"},
    {":not(.a :not(.b + .c), :not(div))", ":not(.a :not(.b + .c)), :not(div)"},
    {":not(:is(.a))", ":not(.a)"},
    {":not(:is(.a, .b))", ":not(.a), :not(.b)"},
    {":not(:is(.a .b))", ":not(.a .b)"},
    {":not(:is(.a .b, .c + .d))", ":not(.a .b, .c + .d)"},
    {".a :not(:is(.b .c))", ".a :not(.b .c)"},
    {":not(:is(.a)) .b", ":not(.a) .b"},
    {":not(:is(.a .b, .c)) :not(:is(.d + .e, .f))",
     ":not(.a .b, .c) :not(.d + .e, .f)"},
    // We don't have any special support for nested :not(): it's treated
    // as a single :not() level in terms of invalidation:
    {":not(:not(.a))", ":not(.a)"},
    {":not(:not(:not(.a)))", ":not(.a)"},
    {".a :not(:is(:not(.b), .c))", ".a :not(.b), .a :not(.c)"},
    {":not(:is(:not(.a), .b)) .c", ":not(.a) .c, :not(.b) .c"},
    {".a :is(:hover)", ".a :hover"},
    {":is(:hover) .a", ":hover .a"},
    {"button:is(:hover, :focus)", "button:hover, button:focus"},
    {".a :is(.b, :hover)", ".a .b, .a :hover"},
    {".a + :is(:hover) + .c", ".a + :hover + .c"},
    {".a + :is(.b, :hover) + .c", ".a + .b + .c, .a + :hover + .c"},
    {":is(ol, li)::before", "ol::before, li::before"},
    {":is(.a + .b, .c)::before", ".a + .b::before, .c::before"},
    {":is(ol, li)::-internal-input-suggested",
     "ol::-internal-input-suggested, li::-internal-input-suggested"},
    {":is([foo], [bar])", "[foo], [bar]"},
    {".a :is([foo], [bar])", ".a [foo], .a [bar]"},
    {":is([foo], [bar]) .a", "[foo] .a, [bar] .a"},
    {":is([a], [b]) :is([c], [d])", "[a] [c], [a] [d], [b] [c], [b] [d]"},

    // clang-format on
};

// The test passes if |main| does not produce the same RuleFeatureSet as |ref|.
RefTestData ref_not_equal_test_data[] = {
    // clang-format off
    {"", ".a"},
    {"", "#a"},
    {"", "div"},
    {"", ":hover"},
    {"", "::before"},
    {"", ":host"},
    {"", ":host(.a)"},
    {"", ":host-context(.a)"},
    {"", "*"},
    {"", ":not(.a)"},
    {".a", ".b"},
    {".a", ".a, .b"},
    {"#a", "#b"},
    {"ol", "ul"},
    {"[foo]", "[bar]"},
    {":link", ":visited"},
    {".a::before", ".b::after"},
    {"::cue(a)", "::cue(b)"},
    {".a .b", ".a .c"},
    {".a + .b", ".a + .c"},
    {".a + .b .c", ".a + .b .d"},
    {"div + .a", "div + .b"},
    {".a:nth-child(1)", ".b:nth-child(1)"},
    {"div", "span"},
    // clang-format on
};

class RuleFeatureSetRefTest : public RuleFeatureSetTest {
 public:
  void Run(const RefTestData& data) {
    RuleFeatureSet main_set;
    RuleFeatureSet ref_set;

    SCOPED_TRACE(testing::Message() << "Ref: " << data.ref);
    SCOPED_TRACE(testing::Message() << "Main: " << data.main);
    SCOPED_TRACE("Please see RuleFeatureSet::ToString for documentation");

    CollectTo(data.main, main_set);
    CollectTo(data.ref, ref_set);

    Compare(main_set, ref_set);
  }

  virtual void CollectTo(const char*, RuleFeatureSet&) const = 0;
  virtual void Compare(const RuleFeatureSet&, const RuleFeatureSet&) const = 0;
};

class RuleFeatureSetSelectorRefTest : public RuleFeatureSetRefTest {
 public:
  void CollectTo(const char* text, RuleFeatureSet& set) const override {
    CollectFeaturesTo(text, set);
  }
};

class RuleFeatureSetRefEqualTest
    : public RuleFeatureSetSelectorRefTest,
      public testing::WithParamInterface<RefTestData> {
 public:
  void Compare(const RuleFeatureSet& main,
               const RuleFeatureSet& ref) const override {
    EXPECT_EQ(main, ref);
  }
};

INSTANTIATE_TEST_SUITE_P(RuleFeatureSetTest,
                         RuleFeatureSetRefEqualTest,
                         testing::ValuesIn(ref_equal_test_data));

TEST_P(RuleFeatureSetRefEqualTest, All) {
  Run(GetParam());
}

class RuleFeatureSetRefNotEqualTest
    : public RuleFeatureSetSelectorRefTest,
      public testing::WithParamInterface<RefTestData> {
 public:
  void Compare(const RuleFeatureSet& main,
               const RuleFeatureSet& ref) const override {
    EXPECT_NE(main, ref);
  }
};

INSTANTIATE_TEST_SUITE_P(RuleFeatureSetTest,
                         RuleFeatureSetRefNotEqualTest,
                         testing::ValuesIn(ref_not_equal_test_data));

TEST_P(RuleFeatureSetRefNotEqualTest, All) {
  Run(GetParam());
}

RefTestData ref_scope_equal_test_data[] = {
    // Note that for ordering consistency :is() is sometimes used
    // "unnecessarily" in the refs below.
    {"@scope (.a) { div {} }", ".a div, .a:is(div) {}"},
    {"@scope (#a) { div {} }", "#a div, #a:is(div) {}"},
    {"@scope (main) { div {} }", "main div, main:is(div) {}"},
    {"@scope ([foo]) { div {} }", "[foo] div, [foo]:is(div) {}"},
    {"@scope (.a) { .b {} }", ".a .b, .a.b {}"},
    {"@scope (.a) { #b {} }", ".a #b, .a#b {}"},
    {"@scope (.a) { [foo] {} }", ".a [foo], .a[foo] {}"},
    {"@scope (.a) { .a {} }", ".a .a, .a.a {}"},

    // Multiple items in selector lists:
    {"@scope (.a, .b) { div {} }", ":is(.a, .b) div, :is(.a, .b):is(div) {}"},
    {"@scope (.a, :is(.b, .c)) { div {} }",
     ":is(.a, .b, .c) div, :is(.a, .b, .c):is(div) {}"},

    // Using "to" keyword:
    {"@scope (.a, .b) to (.c, .d) { div {} }",
     ":is(.a, .b, .c, .d) div, :is(.a, .b):is(div) {}"},

    // TODO(crbug.com/1280240): Many of the following tests current expect
    // whole-subtree invalidation, because we don't extract any features from
    // :scope. That should be improved.

    // Explicit :scope:
    {"@scope (.a) { :scope {} }", ".a *, .a {}"},
    {"@scope (.a) { .b :scope {} }", ".a :is(.b *), .b .a {}"},
    {"@scope (.a, .b) { :scope {} }", ":is(.a, .b) *, :is(.a, .b) {}"},

    {"@scope (.a) to (:scope) { .b {} }", ".a .b, .a.b {}"},
    {"@scope (.a) to (:scope) { :scope {} }", ".a *, .a {}"},

    // Nested @scopes
    {"@scope (.a, .b) { @scope (.c, .d) { .e {} } }",
     ":is(.a, .b, .c, .d) .e, :is(.a, .b, .c, .d):is(.e) {}"},
    {"@scope (.a, .b) { @scope (.c, .d) { :scope {} } }",
     ":is(.a, .b, .c, .d) *, :is(.a, .b, .c, .d) {}"},
    {"@scope (.a, .b) { @scope (:scope, .c) { :scope {} } }",
     ":is(.a, .b, .c) *, :is(.a, .b, .c) {}"},
    {"@scope (.a) to (.b) { @scope (.c) to (.d) { .e {} } }",
     ":is(.a, .b, .c, .d) .e, :is(.a, .c):is(.e) {}"},
};

class RuleFeatureSetScopeRefTest
    : public RuleFeatureSetRefTest,
      public testing::WithParamInterface<RefTestData>,
      private ScopedCSSScopeForTest {
 public:
  RuleFeatureSetScopeRefTest() : ScopedCSSScopeForTest(true) {}

  void CollectTo(const char* text, RuleFeatureSet& set) const override {
    Document* document = Document::CreateForTest();
    StyleRuleBase* rule = css_test_helpers::ParseRule(*document, text);
    ASSERT_TRUE(rule);

    const StyleScope* scope = nullptr;

    // Find the inner StyleRule.
    while (IsA<StyleRuleScope>(rule)) {
      auto& scope_rule = To<StyleRuleScope>(*rule);
      scope = scope_rule.GetStyleScope().CopyWithParent(scope);
      const HeapVector<Member<StyleRuleBase>>& child_rules =
          scope_rule.ChildRules();
      ASSERT_EQ(1u, child_rules.size());
      rule = child_rules[0].Get();
    }

    auto* style_rule = DynamicTo<StyleRule>(rule);
    ASSERT_TRUE(style_rule);

    CollectFeaturesTo(style_rule, scope, set);
  }

  void Compare(const RuleFeatureSet& main,
               const RuleFeatureSet& ref) const override {
    EXPECT_EQ(main, ref);
  }
};

INSTANTIATE_TEST_SUITE_P(SelectorChecker,
                         RuleFeatureSetScopeRefTest,
                         testing::ValuesIn(ref_scope_equal_test_data));

TEST_P(RuleFeatureSetScopeRefTest, All) {
  Run(GetParam());
}

TEST_F(RuleFeatureSetTest, CopyOnWrite) {
  // RuleFeatureSet local1 has an entry in each of the class/id/attribute/
  // pseudo sets.
  RuleFeatureSet local1;
  CollectFeatures(".a .b");
  CollectFeatures("#d .e");
  CollectFeatures("[thing] .f");
  CollectFeatures(":hover .h");
  AddTo(local1);
  ClearFeatures();
  ExpectRefCountForClassInvalidationSet(local1, "a", RefCount::kOne);
  ExpectRefCountForIdInvalidationSet(local1, "d", RefCount::kOne);
  ExpectRefCountForAttributeInvalidationSet(local1, "thing", RefCount::kOne);
  ExpectRefCountForPseudoInvalidationSet(local1, CSSSelector::kPseudoHover,
                                         RefCount::kOne);

  // RuleFeatureSet local2 overlaps partially with local1.
  RuleFeatureSet local2;
  CollectFeatures(".a .c");
  CollectFeatures("#d img");
  AddTo(local2);
  ClearFeatures();
  ExpectRefCountForClassInvalidationSet(local2, "a", RefCount::kOne);
  ExpectRefCountForIdInvalidationSet(local2, "d", RefCount::kOne);

  // RuleFeatureSet local3 overlaps partially with local1, but not with local2.
  RuleFeatureSet local3;
  CollectFeatures("[thing] .g");
  CollectFeatures(":hover .i");
  AddTo(local3);
  ClearFeatures();
  ExpectRefCountForAttributeInvalidationSet(local3, "thing", RefCount::kOne);
  ExpectRefCountForPseudoInvalidationSet(local3, CSSSelector::kPseudoHover,
                                         RefCount::kOne);

  // Using an empty RuleFeatureSet to simulate the global RuleFeatureSet:
  RuleFeatureSet global;

  // After adding local1, we expect to share the InvalidationSets with local1.
  global.Add(local1);
  ExpectRefCountForClassInvalidationSet(global, "a", RefCount::kMany);
  ExpectRefCountForIdInvalidationSet(global, "d", RefCount::kMany);
  ExpectRefCountForAttributeInvalidationSet(global, "thing", RefCount::kMany);
  ExpectRefCountForPseudoInvalidationSet(global, CSSSelector::kPseudoHover,
                                         RefCount::kMany);

  // For the InvalidationSet keys that overlap with local1, |global| now had to
  // copy the existing InvalidationSets at those keys before modifying them,
  // so we expect |global| to be the only reference holder to those
  // InvalidationSets.
  global.Add(local2);
  ExpectRefCountForClassInvalidationSet(global, "a", RefCount::kOne);
  ExpectRefCountForIdInvalidationSet(global, "d", RefCount::kOne);
  ExpectRefCountForAttributeInvalidationSet(global, "thing", RefCount::kMany);
  ExpectRefCountForPseudoInvalidationSet(global, CSSSelector::kPseudoHover,
                                         RefCount::kMany);

  global.Add(local3);
  ExpectRefCountForClassInvalidationSet(global, "a", RefCount::kOne);
  ExpectRefCountForIdInvalidationSet(global, "d", RefCount::kOne);
  ExpectRefCountForAttributeInvalidationSet(global, "thing", RefCount::kOne);
  ExpectRefCountForPseudoInvalidationSet(global, CSSSelector::kPseudoHover,
                                         RefCount::kOne);
}

TEST_F(RuleFeatureSetTest, CopyOnWrite_SiblingDescendantPairs) {
  // Test data:
  Vector<const char*> data;
  // Descendant.
  data.push_back(".a .b0");
  data.push_back(".a .b1");
  // Sibling.
  data.push_back(".a + .b2");
  data.push_back(".a + .b3");
  // Sibling with sibling descendants.
  data.push_back(".a + .b4 .b5");
  data.push_back(".a + .b6 .b7");
  // Sibling with descendants.
  data.push_back(".a + .b8, .a .b9");
  data.push_back(".a + .b10, .a .b11");
  // Sibling with sibling descendants and descendants.
  data.push_back(".a + .b12 .b13, .a .b14");
  data.push_back(".a + .b15 .b16, .a .b17");

  // For each possible pair in |data|, make sure that we are properly sharing
  // the InvalidationSet from |local1| until we add the InvalidationSet from
  // |local2|.
  for (const char* selector1 : data) {
    for (const char* selector2 : data) {
      RuleFeatureSet local1;
      CollectFeatures(selector1);
      AddTo(local1);
      ClearFeatures();

      RuleFeatureSet local2;
      CollectFeatures(selector2);
      AddTo(local2);
      ClearFeatures();

      RuleFeatureSet global;
      global.Add(local1);
      ExpectRefCountForClassInvalidationSet(global, "a", RefCount::kMany);
      global.Add(local2);
      ExpectRefCountForClassInvalidationSet(global, "a", RefCount::kOne);
    }
  }
}

TEST_F(RuleFeatureSetTest, CopyOnWrite_SelfInvalidation) {
  RuleFeatureSet local1;
  CollectFeatures(".a");
  AddTo(local1);
  ClearFeatures();

  RuleFeatureSet local2;
  CollectFeatures(".a");
  AddTo(local2);
  ClearFeatures();

  // Adding the SelfInvalidationSet to the SelfInvalidationSet does not cause
  // a copy.
  RuleFeatureSet global;
  global.Add(local1);
  ExpectRefCountForClassInvalidationSet(global, "a", RefCount::kMany);
  global.Add(local2);
  ExpectRefCountForClassInvalidationSet(global, "a", RefCount::kMany);
}

TEST_F(RuleFeatureSetTest, isPseudoContainingComplexInsideHas1) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".a:has(:is(.b .c))"));

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "a");
    ExpectSelfInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "b");
    ExpectClassInvalidation("a", invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "c");
    ExpectNoInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }
}

TEST_F(RuleFeatureSetTest, isPseudoContainingComplexInsideHas2) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".a:has(:is(.b > .c))"));

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "a");
    ExpectSelfInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "b");
    ExpectClassInvalidation("a", invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "c");
    ExpectNoInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }
}

TEST_F(RuleFeatureSetTest, isPseudoContainingComplexInsideHas3) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".a:has(~ :is(.b ~ .c))"));

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "a");
    ExpectSelfInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "b");
    ExpectNoInvalidation(invalidation_lists.descendants);
    ExpectSiblingClassInvalidation(SiblingInvalidationSet::kDirectAdjacentMax,
                                   "a", invalidation_lists.siblings);
    ExpectSiblingNoDescendantInvalidation(invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "c");
    ExpectNoInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }
}

TEST_F(RuleFeatureSetTest, isPseudoContainingComplexInsideHas4) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".a:has(~ :is(.b + .c))"));

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "a");
    ExpectSelfInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "b");
    ExpectNoInvalidation(invalidation_lists.descendants);
    ExpectSiblingClassInvalidation(SiblingInvalidationSet::kDirectAdjacentMax,
                                   "a", invalidation_lists.siblings);
    ExpectSiblingNoDescendantInvalidation(invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "c");
    ExpectNoInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }
}

TEST_F(RuleFeatureSetTest, isPseudoContainingComplexInsideHas5) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".a:has(~ :is(.b .c ~ .d))"));

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "a");
    ExpectSelfInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "b");
    ExpectClassInvalidation("a", invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "c");
    ExpectNoInvalidation(invalidation_lists.descendants);
    ExpectSiblingClassInvalidation(SiblingInvalidationSet::kDirectAdjacentMax,
                                   "a", invalidation_lists.siblings);
    ExpectSiblingNoDescendantInvalidation(invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "d");
    ExpectNoInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }
}

TEST_F(RuleFeatureSetTest, isPseudoContainingComplexInsideHas6) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".a:has(~ :is(.b > .c + .d))"));

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "a");
    ExpectSelfInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "b");
    ExpectClassInvalidation("a", invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "c");
    ExpectNoInvalidation(invalidation_lists.descendants);
    ExpectSiblingClassInvalidation(SiblingInvalidationSet::kDirectAdjacentMax,
                                   "a", invalidation_lists.siblings);
    ExpectSiblingNoDescendantInvalidation(invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "d");
    ExpectNoInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }
}

TEST_F(RuleFeatureSetTest, isPseudoContainingComplexInsideHas7) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".a:has(:is(.b ~ .c .d))"));

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "a");
    ExpectSelfInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "b");
    ExpectNoInvalidation(invalidation_lists.descendants);
    ExpectSiblingAndSiblingDescendantInvalidationForLogicalCombinationsInHas(
        "a", "c", "a", invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "c");
    ExpectClassInvalidation("a", invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "d");
    ExpectNoInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }
}

TEST_F(RuleFeatureSetTest, isPseudoContainingComplexInsideHas8) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".a:has(:is(.b + .c > .d))"));

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "a");
    ExpectSelfInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "b");
    ExpectNoInvalidation(invalidation_lists.descendants);
    ExpectSiblingAndSiblingDescendantInvalidationForLogicalCombinationsInHas(
        "a", "c", "a", invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "c");
    ExpectClassInvalidation("a", invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "d");
    ExpectNoInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }
}

TEST_F(RuleFeatureSetTest, isPseudoContainingComplexInsideHas9) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".a:has(:is(:is(.b, .c) .d))"));

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "a");
    ExpectSelfInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "b");
    ExpectClassInvalidation("a", invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "c");
    ExpectClassInvalidation("a", invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "d");
    ExpectNoInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }
}

TEST_F(RuleFeatureSetTest, isPseudoContainingComplexInsideHas10) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".a:has(~ :is(:is(.b, .c) ~ .d))"));

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "a");
    ExpectSelfInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "b");
    ExpectNoInvalidation(invalidation_lists.descendants);
    ExpectSiblingClassInvalidation(SiblingInvalidationSet::kDirectAdjacentMax,
                                   "a", invalidation_lists.siblings);
    ExpectSiblingNoDescendantInvalidation(invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "c");
    ExpectNoInvalidation(invalidation_lists.descendants);
    ExpectSiblingClassInvalidation(SiblingInvalidationSet::kDirectAdjacentMax,
                                   "a", invalidation_lists.siblings);
    ExpectSiblingNoDescendantInvalidation(invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "d");
    ExpectNoInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }
}

TEST_F(RuleFeatureSetTest, isPseudoContainingComplexInsideHas11) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":has(:is(.a .b))"));

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "a");
    ExpectWholeSubtreeInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "b");
    ExpectNoInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }
}

TEST_F(RuleFeatureSetTest, isPseudoContainingComplexInsideHas12) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":has(~ :is(.a ~ .b))"));

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "a");
    ExpectNoInvalidation(invalidation_lists.descendants);
    ExpectSelfInvalidation(invalidation_lists.siblings);
    ExpectSiblingNoDescendantInvalidation(invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "b");
    ExpectNoInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }
}

TEST_F(RuleFeatureSetTest, isPseudoContainingComplexInsideHas13) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".a:has(~ :is(.b ~ .c .d ~ .e))"));

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "a");
    ExpectSelfInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "b");
    ExpectNoInvalidation(invalidation_lists.descendants);
    ExpectSiblingAndSiblingDescendantInvalidationForLogicalCombinationsInHas(
        "a", "c", "a", invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "c");
    ExpectClassInvalidation("a", invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "d");
    ExpectNoInvalidation(invalidation_lists.descendants);
    ExpectSiblingClassInvalidation(SiblingInvalidationSet::kDirectAdjacentMax,
                                   "a", invalidation_lists.siblings);
    ExpectSiblingNoDescendantInvalidation(invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "e");
    ExpectNoInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }
}

TEST_F(RuleFeatureSetTest, isPseudoContainingComplexInsideHas14) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".a:has(~ :is(.b ~ .c)) .d"));

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "b");
    ExpectSiblingDescendantInvalidation(
        SiblingInvalidationSet::kDirectAdjacentMax, "a", "d",
        invalidation_lists.siblings);
    ExpectNoSelfInvalidation(invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "c");
    ExpectNoInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }
}

TEST_F(RuleFeatureSetTest, isPseudoContainingComplexInsideHas15) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".a:has(~ :is(* ~ .b))"));
  {
    InvalidationLists invalidation_lists;
    CollectUniversalSiblingInvalidationSet(invalidation_lists);

    ExpectSiblingClassInvalidation(SiblingInvalidationSet::kDirectAdjacentMax,
                                   "a", invalidation_lists.siblings);
    ExpectSelfInvalidation(invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "b");
    ExpectNoInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }
}

TEST_F(RuleFeatureSetTest, isPseudoContainingComplexInsideHas16) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".a:has(~ :is(* ~ .b)) .c"));

  {
    InvalidationLists invalidation_lists;
    CollectUniversalSiblingInvalidationSet(invalidation_lists);

    ExpectSiblingDescendantInvalidation(
        SiblingInvalidationSet::kDirectAdjacentMax, "a", "c",
        invalidation_lists.siblings);
    ExpectNoSelfInvalidation(invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "b");
    ExpectNoInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }
}

TEST_F(RuleFeatureSetTest, isPseudoContainingComplexInsideHas17) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".a :has(:is(.b .c)).d"));

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "a");
    ExpectClassInvalidation("d", invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "b");
    ExpectWholeSubtreeInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "c");
    ExpectNoInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "d");
    ExpectSelfInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }
}

TEST_F(RuleFeatureSetTest, isPseudoContainingComplexInsideHas18) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".a:has(~ :is(.b ~ :is(.c ~ .d)))"));

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "a");
    ExpectSelfInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "b");
    ExpectNoInvalidation(invalidation_lists.descendants);
    ExpectSiblingClassInvalidation(SiblingInvalidationSet::kDirectAdjacentMax,
                                   "a", invalidation_lists.siblings);
    ExpectSiblingNoDescendantInvalidation(invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "c");
    ExpectNoInvalidation(invalidation_lists.descendants);
    ExpectSiblingClassInvalidation(SiblingInvalidationSet::kDirectAdjacentMax,
                                   "a", invalidation_lists.siblings);
    ExpectSiblingNoDescendantInvalidation(invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "d");
    ExpectNoInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }
}

TEST_F(RuleFeatureSetTest, isPseudoContainingComplexInsideHas19) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".a:has(~ :is(:is(.b ~ .c) ~ .d))"));

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "a");
    ExpectSelfInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "b");
    ExpectNoInvalidation(invalidation_lists.descendants);
    ExpectSiblingClassInvalidation(SiblingInvalidationSet::kDirectAdjacentMax,
                                   "a", invalidation_lists.siblings);
    ExpectSiblingNoDescendantInvalidation(invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "c");
    ExpectNoInvalidation(invalidation_lists.descendants);
    ExpectSiblingClassInvalidation(SiblingInvalidationSet::kDirectAdjacentMax,
                                   "a", invalidation_lists.siblings);
    ExpectSiblingNoDescendantInvalidation(invalidation_lists.siblings);
  }

  {
    InvalidationLists invalidation_lists;
    CollectInvalidationSetsForClass(invalidation_lists, "d");
    ExpectNoInvalidation(invalidation_lists.descendants);
    ExpectNoInvalidation(invalidation_lists.siblings);
  }
}

}  // namespace blink
