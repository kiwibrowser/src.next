// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/paint/cull_rect_updater.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/paint/paint_controller_paint_test.h"
#include "third_party/blink/renderer/core/paint/paint_layer.h"

namespace blink {

class CullRectUpdaterTest : public PaintControllerPaintTest {
 protected:
  CullRect GetCullRect(const char* id) {
    return GetLayoutObjectByElementId(id)->FirstFragment().GetCullRect();
  }

  CullRect GetCullRect(const PaintLayer& layer) {
    return layer.GetLayoutObject().FirstFragment().GetCullRect();
  }

  CullRect GetContentsCullRect(const char* id) {
    return GetLayoutObjectByElementId(id)
        ->FirstFragment()
        .GetContentsCullRect();
  }
};

INSTANTIATE_TEST_SUITE_P(All,
                         CullRectUpdaterTest,
                         ::testing::Values(kScrollUpdateOptimizations |
                                               kScrollUnification,
                                           kScrollUpdateOptimizations));

TEST_P(CullRectUpdaterTest, SimpleCullRect) {
  SetBodyInnerHTML(R"HTML(
    <div id='target'
         style='width: 200px; height: 200px; position: relative'>
    </div>
  )HTML");

  EXPECT_EQ(gfx::Rect(0, 0, 800, 600), GetCullRect("target").Rect());
}

TEST_P(CullRectUpdaterTest, TallLayerCullRect) {
  SetBodyInnerHTML(R"HTML(
    <div id='target'
         style='width: 200px; height: 10000px; position: relative'>
    </div>
  )HTML");

  // Viewport rect (0, 0, 800, 600) expanded by 4000 for scrolling then clipped
  // by the contents rect.
  EXPECT_EQ(gfx::Rect(0, 0, 800, 4600), GetCullRect("target").Rect());
}

TEST_P(CullRectUpdaterTest, WideLayerCullRect) {
  SetBodyInnerHTML(R"HTML(
    <div id='target'
         style='width: 10000px; height: 200px; position: relative'>
    </div>
  )HTML");

  // Same as TallLayerCullRect.
  EXPECT_EQ(gfx::Rect(0, 0, 4800, 600), GetCullRect("target").Rect());
}

TEST_P(CullRectUpdaterTest, VerticalRightLeftWritingModeDocument) {
  SetBodyInnerHTML(R"HTML(
    <style>
      html { writing-mode: vertical-rl; }
      body { margin: 0; }
    </style>
    <div id='target' style='width: 10000px; height: 200px; position: relative'>
    </div>
  )HTML");

  GetDocument().View()->LayoutViewport()->SetScrollOffset(
      ScrollOffset(-5000, 0), mojom::blink::ScrollType::kProgrammatic);
  UpdateAllLifecyclePhasesForTest();

  // A scroll by -5000px is equivalent to a scroll by (10000 - 5000 - 800)px =
  // 4200px in non-RTL mode. Expanding the resulting rect by 4000px in each
  // direction and clipping by the contents rect yields this result.
  EXPECT_EQ(gfx::Rect(200, 0, 8800, 600), GetCullRect("target").Rect());
}

TEST_P(CullRectUpdaterTest, ScaledCullRect) {
  SetBodyInnerHTML(R"HTML(
    <style>body { margin: 0 }</style>
    <div id='target'
         style='width: 200px; height: 300px; will-change: transform;
                transform: scaleX(2) scaleY(0.75); transform-origin: 0 0'>
    </div>
  )HTML");

  // The expansion is 4000 / max(scaleX, scaleY).
  EXPECT_EQ(gfx::Rect(-2000, -2000, 4400, 4800), GetCullRect("target").Rect());
}

TEST_P(CullRectUpdaterTest, ScaledCullRectUnderCompositedScroller) {
  SetBodyInnerHTML(R"HTML(
    <div style='width: 200px; height: 300px; overflow: scroll; background: blue;
                transform: scaleX(2) scaleY(0.75)'>
      <div id='target' style='height: 400px; position: relative'></div>
      <div style='width: 10000px; height: 10000px'></div>
    </div>
  )HTML");

  // The expansion is 4000 / max(scaleX, scaleY).
  EXPECT_EQ(gfx::Rect(0, 0, 2200, 2300), GetCullRect("target").Rect());
}

TEST_P(CullRectUpdaterTest, ScaledAndRotatedCullRect) {
  SetBodyInnerHTML(R"HTML(
    <div id='target'
         style='width: 200px; height: 300px; will-change: transform;
                transform: scaleX(3) scaleY(0.5) rotateZ(45deg)'>
    </div>
  )HTML");

  // The expansion 6599 is 4000 * max_dimension(1x1 rect projected from screen
  // to local).
  EXPECT_EQ(gfx::Rect(-6748, -6836, 14236, 14236),
            GetCullRect("target").Rect());
}

TEST_P(CullRectUpdaterTest, ScaledAndRotatedCullRectUnderCompositedScroller) {
  SetBodyInnerHTML(R"HTML(
    <div style='width: 200px; height: 300px; overflow: scroll; background: blue;
                transform: scaleX(3) scaleY(0.5) rotateZ(45deg)'>
      <div id='target' style='height: 400px; position: relative;
               will-change: transform'></div>
      <div style='width: 10000px; height: 10000px'></div>
    </div>
  )HTML");

  // The expansion 6599 is 4000 * max_dimension(1x1 rect projected from screen
  // to local).
  EXPECT_EQ(gfx::Rect(0, 0, 6799, 6899), GetCullRect("target").Rect());
  EXPECT_EQ(gfx::Rect(0, 0, 6799, 6899), GetContentsCullRect("target").Rect());
}

// This is a testcase for https://crbug.com/1227907 where repeated cull rect
// updates are expensive on the motionmark microbenchmark.
TEST_P(CullRectUpdaterTest, OptimizeNonCompositedTransformUpdate) {
  SetBodyInnerHTML(R"HTML(
    <style>
      #target {
        width: 50px;
        height: 50px;
        background: green;
        transform: translate(-8px, -8px);
      }
    </style>
    <div id='target'></div>
  )HTML");

