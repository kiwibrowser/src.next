// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/paint/paint_layer_scrollable_area.h"

#include "build/build_config.h"
#include "cc/layers/picture_layer.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/renderer/core/animation/scroll_timeline.h"
#include "third_party/blink/renderer/core/css/css_numeric_literal_value.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/frame/visual_viewport.h"
#include "third_party/blink/renderer/core/layout/layout_box_model_object.h"
#include "third_party/blink/renderer/core/page/scrolling/snap_coordinator.h"
#include "third_party/blink/renderer/core/paint/paint_controller_paint_test.h"
#include "third_party/blink/renderer/core/paint/paint_layer.h"
#include "third_party/blink/renderer/core/scroll/scroll_types.h"
#include "third_party/blink/renderer/core/scroll/scrollbar_theme.h"
#include "third_party/blink/renderer/core/testing/color_scheme_helper.h"
#include "third_party/blink/renderer/platform/graphics/compositing/paint_artifact_compositor.h"

using testing::_;

namespace blink {
namespace {

class ScrollableAreaMockChromeClient : public RenderingTestChromeClient {
 public:
  MOCK_METHOD3(MockUpdateTooltipUnderCursor,
               void(LocalFrame*, const String&, TextDirection));
  void UpdateTooltipUnderCursor(LocalFrame& frame,
                                const String& tooltip_text,
                                TextDirection dir) override {
    MockUpdateTooltipUnderCursor(&frame, tooltip_text, dir);
  }
};

}  // namespace

#if BUILDFLAG(IS_FUCHSIA)
// TODO(crbug.com/1090230): Fix this test on Fuchsia and re-enable.
#define MAYBE_PaintLayerScrollableAreaTest DISABLED_PaintLayerScrollableAreaTest
#else
#define MAYBE_PaintLayerScrollableAreaTest PaintLayerScrollableAreaTest
#endif
class MAYBE_PaintLayerScrollableAreaTest : public PaintControllerPaintTest {
 public:
  MAYBE_PaintLayerScrollableAreaTest()
      : PaintControllerPaintTest(MakeGarbageCollected<EmptyLocalFrameClient>()),
        chrome_client_(MakeGarbageCollected<ScrollableAreaMockChromeClient>()) {
  }

  ~MAYBE_PaintLayerScrollableAreaTest() override {
    testing::Mock::VerifyAndClearExpectations(&GetChromeClient());
  }

  ScrollableAreaMockChromeClient& GetChromeClient() const override {
    return *chrome_client_;
  }

  bool IsComposited(const LayoutObject* scroller) {
    const auto* paint_properties = scroller->FirstFragment().PaintProperties();
    return paint_properties && paint_properties->Transform() &&
           paint_properties->Transform()->HasDirectCompositingReasons();
  }

  bool UsesCompositedScrolling(const LayoutObject* scroller) {
    const auto* paint_properties = scroller->FirstFragment().PaintProperties();
    bool composited =
        paint_properties && paint_properties->ScrollTranslation() &&
        paint_properties->ScrollTranslation()->HasDirectCompositingReasons();

    auto* layer = To<LayoutBoxModelObject>(scroller)->Layer();
    if (!layer) {
      DCHECK(!composited);
      return false;
    }

    auto* scrollable_area = layer->GetScrollableArea();
    if (!scrollable_area) {
      DCHECK(!composited);
      return false;
    }

    return composited;
  }

 private:
  void SetUp() override {
    EnableCompositing();
    RenderingTest::SetUp();
  }

  Persistent<ScrollableAreaMockChromeClient> chrome_client_;
};

INSTANTIATE_PAINT_TEST_SUITE_P(MAYBE_PaintLayerScrollableAreaTest);

TEST_P(MAYBE_PaintLayerScrollableAreaTest, OpaqueContainedLayersPromoted) {
  SetBodyInnerHTML(R"HTML(
    <style>
    #scroller { overflow: scroll; height: 200px; width: 200px;
    contain: paint; background: white local content-box;
    border: 10px solid rgba(0, 255, 0, 0.5); }
    #scrolled { height: 300px; }
    </style>
    <div id="scroller"><div id="scrolled"></div></div>
  )HTML");

  auto* scroller = GetLayoutObjectByElementId("scroller");
  EXPECT_TRUE(UsesCompositedScrolling(scroller));
}

TEST_P(MAYBE_PaintLayerScrollableAreaTest, NonStackingContextScrollerPromoted) {
  SetBodyInnerHTML(R"HTML(
    <style>
    #scroller { overflow: scroll; height: 200px; width: 200px;
    background: white local content-box;
    border: 10px solid rgba(0, 255, 0, 0.5); }
    #scrolled { height: 300px; }
    #positioned { position: relative; }
    </style>
    <div id="scroller">
      <div id="positioned">Not contained by scroller.</div>
      <div id="scrolled"></div>
    </div>
  )HTML");

  EXPECT_TRUE(UsesCompositedScrolling(GetLayoutObjectByElementId("scroller")));
}

TEST_P(MAYBE_PaintLayerScrollableAreaTest, TransparentLayersNotPromoted) {
  SetBodyInnerHTML(R"HTML(
    <style>
    #scroller { overflow: scroll; height: 200px; width: 200px; background:
    rgba(0, 255, 0, 0.5) local content-box; border: 10px solid rgba(0, 255,
    0, 0.5); contain: paint; }
    #scrolled { height: 300px; }
    </style>
    <div id="scroller"><div id="scrolled"></div></div>
  )HTML");

  EXPECT_FALSE(UsesCompositedScrolling(GetLayoutObjectByElementId("scroller")));
}

TEST_P(MAYBE_PaintLayerScrollableAreaTest,
       OpaqueLayersDepromotedOnStyleChange) {
  SetBodyInnerHTML(R"HTML(
    <style>
    #scroller { overflow: scroll; height: 200px; width: 200px; background:
    white local content-box; contain: paint; }
    #scrolled { height: 300px; }
    </style>
    <div id="scroller"><div id="scrolled"></div></div>
  )HTML");

  Element* scroller = GetDocument().getElementById("scroller");
  EXPECT_TRUE(UsesCompositedScrolling(scroller->GetLayoutObject()));

  // Change the background to transparent
  scroller->setAttribute(
      html_names::kStyleAttr,
      "background: rgba(255,255,255,0.5) local content-box;");
  UpdateAllLifecyclePhasesForTest();
  EXPECT_FALSE(UsesCompositedScrolling(scroller->GetLayoutObject()));
}

TEST_P(MAYBE_PaintLayerScrollableAreaTest, OpaqueLayersPromotedOnStyleChange) {
  SetBodyInnerHTML(R"HTML(
    <style>
    #scroller { overflow: scroll; height: 200px; width: 200px; background:
    rgba(255,255,255,0.5) local content-box; contain: paint; }
    #scrolled { height: 300px; }
    </style>
    <div id="scroller"><div id="scrolled"></div></div>
  )HTML");

  Element* scroller = GetDocument().getElementById("scroller");
  EXPECT_FALSE(UsesCompositedScrolling(scroller->GetLayoutObject()));

  // Change the background to opaque
  scroller->setAttribute(html_names::kStyleAttr,
                         "background: white local content-box;");
  UpdateAllLifecyclePhasesForTest();
  EXPECT_TRUE(UsesCompositedScrolling(scroller->GetLayoutObject()));
}

// Tests that a transform on the scroller or an ancestor doesn't prevent
// promotion.
TEST_P(MAYBE_PaintLayerScrollableAreaTest,
       TransformDoesNotPreventCompositedScrolling) {
  SetBodyInnerHTML(R"HTML(
    <style>
    #scroller { overflow: scroll; height: 200px; width: 200px; background:
    white local content-box; contain: paint; }
    #scrolled { height: 300px; }
    </style>
    <div id="parent">
      <div id="scroller"><div id="scrolled"></div></div>
    </div>
  )HTML");

  Element* parent = GetDocument().getElementById("parent");
  Element* scroller = GetDocument().getElementById("scroller");
  EXPECT_TRUE(UsesCompositedScrolling(scroller->GetLayoutObject()));

  // Change the parent to have a transform.
  parent->setAttribute(html_names::kStyleAttr, "transform: translate(1px, 0);");
  UpdateAllLifecyclePhasesForTest();
  EXPECT_TRUE(UsesCompositedScrolling(scroller->GetLayoutObject()));

  // Change the parent to have no transform again.
  parent->removeAttribute(html_names::kStyleAttr);
  UpdateAllLifecyclePhasesForTest();
  EXPECT_TRUE(UsesCompositedScrolling(scroller->GetLayoutObject()));

  // Apply a transform to the scroller directly.
  scroller->setAttribute(html_names::kStyleAttr,
                         "transform: translate(1px, 0);");
  UpdateAllLifecyclePhasesForTest();
  EXPECT_TRUE(UsesCompositedScrolling(scroller->GetLayoutObject()));
}

