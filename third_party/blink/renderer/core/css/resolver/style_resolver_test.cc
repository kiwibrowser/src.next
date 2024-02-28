// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/css/resolver/style_resolver.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/mojom/frame/user_activation_notification_type.mojom-blink.h"
#include "third_party/blink/public/web/web_print_page_description.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_union_cssnumericvalue_double.h"
#include "third_party/blink/renderer/core/animation/animation_test_helpers.h"
#include "third_party/blink/renderer/core/animation/document_timeline.h"
#include "third_party/blink/renderer/core/animation/element_animations.h"
#include "third_party/blink/renderer/core/css/calculation_expression_anchor_query_node.h"
#include "third_party/blink/renderer/core/css/cascade_layer_map.h"
#include "third_party/blink/renderer/core/css/css_image_set_value.h"
#include "third_party/blink/renderer/core/css/css_image_value.h"
#include "third_party/blink/renderer/core/css/css_test_helpers.h"
#include "third_party/blink/renderer/core/css/css_value_list.h"
#include "third_party/blink/renderer/core/css/parser/css_parser_local_context.h"
#include "third_party/blink/renderer/core/css/position_fallback_data.h"
#include "third_party/blink/renderer/core/css/properties/computed_style_utils.h"
#include "third_party/blink/renderer/core/css/properties/css_property_ref.h"
#include "third_party/blink/renderer/core/css/properties/longhands.h"
#include "third_party/blink/renderer/core/css/resolver/scoped_style_resolver.h"
#include "third_party/blink/renderer/core/css/resolver/style_resolver_state.h"
#include "third_party/blink/renderer/core/css/style_change_reason.h"
#include "third_party/blink/renderer/core/css/style_engine.h"
#include "third_party/blink/renderer/core/css/style_sheet_contents.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/dom/node_computed_style.h"
#include "third_party/blink/renderer/core/dom/pseudo_element.h"
#include "third_party/blink/renderer/core/dom/shadow_root.h"
#include "third_party/blink/renderer/core/dom/text.h"
#include "third_party/blink/renderer/core/execution_context/security_context.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/fullscreen/fullscreen.h"
#include "third_party/blink/renderer/core/html/html_dialog_element.h"
#include "third_party/blink/renderer/core/html/html_style_element.h"
#include "third_party/blink/renderer/core/layout/layout_view.h"
#include "third_party/blink/renderer/core/style/anchor_specifier_value.h"
#include "third_party/blink/renderer/core/style/computed_style_constants.h"
#include "third_party/blink/renderer/core/testing/page_test_base.h"
#include "third_party/blink/renderer/platform/bindings/exception_state.h"
#include "third_party/blink/renderer/platform/geometry/calculation_value.h"
#include "third_party/blink/renderer/platform/testing/runtime_enabled_features_test_helpers.h"

namespace blink {

using animation_test_helpers::CreateSimpleKeyframeEffectForTest;

class StyleResolverTest : public PageTestBase {
 protected:
  const ComputedStyle* StyleForId(const char* id) {
    Element* element = GetElementById(id);
    StyleRecalcContext recalc_context;
    recalc_context.old_style = element->GetComputedStyle();
    const auto* style = GetStyleEngine().GetStyleResolver().ResolveStyle(
        element, recalc_context);
    DCHECK(style);
    return style;
  }

  String ComputedValue(String name, const ComputedStyle& style) {
    CSSPropertyRef ref(name, GetDocument());
    DCHECK(ref.IsValid());
    return ref.GetProperty()
        .CSSValueFromComputedStyle(style, nullptr, false)
        ->CssText();
  }

  void MatchAllRules(StyleResolverState& state,
                     ElementRuleCollector& collector) {
    GetDocument().GetStyleEngine().GetStyleResolver().MatchAllRules(
        state, collector, false /* include_smil_properties */);
  }

  bool IsUseCounted(mojom::WebFeature feature) {
    return GetDocument().IsUseCounted(feature);
  }

  // Access protected inset and sizing property getters
  const Length& GetTop(const ComputedStyle& style) const { return style.Top(); }
  const Length& GetBottom(const ComputedStyle& style) const {
    return style.Bottom();
  }
  const Length& GetLeft(const ComputedStyle& style) const {
    return style.Left();
  }
  const Length& GetRight(const ComputedStyle& style) const {
    return style.Right();
  }
  const Length& GetWidth(const ComputedStyle& style) const {
    return style.Width();
  }
  const Length& GetMinWidth(const ComputedStyle& style) const {
    return style.MinWidth();
  }
  const Length& GetMaxWidth(const ComputedStyle& style) const {
    return style.MaxWidth();
  }
  const Length& GetHeight(const ComputedStyle& style) const {
    return style.Height();
  }
  const Length& GetMinHeight(const ComputedStyle& style) const {
    return style.MinHeight();
  }
  const Length& GetMaxHeight(const ComputedStyle& style) const {
    return style.MaxHeight();
  }

  void UpdateStyleForPositionFallback(Element& element,
                                      ScopedCSSName* name,
                                      wtf_size_t index) {
    DCHECK(name);
    StyleRulePositionFallback* rule =
        GetStyleEngine().GetPositionFallbackRule(*name);
    if (rule) {
      const CSSPropertyValueSet* set = rule->TryPropertyValueSetAt(index);
      GetStyleEngine().UpdateStyleForPositionFallback(element, set);
    }
  }
};

// Variant of `StyleResolverTest` that runs with and without CSSMPCImprovements
// to ensure we have some test coverage of both codepaths.
class ParameterizedStyleResolverTest : public StyleResolverTest,
                                       public testing::WithParamInterface<bool>,
                                       private ScopedCSSMPCImprovementsForTest {
 public:
  ParameterizedStyleResolverTest()
      : ScopedCSSMPCImprovementsForTest(GetParam()) {}
};

INSTANTIATE_TEST_SUITE_P(All, ParameterizedStyleResolverTest, testing::Bool());

class StyleResolverTestCQ : public ParameterizedStyleResolverTest {
 protected:
  StyleResolverTestCQ() = default;
};

INSTANTIATE_TEST_SUITE_P(All, StyleResolverTestCQ, testing::Bool());

TEST_P(ParameterizedStyleResolverTest, StyleForTextInDisplayNone) {
  GetDocument().documentElement()->setInnerHTML(R"HTML(
    <body style="display:none">Text</body>
  )HTML");

  UpdateAllLifecyclePhasesForTest();

  GetDocument().body()->EnsureComputedStyle();

  ASSERT_TRUE(GetDocument().body()->GetComputedStyle());
  EXPECT_TRUE(
      GetDocument().body()->GetComputedStyle()->IsEnsuredInDisplayNone());
  EXPECT_FALSE(GetStyleEngine().GetStyleResolver().StyleForText(
      To<Text>(GetDocument().body()->firstChild())));
}

TEST_P(ParameterizedStyleResolverTest, AnimationBaseComputedStyle) {
  GetDocument().documentElement()->setInnerHTML(R"HTML(
    <style>
      html { font-size: 10px; }
      body { font-size: 20px; }
      @keyframes fade { to { opacity: 0; }}
      #div { animation: fade 1s; }
    </style>
    <div id="div">Test</div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();

  Element* div = GetDocument().getElementById(AtomicString("div"));
  ElementAnimations& animations = div->EnsureElementAnimations();
  animations.SetAnimationStyleChange(true);

  StyleResolver& resolver = GetStyleEngine().GetStyleResolver();
  StyleRecalcContext recalc_context;
  recalc_context.old_style = div->GetComputedStyle();
  const auto* style1 = resolver.ResolveStyle(div, recalc_context);
  ASSERT_TRUE(style1);
  EXPECT_EQ(20, style1->FontSize());
  ASSERT_TRUE(style1->GetBaseComputedStyle());
  EXPECT_EQ(20, style1->GetBaseComputedStyle()->FontSize());

  // Getting style with customized parent style should not affect previously
  // produced animation base computed style.
  const ComputedStyle* parent_style =
      GetDocument().documentElement()->GetComputedStyle();
  StyleRequest style_request;
  style_request.parent_override = parent_style;
  style_request.layout_parent_override = parent_style;
  style_request.can_trigger_animations = false;
  EXPECT_EQ(
      10,
      resolver.ResolveStyle(div, recalc_context, style_request)->FontSize());
  ASSERT_TRUE(style1->GetBaseComputedStyle());
  EXPECT_EQ(20, style1->GetBaseComputedStyle()->FontSize());
  EXPECT_EQ(20, resolver.ResolveStyle(div, recalc_context)->FontSize());
}

TEST_P(ParameterizedStyleResolverTest, HasEmUnits) {
  GetDocument().documentElement()->setInnerHTML("<div id=div>Test</div>");
  UpdateAllLifecyclePhasesForTest();
  EXPECT_FALSE(StyleForId("div")->HasEmUnits());

  GetDocument().documentElement()->setInnerHTML(
      "<div id=div style='width:1em'>Test</div>");
  UpdateAllLifecyclePhasesForTest();
  EXPECT_TRUE(StyleForId("div")->HasEmUnits());
}

TEST_P(ParameterizedStyleResolverTest, BaseReusableIfFontRelativeUnitsAbsent) {
  GetDocument().documentElement()->setInnerHTML("<div id=div>Test</div>");
  UpdateAllLifecyclePhasesForTest();
  Element* div = GetDocument().getElementById(AtomicString("div"));

  auto* effect = CreateSimpleKeyframeEffectForTest(
      div, CSSPropertyID::kFontSize, "50px", "100px");
  GetDocument().Timeline().Play(effect);
  UpdateAllLifecyclePhasesForTest();

  EXPECT_EQ("50px", ComputedValue("font-size", *StyleForId("div")));

  div->SetNeedsAnimationStyleRecalc();
  GetDocument().Lifecycle().AdvanceTo(DocumentLifecycle::kInStyleRecalc);
  StyleForId("div");

  StyleResolverState state(GetDocument(), *div);
  EXPECT_TRUE(StyleResolver::CanReuseBaseComputedStyle(state));
}

TEST_P(ParameterizedStyleResolverTest, AnimationNotMaskedByImportant) {
  GetDocument().documentElement()->setInnerHTML(R"HTML(
    <style>
      div {
        width: 10px;
        height: 10px !important;
      }
    </style>
    <div id=div></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  Element* div = GetDocument().getElementById(AtomicString("div"));

  auto* effect = CreateSimpleKeyframeEffectForTest(div, CSSPropertyID::kWidth,
                                                   "50px", "100px");
  GetDocument().Timeline().Play(effect);
  UpdateAllLifecyclePhasesForTest();

  EXPECT_EQ("50px", ComputedValue("width", *StyleForId("div")));
  EXPECT_EQ("10px", ComputedValue("height", *StyleForId("div")));

  div->SetNeedsAnimationStyleRecalc();
  GetDocument().Lifecycle().AdvanceTo(DocumentLifecycle::kInStyleRecalc);
  const auto* style = StyleForId("div");

  const CSSBitset* bitset = style->GetBaseImportantSet();
  EXPECT_FALSE(CSSAnimations::IsAnimatingStandardProperties(
      div->GetElementAnimations(), bitset, KeyframeEffect::kDefaultPriority));
  EXPECT_TRUE(style->GetBaseComputedStyle());
  EXPECT_FALSE(bitset && bitset->Has(CSSPropertyID::kWidth));
  EXPECT_TRUE(bitset && bitset->Has(CSSPropertyID::kHeight));
}

TEST_P(ParameterizedStyleResolverTest,
       AnimationNotMaskedWithoutElementAnimations) {
  EXPECT_FALSE(CSSAnimations::IsAnimatingStandardProperties(
      /* ElementAnimations */ nullptr, std::make_unique<CSSBitset>().get(),
      KeyframeEffect::kDefaultPriority));
}

TEST_P(ParameterizedStyleResolverTest, AnimationNotMaskedWithoutBitset) {
  GetDocument().documentElement()->setInnerHTML(R"HTML(
    <style>
      div {
        width: 10px;
        height: 10px !important;
      }
    </style>
    <div id=div></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  Element* div = GetDocument().getElementById(AtomicString("div"));

  auto* effect = CreateSimpleKeyframeEffectForTest(div, CSSPropertyID::kWidth,
                                                   "50px", "100px");
  GetDocument().Timeline().Play(effect);
  UpdateAllLifecyclePhasesForTest();

  EXPECT_EQ("50px", ComputedValue("width", *StyleForId("div")));
  EXPECT_EQ("10px", ComputedValue("height", *StyleForId("div")));

  div->SetNeedsAnimationStyleRecalc();
  GetDocument().Lifecycle().AdvanceTo(DocumentLifecycle::kInStyleRecalc);
  StyleForId("div");

  ASSERT_TRUE(div->GetElementAnimations());
  EXPECT_FALSE(CSSAnimations::IsAnimatingStandardProperties(
      div->GetElementAnimations(), /* CSSBitset */ nullptr,
      KeyframeEffect::kDefaultPriority));
}

TEST_P(ParameterizedStyleResolverTest, AnimationMaskedByImportant) {
  GetDocument().documentElement()->setInnerHTML(R"HTML(
    <style>
      div {
        width: 10px;
        height: 10px !important;
      }
    </style>
    <div id=div></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  Element* div = GetDocument().getElementById(AtomicString("div"));

  auto* effect = CreateSimpleKeyframeEffectForTest(div, CSSPropertyID::kHeight,
                                                   "50px", "100px");
  GetDocument().Timeline().Play(effect);
  UpdateAllLifecyclePhasesForTest();

  EXPECT_EQ("10px", ComputedValue("width", *StyleForId("div")));
  EXPECT_EQ("10px", ComputedValue("height", *StyleForId("div")));

  div->SetNeedsAnimationStyleRecalc();
  GetDocument().Lifecycle().AdvanceTo(DocumentLifecycle::kInStyleRecalc);
  const auto* style = StyleForId("div");

  EXPECT_TRUE(style->GetBaseComputedStyle());
  EXPECT_TRUE(style->GetBaseImportantSet());

  StyleResolverState state(GetDocument(), *div);
  EXPECT_FALSE(StyleResolver::CanReuseBaseComputedStyle(state));
}

TEST_P(ParameterizedStyleResolverTest,
       TransitionRetargetRelativeFontSizeOnParentlessElement) {
  GetDocument().documentElement()->setInnerHTML(R"HTML(
    <style>
      html {
        font-size: 20px;
        transition: font-size 100ms;
      }
      .adjust { font-size: 50%; }
    </style>
  )HTML");
  UpdateAllLifecyclePhasesForTest();

  Element* element = GetDocument().documentElement();
  element->setAttribute(html_names::kIdAttr, AtomicString("target"));
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ("20px", ComputedValue("font-size", *StyleForId("target")));
  ElementAnimations* element_animations = element->GetElementAnimations();
  EXPECT_FALSE(element_animations);

  // Trigger a transition with a dependency on the parent style.
  element->setAttribute(html_names::kClassAttr, AtomicString("adjust"));
  UpdateAllLifecyclePhasesForTest();
  element_animations = element->GetElementAnimations();
  EXPECT_TRUE(element_animations);
  Animation* transition = (*element_animations->Animations().begin()).key;
  EXPECT_TRUE(transition);
  EXPECT_EQ("20px", ComputedValue("font-size", *StyleForId("target")));

  // Bump the animation time to ensure a transition reversal.
  transition->setCurrentTime(MakeGarbageCollected<V8CSSNumberish>(50),
                             ASSERT_NO_EXCEPTION);
  transition->pause();
  UpdateAllLifecyclePhasesForTest();
  const String before_reversal_font_size =
      ComputedValue("font-size", *StyleForId("target"));

  // Verify there is no discontinuity in the font-size on transition reversal.
  element->setAttribute(html_names::kClassAttr, g_empty_atom);
  UpdateAllLifecyclePhasesForTest();
  element_animations = element->GetElementAnimations();
  EXPECT_TRUE(element_animations);
  Animation* reverse_transition =
      (*element_animations->Animations().begin()).key;
  EXPECT_TRUE(reverse_transition);
  EXPECT_EQ(before_reversal_font_size,
            ComputedValue("font-size", *StyleForId("target")));
}

class StyleResolverFontRelativeUnitTest
    : public testing::WithParamInterface<const char*>,
      public StyleResolverTest {};

TEST_P(StyleResolverFontRelativeUnitTest,
       BaseNotReusableIfFontRelativeUnitPresent) {
  GetDocument().documentElement()->setInnerHTML(
      String::Format("<div id=div style='width:1%s'>Test</div>", GetParam()));
  UpdateAllLifecyclePhasesForTest();

  Element* div = GetDocument().getElementById(AtomicString("div"));
  auto* effect = CreateSimpleKeyframeEffectForTest(
      div, CSSPropertyID::kFontSize, "50px", "100px");
  GetDocument().Timeline().Play(effect);
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ("50px", ComputedValue("font-size", *StyleForId("div")));

  div->SetNeedsAnimationStyleRecalc();
  GetDocument().Lifecycle().AdvanceTo(DocumentLifecycle::kInStyleRecalc);
  const auto* computed_style = StyleForId("div");

  EXPECT_TRUE(computed_style->HasFontRelativeUnits());
  EXPECT_TRUE(computed_style->GetBaseComputedStyle());

  StyleResolverState state(GetDocument(), *div);
  EXPECT_FALSE(StyleResolver::CanReuseBaseComputedStyle(state));
}

TEST_P(StyleResolverFontRelativeUnitTest,
       BaseReusableIfNoFontAffectingAnimation) {
  GetDocument().documentElement()->setInnerHTML(
      String::Format("<div id=div style='width:1%s'>Test</div>", GetParam()));
  UpdateAllLifecyclePhasesForTest();

  Element* div = GetDocument().getElementById(AtomicString("div"));
  auto* effect = CreateSimpleKeyframeEffectForTest(div, CSSPropertyID::kHeight,
                                                   "50px", "100px");
  GetDocument().Timeline().Play(effect);
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ("50px", ComputedValue("height", *StyleForId("div")));

  div->SetNeedsAnimationStyleRecalc();
  GetDocument().Lifecycle().AdvanceTo(DocumentLifecycle::kInStyleRecalc);
  const auto* computed_style = StyleForId("div");

  EXPECT_TRUE(computed_style->HasFontRelativeUnits());
  EXPECT_TRUE(computed_style->GetBaseComputedStyle());

  StyleResolverState state(GetDocument(), *div);
  EXPECT_TRUE(StyleResolver::CanReuseBaseComputedStyle(state));
}

INSTANTIATE_TEST_SUITE_P(All,
                         StyleResolverFontRelativeUnitTest,
                         testing::Values("em", "rem", "ex", "ch"));

namespace {

const CSSImageValue& GetBackgroundImageValue(const ComputedStyle& style) {
  const CSSValue* computed_value = ComputedStyleUtils::ComputedPropertyValue(
      GetCSSPropertyBackgroundImage(), style);

  const CSSValueList* bg_img_list = To<CSSValueList>(computed_value);

  return To<CSSImageValue>(bg_img_list->Item(0));
}

const CSSImageValue& GetBackgroundImageValue(const Element* element) {
  DCHECK(element);
  return GetBackgroundImageValue(element->ComputedStyleRef());
}

const CSSImageSetValue& GetBackgroundImageSetValue(const ComputedStyle& style) {
  const CSSValue* computed_value = ComputedStyleUtils::ComputedPropertyValue(
      GetCSSPropertyBackgroundImage(), style);

  const CSSValueList* bg_img_list = To<CSSValueList>(computed_value);

  return To<CSSImageSetValue>(bg_img_list->Item(0));
}

const CSSImageSetValue& GetBackgroundImageSetValue(const Element* element) {
  DCHECK(element);
  return GetBackgroundImageSetValue(element->ComputedStyleRef());
}

}  // namespace

TEST_P(ParameterizedStyleResolverTest, BackgroundImageFetch) {
  GetDocument().documentElement()->setInnerHTML(R"HTML(
    <style>
      #none {
        display: none;
        background-image: url(img-none.png);
      }
      #inside-none {
        background-image: url(img-inside-none.png);
      }
      #none-image-set {
        display: none;
        background-image: image-set(url(img-none.png) 1x);
      }
      #hidden {
        visibility: hidden;
        background-image: url(img-hidden.png);
      }
      #inside-hidden {
        background-image: url(img-inside-hidden.png);
      }
      #contents {
        display: contents;
        background-image: url(img-contents.png);
      }
      #inside-contents-parent {
        display: contents;
        background-image: url(img-inside-contents.png);
      }
      #inside-contents {
        background-image: inherit;
      }
      #non-slotted {
        background-image: url(img-non-slotted.png);
      }
      #no-pseudo::before {
        background-image: url(img-no-pseudo.png);
      }
      #first-line::first-line {
        background-image: url(first-line.png);
      }
      #first-line-span::first-line {
        background-image: url(first-line-span.png);
      }
      #first-line-none { display: none; }
      #first-line-none::first-line {
        background-image: url(first-line-none.png);
      }
      frameset {
        display: none;
        border-color: currentColor; /* UA inherit defeats caching */
        background-image: url(frameset-none.png);
      }
    </style>
    <div id="none">
      <div id="inside-none"></div>
    </div>
    <div id="none-image-set">
    </div>
    <div id="hidden">
      <div id="inside-hidden"></div>
    </div>
    <div id="contents"></div>
    <div id="inside-contents-parent">
      <div id="inside-contents"></div>
    </div>
    <div id="host">
      <div id="non-slotted"></div>
    </div>
    <div id="no-pseudo"></div>
    <div id="first-line">XXX</div>
    <span id="first-line-span">XXX</span>
    <div id="first-line-none">XXX</div>
  )HTML");

  auto* frameset1 = GetDocument().CreateRawElement(html_names::kFramesetTag);
  auto* frameset2 = GetDocument().CreateRawElement(html_names::kFramesetTag);
  GetDocument().documentElement()->AppendChild(frameset1);
  GetDocument().documentElement()->AppendChild(frameset2);

  GetDocument()
      .getElementById(AtomicString("host"))
      ->AttachShadowRootInternal(ShadowRootType::kOpen);
  UpdateAllLifecyclePhasesForTest();

  auto* none = GetDocument().getElementById(AtomicString("none"));
  auto* inside_none = GetDocument().getElementById(AtomicString("inside-none"));
  auto* none_image_set =
      GetDocument().getElementById(AtomicString("none-image-set"));
  auto* hidden = GetDocument().getElementById(AtomicString("hidden"));
  auto* inside_hidden =
      GetDocument().getElementById(AtomicString("inside-hidden"));
  auto* contents = GetDocument().getElementById(AtomicString("contents"));
  auto* inside_contents =
      GetDocument().getElementById(AtomicString("inside-contents"));
  auto* non_slotted = GetDocument().getElementById(AtomicString("non-slotted"));
  auto* no_pseudo = GetDocument().getElementById(AtomicString("no-pseudo"));
  auto* first_line = GetDocument().getElementById(AtomicString("first-line"));
  auto* first_line_span =
      GetDocument().getElementById(AtomicString("first-line-span"));
  auto* first_line_none =
      GetDocument().getElementById(AtomicString("first-line-none"));

  inside_none->EnsureComputedStyle();
  non_slotted->EnsureComputedStyle();
  none_image_set->EnsureComputedStyle();
  auto* before_style = no_pseudo->EnsureComputedStyle(kPseudoIdBefore);
  auto* first_line_style = first_line->EnsureComputedStyle(kPseudoIdFirstLine);
  auto* first_line_span_style =
      first_line_span->EnsureComputedStyle(kPseudoIdFirstLine);
  auto* first_line_none_style =
      first_line_none->EnsureComputedStyle(kPseudoIdFirstLine);

  ASSERT_TRUE(before_style);
  EXPECT_TRUE(GetBackgroundImageValue(*before_style).IsCachePending())
      << "No fetch for non-generated ::before";
  ASSERT_TRUE(first_line_style);
  EXPECT_FALSE(GetBackgroundImageValue(*first_line_style).IsCachePending())
      << "Fetched by layout of ::first-line";
  ASSERT_TRUE(first_line_span_style);
  EXPECT_TRUE(GetBackgroundImageValue(*first_line_span_style).IsCachePending())
      << "No fetch for inline with ::first-line";
  ASSERT_TRUE(first_line_none_style);
  EXPECT_TRUE(GetBackgroundImageValue(*first_line_none_style).IsCachePending())
      << "No fetch for display:none with ::first-line";
  EXPECT_TRUE(GetBackgroundImageValue(none).IsCachePending())
      << "No fetch for display:none";
  EXPECT_TRUE(GetBackgroundImageValue(inside_none).IsCachePending())
      << "No fetch inside display:none";
  EXPECT_TRUE(GetBackgroundImageSetValue(none_image_set).IsCachePending(1.0f))
      << "No fetch for display:none";
  EXPECT_FALSE(GetBackgroundImageValue(hidden).IsCachePending())
      << "Fetch for visibility:hidden";
  EXPECT_FALSE(GetBackgroundImageValue(inside_hidden).IsCachePending())
      << "Fetch for inherited visibility:hidden";
  EXPECT_FALSE(GetBackgroundImageValue(contents).IsCachePending())
      << "Fetch for display:contents";
  EXPECT_FALSE(GetBackgroundImageValue(inside_contents).IsCachePending())
      << "Fetch for image inherited from display:contents";
  EXPECT_TRUE(GetBackgroundImageValue(non_slotted).IsCachePending())
      << "No fetch for element outside the flat tree";

  // Added two frameset elements to hit the MatchedPropertiesCache for the
  // second one. Frameset adjusts style to display:block in StyleAdjuster, but
  // adjustments are not run before ComputedStyle is added to the
  // MatchedPropertiesCache leaving the cached style with StylePendingImage
  // unless we also check for LayoutObjectIsNeeded in
  // StyleResolverState::LoadPendingImages.
  EXPECT_FALSE(GetBackgroundImageValue(frameset1).IsCachePending())
      << "Fetch for display:none frameset";
  EXPECT_FALSE(GetBackgroundImageValue(frameset2).IsCachePending())
      << "Fetch for display:none frameset - cached";
}

