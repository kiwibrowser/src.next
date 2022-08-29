// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/css/container_query_evaluator.h"

#include "third_party/blink/renderer/core/css/container_query.h"
#include "third_party/blink/renderer/core/css/css_container_rule.h"
#include "third_party/blink/renderer/core/css/css_custom_property_declaration.h"
#include "third_party/blink/renderer/core/css/css_test_helpers.h"
#include "third_party/blink/renderer/core/css/parser/css_parser_context.h"
#include "third_party/blink/renderer/core/css/parser/css_parser_impl.h"
#include "third_party/blink/renderer/core/css/parser/css_parser_token_stream.h"
#include "third_party/blink/renderer/core/css/parser/css_tokenizer.h"
#include "third_party/blink/renderer/core/css/parser/css_variable_parser.h"
#include "third_party/blink/renderer/core/css/resolver/match_result.h"
#include "third_party/blink/renderer/core/css/resolver/style_resolver.h"
#include "third_party/blink/renderer/core/css/style_engine.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/dom/dom_token_list.h"
#include "third_party/blink/renderer/core/dom/element.h"
#include "third_party/blink/renderer/core/dom/node_computed_style.h"
#include "third_party/blink/renderer/core/execution_context/security_context.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/html/html_div_element.h"
#include "third_party/blink/renderer/core/style/computed_style.h"
#include "third_party/blink/renderer/core/testing/page_test_base.h"
#include "third_party/blink/renderer/platform/testing/runtime_enabled_features_test_helpers.h"