TEST_P(MAYBE_PaintLayerScrollableAreaTest,
       PromoteLayerRegardlessOfSelfAndAncestorOpacity) {
  SetBodyInnerHTML(R"HTML(
    <style>
    #scroller { overflow: scroll; height: 200px; width: 200px; background:
    white local content-box; contain: paint; }
    #scrolled { height: 300px; }
    </style>
    <div id="parent">
      <div id="scroller"><div id="scrolled"></div></div>
    </div>
  )HTML");

  Element* parent = GetDocument().getElementById("parent");
  Element* scroller = GetDocument().getElementById("scroller");
  EXPECT_TRUE(UsesCompositedScrolling(scroller->GetLayoutObject()));

  // Change the parent to be partially translucent.
  parent->setAttribute(html_names::kStyleAttr, "opacity: 0.5;");
  UpdateAllLifecyclePhasesForTest();
  EXPECT_TRUE(UsesCompositedScrolling(scroller->GetLayoutObject()));

  // Change the parent to be opaque again.
  parent->setAttribute(html_names::kStyleAttr, "opacity: 1;");
  UpdateAllLifecyclePhasesForTest();
  EXPECT_TRUE(UsesCompositedScrolling(scroller->GetLayoutObject()));

  // Make the scroller translucent.
  scroller->setAttribute(html_names::kStyleAttr, "opacity: 0.5");
  UpdateAllLifecyclePhasesForTest();
  EXPECT_TRUE(UsesCompositedScrolling(scroller->GetLayoutObject()));
}

// Test that will-change: transform applied to the scroller will cause the
// scrolling contents layer to be promoted.
TEST_P(MAYBE_PaintLayerScrollableAreaTest,
       CompositedScrollOnWillChangeTransform) {
  SetBodyInnerHTML(R"HTML(
    <style>
      #scroller { overflow: scroll; height: 100px; width: 100px; }
      #scrolled { height: 300px; }
    </style>
    <div id="scroller"><div id="scrolled"></div></div>
  )HTML");

  Element* scroller = GetDocument().getElementById("scroller");
  EXPECT_FALSE(UsesCompositedScrolling(scroller->GetLayoutObject()));

  scroller->setAttribute(html_names::kStyleAttr, "will-change: transform");
  UpdateAllLifecyclePhasesForTest();
  EXPECT_TRUE(UsesCompositedScrolling(scroller->GetLayoutObject()));

  scroller->setAttribute(html_names::kStyleAttr, "");
  UpdateAllLifecyclePhasesForTest();
  EXPECT_FALSE(UsesCompositedScrolling(scroller->GetLayoutObject()));
}

// Test that will-change: transform applied to the scroller will cause the
// scrolling contents layer to be promoted.
TEST_P(MAYBE_PaintLayerScrollableAreaTest, ScrollLayerOnPointerEvents) {
  GetDocument().GetFrame()->GetSettings()->SetPreferCompositingToLCDTextEnabled(
      true);
  SetBodyInnerHTML(R"HTML(
    <style>
      #scroller { overflow: scroll; height: 100px; width: 100px; }
      #scrolled { height: 300px; }
    </style>
    <div id="scroller"><div id="scrolled"></div></div>
  )HTML");

  Element* scroller = GetDocument().getElementById("scroller");
  EXPECT_TRUE(UsesCompositedScrolling(scroller->GetLayoutObject()));

  // pointer-events: none causes the scoller to be invisible for hit testing,
  // so ScrollsOverflow becomes false on the PaintLayerScrollableArea, and hence
  // composited scrolling is not present.
  scroller->setAttribute(html_names::kStyleAttr, "pointer-events: none");
  UpdateAllLifecyclePhasesForTest();
  EXPECT_FALSE(UsesCompositedScrolling(scroller->GetLayoutObject()));

  scroller->setAttribute(html_names::kStyleAttr, "");
  UpdateAllLifecyclePhasesForTest();
  EXPECT_TRUE(UsesCompositedScrolling(scroller->GetLayoutObject()));
}

// Test that <input> elements don't use composited scrolling even with
// "will-change:transform".
TEST_P(MAYBE_PaintLayerScrollableAreaTest, InputElementPromotionTest) {
  SetBodyInnerHTML(R"HTML(
    <!DOCTYPE html>
    <style>
     .composited { will-change: transform; }
    </style>
    <input id='input' width=10 style='font-size:40pt;'/>
  )HTML");

  Element* element = GetDocument().getElementById("input");
  EXPECT_FALSE(IsComposited(element->GetLayoutObject()));
  EXPECT_FALSE(UsesCompositedScrolling(element->GetLayoutObject()));

  element->setAttribute("class", "composited");
  UpdateAllLifecyclePhasesForTest();
  EXPECT_TRUE(IsComposited(element->GetLayoutObject()));
  EXPECT_FALSE(UsesCompositedScrolling(element->GetLayoutObject()));
}

// Test that <select> elements use composited scrolling with
// "will-change:transform".
TEST_P(MAYBE_PaintLayerScrollableAreaTest, SelectElementPromotionTest) {
  SetBodyInnerHTML(R"HTML(
    <!DOCTYPE html>
    <style>
     .composited { will-change: transform; }
    </style>
    <select id='select' size='2'>
      <option> value 1</option>
      <option> value 2</option>
      <option> value 3</option>
      <option> value 4</option>
    </select>
  )HTML");

  Element* element = GetDocument().getElementById("select");
  EXPECT_FALSE(IsComposited(element->GetLayoutObject()));
  EXPECT_FALSE(UsesCompositedScrolling(element->GetLayoutObject()));

  element->setAttribute("class", "composited");
  UpdateAllLifecyclePhasesForTest();
  EXPECT_TRUE(IsComposited(element->GetLayoutObject()));
#if BUILDFLAG(IS_ANDROID)
  // <select> implementation is different and not scrollable on Android.
  EXPECT_FALSE(UsesCompositedScrolling(element->GetLayoutObject()));
#else
  EXPECT_TRUE(UsesCompositedScrolling(element->GetLayoutObject()));
#endif
}

// Ensure OverlayScrollbarColorTheme get updated when page load
TEST_P(MAYBE_PaintLayerScrollableAreaTest, OverlayScrollbarColorThemeUpdated) {
  SetBodyInnerHTML(R"HTML(
    <style>
    div { overflow: scroll; }
    #white { background-color: white; }
    #black { background-color: black; }
    </style>
    <div id="none">a</div>
    <div id="white">b</div>
    <div id="black">c</div>
  )HTML");

  PaintLayer* none_layer = GetPaintLayerByElementId("none");
  PaintLayer* white_layer = GetPaintLayerByElementId("white");
  PaintLayer* black_layer = GetPaintLayerByElementId("black");

  ASSERT_TRUE(none_layer);
  ASSERT_TRUE(white_layer);
  ASSERT_TRUE(black_layer);

  ASSERT_EQ(ScrollbarOverlayColorTheme::kScrollbarOverlayColorThemeDark,
            none_layer->GetScrollableArea()->GetScrollbarOverlayColorTheme());
  ASSERT_EQ(ScrollbarOverlayColorTheme::kScrollbarOverlayColorThemeDark,
            white_layer->GetScrollableArea()->GetScrollbarOverlayColorTheme());
  ASSERT_EQ(ScrollbarOverlayColorTheme::kScrollbarOverlayColorThemeLight,
            black_layer->GetScrollableArea()->GetScrollbarOverlayColorTheme());
}

