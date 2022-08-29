// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/paint/block_painter.h"

#include "base/test/scoped_feature_list.h"
#include "cc/base/features.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/renderer/core/dom/events/add_event_listener_options_resolved.h"
#include "third_party/blink/renderer/core/dom/events/native_event_listener.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/paint/paint_controller_paint_test.h"
#include "third_party/blink/renderer/platform/graphics/paint/drawing_display_item.h"
#include "third_party/blink/renderer/platform/graphics/paint/paint_chunk.h"
#include "third_party/blink/renderer/platform/testing/paint_property_test_helpers.h"

using testing::ElementsAre;

namespace blink {

using BlockPainterTest = PaintControllerPaintTest;

INSTANTIATE_PAINT_TEST_SUITE_P(BlockPainterTest);

TEST_P(BlockPainterTest, OverflowRectForCullRectTesting) {
  SetBodyInnerHTML(R"HTML(
    <div id='scroller' style='width: 50px; height: 50px; overflow: scroll'>
      <div style='width: 50px; height: 5000px'></div>
    </div>
  )HTML");
  auto* scroller = To<LayoutBlock>(GetLayoutObjectByElementId("scroller"));
  EXPECT_EQ(PhysicalRect(0, 0, 50, 5000),
            BlockPainter(*scroller).OverflowRectForCullRectTesting());
}

TEST_P(BlockPainterTest, OverflowRectCompositedScrollingForCullRectTesting) {
  SetBodyInnerHTML(R"HTML(
    <div id='scroller' style='width: 50px; height: 50px; overflow: scroll;
                              will-change: transform'>
      <div style='width: 50px; height: 5000px'></div>
    </div>
  )HTML");
  auto* scroller = To<LayoutBlock>(GetLayoutObjectByElementId("scroller"));
  EXPECT_EQ(PhysicalRect(0, 0, 50, 5000),
            BlockPainter(*scroller).OverflowRectForCullRectTesting());
}
namespace {
class BlockPainterTestMockEventListener final : public NativeEventListener {
 public:
  void Invoke(ExecutionContext*, Event*) override {}
};

void SetWheelEventListener(const Document& document, const char* element_id) {
  auto* element = document.getElementById(element_id);
  auto* listener = MakeGarbageCollected<BlockPainterTestMockEventListener>();
  auto* resolved_options =
      MakeGarbageCollected<AddEventListenerOptionsResolved>();
  resolved_options->setPassive(false);
  element->addEventListener(event_type_names::kWheel, listener,
                            resolved_options);
  document.View()->UpdateAllLifecyclePhasesForTest();
}
}  // namespace

TEST_P(BlockPainterTest, BlockingWheelRectsWithoutPaint) {
  SetBodyInnerHTML(R"HTML(
    <style>
      ::-webkit-scrollbar { display: none; }
      body { margin: 0; }
      #parent { width: 100px; height: 100px; }
      #childVisible { width: 200px; height: 25px; }
      #childHidden { width: 200px; height: 30px; visibility: hidden; }
      #childDisplayNone { width: 200px; height: 30px; display: none; }
    </style>
    <div id='parent'>
      <div id='childVisible'></div>
      <div id='childHidden'></div>
    </div>
  )HTML");

  // Initially there should be no hit test data because there is no blocking
  // wheel handler.
  EXPECT_THAT(ContentDisplayItems(),
              ElementsAre(VIEW_SCROLLING_BACKGROUND_DISPLAY_ITEM));
  EXPECT_THAT(ContentPaintChunks(),
              ElementsAre(VIEW_SCROLLING_BACKGROUND_CHUNK_COMMON));

  // Add a blocking wheel event handler to parent and ensure that hit test data
  // are created for both the parent and the visible child.
  SetWheelEventListener(GetDocument(), "parent");

  EXPECT_THAT(ContentDisplayItems(),
              ElementsAre(VIEW_SCROLLING_BACKGROUND_DISPLAY_ITEM));

  HitTestData hit_test_data;
  hit_test_data.wheel_event_rects = {{gfx::Rect(0, 0, 100, 100)},
                                     {gfx::Rect(0, 0, 200, 25)}};
  ContentPaintChunks(),
      ElementsAre(VIEW_SCROLLING_BACKGROUND_CHUNK(1, &hit_test_data));

  // Remove the blocking wheel event handler from parent and ensure no hit test
  // data are left.
  auto* parent_element = GetElementById("parent");
  parent_element->RemoveAllEventListeners();
  UpdateAllLifecyclePhasesForTest();
  EXPECT_THAT(ContentDisplayItems(),
              ElementsAre(VIEW_SCROLLING_BACKGROUND_DISPLAY_ITEM));
  EXPECT_THAT(ContentPaintChunks(),
              ElementsAre(VIEW_SCROLLING_BACKGROUND_CHUNK_COMMON));
}

TEST_P(BlockPainterTest, BlockingWheelEventRectSubsequenceCaching) {
  SetBodyInnerHTML(R"HTML(
    <style>
      body { margin: 0; }
      #stacking-context {
        position: absolute;
        z-index: 1;
      }
      #wheelhandler {
        width: 100px;
        height: 100px;
      }
    </style>
    <div id='stacking-context'>
      <div id='wheelhandler'></div>
    </div>
  )HTML");

  SetWheelEventListener(GetDocument(), "wheelhandler");

  const auto* wheelhandler = GetLayoutObjectByElementId("wheelhandler");
  EXPECT_THAT(ContentDisplayItems(),
              ElementsAre(VIEW_SCROLLING_BACKGROUND_DISPLAY_ITEM));

  const auto& hit_test_client = *GetPaintLayerByElementId("stacking-context");
  EXPECT_SUBSEQUENCE_FROM_CHUNK(hit_test_client,
                                ContentPaintChunks().begin() + 1, 1);

  PaintChunk::Id hit_test_chunk_id(hit_test_client.Id(),
                                   DisplayItem::kLayerChunk);
  auto hit_test_chunk_properties = wheelhandler->EnclosingLayer()
                                       ->GetLayoutObject()
                                       .FirstFragment()
                                       .ContentsProperties();
  HitTestData hit_test_data;
  hit_test_data.wheel_event_rects = {{gfx::Rect(0, 0, 100, 100)}};

  EXPECT_THAT(
      ContentPaintChunks(),
      ElementsAre(
          VIEW_SCROLLING_BACKGROUND_CHUNK_COMMON,
          IsPaintChunk(1, 1, hit_test_chunk_id, hit_test_chunk_properties,
                       &hit_test_data, gfx::Rect(0, 0, 100, 100))));

  // Trigger a repaint with the whole stacking-context subsequence cached.
  GetLayoutView().Layer()->SetNeedsRepaint();
  PaintController::CounterForTesting counter;
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(1u, counter.num_cached_items);
  EXPECT_EQ(1u, counter.num_cached_subsequences);

  EXPECT_SUBSEQUENCE_FROM_CHUNK(hit_test_client,
                                ContentPaintChunks().begin() + 1, 1);

  EXPECT_THAT(
      ContentPaintChunks(),
      ElementsAre(
          VIEW_SCROLLING_BACKGROUND_CHUNK_COMMON,
          IsPaintChunk(1, 1, hit_test_chunk_id, hit_test_chunk_properties,
                       &hit_test_data, gfx::Rect(0, 0, 100, 100))));
}