namespace blink {

class ContainerQueryEvaluatorTest : public PageTestBase,
                                    private ScopedCSSContainerQueriesForTest,
                                    private ScopedLayoutNGForTest {
 public:
  ContainerQueryEvaluatorTest()
      : ScopedCSSContainerQueriesForTest(true), ScopedLayoutNGForTest(true) {}

  ContainerQuery* ParseContainer(String query) {
    String rule = "@container " + query + " {}";
    auto* style_rule = DynamicTo<StyleRuleContainer>(
        css_test_helpers::ParseRule(GetDocument(), rule));
    if (!style_rule)
      return nullptr;
    return &style_rule->GetContainerQuery();
  }

  class TemporaryContainerElement {
    STACK_ALLOCATED();

   public:
    explicit TemporaryContainerElement(
        Document& document,
        scoped_refptr<const ComputedStyle> style) {
      element = MakeGarbageCollected<HTMLDivElement>(document);
      document.body()->AppendChild(element);
      element->SetComputedStyle(style);
    }

    ~TemporaryContainerElement() { element->remove(); }

    Element* element;
  };

  bool Eval(String query,
            double width,
            double height,
            unsigned container_type,
            PhysicalAxes contained_axes) {
    auto style = ComputedStyle::Clone(GetDocument().ComputedStyleRef());
    style->SetContainerType(container_type);

    TemporaryContainerElement temp_container(GetDocument(), style);

    ContainerQuery* container_query = ParseContainer(query);
    DCHECK(container_query);
    auto* evaluator = MakeGarbageCollected<ContainerQueryEvaluator>();
    evaluator->ContainerChanged(
        GetDocument(), *temp_container.element,
        PhysicalSize(LayoutUnit(width), LayoutUnit(height)), contained_axes);
    return evaluator->Eval(*container_query);
  }

  bool Eval(String query,
            String custom_property_name,
            String custom_property_value) {
    CSSTokenizer tokenizer(custom_property_value);
    CSSParserTokenStream stream(tokenizer);
    CSSTokenizedValue tokenized_value = CSSParserImpl::ConsumeValue(stream);
    const CSSParserContext* context =
        StrictCSSParserContext(SecureContextMode::kSecureContext);
    CSSCustomPropertyDeclaration* value =
        CSSVariableParser::ParseDeclarationValue(tokenized_value, false,
                                                 *context);
    DCHECK(value);

    scoped_refptr<ComputedStyle> style =
        GetDocument().GetStyleResolver().InitialStyleForElement();
    style->SetVariableData(AtomicString(custom_property_name), value->Value(),
                           false);

    TemporaryContainerElement temp_container(GetDocument(), style);

    auto* evaluator = MakeGarbageCollected<ContainerQueryEvaluator>();
    evaluator->ContainerChanged(GetDocument(), *temp_container.element,
                                PhysicalSize(LayoutUnit(100), LayoutUnit(100)),
                                PhysicalAxes{kPhysicalAxisNone});

    ContainerQuery* container_query = ParseContainer(query);
    return evaluator->Eval(*container_query);
  }

  using Change = ContainerQueryEvaluator::Change;

  Change ContainerChanged(ContainerQueryEvaluator* evaluator,
                          PhysicalSize size,
                          unsigned container_type,
                          PhysicalAxes axes) {
    auto style = ComputedStyle::Clone(GetDocument().ComputedStyleRef());
    style->SetContainerType(container_type);

    TemporaryContainerElement temp_container(GetDocument(), style);

    return evaluator->ContainerChanged(GetDocument(), *temp_container.element,
                                       size, axes);
  }

  bool EvalAndAdd(ContainerQueryEvaluator* evaluator,
                  const ContainerQuery& query,
                  Change change = Change::kNearestContainer) {
    MatchResult dummy_result;
    return evaluator->EvalAndAdd(query, change, dummy_result);
  }

  using Result = ContainerQueryEvaluator::Result;
  const HeapHashMap<Member<const ContainerQuery>, Result>& GetResults(
      ContainerQueryEvaluator* evaluator) const {
    return evaluator->results_;
  }

  unsigned GetUnitFlags(ContainerQueryEvaluator* evaluator) const {
    return evaluator->unit_flags_;
  }

  void ClearResults(ContainerQueryEvaluator* evaluator, Change change) const {
    return evaluator->ClearResults(change);
  }

  const PhysicalAxes none{kPhysicalAxisNone};
  const PhysicalAxes both{kPhysicalAxisBoth};
  const PhysicalAxes horizontal{kPhysicalAxisHorizontal};
  const PhysicalAxes vertical{kPhysicalAxisVertical};

  const unsigned type_normal = kContainerTypeNormal;
  const unsigned type_size = kContainerTypeSize;
  const unsigned type_inline_size = kContainerTypeInlineSize;
};

TEST_F(ContainerQueryEvaluatorTest, ContainmentMatch) {
  {
    String query = "(min-width: 100px)";
    EXPECT_TRUE(Eval(query, 100.0, 100.0, type_size, horizontal));
    EXPECT_TRUE(Eval(query, 100.0, 100.0, type_size, both));
    EXPECT_TRUE(Eval(query, 100.0, 100.0, type_inline_size, horizontal));
    EXPECT_TRUE(Eval(query, 100.0, 100.0, type_inline_size, both));
    EXPECT_FALSE(Eval(query, 100.0, 100.0, type_size, vertical));
    EXPECT_FALSE(Eval(query, 100.0, 100.0, type_size, none));
    EXPECT_FALSE(Eval(query, 99.0, 100.0, type_size, horizontal));
    EXPECT_FALSE(Eval(query, 100.0, 100.0, type_normal, both));
  }

  {
    String query = "(min-height: 100px)";
    EXPECT_TRUE(Eval(query, 100.0, 100.0, type_size, vertical));
    EXPECT_TRUE(Eval(query, 100.0, 100.0, type_size, both));
    EXPECT_FALSE(Eval(query, 100.0, 100.0, type_size, horizontal));
    EXPECT_FALSE(Eval(query, 100.0, 100.0, type_size, none));
    EXPECT_FALSE(Eval(query, 100.0, 99.0, type_size, vertical));
    EXPECT_FALSE(Eval(query, 100.0, 100.0, type_normal, both));
    EXPECT_FALSE(Eval(query, 100.0, 100.0, type_inline_size, both));
  }

  {
    String query = "((min-width: 100px) and (min-height: 100px))";
    EXPECT_TRUE(Eval(query, 100.0, 100.0, type_size, both));
    EXPECT_FALSE(Eval(query, 100.0, 100.0, type_size, vertical));
    EXPECT_FALSE(Eval(query, 100.0, 100.0, type_size, horizontal));
    EXPECT_FALSE(Eval(query, 100.0, 100.0, type_size, none));
    EXPECT_FALSE(Eval(query, 100.0, 99.0, type_size, both));
    EXPECT_FALSE(Eval(query, 99.0, 100.0, type_size, both));
    EXPECT_FALSE(Eval(query, 100.0, 100.0, type_normal, both));
    EXPECT_FALSE(Eval(query, 100.0, 100.0, type_inline_size, both));
  }
}

TEST_F(ContainerQueryEvaluatorTest, ContainerChanged) {
  PhysicalSize size_50(LayoutUnit(50), LayoutUnit(50));
  PhysicalSize size_100(LayoutUnit(100), LayoutUnit(100));
  PhysicalSize size_200(LayoutUnit(200), LayoutUnit(200));

  ContainerQuery* container_query_50 = ParseContainer("(min-width: 50px)");
  ContainerQuery* container_query_100 = ParseContainer("(min-width: 100px)");
  ContainerQuery* container_query_200 = ParseContainer("(min-width: 200px)");
  ASSERT_TRUE(container_query_50);
  ASSERT_TRUE(container_query_100);
  ASSERT_TRUE(container_query_200);

  auto* evaluator = MakeGarbageCollected<ContainerQueryEvaluator>();
  ContainerChanged(evaluator, size_100, type_size, horizontal);

  EXPECT_TRUE(EvalAndAdd(evaluator, *container_query_100));
  EXPECT_FALSE(EvalAndAdd(evaluator, *container_query_200));
  EXPECT_EQ(2u, GetResults(evaluator).size());

  // Calling ContainerChanged the values we already have should not produce
  // a Change.
  EXPECT_EQ(Change::kNone,
            ContainerChanged(evaluator, size_100, type_size, horizontal));
  EXPECT_EQ(2u, GetResults(evaluator).size());

  // EvalAndAdding the same queries again is allowed.
  EXPECT_TRUE(EvalAndAdd(evaluator, *container_query_100));
  EXPECT_FALSE(EvalAndAdd(evaluator, *container_query_200));
  EXPECT_EQ(2u, GetResults(evaluator).size());

  // Resize from 100px to 200px.
  EXPECT_EQ(Change::kNearestContainer,
            ContainerChanged(evaluator, size_200, type_size, horizontal));
  EXPECT_EQ(0u, GetResults(evaluator).size());

  // Now both 100px and 200px queries should return true.
  EXPECT_TRUE(EvalAndAdd(evaluator, *container_query_100));
  EXPECT_TRUE(EvalAndAdd(evaluator, *container_query_200));
  EXPECT_EQ(2u, GetResults(evaluator).size());

  // Calling ContainerChanged the values we already have should not produce
  // a Change.
  EXPECT_EQ(Change::kNone,
            ContainerChanged(evaluator, size_200, type_size, horizontal));
  EXPECT_EQ(2u, GetResults(evaluator).size());

  // Still valid to EvalAndAdd the same queries again.
  EXPECT_TRUE(EvalAndAdd(evaluator, *container_query_100));
  EXPECT_TRUE(EvalAndAdd(evaluator, *container_query_200));
  EXPECT_EQ(2u, GetResults(evaluator).size());

  // Setting contained_axes=vertical should invalidate the queries, since
  // they query width.
  EXPECT_EQ(Change::kNearestContainer,
            ContainerChanged(evaluator, size_200, type_size, vertical));
  EXPECT_EQ(0u, GetResults(evaluator).size());

  EXPECT_FALSE(EvalAndAdd(evaluator, *container_query_100));
  EXPECT_FALSE(EvalAndAdd(evaluator, *container_query_200));
  EXPECT_EQ(2u, GetResults(evaluator).size());

  // Switching back to horizontal.
  EXPECT_EQ(Change::kNearestContainer,
            ContainerChanged(evaluator, size_100, type_size, horizontal));
  EXPECT_EQ(0u, GetResults(evaluator).size());

  // Resize to 200px.
  EXPECT_EQ(Change::kNone,
            ContainerChanged(evaluator, size_200, type_size, horizontal));
  EXPECT_EQ(0u, GetResults(evaluator).size());

  // Add a query of each Change type.
  EXPECT_TRUE(
      EvalAndAdd(evaluator, *container_query_100, Change::kNearestContainer));
  EXPECT_TRUE(EvalAndAdd(evaluator, *container_query_200,
                         Change::kDescendantContainers));
  EXPECT_EQ(2u, GetResults(evaluator).size());

  // Resize to 50px should cause both queries to change their evaluation.
  // `ContainerChanged` should return the biggest `Change`.
  EXPECT_EQ(Change::kDescendantContainers,
            ContainerChanged(evaluator, size_50, type_size, horizontal));
}

TEST_F(ContainerQueryEvaluatorTest, ClearResults) {
  PhysicalSize size_100(LayoutUnit(100), LayoutUnit(100));

  ContainerQuery* container_query_px = ParseContainer("(min-width: 50px)");
  ContainerQuery* container_query_em = ParseContainer("(min-width: 10em)");
  ContainerQuery* container_query_vh = ParseContainer("(min-width: 10vh)");
  ContainerQuery* container_query_cqw = ParseContainer("(min-width: 10cqw)");
  ASSERT_TRUE(container_query_px);
  ASSERT_TRUE(container_query_em);
  ASSERT_TRUE(container_query_vh);
  ASSERT_TRUE(container_query_cqw);

  auto* evaluator = MakeGarbageCollected<ContainerQueryEvaluator>();
  ContainerChanged(evaluator, size_100, type_size, horizontal);

  EXPECT_EQ(0u, GetResults(evaluator).size());

  using UnitFlags = MediaQueryExpValue::UnitFlags;

  // EvalAndAdd (min-width: 50px), nearest.
  EvalAndAdd(evaluator, *container_query_px, Change::kNearestContainer);
  ASSERT_EQ(1u, GetResults(evaluator).size());
  EXPECT_EQ(Change::kNearestContainer,
            GetResults(evaluator).at(container_query_px).change);
  EXPECT_EQ(UnitFlags::kNone,
            GetResults(evaluator).at(container_query_px).unit_flags);
  EXPECT_EQ(UnitFlags::kNone, GetUnitFlags(evaluator));

  // EvalAndAdd (min-width: 10em), descendant
  EvalAndAdd(evaluator, *container_query_em, Change::kDescendantContainers);
  ASSERT_EQ(2u, GetResults(evaluator).size());
  EXPECT_EQ(Change::kDescendantContainers,
            GetResults(evaluator).at(container_query_em).change);
  EXPECT_EQ(UnitFlags::kFontRelative,
            GetResults(evaluator).at(container_query_em).unit_flags);
  EXPECT_EQ(UnitFlags::kFontRelative, GetUnitFlags(evaluator));

  // EvalAndAdd (min-width: 10vh), nearest
  EvalAndAdd(evaluator, *container_query_vh, Change::kNearestContainer);
  ASSERT_EQ(3u, GetResults(evaluator).size());
  EXPECT_EQ(Change::kNearestContainer,
            GetResults(evaluator).at(container_query_vh).change);
  EXPECT_EQ(UnitFlags::kStaticViewport,
            GetResults(evaluator).at(container_query_vh).unit_flags);
  EXPECT_EQ(static_cast<unsigned>(UnitFlags::kFontRelative |
                                  UnitFlags::kStaticViewport),
            GetUnitFlags(evaluator));

  // EvalAndAdd (min-width: 10cqw), descendant
  EvalAndAdd(evaluator, *container_query_cqw, Change::kDescendantContainers);
  ASSERT_EQ(4u, GetResults(evaluator).size());
  EXPECT_EQ(Change::kDescendantContainers,
            GetResults(evaluator).at(container_query_cqw).change);
  EXPECT_EQ(UnitFlags::kContainer,
            GetResults(evaluator).at(container_query_cqw).unit_flags);
  EXPECT_EQ(
      static_cast<unsigned>(UnitFlags::kFontRelative |
                            UnitFlags::kStaticViewport | UnitFlags::kContainer),
      GetUnitFlags(evaluator));

  // Clearing kNearestContainer should leave all information originating from
  // kDescendantContainers.
  ClearResults(evaluator, Change::kNearestContainer);
  ASSERT_EQ(2u, GetResults(evaluator).size());
  EXPECT_EQ(Change::kDescendantContainers,
            GetResults(evaluator).at(container_query_em).change);
  EXPECT_EQ(Change::kDescendantContainers,
            GetResults(evaluator).at(container_query_cqw).change);
  EXPECT_EQ(UnitFlags::kFontRelative,
            GetResults(evaluator).at(container_query_em).unit_flags);
  EXPECT_EQ(UnitFlags::kContainer,
            GetResults(evaluator).at(container_query_cqw).unit_flags);
  EXPECT_EQ(
      static_cast<unsigned>(UnitFlags::kFontRelative | UnitFlags::kContainer),
      GetUnitFlags(evaluator));

  // Clearing Change::kDescendantContainers should clear everything.
  ClearResults(evaluator, Change::kDescendantContainers);
  ASSERT_EQ(0u, GetResults(evaluator).size());
  EXPECT_EQ(UnitFlags::kNone, GetUnitFlags(evaluator));

  // Add everything again, to ensure that
  // ClearResults(Change::kDescendantContainers) also clears
  // Change::kNearestContainer.
  EvalAndAdd(evaluator, *container_query_px, Change::kNearestContainer);
  EvalAndAdd(evaluator, *container_query_em, Change::kDescendantContainers);
  EvalAndAdd(evaluator, *container_query_vh, Change::kNearestContainer);
  EvalAndAdd(evaluator, *container_query_cqw, Change::kDescendantContainers);
  ASSERT_EQ(4u, GetResults(evaluator).size());
  EXPECT_EQ(
      static_cast<unsigned>(UnitFlags::kFontRelative |
                            UnitFlags::kStaticViewport | UnitFlags::kContainer),
      GetUnitFlags(evaluator));
  ClearResults(evaluator, Change::kDescendantContainers);
  ASSERT_EQ(0u, GetResults(evaluator).size());
  EXPECT_EQ(UnitFlags::kNone, GetUnitFlags(evaluator));
}

TEST_F(ContainerQueryEvaluatorTest, SizeInvalidation) {
  SetBodyInnerHTML(R"HTML(
    <style>
      #container {
        container-type: size;
        width: 500px;
        height: 500px;
      }
      @container (min-width: 500px) {
        div { z-index:1; }
      }
    </style>
    <div id=container>
      <div id=div></div>
      <div id=div></div>
      <div id=div></div>
      <div id=div></div>
      <div id=div></div>
      <div id=div></div>
    </div>
  )HTML");