TEST_P(MAYBE_PaintLayerScrollableAreaTest,
       RecalculatesScrollbarOverlayIfBackgroundChanges) {
  SetBodyInnerHTML(R"HTML(
    <style>
      #scroller {
        width: 10px;
        height: 10px;
        overflow: scroll;
      }
      .forcescroll { height: 1000px; }
    </style>
    <div id="scroller">
      <div class="forcescroll"></div>
    </div>
  )HTML");
  PaintLayer* scroll_paint_layer = GetPaintLayerByElementId("scroller");
  EXPECT_EQ(
      ScrollbarOverlayColorTheme::kScrollbarOverlayColorThemeDark,
      scroll_paint_layer->GetScrollableArea()->GetScrollbarOverlayColorTheme());

  GetElementById("scroller")
      ->setAttribute(html_names::kStyleAttr, "background: rgb(34, 85, 51);");
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(
      ScrollbarOverlayColorTheme::kScrollbarOverlayColorThemeLight,
      scroll_paint_layer->GetScrollableArea()->GetScrollbarOverlayColorTheme());

  GetElementById("scroller")
      ->setAttribute(html_names::kStyleAttr, "background: rgb(236, 143, 185);");
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(
      ScrollbarOverlayColorTheme::kScrollbarOverlayColorThemeDark,
      scroll_paint_layer->GetScrollableArea()->GetScrollbarOverlayColorTheme());
}

// The scrollbar overlay color theme should follow the used color scheme when a
// background color is not available on the scroller itself.
TEST_P(MAYBE_PaintLayerScrollableAreaTest,
       PreferredOverlayScrollbarColorTheme) {
  ColorSchemeHelper color_scheme_helper(GetDocument());
  color_scheme_helper.SetPreferredColorScheme(
      mojom::blink::PreferredColorScheme::kDark);
  SetBodyInnerHTML(R"HTML(
    <meta name="color-scheme" content="light dark">
    <style>
      .scroller {
        width: 10px;
        height: 10px;
        overflow: scroll;
      }
      #white { background-color: white; }
      #black { background-color: black; }
      .forcescroll { height: 1000px; }
    </style>
    <div class="scroller" id="none">
      <div class="forcescroll"></div>
    </div>
    <div class="scroller" id="white">
      <div class="forcescroll"></div>
    </div>
    <div class="scroller" id="black">
      <div class="forcescroll"></div>
    </div>
  )HTML");

  PaintLayer* none_layer = GetPaintLayerByElementId("none");
  PaintLayer* white_layer = GetPaintLayerByElementId("white");
  PaintLayer* black_layer = GetPaintLayerByElementId("black");
  EXPECT_EQ(ScrollbarOverlayColorTheme::kScrollbarOverlayColorThemeLight,
            none_layer->GetScrollableArea()->GetScrollbarOverlayColorTheme());
  EXPECT_EQ(ScrollbarOverlayColorTheme::kScrollbarOverlayColorThemeDark,
            white_layer->GetScrollableArea()->GetScrollbarOverlayColorTheme());
  EXPECT_EQ(ScrollbarOverlayColorTheme::kScrollbarOverlayColorThemeLight,
            black_layer->GetScrollableArea()->GetScrollbarOverlayColorTheme());

  color_scheme_helper.SetPreferredColorScheme(
      mojom::blink::PreferredColorScheme::kLight);
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(ScrollbarOverlayColorTheme::kScrollbarOverlayColorThemeDark,
            none_layer->GetScrollableArea()->GetScrollbarOverlayColorTheme());
  EXPECT_EQ(ScrollbarOverlayColorTheme::kScrollbarOverlayColorThemeDark,
            white_layer->GetScrollableArea()->GetScrollbarOverlayColorTheme());
  EXPECT_EQ(ScrollbarOverlayColorTheme::kScrollbarOverlayColorThemeLight,
            black_layer->GetScrollableArea()->GetScrollbarOverlayColorTheme());
}

TEST_P(MAYBE_PaintLayerScrollableAreaTest,
       HideTooltipWhenScrollPositionChanges) {
  SetBodyInnerHTML(R"HTML(
    <style>
    #scroller { width: 100px; height: 100px; overflow: scroll; }
    #scrolled { height: 300px; }
    </style>
    <div id="scroller"><div id="scrolled"></div></div>
  )HTML");

  Element* scroller = GetDocument().getElementById("scroller");
  PaintLayerScrollableArea* scrollable_area =
      To<LayoutBoxModelObject>(scroller->GetLayoutObject())
          ->GetScrollableArea();
  ASSERT_TRUE(scrollable_area);

  EXPECT_CALL(GetChromeClient(), MockUpdateTooltipUnderCursor(
                                     GetDocument().GetFrame(), String(), _))
      .Times(1);
  scrollable_area->SetScrollOffset(ScrollOffset(1, 1),
                                   mojom::blink::ScrollType::kUser);

  // Programmatic scrolling should not dismiss the tooltip, so
  // UpdateTooltipUnderCursor should not be called for this invocation.
  EXPECT_CALL(GetChromeClient(), MockUpdateTooltipUnderCursor(
                                     GetDocument().GetFrame(), String(), _))
      .Times(0);
  scrollable_area->SetScrollOffset(ScrollOffset(2, 2),
                                   mojom::blink::ScrollType::kProgrammatic);
}

TEST_P(MAYBE_PaintLayerScrollableAreaTest,
       IncludeOverlayScrollbarsInVisibleWidth) {
  USE_NON_OVERLAY_SCROLLBARS();

  SetBodyInnerHTML(R"HTML(
    <style>
    #scroller { overflow: overlay; height: 100px; width: 100px; }
    #scrolled { width: 100px; height: 200px; }
    </style>
    <div id="scroller"><div id="scrolled"></div></div>
  )HTML");

  Element* scroller = GetDocument().getElementById("scroller");
  ASSERT_TRUE(scroller);
  PaintLayerScrollableArea* scrollable_area =
      To<LayoutBoxModelObject>(scroller->GetLayoutObject())
          ->GetScrollableArea();
  ASSERT_TRUE(scrollable_area);
  scrollable_area->SetScrollOffset(ScrollOffset(100, 0),
                                   mojom::blink::ScrollType::kClamping);
  EXPECT_EQ(scrollable_area->GetScrollOffset().x(), 0);
}

TEST_P(MAYBE_PaintLayerScrollableAreaTest,
       ShowAutoScrollbarsForVisibleContent) {
  USE_NON_OVERLAY_SCROLLBARS();

  SetBodyInnerHTML(R"HTML(
    <style>
    #outerDiv {
      width: 15px;
      height: 100px;
      overflow-y: auto;
      overflow-x: hidden;
    }
    #innerDiv {
      height:300px;
      width: 1px;
    }
    </style>
    <div id='outerDiv'>
      <div id='innerDiv'></div>
    </div>
  )HTML");

  Element* outer_div = GetDocument().getElementById("outerDiv");
  ASSERT_TRUE(outer_div);
  outer_div->GetLayoutObject()->SetNeedsLayout("test");
  UpdateAllLifecyclePhasesForTest();
  PaintLayerScrollableArea* scrollable_area =
      To<LayoutBoxModelObject>(outer_div->GetLayoutObject())
          ->GetScrollableArea();
  ASSERT_TRUE(scrollable_area);
  EXPECT_TRUE(scrollable_area->HasVerticalScrollbar());
}

TEST_P(MAYBE_PaintLayerScrollableAreaTest, FloatOverflowInRtlContainer) {
  USE_NON_OVERLAY_SCROLLBARS();

  SetBodyInnerHTML(R"HTML(
    <!DOCTYPE html>
    <style>
    #container {
      width: 200px;
      overflow-x: auto;
      overflow-y: scroll;
      direction: rtl;
    }
    </style>
    <div id='container'>
      <div style='float:left'>
    lorem ipsum
      </div>
    </div>
  )HTML");

  Element* container = GetDocument().getElementById("container");
  ASSERT_TRUE(container);
  PaintLayerScrollableArea* scrollable_area =
      To<LayoutBoxModelObject>(container->GetLayoutObject())
          ->GetScrollableArea();
  ASSERT_TRUE(scrollable_area);
  EXPECT_FALSE(scrollable_area->HasHorizontalScrollbar());
}

TEST_P(MAYBE_PaintLayerScrollableAreaTest, ScrollOriginInRtlContainer) {
  SetBodyInnerHTML(R"HTML(
    <!DOCTYPE html>
    <style>
    #container {
      width: 200px;
      overflow: auto;
      direction: rtl;
    }
    #content {
      width: 300px;
    }
    </style>
    <div id='container'>
      <div id='content'>
    lorem ipsum
      <div>
    </div>
  )HTML");

  Element* container = GetDocument().getElementById("container");
  ASSERT_TRUE(container);
  PaintLayerScrollableArea* scrollable_area =
      To<LayoutBoxModelObject>(container->GetLayoutObject())
          ->GetScrollableArea();
  ASSERT_TRUE(scrollable_area);
  EXPECT_EQ(scrollable_area->ScrollOrigin().x(), 100);
}