TEST_P(ParameterizedStyleResolverTest, NoFetchForAtPage) {
  // Strictly, we should drop descriptors from @page rules which are not valid
  // descriptors, but as long as we apply them to ComputedStyle we should at
  // least not trigger fetches. The display:contents is here to make sure we
  // don't hit a DCHECK in StylePendingImage::ComputedCSSValue().
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      @page {
        display: contents;
        background-image: url(bg-img.png);
      }
    </style>
  )HTML");

  UpdateAllLifecyclePhasesForTest();
  const ComputedStyle* page_style =
      GetDocument().GetStyleResolver().StyleForPage(0, g_empty_atom);
  ASSERT_TRUE(page_style);
  const CSSValue* computed_value = ComputedStyleUtils::ComputedPropertyValue(
      GetCSSPropertyBackgroundImage(), *page_style);

  const CSSValueList* bg_img_list = To<CSSValueList>(computed_value);
  EXPECT_TRUE(To<CSSImageValue>(bg_img_list->Item(0)).IsCachePending());
}

TEST_P(ParameterizedStyleResolverTest, NoFetchForHighlightPseudoElements) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      body::target-text, body::selection {
        color: green;
        background-image: url(bg-img.png);
        cursor: url(cursor.ico), auto;
      }
    </style>
  )HTML");

  UpdateAllLifecyclePhasesForTest();

  auto* body = GetDocument().body();
  ASSERT_TRUE(body);
  const auto* element_style = body->GetComputedStyle();
  ASSERT_TRUE(element_style);

  StyleRequest pseudo_style_request;
  pseudo_style_request.parent_override = element_style;
  pseudo_style_request.layout_parent_override = element_style;
  pseudo_style_request.originating_element_style = element_style;

  StyleRequest target_text_style_request = pseudo_style_request;
  target_text_style_request.pseudo_id = kPseudoIdTargetText;

  const ComputedStyle* target_text_style =
      GetDocument().GetStyleResolver().ResolveStyle(GetDocument().body(),
                                                    StyleRecalcContext(),
                                                    target_text_style_request);
  ASSERT_TRUE(target_text_style);

  StyleRequest selection_style_style_request = pseudo_style_request;
  selection_style_style_request.pseudo_id = kPseudoIdSelection;

  const ComputedStyle* selection_style =
      GetDocument().GetStyleResolver().ResolveStyle(
          GetDocument().body(), StyleRecalcContext(),
          selection_style_style_request);
  ASSERT_TRUE(selection_style);

  // Check that the cursor does not apply to ::selection.
  ASSERT_FALSE(selection_style->Cursors());

  // Check that the cursor does not apply to ::target-text.
  ASSERT_FALSE(target_text_style->Cursors());

  // Check that we don't fetch the cursor url() for ::target-text.
  CursorList* cursor_list = target_text_style->Cursors();
  ASSERT_FALSE(cursor_list);

  for (const auto* pseudo_style : {target_text_style, selection_style}) {
    // Check that the color applies.
    EXPECT_EQ(Color(0, 128, 0),
              pseudo_style->VisitedDependentColor(GetCSSPropertyColor()));

    // Check that the background-image does not apply.
    const CSSValue* computed_value = ComputedStyleUtils::ComputedPropertyValue(
        GetCSSPropertyBackgroundImage(), *pseudo_style);
    const CSSValueList* list = DynamicTo<CSSValueList>(computed_value);
    ASSERT_TRUE(list);
    ASSERT_EQ(1u, list->length());
    const auto* keyword = DynamicTo<CSSIdentifierValue>(list->Item(0));
    ASSERT_TRUE(keyword);
    EXPECT_EQ(CSSValueID::kNone, keyword->GetValueID());
  }
}

TEST_P(ParameterizedStyleResolverTest, CSSMarkerPseudoElement) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      b::before {
        content: "[before]";
        display: list-item;
      }
      #marker ::marker {
        color: blue;
      }
    </style>
    <ul>
      <li style="list-style: decimal outside"><b></b></li>
      <li style="list-style: decimal inside"><b></b></li>
      <li style="list-style: disc outside"><b></b></li>
      <li style="list-style: disc inside"><b></b></li>
      <li style="list-style: '- ' outside"><b></b></li>
      <li style="list-style: '- ' inside"><b></b></li>
      <li style="list-style: linear-gradient(blue, cyan) outside"><b></b></li>
      <li style="list-style: linear-gradient(blue, cyan) inside"><b></b></li>
      <li style="list-style: none outside"><b></b></li>
      <li style="list-style: none inside"><b></b></li>
    </ul>
  )HTML");
  StaticElementList* lis = GetDocument().QuerySelectorAll(AtomicString("li"));
  EXPECT_EQ(lis->length(), 10U);

  UpdateAllLifecyclePhasesForTest();
  for (unsigned i = 0; i < lis->length(); ++i) {
    Element* li = lis->item(i);
    PseudoElement* marker = li->GetPseudoElement(kPseudoIdMarker);
    PseudoElement* before =
        li->QuerySelector(AtomicString("b"))->GetPseudoElement(kPseudoIdBefore);
    PseudoElement* nested_marker = before->GetPseudoElement(kPseudoIdMarker);

    // Check that UA styles for list markers don't set HasPseudoElementStyle
    const ComputedStyle* li_style = li->GetComputedStyle();
    EXPECT_FALSE(li_style->HasPseudoElementStyle(kPseudoIdMarker));
    EXPECT_FALSE(li_style->HasAnyPseudoElementStyles());
    const ComputedStyle* before_style = before->GetComputedStyle();
    EXPECT_FALSE(before_style->HasPseudoElementStyle(kPseudoIdMarker));
    EXPECT_FALSE(before_style->HasAnyPseudoElementStyles());

    if (i >= 8) {
      EXPECT_FALSE(marker);
      EXPECT_FALSE(nested_marker);
      continue;
    }

    // Check that list markers have UA styles
    EXPECT_TRUE(marker);
    EXPECT_TRUE(nested_marker);
    EXPECT_EQ(marker->GetComputedStyle()->GetUnicodeBidi(),
              UnicodeBidi::kIsolate);
    EXPECT_EQ(nested_marker->GetComputedStyle()->GetUnicodeBidi(),
              UnicodeBidi::kIsolate);
  }

  GetDocument().body()->SetIdAttribute(AtomicString("marker"));
  UpdateAllLifecyclePhasesForTest();
  for (unsigned i = 0; i < lis->length(); ++i) {
    Element* li = lis->item(i);
    PseudoElement* before =
        li->QuerySelector(AtomicString("b"))->GetPseudoElement(kPseudoIdBefore);

    // Check that author styles for list markers do set HasPseudoElementStyle
    const ComputedStyle* li_style = li->GetComputedStyle();
    EXPECT_TRUE(li_style->HasPseudoElementStyle(kPseudoIdMarker));
    EXPECT_TRUE(li_style->HasAnyPseudoElementStyles());

    // But ::marker styles don't match a ::before::marker
    const ComputedStyle* before_style = before->GetComputedStyle();
    EXPECT_FALSE(before_style->HasPseudoElementStyle(kPseudoIdMarker));
    EXPECT_FALSE(before_style->HasAnyPseudoElementStyles());
  }
}

TEST_P(ParameterizedStyleResolverTest, ApplyInheritedOnlyCustomPropertyChange) {
  // This test verifies that when we get a "apply inherited only"-type
  // hit in the MatchesPropertiesCache, we're able to detect that custom
  // properties changed, and that we therefore need to apply the non-inherited
  // properties as well.

  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      #parent1 { --a: 10px; }
      #parent2 { --a: 20px; }
      #child1, #child2 {
        --b: var(--a);
        width: var(--b);
      }
    </style>
    <div id=parent1><div id=child1></div></div>
    <div id=parent2><div id=child2></div></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();

  EXPECT_EQ("10px", ComputedValue("width", *StyleForId("child1")));
  EXPECT_EQ("20px", ComputedValue("width", *StyleForId("child2")));
}

TEST_P(ParameterizedStyleResolverTest, CssRulesForElementIncludedRules) {
  UpdateAllLifecyclePhasesForTest();

  Element* body = GetDocument().body();
  ASSERT_TRUE(body);

  // Don't crash when only getting one type of rule.
  auto& resolver = GetDocument().GetStyleResolver();
  resolver.CssRulesForElement(body, StyleResolver::kUACSSRules);
  resolver.CssRulesForElement(body, StyleResolver::kUserCSSRules);
  resolver.CssRulesForElement(body, StyleResolver::kAuthorCSSRules);
}

TEST_P(ParameterizedStyleResolverTest, NestedPseudoElement) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      div::before { content: "Hello"; display: list-item; }
      div::before::marker { color: green; }
    </style>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  // Don't crash when calculating style for nested pseudo elements.
}

TEST_P(ParameterizedStyleResolverTest, CascadedValuesForElement) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      #div {
        top: 1em;
      }
      div {
        top: 10em;
        right: 20em;
        bottom: 30em;
        left: 40em;

        width: 50em;
        width: 51em;
        height: 60em !important;
        height: 61em;
      }
    </style>
    <div id=div style="bottom:300em;"></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();

  auto& resolver = GetDocument().GetStyleResolver();
  Element* div = GetDocument().getElementById(AtomicString("div"));
  ASSERT_TRUE(div);

  auto map = resolver.CascadedValuesForElement(div, kPseudoIdNone);

  CSSPropertyName top(CSSPropertyID::kTop);
  CSSPropertyName right(CSSPropertyID::kRight);
  CSSPropertyName bottom(CSSPropertyID::kBottom);
  CSSPropertyName left(CSSPropertyID::kLeft);
  CSSPropertyName width(CSSPropertyID::kWidth);
  CSSPropertyName height(CSSPropertyID::kHeight);

  ASSERT_TRUE(map.at(top));
  ASSERT_TRUE(map.at(right));
  ASSERT_TRUE(map.at(bottom));
  ASSERT_TRUE(map.at(left));
  ASSERT_TRUE(map.at(width));
  ASSERT_TRUE(map.at(height));

  EXPECT_EQ("1em", map.at(top)->CssText());
  EXPECT_EQ("20em", map.at(right)->CssText());
  EXPECT_EQ("300em", map.at(bottom)->CssText());
  EXPECT_EQ("40em", map.at(left)->CssText());
  EXPECT_EQ("51em", map.at(width)->CssText());
  EXPECT_EQ("60em", map.at(height)->CssText());
}

TEST_P(ParameterizedStyleResolverTest, CascadedValuesForPseudoElement) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      #div::before {
        top: 1em;
      }
      div::before {
        top: 10em;
      }
    </style>
    <div id=div></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();

  auto& resolver = GetDocument().GetStyleResolver();
  Element* div = GetDocument().getElementById(AtomicString("div"));
  ASSERT_TRUE(div);

  auto map = resolver.CascadedValuesForElement(div, kPseudoIdBefore);

  CSSPropertyName top(CSSPropertyID::kTop);
  ASSERT_TRUE(map.at(top));
  EXPECT_EQ("1em", map.at(top)->CssText());
}

