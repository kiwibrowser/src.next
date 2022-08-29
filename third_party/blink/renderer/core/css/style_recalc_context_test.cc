// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/css/style_recalc_context.h"

#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/dom/element.h"
#include "third_party/blink/renderer/core/dom/pseudo_element.h"
#include "third_party/blink/renderer/core/dom/shadow_root.h"
#include "third_party/blink/renderer/core/html/html_element.h"
#include "third_party/blink/renderer/core/testing/page_test_base.h"
#include "third_party/blink/renderer/platform/testing/runtime_enabled_features_test_helpers.h"

namespace blink {

class StyleRecalcContextTest : public PageTestBase,
                               private ScopedCSSContainerQueriesForTest,
                               private ScopedLayoutNGForTest {
 public:
  StyleRecalcContextTest()
      : ScopedCSSContainerQueriesForTest(true), ScopedLayoutNGForTest(true) {}
};

TEST_F(StyleRecalcContextTest, FromAncestors) {
  SetBodyInnerHTML(R"HTML(
    <style>
      .container { container-type: size; }
      #display_contents { display: contents; }
      #display_none { display: none; }
      #before::before { content: "X"; container-type: size; }
    </style>
    <div id="outer" class="container">
      <div>
        <div id="inner" class="container">
          <div id="display_contents" class="container">
            <div id="in_display_contents" class="container"></div>
          </div>
        </div>
        <div>
          <div id="display_none" class="container">
            <div id="in_display_none" class="container"></div>
          </div>
        </div>
        <span id="inline_container" class="container">
          <span id="in_inline_container"></span>
        </span>
        <div id="before" class="container"></div>
      </div>
    </div>
  )HTML");

  auto* outer = GetDocument().getElementById("outer");
  auto* inner = GetDocument().getElementById("inner");
  auto* display_contents = GetDocument().getElementById("display_contents");
  auto* in_display_contents =
      GetDocument().getElementById("in_display_contents");
  auto* display_none = GetDocument().getElementById("display_none");
  auto* in_display_none = GetDocument().getElementById("in_display_none");
  auto* inline_container = GetDocument().getElementById("inline_container");
  auto* in_inline_container =
      GetDocument().getElementById("in_inline_container");
  auto* before = GetDocument().getElementById("before");
  auto* before_pseudo = before->GetPseudoElement(kPseudoIdBefore);

  EXPECT_FALSE(StyleRecalcContext::FromAncestors(*outer).container);
  EXPECT_EQ(StyleRecalcContext::FromInclusiveAncestors(*outer).container,
            outer);

  EXPECT_EQ(StyleRecalcContext::FromAncestors(*inner).container, outer);
  EXPECT_EQ(StyleRecalcContext::FromInclusiveAncestors(*inner).container,
            inner);

  EXPECT_EQ(StyleRecalcContext::FromAncestors(*display_contents).container,
            inner);
  EXPECT_EQ(
      StyleRecalcContext::FromInclusiveAncestors(*display_contents).container,
      display_contents);

  EXPECT_EQ(StyleRecalcContext::FromAncestors(*in_display_contents).container,
            display_contents);
  EXPECT_EQ(StyleRecalcContext::FromInclusiveAncestors(*in_display_contents)
                .container,
            in_display_contents);

  EXPECT_EQ(StyleRecalcContext::FromAncestors(*display_none).container, outer);
  EXPECT_EQ(StyleRecalcContext::FromInclusiveAncestors(*display_none).container,
            outer);

  EXPECT_EQ(StyleRecalcContext::FromAncestors(*in_display_none).container,
            outer);
  EXPECT_EQ(
      StyleRecalcContext::FromInclusiveAncestors(*in_display_none).container,
      outer);

  EXPECT_EQ(StyleRecalcContext::FromAncestors(*inline_container).container,
            outer);
  EXPECT_EQ(
      StyleRecalcContext::FromInclusiveAncestors(*inline_container).container,
      inline_container);

  EXPECT_EQ(StyleRecalcContext::FromAncestors(*in_inline_container).container,
            inline_container);
  EXPECT_EQ(StyleRecalcContext::FromInclusiveAncestors(*in_inline_container)
                .container,
            inline_container);

  EXPECT_EQ(StyleRecalcContext::FromAncestors(*before).container, outer);
  EXPECT_EQ(StyleRecalcContext::FromInclusiveAncestors(*before).container,
            before);

  EXPECT_EQ(StyleRecalcContext::FromAncestors(*before_pseudo).container,
            before);
  EXPECT_EQ(
      StyleRecalcContext::FromInclusiveAncestors(*before_pseudo).container,
      before);
}

TEST_F(StyleRecalcContextTest, FromAncestors_ShadowIncluding) {
  GetDocument().body()->setInnerHTMLWithDeclarativeShadowDOMForTesting(R"HTML(
    <div id="outer_host" style="container-type:size">
      <template shadowroot="open">
        <div id="inner_host" style="container-type:size">
          <template shadowroot="open">
            <slot id="inner_slot" style="container-type:size"></slot>
          </template>
          <div id="inner_child" style="container-type:size"></div>
        </div>
        <slot id="outer_slot" style="container-type:size"></slot>
      </template>
      <div id="outer_child" style="container-type:size"></div>
    </div>
  )HTML");

  UpdateAllLifecyclePhasesForTest();

  auto* outer_host = GetDocument().getElementById("outer_host");
  auto* outer_child = GetDocument().getElementById("outer_child");
  auto* outer_root = outer_host->GetShadowRoot();
  auto* outer_slot = outer_root->getElementById("outer_slot");
  auto* inner_host = outer_root->getElementById("inner_host");
  auto* inner_child = outer_root->getElementById("inner_child");
  auto* inner_root = inner_host->GetShadowRoot();
  auto* inner_slot = inner_root->getElementById("inner_slot");

  EXPECT_FALSE(StyleRecalcContext::FromAncestors(*outer_host).container);
  EXPECT_EQ(StyleRecalcContext::FromInclusiveAncestors(*outer_host).container,
            outer_host);

  EXPECT_EQ(StyleRecalcContext::FromAncestors(*outer_child).container,
            outer_host);
  EXPECT_EQ(StyleRecalcContext::FromInclusiveAncestors(*outer_child).container,
            outer_child);

  EXPECT_EQ(StyleRecalcContext::FromAncestors(*outer_slot).container,
            outer_host);
  EXPECT_EQ(StyleRecalcContext::FromInclusiveAncestors(*outer_slot).container,
            outer_slot);

  EXPECT_EQ(StyleRecalcContext::FromAncestors(*inner_host).container,
            outer_host);
  EXPECT_EQ(StyleRecalcContext::FromInclusiveAncestors(*inner_host).container,
            inner_host);

  EXPECT_EQ(StyleRecalcContext::FromAncestors(*inner_child).container,
            inner_host);
  EXPECT_EQ(StyleRecalcContext::FromInclusiveAncestors(*inner_child).container,
            inner_child);

  EXPECT_EQ(StyleRecalcContext::FromAncestors(*inner_slot).container,
            inner_host);
  EXPECT_EQ(StyleRecalcContext::FromInclusiveAncestors(*inner_slot).container,
            inner_slot);
}

}  // namespace blink