TEST_P(BlockPainterTest, WheelEventRectPaintCaching) {
  SetBodyInnerHTML(R"HTML(
    <style>
      body { margin: 0; }
      #wheelhandler {
        width: 100px;
        height: 100px;
      }
      #sibling {
        width: 100px;
        height: 100px;
        background: blue;
      }
    </style>
    <div id='wheelhandler'></div>
    <div id='sibling'></div>
  )HTML");

  SetWheelEventListener(GetDocument(), "wheelhandler");

  auto* sibling_element = GetElementById("sibling");
  const auto* sibling = sibling_element->GetLayoutObject();
  EXPECT_THAT(ContentDisplayItems(),
              ElementsAre(VIEW_SCROLLING_BACKGROUND_DISPLAY_ITEM,
                          IsSameId(sibling->Id(), kBackgroundType)));

  HitTestData hit_test_data;
  hit_test_data.wheel_event_rects = {{gfx::Rect(0, 0, 100, 100)}};

  EXPECT_THAT(ContentPaintChunks(),
              ElementsAre(VIEW_SCROLLING_BACKGROUND_CHUNK(2, &hit_test_data)));

  sibling_element->setAttribute(html_names::kStyleAttr, "background: green;");
  PaintController::CounterForTesting counter;
  UpdateAllLifecyclePhasesForTest();
  // Only the background display item of the sibling should be invalidated.
  EXPECT_EQ(1u, counter.num_cached_items);

  EXPECT_THAT(ContentPaintChunks(),
              ElementsAre(VIEW_SCROLLING_BACKGROUND_CHUNK(2, &hit_test_data)));
}