  // The cull rect should be correctly calculated on first paint.
  EXPECT_EQ(gfx::Rect(0, 0, 800, 600), GetCullRect("target").Rect());

  // On subsequent paints, fall back to an infinite cull rect.
  GetDocument().getElementById("target")->setAttribute(
      html_names::kStyleAttr, "transform: rotate(10deg);");
  UpdateAllLifecyclePhasesForTest();
  EXPECT_TRUE(GetCullRect("target").IsInfinite());
}

TEST_P(CullRectUpdaterTest, 3DRotated90DegreesCullRect) {
  SetBodyInnerHTML(R"HTML(
    <div id='target'
         style='width: 200px; height: 300px; will-change: transform;
                transform: rotateY(90deg)'>
    </div>
  )HTML");

  EXPECT_TRUE(GetCullRect("target").Rect().Contains(gfx::Rect(0, 0, 200, 300)));
}

TEST_P(CullRectUpdaterTest, 3DRotatedNear90DegreesCullRect) {
  SetBodyInnerHTML(R"HTML(
    <div id='target'
         style='width: 200px; height: 300px; will-change: transform;
                transform: rotateY(89.9999deg)'>
    </div>
  )HTML");

  EXPECT_TRUE(GetCullRect("target").Rect().Contains(gfx::Rect(0, 0, 200, 300)));
}

TEST_P(CullRectUpdaterTest, PerspectiveCullRect) {
  SetBodyInnerHTML(R"HTML(
    <div id=target style='transform: perspective(1000px) rotateX(-100deg);'>
      <div style='width: 2000px; height: 3000px></div>
    </div>
  )HTML");

  EXPECT_TRUE(
      GetCullRect("target").Rect().Contains(gfx::Rect(0, 0, 2000, 3000)));
}

TEST_P(CullRectUpdaterTest, 3D60DegRotatedTallCullRect) {
  SetBodyInnerHTML(R"HTML(
    <style>body { margin: 0 }</style>
    <div id='target'
         style='width: 200px; height: 10000px; transform: rotateY(60deg)'>
    </div>
  )HTML");

  // The cull rect is expanded in the y direction for the root scroller, and
  // x direction for |target| itself.
  EXPECT_EQ(gfx::Rect(-4100, 0, 9600, 4600), GetCullRect("target").Rect());
}

TEST_P(CullRectUpdaterTest, FixedPositionInNonScrollableViewCullRect) {
  SetBodyInnerHTML(R"HTML(
    <div id='target' style='width: 1000px; height: 2000px;
                            position: fixed; top: 100px; left: 200px;'>
    </div>
  )HTML");

  EXPECT_EQ(gfx::Rect(-200, -100, 800, 600), GetCullRect("target").Rect());
}