TEST_P(MAYBE_PaintLayerScrollableAreaTest,
       OverflowHiddenScrollOffsetInvalidation) {
  SetBodyInnerHTML(R"HTML(
    <style>
    #scroller {
      overflow: hidden;
      height: 200px;
      width: 200px;
    }
    </style>
    <div id='scroller'>
      <div id='forceScroll' style='height: 2000px;'></div>
    </div>
  )HTML");

  auto* scroller = GetLayoutObjectByElementId("scroller");
  auto* scrollable_area =
      To<LayoutBoxModelObject>(scroller)->GetScrollableArea();

  const auto* properties = scroller->FirstFragment().PaintProperties();

  // No scroll offset translation is needed when scroll offset is zero.
  EXPECT_EQ(nullptr, properties->ScrollTranslation());
  EXPECT_EQ(ScrollOffset(0, 0), scrollable_area->GetScrollOffset());

  // A property update is needed when scroll offset changes.
  scrollable_area->SetScrollOffset(ScrollOffset(0, 1),
                                   mojom::blink::ScrollType::kProgrammatic);
  EXPECT_TRUE(scroller->NeedsPaintPropertyUpdate());
  UpdateAllLifecyclePhasesExceptPaint();
  EXPECT_TRUE(scroller->PaintingLayer()->SelfNeedsRepaint());

  // A scroll offset translation is needed when scroll offset is non-zero.
  EXPECT_EQ(ScrollOffset(0, 1), scrollable_area->GetScrollOffset());
  EXPECT_NE(nullptr, properties->ScrollTranslation());

  UpdateAllLifecyclePhasesForTest();

  scrollable_area->SetScrollOffset(ScrollOffset(0, 2),
                                   mojom::blink::ScrollType::kProgrammatic);
  EXPECT_TRUE(scroller->NeedsPaintPropertyUpdate());
  UpdateAllLifecyclePhasesExceptPaint();
  EXPECT_TRUE(scroller->PaintingLayer()->SelfNeedsRepaint());

  // A scroll offset translation is still needed when scroll offset is non-zero.
  EXPECT_EQ(ScrollOffset(0, 2), scrollable_area->GetScrollOffset());
  EXPECT_NE(nullptr, properties->ScrollTranslation());

  UpdateAllLifecyclePhasesForTest();

  scrollable_area->SetScrollOffset(ScrollOffset(0, 0),
                                   mojom::blink::ScrollType::kProgrammatic);
  EXPECT_TRUE(scroller->NeedsPaintPropertyUpdate());
  UpdateAllLifecyclePhasesExceptPaint();
  EXPECT_TRUE(scroller->PaintingLayer()->SelfNeedsRepaint());

  // No scroll offset translation is needed when scroll offset is zero.
  EXPECT_EQ(nullptr, properties->ScrollTranslation());
  EXPECT_EQ(ScrollOffset(0, 0), scrollable_area->GetScrollOffset());
}

TEST_P(MAYBE_PaintLayerScrollableAreaTest, ScrollDoesNotInvalidate) {
  SetBodyInnerHTML(R"HTML(
    <style>
      #scroller {
        overflow: scroll;
        height: 200px;
        width: 200px;
        background: linear-gradient(black, white);
      }
    </style>
    <div id='scroller'>
      <div id='forceScroll' style='height: 2000px;'></div>
    </div>
  )HTML");

  auto* scroller = GetLayoutObjectByElementId("scroller");
  auto* scrollable_area =
      To<LayoutBoxModelObject>(scroller)->GetScrollableArea();

  const auto* properties = scroller->FirstFragment().PaintProperties();
  // Scroll offset translation is needed even when scroll offset is zero.
  EXPECT_NE(nullptr, properties->ScrollTranslation());
  EXPECT_EQ(ScrollOffset(0, 0), scrollable_area->GetScrollOffset());

  // Changing the scroll offset should not require paint invalidation.
  scrollable_area->SetScrollOffset(ScrollOffset(0, 1),
                                   mojom::blink::ScrollType::kProgrammatic);
  EXPECT_FALSE(scroller->ShouldDoFullPaintInvalidation());
  EXPECT_TRUE(scroller->NeedsPaintPropertyUpdate());
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(ScrollOffset(0, 1), scrollable_area->GetScrollOffset());
  EXPECT_NE(nullptr, properties->ScrollTranslation());
}

TEST_P(MAYBE_PaintLayerScrollableAreaTest,
       ScrollWithStickyNeedsCompositingUpdate) {
  SetBodyInnerHTML(R"HTML(
    <style>
      * {
        margin: 0;
      }
      body {
        height: 610px;
        width: 820px;
      }
      #sticky {
        height: 10px;
        left: 50px;
        position: sticky;
        top: 50px;
        width: 10px;
      }
    </style>
    <div id=sticky></div>
  )HTML");

  auto* scrollable_area = GetLayoutView().GetScrollableArea();
  EXPECT_EQ(ScrollOffset(0, 0), scrollable_area->GetScrollOffset());

  // Changing the scroll offset requires a compositing update to rerun overlap
  // testing.
  scrollable_area->SetScrollOffset(ScrollOffset(0, 1),
                                   mojom::blink::ScrollType::kProgrammatic);
  UpdateAllLifecyclePhasesExceptPaint();
  EXPECT_EQ(!RuntimeEnabledFeatures::ScrollUpdateOptimizationsEnabled(),
            GetDocument().View()->GetPaintArtifactCompositor()->NeedsUpdate());
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(ScrollOffset(0, 1), scrollable_area->GetScrollOffset());
}

TEST_P(MAYBE_PaintLayerScrollableAreaTest,
       ScrollWithFixedDoesNotNeedCompositingUpdate) {
  SetBodyInnerHTML(R"HTML(
    <style>
      * {
        margin: 0;
      }
      body {
        height: 610px;
        width: 820px;
      }
      #fixed {
        height: 10px;
        left: 50px;
        position: fixed;
        top: 50px;
        width: 10px;
      }
    </style>
    <div id=fixed></div>
  )HTML");

  auto* scrollable_area = GetLayoutView().GetScrollableArea();
  EXPECT_EQ(ScrollOffset(0, 0), scrollable_area->GetScrollOffset());

  // Changing the scroll offset should not require a compositing update even
  // though fixed-pos content is present as fixed bounds is already expanded to
  // include all possible scroll offsets.
  scrollable_area->SetScrollOffset(ScrollOffset(0, 1),
                                   mojom::blink::ScrollType::kProgrammatic);
  UpdateAllLifecyclePhasesExceptPaint();
  EXPECT_FALSE(
      GetDocument().View()->GetPaintArtifactCompositor()->NeedsUpdate());
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(ScrollOffset(0, 1), scrollable_area->GetScrollOffset());
}

TEST_P(MAYBE_PaintLayerScrollableAreaTest,
       ScrollWithLocalAttachmentBackgroundInScrollingContents) {
  SetBodyInnerHTML(R"HTML(
    <style>
      #scroller {
        overflow: scroll;
        height: 200px;
        width: 200px;
        background: linear-gradient(black, white);
        background-attachment: local;
      }
    </style>
    <div id='scroller'>
      <div id='forceScroll' style='height: 2000px;'></div>
    </div>
  )HTML");

  auto* scroller = GetLayoutBoxByElementId("scroller");
  auto* scrollable_area = scroller->GetScrollableArea();
  EXPECT_EQ(kBackgroundPaintInContentsSpace,
            scroller->ComputeBackgroundPaintLocationIfComposited());
  EXPECT_EQ(kBackgroundPaintInContentsSpace,
            scroller->GetBackgroundPaintLocation());
  EXPECT_TRUE(UsesCompositedScrolling(scroller));

  // Programmatically changing the scroll offset.
  scrollable_area->SetScrollOffset(ScrollOffset(0, 1),
                                   mojom::blink::ScrollType::kProgrammatic);
  // No paint invalidation because it uses composited scrolling.
  EXPECT_FALSE(scroller->ShouldDoFullPaintInvalidation());
  EXPECT_FALSE(scroller->BackgroundNeedsFullPaintInvalidation());

  EXPECT_TRUE(scroller->NeedsPaintPropertyUpdate());
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(ScrollOffset(0, 1), scrollable_area->GetScrollOffset());
  const auto* properties = scroller->FirstFragment().PaintProperties();
  EXPECT_NE(nullptr, properties->ScrollTranslation());
}