TEST_P(BlockPainterTest, BlockingWheelRectScrollingContents) {
  SetBodyInnerHTML(R"HTML(
    <style>
      ::-webkit-scrollbar { display: none; }
      body { margin: 0; }
      #scroller {
        width: 100px;
        height: 100px;
        overflow: scroll;
        will-change: transform;
        background-color: blue;
      }
      #child {
        width: 10px;
        height: 400px;
      }
    </style>
    <div id='scroller'>
      <div id='child'></div>
    </div>
  )HTML");

  auto* scroller_element = GetElementById("scroller");
  auto* scroller =
      To<LayoutBoxModelObject>(scroller_element->GetLayoutObject());
  const auto& scroller_scrolling_client =
      scroller->GetScrollableArea()->GetScrollingBackgroundDisplayItemClient();

  SetWheelEventListener(GetDocument(), "scroller");

  HitTestData hit_test_data;
  hit_test_data.wheel_event_rects = {{gfx::Rect(0, 0, 100, 400)},
                                     {gfx::Rect(0, 0, 10, 400)}};
  EXPECT_THAT(
      ContentDisplayItems(),
      ElementsAre(VIEW_SCROLLING_BACKGROUND_DISPLAY_ITEM,
                  IsSameId(scroller->Id(), kBackgroundType),
                  IsSameId(scroller_scrolling_client.Id(), kBackgroundType)));
  EXPECT_THAT(
      ContentPaintChunks(),
      ElementsAre(
          VIEW_SCROLLING_BACKGROUND_CHUNK_COMMON,
          IsPaintChunk(1, 2),  // scroller background.
          IsPaintChunk(2, 2),  // scroller scroll hit test.
          IsPaintChunk(
              2, 3,
              PaintChunk::Id(scroller->Id(), kScrollingBackgroundChunkType),
              scroller->FirstFragment().ContentsProperties(), &hit_test_data)));
}

TEST_P(BlockPainterTest, WheelEventRectPaintChunkChanges) {
  SetBodyInnerHTML(R"HTML(
    <style>
      body { margin: 0; }
      #wheelevent {
        width: 100px;
        height: 100px;
      }
    </style>
    <div id='wheelevent'></div>
  )HTML");

  EXPECT_THAT(ContentDisplayItems(),
              ElementsAre(VIEW_SCROLLING_BACKGROUND_DISPLAY_ITEM));

  EXPECT_THAT(ContentPaintChunks(),
              ElementsAre(VIEW_SCROLLING_BACKGROUND_CHUNK_COMMON));

  SetWheelEventListener(GetDocument(), "wheelevent");

  EXPECT_THAT(ContentDisplayItems(),
              ElementsAre(VIEW_SCROLLING_BACKGROUND_DISPLAY_ITEM));

  HitTestData hit_test_data;
  hit_test_data.wheel_event_rects = {{gfx::Rect(0, 0, 100, 100)}};

  EXPECT_THAT(ContentPaintChunks(),
              ElementsAre(VIEW_SCROLLING_BACKGROUND_CHUNK(1, &hit_test_data)));

  GetElementById("wheelevent")->RemoveAllEventListeners();
  UpdateAllLifecyclePhasesForTest();
  EXPECT_THAT(ContentDisplayItems(),
              ElementsAre(VIEW_SCROLLING_BACKGROUND_DISPLAY_ITEM));
  EXPECT_THAT(ContentPaintChunks(),
              ElementsAre(VIEW_SCROLLING_BACKGROUND_CHUNK_COMMON));
}