TEST_P(StyleResolverTestCQ, CascadedValuesForElementInContainer) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      #container { container-type: inline-size; }
      @container (min-width: 1px) {
        #inner {
          top: 1em;
        }
      }
      div {
        top: 10em;
      }
    </style>
    <div id="container">
      <div id="inner"></div>
    </div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();

  auto& resolver = GetDocument().GetStyleResolver();
  Element* inner = GetDocument().getElementById(AtomicString("inner"));
  ASSERT_TRUE(inner);

  auto map = resolver.CascadedValuesForElement(inner, kPseudoIdNone);

  CSSPropertyName top(CSSPropertyID::kTop);
  ASSERT_TRUE(map.at(top));
  EXPECT_EQ("1em", map.at(top)->CssText());
}

TEST_P(StyleResolverTestCQ, CascadedValuesForPseudoElementInContainer) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      #container { container-type: inline-size; }
      @container (min-width: 1px) {
        #inner::before {
          top: 1em;
        }
      }
      div::before {
        top: 10em;
      }
    </style>
    <div id="container">
      <div id="inner"></div>
    </div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();

  auto& resolver = GetDocument().GetStyleResolver();
  Element* inner = GetDocument().getElementById(AtomicString("inner"));
  ASSERT_TRUE(inner);

  auto map = resolver.CascadedValuesForElement(inner, kPseudoIdBefore);

  CSSPropertyName top(CSSPropertyID::kTop);
  ASSERT_TRUE(map.at(top));
  EXPECT_EQ("1em", map.at(top)->CssText());
}

TEST_P(ParameterizedStyleResolverTest, EnsureComputedStyleSlotFallback) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <div id="host"><span></span></div>
  )HTML");

  ShadowRoot& shadow_root =
      GetDocument()
          .getElementById(AtomicString("host"))
          ->AttachShadowRootInternal(ShadowRootType::kOpen);
  shadow_root.setInnerHTML(R"HTML(
    <style>
      slot { color: red }
    </style>
    <slot><span id="fallback"></span></slot>
  )HTML");
  Element* fallback = shadow_root.getElementById(AtomicString("fallback"));
  ASSERT_TRUE(fallback);

  UpdateAllLifecyclePhasesForTest();

  // Elements outside the flat tree does not get styles computed during the
  // lifecycle update.
  EXPECT_FALSE(fallback->GetComputedStyle());

  // We are currently allowed to query the computed style of elements outside
  // the flat tree, but slot fallback does not inherit from the slot.
  const ComputedStyle* fallback_style = fallback->EnsureComputedStyle();
  ASSERT_TRUE(fallback_style);
  EXPECT_EQ(Color::kBlack,
            fallback_style->VisitedDependentColor(GetCSSPropertyColor()));
}

TEST_P(ParameterizedStyleResolverTest, EnsureComputedStyleOutsideFlatTree) {
  GetDocument()
      .documentElement()
      ->setInnerHTMLWithDeclarativeShadowDOMForTesting(R"HTML(
    <div id=host>
      <template shadowrootmode=open>
      </template>
      <div id=a>
        <div id=b>
          <div id=c>
            <div id=d>
              <div id=e>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();

  Element* host = GetElementById("host");
  ASSERT_TRUE(host);
  ASSERT_TRUE(host->GetShadowRoot());

  Element* a = GetElementById("a");
  Element* b = GetElementById("b");
  Element* c = GetElementById("c");
  Element* d = GetElementById("d");
  Element* e = GetElementById("e");
  ASSERT_TRUE(a);
  ASSERT_TRUE(b);
  ASSERT_TRUE(c);
  ASSERT_TRUE(d);
  ASSERT_TRUE(e);

  EXPECT_FALSE(a->GetComputedStyle());
  EXPECT_FALSE(b->GetComputedStyle());
  EXPECT_FALSE(c->GetComputedStyle());
  EXPECT_FALSE(d->GetComputedStyle());
  EXPECT_FALSE(e->GetComputedStyle());

  c->EnsureComputedStyle();

  const ComputedStyle* a_style = a->GetComputedStyle();
  const ComputedStyle* b_style = b->GetComputedStyle();
  const ComputedStyle* c_style = c->GetComputedStyle();

  ASSERT_TRUE(a_style);
  ASSERT_TRUE(b_style);
  ASSERT_TRUE(c_style);
  EXPECT_FALSE(d->GetComputedStyle());
  EXPECT_FALSE(e->GetComputedStyle());

  // Dirty style of #a.
  a->SetInlineStyleProperty(CSSPropertyID::kZIndex, "42");

  // Note that there is no call to UpdateAllLifecyclePhasesForTest here,
  // because #a is outside the flat tree, hence that process would anyway not
  // reach #a.

  // Ensuring the style of some deep descendant must discover that some ancestor
  // is marked for recalc.
  e->EnsureComputedStyle();
  EXPECT_TRUE(a->GetComputedStyle());
  EXPECT_TRUE(b->GetComputedStyle());
  EXPECT_TRUE(c->GetComputedStyle());
  EXPECT_TRUE(d->GetComputedStyle());
  EXPECT_TRUE(e->GetComputedStyle());
  EXPECT_NE(a_style, a->GetComputedStyle());
  EXPECT_NE(b_style, b->GetComputedStyle());
  EXPECT_NE(c_style, c->GetComputedStyle());
}

TEST_P(ParameterizedStyleResolverTest, ComputeValueStandardProperty) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      #target { --color: green }
    </style>
    <div id="target"></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();

  Element* target = GetDocument().getElementById(AtomicString("target"));
  ASSERT_TRUE(target);

  // Unable to parse a variable reference with css_test_helpers::ParseLonghand.
  CSSPropertyID property_id = CSSPropertyID::kColor;
  auto* set =
      MakeGarbageCollected<MutableCSSPropertyValueSet>(kHTMLStandardMode);
  MutableCSSPropertyValueSet::SetResult result = set->ParseAndSetProperty(
      property_id, "var(--color)", false, SecureContextMode::kInsecureContext,
      /*context_style_sheet=*/nullptr);
  ASSERT_NE(MutableCSSPropertyValueSet::kParseError, result);
  const CSSValue* parsed_value = set->GetPropertyCSSValue(property_id);
  ASSERT_TRUE(parsed_value);
  const CSSValue* computed_value = StyleResolver::ComputeValue(
      target, CSSPropertyName(property_id), *parsed_value);
  ASSERT_TRUE(computed_value);
  EXPECT_EQ("rgb(0, 128, 0)", computed_value->CssText());
}

namespace {

const CSSValue* ParseCustomProperty(Document& document,
                                    const CustomProperty& property,
                                    const String& value) {
  const auto* context = MakeGarbageCollected<CSSParserContext>(document);
  CSSParserLocalContext local_context;
  auto tokens = CSSTokenizer(value).TokenizeToEOF();
  CSSParserTokenRange range(tokens);

  return property.Parse(CSSTokenizedValue{range, value}, *context,
                        local_context);
}

}  // namespace

TEST_P(ParameterizedStyleResolverTest, ComputeValueCustomProperty) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      #target { --color: green }
    </style>
    <div id="target"></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();

  Element* target = GetDocument().getElementById(AtomicString("target"));
  ASSERT_TRUE(target);

  AtomicString custom_property_name("--color");
  const CSSValue* parsed_value = ParseCustomProperty(
      GetDocument(), CustomProperty(custom_property_name, GetDocument()),
      "blue");
  ASSERT_TRUE(parsed_value);
  const CSSValue* computed_value = StyleResolver::ComputeValue(
      target, CSSPropertyName(custom_property_name), *parsed_value);
  ASSERT_TRUE(computed_value);
  EXPECT_EQ("blue", computed_value->CssText());
}

TEST_P(ParameterizedStyleResolverTest, TreeScopedReferences) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      #host { animation-name: anim }
    </style>
    <div id="host">
      <span id="slotted"></span>
    </host>
  )HTML");

  Element* host = GetDocument().getElementById(AtomicString("host"));
  ASSERT_TRUE(host);
  ShadowRoot& root = host->AttachShadowRootInternal(ShadowRootType::kOpen);
  root.setInnerHTML(R"HTML(
    <style>
      ::slotted(span) { animation-name: anim-slotted }
      :host { font-family: myfont }
    </style>
    <div id="inner-host">
      <slot></slot>
    </div>
  )HTML");

  Element* inner_host = root.getElementById(AtomicString("inner-host"));
  ASSERT_TRUE(inner_host);
  ShadowRoot& inner_root =
      inner_host->AttachShadowRootInternal(ShadowRootType::kOpen);
  inner_root.setInnerHTML(R"HTML(
    <style>
      ::slotted(span) { animation-name: anim-inner-slotted }
    </style>
    <slot></slot>
  )HTML");

  UpdateAllLifecyclePhasesForTest();

  {
    StyleResolverState state(GetDocument(), *host);
    SelectorFilter filter;
    MatchResult match_result;
    ElementRuleCollector collector(state.ElementContext(), StyleRecalcContext(),
                                   filter, match_result,
                                   EInsideLink::kNotInsideLink);
    GetDocument().GetStyleEngine().GetStyleResolver().MatchAllRules(
        state, collector, false /* include_smil_properties */);
    const auto& properties = match_result.GetMatchedProperties();
    ASSERT_EQ(properties.size(), 3u);

    // div { display: block }
    EXPECT_EQ(properties[0].types_.origin, CascadeOrigin::kUserAgent);

    // :host { font-family: myfont }
    EXPECT_EQ(match_result.ScopeFromTreeOrder(properties[1].types_.tree_order),
              root.GetTreeScope());
    EXPECT_EQ(properties[1].types_.origin, CascadeOrigin::kAuthor);

    // #host { animation-name: anim }
    EXPECT_EQ(properties[2].types_.origin, CascadeOrigin::kAuthor);
    EXPECT_EQ(match_result.ScopeFromTreeOrder(properties[2].types_.tree_order),
              host->GetTreeScope());
  }

  {
    auto* span = GetDocument().getElementById(AtomicString("slotted"));
    StyleResolverState state(GetDocument(), *span);
    SelectorFilter filter;
    MatchResult match_result;
    ElementRuleCollector collector(state.ElementContext(), StyleRecalcContext(),
                                   filter, match_result,
                                   EInsideLink::kNotInsideLink);
    GetDocument().GetStyleEngine().GetStyleResolver().MatchAllRules(
        state, collector, false /* include_smil_properties */);
    const auto& properties = match_result.GetMatchedProperties();
    ASSERT_EQ(properties.size(), 2u);

    // ::slotted(span) { animation-name: anim-inner-slotted }
    EXPECT_EQ(properties[0].types_.origin, CascadeOrigin::kAuthor);
    EXPECT_EQ(match_result.ScopeFromTreeOrder(properties[0].types_.tree_order),
              inner_root.GetTreeScope());

    // ::slotted(span) { animation-name: anim-slotted }
    EXPECT_EQ(properties[1].types_.origin, CascadeOrigin::kAuthor);
    EXPECT_EQ(match_result.ScopeFromTreeOrder(properties[1].types_.tree_order),
              root.GetTreeScope());
  }
}

TEST_P(ParameterizedStyleResolverTest, InheritStyleImagesFromDisplayContents) {
  GetDocument().documentElement()->setInnerHTML(R"HTML(
    <style>
      #parent {
        display: contents;

        background-image: url(1.png);
        border-image-source: url(2.png);
        cursor: url(3.ico), text;
        list-style-image: url(4.png);
        shape-outside: url(5.png);
        -webkit-box-reflect: below 0 url(6.png);
        -webkit-mask-box-image-source: url(7.png);
        -webkit-mask-image: url(8.png);
      }
      #child {
        background-image: inherit;
        border-image-source: inherit;
        cursor: inherit;
        list-style-image: inherit;
        shape-outside: inherit;
        -webkit-box-reflect: inherit;
        -webkit-mask-box-image-source: inherit;
        -webkit-mask-image: inherit;
      }
    </style>
    <div id="parent">
      <div id="child"></div>
    </div>
  )HTML");

  UpdateAllLifecyclePhasesForTest();

  auto* child = GetDocument().getElementById(AtomicString("child"));
  auto* style = child->GetComputedStyle();
  ASSERT_TRUE(style);

  ASSERT_TRUE(style->BackgroundLayers().GetImage());
  EXPECT_FALSE(style->BackgroundLayers().GetImage()->IsPendingImage())
      << "background-image is fetched";

  ASSERT_TRUE(style->BorderImageSource());
  EXPECT_FALSE(style->BorderImageSource()->IsPendingImage())
      << "border-image-source is fetched";

  ASSERT_TRUE(style->Cursors());
  ASSERT_TRUE(style->Cursors()->size());
  ASSERT_TRUE(style->Cursors()->at(0).GetImage());
  EXPECT_FALSE(style->Cursors()->at(0).GetImage()->IsPendingImage())
      << "cursor is fetched";

  ASSERT_TRUE(style->ListStyleImage());
  EXPECT_FALSE(style->ListStyleImage()->IsPendingImage())
      << "list-style-image is fetched";

  ASSERT_TRUE(style->ShapeOutside());
  ASSERT_TRUE(style->ShapeOutside()->GetImage());
  EXPECT_FALSE(style->ShapeOutside()->GetImage()->IsPendingImage())
      << "shape-outside is fetched";

  ASSERT_TRUE(style->BoxReflect());
  ASSERT_TRUE(style->BoxReflect()->Mask().GetImage());
  EXPECT_FALSE(style->BoxReflect()->Mask().GetImage()->IsPendingImage())
      << "-webkit-box-reflect is fetched";

  ASSERT_TRUE(style->MaskBoxImageSource());
  EXPECT_FALSE(style->MaskBoxImageSource()->IsPendingImage())
      << "-webkit-mask-box-image-source";

  ASSERT_TRUE(style->MaskLayers().GetImage());
  EXPECT_FALSE(style->MaskLayers().GetImage()->IsPendingImage())
      << "-webkit-mask-image is fetched";
}

TEST_P(ParameterizedStyleResolverTest, TextShadowInHighlightPseudoNotCounted1) {
  EXPECT_FALSE(
      GetDocument().IsUseCounted(WebFeature::kTextShadowInHighlightPseudo));
  EXPECT_FALSE(GetDocument().IsUseCounted(
      WebFeature::kTextShadowNotNoneInHighlightPseudo));

  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      * {
        text-shadow: 5px 5px green;
      }
    </style>
    <div id="target">target</div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();

  Element* target = GetDocument().getElementById(AtomicString("target"));
  ASSERT_TRUE(target);
  const auto* element_style = target->GetComputedStyle();
  ASSERT_TRUE(element_style);

  StyleRequest pseudo_style_request;
  pseudo_style_request.parent_override = element_style;
  pseudo_style_request.layout_parent_override = element_style;
  pseudo_style_request.originating_element_style = element_style;
  pseudo_style_request.pseudo_id = kPseudoIdSelection;
  const ComputedStyle* selection_style =
      GetDocument().GetStyleResolver().ResolveStyle(
          target, StyleRecalcContext(), pseudo_style_request);
  ASSERT_FALSE(selection_style);

  EXPECT_FALSE(
      GetDocument().IsUseCounted(WebFeature::kTextShadowInHighlightPseudo));
  EXPECT_FALSE(GetDocument().IsUseCounted(
      WebFeature::kTextShadowNotNoneInHighlightPseudo));
}

TEST_P(ParameterizedStyleResolverTest, TextShadowInHighlightPseudoNotCounted2) {
  EXPECT_FALSE(
      GetDocument().IsUseCounted(WebFeature::kTextShadowInHighlightPseudo));
  EXPECT_FALSE(GetDocument().IsUseCounted(
      WebFeature::kTextShadowNotNoneInHighlightPseudo));

  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      * {
        text-shadow: 5px 5px green;
      }
      ::selection {
        color: white;
        background: blue;
      }
    </style>
    <div id="target">target</div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();

  Element* target = GetDocument().getElementById(AtomicString("target"));
  ASSERT_TRUE(target);
  const auto* element_style = target->GetComputedStyle();
  ASSERT_TRUE(element_style);

  StyleRequest pseudo_style_request;
  pseudo_style_request.parent_override = element_style;
  pseudo_style_request.layout_parent_override = element_style;
  pseudo_style_request.originating_element_style = element_style;
  pseudo_style_request.pseudo_id = kPseudoIdSelection;
  const ComputedStyle* selection_style =
      GetDocument().GetStyleResolver().ResolveStyle(
          target, StyleRecalcContext(), pseudo_style_request);
  ASSERT_TRUE(selection_style);

  EXPECT_FALSE(
      GetDocument().IsUseCounted(WebFeature::kTextShadowInHighlightPseudo));
  EXPECT_FALSE(GetDocument().IsUseCounted(
      WebFeature::kTextShadowNotNoneInHighlightPseudo));
}

TEST_P(ParameterizedStyleResolverTest, TextShadowInHighlightPseudotNone) {
  EXPECT_FALSE(
      GetDocument().IsUseCounted(WebFeature::kTextShadowInHighlightPseudo));
  EXPECT_FALSE(GetDocument().IsUseCounted(
      WebFeature::kTextShadowNotNoneInHighlightPseudo));

  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      * {
        text-shadow: 5px 5px green;
      }
      ::selection {
        text-shadow: none;
      }
    </style>
    <div id="target">target</div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();

  Element* target = GetDocument().getElementById(AtomicString("target"));
  ASSERT_TRUE(target);
  const auto* element_style = target->GetComputedStyle();
  ASSERT_TRUE(element_style);

  StyleRequest pseudo_style_request;
  pseudo_style_request.parent_override = element_style;
  pseudo_style_request.layout_parent_override = element_style;
  pseudo_style_request.originating_element_style = element_style;
  pseudo_style_request.pseudo_id = kPseudoIdSelection;
  const ComputedStyle* selection_style =
      GetDocument().GetStyleResolver().ResolveStyle(
          target, StyleRecalcContext(), pseudo_style_request);
  ASSERT_TRUE(selection_style);

  EXPECT_TRUE(
      GetDocument().IsUseCounted(WebFeature::kTextShadowInHighlightPseudo));
  EXPECT_FALSE(GetDocument().IsUseCounted(
      WebFeature::kTextShadowNotNoneInHighlightPseudo));
}

TEST_P(ParameterizedStyleResolverTest, TextShadowInHighlightPseudoNotNone1) {
  EXPECT_FALSE(
      GetDocument().IsUseCounted(WebFeature::kTextShadowInHighlightPseudo));
  EXPECT_FALSE(GetDocument().IsUseCounted(
      WebFeature::kTextShadowNotNoneInHighlightPseudo));

  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      ::selection {
        text-shadow: 5px 5px green;
      }
    </style>
    <div id="target">target</div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();

  Element* target = GetDocument().getElementById(AtomicString("target"));
  ASSERT_TRUE(target);
  const auto* element_style = target->GetComputedStyle();
  ASSERT_TRUE(element_style);

  StyleRequest pseudo_style_request;
  pseudo_style_request.parent_override = element_style;
  pseudo_style_request.layout_parent_override = element_style;
  pseudo_style_request.originating_element_style = element_style;
  pseudo_style_request.pseudo_id = kPseudoIdSelection;
  const ComputedStyle* selection_style =
      GetDocument().GetStyleResolver().ResolveStyle(
          target, StyleRecalcContext(), pseudo_style_request);
  ASSERT_TRUE(selection_style);

  EXPECT_TRUE(
      GetDocument().IsUseCounted(WebFeature::kTextShadowInHighlightPseudo));
  EXPECT_TRUE(GetDocument().IsUseCounted(
      WebFeature::kTextShadowNotNoneInHighlightPseudo));
}

