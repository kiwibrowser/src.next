// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <variant>

#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/dom/element.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/testing/dummy_page_holder.h"
#include "third_party/blink/renderer/platform/testing/runtime_enabled_features_test_helpers.h"
#include "third_party/blink/renderer/platform/testing/task_environment.h"

namespace blink {

namespace {

bool IsCountedOnParsing(std::variant<WebFeature, WebDXFeature> feature,
                        String css) {
  auto holder = std::make_unique<DummyPageHolder>(gfx::Size(800, 600));
  Document& document = holder->GetDocument();
  document.documentElement()->SetInnerHTMLWithoutTrustedTypes(
      "<style id=style></style>");
  Element* style = document.getElementById(AtomicString("style"));
  CHECK(style);
  style->SetInnerHTMLWithoutTrustedTypes(css);
  document.View()->UpdateAllLifecyclePhasesForTest();
  if (WebFeature* web_feature = std::get_if<WebFeature>(&feature)) {
    return document.IsUseCounted(*web_feature);
  }
  return document.IsWebDXFeatureCounted(std::get<WebDXFeature>(feature));
}

}  // namespace

class StyleUseCounterTest : public testing::Test {
 private:
  test::TaskEnvironment task_environment_;
};

TEST_F(StyleUseCounterTest, CSSFunctions) {
  WebFeature feature = WebFeature::kCSSFunctions;
  EXPECT_FALSE(IsCountedOnParsing(feature, "div {}"));
  EXPECT_FALSE(IsCountedOnParsing(feature, "@invalid {}"));
  EXPECT_FALSE(IsCountedOnParsing(feature, "@layer {}"));
  EXPECT_TRUE(IsCountedOnParsing(feature, "@function --f() {}"));
}

TEST_F(StyleUseCounterTest, CssIf) {
  WebDXFeature feature = WebDXFeature::kIf;
  EXPECT_FALSE(IsCountedOnParsing(feature, "div { top: var(--x); }"));
  EXPECT_FALSE(IsCountedOnParsing(feature, "div { top: 10px; }"));
  EXPECT_FALSE(IsCountedOnParsing(feature, "div { top: if(!!!); }"));
  EXPECT_TRUE(
      IsCountedOnParsing(feature, "div { top: if(style(--x: 10px): 20px); }"));
}

TEST_F(StyleUseCounterTest, ViewportUnitVariants) {
  WebDXFeature feature = WebDXFeature::kViewportUnitVariants;
  EXPECT_FALSE(IsCountedOnParsing(feature, "body { top: 10vh; }"));
  EXPECT_FALSE(IsCountedOnParsing(feature, "body { top: 10vi; }"));
  EXPECT_FALSE(IsCountedOnParsing(feature, "body { top: 10vmax; }"));
  EXPECT_TRUE(IsCountedOnParsing(feature, "body { top: 10svh; }"));
  EXPECT_TRUE(IsCountedOnParsing(feature, "body { top: 10lvi; }"));
  EXPECT_TRUE(IsCountedOnParsing(feature, "body { top: 10dvmax; }"));
}

TEST_F(StyleUseCounterTest, CSSURLRequestModifiers) {
  ScopedCSSURLRequestModifiersForTest scoped(true);

  EXPECT_FALSE(
      IsCountedOnParsing(WebFeature::kCSSURLRequestModifierCrossOrigin,
                         "body { background-image: url('/image.png'); }"));
  EXPECT_TRUE(IsCountedOnParsing(
      WebFeature::kCSSURLRequestModifierCrossOrigin,
      "body { background-image: url('/image.png' cross-origin(anonymous)); }"));
  EXPECT_TRUE(IsCountedOnParsing(
      WebFeature::kCSSURLRequestModifierIntegrity,
      "body { background-image: url('/image.png' integrity('sha256-abc')); }"));
  EXPECT_TRUE(IsCountedOnParsing(
      WebFeature::kCSSURLRequestModifierReferrerPolicy,
      "body { background-image: url('/image.png' referrer-policy(origin)); }"));

  EXPECT_TRUE(
      IsCountedOnParsing(WebFeature::kCSSURLRequestModifierCrossOrigin,
                         "@import url('/style.css' cross-origin(anonymous));"));
  EXPECT_TRUE(
      IsCountedOnParsing(WebFeature::kCSSURLRequestModifierIntegrity,
                         "@import url('/style.css' integrity('sha256-abc'));"));
  EXPECT_TRUE(
      IsCountedOnParsing(WebFeature::kCSSURLRequestModifierReferrerPolicy,
                         "@import url('/style.css' referrer-policy(origin));"));

  EXPECT_FALSE(IsCountedOnParsing(
      WebFeature::kCSSURLRequestModifierCrossOrigin,
      "body { background-image: url('/image.png' cross-origin(invalid)); }"));
}

}  // namespace blink