TEST_P(BlockPainterTest, TouchActionRectsWithoutPaint) {
  SetBodyInnerHTML(R"HTML(
    <style>
      ::-webkit-scrollbar { display: none; }
      body { margin: 0; }
      #parent { width: 100px; height: 100px; }
      .touchActionNone { touch-action: none; }
      #childVisible { width: 200px; height: 25px; }
      #childHidden { width: 200px; height: 30px; visibility: hidden; }
      #childDisplayNone { width: 200px; height: 30px; display: none; }
    </style>
    <div id='parent'>
      <div id='childVisible'></div>
      <div id='childHidden'></div>
    </div>
  )HTML");

  // Initially there should be no hit test data because there is no touch
  // action.
  EXPECT_THAT(ContentDisplayItems(),
              ElementsAre(VIEW_SCROLLING_BACKGROUND_DISPLAY_ITEM));
  EXPECT_THAT(ContentPaintChunks(),
              ElementsAre(VIEW_SCROLLING_BACKGROUND_CHUNK_COMMON));

  // Add a touch action to parent and ensure that hit test data are created
  // for both the parent and the visible child.
  auto* parent_element = GetElementById("parent");
  parent_element->setAttribute(html_names::kClassAttr, "touchActionNone");
  UpdateAllLifecyclePhasesForTest();
  EXPECT_THAT(ContentDisplayItems(),
              ElementsAre(VIEW_SCROLLING_BACKGROUND_DISPLAY_ITEM));
  HitTestData hit_test_data;
  hit_test_data.touch_action_rects = {{gfx::Rect(0, 0, 100, 100)},
                                      {gfx::Rect(0, 0, 200, 25)}};
  ContentPaintChunks(),
      ElementsAre(VIEW_SCROLLING_BACKGROUND_CHUNK(1, &hit_test_data));

  // Remove the touch action from parent and ensure no hit test data are left.
  parent_element->removeAttribute(html_names::kClassAttr);
  UpdateAllLifecyclePhasesForTest();
  EXPECT_THAT(ContentDisplayItems(),
              ElementsAre(VIEW_SCROLLING_BACKGROUND_DISPLAY_ITEM));
  EXPECT_THAT(ContentPaintChunks(),
              ElementsAre(VIEW_SCROLLING_BACKGROUND_CHUNK_COMMON));
}

TEST_P(BlockPainterTest, TouchActionRectSubsequenceCaching) {
  SetBodyInnerHTML(R"HTML(
    <style>
      body { margin: 0; }
      #stacking-context {
        position: absolute;
        z-index: 1;
      }
      #touchaction {
        width: 100px;
        height: 100px;
        touch-action: none;
      }
    </style>
    <div id='stacking-context'>
      <div id='touchaction'></div>
    </div>
  )HTML");

  const auto* touchaction = GetLayoutObjectByElementId("touchaction");
  EXPECT_THAT(ContentDisplayItems(),
              ElementsAre(VIEW_SCROLLING_BACKGROUND_DISPLAY_ITEM));

  const auto& hit_test_client = *GetPaintLayerByElementId("stacking-context");
  EXPECT_SUBSEQUENCE_FROM_CHUNK(hit_test_client,
                                ContentPaintChunks().begin() + 1, 1);

  PaintChunk::Id hit_test_chunk_id(hit_test_client.Id(),
                                   DisplayItem::kLayerChunk);
  auto hit_test_chunk_properties = touchaction->EnclosingLayer()
                                       ->GetLayoutObject()
                                       .FirstFragment()
                                       .ContentsProperties();
  HitTestData hit_test_data;
  hit_test_data.touch_action_rects = {{gfx::Rect(0, 0, 100, 100)}};

  EXPECT_THAT(
      ContentPaintChunks(),
      ElementsAre(
          VIEW_SCROLLING_BACKGROUND_CHUNK_COMMON,
          IsPaintChunk(1, 1, hit_test_chunk_id, hit_test_chunk_properties,
                       &hit_test_data, gfx::Rect(0, 0, 100, 100))));

  // Trigger a repaint with the whole stacking-context subsequence cached.
  GetLayoutView().Layer()->SetNeedsRepaint();
  PaintController::CounterForTesting counter;
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(1u, counter.num_cached_items);
  EXPECT_EQ(1u, counter.num_cached_subsequences);

  EXPECT_SUBSEQUENCE_FROM_CHUNK(hit_test_client,
                                ContentPaintChunks().begin() + 1, 1);

  EXPECT_THAT(
      ContentPaintChunks(),
      ElementsAre(
          VIEW_SCROLLING_BACKGROUND_CHUNK_COMMON,
          IsPaintChunk(1, 1, hit_test_chunk_id, hit_test_chunk_properties,
                       &hit_test_data, gfx::Rect(0, 0, 100, 100))));
}