TEST_P(CullRectUpdaterTest, FixedPositionInScrollableViewCullRect) {
  SetBodyInnerHTML(R"HTML(
    <div id='target' style='width: 1000px; height: 2000px;
                            position: fixed; top: 100px; left: 200px;'>
    </div>
    <div style='height: 3000px'></div>
  )HTML");

  EXPECT_EQ(gfx::Rect(-200, -100, 800, 600), GetCullRect("target").Rect());
}

TEST_P(CullRectUpdaterTest, LayerOffscreenNearCullRect) {
  SetBodyInnerHTML(R"HTML(
    <div id='target'
         style='width: 200px; height: 300px; will-change: transform;
                position: absolute; top: 3000px; left: 0px;'>
    </div>
  )HTML");

  auto cull_rect = GetCullRect("target").Rect();
  EXPECT_TRUE(cull_rect.Contains(gfx::Rect(0, 0, 200, 300)));
}

TEST_P(CullRectUpdaterTest, LayerOffscreenFarCullRect) {
  SetBodyInnerHTML(R"HTML(
    <div id='target'
         style='width: 200px; height: 300px; will-change: transform;
                position: absolute; top: 9000px'>
    </div>
  )HTML");

  // The layer is too far away from the viewport.
  EXPECT_FALSE(
      GetCullRect("target").Rect().Intersects(gfx::Rect(0, 0, 200, 300)));
}

TEST_P(CullRectUpdaterTest, ScrollingLayerCullRect) {
  SetBodyInnerHTML(R"HTML(
    <style>
      div::-webkit-scrollbar { width: 5px; }
    </style>
    <div style='width: 200px; height: 200px; overflow: scroll;
                background: blue'>
      <div id='target'
           style='width: 100px; height: 10000px; position: relative'>
      </div>
    </div>
  )HTML");

  // In screen space, the scroller is (8, 8, 195, 193) (because of overflow clip
  // of 'target', scrollbar and root margin).
  // Applying the viewport clip of the root has no effect because
  // the clip is already small. Mapping it down into the graphics layer
  // space yields (0, 0, 195, 193). This is then expanded by 4000px and clipped
  // by the contents rect.
  EXPECT_EQ(gfx::Rect(0, 0, 195, 4193), GetCullRect("target").Rect());
}

TEST_P(CullRectUpdaterTest, NonCompositedScrollingLayerCullRect) {
  GetDocument().GetSettings()->SetPreferCompositingToLCDTextEnabled(false);
  SetBodyInnerHTML(R"HTML(
    <style>
      div::-webkit-scrollbar { width: 5px; }
    </style>
    <div style='width: 200px; height: 200px; overflow: scroll'>
      <div id='target'
           style='width: 100px; height: 10000px; position: relative'>
      </div>
    </div>
  )HTML");

  // See ScrollingLayerCullRect for the calculation.
  EXPECT_EQ(gfx::Rect(0, 0, 195, 193), GetCullRect("target").Rect());
}

TEST_P(CullRectUpdaterTest, ClippedBigLayer) {
  SetBodyInnerHTML(R"HTML(
    <div style='width: 1px; height: 1px; overflow: hidden'>
      <div id='target'
           style='width: 10000px; height: 10000px; position: relative'>
      </div>
    </div>
  )HTML");

  EXPECT_EQ(gfx::Rect(8, 8, 1, 1), GetCullRect("target").Rect());
}

TEST_P(CullRectUpdaterTest, TallScrolledLayerCullRect) {
  SetBodyInnerHTML(R"HTML(
    <div id='target' style='width: 200px; height: 12000px; position: relative'>
    </div>
  )HTML");

  // Viewport rect (0, 0, 800, 600) expanded by 4000 for scrolling then clipped
  // by the contents rect.
  EXPECT_EQ(gfx::Rect(0, 0, 800, 4600), GetCullRect("target").Rect());

  GetDocument().View()->LayoutViewport()->SetScrollOffset(
      ScrollOffset(0, 4000), mojom::blink::ScrollType::kProgrammatic);
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(gfx::Rect(0, 0, 800, 8600), GetCullRect("target").Rect());

  GetDocument().View()->LayoutViewport()->SetScrollOffset(
      ScrollOffset(0, 4500), mojom::blink::ScrollType::kProgrammatic);
  UpdateAllLifecyclePhasesForTest();
  // Used the previous cull rect because the scroll amount is small.
  EXPECT_EQ(gfx::Rect(0, 0, 800, 8600), GetCullRect("target").Rect());

  GetDocument().View()->LayoutViewport()->SetScrollOffset(
      ScrollOffset(0, 4600), mojom::blink::ScrollType::kProgrammatic);
  UpdateAllLifecyclePhasesForTest();
  // Used new cull rect.
  EXPECT_EQ(gfx::Rect(0, 600, 800, 8600), GetCullRect("target").Rect());
}