  Element* container = GetDocument().getElementById("container");
  ASSERT_TRUE(container);
  ASSERT_TRUE(container->GetContainerQueryEvaluator());

  {
    // Causes re-layout, but the size does not change
    container->SetInlineStyleProperty(CSSPropertyID::kFloat, "left");

    unsigned before_count = GetStyleEngine().StyleForElementCount();

    UpdateAllLifecyclePhasesForTest();

    unsigned after_count = GetStyleEngine().StyleForElementCount();

    // Only #container should be affected. In particular, we should not
    // recalc any style for <div> children of #container.
    EXPECT_EQ(1u, after_count - before_count);
  }

  {
    // The size of the container changes, but it does not matter for
    // the result of the query (min-width: 500px).
    container->SetInlineStyleProperty(CSSPropertyID::kWidth, "600px");

    unsigned before_count = GetStyleEngine().StyleForElementCount();

    UpdateAllLifecyclePhasesForTest();

    unsigned after_count = GetStyleEngine().StyleForElementCount();

    // Only #container should be affected. In particular, we should not
    // recalc any style for <div> children of #container.
    EXPECT_EQ(1u, after_count - before_count);
  }
}

TEST_F(ContainerQueryEvaluatorTest, DependentQueries) {
  PhysicalSize size_100(LayoutUnit(100), LayoutUnit(100));
  PhysicalSize size_150(LayoutUnit(150), LayoutUnit(150));
  PhysicalSize size_200(LayoutUnit(200), LayoutUnit(200));
  PhysicalSize size_300(LayoutUnit(300), LayoutUnit(300));
  PhysicalSize size_400(LayoutUnit(400), LayoutUnit(400));

  ContainerQuery* query_min_200px = ParseContainer("(min-width: 200px)");
  ContainerQuery* query_max_300px = ParseContainer("(max-width: 300px)");
  ASSERT_TRUE(query_min_200px);

  auto* evaluator = MakeGarbageCollected<ContainerQueryEvaluator>();
  ContainerChanged(evaluator, size_100, type_size, horizontal);

  EvalAndAdd(evaluator, *query_min_200px);
  EvalAndAdd(evaluator, *query_max_300px);
  // Updating with the same size as we initially had should not invalidate
  // any query results.
  EXPECT_EQ(Change::kNone,
            ContainerChanged(evaluator, size_100, type_size, horizontal));

  // Makes no difference for either of (min-width: 200px), (max-width: 300px):
  EXPECT_EQ(Change::kNone,
            ContainerChanged(evaluator, size_150, type_size, horizontal));

  // (min-width: 200px) becomes true:
  EXPECT_EQ(Change::kNearestContainer,
            ContainerChanged(evaluator, size_200, type_size, horizontal));

  EvalAndAdd(evaluator, *query_min_200px);
  EvalAndAdd(evaluator, *query_max_300px);
  EXPECT_EQ(Change::kNone,
            ContainerChanged(evaluator, size_200, type_size, horizontal));

  // Makes no difference for either of (min-width: 200px), (max-width: 300px):
  EXPECT_EQ(Change::kNone,
            ContainerChanged(evaluator, size_300, type_size, horizontal));

  // (max-width: 300px) becomes false:
  EXPECT_EQ(Change::kNearestContainer,
            ContainerChanged(evaluator, size_400, type_size, horizontal));
}