TEST_P(ParameterizedStyleResolverTest, TextShadowInHighlightPseudoNotNone2) {
  EXPECT_FALSE(
      GetDocument().IsUseCounted(WebFeature::kTextShadowInHighlightPseudo));
  EXPECT_FALSE(GetDocument().IsUseCounted(
      WebFeature::kTextShadowNotNoneInHighlightPseudo));

  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      * {
        text-shadow: 5px 5px green;
      }
      ::selection {
        text-shadow: 5px 5px green;
      }
    </style>
    <div id="target">target</div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();

  Element* target = GetDocument().getElementById(AtomicString("target"));
  ASSERT_TRUE(target);
  const auto* element_style = target->GetComputedStyle();
  ASSERT_TRUE(element_style);

  StyleRequest pseudo_style_request;
  pseudo_style_request.parent_override = element_style;
  pseudo_style_request.layout_parent_override = element_style;
  pseudo_style_request.originating_element_style = element_style;
  pseudo_style_request.pseudo_id = kPseudoIdSelection;
  const ComputedStyle* selection_style =
      GetDocument().GetStyleResolver().ResolveStyle(
          target, StyleRecalcContext(), pseudo_style_request);
  ASSERT_TRUE(selection_style);

  EXPECT_TRUE(
      GetDocument().IsUseCounted(WebFeature::kTextShadowInHighlightPseudo));
  EXPECT_TRUE(GetDocument().IsUseCounted(
      WebFeature::kTextShadowNotNoneInHighlightPseudo));
}

TEST_P(StyleResolverTestCQ, DependsOnSizeContainerQueries) {
  GetDocument().documentElement()->setInnerHTML(R"HTML(
    <style>
      #a { color: red; }
      @container (min-width: 0px) {
        #b { color: blue; }
        span { color: green; }
        #d { color: coral; }
      }
    </style>
    <div id=a></div>
    <span id=b></span>
    <span id=c></span>
    <div id=d></div>
    <div id=e></div>
  )HTML");

  UpdateAllLifecyclePhasesForTest();

  auto* a = GetDocument().getElementById(AtomicString("a"));
  auto* b = GetDocument().getElementById(AtomicString("b"));
  auto* c = GetDocument().getElementById(AtomicString("c"));
  auto* d = GetDocument().getElementById(AtomicString("d"));
  auto* e = GetDocument().getElementById(AtomicString("e"));

  ASSERT_TRUE(a);
  ASSERT_TRUE(b);
  ASSERT_TRUE(c);
  ASSERT_TRUE(d);
  ASSERT_TRUE(e);

  EXPECT_FALSE(a->ComputedStyleRef().DependsOnSizeContainerQueries());
  EXPECT_TRUE(b->ComputedStyleRef().DependsOnSizeContainerQueries());
  EXPECT_TRUE(c->ComputedStyleRef().DependsOnSizeContainerQueries());
  EXPECT_TRUE(d->ComputedStyleRef().DependsOnSizeContainerQueries());
  EXPECT_FALSE(e->ComputedStyleRef().DependsOnSizeContainerQueries());

  EXPECT_FALSE(a->ComputedStyleRef().DependsOnStyleContainerQueries());
  EXPECT_FALSE(b->ComputedStyleRef().DependsOnStyleContainerQueries());
  EXPECT_FALSE(c->ComputedStyleRef().DependsOnStyleContainerQueries());
  EXPECT_FALSE(d->ComputedStyleRef().DependsOnStyleContainerQueries());
  EXPECT_FALSE(e->ComputedStyleRef().DependsOnStyleContainerQueries());
}

TEST_P(StyleResolverTestCQ, DependsOnSizeContainerQueriesPseudo) {
  GetDocument().documentElement()->setInnerHTML(R"HTML(
    <style>
      main { container-type: size; width: 100px; }
      #a::before { content: "before"; }
      @container (min-width: 0px) {
        #a::after { content: "after"; }
      }
    </style>
    <main>
      <div id=a></div>
    </main>
  )HTML");

  UpdateAllLifecyclePhasesForTest();

  auto* a = GetDocument().getElementById(AtomicString("a"));
  auto* before = a->GetPseudoElement(kPseudoIdBefore);
  auto* after = a->GetPseudoElement(kPseudoIdAfter);

  ASSERT_TRUE(a);
  ASSERT_TRUE(before);
  ASSERT_TRUE(after);

  EXPECT_TRUE(a->ComputedStyleRef().DependsOnSizeContainerQueries());
  EXPECT_FALSE(before->ComputedStyleRef().DependsOnSizeContainerQueries());
  EXPECT_TRUE(after->ComputedStyleRef().DependsOnSizeContainerQueries());
}

// Verify that the ComputedStyle::DependsOnSizeContainerQuery flag does
// not end up in the MatchedPropertiesCache (MPC).
TEST_P(StyleResolverTestCQ, DependsOnSizeContainerQueriesMPC) {
  GetDocument().documentElement()->setInnerHTML(R"HTML(
    <style>
      @container (min-width: 9999999px) {
        #a { color: green; }
      }
    </style>
    <div id=a></div>
    <div id=b></div>
  )HTML");

  // In the above example, both <div id=a> and <div id=b> match the same
  // rules (i.e. whatever is provided by UA style). The selector inside
  // the @container rule does ultimately _not_ match <div id=a> (because the
  // container query evaluates to 'false'), however, it _does_ cause the
  // ComputedStyle::DependsOnSizeContainerQuery flag to be set on #a.
  //
  // We must ensure that we don't add the DependsOnSizeContainerQuery-flagged
  // style to the MPC, otherwise the subsequent cache hit for #b would result
  // in the flag being (incorrectly) set for that element.

  UpdateAllLifecyclePhasesForTest();

  auto* a = GetDocument().getElementById(AtomicString("a"));
  auto* b = GetDocument().getElementById(AtomicString("b"));

  ASSERT_TRUE(a);
  ASSERT_TRUE(b);

  EXPECT_TRUE(a->ComputedStyleRef().DependsOnSizeContainerQueries());
  EXPECT_FALSE(b->ComputedStyleRef().DependsOnSizeContainerQueries());
}

TEST_P(StyleResolverTestCQ, DependsOnStyleContainerQueries) {
  GetDocument().documentElement()->setInnerHTML(R"HTML(
    <style>
      #a { color: red; }
      @container style(--foo: bar) {
        #b { color: blue; }
        span { color: green; }
        #d { color: coral; }
      }
    </style>
    <div id=a></div>
    <span id=b></span>
    <span id=c></span>
    <div id=d></div>
    <div id=e></div>
  )HTML");

  UpdateAllLifecyclePhasesForTest();

  auto* a = GetDocument().getElementById(AtomicString("a"));
  auto* b = GetDocument().getElementById(AtomicString("b"));
  auto* c = GetDocument().getElementById(AtomicString("c"));
  auto* d = GetDocument().getElementById(AtomicString("d"));
  auto* e = GetDocument().getElementById(AtomicString("e"));

  ASSERT_TRUE(a);
  ASSERT_TRUE(b);
  ASSERT_TRUE(c);
  ASSERT_TRUE(d);
  ASSERT_TRUE(e);

  EXPECT_FALSE(a->ComputedStyleRef().DependsOnStyleContainerQueries());
  EXPECT_TRUE(b->ComputedStyleRef().DependsOnStyleContainerQueries());
  EXPECT_TRUE(c->ComputedStyleRef().DependsOnStyleContainerQueries());
  EXPECT_TRUE(d->ComputedStyleRef().DependsOnStyleContainerQueries());
  EXPECT_FALSE(e->ComputedStyleRef().DependsOnStyleContainerQueries());

  EXPECT_FALSE(a->ComputedStyleRef().DependsOnSizeContainerQueries());
  EXPECT_FALSE(b->ComputedStyleRef().DependsOnSizeContainerQueries());
  EXPECT_FALSE(c->ComputedStyleRef().DependsOnSizeContainerQueries());
  EXPECT_FALSE(d->ComputedStyleRef().DependsOnSizeContainerQueries());
  EXPECT_FALSE(e->ComputedStyleRef().DependsOnSizeContainerQueries());
}

TEST_P(ParameterizedStyleResolverTest, NoCascadeLayers) {
  GetDocument().documentElement()->setInnerHTML(R"HTML(
    <style>
      #a { color: green; }
      .b { font-size: 16px; }
    </style>
    <div id=a class=b></div>
  )HTML");

  UpdateAllLifecyclePhasesForTest();

  StyleResolverState state(GetDocument(),
                           *GetDocument().getElementById(AtomicString("a")));
  SelectorFilter filter;
  MatchResult match_result;
  ElementRuleCollector collector(state.ElementContext(), StyleRecalcContext(),
                                 filter, match_result,
                                 EInsideLink::kNotInsideLink);
  MatchAllRules(state, collector);
  const auto& properties = match_result.GetMatchedProperties();
  ASSERT_EQ(properties.size(), 3u);

  const uint16_t kImplicitOuterLayerOrder =
      ClampTo<uint16_t>(CascadeLayerMap::kImplicitOuterLayerOrder);

  // div { display: block; }
  EXPECT_TRUE(properties[0].properties->HasProperty(CSSPropertyID::kDisplay));
  EXPECT_EQ(kImplicitOuterLayerOrder, properties[0].types_.layer_order);
  EXPECT_EQ(properties[0].types_.origin, CascadeOrigin::kUserAgent);

  // .b { font-size: 16px; }
  EXPECT_TRUE(properties[1].properties->HasProperty(CSSPropertyID::kFontSize));
  EXPECT_EQ(kImplicitOuterLayerOrder, properties[1].types_.layer_order);
  EXPECT_EQ(properties[1].types_.origin, CascadeOrigin::kAuthor);

  // #a { color: green; }
  EXPECT_TRUE(properties[2].properties->HasProperty(CSSPropertyID::kColor));
  EXPECT_EQ(kImplicitOuterLayerOrder, properties[2].types_.layer_order);
  EXPECT_EQ(properties[2].types_.origin, CascadeOrigin::kAuthor);
}

TEST_P(ParameterizedStyleResolverTest, CascadeLayersInDifferentSheets) {
  GetDocument().documentElement()->setInnerHTML(R"HTML(
    <style>
      @layer foo, bar;
      @layer bar {
        .b { color: green; }
      }
    </style>
    <style>
      @layer foo {
        #a { font-size: 16px; }
      }
    </style>
    <div id=a class=b style="font-family: custom"></div>
  )HTML");

  UpdateAllLifecyclePhasesForTest();

  StyleResolverState state(GetDocument(),
                           *GetDocument().getElementById(AtomicString("a")));
  SelectorFilter filter;
  MatchResult match_result;
  ElementRuleCollector collector(state.ElementContext(), StyleRecalcContext(),
                                 filter, match_result,
                                 EInsideLink::kNotInsideLink);
  MatchAllRules(state, collector);
  const auto& properties = match_result.GetMatchedProperties();
  ASSERT_EQ(properties.size(), 4u);

  const uint16_t kImplicitOuterLayerOrder =
      ClampTo<uint16_t>(CascadeLayerMap::kImplicitOuterLayerOrder);

  // div { display: block; }
  EXPECT_TRUE(properties[0].properties->HasProperty(CSSPropertyID::kDisplay));
  EXPECT_EQ(kImplicitOuterLayerOrder, properties[0].types_.layer_order);
  EXPECT_EQ(properties[0].types_.origin, CascadeOrigin::kUserAgent);

  // @layer foo { #a { font-size: 16px } }"
  EXPECT_TRUE(properties[1].properties->HasProperty(CSSPropertyID::kFontSize));
  EXPECT_EQ(0u, properties[1].types_.layer_order);
  EXPECT_EQ(properties[1].types_.origin, CascadeOrigin::kAuthor);

  // @layer bar { .b { color: green } }"
  EXPECT_TRUE(properties[2].properties->HasProperty(CSSPropertyID::kColor));
  EXPECT_EQ(1u, properties[2].types_.layer_order);
  EXPECT_EQ(properties[2].types_.origin, CascadeOrigin::kAuthor);

  // style="font-family: custom"
  EXPECT_TRUE(
      properties[3].properties->HasProperty(CSSPropertyID::kFontFamily));
  EXPECT_TRUE(properties[3].types_.is_inline_style);
  EXPECT_EQ(properties[3].types_.origin, CascadeOrigin::kAuthor);
  // There's no layer order for inline style; it's always above all layers.
}

TEST_P(ParameterizedStyleResolverTest, CascadeLayersInDifferentTreeScopes) {
  GetDocument()
      .documentElement()
      ->setInnerHTMLWithDeclarativeShadowDOMForTesting(R"HTML(
    <style>
      @layer foo {
        #host { color: green; }
      }
    </style>
    <div id=host>
      <template shadowrootmode=open>
        <style>
          @layer bar {
            :host { font-size: 16px; }
          }
        </style>
      </template>
    </div>
  )HTML");

  UpdateAllLifecyclePhasesForTest();

  StyleResolverState state(GetDocument(),
                           *GetDocument().getElementById(AtomicString("host")));
  SelectorFilter filter;
  MatchResult match_result;
  ElementRuleCollector collector(state.ElementContext(), StyleRecalcContext(),
                                 filter, match_result,
                                 EInsideLink::kNotInsideLink);
  MatchAllRules(state, collector);
  const auto& properties = match_result.GetMatchedProperties();
  ASSERT_EQ(properties.size(), 3u);

  const uint16_t kImplicitOuterLayerOrder =
      ClampTo<uint16_t>(CascadeLayerMap::kImplicitOuterLayerOrder);

  // div { display: block }
  EXPECT_TRUE(properties[0].properties->HasProperty(CSSPropertyID::kDisplay));
  EXPECT_EQ(kImplicitOuterLayerOrder, properties[0].types_.layer_order);
  EXPECT_EQ(properties[0].types_.origin, CascadeOrigin::kUserAgent);

  // @layer bar { :host { font-size: 16px } }
  EXPECT_TRUE(properties[1].properties->HasProperty(CSSPropertyID::kFontSize));
  EXPECT_EQ(0u, properties[1].types_.layer_order);
  EXPECT_EQ(properties[1].types_.origin, CascadeOrigin::kAuthor);
  EXPECT_EQ(
      match_result.ScopeFromTreeOrder(properties[1].types_.tree_order),
      GetDocument().getElementById(AtomicString("host"))->GetShadowRoot());

  // @layer foo { #host { color: green } }
  EXPECT_TRUE(properties[2].properties->HasProperty(CSSPropertyID::kColor));
  EXPECT_EQ(0u, properties[2].types_.layer_order);
  EXPECT_EQ(match_result.ScopeFromTreeOrder(properties[2].types_.tree_order),
            &GetDocument());
}

// https://crbug.com/1313357
TEST_P(ParameterizedStyleResolverTest,
       CascadeLayersAfterModifyingAnotherSheet) {
  GetDocument().documentElement()->setInnerHTML(R"HTML(
    <style>
      @layer {
        target { color: red; }
      }
    </style>
    <style id="addrule"></style>
    <target></target>
  )HTML");

  UpdateAllLifecyclePhasesForTest();

  GetDocument()
      .getElementById(AtomicString("addrule"))
      ->appendChild(
          GetDocument().createTextNode("target { font-size: 10px; }"));

  UpdateAllLifecyclePhasesForTest();

  ASSERT_TRUE(GetDocument().GetScopedStyleResolver()->GetCascadeLayerMap());

  StyleResolverState state(
      GetDocument(), *GetDocument().QuerySelector(AtomicString("target")));
  SelectorFilter filter;
  MatchResult match_result;
  ElementRuleCollector collector(state.ElementContext(), StyleRecalcContext(),
                                 filter, match_result,
                                 EInsideLink::kNotInsideLink);
  MatchAllRules(state, collector);
  const auto& properties = match_result.GetMatchedProperties();
  ASSERT_EQ(properties.size(), 2u);

  const uint16_t kImplicitOuterLayerOrder =
      ClampTo<uint16_t>(CascadeLayerMap::kImplicitOuterLayerOrder);

  // @layer { target { color: red } }"
  EXPECT_TRUE(properties[0].properties->HasProperty(CSSPropertyID::kColor));
  EXPECT_EQ(0u, properties[0].types_.layer_order);
  EXPECT_EQ(properties[0].types_.origin, CascadeOrigin::kAuthor);

  // target { font-size: 10px }
  EXPECT_TRUE(properties[1].properties->HasProperty(CSSPropertyID::kFontSize));
  EXPECT_EQ(kImplicitOuterLayerOrder, properties[1].types_.layer_order);
  EXPECT_EQ(properties[1].types_.origin, CascadeOrigin::kAuthor);
}

// https://crbug.com/1326791
TEST_P(ParameterizedStyleResolverTest,
       CascadeLayersAddLayersWithImportantDeclarations) {
  GetDocument().documentElement()->setInnerHTML(R"HTML(
    <style id="addrule"></style>
    <target></target>
  )HTML");

  UpdateAllLifecyclePhasesForTest();

  GetDocument()
      .getElementById(AtomicString("addrule"))
      ->appendChild(GetDocument().createTextNode(
          "@layer { target { font-size: 20px !important; } }"
          "@layer { target { font-size: 10px !important; } }"));

  UpdateAllLifecyclePhasesForTest();

  ASSERT_TRUE(GetDocument().GetScopedStyleResolver()->GetCascadeLayerMap());

  StyleResolverState state(
      GetDocument(), *GetDocument().QuerySelector(AtomicString("target")));
  SelectorFilter filter;
  MatchResult match_result;
  ElementRuleCollector collector(state.ElementContext(), StyleRecalcContext(),
                                 filter, match_result,
                                 EInsideLink::kNotInsideLink);
  MatchAllRules(state, collector);
  const auto& properties = match_result.GetMatchedProperties();
  ASSERT_EQ(properties.size(), 2u);

  // @layer { target { font-size: 20px !important } }
  EXPECT_TRUE(properties[0].properties->HasProperty(CSSPropertyID::kFontSize));
  EXPECT_TRUE(
      properties[0].properties->PropertyIsImportant(CSSPropertyID::kFontSize));
  EXPECT_EQ("20px", properties[0].properties->GetPropertyValue(
                        CSSPropertyID::kFontSize));
  EXPECT_EQ(0u, properties[0].types_.layer_order);
  EXPECT_EQ(properties[0].types_.origin, CascadeOrigin::kAuthor);

  // @layer { target { font-size: 10px !important } }
  EXPECT_TRUE(properties[1].properties->HasProperty(CSSPropertyID::kFontSize));
  EXPECT_TRUE(
      properties[1].properties->PropertyIsImportant(CSSPropertyID::kFontSize));
  EXPECT_EQ("10px", properties[1].properties->GetPropertyValue(
                        CSSPropertyID::kFontSize));
  EXPECT_EQ(1u, properties[1].types_.layer_order);
  EXPECT_EQ(properties[1].types_.origin, CascadeOrigin::kAuthor);
}