TEST_P(CullRectUpdaterTest, WholeDocumentCullRect) {
  GetDocument().GetSettings()->SetPreferCompositingToLCDTextEnabled(true);
  GetDocument().GetSettings()->SetMainFrameClipsContent(false);
  SetBodyInnerHTML(R"HTML(
    <style>
      div { background: blue; }
      ::-webkit-scrollbar { display: none; }
    </style>
    <div id='relative'
         style='width: 200px; height: 10000px; position: relative'>
    </div>
    <div id='fixed' style='width: 200px; height: 200px; position: fixed'>
    </div>
    <div id='scroll' style='width: 200px; height: 200px; overflow: scroll'>
      <div id='below-scroll' style='height: 5000px; position: relative'></div>
      <div style='height: 200px'>Should not paint</div>
    </div>
    <div id='normal' style='width: 200px; height: 200px'></div>
  )HTML");

  // Viewport clipping is disabled.
  EXPECT_TRUE(GetCullRect(*GetLayoutView().Layer()).IsInfinite());
  EXPECT_TRUE(GetCullRect("relative").IsInfinite());
  EXPECT_TRUE(GetCullRect("fixed").IsInfinite());
  EXPECT_TRUE(GetCullRect("scroll").IsInfinite());

  // Cull rect is normal for contents below scroll other than the viewport.
  EXPECT_EQ(gfx::Rect(0, 0, 200, 4200), GetCullRect("below-scroll").Rect());

  EXPECT_EQ(7u, ContentDisplayItems().size());
}

TEST_P(CullRectUpdaterTest, FixedPositionUnderClipPath) {
  GetDocument().View()->Resize(800, 600);
  SetBodyInnerHTML(R"HTML(
    <div style="height: 100vh"></div>
    <div style="width: 100px; height: 100px; clip-path: inset(0 0 0 0)">
      <div id="fixed" style="position: fixed; top: 0; left: 0; width: 1000px;
                             height: 1000px"></div>
    </div>
  )HTML");

  EXPECT_EQ(gfx::Rect(0, 0, 800, 600), GetCullRect("fixed").Rect());

  GetDocument().GetFrame()->DomWindow()->scrollTo(0, 1000);
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(gfx::Rect(0, 0, 800, 600), GetCullRect("fixed").Rect());

  GetDocument().View()->Resize(800, 1000);
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(gfx::Rect(0, 0, 800, 1000), GetCullRect("fixed").Rect());
}

TEST_P(CullRectUpdaterTest, FixedPositionUnderClipPathWillChangeTransform) {
  GetDocument().View()->Resize(800, 600);
  SetBodyInnerHTML(R"HTML(
    <div style="height: 100vh"></div>
    <div style="width: 100px; height: 100px; clip-path: inset(0 0 0 0)">
      <div id="fixed" style="position: fixed; top: 0; left: 0; width: 1000px;
                             height: 1000px; will-change: transform"></div>
    </div>
  )HTML");

  EXPECT_EQ(gfx::Rect(-4000, -4000, 8800, 8600), GetCullRect("fixed").Rect());

  GetDocument().GetFrame()->DomWindow()->scrollTo(0, 1000);
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(gfx::Rect(-4000, -4000, 8800, 8600), GetCullRect("fixed").Rect());

  GetDocument().View()->Resize(800, 2000);
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(gfx::Rect(-4000, -4000, 8800, 10000), GetCullRect("fixed").Rect());
}

TEST_P(CullRectUpdaterTest, AbsolutePositionUnderNonContainingStackingContext) {
  GetDocument().GetSettings()->SetPreferCompositingToLCDTextEnabled(false);
  SetBodyInnerHTML(R"HTML(
    <div id="scroller" style="width: 200px; height: 200px; overflow: auto;
                              position: relative">
      <div style="height: 0; overflow: hidden; opacity: 0.5; margin: 250px">
        <div id="absolute"
             style="width: 100px; height: 100px; position: absolute;
                    background: green"></div>
      </div>
    </div>
  )HTML");

  EXPECT_EQ(gfx::Rect(0, 0, 200, 200), GetCullRect("absolute").Rect());

  GetDocument().getElementById("scroller")->scrollTo(200, 200);
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(RuntimeEnabledFeatures::LayoutNGEnabled()
                ? gfx::Rect(200, 200, 200, 200)
                : gfx::Rect(150, 200, 200, 200),
            GetCullRect("absolute").Rect());
}