TEST_P(MAYBE_PaintLayerScrollableAreaTest, ScrollWith3DPreserveParent) {
  SetBodyInnerHTML(R"HTML(
    <style>
      #scroller {
        overflow-y: scroll;
        height: 200px;
        width: 200px;
        background: white;
        /* TODO(crbug.com/1256990): This is to work around the issue of
           unexpected effect node on a non-self-painting PaintLayer. */
        position: relative;
      }
    </style>
    <div style='transform-style: preserve-3d;'>
      <div id='scroller'>
        <div style='height: 2000px;'></div>
      </div>
    </div>
  )HTML");

  auto* scroller = GetLayoutBoxByElementId("scroller");
  EXPECT_EQ(kBackgroundPaintInBorderBoxSpace,
            scroller->ComputeBackgroundPaintLocationIfComposited());
}

TEST_P(MAYBE_PaintLayerScrollableAreaTest,
       ScrollWithLocalAttachmentBackgroundInMainLayer) {
  SetBodyInnerHTML(R"HTML(
    <style>
      #scroller {
        overflow: scroll;
        height: 200px;
        width: 200px;
        border: 10px dashed black;
        background: linear-gradient(black, white) local, yellow;
      }
    </style>
    <div id='scroller'>
      <div id='forceScroll' style='height: 2000px;'></div>
    </div>
  )HTML");

  auto* scroller = GetLayoutBoxByElementId("scroller");
  auto* scrollable_area = scroller->GetScrollableArea();
  EXPECT_EQ(kBackgroundPaintInBothSpaces,
            scroller->ComputeBackgroundPaintLocationIfComposited());
  EXPECT_EQ(kBackgroundPaintInBothSpaces,
            scroller->GetBackgroundPaintLocation());

  // Programmatically changing the scroll offset.
  scrollable_area->SetScrollOffset(ScrollOffset(0, 1),
                                   mojom::blink::ScrollType::kProgrammatic);
  // No invalidation because the background paints into the main layer.
  EXPECT_TRUE(scroller->ShouldDoFullPaintInvalidation());
  EXPECT_TRUE(scroller->BackgroundNeedsFullPaintInvalidation());
  EXPECT_TRUE(scroller->NeedsPaintPropertyUpdate());
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(ScrollOffset(0, 1), scrollable_area->GetScrollOffset());
  const auto* properties = scroller->FirstFragment().PaintProperties();
  EXPECT_NE(nullptr, properties->ScrollTranslation());
}

TEST_P(MAYBE_PaintLayerScrollableAreaTest,
       ViewScrollWithFixedAttachmentBackground) {
  SetBodyInnerHTML(R"HTML(
    <style>
      html, #fixed-background {
        background: linear-gradient(black, white) fixed;
      }
      #fixed-background {
        width: 200px;
        height: 200px;
        overflow: scroll;
      }
    </style>
    <div id="fixed-background">
      <div style="height: 3000px"></div>
    </div>
    <div style="height: 3000px"></div>
  )HTML");

  EXPECT_EQ(kBackgroundPaintInContentsSpace,
            GetLayoutView().GetBackgroundPaintLocation());
  auto* fixed_background_div = GetLayoutBoxByElementId("fixed-background");
  EXPECT_EQ(kBackgroundPaintInBorderBoxSpace,
            fixed_background_div->GetBackgroundPaintLocation());
  auto* div_scrollable_area = fixed_background_div->GetScrollableArea();
  auto* view_scrollable_area = GetLayoutView().GetScrollableArea();

  // Programmatically changing the view's scroll offset. Should invalidate all
  // objects with fixed attachment background.
  view_scrollable_area->SetScrollOffset(
      ScrollOffset(0, 1), mojom::blink::ScrollType::kProgrammatic);
  EXPECT_TRUE(fixed_background_div->ShouldDoFullPaintInvalidation());
  EXPECT_TRUE(fixed_background_div->BackgroundNeedsFullPaintInvalidation());
  EXPECT_FALSE(fixed_background_div->NeedsPaintPropertyUpdate());
  EXPECT_TRUE(GetLayoutView().ShouldDoFullPaintInvalidation());
  EXPECT_TRUE(GetLayoutView().BackgroundNeedsFullPaintInvalidation());
  EXPECT_TRUE(GetLayoutView().NeedsPaintPropertyUpdate());
  UpdateAllLifecyclePhasesForTest();

  // Programmatically changing the div's scroll offset. Should invalidate the
  // scrolled div with fixed attachment background.
  div_scrollable_area->SetScrollOffset(ScrollOffset(0, 1),
                                       mojom::blink::ScrollType::kProgrammatic);
  EXPECT_TRUE(fixed_background_div->ShouldDoFullPaintInvalidation());
  EXPECT_TRUE(fixed_background_div->BackgroundNeedsFullPaintInvalidation());
  EXPECT_TRUE(fixed_background_div->NeedsPaintPropertyUpdate());
  EXPECT_FALSE(GetLayoutView().ShouldDoFullPaintInvalidation());
  EXPECT_FALSE(GetLayoutView().BackgroundNeedsFullPaintInvalidation());
  EXPECT_FALSE(GetLayoutView().NeedsPaintPropertyUpdate());
}

TEST_P(MAYBE_PaintLayerScrollableAreaTest,
       ViewScrollWithSolidColorFixedAttachmentBackground) {
  SetBodyInnerHTML(R"HTML(
    <style>
      html, #fixed-background {
        background: green fixed;
      }
      #fixed-background {
        width: 200px;
        height: 200px;
        overflow: scroll;
      }
    </style>
    <div id="fixed-background">
      <div style="height: 3000px"></div>
    </div>
    <div style="height: 3000px"></div>
  )HTML");

  // Fixed-attachment solid-color background should be treated as default
  // attachment.
  EXPECT_EQ(kBackgroundPaintInContentsSpace,
            GetLayoutView().GetBackgroundPaintLocation());
  auto* fixed_background_div = GetLayoutBoxByElementId("fixed-background");
  EXPECT_EQ(kBackgroundPaintInContentsSpace,
            fixed_background_div->ComputeBackgroundPaintLocationIfComposited());
  EXPECT_EQ(kBackgroundPaintInContentsSpace,
            fixed_background_div->GetBackgroundPaintLocation());
  auto* div_scrollable_area = fixed_background_div->GetScrollableArea();
  auto* view_scrollable_area = GetLayoutView().GetScrollableArea();

  // Programmatically changing the view's scroll offset. Should invalidate all
  // objects with fixed attachment background.
  view_scrollable_area->SetScrollOffset(
      ScrollOffset(0, 1), mojom::blink::ScrollType::kProgrammatic);
  EXPECT_FALSE(fixed_background_div->ShouldDoFullPaintInvalidation());
  EXPECT_FALSE(fixed_background_div->BackgroundNeedsFullPaintInvalidation());
  EXPECT_FALSE(fixed_background_div->NeedsPaintPropertyUpdate());
  EXPECT_FALSE(GetLayoutView().ShouldDoFullPaintInvalidation());
  EXPECT_FALSE(GetLayoutView().BackgroundNeedsFullPaintInvalidation());
  EXPECT_TRUE(GetLayoutView().NeedsPaintPropertyUpdate());
  UpdateAllLifecyclePhasesForTest();

  // Programmatically changing the div's scroll offset. Should invalidate the
  // scrolled div with fixed attachment background.
  div_scrollable_area->SetScrollOffset(ScrollOffset(0, 1),
                                       mojom::blink::ScrollType::kProgrammatic);
  EXPECT_FALSE(fixed_background_div->ShouldDoFullPaintInvalidation());
  EXPECT_FALSE(fixed_background_div->BackgroundNeedsFullPaintInvalidation());
  EXPECT_TRUE(fixed_background_div->NeedsPaintPropertyUpdate());
  EXPECT_FALSE(GetLayoutView().ShouldDoFullPaintInvalidation());
  EXPECT_FALSE(GetLayoutView().BackgroundNeedsFullPaintInvalidation());
  EXPECT_FALSE(GetLayoutView().NeedsPaintPropertyUpdate());
}