// TODO(crbug.com/1095765): We should have a WPT for this test case, and the
// Blink web test runner can now test @page rules in WPT.
TEST_P(ParameterizedStyleResolverTest, CascadeLayersAndPageRules) {
  GetDocument().documentElement()->setInnerHTML(R"HTML(
    <style>
    @page { margin-top: 100px; }
    @layer {
      @page { margin-top: 50px; }
    }
    </style>
  )HTML");

  GetDocument().GetFrame()->StartPrinting();
  GetDocument().View()->UpdateLifecyclePhasesForPrinting();

  WebPrintPageDescription description(gfx::SizeF(800, 600));
  GetDocument().GetPageDescription(0, &description);

  // The layered declaraion should win the cascading.
  EXPECT_EQ(100, description.margin_top);
}

TEST_P(ParameterizedStyleResolverTest, BodyPropagationLayoutImageContain) {
  GetDocument().documentElement()->setAttribute(
      html_names::kStyleAttr,
      AtomicString("contain:size; display:inline-table; content:url(img);"));
  GetDocument().body()->SetInlineStyleProperty(CSSPropertyID::kBackgroundColor,
                                               "red");

  // Should not trigger DCHECK
  UpdateAllLifecyclePhasesForTest();

  EXPECT_EQ(Color::kTransparent,
            GetDocument().GetLayoutView()->StyleRef().VisitedDependentColor(
                GetCSSPropertyBackgroundColor()));
}

TEST_P(ParameterizedStyleResolverTest, IsInertWithAttributeAndDialog) {
  Document& document = GetDocument();
  NonThrowableExceptionState exception_state;

  document.body()->setInnerHTML(R"HTML(
    <div inert>
      div_text
      <dialog>dialog_text</dialog>
    </div>
  )HTML");
  Element* html = document.documentElement();
  Element* body = document.body();
  Element* div = document.QuerySelector(AtomicString("div"));
  Node* div_text = div->firstChild();
  auto* dialog =
      To<HTMLDialogElement>(document.QuerySelector(AtomicString("dialog")));
  Node* dialog_text = dialog->firstChild();
  UpdateAllLifecyclePhasesForTest();

  EXPECT_FALSE(html->GetComputedStyle()->IsInert());
  EXPECT_FALSE(body->GetComputedStyle()->IsInert());
  EXPECT_TRUE(div->GetComputedStyle()->IsInert());
  EXPECT_TRUE(div_text->GetLayoutObject()->StyleRef().IsInert());
  EXPECT_EQ(dialog->GetComputedStyle(), nullptr);
  EXPECT_EQ(dialog_text->GetLayoutObject(), nullptr);

  div->SetBooleanAttribute(html_names::kInertAttr, false);
  UpdateAllLifecyclePhasesForTest();

  EXPECT_FALSE(html->GetComputedStyle()->IsInert());
  EXPECT_FALSE(body->GetComputedStyle()->IsInert());
  EXPECT_FALSE(div->GetComputedStyle()->IsInert());
  EXPECT_FALSE(div_text->GetLayoutObject()->StyleRef().IsInert());
  EXPECT_EQ(dialog->GetComputedStyle(), nullptr);
  EXPECT_EQ(dialog_text->GetLayoutObject(), nullptr);

  dialog->showModal(exception_state);
  UpdateAllLifecyclePhasesForTest();

  EXPECT_TRUE(html->GetComputedStyle()->IsInert());
  EXPECT_TRUE(body->GetComputedStyle()->IsInert());
  EXPECT_TRUE(div->GetComputedStyle()->IsInert());
  EXPECT_TRUE(div_text->GetLayoutObject()->StyleRef().IsInert());
  EXPECT_FALSE(dialog->GetComputedStyle()->IsInert());
  EXPECT_FALSE(dialog_text->GetLayoutObject()->StyleRef().IsInert());

  div->SetBooleanAttribute(html_names::kInertAttr, true);
  UpdateAllLifecyclePhasesForTest();

  EXPECT_TRUE(html->GetComputedStyle()->IsInert());
  EXPECT_TRUE(body->GetComputedStyle()->IsInert());
  EXPECT_TRUE(div->GetComputedStyle()->IsInert());
  EXPECT_TRUE(div_text->GetLayoutObject()->StyleRef().IsInert());
  EXPECT_FALSE(dialog->GetComputedStyle()->IsInert());
  EXPECT_FALSE(dialog_text->GetLayoutObject()->StyleRef().IsInert());

  dialog->close();
  UpdateAllLifecyclePhasesForTest();

  EXPECT_FALSE(html->GetComputedStyle()->IsInert());
  EXPECT_FALSE(body->GetComputedStyle()->IsInert());
  EXPECT_TRUE(div->GetComputedStyle()->IsInert());
  EXPECT_TRUE(div_text->GetLayoutObject()->StyleRef().IsInert());
  EXPECT_EQ(dialog->GetComputedStyle(), nullptr);
  EXPECT_EQ(dialog_text->GetLayoutObject(), nullptr);
}

TEST_P(ParameterizedStyleResolverTest, IsInertWithDialogs) {
  Document& document = GetDocument();
  NonThrowableExceptionState exception_state;

  document.body()->setInnerHTML(R"HTML(
    <dialog>
      dialog1_text
      <dialog>dialog2_text</dialog>
    </dialog>
    <div>
      <dialog>dialog3_text</dialog>
    </div>
  )HTML");
  StaticElementList* dialogs =
      document.QuerySelectorAll(AtomicString("dialog"));
  Element* html = document.documentElement();
  Element* body = document.body();
  auto* dialog1 = To<HTMLDialogElement>(dialogs->item(0));
  Node* dialog1_text = dialog1->firstChild();
  auto* dialog2 = To<HTMLDialogElement>(dialogs->item(1));
  Node* dialog2_text = dialog2->firstChild();
  Element* div = document.QuerySelector(AtomicString("div"));
  auto* dialog3 = To<HTMLDialogElement>(dialogs->item(2));
  Node* dialog3_text = dialog3->firstChild();
  UpdateAllLifecyclePhasesForTest();

  auto ExpectState0 = [&]() {
    EXPECT_FALSE(html->GetComputedStyle()->IsInert());
    EXPECT_FALSE(body->GetComputedStyle()->IsInert());
    EXPECT_EQ(dialog1->GetComputedStyle(), nullptr);
    EXPECT_EQ(dialog1_text->GetLayoutObject(), nullptr);
    EXPECT_EQ(dialog2->GetComputedStyle(), nullptr);
    EXPECT_EQ(dialog2_text->GetLayoutObject(), nullptr);
    EXPECT_FALSE(div->GetComputedStyle()->IsInert());
    EXPECT_EQ(dialog3->GetComputedStyle(), nullptr);
    EXPECT_EQ(dialog3_text->GetLayoutObject(), nullptr);
  };
  ExpectState0();

  dialog1->showModal(exception_state);
  UpdateAllLifecyclePhasesForTest();

  auto ExpectState1 = [&]() {
    EXPECT_TRUE(html->GetComputedStyle()->IsInert());
    EXPECT_TRUE(body->GetComputedStyle()->IsInert());
    EXPECT_FALSE(dialog1->GetComputedStyle()->IsInert());
    EXPECT_FALSE(dialog1_text->GetLayoutObject()->StyleRef().IsInert());
    EXPECT_EQ(dialog2->GetComputedStyle(), nullptr);
    EXPECT_EQ(dialog2_text->GetLayoutObject(), nullptr);
    EXPECT_TRUE(div->GetComputedStyle()->IsInert());
    EXPECT_EQ(dialog3->GetComputedStyle(), nullptr);
    EXPECT_EQ(dialog3_text->GetLayoutObject(), nullptr);
  };
  ExpectState1();

  dialog2->showModal(exception_state);
  UpdateAllLifecyclePhasesForTest();

  auto ExpectState2 = [&]() {
    EXPECT_TRUE(html->GetComputedStyle()->IsInert());
    EXPECT_TRUE(body->GetComputedStyle()->IsInert());
    EXPECT_TRUE(dialog1->GetComputedStyle()->IsInert());
    EXPECT_TRUE(dialog1_text->GetLayoutObject()->StyleRef().IsInert());
    EXPECT_FALSE(dialog2->GetComputedStyle()->IsInert());
    EXPECT_FALSE(dialog2_text->GetLayoutObject()->StyleRef().IsInert());
    EXPECT_TRUE(div->GetComputedStyle()->IsInert());
    EXPECT_EQ(dialog3->GetComputedStyle(), nullptr);
    EXPECT_EQ(dialog3_text->GetLayoutObject(), nullptr);
  };
  ExpectState2();

  dialog3->showModal(exception_state);
  UpdateAllLifecyclePhasesForTest();

  auto ExpectState3 = [&]() {
    EXPECT_TRUE(html->GetComputedStyle()->IsInert());
    EXPECT_TRUE(body->GetComputedStyle()->IsInert());
    EXPECT_TRUE(dialog1->GetComputedStyle()->IsInert());
    EXPECT_TRUE(dialog1_text->GetLayoutObject()->StyleRef().IsInert());
    EXPECT_TRUE(dialog2->GetComputedStyle()->IsInert());
    EXPECT_TRUE(dialog2_text->GetLayoutObject()->StyleRef().IsInert());
    EXPECT_TRUE(div->GetComputedStyle()->IsInert());
    EXPECT_FALSE(dialog3->GetComputedStyle()->IsInert());
    EXPECT_FALSE(dialog3_text->GetLayoutObject()->StyleRef().IsInert());
  };
  ExpectState3();

  dialog3->close();
  UpdateAllLifecyclePhasesForTest();

  ExpectState2();

  dialog2->close();
  UpdateAllLifecyclePhasesForTest();

  ExpectState1();

  dialog1->close();
  UpdateAllLifecyclePhasesForTest();

  ExpectState0();
}

static void EnterFullscreen(Document& document, Element& element) {
  LocalFrame::NotifyUserActivation(
      document.GetFrame(), mojom::UserActivationNotificationType::kTest);
  Fullscreen::RequestFullscreen(element);
  Fullscreen::DidResolveEnterFullscreenRequest(document, /*granted*/ true);
  EXPECT_EQ(Fullscreen::FullscreenElementFrom(document), element);
}

static void ExitFullscreen(Document& document) {
  Fullscreen::FullyExitFullscreen(document);
  Fullscreen::DidExitFullscreen(document);
  EXPECT_EQ(Fullscreen::FullscreenElementFrom(document), nullptr);
}

TEST_P(ParameterizedStyleResolverTest, IsInertWithFullscreen) {
  Document& document = GetDocument();
  document.body()->setInnerHTML(R"HTML(
    <div>
      div_text
      <span>span_text</span>
    </div>
    <p>p_text</p>
  )HTML");
  Element* html = document.documentElement();
  Element* body = document.body();
  Element* div = document.QuerySelector(AtomicString("div"));
  Node* div_text = div->firstChild();
  Element* span = document.QuerySelector(AtomicString("span"));
  Node* span_text = span->firstChild();
  Element* p = document.QuerySelector(AtomicString("p"));
  Node* p_text = p->firstChild();
  UpdateAllLifecyclePhasesForTest();

  auto ExpectState0 = [&]() {
    EXPECT_FALSE(html->GetComputedStyle()->IsInert());
    EXPECT_FALSE(body->GetComputedStyle()->IsInert());
    EXPECT_FALSE(div->GetComputedStyle()->IsInert());
    EXPECT_FALSE(div_text->GetLayoutObject()->StyleRef().IsInert());
    EXPECT_FALSE(span->GetComputedStyle()->IsInert());
    EXPECT_FALSE(span_text->GetLayoutObject()->StyleRef().IsInert());
    EXPECT_FALSE(p->GetComputedStyle()->IsInert());
    EXPECT_FALSE(p_text->GetLayoutObject()->StyleRef().IsInert());
  };
  ExpectState0();

  EnterFullscreen(document, *div);
  UpdateAllLifecyclePhasesForTest();

  EXPECT_TRUE(html->GetComputedStyle()->IsInert());
  EXPECT_TRUE(body->GetComputedStyle()->IsInert());
  EXPECT_FALSE(div->GetComputedStyle()->IsInert());
  EXPECT_FALSE(div_text->GetLayoutObject()->StyleRef().IsInert());
  EXPECT_FALSE(span->GetComputedStyle()->IsInert());
  EXPECT_FALSE(span_text->GetLayoutObject()->StyleRef().IsInert());
  EXPECT_TRUE(p->GetComputedStyle()->IsInert());
  EXPECT_TRUE(p_text->GetLayoutObject()->StyleRef().IsInert());

  EnterFullscreen(document, *span);
  UpdateAllLifecyclePhasesForTest();

  EXPECT_TRUE(html->GetComputedStyle()->IsInert());
  EXPECT_TRUE(body->GetComputedStyle()->IsInert());
  EXPECT_TRUE(div->GetComputedStyle()->IsInert());
  EXPECT_TRUE(div_text->GetLayoutObject()->StyleRef().IsInert());
  EXPECT_FALSE(span->GetComputedStyle()->IsInert());
  EXPECT_FALSE(span_text->GetLayoutObject()->StyleRef().IsInert());
  EXPECT_TRUE(p->GetComputedStyle()->IsInert());
  EXPECT_TRUE(p_text->GetLayoutObject()->StyleRef().IsInert());

  EnterFullscreen(document, *p);
  UpdateAllLifecyclePhasesForTest();

  EXPECT_TRUE(html->GetComputedStyle()->IsInert());
  EXPECT_TRUE(body->GetComputedStyle()->IsInert());
  EXPECT_TRUE(div->GetComputedStyle()->IsInert());
  EXPECT_TRUE(div_text->GetLayoutObject()->StyleRef().IsInert());
  EXPECT_TRUE(span->GetComputedStyle()->IsInert());
  EXPECT_TRUE(span_text->GetLayoutObject()->StyleRef().IsInert());
  EXPECT_FALSE(p->GetComputedStyle()->IsInert());
  EXPECT_FALSE(p_text->GetLayoutObject()->StyleRef().IsInert());

  ExitFullscreen(document);
  UpdateAllLifecyclePhasesForTest();

  ExpectState0();
}

TEST_P(ParameterizedStyleResolverTest, IsInertWithFrameAndFullscreen) {
  Document& document = GetDocument();
  document.body()->setInnerHTML(R"HTML(
    <div>div_text</div>
  )HTML");
  Element* html = document.documentElement();
  Element* body = document.body();
  Element* div = document.QuerySelector(AtomicString("div"));
  Node* div_text = div->firstChild();
  UpdateAllLifecyclePhasesForTest();

  EXPECT_FALSE(html->GetComputedStyle()->IsInert());
  EXPECT_FALSE(body->GetComputedStyle()->IsInert());
  EXPECT_FALSE(div->GetComputedStyle()->IsInert());
  EXPECT_FALSE(div_text->GetLayoutObject()->StyleRef().IsInert());

  EnterFullscreen(document, *div);
  UpdateAllLifecyclePhasesForTest();

  EXPECT_TRUE(html->GetComputedStyle()->IsInert());
  EXPECT_TRUE(body->GetComputedStyle()->IsInert());
  EXPECT_FALSE(div->GetComputedStyle()->IsInert());
  EXPECT_FALSE(div_text->GetLayoutObject()->StyleRef().IsInert());

  EnterFullscreen(document, *body);
  UpdateAllLifecyclePhasesForTest();

  EXPECT_TRUE(html->GetComputedStyle()->IsInert());
  EXPECT_FALSE(body->GetComputedStyle()->IsInert());
  EXPECT_FALSE(div->GetComputedStyle()->IsInert());
  EXPECT_FALSE(div_text->GetLayoutObject()->StyleRef().IsInert());

  EnterFullscreen(document, *html);
  UpdateAllLifecyclePhasesForTest();

  EXPECT_FALSE(html->GetComputedStyle()->IsInert());
  EXPECT_FALSE(body->GetComputedStyle()->IsInert());
  EXPECT_FALSE(div->GetComputedStyle()->IsInert());
  EXPECT_FALSE(div_text->GetLayoutObject()->StyleRef().IsInert());
  ExitFullscreen(document);
}

TEST_P(ParameterizedStyleResolverTest, IsInertWithBackdrop) {
  ScopedBackdropInheritOriginatingForTest backdrop_inherit(true);

  Document& document = GetDocument();
  NonThrowableExceptionState exception_state;

  document.documentElement()->setInnerHTML(R"HTML(
    <style>:root:fullscreen::backdrop { --enable: true }</style>
    <dialog></dialog>
  )HTML");
  Element* html = document.documentElement();
  Element* body = document.body();
  auto* dialog =
      To<HTMLDialogElement>(document.QuerySelector(AtomicString("dialog")));

  auto IsBackdropInert = [](Element* element) {
    PseudoElement* backdrop = element->GetPseudoElement(kPseudoIdBackdrop);
    EXPECT_NE(backdrop, nullptr) << element;
    return backdrop->GetComputedStyle()->IsInert();
  };

  EnterFullscreen(document, *body);
  UpdateAllLifecyclePhasesForTest();

  EXPECT_EQ(html->GetPseudoElement(kPseudoIdBackdrop), nullptr);
  EXPECT_FALSE(IsBackdropInert(body));
  EXPECT_EQ(dialog->GetPseudoElement(kPseudoIdBackdrop), nullptr);

  dialog->showModal(exception_state);
  UpdateAllLifecyclePhasesForTest();

  EXPECT_EQ(html->GetPseudoElement(kPseudoIdBackdrop), nullptr);
  EXPECT_TRUE(IsBackdropInert(body));
  EXPECT_FALSE(IsBackdropInert(dialog));

  dialog->close();
  UpdateAllLifecyclePhasesForTest();

  EXPECT_EQ(html->GetPseudoElement(kPseudoIdBackdrop), nullptr);
  EXPECT_FALSE(IsBackdropInert(body));
  EXPECT_EQ(dialog->GetPseudoElement(kPseudoIdBackdrop), nullptr);

  EnterFullscreen(document, *html);
  UpdateAllLifecyclePhasesForTest();

  EXPECT_FALSE(IsBackdropInert(html));
  EXPECT_FALSE(IsBackdropInert(body));
  EXPECT_EQ(dialog->GetPseudoElement(kPseudoIdBackdrop), nullptr);

  dialog->showModal(exception_state);
  UpdateAllLifecyclePhasesForTest();

  EXPECT_TRUE(IsBackdropInert(html));
  EXPECT_TRUE(IsBackdropInert(body));
  EXPECT_FALSE(IsBackdropInert(dialog));
  ExitFullscreen(document);
}