TEST_F(ContainerQueryEvaluatorTest, EvaluatorDisplayNone) {
  SetBodyInnerHTML(R"HTML(
    <style>
      main {
        display: block;
        container-type: size;
        width: 500px;
        height: 500px;
      }
      main.none {
        display: none;
      }
      @container (min-width: 500px) {
        div { --x:test; }
      }
    </style>
    <main id=outer>
      <div>
        <main id=inner>
          <div></div>
        </main>
      </div>
    </main>
  )HTML");

  // Inner container
  Element* inner = GetDocument().getElementById("inner");
  ASSERT_TRUE(inner);
  EXPECT_TRUE(inner->GetContainerQueryEvaluator());

  inner->classList().Add("none");
  UpdateAllLifecyclePhasesForTest();
  EXPECT_FALSE(inner->GetContainerQueryEvaluator());

  inner->classList().Remove("none");
  UpdateAllLifecyclePhasesForTest();
  ASSERT_TRUE(inner->GetContainerQueryEvaluator());

  // Outer container
  Element* outer = GetDocument().getElementById("outer");
  ASSERT_TRUE(outer);
  EXPECT_TRUE(outer->GetContainerQueryEvaluator());
  EXPECT_TRUE(inner->GetContainerQueryEvaluator());

  outer->classList().Add("none");
  UpdateAllLifecyclePhasesForTest();
  EXPECT_FALSE(outer->GetContainerQueryEvaluator());
  EXPECT_FALSE(inner->GetContainerQueryEvaluator());

  outer->classList().Remove("none");
  UpdateAllLifecyclePhasesForTest();
  EXPECT_TRUE(outer->GetContainerQueryEvaluator());
  EXPECT_TRUE(inner->GetContainerQueryEvaluator());
}