TEST_P(CullRectUpdaterTest, StackedChildOfNonStackingContextScroller) {
  SetBodyInnerHTML(R"HTML(
    <div id="scroller" style="width: 200px; height: 200px; overflow: auto;
                              background: white">
      <div id="child" style="height: 7000px; position: relative"></div>
    </div>
  )HTML");

  auto* scroller = GetDocument().getElementById("scroller");

  EXPECT_EQ(gfx::Rect(0, 0, 200, 4200), GetContentsCullRect("scroller").Rect());
  EXPECT_EQ(gfx::Rect(0, 0, 200, 4200), GetCullRect("child").Rect());

  for (int i = 1000; i < 7000; i += 1000) {
    scroller->scrollTo(0, i);
    UpdateAllLifecyclePhasesForTest();
  }
  // When scrolled to 3800, the cull rect covers the whole scrolling contents.
  // Then we use this full cull rect on further scroll to avoid repaint.
  EXPECT_EQ(gfx::Rect(0, 0, 200, 7000), GetContentsCullRect("scroller").Rect());
  EXPECT_EQ(gfx::Rect(0, 0, 200, 7000), GetCullRect("child").Rect());

  // The full cull rect still applies when the scroller scrolls to the top.
  scroller->scrollTo(0, 0);
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(gfx::Rect(0, 0, 200, 7000), GetContentsCullRect("scroller").Rect());
  EXPECT_EQ(gfx::Rect(0, 0, 200, 7000), GetCullRect("child").Rect());

  // CullRectUpdater won't update |child|'s cull rect even it needs repaint
  // because its container's cull rect doesn't change.
  GetPaintLayerByElementId("child")->SetNeedsRepaint();
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(gfx::Rect(0, 0, 200, 7000), GetContentsCullRect("scroller").Rect());
  EXPECT_EQ(gfx::Rect(0, 0, 200, 7000), GetCullRect("child").Rect());

  // Setting |scroller| needs repaint will lead to proactive update for it,
  // and for |child| because |scroller|'s cull rect changes.
  GetPaintLayerByElementId("scroller")->SetNeedsRepaint();
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(gfx::Rect(0, 0, 200, 4200), GetContentsCullRect("scroller").Rect());
  EXPECT_EQ(gfx::Rect(0, 0, 200, 4200), GetCullRect("child").Rect());
}

TEST_P(CullRectUpdaterTest, ContentsCullRectCoveringWholeContentsRect) {
  GetDocument().GetSettings()->SetPreferCompositingToLCDTextEnabled(true);
  SetBodyInnerHTML(R"HTML(
    <div id="scroller" style="width: 400px; height: 400px; overflow: scroll">
      <div style="width: 600px; height: 7000px"></div>
      <div id="child" style="will-change: transform; height: 20px"></div>
    </div>
  )HTML");

  EXPECT_EQ(gfx::Rect(0, 0, 600, 4400), GetContentsCullRect("scroller").Rect());
  EXPECT_EQ(gfx::Rect(-4000, -7000, 8600, 4400), GetCullRect("child").Rect());

  auto* scroller = GetDocument().getElementById("scroller");
  scroller->scrollTo(0, 2500);
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(gfx::Rect(0, 0, 600, 6900), GetContentsCullRect("scroller").Rect());
  EXPECT_EQ(gfx::Rect(-4000, -7000, 8600, 6900), GetCullRect("child").Rect());

  scroller->scrollTo(0, 2800);
  UpdateAllLifecyclePhasesForTest();
  // Cull rects are not updated with a small scroll delta.
  EXPECT_EQ(gfx::Rect(0, 0, 600, 6900), GetContentsCullRect("scroller").Rect());
  EXPECT_EQ(gfx::Rect(-4000, -7000, 8600, 6900), GetCullRect("child").Rect());

  scroller->scrollTo(0, 3100);
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(gfx::Rect(0, 0, 600, 7020), GetContentsCullRect("scroller").Rect());
  EXPECT_EQ(gfx::Rect(-4000, -7000, 8600, 7020), GetCullRect("child").Rect());

  // We will use the same cull rects that cover the whole contents on further
  // scroll.
  scroller->scrollTo(0, 4000);
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(gfx::Rect(0, 0, 600, 7020), GetContentsCullRect("scroller").Rect());
  EXPECT_EQ(gfx::Rect(-4000, -7000, 8600, 7020), GetCullRect("child").Rect());

  scroller->scrollTo(0, 0);
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(gfx::Rect(0, 0, 600, 7020), GetContentsCullRect("scroller").Rect());
  EXPECT_EQ(gfx::Rect(-4000, -7000, 8600, 7020), GetCullRect("child").Rect());
}