TEST_P(ParameterizedStyleResolverTest, IsInertWithDialogAndFullscreen) {
  Document& document = GetDocument();
  NonThrowableExceptionState exception_state;

  document.body()->setInnerHTML(R"HTML(
    <div></div>
    <dialog></dialog>
  )HTML");
  Element* html = document.documentElement();
  Element* body = document.body();
  Element* div = document.QuerySelector(AtomicString("div"));
  auto* dialog =
      To<HTMLDialogElement>(document.QuerySelector(AtomicString("dialog")));
  UpdateAllLifecyclePhasesForTest();

  EXPECT_FALSE(html->GetComputedStyle()->IsInert());
  EXPECT_FALSE(body->GetComputedStyle()->IsInert());
  EXPECT_FALSE(div->GetComputedStyle()->IsInert());
  EXPECT_EQ(dialog->GetComputedStyle(), nullptr);

  EnterFullscreen(document, *div);
  UpdateAllLifecyclePhasesForTest();

  EXPECT_TRUE(html->GetComputedStyle()->IsInert());
  EXPECT_TRUE(body->GetComputedStyle()->IsInert());
  EXPECT_FALSE(div->GetComputedStyle()->IsInert());
  EXPECT_EQ(dialog->GetComputedStyle(), nullptr);

  dialog->showModal(exception_state);
  UpdateAllLifecyclePhasesForTest();

  EXPECT_TRUE(html->GetComputedStyle()->IsInert());
  EXPECT_TRUE(body->GetComputedStyle()->IsInert());
  EXPECT_TRUE(div->GetComputedStyle()->IsInert());
  EXPECT_FALSE(dialog->GetComputedStyle()->IsInert());

  dialog->close();
  UpdateAllLifecyclePhasesForTest();

  EXPECT_TRUE(html->GetComputedStyle()->IsInert());
  EXPECT_TRUE(body->GetComputedStyle()->IsInert());
  EXPECT_FALSE(div->GetComputedStyle()->IsInert());
  EXPECT_EQ(dialog->GetComputedStyle(), nullptr);

  ExitFullscreen(document);
  UpdateAllLifecyclePhasesForTest();

  EXPECT_FALSE(html->GetComputedStyle()->IsInert());
  EXPECT_FALSE(body->GetComputedStyle()->IsInert());
  EXPECT_FALSE(div->GetComputedStyle()->IsInert());
  EXPECT_EQ(dialog->GetComputedStyle(), nullptr);

  dialog->showModal(exception_state);
  UpdateAllLifecyclePhasesForTest();

  EXPECT_TRUE(html->GetComputedStyle()->IsInert());
  EXPECT_TRUE(body->GetComputedStyle()->IsInert());
  EXPECT_TRUE(div->GetComputedStyle()->IsInert());
  EXPECT_FALSE(dialog->GetComputedStyle()->IsInert());

  EnterFullscreen(document, *div);
  UpdateAllLifecyclePhasesForTest();

  EXPECT_TRUE(html->GetComputedStyle()->IsInert());
  EXPECT_TRUE(body->GetComputedStyle()->IsInert());
  EXPECT_TRUE(div->GetComputedStyle()->IsInert());
  EXPECT_FALSE(dialog->GetComputedStyle()->IsInert());

  ExitFullscreen(document);
  UpdateAllLifecyclePhasesForTest();

  EXPECT_TRUE(html->GetComputedStyle()->IsInert());
  EXPECT_TRUE(body->GetComputedStyle()->IsInert());
  EXPECT_TRUE(div->GetComputedStyle()->IsInert());
  EXPECT_FALSE(dialog->GetComputedStyle()->IsInert());

  dialog->close();
  UpdateAllLifecyclePhasesForTest();

  EXPECT_FALSE(html->GetComputedStyle()->IsInert());
  EXPECT_FALSE(body->GetComputedStyle()->IsInert());
  EXPECT_FALSE(div->GetComputedStyle()->IsInert());
  EXPECT_EQ(dialog->GetComputedStyle(), nullptr);
}

TEST_P(StyleResolverTestCQ, StyleRulesForElementContainerQuery) {
  GetDocument().documentElement()->setInnerHTML(R"HTML(
    <style>
      #container { container-type: inline-size }
      @container (min-width: 1px) {
        #target { }
      }
      @container (min-width: 99999px) {
        #target { color: red }
      }
    </style>
    <div id="container">
      <div id="target"></div>
    </div>
  )HTML");

  UpdateAllLifecyclePhasesForTest();

  auto* target = GetDocument().getElementById(AtomicString("target"));
  auto& resolver = GetDocument().GetStyleResolver();

  auto* rule_list =
      resolver.StyleRulesForElement(target, StyleResolver::kAuthorCSSRules);
  ASSERT_TRUE(rule_list);
  ASSERT_EQ(rule_list->size(), 1u)
      << "The empty #target rule in the container query should be collected";
  EXPECT_TRUE(rule_list->at(0)->Properties().IsEmpty())
      << "Check that it is in fact the empty rule";
}

TEST_P(ParameterizedStyleResolverTest, LegacyOverlapPerspectiveOrigin_Single) {
  SetBodyInnerHTML(R"HTML(
      <style>
        div {
          perspective-origin: 1px 2px;
        }
      </style>
      <div>target</div>
    )HTML");
  EXPECT_FALSE(IsUseCounted(WebFeature::kCSSLegacyPerspectiveOrigin))
      << "Not counted when only perspective-origin is used";
}

TEST_P(ParameterizedStyleResolverTest, LegacyOverlapPerspectiveOrigin_Order) {
  SetBodyInnerHTML(R"HTML(
      <style>
        div {
          -webkit-perspective-origin-x: 1px;
          -webkit-perspective-origin-y: 2px;
          perspective-origin: 3px 4px;
        }
      </style>
      <div>target</div>
    )HTML");
  EXPECT_FALSE(IsUseCounted(WebFeature::kCSSLegacyPerspectiveOrigin))
      << "Not counted when perspective-origin is last";
}

TEST_P(ParameterizedStyleResolverTest, LegacyOverlapPerspectiveOrigin_Values) {
  SetBodyInnerHTML(R"HTML(
      <style>
        div {
          perspective-origin: 1px 2px;
          -webkit-perspective-origin-x: 1px;
          -webkit-perspective-origin-y: 2px;
        }
      </style>
      <div>target</div>
    )HTML");
  EXPECT_FALSE(IsUseCounted(WebFeature::kCSSLegacyPerspectiveOrigin))
      << "Not counted when values are the same";
}

TEST_P(ParameterizedStyleResolverTest, LegacyOverlapPerspectiveOrigin_Last) {
  SetBodyInnerHTML(R"HTML(
      <style>
        div {
          perspective-origin: 1px 2px;
          -webkit-perspective-origin-x: 3px;
          -webkit-perspective-origin-y: 4px;
        }
      </style>
      <div>target</div>
    )HTML");
  EXPECT_TRUE(IsUseCounted(WebFeature::kCSSLegacyPerspectiveOrigin))
      << "Counted when -webkit-perspective-* is last with different values";
}

TEST_P(ParameterizedStyleResolverTest, LegacyOverlapTransformOrigin_Single) {
  SetBodyInnerHTML(R"HTML(
      <style>
        div {
          transform-origin: 1px 2px 3px;
        }
      </style>
      <div>target</div>
    )HTML");
  EXPECT_FALSE(IsUseCounted(WebFeature::kCSSLegacyTransformOrigin))
      << "Not counted when only transform-origin is used";
}

TEST_P(ParameterizedStyleResolverTest, LegacyOverlapTransformOrigin_Order) {
  SetBodyInnerHTML(R"HTML(
      <style>
        div {
          -webkit-transform-origin-x: 1px;
          -webkit-transform-origin-y: 2px;
          -webkit-transform-origin-z: 3px;
          transform-origin: 4px 5px 6px;
        }
      </style>
      <div>target</div>
    )HTML");
  EXPECT_FALSE(IsUseCounted(WebFeature::kCSSLegacyTransformOrigin))
      << "Not counted when transform-origin is last";
}

TEST_P(ParameterizedStyleResolverTest, LegacyOverlapTransformOrigin_Values) {
  SetBodyInnerHTML(R"HTML(
      <style>
        div {
          transform-origin: 1px 2px 3px;
          -webkit-transform-origin-x: 1px;
          -webkit-transform-origin-y: 2px;
          -webkit-transform-origin-z: 3px;
        }
      </style>
      <div>target</div>
    )HTML");
  EXPECT_FALSE(IsUseCounted(WebFeature::kCSSLegacyTransformOrigin))
      << "Not counted when values are the same";
}

TEST_P(ParameterizedStyleResolverTest, LegacyOverlapTransformOrigin_Last) {
  SetBodyInnerHTML(R"HTML(
      <style>
        div {
          transform-origin: 1px 2px 3px;
          -webkit-transform-origin-x: 4px;
          -webkit-transform-origin-y: 5px;
          -webkit-transform-origin-z: 6px;
        }
      </style>
      <div>target</div>
    )HTML");
  EXPECT_TRUE(IsUseCounted(WebFeature::kCSSLegacyTransformOrigin))
      << "Counted when -webkit-transform-origin-* is last with different "
         "values";
}

TEST_P(ParameterizedStyleResolverTest, LegacyOverlapBorderImage_Single) {
  SetBodyInnerHTML(R"HTML(
      <style>
        div {
          border-image: url("#a") 1 fill / 2 / 3 round;
        }
      </style>
      <div>target</div>
    )HTML");
  EXPECT_FALSE(IsUseCounted(WebFeature::kCSSLegacyBorderImage))
      << "Not counted when only border-image is used";
}

TEST_P(ParameterizedStyleResolverTest, LegacyOverlapBorderImage_Order) {
  SetBodyInnerHTML(R"HTML(
      <style>
        div {
          -webkit-border-image: url("#b") 2 fill / 3 / 4 round;
          border-image: url("#a") 1 fill / 2 / 3 round;
        }
      </style>
      <div>target</div>
    )HTML");
  EXPECT_FALSE(IsUseCounted(WebFeature::kCSSLegacyBorderImage))
      << "Not counted when border-image is last";
}

TEST_P(ParameterizedStyleResolverTest, LegacyOverlapBorderImage_Values) {
  SetBodyInnerHTML(R"HTML(
      <style>
        div {
          border-image: url("#a") 1 fill / 2 / 3 round;
          -webkit-border-image: url("#a") 1 fill / 2 / 3 round;
        }
      </style>
      <div>target</div>
    )HTML");
  EXPECT_FALSE(IsUseCounted(WebFeature::kCSSLegacyBorderImage))
      << "Not counted when values are the same";
}

TEST_P(ParameterizedStyleResolverTest, LegacyOverlapBorderImage_Last_Source) {
  SetBodyInnerHTML(R"HTML(
      <style>
        div {
          border-image: url("#a") 1 fill / 2 / 3 round;
          -webkit-border-image: url("#b") 1 fill / 2 / 3 round;
        }
      </style>
      <div>target</div>
    )HTML");
  EXPECT_TRUE(IsUseCounted(WebFeature::kCSSLegacyBorderImage))
      << "Counted when border-image-source differs";
}

TEST_P(ParameterizedStyleResolverTest, LegacyOverlapBorderImage_Last_Slice) {
  SetBodyInnerHTML(R"HTML(
      <style>
        div {
          border-image: url("#a") 1 fill / 2 / 3 round;
          -webkit-border-image: url("#a") 2 fill / 2 / 3 round;
        }
      </style>
      <div>target</div>
    )HTML");
  EXPECT_TRUE(IsUseCounted(WebFeature::kCSSLegacyBorderImage))
      << "Counted when border-image-slice differs";
}

TEST_P(ParameterizedStyleResolverTest,
       LegacyOverlapBorderImage_Last_SliceFill) {
  SetBodyInnerHTML(R"HTML(
      <style>
        div {
          border-image: url("#a") 1 / 2 / 3 round;
          -webkit-border-image: url("#a") 1 fill / 2 / 3 round;
        }
      </style>
      <div>target</div>
    )HTML");
  EXPECT_TRUE(IsUseCounted(WebFeature::kCSSLegacyBorderImage))
      << "Counted when the fill keyword of border-image-slice differs";
}

TEST_P(ParameterizedStyleResolverTest,
       LegacyOverlapBorderImage_SliceFillImplicit) {
  SetBodyInnerHTML(R"HTML(
      <style>
        div {
          border-image: url("#a") 1 / 2 / 3 round;
          -webkit-border-image: url("#a") 1 / 2 / 3 round;
        }
      </style>
      <div>target</div>
    )HTML");
  // Note that -webkit-border-image implicitly adds "fill", but
  // border-image does not.
  EXPECT_TRUE(IsUseCounted(WebFeature::kCSSLegacyBorderImage))
      << "Counted when fill-less values are the same";
}

TEST_P(ParameterizedStyleResolverTest, LegacyOverlapBorderImage_Last_Width) {
  SetBodyInnerHTML(R"HTML(
      <style>
        div {
          border-image: url("#a") 1 fill / 2 / 3 round;
          -webkit-border-image: url("#a") 1 fill / 5 / 3 round;
        }
      </style>
      <div>target</div>
    )HTML");
  EXPECT_TRUE(IsUseCounted(WebFeature::kCSSLegacyBorderImage))
      << "Counted when border-image-slice differs";
}

TEST_P(ParameterizedStyleResolverTest, LegacyOverlapBorderImage_Last_Outset) {
  SetBodyInnerHTML(R"HTML(
      <style>
        div {
          border-image: url("#a") 1 fill / 2 / 3 round;
          -webkit-border-image: url("#a") 1 fill / 2 / 5 round;
        }
      </style>
      <div>target</div>
    )HTML");
  EXPECT_TRUE(IsUseCounted(WebFeature::kCSSLegacyBorderImage))
      << "Counted when border-image-outset differs";
}

TEST_P(ParameterizedStyleResolverTest, LegacyOverlapBorderImage_Last_Repeat) {
  SetBodyInnerHTML(R"HTML(
      <style>
        div {
          border-image: url("#a") 1 fill / 2 / 3 round;
          -webkit-border-image: url("#a") 1 fill / 2 / 3 space;
        }
      </style>
      <div>target</div>
    )HTML");
  EXPECT_TRUE(IsUseCounted(WebFeature::kCSSLegacyBorderImage))
      << "Counted when border-image-repeat differs";
}

TEST_P(ParameterizedStyleResolverTest, LegacyOverlapBorderImageWidth_Single) {
  SetBodyInnerHTML(R"HTML(
    <style>
      div {
        border: 1px solid black;
      }
    </style>
    <div>target</div>
  )HTML");
  EXPECT_FALSE(IsUseCounted(WebFeature::kCSSLegacyBorderImageWidth))
      << "Not counted when only border is used";
}

TEST_P(ParameterizedStyleResolverTest, LegacyOverlapBorderImageWidth_Order) {
  SetBodyInnerHTML(R"HTML(
    <style>
      div {
        -webkit-border-image: url("#b") 2 fill / 3px / 4 round;
        border: 1px solid black;
      }
    </style>
    <div>target</div>
  )HTML");
  EXPECT_FALSE(IsUseCounted(WebFeature::kCSSLegacyBorderImageWidth))
      << "Not counted when border is last";
}

TEST_P(ParameterizedStyleResolverTest, LegacyOverlapBorderImageWidth_Values) {
  SetBodyInnerHTML(R"HTML(
    <style>
      div {
        border: 1px solid black;
        -webkit-border-image: url("#b") 2 fill / 1px / 4 round;
      }
    </style>
    <div>target</div>
  )HTML");
  EXPECT_FALSE(IsUseCounted(WebFeature::kCSSLegacyBorderImageWidth))
      << "Not counted when values are the same";
}

TEST_P(ParameterizedStyleResolverTest,
       LegacyOverlapBorderImageWidth_Last_Border) {
  SetBodyInnerHTML(R"HTML(
      <style>
        div {
          border: 1px solid black;
          -webkit-border-image: url("#a") 1 fill / 2px / 3 round;
        }
      </style>
      <div>target</div>
    )HTML");
  // Since -webkit-border-image also sets border-width, we would normally
  // expect TRUE here. However, StyleCascade always applies
  // -webkit-border-image *first*, and does not do anything to prevent
  // border-width properties from also being applied. Hence border-width
  // always wins.
  EXPECT_FALSE(IsUseCounted(WebFeature::kCSSLegacyBorderImageWidth))
      << "Not even counted when -webkit-border-image is last";
}

TEST_P(ParameterizedStyleResolverTest,
       LegacyOverlapBorderImageWidth_Last_Style) {
  // Note that border-style is relevant here because the used border-width
  // is 0px if we don'y have any border-style. See e.g.
  // ComputedStyle::BorderLeftWidth.
  SetBodyInnerHTML(R"HTML(
      <style>
        div {
          border-style: solid;
          -webkit-border-image: url("#b") 1 fill / 2px / 3 round;
        }
      </style>
      <div>target</div>
    )HTML");
  EXPECT_TRUE(IsUseCounted(WebFeature::kCSSLegacyBorderImageWidth))
      << "Counted when -webkit-border-image is last and there's no "
         "border-width";
}