TEST_F(ContainerQueryEvaluatorTest, LegacyPrinting) {
  ScopedLayoutNGPrintingForTest legacy_print(false);

  SetBodyInnerHTML(R"HTML(
    <style>
      #container {
        container-type: size;
        width: 100px;
        height: 100px;
      }
      @container (width >= 0px) {
        #inner { z-index: 1; }
      }
    </style>
    <div id="container">
      <div id="inner"></div>
    </div>
  )HTML");

  Element* inner = GetDocument().getElementById("inner");
  ASSERT_TRUE(inner);

  EXPECT_EQ(inner->ComputedStyleRef().ZIndex(), 1);

  constexpr gfx::SizeF initial_page_size(800, 600);

  GetDocument().GetFrame()->StartPrinting(initial_page_size, initial_page_size);
  GetDocument().View()->UpdateLifecyclePhasesForPrinting();

  EXPECT_EQ(inner->ComputedStyleRef().ZIndex(), 0);

  GetDocument().GetFrame()->EndPrinting();
  UpdateAllLifecyclePhasesForTest();

  EXPECT_EQ(inner->ComputedStyleRef().ZIndex(), 1);
}

TEST_F(ContainerQueryEvaluatorTest, Printing) {
  ScopedLayoutNGPrintingForTest ng_printing_scope(true);

  SetBodyInnerHTML(R"HTML(
    <style>
      @page { size: 400px 400px; }
      body { margin: 0; }
      #container {
        container-type: size;
        width: 50vw;
      }

      @container (width = 200px) {
        #target { color: green; }
      }
    </style>
    <div id="container">
      <span id="target"></span>
    </div>
  )HTML");

  UpdateAllLifecyclePhasesForTest();
  Element* target = GetDocument().getElementById("target");
  EXPECT_NE(
      target->ComputedStyleRef().VisitedDependentColor(GetCSSPropertyColor()),
      Color(0, 128, 0));

  constexpr gfx::SizeF initial_page_size(400, 400);
  GetDocument().GetFrame()->StartPrinting(initial_page_size, initial_page_size);
  GetDocument().View()->UpdateLifecyclePhasesForPrinting();

  EXPECT_EQ(
      target->ComputedStyleRef().VisitedDependentColor(GetCSSPropertyColor()),
      Color(0, 128, 0));
}