TEST_P(BlockPainterTest, TouchActionRectPaintCaching) {
  SetBodyInnerHTML(R"HTML(
    <style>
      body { margin: 0; }
      #touchaction {
        width: 100px;
        height: 100px;
        touch-action: none;
      }
      #sibling {
        width: 100px;
        height: 100px;
        background: blue;
      }
    </style>
    <div id='touchaction'></div>
    <div id='sibling'></div>
  )HTML");

  auto* sibling_element = GetElementById("sibling");
  const auto* sibling = sibling_element->GetLayoutObject();
  EXPECT_THAT(ContentDisplayItems(),
              ElementsAre(VIEW_SCROLLING_BACKGROUND_DISPLAY_ITEM,
                          IsSameId(sibling->Id(), kBackgroundType)));

  HitTestData hit_test_data;
  hit_test_data.touch_action_rects = {{gfx::Rect(0, 0, 100, 100)}};

  EXPECT_THAT(ContentPaintChunks(),
              ElementsAre(VIEW_SCROLLING_BACKGROUND_CHUNK(2, &hit_test_data)));

  sibling_element->setAttribute(html_names::kStyleAttr, "background: green;");
  PaintController::CounterForTesting counter;
  UpdateAllLifecyclePhasesForTest();
  // Only the background display item of the sibling should be invalidated.
  EXPECT_EQ(1u, counter.num_cached_items);

  EXPECT_THAT(ContentPaintChunks(),
              ElementsAre(VIEW_SCROLLING_BACKGROUND_CHUNK(2, &hit_test_data)));
}

TEST_P(BlockPainterTest, TouchActionRectScrollingContents) {
  SetBodyInnerHTML(R"HTML(
    <style>
      ::-webkit-scrollbar { display: none; }
      body { margin: 0; }
      #scroller {
        width: 100px;
        height: 100px;
        overflow: scroll;
        touch-action: none;
        will-change: transform;
        background-color: blue;
      }
      #child {
        width: 10px;
        height: 400px;
      }
    </style>
    <div id='scroller'>
      <div id='child'></div>
    </div>
  )HTML");

  auto* scroller_element = GetElementById("scroller");
  auto* scroller =
      To<LayoutBoxModelObject>(scroller_element->GetLayoutObject());
  const auto& scroller_scrolling_client =
      scroller->GetScrollableArea()->GetScrollingBackgroundDisplayItemClient();
  HitTestData hit_test_data;
  hit_test_data.touch_action_rects = {{gfx::Rect(0, 0, 100, 400)},
                                      {gfx::Rect(0, 0, 10, 400)}};
  EXPECT_THAT(
      ContentDisplayItems(),
      ElementsAre(VIEW_SCROLLING_BACKGROUND_DISPLAY_ITEM,
                  IsSameId(scroller->Id(), kBackgroundType),
                  IsSameId(scroller_scrolling_client.Id(), kBackgroundType)));
  EXPECT_THAT(
      ContentPaintChunks(),
      ElementsAre(
          VIEW_SCROLLING_BACKGROUND_CHUNK_COMMON,
          IsPaintChunk(1, 2),  // scroller background.
          IsPaintChunk(2, 2),  // scroller scroll hit test.
          IsPaintChunk(
              2, 3,
              PaintChunk::Id(scroller->Id(), kScrollingBackgroundChunkType),
              scroller->FirstFragment().ContentsProperties(), &hit_test_data)));
}