TEST_P(ParameterizedStyleResolverTest, PositionFallbackStylesBasic_Cascade) {
  ScopedCSSAnchorPositioningForTest enabled(true);
  ScopedCSSAnchorPositioningCascadeFallbackForTest cascade(true);

  SetBodyInnerHTML(R"HTML(
    <style>
      @position-fallback --fallback {
        @try { }
        @try { left: 100px; }
        @try { top: 100px; }
        @try { inset: 50px; }
      }
      #target {
        position: absolute;
        position-fallback: --fallback;
      }
    </style>
    <div id="target"></div>
  )HTML");

  UpdateAllLifecyclePhasesForTest();

  ScopedCSSName* fallback_name = MakeGarbageCollected<ScopedCSSName>(
      AtomicString("--fallback"), &GetDocument());
  Element* target = GetElementById("target");
  const ComputedStyle* base_style = target->GetComputedStyle();
  ASSERT_TRUE(base_style);
  EXPECT_EQ(Length::Auto(), GetTop(*base_style));
  EXPECT_EQ(Length::Auto(), GetLeft(*base_style));

  UpdateStyleForPositionFallback(*target, fallback_name, 1);
  const ComputedStyle* try1 = target->GetComputedStyle();
  ASSERT_TRUE(try1);
  EXPECT_EQ(Length::Auto(), GetTop(*try1));
  EXPECT_EQ(Length::Fixed(100), GetLeft(*try1));

  UpdateStyleForPositionFallback(*target, fallback_name, 2);
  const ComputedStyle* try2 = target->GetComputedStyle();
  ASSERT_TRUE(try2);
  EXPECT_EQ(Length::Fixed(100), GetTop(*try2));
  EXPECT_EQ(Length::Auto(), GetLeft(*try2));

  // Shorthand should also work
  UpdateStyleForPositionFallback(*target, fallback_name, 3);
  const ComputedStyle* try3 = target->GetComputedStyle();
  ASSERT_TRUE(try3);
  EXPECT_EQ(Length::Fixed(50), GetTop(*try3));
  EXPECT_EQ(Length::Fixed(50), GetLeft(*try3));
  EXPECT_EQ(Length::Fixed(50), GetBottom(*try3));
  EXPECT_EQ(Length::Fixed(50), GetRight(*try3));

  // Style without fallback when index is out of bounds.
  UpdateStyleForPositionFallback(*target, fallback_name, 4);
  const ComputedStyle* try4 = target->GetComputedStyle();
  EXPECT_EQ(Length::Auto(), GetTop(*try4));
  EXPECT_EQ(Length::Auto(), GetLeft(*try4));
}

TEST_P(ParameterizedStyleResolverTest,
       PositionFallbackStylesResolveLogicalProperties_Cascade) {
  ScopedCSSAnchorPositioningForTest enabled(true);
  ScopedCSSAnchorPositioningCascadeFallbackForTest cascade(true);

  SetBodyInnerHTML(R"HTML(
    <style>
      @position-fallback --fallback {
        @try { }
        @try { inset-inline-start: 100px; }
        @try { inset-block: 100px 90px; }
      }
      #target {
        position: absolute;
        writing-mode: vertical-rl;
        direction: rtl;
        inset: 50px;
        position-fallback: --fallback;
      }
    </style>
    <div id="target"></div>
  )HTML");

  UpdateAllLifecyclePhasesForTest();

  ScopedCSSName* fallback_name = MakeGarbageCollected<ScopedCSSName>(
      AtomicString("--fallback"), &GetDocument());
  Element* target = GetElementById("target");
  const ComputedStyle* base_style = target->GetComputedStyle();
  ASSERT_TRUE(base_style);
  EXPECT_EQ(Length::Fixed(50), GetTop(*base_style));
  EXPECT_EQ(Length::Fixed(50), GetLeft(*base_style));
  EXPECT_EQ(Length::Fixed(50), GetBottom(*base_style));
  EXPECT_EQ(Length::Fixed(50), GetRight(*base_style));

  // 'inset-inline-start' should resolve to 'bottom'
  UpdateStyleForPositionFallback(*target, fallback_name, 1);
  const ComputedStyle* try1 = target->GetComputedStyle();
  ASSERT_TRUE(try1);
  EXPECT_EQ(Length::Fixed(50), GetTop(*try1));
  EXPECT_EQ(Length::Fixed(50), GetLeft(*try1));
  EXPECT_EQ(Length::Fixed(100), GetBottom(*try1));
  EXPECT_EQ(Length::Fixed(50), GetRight(*try1));

  // 'inset-block' with two parameters should set 'right' and then 'left'
  UpdateStyleForPositionFallback(*target, fallback_name, 2);
  const ComputedStyle* try2 = target->GetComputedStyle();
  ASSERT_TRUE(try2);
  EXPECT_EQ(Length::Fixed(50), GetTop(*try2));
  EXPECT_EQ(Length::Fixed(90), GetLeft(*try2));
  EXPECT_EQ(Length::Fixed(50), GetBottom(*try2));
  EXPECT_EQ(Length::Fixed(100), GetRight(*try2));

  // @try index out of bounds
  UpdateStyleForPositionFallback(*target, fallback_name, 3);
  const ComputedStyle* try3 = target->GetComputedStyle();
  ASSERT_TRUE(try3);
  EXPECT_EQ(Length::Fixed(50), GetTop(*try3));
  EXPECT_EQ(Length::Fixed(50), GetLeft(*try3));
  EXPECT_EQ(Length::Fixed(50), GetBottom(*try3));
  EXPECT_EQ(Length::Fixed(50), GetRight(*try3));
}

TEST_P(ParameterizedStyleResolverTest,
       PositionFallbackStylesResolveRelativeLengthUnits_Cascade) {
  ScopedCSSAnchorPositioningForTest enabled(true);
  ScopedCSSAnchorPositioningCascadeFallbackForTest cascade(true);

  SetBodyInnerHTML(R"HTML(
    <style>
      @position-fallback --fallback {
        @try { }
        @try { top: 2em; }
      }
      #target {
        position: absolute;
        font-size: 20px;
        position-fallback: --fallback;
      }
    </style>
    <div id="target"></div>
  )HTML");

  UpdateAllLifecyclePhasesForTest();

  ScopedCSSName* fallback_name = MakeGarbageCollected<ScopedCSSName>(
      AtomicString("--fallback"), &GetDocument());
  Element* target = GetElementById("target");
  const ComputedStyle* base_style = target->GetComputedStyle();
  ASSERT_TRUE(base_style);
  EXPECT_EQ(Length::Auto(), GetTop(*base_style));

  // '2em' should resolve to '40px'
  UpdateStyleForPositionFallback(*target, fallback_name, 1);
  const ComputedStyle* try1 = target->GetComputedStyle();
  ASSERT_TRUE(try1);
  EXPECT_EQ(Length::Fixed(40), GetTop(*try1));
}

TEST_P(ParameterizedStyleResolverTest,
       PositionFallbackStylesInBeforePseudoElement_Cascade) {
  ScopedCSSAnchorPositioningForTest enabled(true);
  ScopedCSSAnchorPositioningCascadeFallbackForTest cascade(true);

  SetBodyInnerHTML(R"HTML(
    <style>
      @position-fallback --fallback {
        @try { }
        @try { top: 50px; }
      }
      #target::before {
        display: block;
        content: 'before';
        position: absolute;
        position-fallback: --fallback;
      }
    </style>
    <div id="target"></div>
  )HTML");

  UpdateAllLifecyclePhasesForTest();

  ScopedCSSName* fallback_name = MakeGarbageCollected<ScopedCSSName>(
      AtomicString("--fallback"), &GetDocument());
  Element* target = GetElementById("target");
  Element* before = target->GetPseudoElement(kPseudoIdBefore);
  ASSERT_TRUE(before);

  const ComputedStyle* base_style = before->GetComputedStyle();
  ASSERT_TRUE(base_style);
  EXPECT_EQ(Length::Auto(), GetTop(*base_style));

  // 'position-fallback' applies to ::before pseudo-element.
  UpdateStyleForPositionFallback(*before, fallback_name, 1);
  const ComputedStyle* try1 = before->GetComputedStyle();
  ASSERT_TRUE(try1);
  EXPECT_EQ(Length::Fixed(50), GetTop(*try1));
}

TEST_P(ParameterizedStyleResolverTest,
       PositionFallbackStylesCSSWideKeywords_Cascade) {
  ScopedCSSAnchorPositioningForTest enabled(true);
  ScopedCSSAnchorPositioningCascadeFallbackForTest cascade(true);

  SetBodyInnerHTML(R"HTML(
    <style>
      @position-fallback --fallback {
        @try { }
        @try { top: initial }
        @try { left: inherit }
        @try { right: unset }
        /" 'revert' and 'revert-layer' are already rejected by parser */
      }
      #target {
        position: absolute;
        inset: 50px;
        position-fallback: --fallback;
      }
      #container {
        position: absolute;
        inset: 100px;
      }
    </style>
    <div id="container">
      <div id="target"></div>
    </div>
  )HTML");

  UpdateAllLifecyclePhasesForTest();

  ScopedCSSName* fallback_name = MakeGarbageCollected<ScopedCSSName>(
      AtomicString("--fallback"), &GetDocument());
  Element* target = GetElementById("target");
  const ComputedStyle* base_style = target->GetComputedStyle();
  ASSERT_TRUE(base_style);
  EXPECT_EQ(Length::Fixed(50), GetTop(*base_style));
  EXPECT_EQ(Length::Fixed(50), GetLeft(*base_style));
  EXPECT_EQ(Length::Fixed(50), GetBottom(*base_style));
  EXPECT_EQ(Length::Fixed(50), GetRight(*base_style));

  UpdateStyleForPositionFallback(*target, fallback_name, 1);
  const ComputedStyle* try1 = target->GetComputedStyle();
  ASSERT_TRUE(try1);
  EXPECT_EQ(Length::Auto(), GetTop(*try1));
  EXPECT_EQ(Length::Fixed(50), GetLeft(*try1));
  EXPECT_EQ(Length::Fixed(50), GetBottom(*try1));
  EXPECT_EQ(Length::Fixed(50), GetRight(*try1));

  UpdateStyleForPositionFallback(*target, fallback_name, 2);
  const ComputedStyle* try2 = target->GetComputedStyle();
  ASSERT_TRUE(try2);
  EXPECT_EQ(Length::Fixed(50), GetTop(*try2));
  EXPECT_EQ(Length::Fixed(100), GetLeft(*try2));
  EXPECT_EQ(Length::Fixed(50), GetBottom(*try2));
  EXPECT_EQ(Length::Fixed(50), GetRight(*try2));

  UpdateStyleForPositionFallback(*target, fallback_name, 3);
  const ComputedStyle* try3 = target->GetComputedStyle();
  ASSERT_TRUE(try3);
  EXPECT_EQ(Length::Fixed(50), GetTop(*try3));
  EXPECT_EQ(Length::Fixed(50), GetLeft(*try3));
  EXPECT_EQ(Length::Fixed(50), GetBottom(*try3));
  EXPECT_EQ(Length::Auto(), GetRight(*try3));
}

TEST_P(ParameterizedStyleResolverTest,
       PositionFallbackPropertyValueChange_Cascade) {
  ScopedCSSAnchorPositioningForTest enabled(true);
  ScopedCSSAnchorPositioningCascadeFallbackForTest cascade(true);

  SetBodyInnerHTML(R"HTML(
    <style>
      @position-fallback --foo {
        @try { }
        @try { top: 100px }
      }
      @position-fallback --bar {
        @try { }
        @try { left: 100px }
      }
      #target {
        position: absolute;
        position-fallback: --foo;
      }
    </style>
    <div id="target"></div>
  )HTML");

  UpdateAllLifecyclePhasesForTest();

  ScopedCSSName* foo_name = MakeGarbageCollected<ScopedCSSName>(
      AtomicString("--foo"), &GetDocument());
  ScopedCSSName* bar_name = MakeGarbageCollected<ScopedCSSName>(
      AtomicString("--bar"), &GetDocument());
  Element* target = GetElementById("target");

  {
    const ComputedStyle* base_style = target->GetComputedStyle();
    ASSERT_TRUE(base_style);
    EXPECT_EQ(Length::Auto(), GetTop(*base_style));
    EXPECT_EQ(Length::Auto(), GetLeft(*base_style));

    UpdateStyleForPositionFallback(*target, foo_name, 1);
    const ComputedStyle* fallback = target->GetComputedStyle();
    ASSERT_TRUE(fallback);
    EXPECT_EQ(Length::Fixed(100), GetTop(*fallback));
    EXPECT_EQ(Length::Auto(), GetLeft(*fallback));
  }

  target->SetInlineStyleProperty(CSSPropertyID::kPositionFallback, "--bar");
  UpdateAllLifecyclePhasesForTest();

  {
    const ComputedStyle* base_style = target->GetComputedStyle();
    ASSERT_TRUE(base_style);
    EXPECT_EQ(Length::Auto(), GetTop(*base_style));
    EXPECT_EQ(Length::Auto(), GetLeft(*base_style));

    UpdateStyleForPositionFallback(*target, bar_name, 1);
    const ComputedStyle* fallback = target->GetComputedStyle();
    ASSERT_TRUE(fallback);
    ASSERT_TRUE(fallback);
    EXPECT_EQ(Length::Auto(), GetTop(*fallback));
    EXPECT_EQ(Length::Fixed(100), GetLeft(*fallback));
  }
}

TEST_P(ParameterizedStyleResolverTest, PositionFallback_PersistentTrySet) {
  ScopedCSSAnchorPositioningForTest enabled(true);
  ScopedCSSAnchorPositioningCascadeFallbackForTest cascade(true);

  SetBodyInnerHTML(R"HTML(
    <style>
      @position-fallback --fallback {
        @try { left: 100px; }
        @try { top: 100px; }
      }
      #target {
        position: absolute;
        position-fallback: --fallback;
      }
    </style>
    <div id="target"></div>
  )HTML");

  UpdateAllLifecyclePhasesForTest();

  Element* target = GetElementById("target");
  const ComputedStyle* style = target->GetComputedStyle();
  ASSERT_TRUE(style);
  EXPECT_EQ(Length::Fixed(100), GetLeft(*style));
  EXPECT_EQ(Length::Auto(), GetTop(*style));
  EXPECT_TRUE(target->GetPositionFallbackData() &&
              target->GetPositionFallbackData()->GetTryPropertyValueSet());

  // The set should be cleared when 'position-fallback' is cleared.
  target->SetInlineStyleProperty(CSSPropertyID::kPositionFallback, "none");
  UpdateAllLifecyclePhasesForTest();
  EXPECT_FALSE(target->GetPositionFallbackData() &&
               target->GetPositionFallbackData()->GetTryPropertyValueSet());

  target->SetInlineStyleProperty(CSSPropertyID::kPositionFallback,
                                 "--fallback");
  UpdateAllLifecyclePhasesForTest();
  EXPECT_TRUE(target->GetPositionFallbackData() &&
              target->GetPositionFallbackData()->GetTryPropertyValueSet());

  // The set should also be cleared when referencing a non-existent fallback.
  target->SetInlineStyleProperty(CSSPropertyID::kPositionFallback, "--unknown");
  UpdateAllLifecyclePhasesForTest();
  EXPECT_FALSE(target->GetPositionFallbackData() &&
               target->GetPositionFallbackData()->GetTryPropertyValueSet());
}

TEST_P(ParameterizedStyleResolverTest, PositionFallback_PaintInvalidation) {
  ScopedCSSAnchorPositioningForTest enabled(true);
  ScopedCSSAnchorPositioningCascadeFallbackForTest cascade(true);

  SetBodyInnerHTML(R"HTML(
    <style>
      @position-fallback --fallback {
        @try { left: 1111111px; }
        @try { left: 2222222px; }
        @try { left: 3333333px; }
        @try { top: 100px; }
      }
      #target {
        position: absolute;
        position-fallback: --fallback;
      }
    </style>
    <div id="target"></div>
  )HTML");

  UpdateAllLifecyclePhasesForTest();

  Element* target = GetElementById("target");
  const ComputedStyle* style = target->GetComputedStyle();
  ASSERT_TRUE(style);
  EXPECT_EQ(Length::Fixed(100), GetTop(*style));
  EXPECT_EQ(Length::Auto(), GetLeft(*style));

  EXPECT_FALSE(target->GetLayoutObject()->NeedsLayout());

  // Invalidate paint (but not layout).
  target->SetInlineStyleProperty(CSSPropertyID::kBackgroundColor, "green");
  target->GetDocument().UpdateStyleAndLayoutTreeForThisDocument();

  EXPECT_FALSE(target->GetLayoutObject()->NeedsLayout());
  EXPECT_TRUE(target->GetLayoutObject()->ShouldCheckForPaintInvalidation());
}