TEST_P(MAYBE_PaintLayerScrollableAreaTest,
       ViewScrollWithFixedAttachmentBackgroundPreferCompositingToLCDText) {
  GetDocument().GetFrame()->GetSettings()->SetPreferCompositingToLCDTextEnabled(
      true);
  SetBodyInnerHTML(R"HTML(
    <style>
      html, #fixed-background {
        background: linear-gradient(black, white) fixed;
      }
      #fixed-background {
        width: 200px;
        height: 200px;
        overflow: scroll;
      }
    </style>
    <div id="fixed-background">
      <div style="height: 3000px"></div>
    </div>
    <div style="height: 3000px"></div>
  )HTML");

  EXPECT_EQ(kBackgroundPaintInBorderBoxSpace,
            GetLayoutView().GetBackgroundPaintLocation());
  auto* fixed_background_div = GetLayoutBoxByElementId("fixed-background");
  EXPECT_EQ(kBackgroundPaintInBorderBoxSpace,
            fixed_background_div->GetBackgroundPaintLocation());
  auto* div_scrollable_area = fixed_background_div->GetScrollableArea();
  auto* view_scrollable_area = GetLayoutView().GetScrollableArea();

  // Programmatically changing the view's scroll offset. Should invalidate all
  // objects with fixed attachment background except the layout view.
  view_scrollable_area->SetScrollOffset(
      ScrollOffset(0, 1), mojom::blink::ScrollType::kProgrammatic);
  EXPECT_TRUE(fixed_background_div->ShouldDoFullPaintInvalidation());
  EXPECT_TRUE(fixed_background_div->BackgroundNeedsFullPaintInvalidation());
  EXPECT_FALSE(fixed_background_div->NeedsPaintPropertyUpdate());
  EXPECT_FALSE(GetLayoutView().ShouldDoFullPaintInvalidation());
  EXPECT_FALSE(GetLayoutView().BackgroundNeedsFullPaintInvalidation());
  EXPECT_TRUE(GetLayoutView().NeedsPaintPropertyUpdate());
  UpdateAllLifecyclePhasesForTest();

  // Programmatically changing the div's scroll offset. Should invalidate the
  // scrolled div with fixed attachment background.
  div_scrollable_area->SetScrollOffset(ScrollOffset(0, 1),
                                       mojom::blink::ScrollType::kProgrammatic);
  EXPECT_TRUE(fixed_background_div->ShouldDoFullPaintInvalidation());
  EXPECT_TRUE(fixed_background_div->BackgroundNeedsFullPaintInvalidation());
  EXPECT_TRUE(fixed_background_div->NeedsPaintPropertyUpdate());
  EXPECT_FALSE(GetLayoutView().ShouldDoFullPaintInvalidation());
  EXPECT_FALSE(GetLayoutView().BackgroundNeedsFullPaintInvalidation());
  EXPECT_FALSE(GetLayoutView().NeedsPaintPropertyUpdate());
}

TEST_P(MAYBE_PaintLayerScrollableAreaTest, HitTestOverlayScrollbars) {
  SetBodyInnerHTML(R"HTML(
    <style>
    html, body {
      margin: 0;
    }
    #scroller {
      overflow: scroll;
      height: 100px;
      width: 100px;
    }
    #scrolled {
      width: 1000px;
      height: 1000px;
    }
    </style>
    <div id='scroller'><div id='scrolled'></div></div>
  )HTML");

  auto* scroller = GetLayoutObjectByElementId("scroller");
  auto* scrollable_area =
      To<LayoutBoxModelObject>(scroller)->GetScrollableArea();

  scrollable_area->SetScrollbarsHiddenIfOverlay(true);

  HitTestRequest hit_request(HitTestRequest::kMove | HitTestRequest::kReadOnly);
  HitTestLocation location(PhysicalOffset(95, 5));
  HitTestResult hit_result(hit_request, location);
  GetDocument().GetLayoutView()->HitTest(location, hit_result);
  EXPECT_EQ(hit_result.GetScrollbar(), nullptr);
  location = HitTestLocation(PhysicalOffset(5, 95));
  hit_result = HitTestResult(hit_request, location);
  GetDocument().GetLayoutView()->HitTest(location, hit_result);
  EXPECT_EQ(hit_result.GetScrollbar(), nullptr);

  scrollable_area->SetScrollbarsHiddenIfOverlay(false);

  location = HitTestLocation(PhysicalOffset(95, 5));
  hit_result = HitTestResult(hit_request, location);
  GetDocument().GetLayoutView()->HitTest(location, hit_result);
  EXPECT_EQ(hit_result.GetScrollbar(), scrollable_area->VerticalScrollbar());
  location = HitTestLocation(PhysicalOffset(5, 95));
  hit_result = HitTestResult(hit_request, location);
  GetDocument().GetLayoutView()->HitTest(location, hit_result);
  EXPECT_EQ(hit_result.GetScrollbar(), scrollable_area->HorizontalScrollbar());
}

TEST_P(MAYBE_PaintLayerScrollableAreaTest, CompositedStickyDescendant) {
  SetBodyInnerHTML(R"HTML(
    <div id=scroller style="overflow: scroll; width: 500px; height: 300px;
        will-change: transform">
      <div id=sticky style="top: 0px; position: sticky; background: green">
      </div>
      <div style="width: 10px; height: 700px; background: lightblue"></div>
    </div>
  )HTML");
  auto* scroller =
      To<LayoutBoxModelObject>(GetLayoutObjectByElementId("scroller"));
  auto* scrollable_area = scroller->GetScrollableArea();
  auto* sticky = To<LayoutBoxModelObject>(GetLayoutObjectByElementId("sticky"));

  EXPECT_EQ(&sticky->FirstFragment().LocalBorderBoxProperties().Transform(),
            sticky->FirstFragment().PaintProperties()->StickyTranslation());
  EXPECT_TRUE(sticky->FirstFragment()
                  .PaintProperties()
                  ->StickyTranslation()
                  ->IsIdentity());

  scrollable_area->SetScrollOffset(ScrollOffset(0, 50),
                                   mojom::blink::ScrollType::kUser);
  UpdateAllLifecyclePhasesForTest();

  EXPECT_EQ(gfx::Vector2dF(0, 50), sticky->FirstFragment()
                                       .PaintProperties()
                                       ->StickyTranslation()
                                       ->Translation2D());
}

TEST_P(MAYBE_PaintLayerScrollableAreaTest, StickyPositionUseCounter) {
  SetBodyInnerHTML(R"HTML(
    <div style="overflow: scroll; width: 500px; height: 300px;">
      <div id=test></div>
      <div id=forcescroll style="width: 10px; height: 700px;"></div>
    </div>
  )HTML");
  EXPECT_FALSE(GetDocument().IsUseCounted(WebFeature::kPositionSticky));

  auto* test = GetElementById("test");
  test->setAttribute(html_names::kStyleAttr, "position: sticky;");
  UpdateAllLifecyclePhasesForTest();
  EXPECT_FALSE(GetDocument().IsUseCounted(WebFeature::kPositionSticky));

  test->setAttribute(html_names::kStyleAttr, "top: 0; position: sticky;");
  UpdateAllLifecyclePhasesForTest();
  EXPECT_TRUE(GetDocument().IsUseCounted(WebFeature::kPositionSticky));
}

// Delayed scroll offset clamping should not crash. https://crbug.com/842495
TEST_P(MAYBE_PaintLayerScrollableAreaTest,
       IgnoreDelayedScrollOnDestroyedLayer) {
  SetBodyInnerHTML(R"HTML(
    <div id=scroller style="overflow: scroll; width: 200px; height: 200px;">
      <div style="height: 1000px;"></div>
    </div>
  )HTML");
  Element* scroller = GetDocument().getElementById("scroller");
  {
    PaintLayerScrollableArea::DelayScrollOffsetClampScope scope;
    PaintLayerScrollableArea::DelayScrollOffsetClampScope::SetNeedsClamp(
        scroller->GetLayoutBox()->GetScrollableArea());
    scroller->SetInlineStyleProperty(CSSPropertyID::kDisplay,
                                     CSSValueID::kNone);
    UpdateAllLifecyclePhasesForTest();
  }
}

TEST_P(MAYBE_PaintLayerScrollableAreaTest, ScrollbarMaximum) {
  SetBodyInnerHTML(R"HTML(
    <style>
    #spacer {
      height: 17.984375px;
    }
    #scroller {
      border-top: 0.328125px solid gray;
      border-bottom: 0.328125px solid gray;
      height:149.34375px;
      width: 100px;
      overflow-y:auto;
    }
    #content {
      height: 156.578125px;
    }
    </style>
    <div id='spacer'></div>
    <div id='scroller'>
      <div id='content'></div>
    </div>
  )HTML");

  LayoutBox* scroller = GetLayoutBoxByElementId("scroller");
  PaintLayerScrollableArea* scrollable_area = scroller->GetScrollableArea();
  Scrollbar* scrollbar = scrollable_area->VerticalScrollbar();

  scrollable_area->ScrollBy(ScrollOffset(0, 1000),
                            mojom::blink::ScrollType::kProgrammatic);
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(scrollbar->CurrentPos(), scrollbar->Maximum());
}