TEST_P(BlockPainterTest, TouchActionRectPaintChunkChanges) {
  SetBodyInnerHTML(R"HTML(
    <style>
      body { margin: 0; }
      #touchaction {
        width: 100px;
        height: 100px;
      }
    </style>
    <div id='touchaction'></div>
  )HTML");

  auto* touchaction_element = GetElementById("touchaction");
  auto* touchaction = touchaction_element->GetLayoutObject();
  EXPECT_THAT(ContentDisplayItems(),
              ElementsAre(VIEW_SCROLLING_BACKGROUND_DISPLAY_ITEM));

  EXPECT_THAT(ContentPaintChunks(),
              ElementsAre(VIEW_SCROLLING_BACKGROUND_CHUNK_COMMON));

  touchaction_element->setAttribute(html_names::kStyleAttr,
                                    "touch-action: none;");
  UpdateAllLifecyclePhasesForTest();
  EXPECT_THAT(ContentDisplayItems(),
              ElementsAre(VIEW_SCROLLING_BACKGROUND_DISPLAY_ITEM));

  PaintChunk::Id hit_test_chunk_id(touchaction->EnclosingLayer()->Id(),
                                   kHitTestChunkType);
  HitTestData hit_test_data;
  hit_test_data.touch_action_rects = {{gfx::Rect(0, 0, 100, 100)}};

  EXPECT_THAT(ContentPaintChunks(),
              ElementsAre(VIEW_SCROLLING_BACKGROUND_CHUNK(1, &hit_test_data)));

  touchaction_element->removeAttribute(html_names::kStyleAttr);
  UpdateAllLifecyclePhasesForTest();
  EXPECT_THAT(ContentDisplayItems(),
              ElementsAre(VIEW_SCROLLING_BACKGROUND_DISPLAY_ITEM));
  EXPECT_THAT(ContentPaintChunks(),
              ElementsAre(VIEW_SCROLLING_BACKGROUND_CHUNK_COMMON));
}

namespace {
class BlockPainterMockEventListener final : public NativeEventListener {
 public:
  void Invoke(ExecutionContext*, Event*) final {}
};
}  // namespace

TEST_P(BlockPainterTest, TouchHandlerRectsWithoutPaint) {
  SetBodyInnerHTML(R"HTML(
    <style>
      ::-webkit-scrollbar { display: none; }
      body { margin: 0; }
      #parent { width: 100px; height: 100px; }
      #child { width: 200px; height: 50px; }
    </style>
    <div id='parent'>
      <div id='child'></div>
    </div>
  )HTML");

  // Initially there should be no hit test data because there are no event
  // handlers.
  EXPECT_THAT(ContentDisplayItems(),
              ElementsAre(VIEW_SCROLLING_BACKGROUND_DISPLAY_ITEM));

  // Add an event listener to parent and ensure that hit test data are created
  // for both the parent and child.
  BlockPainterMockEventListener* callback =
      MakeGarbageCollected<BlockPainterMockEventListener>();
  auto* parent_element = GetElementById("parent");
  parent_element->addEventListener(event_type_names::kTouchstart, callback);
  UpdateAllLifecyclePhasesForTest();

  EXPECT_THAT(ContentDisplayItems(),
              ElementsAre(VIEW_SCROLLING_BACKGROUND_DISPLAY_ITEM));
  HitTestData hit_test_data;
  hit_test_data.touch_action_rects = {{gfx::Rect(0, 0, 100, 100)},
                                      {gfx::Rect(0, 0, 200, 50)}};
  EXPECT_THAT(ContentPaintChunks(),
              ElementsAre(VIEW_SCROLLING_BACKGROUND_CHUNK(1, &hit_test_data)));

  // Remove the event handler from parent and ensure no hit test data are left.
  parent_element->RemoveAllEventListeners();
  UpdateAllLifecyclePhasesForTest();
  EXPECT_THAT(ContentDisplayItems(),
              ElementsAre(VIEW_SCROLLING_BACKGROUND_DISPLAY_ITEM));
  EXPECT_THAT(ContentPaintChunks(),
              ElementsAre(VIEW_SCROLLING_BACKGROUND_CHUNK_COMMON));
}