TEST_P(ParameterizedStyleResolverTest, TrySet_Basic) {
  SetBodyInnerHTML(R"HTML(
    <style>
      div {
        position: absolute;
        left: 10px;
      }
    </style>
    <div id=div></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();

  Element* div = GetElementById("div");
  ASSERT_TRUE(div);
  EXPECT_EQ("10px", ComputedValue("left", div->ComputedStyleRef()));
  EXPECT_EQ("auto", ComputedValue("right", div->ComputedStyleRef()));

  // Resolving a style with some try set stored on Element,
  // should cause that set to be added to the cascade.

  const CSSPropertyValueSet* try_set =
      css_test_helpers::ParseDeclarationBlock(R"CSS(
      left: 20px;
      right: 30px;
  )CSS");
  ASSERT_TRUE(try_set);

  div->EnsurePositionFallbackData().SetTryPropertyValueSet(try_set);
  const ComputedStyle* try_style = StyleForId("div");
  ASSERT_TRUE(try_style);
  EXPECT_EQ("20px", ComputedValue("left", *try_style));
  EXPECT_EQ("30px", ComputedValue("right", *try_style));
}

TEST_P(ParameterizedStyleResolverTest, TrySet_RevertLayer) {
  SetBodyInnerHTML(R"HTML(
    <style>
      div {
        position: absolute;
        left: 10px;
      }
    </style>
    <div id=div></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();

  Element* div = GetElementById("div");
  ASSERT_TRUE(div);

  // Declarations from the try set should appear in a separate layer.

  const CSSPropertyValueSet* try_set =
      css_test_helpers::ParseDeclarationBlock(R"CSS(
      left: revert-layer;
      right: 30px;
  )CSS");
  ASSERT_TRUE(try_set);

  div->EnsurePositionFallbackData().SetTryPropertyValueSet(try_set);
  const ComputedStyle* try_style = StyleForId("div");
  ASSERT_TRUE(try_style);
  EXPECT_EQ("10px", ComputedValue("left", *try_style));
  EXPECT_EQ("30px", ComputedValue("right", *try_style));
}

TEST_P(ParameterizedStyleResolverTest, TrySet_Revert) {
  SetBodyInnerHTML(R"HTML(
    <style>
      div {
        position: absolute;
        left: 10px;
      }
    </style>
    <div id=div></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();

  Element* div = GetElementById("div");
  ASSERT_TRUE(div);

  // Declarations from the try set should appear in the author origin.

  const CSSPropertyValueSet* try_set =
      css_test_helpers::ParseDeclarationBlock(R"CSS(
      left: revert;
      right: 30px;
  )CSS");
  ASSERT_TRUE(try_set);

  div->EnsurePositionFallbackData().SetTryPropertyValueSet(try_set);
  const ComputedStyle* try_style = StyleForId("div");
  ASSERT_TRUE(try_style);
  EXPECT_EQ("auto", ComputedValue("left", *try_style));
  EXPECT_EQ("30px", ComputedValue("right", *try_style));
}

TEST_P(ParameterizedStyleResolverTest, TrySet_NonAbsPos) {
  SetBodyInnerHTML(R"HTML(
    <style>
      div {
        position: static;
        left: 10px;
      }
    </style>
    <div id=div></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();

  Element* div = GetElementById("div");
  ASSERT_TRUE(div);

  // Declarations from the try set should only apply when absolutely positioned.
  // If not absolutely positioned, they should behave as 'revert-layer'.

  const CSSPropertyValueSet* try_set =
      css_test_helpers::ParseDeclarationBlock(R"CSS(
      left: 20px;
      right: 30px;
  )CSS");
  ASSERT_TRUE(try_set);

  div->EnsurePositionFallbackData().SetTryPropertyValueSet(try_set);
  const ComputedStyle* try_style = StyleForId("div");
  ASSERT_TRUE(try_style);
  EXPECT_EQ("10px", ComputedValue("left", *try_style));
  EXPECT_EQ("auto", ComputedValue("right", *try_style));
}

TEST_P(ParameterizedStyleResolverTest, TrySet_NonAbsPosDynamic) {
  SetBodyInnerHTML(R"HTML(
    <style>
      div {
        position: absolute;
        left: 10px;
      }
    </style>
    <div id=div></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();

  Element* div = GetElementById("div");
  ASSERT_TRUE(div);
  EXPECT_EQ("10px", ComputedValue("left", div->ComputedStyleRef()));
  EXPECT_EQ("auto", ComputedValue("right", div->ComputedStyleRef()));

  // Declarations from the try set should only apply when absolutely positioned,
  // including the cases where 'position' changes in the same style resolve.

  const CSSPropertyValueSet* try_set =
      css_test_helpers::ParseDeclarationBlock(R"CSS(
      left: 20px;
      right: 30px;
  )CSS");
  ASSERT_TRUE(try_set);

  div->SetInlineStyleProperty(CSSPropertyID::kPosition, "static");
  div->EnsurePositionFallbackData().SetTryPropertyValueSet(try_set);
  const ComputedStyle* try_style = StyleForId("div");
  ASSERT_TRUE(try_style);
  EXPECT_EQ("10px", ComputedValue("left", *try_style));
  EXPECT_EQ("auto", ComputedValue("right", *try_style));
}

TEST_P(ParameterizedStyleResolverTest,
       PseudoElementWithAnimationAndOriginatingElementStyleChange) {
  SetBodyInnerHTML(R"HTML(
      <style>
        div {
          width:100px;
          height:100px;
          background:red;
        }
        div:before {
          content:"blahblahblah";
          background:blue;
          transition:all 1s;
        }
        .content:before {
          content:"blahblah";
        }
        .color:before {
          background:red;
        }
      </style>
      <div class="content color" id="target"></div>
    )HTML");

  UpdateAllLifecyclePhasesForTest();

  auto* element = GetDocument().getElementById(AtomicString("target"));
  ASSERT_TRUE(element);
  auto* before = element->GetPseudoElement(kPseudoIdBefore);
  ASSERT_TRUE(before);

  // Remove the color class to start an animation.
  NonThrowableExceptionState exception_state;
  element->classList().remove({"color"}, exception_state);
  UpdateAllLifecyclePhasesForTest();
  ASSERT_TRUE(before->GetElementAnimations());

  // Trigger a style invalidation for the transition animation and remove the
  // class from the originating element. The latter should reset the animation
  // bit.
  before->SetNeedsAnimationStyleRecalc();
  EXPECT_TRUE(before->GetElementAnimations()->IsAnimationStyleChange());
  element->classList().remove({"content"}, exception_state);
  EXPECT_TRUE(element->NeedsStyleRecalc());

  // Element::RecalcOwnStyle should detect that the style change on the
  // "target" ancestor node requires re-computing the base style for the
  // pseudo element and skip the optimization for animation style change.
  UpdateAllLifecyclePhasesForTest();
}

TEST_P(StyleResolverTestCQ, ContainerUnitContext) {
  SetBodyInnerHTML(R"HTML(
    <style>
      #container, #div { container-type:size; }
      #container {
        width: 200px;
        height: 200px;
      }
      #div {
        width: 100px;
        height: 100px;
      }
    </style>
    <div id="container">
      <div id="div"></div>
    </div>
  )HTML");

  Element* div = GetDocument().getElementById(AtomicString("div"));
  ASSERT_TRUE(div);

  // Don't provide a StyleRecalcContext here.
  StyleResolverState state(GetDocument(), *div);

  // To make UpdateLengthConversionData happen.
  state.SetStyle(div->ComputedStyleRef());

  EXPECT_DOUBLE_EQ(200.0, state.CssToLengthConversionData().ContainerWidth());
  EXPECT_DOUBLE_EQ(200.0, state.CssToLengthConversionData().ContainerHeight());
}

TEST_P(ParameterizedStyleResolverTest, ScopedAnchorName) {
  GetDocument()
      .documentElement()
      ->setInnerHTMLWithDeclarativeShadowDOMForTesting(R"HTML(
    <div id="outer-anchor" style="anchor-name: --outer"></div>
    <style>#host::part(anchor) { anchor-name: --part; }</style>
    <div id="host">
      <template shadowrootmode=open>
        <style>:host { anchor-name: --host; }</style>
        <div id="part" part="anchor"></div>
        <div id="inner-anchor" style="anchor-name: --inner"></div>
      </template>
    </div>
  )HTML");

  UpdateAllLifecyclePhasesForTest();

  Element* outer_anchor = GetElementById("outer-anchor");
  Element* host = GetElementById("host");
  ShadowRoot* shadow = host->GetShadowRoot();
  Element* part = shadow->getElementById(AtomicString("part"));
  Element* inner_anchor = shadow->getElementById(AtomicString("inner-anchor"));

  EXPECT_EQ(*MakeGarbageCollected<ScopedCSSName>(AtomicString("--outer"),
                                                 &GetDocument()),
            *outer_anchor->ComputedStyleRef().AnchorName()->GetNames()[0]);
  EXPECT_EQ(
      *MakeGarbageCollected<ScopedCSSName>(AtomicString("--host"), shadow),
      *host->ComputedStyleRef().AnchorName()->GetNames()[0]);
  EXPECT_EQ(*MakeGarbageCollected<ScopedCSSName>(AtomicString("--part"),
                                                 &GetDocument()),
            *part->ComputedStyleRef().AnchorName()->GetNames()[0]);
  EXPECT_EQ(
      *MakeGarbageCollected<ScopedCSSName>(AtomicString("--inner"), shadow),
      *inner_anchor->ComputedStyleRef().AnchorName()->GetNames()[0]);
}

TEST_P(ParameterizedStyleResolverTest, ScopedAnchorDefault) {
  GetDocument()
      .documentElement()
      ->setInnerHTMLWithDeclarativeShadowDOMForTesting(R"HTML(
    <div id="outer-anchor" style="anchor-default: --outer"></div>
    <style>#host::part(anchor) { anchor-default: --part; }</style>
    <div id="host">
      <template shadowrootmode=open>
        <style>:host { anchor-default: --host; }</style>
        <div id="part" part="anchor"></div>
        <div id="inner-anchor" style="anchor-default: --inner"></div>
      </template>
    </div>
  )HTML");

  UpdateAllLifecyclePhasesForTest();

  Element* outer_anchor = GetElementById("outer-anchor");
  Element* host = GetElementById("host");
  ShadowRoot* shadow = host->GetShadowRoot();
  Element* part = shadow->getElementById(AtomicString("part"));
  Element* inner_anchor = shadow->getElementById(AtomicString("inner-anchor"));

  EXPECT_EQ(*MakeGarbageCollected<ScopedCSSName>(AtomicString("--outer"),
                                                 &GetDocument()),
            *outer_anchor->ComputedStyleRef().AnchorDefault());
  EXPECT_EQ(
      *MakeGarbageCollected<ScopedCSSName>(AtomicString("--host"), shadow),
      *host->ComputedStyleRef().AnchorDefault());
  EXPECT_EQ(*MakeGarbageCollected<ScopedCSSName>(AtomicString("--part"),
                                                 &GetDocument()),
            *part->ComputedStyleRef().AnchorDefault());
  EXPECT_EQ(
      *MakeGarbageCollected<ScopedCSSName>(AtomicString("--inner"), shadow),
      *inner_anchor->ComputedStyleRef().AnchorDefault());
}

// |length| must be a calculated value of a single anchor query node.
static const TreeScope* GetAnchorQueryTreeScope(const Length& length) {
  DCHECK(length.IsCalculated());
  DCHECK(length.GetCalculationValue().IsExpression());
  const auto& query = To<CalculationExpressionAnchorQueryNode>(
      *length.GetCalculationValue().GetOrCreateExpression());
  return query.AnchorSpecifier().IsNamed()
             ? query.AnchorSpecifier().GetName().GetTreeScope()
             : nullptr;
}

TEST_P(ParameterizedStyleResolverTest, ScopedAnchorFunction) {
  GetDocument()
      .documentElement()
      ->setInnerHTMLWithDeclarativeShadowDOMForTesting(R"HTML(
    <style>
      div { position: absolute; }
      #left { left: anchor(--a left); }
      #bottom::part(right) { right: anchor(--a right); }
    </style>
    <div id="left"></div>
    <div id="bottom">
      <template shadowrootmode=open>
        <style>
          div { position: absolute; }
          #top { top: anchor(--a top); }
          :host { bottom: anchor(--a bottom); }
        </style>
        <div id="top"></div>
        <div id="right" part="right"></div>
      </template>
    </div>

    <style>
      #inline-start { inset-inline-start: anchor(--a left); }
      #block-end::part(inline-end) { inset-inline-end: anchor(--a right); }
    </style>
    <div id="inline-start"></div>
    <div id="block-end">
      <template shadowrootmode=open>
        <style>
          div { position: absolute }
          :host { inset-block-end: anchor(--a bottom); }
          #block-start { inset-block-start: anchor(--a top); }
        </style>
        <div id="block-start"></div>
        <div id="inline-end" part="inline-end"></div>
      </template>
    </div>
  )HTML");

  UpdateAllLifecyclePhasesForTest();

  {
    Element* left = GetElementById("left");
    Element* bottom = GetElementById("bottom");
    ShadowRoot* shadow = bottom->GetShadowRoot();
    Element* top = shadow->getElementById(AtomicString("top"));
    Element* right = shadow->getElementById(AtomicString("right"));

    EXPECT_EQ(&GetDocument(),
              GetAnchorQueryTreeScope(GetLeft(left->ComputedStyleRef())));
    EXPECT_EQ(&GetDocument(),
              GetAnchorQueryTreeScope(GetRight(right->ComputedStyleRef())));
    EXPECT_EQ(shadow, GetAnchorQueryTreeScope(GetTop(top->ComputedStyleRef())));
    EXPECT_EQ(shadow,
              GetAnchorQueryTreeScope(GetBottom(bottom->ComputedStyleRef())));
  }

  {
    // Verify that it also works for logical properties.
    Element* inline_start = GetElementById("inline-start");
    Element* block_end = GetElementById("block-end");
    ShadowRoot* shadow = block_end->GetShadowRoot();
    Element* block_start = shadow->getElementById(AtomicString("block-start"));
    Element* inline_end = shadow->getElementById(AtomicString("inline-end"));

    EXPECT_EQ(&GetDocument(), GetAnchorQueryTreeScope(
                                  GetLeft(inline_start->ComputedStyleRef())));
    EXPECT_EQ(&GetDocument(), GetAnchorQueryTreeScope(
                                  GetRight(inline_end->ComputedStyleRef())));
    EXPECT_EQ(shadow,
              GetAnchorQueryTreeScope(GetTop(block_start->ComputedStyleRef())));
    EXPECT_EQ(shadow, GetAnchorQueryTreeScope(
                          GetBottom(block_end->ComputedStyleRef())));
  }
}

TEST_P(ParameterizedStyleResolverTest, ScopedAnchorSizeFunction) {
  GetDocument()
      .documentElement()
      ->setInnerHTMLWithDeclarativeShadowDOMForTesting(R"HTML(
    <style>
      div { position: absolute; }
      #width { width: anchor-size(--a width); }
    </style>
    <div id="width">
      <template shadowrootmode=open>
        <style>
          div { position: absolute; }
          #height { height: anchor-size(--a height); }
        </style>
        <div id="height"></div>
      </template>
    </div>

    <style>
      #min-width { min-width: anchor-size(--a width); }
      #max-width::part(max-height) { max-height: anchor-size(--a height); }
    </style>
    <div id="min-width"></div>
    <div id="max-width">
      <template shadowrootmode=open>
        <style>
          div { position: absolute; }
          #min-height { min-height: anchor-size(--a height); }
          :host { max-width: anchor-size(--a width); }
        </style>
        <div id="min-height"></div>
        <div id="max-height" part="max-height"></div>
      </template>
    </div>
  )HTML");

  UpdateAllLifecyclePhasesForTest();

  Element* width = GetElementById("width");
  Element* min_width = GetElementById("min-width");
  Element* max_width = GetElementById("max-width");
  ShadowRoot* shadow1 = width->GetShadowRoot();
  ShadowRoot* shadow2 = max_width->GetShadowRoot();
  Element* height = shadow1->getElementById(AtomicString("height"));
  Element* min_height = shadow2->getElementById(AtomicString("min-height"));
  Element* max_height = shadow2->getElementById(AtomicString("max-height"));

  EXPECT_EQ(&GetDocument(),
            GetAnchorQueryTreeScope(GetWidth(width->ComputedStyleRef())));
  EXPECT_EQ(shadow1,
            GetAnchorQueryTreeScope(GetHeight(height->ComputedStyleRef())));
  EXPECT_EQ(&GetDocument(), GetAnchorQueryTreeScope(
                                GetMinWidth(min_width->ComputedStyleRef())));
  EXPECT_EQ(shadow2, GetAnchorQueryTreeScope(
                         GetMaxWidth(max_width->ComputedStyleRef())));
  EXPECT_EQ(shadow2, GetAnchorQueryTreeScope(
                         GetMinHeight(min_height->ComputedStyleRef())));
  EXPECT_EQ(&GetDocument(), GetAnchorQueryTreeScope(
                                GetMaxHeight(max_height->ComputedStyleRef())));
}

TEST_P(StyleResolverTestCQ, CanAffectAnimationsMPC) {
  GetDocument().documentElement()->setInnerHTML(R"HTML(
    <style>
      #a { transition: color 1s; }
      @container (width > 100000px) {
        #b { animation-name: anim; }
      }
    </style>
    <div id=a></div>
    <div id=b></div>
    <div id=c></div>
  )HTML");

  UpdateAllLifecyclePhasesForTest();

  auto* a = GetDocument().getElementById(AtomicString("a"));
  auto* b = GetDocument().getElementById(AtomicString("b"));
  auto* c = GetDocument().getElementById(AtomicString("c"));

  ASSERT_TRUE(a);
  ASSERT_TRUE(b);
  ASSERT_TRUE(c);

  EXPECT_TRUE(a->ComputedStyleRef().CanAffectAnimations());
  EXPECT_FALSE(b->ComputedStyleRef().CanAffectAnimations());
  EXPECT_FALSE(c->ComputedStyleRef().CanAffectAnimations());
}

TEST_P(ParameterizedStyleResolverTest, CssRulesForElementExcludeStartingStyle) {
  SetBodyInnerHTML(R"HTML(
    <style>
      @starting-style {
        #target {
          color: red;
        }
      }
    </style>
    <div id="wrapper" hidden>
      <span id="target"></span>
    </div>
  )HTML");

  Element* target = GetDocument().getElementById(AtomicString("target"));
  EXPECT_EQ(target->GetComputedStyle(), nullptr);
  EXPECT_EQ(GetStyleEngine().GetStyleResolver().CssRulesForElement(target),
            nullptr);

  GetElementById("wrapper")->removeAttribute(html_names::kHiddenAttr);
  UpdateAllLifecyclePhasesForTest();

  EXPECT_NE(target->GetComputedStyle(), nullptr);
  EXPECT_EQ(GetStyleEngine().GetStyleResolver().CssRulesForElement(target),
            nullptr);
}

TEST_P(ParameterizedStyleResolverTest,
       PseudoCSSRulesForElementExcludeStartingStyle) {
  SetBodyInnerHTML(R"HTML(
    <style>
      @starting-style {
        #target::before {
          color: red;
        }
      }
      #target::before {
        content: "X";
        color: green;
      }
    </style>
    <div id="wrapper" hidden>
      <span id="target"></span>
    </div>
  )HTML");

  Element* target = GetDocument().getElementById(AtomicString("target"));
  EXPECT_EQ(target->GetComputedStyle(), nullptr);
  EXPECT_EQ(target->GetPseudoElement(kPseudoIdBefore), nullptr);

  RuleIndexList* pseudo_rules =
      GetStyleEngine().GetStyleResolver().PseudoCSSRulesForElement(
          target, kPseudoIdBefore, g_null_atom);
  ASSERT_NE(pseudo_rules, nullptr);
  EXPECT_EQ(pseudo_rules->size(), 1u);

  GetElementById("wrapper")->removeAttribute(html_names::kHiddenAttr);
  UpdateAllLifecyclePhasesForTest();

  EXPECT_NE(target->GetComputedStyle(), nullptr);
  EXPECT_NE(target->GetPseudoElement(kPseudoIdBefore), nullptr);

  pseudo_rules = GetStyleEngine().GetStyleResolver().PseudoCSSRulesForElement(
      target, kPseudoIdBefore, g_null_atom);
  ASSERT_NE(pseudo_rules, nullptr);
  EXPECT_EQ(pseudo_rules->size(), 1u);
  EXPECT_EQ(pseudo_rules->at(0).first->cssText(),
            "#target::before { content: \"X\"; color: green; }");
}

TEST_P(ParameterizedStyleResolverTest, ResizeAutoInUANotCounted) {
  SetBodyInnerHTML(R"HTML(<textarea></textarea>)HTML");
  EXPECT_FALSE(IsUseCounted(WebFeature::kCSSResizeAuto))
      << "resize:auto UA rule for textarea should not be counted";
}

TEST_P(ParameterizedStyleResolverTest, ResizeAutoCounted) {
  SetBodyInnerHTML(R"HTML(
    <style>
      #resize {
        width: 100px;
        height: 100px;
        overflow: scroll;
        resize: auto;
      }
    </style>
    <div id="resize"></div>
  )HTML");
  EXPECT_TRUE(IsUseCounted(WebFeature::kCSSResizeAuto))
      << "Author style resize:auto applied to div should be counted";
}

}  // namespace blink