TEST_P(CullRectUpdaterTest, SVGForeignObject) {
  GetDocument().GetSettings()->SetPreferCompositingToLCDTextEnabled(false);
  SetBodyInnerHTML(R"HTML(
    <div id="scroller" style="width: 100px; height: 100px; overflow: scroll">
      <svg id="svg" style="width: 100px; height: 4000px">
        <foreignObject id="foreign" style="width: 500px; height: 1000px">
          <div id="child" style="position: relative">Child</div>
        </foreignObject>
      </svg>
    </div>
  )HTML");

  auto* child = GetPaintLayerByElementId("child");
  auto* foreign = GetPaintLayerByElementId("foreign");
  auto* svg = GetPaintLayerByElementId("svg");
  EXPECT_FALSE(child->NeedsCullRectUpdate());
  EXPECT_FALSE(foreign->DescendantNeedsCullRectUpdate());
  EXPECT_FALSE(svg->DescendantNeedsCullRectUpdate());

  GetDocument().getElementById("scroller")->scrollTo(0, 500);
  UpdateAllLifecyclePhasesForTest();
  EXPECT_FALSE(child->NeedsCullRectUpdate());
  EXPECT_FALSE(foreign->DescendantNeedsCullRectUpdate());
  EXPECT_FALSE(svg->DescendantNeedsCullRectUpdate());

  child->SetNeedsCullRectUpdate();
  EXPECT_TRUE(child->NeedsCullRectUpdate());
  EXPECT_TRUE(foreign->DescendantNeedsCullRectUpdate());
  EXPECT_TRUE(svg->DescendantNeedsCullRectUpdate());

  UpdateAllLifecyclePhasesForTest();
  EXPECT_FALSE(child->NeedsCullRectUpdate());
  EXPECT_FALSE(foreign->DescendantNeedsCullRectUpdate());
  EXPECT_FALSE(svg->DescendantNeedsCullRectUpdate());
}

TEST_P(CullRectUpdaterTest, LayerUnderSVGHiddenContainer) {
  SetBodyInnerHTML(R"HTML(
    <div id="div" style="display: contents">
      <svg id="svg1"></svg>
    </div>
    <svg id="svg2">
      <defs id="defs"/>
    </svg>
  )HTML");

  EXPECT_FALSE(GetCullRect("svg1").Rect().IsEmpty());

  GetDocument().getElementById("defs")->appendChild(
      GetDocument().getElementById("div"));
  // This should not crash.
  UpdateAllLifecyclePhasesForTest();
  EXPECT_TRUE(GetCullRect("svg1").Rect().IsEmpty());
}

TEST_P(CullRectUpdaterTest, PerspectiveDescendants) {
  SetBodyInnerHTML(R"HTML(
    <div style="perspective: 1000px">
      <div style="height: 300px; transform-style: preserve-3d; contain: strict">
        <div id="target" style="transform: rotateX(20deg)">TARGET</div>
      </div>
    </div>
  )HTML");
  EXPECT_TRUE(GetCullRect("target").IsInfinite());
}

class CullRectUpdateOnPaintPropertyChangeTest : public CullRectUpdaterTest {
 protected:
  void Check(const String& old_style,
             const String& new_style,
             bool expected_needs_repaint,
             bool expected_needs_cull_rect_update,
             bool expected_needs_repaint_after_cull_rect_update) {
    UpdateAllLifecyclePhasesExceptPaint(/*update_cull_rects*/ false);
    const auto* target_layer = GetPaintLayerByElementId("target");
    EXPECT_EQ(expected_needs_repaint, target_layer->SelfNeedsRepaint())
        << old_style << " -> " << new_style;
    EXPECT_EQ(expected_needs_cull_rect_update,
              target_layer->NeedsCullRectUpdate())
        << old_style << " -> " << new_style;
    UpdateCullRects();
    EXPECT_EQ(expected_needs_repaint_after_cull_rect_update,
              target_layer->SelfNeedsRepaint())
        << old_style << " -> " << new_style;
  }