TEST_P(BlockPainterTest, TouchActionRectsAcrossPaintChanges) {
  SetBodyInnerHTML(R"HTML(
    <style>
      ::-webkit-scrollbar { display: none; }
      body { margin: 0; }
      #parent { width: 100px; height: 100px; touch-action: none; }
      #child { width: 200px; height: 50px; }
    </style>
    <div id='parent'>
      <div id='child'></div>
    </div>
  )HTML");

  EXPECT_THAT(ContentDisplayItems(),
              ElementsAre(VIEW_SCROLLING_BACKGROUND_DISPLAY_ITEM));
  HitTestData hit_test_data;
  hit_test_data.touch_action_rects = {{gfx::Rect(0, 0, 100, 100)},
                                      {gfx::Rect(0, 0, 200, 50)}};
  EXPECT_THAT(ContentPaintChunks(),
              ElementsAre(VIEW_SCROLLING_BACKGROUND_CHUNK(
                  1, &hit_test_data, gfx::Rect(0, 0, 800, 600))));

  auto* child_element = GetElementById("child");
  child_element->setAttribute("style", "background: blue;");
  UpdateAllLifecyclePhasesForTest();
  EXPECT_THAT(ContentDisplayItems(),
              ElementsAre(VIEW_SCROLLING_BACKGROUND_DISPLAY_ITEM,
                          IsSameId(child_element->GetLayoutObject()->Id(),
                                   kBackgroundType)));
  EXPECT_THAT(ContentPaintChunks(),
              ElementsAre(VIEW_SCROLLING_BACKGROUND_CHUNK(2, &hit_test_data)));
}

TEST_P(BlockPainterTest, ScrolledHitTestChunkProperties) {
  SetBodyInnerHTML(R"HTML(
    <style>
      ::-webkit-scrollbar { display: none; }
      body { margin: 0; }
      #scroller {
        width: 100px;
        height: 100px;
        overflow: scroll;
        touch-action: none;
      }
      #child {
        width: 200px;
        height: 50px;
        touch-action: none;
      }
    </style>
    <div id='scroller'>
      <div id='child'></div>
    </div>
  )HTML");

  const auto* scroller =
      To<LayoutBlock>(GetLayoutObjectByElementId("scroller"));
  EXPECT_THAT(ContentDisplayItems(),
              ElementsAre(VIEW_SCROLLING_BACKGROUND_DISPLAY_ITEM));

  HitTestData scroller_touch_action_hit_test_data;
  scroller_touch_action_hit_test_data.touch_action_rects = {
      {gfx::Rect(0, 0, 100, 100)}};
  HitTestData scroll_hit_test_data;
  scroll_hit_test_data.scroll_translation =
      scroller->FirstFragment().PaintProperties()->ScrollTranslation();
  scroll_hit_test_data.scroll_hit_test_rect = gfx::Rect(0, 0, 100, 100);
  HitTestData scrolled_hit_test_data;
  scrolled_hit_test_data.touch_action_rects = {{gfx::Rect(0, 0, 200, 50)}};

  const auto& paint_chunks = ContentPaintChunks();
  EXPECT_THAT(
      paint_chunks,
      ElementsAre(
          VIEW_SCROLLING_BACKGROUND_CHUNK_COMMON,
          IsPaintChunk(
              1, 1, PaintChunk::Id(scroller->Id(), kBackgroundChunkType),
              scroller->FirstFragment().LocalBorderBoxProperties(),
              &scroller_touch_action_hit_test_data, gfx::Rect(0, 0, 100, 100)),
          IsPaintChunk(
              1, 1, PaintChunk::Id(scroller->Id(), DisplayItem::kScrollHitTest),
              scroller->FirstFragment().LocalBorderBoxProperties(),
              &scroll_hit_test_data, gfx::Rect(0, 0, 100, 100)),
          IsPaintChunk(1, 1,
                       PaintChunk::Id(scroller->Id(),
                                      kClippedContentsBackgroundChunkType),
                       scroller->FirstFragment().ContentsProperties(),
                       &scrolled_hit_test_data, gfx::Rect(0, 0, 200, 50))));

  const auto& scroller_paint_chunk = *(paint_chunks.begin() + 1);
  // The hit test rect for the scroller itself should not be scrolled.
  EXPECT_FALSE(
      ToUnaliased(scroller_paint_chunk.properties.Transform()).ScrollNode());

  const auto& scrolled_paint_chunk = *(paint_chunks.begin() + 3);
  // The hit test rect for the scrolled contents should be scrolled.
  EXPECT_TRUE(
      ToUnaliased(scrolled_paint_chunk.properties.Transform()).ScrollNode());
}

}  // namespace blink