TEST_P(MAYBE_PaintLayerScrollableAreaTest, ScrollingBackgroundVisualRect) {
  SetBodyInnerHTML(R"HTML(
    <style>
      ::-webkit-scrollbar { display: none; }
      #scroller {
        width: 100.7px;
        height: 100.4px;
        overflow: scroll;
        border-top: 2.6px solid blue;
        border-left: 2.4px solid blue;
        will-change: transform;
      }
      #content {
        width: 50.7px;
        height: 200.4px;
      }
    </style>
    <div id="scroller">
      <div id="content"></div>
    </div>
  )HTML");

  EXPECT_EQ(gfx::Rect(2, 3, 101, 200),
            GetLayoutBoxByElementId("scroller")
                ->GetScrollableArea()
                ->ScrollingBackgroundVisualRect(PhysicalOffset()));
}

TEST_P(MAYBE_PaintLayerScrollableAreaTest, RtlScrollOriginSnapping) {
  SetBodyInnerHTML(R"HTML(
    <style>
      #container {
        direction: rtl;
        display: flex;
      }
      #scroller {
        width: 100%;
        height: 100px;
        overflow: hidden;
      }
      #scroller-content {
        width: 200%;
        height: 200px;
      }
    </style>
    <div id="container">
      <div id="first-child" style="flex:1; display:none"></div>
      <div style="flex:2.2">
        <div id="scroller">
          <div id ="scroller-content"></div>
        </div>
      </div>
    </div>
  )HTML");

  // Test that scroll origin is snapped such that maximum scroll offset is
  // always zero for an rtl block.

  GetFrame().View()->Resize(795, 600);
  UpdateAllLifecyclePhasesForTest();
  LayoutBox* scroller = GetLayoutBoxByElementId("scroller");
  PaintLayerScrollableArea* scrollable_area = scroller->GetScrollableArea();
  EXPECT_EQ(scrollable_area->MaximumScrollOffsetInt(), gfx::Vector2d(0, 100));

  Element* first_child = GetElementById("first-child");
  first_child->RemoveInlineStyleProperty(CSSPropertyID::kDisplay);
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(scrollable_area->MaximumScrollOffsetInt(), gfx::Vector2d(0, 100));
}

TEST_P(MAYBE_PaintLayerScrollableAreaTest, ShowCustomResizerInTextarea) {
  GetPage().GetSettings().SetTextAreasAreResizable(true);
  SetBodyInnerHTML(R"HTML(
    <!doctype HTML>
    <style>
      textarea {
        width: 200px;
        height: 100px;
      }
      ::-webkit-resizer {
        background-color: red;
      }
    </style>
    <textarea id="target"></textarea>
  )HTML");

  const auto* paint_layer = GetPaintLayerByElementId("target");
  ASSERT_TRUE(paint_layer);

  EXPECT_NE(paint_layer->GetScrollableArea()->Resizer(), nullptr);
}

TEST_P(MAYBE_PaintLayerScrollableAreaTest,
       ApplyPendingHistoryRestoreScrollOffsetTwice) {
  GetPage().GetSettings().SetTextAreasAreResizable(true);
  SetBodyInnerHTML(R"HTML(
    <!doctype HTML>
    <div id="target" style="overflow: scroll; width: 50px; height: 50px">
      <div style="width: 50px; height: 500px">
      </div>
    </div>
  )HTML");

  const auto* paint_layer = GetPaintLayerByElementId("target");
  auto* scrollable_area = paint_layer->GetScrollableArea();

  HistoryItem::ViewState view_state;
  view_state.scroll_offset_ = ScrollOffset(0, 100);
  scrollable_area->SetPendingHistoryRestoreScrollOffset(view_state, true);
  scrollable_area->ApplyPendingHistoryRestoreScrollOffset();
  EXPECT_EQ(ScrollOffset(0, 100), scrollable_area->GetScrollOffset());

  scrollable_area->SetScrollOffset(ScrollOffset(0, 50),
                                   mojom::blink::ScrollType::kUser);

  // The second call to ApplyPendingHistoryRestoreScrollOffset should
  // do nothing, since the history was already restored.
  scrollable_area->ApplyPendingHistoryRestoreScrollOffset();
  EXPECT_EQ(ScrollOffset(0, 50), scrollable_area->GetScrollOffset());
}

// Test that a trivial 3D transform results in composited scrolling.
TEST_P(MAYBE_PaintLayerScrollableAreaTest, CompositeWithTrivial3D) {
  SetBodyInnerHTML(R"HTML(
    <style>
      #scroller {
        width: 100px;
        height: 100px;
        overflow: scroll;
        transform: translateZ(0);
      }
      #scrolled {
        width: 200px;
        height: 200px;
      }
    </style>
    <div id="scroller">
      <div id="scrolled"></div>
    </div>
  )HTML");

  EXPECT_TRUE(UsesCompositedScrolling(GetLayoutObjectByElementId("scroller")));
}

class PaintLayerScrollableAreaTestLowEndPlatform
    : public TestingPlatformSupport {
 public:
  bool IsLowEndDevice() override { return true; }
};

// Test that a trivial 3D transform results in composited scrolling even on
// low-end devices that may not composite trivial 3D transforms.
TEST_P(MAYBE_PaintLayerScrollableAreaTest, LowEndCompositeWithTrivial3D) {
  ScopedTestingPlatformSupport<PaintLayerScrollableAreaTestLowEndPlatform>
      platform;
  SetBodyInnerHTML(R"HTML(
    <style>
      #scroller {
        width: 100px;
        height: 100px;
        overflow: scroll;
        transform: translateZ(0);
      }
      #scrolled {
        width: 200px;
        height: 200px;
      }
    </style>
    <div id="scroller">
      <div id="scrolled"></div>
    </div>
  )HTML");

  EXPECT_TRUE(UsesCompositedScrolling(GetLayoutObjectByElementId("scroller")));
}

TEST_P(MAYBE_PaintLayerScrollableAreaTest, SetSnapContainerDataNeedsUpdate) {
  SetBodyInnerHTML(R"HTML(
    <style>
    .scroller {
      overflow: scroll;
      height: 200px;
      width: 200px;
    }
    </style>
    <div id='first_scroller' class='scroller'>
      <div style='height: 2000px;'></div>
    </div>
    <div id='second_scroller' class='scroller'>
      <div style='height: 2000px;'></div>
    </div>
  )HTML");

  auto* first_scroller = GetLayoutObjectByElementId("first_scroller");
  auto* first_scrollable_area =
      To<LayoutBoxModelObject>(first_scroller)->GetScrollableArea();

  auto* second_scroller = GetLayoutObjectByElementId("second_scroller");
  auto* second_scrollable_area =
      To<LayoutBoxModelObject>(second_scroller)->GetScrollableArea();

  EXPECT_EQ(&first_scroller->GetDocument().GetSnapCoordinator(),
            &second_scroller->GetDocument().GetSnapCoordinator());

  auto& snap_coordinator = first_scroller->GetDocument().GetSnapCoordinator();
  EXPECT_FALSE(snap_coordinator.AnySnapContainerDataNeedsUpdate());

  // SnapCoordinator needs to update all its snap containers if one of them asks
  // for an update.
  first_scrollable_area->SetSnapContainerDataNeedsUpdate(true);
  EXPECT_TRUE(snap_coordinator.AnySnapContainerDataNeedsUpdate());

  // SnapCoordinator still needs to update all its snap containers even if one
  // of them asks not to.
  second_scrollable_area->SetSnapContainerDataNeedsUpdate(false);
  EXPECT_TRUE(snap_coordinator.AnySnapContainerDataNeedsUpdate());

  first_scrollable_area->SetSnapContainerDataNeedsUpdate(false);
  EXPECT_TRUE(snap_coordinator.AnySnapContainerDataNeedsUpdate());

  snap_coordinator.UpdateAllSnapContainerDataIfNeeded();
  EXPECT_FALSE(snap_coordinator.AnySnapContainerDataNeedsUpdate());
}