  void TestTargetChange(const AtomicString& old_style,
                        const AtomicString& new_style,
                        bool expected_needs_repaint,
                        bool expected_needs_cull_rect_update,
                        bool expected_needs_repaint_after_cull_rect_update) {
    SetBodyInnerHTML(html_);
    auto* target = GetDocument().getElementById("target");
    target->setAttribute(html_names::kStyleAttr, old_style);
    UpdateAllLifecyclePhasesForTest();
    target->setAttribute(html_names::kStyleAttr, new_style);
    Check(old_style, new_style, expected_needs_repaint,
          expected_needs_cull_rect_update,
          expected_needs_repaint_after_cull_rect_update);
  }

  void TestChildChange(const AtomicString& old_style,
                       const AtomicString& new_style,
                       bool expected_needs_repaint,
                       bool expected_needs_cull_rect_update,
                       bool expected_needs_repaint_after_cull_rect_update) {
    SetBodyInnerHTML(html_);
    auto* child = GetDocument().getElementById("child");
    child->setAttribute(html_names::kStyleAttr, old_style);
    UpdateAllLifecyclePhasesForTest();
    child->setAttribute(html_names::kStyleAttr, new_style);
    Check(old_style, new_style, expected_needs_repaint,
          expected_needs_cull_rect_update,
          expected_needs_repaint_after_cull_rect_update);
  }

  void TestTargetScroll(const ScrollOffset& old_scroll_offset,
                        const ScrollOffset& new_scroll_offset,
                        bool expected_needs_repaint,
                        bool expected_needs_cull_rect_update,
                        bool expected_needs_repaint_after_cull_rect_update) {
    SetBodyInnerHTML(html_);
    auto* target = GetDocument().getElementById("target");
    target->scrollTo(old_scroll_offset.x(), old_scroll_offset.y()),
        UpdateAllLifecyclePhasesForTest();
    target->scrollTo(new_scroll_offset.x(), new_scroll_offset.y()),
        Check(String(old_scroll_offset.ToString()),
              String(new_scroll_offset.ToString()), expected_needs_repaint,
              expected_needs_cull_rect_update,
              expected_needs_repaint_after_cull_rect_update);
  }

  String html_ = R"HTML(
    <style>
      #target {
        width: 100px;
        height: 100px;
        position: relative;
        overflow: scroll;
        background: white;
      }
      #child { width: 1000px; height: 1000px; }
    </style>
    <div id="target">
      <div id="child">child</div>
    </div>"
  )HTML";
};

INSTANTIATE_TEST_SUITE_P(All,
                         CullRectUpdateOnPaintPropertyChangeTest,
                         ::testing::Values(kScrollUpdateOptimizations |
                                               kScrollUnification,
                                           kScrollUpdateOptimizations));

TEST_P(CullRectUpdateOnPaintPropertyChangeTest, Opacity) {
  TestTargetChange("opacity: 0.2", "opacity: 0.8", false, false, false);
  TestTargetChange("opacity: 0.5", "", true, false, true);
  TestTargetChange("", "opacity: 0.5", true, false, true);
  TestTargetChange("will-change: opacity", "will-change: opacity; opacity: 0.5",
                   false, false, false);
  TestTargetChange("will-change: opacity; opacity: 0.5", "will-change: opacity",
                   false, false, false);
}

TEST_P(CullRectUpdateOnPaintPropertyChangeTest, NonPixelMovingFilter) {
  TestTargetChange("filter: invert(5%)", "filter: invert(8%)", false, false,
                   false);
  TestTargetChange("filter: invert(5%)", "", true, false, true);
  TestTargetChange("", "filter: invert(5%)", true, false, true);
  TestTargetChange("will-change: filter; filter: invert(5%)",
                   "will-change: filter", false, false, false);
  TestTargetChange("will-change: filter",
                   "will-change: filter; filter: invert(5%)", false, false,
                   false);
}

TEST_P(CullRectUpdateOnPaintPropertyChangeTest, PixelMovingFilter) {
  TestTargetChange("filter: blur(5px)", "filter: blur(8px)", false, false,
                   false);
  TestTargetChange("filter: blur(5px)", "", true, true, true);
  TestTargetChange("", "filter: blur(5px)", true, true, true);
  TestTargetChange("will-change: filter; filter: blur(5px)",
                   "will-change: filter", true, false, true);
  TestTargetChange("will-change: filter",
                   "will-change: filter; filter: blur(5px)", true, false, true);
}