TEST_F(ContainerQueryEvaluatorTest, CustomPropertyStyleQuery) {
  EXPECT_FALSE(Eval("style(--my-prop)", "--my-prop", "10px"));
  EXPECT_FALSE(Eval("style(--my-prop:)", "--my-prop", "10px"));
  EXPECT_FALSE(Eval("style(--my-prop: )", "--my-prop", "10px"));

  EXPECT_FALSE(Eval("style(--my-prop)", "--my-prop", ""));
  EXPECT_TRUE(Eval("style(--my-prop:)", "--my-prop", ""));
  EXPECT_TRUE(Eval("style(--my-prop: )", "--my-prop", ""));

  EXPECT_FALSE(Eval("style(--my-prop)", "--my-prop", " "));
  EXPECT_FALSE(Eval("style(--my-prop:)", "--my-prop", " "));
  EXPECT_FALSE(Eval("style(--my-prop: )", "--my-prop", " "));

  EXPECT_TRUE(Eval("style(--my-prop:10px)", "--my-prop", "10px"));
  EXPECT_TRUE(Eval("style(--my-prop: 10px)", "--my-prop", "10px"));
  EXPECT_FALSE(Eval("style(--my-prop:10px )", "--my-prop", "10px"));
  EXPECT_FALSE(Eval("style(--my-prop:10px)", "--my-prop", "10px "));
  EXPECT_FALSE(Eval("style(--my-prop: 10px)", "--my-prop", "10px "));
  EXPECT_TRUE(Eval("style(--my-prop:10px )", "--my-prop", "10px "));
  EXPECT_TRUE(Eval("style(--my-prop: 10px )", "--my-prop", "10px "));
}

}  // namespace blink