class ScrollTimelineForTest : public ScrollTimeline {
 public:
  ScrollTimelineForTest(Document* document, Element* scroll_source)
      : ScrollTimeline(document,
                       ScrollTimeline::ReferenceType::kSource,
                       scroll_source,
                       ScrollDirection::kVertical),
        invalidated_(false) {}
  void Invalidate() override {
    ScrollTimeline::Invalidate();
    invalidated_ = true;
  }
  bool Invalidated() const { return invalidated_; }
  void ResetInvalidated() { invalidated_ = false; }
  void Trace(Visitor* visitor) const override {
    ScrollTimeline::Trace(visitor);
  }

 private:
  bool invalidated_;
};

// Verify that scrollable area changes invalidate scroll timeline.
TEST_P(MAYBE_PaintLayerScrollableAreaTest, ScrollTimelineInvalidation) {
  SetBodyInnerHTML(R"HTML(
    <style>
      #scroller { overflow: scroll; width: 100px; height: 100px; }
      #spacer { height: 1000px; }
    </style>
    <div id='scroller'>
      <div id ='spacer'></div>
    </div>
  )HTML");

  auto* scroller =
      To<LayoutBoxModelObject>(GetLayoutObjectByElementId("scroller"));
  PaintLayerScrollableArea* scrollable_area = scroller->GetScrollableArea();
  scrollable_area->SetScrollOffset(ScrollOffset(0, 20),
                                   mojom::blink::ScrollType::kProgrammatic);
  Element* scroller_element = GetElementById("scroller");
  ScrollTimelineForTest* scroll_timeline =
      MakeGarbageCollected<ScrollTimelineForTest>(&GetDocument(),
                                                  scroller_element);
  scroll_timeline->ResetInvalidated();
  // Verify that changing scroll offset invalidates scroll timeline.
  scrollable_area->SetScrollOffset(ScrollOffset(0, 30),
                                   mojom::blink::ScrollType::kProgrammatic);
  EXPECT_TRUE(scroll_timeline->Invalidated());
  scroll_timeline->ResetInvalidated();

  // Verify that changing scroller size invalidates scroll timeline.
  scroller_element->setAttribute(html_names::kStyleAttr, "height:110px;");
  UpdateAllLifecyclePhasesForTest();
  EXPECT_TRUE(scroll_timeline->Invalidated());
  scroll_timeline->ResetInvalidated();

  // Verify that changing content area size invalidates scroll timeline.
  Element* spacer_element = GetElementById("spacer");
  spacer_element->setAttribute(html_names::kStyleAttr, "height:900px;");
  UpdateAllLifecyclePhasesForTest();
  EXPECT_TRUE(scroll_timeline->Invalidated());
  scroll_timeline->ResetInvalidated();
}

TEST_P(MAYBE_PaintLayerScrollableAreaTest,
       RootScrollbarShouldUseParentOfOverscrollNodeAsTransformNode) {
  auto& document = GetDocument();
  document.GetFrame()->GetSettings()->SetPreferCompositingToLCDTextEnabled(
      true);
  SetBodyInnerHTML(R"HTML(
    <style>
    ::-webkit-scrollbar {
      width: 12px;
      background: darkblue;
    }
    ::-webkit-scrollbar-thumb {
      background: white;
    }
    #scroller {
      height: 100px;
      overflow-y: scroll;
    }
    .big {
      height: 1000px;
    }
    </style>

    <div class='big'></div>
    <div id='scroller'>
      <div class='big'></div>
    </div>
  )HTML");

  {
    const auto* root_scrollable = document.View()->LayoutViewport();
    const auto& visual_viewport =
        document.View()->GetPage()->GetVisualViewport();

    const auto& paint_chunks = ContentPaintChunks();
    bool found_root_scrollbar = false;
    for (const auto& chunk : paint_chunks) {
      if (chunk.id == PaintChunk::Id(root_scrollable->VerticalScrollbar()->Id(),
                                     DisplayItem::kScrollbarHitTest)) {
        EXPECT_EQ(
            &chunk.properties.Transform(),
            visual_viewport.GetOverscrollElasticityTransformNode()->Parent());
        found_root_scrollbar = true;
      }
    }
    EXPECT_TRUE(found_root_scrollbar);
  }

  // Non root scrollbar should use scroller's transform node.
  {
    PaintLayer* scroller_layer = GetPaintLayerByElementId("scroller");
    PaintLayerScrollableArea* scrollable_area =
        scroller_layer->GetScrollableArea();
    ASSERT_TRUE(scrollable_area);

    auto paint_properties = scroller_layer->GetLayoutObject()
                                .FirstFragment()
                                .LocalBorderBoxProperties();

    const auto& paint_chunks = ContentPaintChunks();
    bool found_subscroller_scrollbar = false;
    for (const auto& chunk : paint_chunks) {
      if (chunk.id == PaintChunk::Id(scrollable_area->VerticalScrollbar()->Id(),
                                     DisplayItem::kScrollbarHitTest)) {
        EXPECT_EQ(&chunk.properties.Transform(), &paint_properties.Transform());

        found_subscroller_scrollbar = true;
      }
    }
    EXPECT_TRUE(found_subscroller_scrollbar);
  }
}

TEST_P(MAYBE_PaintLayerScrollableAreaTest,
       ResizeSmallerToBeScrollableWithResizerAndStackedChild) {
  USE_NON_OVERLAY_SCROLLBARS();

  SetBodyInnerHTML(R"HTML(
    <div id="scroller"
         style="overflow: auto; width: 150px; height: 100px; resize: both">
      <div style="width: 149px; height: 98px; position: relative"></div>
    </div>
  )HTML");

  auto* scroller = GetDocument().getElementById("scroller");
  auto* scrollable_area = scroller->GetLayoutBox()->GetScrollableArea();
  ASSERT_TRUE(scrollable_area);
  EXPECT_FALSE(scrollable_area->HasScrollbar());
  // The resizer needs to be painted above the stacked child.
  EXPECT_TRUE(scrollable_area->HasOverlayOverflowControls());
  EXPECT_TRUE(
      scroller->GetLayoutBox()->Layer()->NeedsReorderOverlayOverflowControls());

  // Shrink the scroller, and it becomes scrollable.
  scroller->SetInlineStyleProperty(CSSPropertyID::kWidth, "140px");
  UpdateAllLifecyclePhasesForTest();
  ASSERT_TRUE(scrollable_area->HasScrollbar());
  ASSERT_FALSE(scrollable_area->HorizontalScrollbar()->IsOverlayScrollbar());
  // Because there is non-overlay scrollbar, the resizer on longer overlaps
  // with the contents, so no need to overlay.
  EXPECT_FALSE(scrollable_area->HasOverlayOverflowControls());
  EXPECT_FALSE(
      scroller->GetLayoutBox()->Layer()->NeedsReorderOverlayOverflowControls());
}

TEST_P(MAYBE_PaintLayerScrollableAreaTest, RemoveAddResizerWithoutScrollbars) {
  SetBodyInnerHTML(R"HTML(
    <div id="target"
         style="width: 100px; height: 100px; resize: both; overflow: hidden">
      <div style="position: relative; height: 50px"></div>
    </div>
  )HTML");

  auto* target = GetDocument().getElementById("target");
  auto* scrollable_area = target->GetLayoutBox()->GetScrollableArea();
  ASSERT_TRUE(scrollable_area);
  EXPECT_FALSE(scrollable_area->HasScrollbar());
  EXPECT_TRUE(scrollable_area->HasOverlayOverflowControls());
  EXPECT_TRUE(scrollable_area->Layer()->NeedsReorderOverlayOverflowControls());

  target->RemoveInlineStyleProperty(CSSPropertyID::kResize);
  LOG(ERROR) << "REMOVE";
  UpdateAllLifecyclePhasesForTest();
  ASSERT_EQ(scrollable_area, target->GetLayoutBox()->GetScrollableArea());
  ASSERT_FALSE(scrollable_area->HasScrollbar());
  EXPECT_FALSE(scrollable_area->HasOverlayOverflowControls());
  EXPECT_FALSE(scrollable_area->Layer()->NeedsReorderOverlayOverflowControls());

  target->SetInlineStyleProperty(CSSPropertyID::kResize, "both");
  LOG(ERROR) << "ADD";
  UpdateAllLifecyclePhasesForTest();
  ASSERT_EQ(scrollable_area, target->GetLayoutBox()->GetScrollableArea());
  ASSERT_FALSE(scrollable_area->HasScrollbar());
  EXPECT_TRUE(scrollable_area->HasOverlayOverflowControls());
  EXPECT_TRUE(scrollable_area->Layer()->NeedsReorderOverlayOverflowControls());
}

}  // namespace blink