TEST_P(CullRectUpdateOnPaintPropertyChangeTest, Transform) {
  // We use infinite cull rect for small layers with non-composited transforms,
  // so don't need to update cull rect on non-composited transform change.
  TestTargetChange("transform: translateX(10px)", "transform: translateX(20px)",
                   false, false, false);
  TestTargetChange("transform: translateX(10px)", "", true, true, true);
  TestTargetChange("", "transform: translateX(10px)", true, true, true);
  // We don't use infinite cull rect for layers with composited transforms.
  TestTargetChange("will-change: transform; transform: translateX(10px)",
                   "will-change: transform; transform: translateX(20px)", false,
                   true, false);
  TestTargetChange("will-change: transform; transform: translateX(10px)",
                   "will-change: transform", false, true, false);
  TestTargetChange("will-change: transform",
                   "will-change: transform; transform: translateX(10px)", false,
                   true, false);
}

TEST_P(CullRectUpdateOnPaintPropertyChangeTest, AnimatingTransform) {
  html_ = html_ + R"HTML(
    <style>
      @keyframes test {
        0% { transform: translateX(0); }
        100% { transform: translateX(200px); }
      }
      #target { animation: test 1s infinite; }
    </style>
  )HTML";
  TestTargetChange("transform: translateX(10px)", "transform: translateX(20px)",
                   false, false, false);
  TestTargetChange("transform: translateX(10px)", "", false, false, false);
  TestTargetChange("", "transform: translateX(10px)", false, false, false);
}

TEST_P(CullRectUpdateOnPaintPropertyChangeTest, ScrollContentsSizeChange) {
  TestChildChange("", "width: 3000px", true, true, true);
  TestChildChange("", "height: 3000px", true, true, true);
  TestChildChange("", "width: 50px; height: 50px", true, true, true);
}

TEST_P(CullRectUpdateOnPaintPropertyChangeTest, SmallContentsScroll) {
  // TODO(wangxianzhu): Optimize for scrollers with small contents.
  bool needs_cull_rect_update = false;
  TestTargetScroll(ScrollOffset(), ScrollOffset(100, 200), false,
                   needs_cull_rect_update, false);
  TestTargetScroll(ScrollOffset(100, 200), ScrollOffset(1000, 1000), false,
                   needs_cull_rect_update, false);
  TestTargetScroll(ScrollOffset(1000, 1000), ScrollOffset(), false,
                   needs_cull_rect_update, false);
}

TEST_P(CullRectUpdateOnPaintPropertyChangeTest,
       LargeContentsScrollSmallDeltaOrNotExposingNewContents) {
  html_ = html_ + "<style>#child { width: 10000px; height: 10000px; }</style>";
  // Scroll offset changes that are small or won't expose new contents don't
  // need cull rect update when ScrollUpdateOptimizationsEnabled.
  bool needs_cull_rect_update = false;
  TestTargetScroll(ScrollOffset(), ScrollOffset(200, 200), false,
                   needs_cull_rect_update, false);
  TestTargetScroll(ScrollOffset(200, 200), ScrollOffset(), false,
                   needs_cull_rect_update, false);
  TestTargetScroll(ScrollOffset(2000, 2000), ScrollOffset(), false,
                   needs_cull_rect_update, false);
  TestTargetScroll(ScrollOffset(7000, 7000), ScrollOffset(8000, 8000), false,
                   needs_cull_rect_update, false);
}

TEST_P(CullRectUpdateOnPaintPropertyChangeTest,
       LargeContentsScrollExposingNewContents) {
  html_ = html_ + "<style>#child { width: 10000px; height: 10000px; }</style>";
  // Big scroll offset changes that will expose new contents to paint need cull
  // rect update.
  TestTargetScroll(ScrollOffset(100, 200), ScrollOffset(100, 800), false, true,
                   true);
  TestTargetScroll(ScrollOffset(100, 800), ScrollOffset(700, 800), false, true,
                   true);
  TestTargetScroll(ScrollOffset(700, 800), ScrollOffset(1700, 1800), false,
                   true, true);
  TestTargetScroll(ScrollOffset(8000, 8000), ScrollOffset(0, 8000), false, true,
                   true);
  TestTargetScroll(ScrollOffset(8000, 100), ScrollOffset(), false, true, true);
  TestTargetScroll(ScrollOffset(100, 8000), ScrollOffset(), false, true, true);
}

}  // namespace blink
