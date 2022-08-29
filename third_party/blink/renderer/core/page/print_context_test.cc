// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/page/print_context.h"

#include <memory>

#include "base/test/scoped_feature_list.h"
#include "components/viz/test/test_context_provider.h"
#include "components/viz/test/test_gles2_interface.h"
#include "components/viz/test/test_raster_interface.h"
#include "gpu/config/gpu_finch_features.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/events/before_print_event.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/html/canvas/canvas_rendering_context.h"
#include "third_party/blink/renderer/core/html/html_element.h"
#include "third_party/blink/renderer/core/layout/layout_view.h"
#include "third_party/blink/renderer/core/paint/paint_layer.h"
#include "third_party/blink/renderer/core/paint/paint_layer_painter.h"
#include "third_party/blink/renderer/core/scroll/scrollbar_theme.h"
#include "third_party/blink/renderer/core/testing/core_unit_test_helper.h"
#include "third_party/blink/renderer/core/testing/dummy_page_holder.h"
#include "third_party/blink/renderer/platform/graphics/canvas_resource_provider.h"
#include "third_party/blink/renderer/platform/graphics/gpu/shared_gpu_context.h"
#include "third_party/blink/renderer/platform/graphics/graphics_context.h"
#include "third_party/blink/renderer/platform/graphics/paint/drawing_recorder.h"
#include "third_party/blink/renderer/platform/graphics/paint/paint_record_builder.h"
#include "third_party/blink/renderer/platform/graphics/test/gpu_test_utils.h"
#include "third_party/blink/renderer/platform/testing/paint_test_configurations.h"
#include "third_party/blink/renderer/platform/wtf/text/text_stream.h"
#include "third_party/skia/include/core/SkCanvas.h"

using testing::_;

namespace blink {

const int kPageWidth = 800;
const int kPageHeight = 600;

class MockPageContextCanvas : public SkCanvas {
 public:
  enum OperationType { kDrawRect, kDrawPoint };

  struct Operation {
    OperationType type;
    SkRect rect;
  };

  MockPageContextCanvas() : SkCanvas(kPageWidth, kPageHeight) {}
  ~MockPageContextCanvas() override = default;

  void onDrawAnnotation(const SkRect& rect,
                        const char key[],
                        SkData* value) override {
    // Ignore PDF node key annotations, defined in SkPDFDocument.cpp.
    if (0 == strcmp(key, "PDF_Node_Key"))
      return;

    if (rect.width() == 0 && rect.height() == 0) {
      SkPoint point = getTotalMatrix().mapXY(rect.x(), rect.y());
      Operation operation = {kDrawPoint,
                             SkRect::MakeXYWH(point.x(), point.y(), 0, 0)};
      recorded_operations_.push_back(operation);
    } else {
      Operation operation = {kDrawRect, rect};
      getTotalMatrix().mapRect(&operation.rect);
      recorded_operations_.push_back(operation);
    }
  }

  const Vector<Operation>& RecordedOperations() const {
    return recorded_operations_;
  }

  MOCK_METHOD2(onDrawRect, void(const SkRect&, const SkPaint&));
  MOCK_METHOD1(DrawPicture, void(const SkPicture*));
  MOCK_METHOD1(OnDrawPicture, void(const SkPicture*));
  MOCK_METHOD3(OnDrawPicture,
               void(const SkPicture*, const SkMatrix*, const SkPaint*));
  MOCK_METHOD3(DrawPicture,
               void(const SkPicture*, const SkMatrix*, const SkPaint*));
  MOCK_METHOD5(onDrawImage2,
               void(const SkImage*,
                    SkScalar,
                    SkScalar,
                    const SkSamplingOptions&,
                    const SkPaint*));
  MOCK_METHOD6(onDrawImageRect2,
               void(const SkImage*,
                    const SkRect&,
                    const SkRect&,
                    const SkSamplingOptions&,
                    const SkPaint*,
                    SrcRectConstraint));

 private:
  Vector<Operation> recorded_operations_;
};

class PrintContextTest : public PaintTestConfigurations, public RenderingTest {
 protected:
  explicit PrintContextTest(LocalFrameClient* local_frame_client = nullptr)
      : RenderingTest(local_frame_client) {}
  ~PrintContextTest() override = default;

  void SetUp() override {
    RenderingTest::SetUp();
    print_context_ =
        MakeGarbageCollected<PrintContext>(GetDocument().GetFrame(),
                                           /*use_printing_layout=*/true);
    CanvasResourceProvider::SetMaxPinnedImageBytesForTesting(100);
  }

  void TearDown() override {
    RenderingTest::TearDown();
    CanvasRenderingContext::GetCanvasPerformanceMonitor().ResetForTesting();
    CanvasResourceProvider::ResetMaxPinnedImageBytesForTesting();
  }

  PrintContext& GetPrintContext() { return *print_context_.Get(); }

  void SetBodyInnerHTML(String body_content) {
    GetDocument().body()->setAttribute(html_names::kStyleAttr, "margin: 0");
    GetDocument().body()->setInnerHTML(body_content);
  }

  void PrintSinglePage(SkCanvas& canvas) {
    gfx::Rect page_rect(0, 0, kPageWidth, kPageHeight);
    GetDocument().SetPrinting(Document::kBeforePrinting);
    Event* event = MakeGarbageCollected<BeforePrintEvent>();
    GetPrintContext().GetFrame()->DomWindow()->DispatchEvent(*event);
    GetPrintContext().BeginPrintMode(page_rect.width(), page_rect.height());
    GetDocument().View()->UpdateAllLifecyclePhasesExceptPaint(
        DocumentUpdateReason::kTest);
    auto* builder = MakeGarbageCollected<PaintRecordBuilder>();
    GraphicsContext& context = builder->Context();
    context.SetPrinting(true);
    GetDocument().View()->PaintOutsideOfLifecycle(
        context, PaintFlag::kAddUrlMetadata, CullRect(page_rect));
    {
      DrawingRecorder recorder(
          context, *GetDocument().GetLayoutView(),
          DisplayItem::kPrintedContentDestinationLocations);
      GetPrintContext().OutputLinkedDestinations(context, page_rect);
    }
    builder->EndRecording()->Playback(&canvas);
    GetPrintContext().EndPrintMode();
  }

  static String AbsoluteBlockHtmlForLink(int x,
                                         int y,
                                         int width,
                                         int height,
                                         String url,
                                         String children = String()) {
    WTF::TextStream ts;
    ts << "<a style='position: absolute; left: " << x << "px; top: " << y
       << "px; width: " << width << "px; height: " << height << "px' href='"
       << url << "'>" << (children ? children : url) << "</a>";
    return ts.Release();
  }

  static String InlineHtmlForLink(String url, String children = String()) {
    WTF::TextStream ts;
    ts << "<a href='" << url << "'>" << (children ? children : url) << "</a>";
    return ts.Release();
  }

  static String HtmlForAnchor(int x, int y, String name, String text_content) {
    WTF::TextStream ts;
    ts << "<a name='" << name << "' style='position: absolute; left: " << x
       << "px; top: " << y << "px'>" << text_content << "</a>";
    return ts.Release();
  }

 private:
  std::unique_ptr<DummyPageHolder> page_holder_;
  Persistent<PrintContext> print_context_;
};

class PrintContextFrameTest : public PrintContextTest {
 public:
  PrintContextFrameTest()
      : PrintContextTest(MakeGarbageCollected<SingleChildLocalFrameClient>()) {}
};

#define EXPECT_SKRECT_EQ(expectedX, expectedY, expectedWidth, expectedHeight, \
                         actualRect)                                          \
  EXPECT_EQ(expectedX, actualRect.x());                                       \
  EXPECT_EQ(expectedY, actualRect.y());                                       \
  EXPECT_EQ(expectedWidth, actualRect.width());                               \
  EXPECT_EQ(expectedHeight, actualRect.height());

INSTANTIATE_PAINT_TEST_SUITE_P(PrintContextTest);

TEST_P(PrintContextTest, LinkTarget) {
  MockPageContextCanvas canvas;
  SetBodyInnerHTML(
      AbsoluteBlockHtmlForLink(50, 60, 70, 80, "http://www.google.com") +
      AbsoluteBlockHtmlForLink(150, 160, 170, 180,
                               "http://www.google.com#fragment"));
  PrintSinglePage(canvas);

  const Vector<MockPageContextCanvas::Operation>& operations =
      canvas.RecordedOperations();
  ASSERT_EQ(2u, operations.size());
  EXPECT_EQ(MockPageContextCanvas::kDrawRect, operations[0].type);
  EXPECT_SKRECT_EQ(50, 60, 70, 80, operations[0].rect);
  EXPECT_EQ(MockPageContextCanvas::kDrawRect, operations[1].type);
  EXPECT_SKRECT_EQ(150, 160, 170, 180, operations[1].rect);
}

TEST_P(PrintContextTest, LinkTargetUnderAnonymousBlockBeforeBlock) {
  GetDocument().SetCompatibilityMode(Document::kQuirksMode);
  MockPageContextCanvas canvas;
  SetBodyInnerHTML("<div style='padding-top: 50px'>" +
                   InlineHtmlForLink("http://www.google.com",
                                     "<img style='width: 111; height: 10'>") +
                   "<div> " +
                   InlineHtmlForLink("http://www.google1.com",
                                     "<img style='width: 122; height: 20'>") +
                   "</div>" + "</div>");
  PrintSinglePage(canvas);
  const Vector<MockPageContextCanvas::Operation>& operations =
      canvas.RecordedOperations();
  ASSERT_EQ(2u, operations.size());
  EXPECT_EQ(MockPageContextCanvas::kDrawRect, operations[0].type);
  EXPECT_SKRECT_EQ(0, 50, 111, 10, operations[0].rect);
  EXPECT_EQ(MockPageContextCanvas::kDrawRect, operations[1].type);
  EXPECT_SKRECT_EQ(0, 60, 122, 20, operations[1].rect);
}

TEST_P(PrintContextTest, LinkTargetContainingABlock) {
  GetDocument().SetCompatibilityMode(Document::kQuirksMode);
  MockPageContextCanvas canvas;
  SetBodyInnerHTML(
      "<div style='padding-top: 50px'>" +
      InlineHtmlForLink("http://www.google2.com",
                        "<div style='width:133; height: 30'>BLOCK</div>") +
      "</div>");
  PrintSinglePage(canvas);
  const Vector<MockPageContextCanvas::Operation>& operations =
      canvas.RecordedOperations();
  ASSERT_EQ(1u, operations.size());
  EXPECT_EQ(MockPageContextCanvas::kDrawRect, operations[0].type);
  EXPECT_SKRECT_EQ(0, 50, 133, 30, operations[0].rect);
}

TEST_P(PrintContextTest, LinkTargetUnderInInlines) {
  MockPageContextCanvas canvas;
  SetBodyInnerHTML(
      "<span><b><i><img style='width: 40px; height: 40px'><br>" +
      InlineHtmlForLink("http://www.google3.com",
                        "<img style='width: 144px; height: 40px'>") +
      "</i></b></span>");
  PrintSinglePage(canvas);
  const Vector<MockPageContextCanvas::Operation>& operations =
      canvas.RecordedOperations();
  ASSERT_EQ(1u, operations.size());
  EXPECT_EQ(MockPageContextCanvas::kDrawRect, operations[0].type);
  EXPECT_SKRECT_EQ(0, 40, 144, 40, operations[0].rect);
}

TEST_P(PrintContextTest, LinkTargetUnderRelativelyPositionedInline) {
  MockPageContextCanvas canvas;
  SetBodyInnerHTML(
        + "<span style='position: relative; top: 50px; left: 50px'><b><i><img style='width: 1px; height: 40px'><br>"
        + InlineHtmlForLink("http://www.google3.com", "<img style='width: 155px; height: 50px'>")
        + "</i></b></span>");
  PrintSinglePage(canvas);
  const Vector<MockPageContextCanvas::Operation>& operations =
      canvas.RecordedOperations();
  ASSERT_EQ(1u, operations.size());
  EXPECT_EQ(MockPageContextCanvas::kDrawRect, operations[0].type);
  EXPECT_SKRECT_EQ(50, 90, 155, 50, operations[0].rect);
}

TEST_P(PrintContextTest, LinkTargetSvg) {
  MockPageContextCanvas canvas;
  SetBodyInnerHTML(R"HTML(
    <svg width='100' height='100'>
    <a xlink:href='http://www.w3.org'><rect x='20' y='20' width='50'
    height='50'/></a>
    <text x='10' y='90'><a
    xlink:href='http://www.google.com'><tspan>google</tspan></a></text>
    </svg>
  )HTML");
  PrintSinglePage(canvas);

  const Vector<MockPageContextCanvas::Operation>& operations =
      canvas.RecordedOperations();
  ASSERT_EQ(2u, operations.size());
  EXPECT_EQ(MockPageContextCanvas::kDrawRect, operations[0].type);
  EXPECT_SKRECT_EQ(20, 20, 50, 50, operations[0].rect);
  EXPECT_EQ(MockPageContextCanvas::kDrawRect, operations[1].type);
  EXPECT_EQ(10, operations[1].rect.x());
  EXPECT_GE(90, operations[1].rect.y());
}

TEST_P(PrintContextTest, LinkedTarget) {
  MockPageContextCanvas canvas;
  GetDocument().SetBaseURLOverride(KURL("http://a.com/"));
  // Careful about locations, the page is 800x600 and only one page is printed.
  SetBodyInnerHTML(
      AbsoluteBlockHtmlForLink(
          50, 60, 10, 10,
          "#fragment")  // Generates a Link_Named_Dest_Key annotation
      + AbsoluteBlockHtmlForLink(50, 160, 10, 10,
                                 "#not-found")  // Generates no annotation
      + AbsoluteBlockHtmlForLink(
            50, 260, 10, 10,
            u"#\u00F6")  // Generates a Link_Named_Dest_Key annotation
      + AbsoluteBlockHtmlForLink(
            50, 360, 10, 10,
            "#")  // Generates a Link_Named_Dest_Key annotation
      + AbsoluteBlockHtmlForLink(
            50, 460, 10, 10,
            "#t%6Fp")  // Generates a Link_Named_Dest_Key annotation
      +
      HtmlForAnchor(450, 60, "fragment",
                    "fragment")  // Generates a Define_Named_Dest_Key annotation
      + HtmlForAnchor(450, 160, "fragment-not-used",
                      "fragment-not-used")  // Generates no annotation
      + HtmlForAnchor(450, 260, u"\u00F6",
                      "O")  // Generates a Define_Named_Dest_Key annotation
      // TODO(1117212): The escaped version currently takes precedence.
      //+ HtmlForAnchor(450, 360, "%C3%B6",
      //                "O2")  // Generates a Define_Named_Dest_Key annotation
  );
  PrintSinglePage(canvas);

  const Vector<MockPageContextCanvas::Operation>& operations =
      canvas.RecordedOperations();
  ASSERT_EQ(8u, operations.size());
  // The DrawRect operations come from a stable iterator.
  EXPECT_EQ(MockPageContextCanvas::kDrawRect, operations[0].type);
  EXPECT_SKRECT_EQ(50, 60, 10, 10, operations[0].rect);
  EXPECT_EQ(MockPageContextCanvas::kDrawRect, operations[1].type);
  EXPECT_SKRECT_EQ(50, 260, 10, 10, operations[1].rect);
  EXPECT_EQ(MockPageContextCanvas::kDrawRect, operations[2].type);
  EXPECT_SKRECT_EQ(50, 360, 10, 10, operations[2].rect);
  EXPECT_EQ(MockPageContextCanvas::kDrawRect, operations[3].type);
  EXPECT_SKRECT_EQ(50, 460, 10, 10, operations[3].rect);

  // The DrawPoint operations come from an unstable iterator.
  EXPECT_EQ(MockPageContextCanvas::kDrawPoint, operations[4].type);
  EXPECT_SKRECT_EQ(450, 260, 0, 0, operations[4].rect);
  EXPECT_EQ(MockPageContextCanvas::kDrawPoint, operations[5].type);
  EXPECT_SKRECT_EQ(0, 0, 0, 0, operations[5].rect);
  EXPECT_EQ(MockPageContextCanvas::kDrawPoint, operations[6].type);
  EXPECT_SKRECT_EQ(0, 0, 0, 0, operations[6].rect);
  EXPECT_EQ(MockPageContextCanvas::kDrawPoint, operations[7].type);
  EXPECT_SKRECT_EQ(450, 60, 0, 0, operations[7].rect);
}

TEST_P(PrintContextTest, EmptyLinkedTarget) {
  MockPageContextCanvas canvas;
  GetDocument().SetBaseURLOverride(KURL("http://a.com/"));
  SetBodyInnerHTML(AbsoluteBlockHtmlForLink(50, 60, 70, 80, "#fragment") +
                   HtmlForAnchor(250, 260, "fragment", ""));
  PrintSinglePage(canvas);

  const Vector<MockPageContextCanvas::Operation>& operations =
      canvas.RecordedOperations();
  ASSERT_EQ(2u, operations.size());
  EXPECT_EQ(MockPageContextCanvas::kDrawRect, operations[0].type);
  EXPECT_SKRECT_EQ(50, 60, 70, 80, operations[0].rect);
  EXPECT_EQ(MockPageContextCanvas::kDrawPoint, operations[1].type);
  EXPECT_SKRECT_EQ(250, 260, 0, 0, operations[1].rect);
}

TEST_P(PrintContextTest, LinkTargetBoundingBox) {
  MockPageContextCanvas canvas;
  SetBodyInnerHTML(
      AbsoluteBlockHtmlForLink(50, 60, 70, 20, "http://www.google.com",
                               "<img style='width: 200px; height: 100px'>"));
  PrintSinglePage(canvas);

  const Vector<MockPageContextCanvas::Operation>& operations =
      canvas.RecordedOperations();
  ASSERT_EQ(1u, operations.size());
  EXPECT_EQ(MockPageContextCanvas::kDrawRect, operations[0].type);
  EXPECT_SKRECT_EQ(50, 60, 200, 100, operations[0].rect);
}

TEST_P(PrintContextTest, ScaledVerticalRL) {
  SetBodyInnerHTML(R"HTML(
    <style>html { writing-mode:vertical-rl; }</style>
    <div style="break-after:page;">x</div>
    <div style="inline-size:10000px; block-size:10px;"></div>
  )HTML");

  int page_count = PrintContext::NumberOfPages(GetDocument().GetFrame(),
                                               gfx::SizeF(500, 500));
  EXPECT_EQ(2, page_count);
}

INSTANTIATE_PAINT_TEST_SUITE_P(PrintContextFrameTest);

TEST_P(PrintContextFrameTest, WithSubframe) {
  GetDocument().SetBaseURLOverride(KURL("http://a.com/"));
  SetBodyInnerHTML(R"HTML(
    <style>::-webkit-scrollbar { display: none }</style>
    <iframe src='http://b.com/' width='500' height='500'
     style='border-width: 5px; margin: 5px; position: absolute; top: 90px;
    left: 90px'></iframe>
  )HTML");
  SetChildFrameHTML(
      AbsoluteBlockHtmlForLink(50, 60, 70, 80, "#fragment") +
      AbsoluteBlockHtmlForLink(150, 160, 170, 180, "http://www.google.com") +
      AbsoluteBlockHtmlForLink(250, 260, 270, 280,
                               "http://www.google.com#fragment"));

  MockPageContextCanvas canvas;
  PrintSinglePage(canvas);

  const Vector<MockPageContextCanvas::Operation>& operations =
      canvas.RecordedOperations();
  ASSERT_EQ(2u, operations.size());
  EXPECT_EQ(MockPageContextCanvas::kDrawRect, operations[0].type);
  EXPECT_SKRECT_EQ(250, 260, 170, 180, operations[0].rect);
  EXPECT_EQ(MockPageContextCanvas::kDrawRect, operations[1].type);
  EXPECT_SKRECT_EQ(350, 360, 270, 280, operations[1].rect);
}

TEST_P(PrintContextFrameTest, WithScrolledSubframe) {
  GetDocument().SetBaseURLOverride(KURL("http://a.com/"));
  SetBodyInnerHTML(R"HTML(
    <style>::-webkit-scrollbar { display: none }</style>
    <iframe src='http://b.com/' width='500' height='500'
     style='border-width: 5px; margin: 5px; position: absolute; top: 90px;
    left: 90px'></iframe>
  )HTML");
  SetChildFrameHTML(
      AbsoluteBlockHtmlForLink(10, 10, 20, 20, "http://invisible.com") +
      AbsoluteBlockHtmlForLink(50, 60, 70, 80, "http://partly.visible.com") +
      AbsoluteBlockHtmlForLink(150, 160, 170, 180, "http://www.google.com") +
      AbsoluteBlockHtmlForLink(250, 260, 270, 280,
                               "http://www.google.com#fragment") +
      AbsoluteBlockHtmlForLink(850, 860, 70, 80,
                               "http://another.invisible.com"));

  ChildDocument().domWindow()->scrollTo(100, 100);

  MockPageContextCanvas canvas;
  PrintSinglePage(canvas);

  const Vector<MockPageContextCanvas::Operation>& operations =
      canvas.RecordedOperations();
  ASSERT_EQ(3u, operations.size());
  EXPECT_EQ(MockPageContextCanvas::kDrawRect, operations[0].type);
  EXPECT_SKRECT_EQ(50, 60, 70, 80,
                   operations[0].rect);  // FIXME: the rect should be clipped.
  EXPECT_EQ(MockPageContextCanvas::kDrawRect, operations[1].type);
  EXPECT_SKRECT_EQ(150, 160, 170, 180, operations[1].rect);
  EXPECT_EQ(MockPageContextCanvas::kDrawRect, operations[2].type);
  EXPECT_SKRECT_EQ(250, 260, 270, 280, operations[2].rect);
}

// This tests that we properly resize and re-layout pages for printing.
TEST_P(PrintContextFrameTest, BasicPrintPageLayout) {
  gfx::SizeF page_size(400, 400);
  float maximum_shrink_ratio = 1.1;
  auto* node = GetDocument().documentElement();

  GetDocument().GetFrame()->StartPrinting(page_size, page_size,
                                          maximum_shrink_ratio);
  EXPECT_EQ(node->OffsetWidth(), 400);
  GetDocument().GetFrame()->EndPrinting();
  EXPECT_EQ(node->OffsetWidth(), 800);

  SetBodyInnerHTML(R"HTML(
      <div style='border: 0px; margin: 0px; background-color: #0000FF;
      width:800px; height:400px'></div>)HTML");
  GetDocument().GetFrame()->StartPrinting(page_size, page_size,
                                          maximum_shrink_ratio);
  EXPECT_EQ(node->OffsetWidth(), 440);
  GetDocument().GetFrame()->EndPrinting();
  EXPECT_EQ(node->OffsetWidth(), 800);
}

TEST_P(PrintContextTest, Canvas2DBeforePrint) {
  MockPageContextCanvas canvas;
  SetBodyInnerHTML("<canvas id='c' width=100 height=100></canvas>");
  GetDocument().GetSettings()->SetScriptEnabled(true);
  Element* const script_element =
      GetDocument().CreateRawElement(html_names::kScriptTag);
  script_element->setTextContent(
      "window.addEventListener('beforeprint', (ev) => {"
      "const ctx = document.getElementById('c').getContext('2d');"
      "ctx.fillRect(0, 0, 10, 10);"
      "ctx.fillRect(50, 50, 10, 10);"
      "});");
  GetDocument().body()->AppendChild(script_element);

  EXPECT_CALL(canvas, onDrawRect(_, _)).Times(testing::AtLeast(2));

  PrintSinglePage(canvas);
}

TEST_P(PrintContextTest, Canvas2DPixelated) {
  MockPageContextCanvas canvas;
  SetBodyInnerHTML(
      "<canvas id='c' style='image-rendering: pixelated' "
      "width=100 height=100></canvas>");
  GetDocument().GetSettings()->SetScriptEnabled(true);
  Element* const script_element =
      GetDocument().CreateRawElement(html_names::kScriptTag);
  script_element->setTextContent(
      "window.addEventListener('beforeprint', (ev) => {"
      "const ctx = document.getElementById('c').getContext('2d');"
      "ctx.fillRect(0, 0, 10, 10);"
      "ctx.fillRect(50, 50, 10, 10);"
      "});");
  GetDocument().body()->AppendChild(script_element);

  EXPECT_CALL(canvas, onDrawImageRect2(_, _, _, _, _, _));

  PrintSinglePage(canvas);
}

TEST_P(PrintContextTest, Canvas2DAutoFlushingSuppressed) {
  // When printing, we're supposed to make a best effore to avoid flushing
  // a canvas's PaintOps in order to support vector printing whenever possible.
  MockPageContextCanvas canvas;
  SetBodyInnerHTML("<canvas id='c' width=200 height=100></canvas>");
  GetDocument().GetSettings()->SetScriptEnabled(true);
  Element* const script_element =
      GetDocument().CreateRawElement(html_names::kScriptTag);
  // Note: source_canvas is 10x10, which consumes 400 bytes for pixel data,
  // which is larger than the 100 limit set in PrintContextTest::SetUp().
  script_element->setTextContent(
      "source_canvas = document.createElement('canvas');"
      "source_canvas.width = 10;"
      "source_canvas.height = 10;"
      "source_ctx = source_canvas.getContext('2d');"
      "source_ctx.fillRect(1000, 0, 1, 1);"
      "window.addEventListener('beforeprint', (ev) => {"
      "  ctx = document.getElementById('c').getContext('2d');"
      "  ctx.fillStyle = 'green';"
      "  ctx.fillRect(0, 0, 100, 100);"
      "  ctx.drawImage(source_canvas, 101, 0);"
      // Next op normally triggers an auto-flush due to exceeded memory limit
      // but in this case, the auto-flush is suppressed.
      "  ctx.fillRect(0, 0, 1, 1);"
      "});");
  GetDocument().body()->AppendChild(script_element);

  // Verify that the auto-flush was suppressed by checking that the first
  // fillRect call flowed through to 'canvas'.
  testing::Sequence s;
  // The initial clear and the first fillRect call
  EXPECT_CALL(canvas, onDrawRect(_, _))
      .Times(testing::Exactly(2))
      .InSequence(s);
  // The drawImage call
  EXPECT_CALL(canvas, onDrawImageRect2(_, _, _, _, _, _)).InSequence(s);
  // The secondFillRect
  EXPECT_CALL(canvas, onDrawRect(_, _)).InSequence(s);

  PrintSinglePage(canvas);
}

// For testing printing behavior when 2d canvases are gpu-accelerated.
class PrintContextAcceleratedCanvasTest : public PrintContextTest {
 public:
  void SetUp() override {
    accelerated_canvas_scope_ =
        std::make_unique<ScopedAccelerated2dCanvasForTest>(true);
    test_context_provider_ = viz::TestContextProvider::Create();
    InitializeSharedGpuContext(test_context_provider_.get());

    PrintContextTest::SetUp();

    GetDocument().GetSettings()->SetAcceleratedCompositingEnabled(true);
  }

  void TearDown() override {
    // Call base class TeardDown first to ensure Canvas2DLayerBridge is
    // destroyed before the TestContextProvider.
    PrintContextTest::TearDown();

    SharedGpuContext::ResetForTesting();
    test_context_provider_ = nullptr;
    accelerated_canvas_scope_ = nullptr;
  }

 private:
  scoped_refptr<viz::TestContextProvider> test_context_provider_;
  std::unique_ptr<ScopedAccelerated2dCanvasForTest> accelerated_canvas_scope_;
};

INSTANTIATE_PAINT_TEST_SUITE_P(PrintContextAcceleratedCanvasTest);

TEST_P(PrintContextAcceleratedCanvasTest, Canvas2DBeforePrint) {
  MockPageContextCanvas canvas;
  SetBodyInnerHTML("<canvas id='c' width=100 height=100></canvas>");
  GetDocument().GetSettings()->SetScriptEnabled(true);
  Element* const script_element =
      GetDocument().CreateRawElement(html_names::kScriptTag);
  script_element->setTextContent(
      "window.addEventListener('beforeprint', (ev) => {"
      "const ctx = document.getElementById('c').getContext('2d');"
      "ctx.fillRect(0, 0, 10, 10);"
      "ctx.fillRect(50, 50, 10, 10);"
      "});");
  GetDocument().body()->AppendChild(script_element);

  // Initial clear + 2 fillRects.
  EXPECT_CALL(canvas, onDrawRect(_, _)).Times(testing::Exactly(3));

  PrintSinglePage(canvas);
}

// For testing printing behavior when 2d canvas contexts use oop rasterization.
class PrintContextOOPRCanvasTest : public PrintContextTest {
 public:
  void SetUp() override {
    accelerated_canvas_scope_ =
        std::make_unique<ScopedAccelerated2dCanvasForTest>(true);
    std::unique_ptr<viz::TestGLES2Interface> gl_context =
        std::make_unique<viz::TestGLES2Interface>();
    gl_context->set_supports_oop_raster(true);
    std::unique_ptr<viz::TestContextSupport> context_support =
        std::make_unique<viz::TestContextSupport>();
    std::unique_ptr<viz::TestRasterInterface> raster_interface =
        std::make_unique<viz::TestRasterInterface>();
    test_context_provider_ = base::MakeRefCounted<viz::TestContextProvider>(
        std::move(context_support), std::move(gl_context),
        std::move(raster_interface),
        /*shared_image_interface=*/nullptr,
        /*support_locking=*/false);

    InitializeSharedGpuContext(test_context_provider_.get());

    PrintContextTest::SetUp();

    GetDocument().GetSettings()->SetAcceleratedCompositingEnabled(true);
  }

  void TearDown() override {
    // Call base class TeardDown first to ensure Canvas2DLayerBridge is
    // destroyed before the TestContextProvider.
    PrintContextTest::TearDown();

    SharedGpuContext::ResetForTesting();
    test_context_provider_ = nullptr;
    accelerated_canvas_scope_ = nullptr;
  }

 private:
  scoped_refptr<viz::TestContextProvider> test_context_provider_;
  std::unique_ptr<ScopedAccelerated2dCanvasForTest> accelerated_canvas_scope_;
};

INSTANTIATE_PAINT_TEST_SUITE_P(PrintContextOOPRCanvasTest);

TEST_P(PrintContextOOPRCanvasTest, Canvas2DBeforePrint) {
  MockPageContextCanvas canvas;
  SetBodyInnerHTML("<canvas id='c' width=100 height=100></canvas>");
  GetDocument().GetSettings()->SetScriptEnabled(true);
  Element* const script_element =
      GetDocument().CreateRawElement(html_names::kScriptTag);
  script_element->setTextContent(
      "window.addEventListener('beforeprint', (ev) => {"
      "const ctx = document.getElementById('c').getContext('2d');"
      "ctx.fillRect(0, 0, 10, 10);"
      "ctx.fillRect(50, 50, 10, 10);"
      "});");
  GetDocument().body()->AppendChild(script_element);

  // Initial clear + 2 fillRects.
  EXPECT_CALL(canvas, onDrawRect(_, _)).Times(testing::Exactly(3));

  PrintSinglePage(canvas);
}

TEST_P(PrintContextOOPRCanvasTest, Canvas2DFlushForImageListener) {
  base::test::ScopedFeatureList feature_list_;
  feature_list_.InitAndEnableFeature(features::kCanvas2dStaysGPUOnReadback);
  // Verifies that a flush triggered by a change to a source canvas results
  // in printing falling out of vector print mode.

  // This test needs to run with CanvasOopRasterization enabled in order to
  // exercise the FlushForImageListener code path in CanvasResourceProvider.
  MockPageContextCanvas canvas;
  SetBodyInnerHTML("<canvas id='c' width=200 height=100></canvas>");
  GetDocument().GetSettings()->SetScriptEnabled(true);
  Element* const script_element =
      GetDocument().CreateRawElement(html_names::kScriptTag);
  script_element->setTextContent(
      "source_canvas = document.createElement('canvas');"
      "source_canvas.width = 5;"
      "source_canvas.height = 5;"
      "source_ctx = source_canvas.getContext('2d');"
      "source_ctx.fillRect(0, 0, 1, 1);"
      "image_data = source_ctx.getImageData(0, 0, 5, 5);"
      "window.addEventListener('beforeprint', (ev) => {"
      "  ctx = document.getElementById('c').getContext('2d');"
      "  ctx.drawImage(source_canvas, 0, 0);"
      // Touching source_ctx forces a flush of both contexts, which cancels
      // vector printing.
      "  source_ctx.putImageData(image_data, 0, 0);"
      "  ctx.fillRect(0, 0, 1, 1);"
      "});");
  GetDocument().body()->AppendChild(script_element);

  // Verify that the auto-flush caused the canvas printing to fall out of
  // vector mode.
  testing::Sequence s;
  // The initial clear
  EXPECT_CALL(canvas, onDrawRect(_, _)).InSequence(s);
  // The bitmap blit
  EXPECT_CALL(canvas, onDrawImageRect2(_, _, _, _, _, _)).InSequence(s);
  // The fill rect in the event listener should leave no trace here because
  // it is supposed to be included in the canvas blit.
  EXPECT_CALL(canvas, onDrawRect(_, _))
      .Times(testing::Exactly(0))
      .InSequence(s);

  PrintSinglePage(canvas);
}

TEST_P(PrintContextOOPRCanvasTest, Canvas2DNoFlushForImageListener) {
  // Verifies that a the canvas printing stays in vector mode after a
  // canvas to canvas drawImage, as long as the source canvas is not
  // touched afterwards.
  MockPageContextCanvas canvas;
  SetBodyInnerHTML("<canvas id='c' width=200 height=100></canvas>");
  GetDocument().GetSettings()->SetScriptEnabled(true);
  Element* const script_element =
      GetDocument().CreateRawElement(html_names::kScriptTag);
  script_element->setTextContent(
      "source_canvas = document.createElement('canvas');"
      "source_canvas.width = 5;"
      "source_canvas.height = 5;"
      "source_ctx = source_canvas.getContext('2d');"
      "source_ctx.fillRect(0, 0, 1, 1);"
      "window.addEventListener('beforeprint', (ev) => {"
      "  ctx = document.getElementById('c').getContext('2d');"
      "  ctx.fillStyle = 'green';"
      "  ctx.fillRect(0, 0, 100, 100);"
      "  ctx.drawImage(source_canvas, 0, 0, 5, 5, 101, 0, 10, 10);"
      "  ctx.fillRect(0, 0, 1, 1);"
      "});");
  GetDocument().body()->AppendChild(script_element);

  // Verify that the auto-flush caused the canvas printing to fall out of
  // vector mode.
  testing::Sequence s;
  // The initial clear and the fillRect call
  EXPECT_CALL(canvas, onDrawRect(_, _))
      .Times(testing::Exactly(2))
      .InSequence(s);
  // The drawImage
  EXPECT_CALL(canvas, onDrawImageRect2(_, _, _, _, _, _)).InSequence(s);
  // The fill rect after the drawImage
  EXPECT_CALL(canvas, onDrawRect(_, _))
      .Times(testing::Exactly(1))
      .InSequence(s);

  PrintSinglePage(canvas);
}

TEST_P(PrintContextTest, Canvas2DAutoFlushBeforePrinting) {
  // This test verifies that if an autoflush is triggered before printing,
  // and the canvas is not cleared in the beforeprint handler, then the canvas
  // cannot be vector printed.
  MockPageContextCanvas canvas;
  SetBodyInnerHTML("<canvas id='c' width=200 height=100></canvas>");
  GetDocument().GetSettings()->SetScriptEnabled(true);
  Element* const script_element =
      GetDocument().CreateRawElement(html_names::kScriptTag);
  // Note: source_canvas is 10x10, which consumes 400 bytes for pixel data,
  // which is larger than the 100 limit set in PrintContextTest::SetUp().
  script_element->setTextContent(
      "source_canvas = document.createElement('canvas');"
      "source_canvas.width = 10;"
      "source_canvas.height = 10;"
      "source_ctx = source_canvas.getContext('2d');"
      "source_ctx.fillRect(0, 0, 1, 1);"
      "ctx = document.getElementById('c').getContext('2d');"
      "ctx.fillRect(0, 0, 100, 100);"
      "ctx.drawImage(source_canvas, 101, 0);"
      // Next op triggers an auto-flush due to exceeded memory limit
      "ctx.fillRect(0, 0, 1, 1);"
      "window.addEventListener('beforeprint', (ev) => {"
      "  ctx.fillRect(0, 0, 1, 1);"
      "});");
  GetDocument().body()->AppendChild(script_element);

  // Verify that the auto-flush caused the canvas printing to fall out of
  // vector mode.
  testing::Sequence s;
  // The initial clear
  EXPECT_CALL(canvas, onDrawRect(_, _)).InSequence(s);
  // The bitmap blit
  EXPECT_CALL(canvas, onDrawImageRect2(_, _, _, _, _, _)).InSequence(s);
  // The fill rect in the event listener should leave no trace here because
  // it is supposed to be included in the canvas blit.
  EXPECT_CALL(canvas, onDrawRect(_, _))
      .Times(testing::Exactly(0))
      .InSequence(s);

  PrintSinglePage(canvas);
}

// This tests that we don't resize or re-layout subframes in printed content.
// TODO(weili): This test fails when the iframe isn't the root scroller - e.g.
// Adding ScopedImplicitRootScrollerForTest disabler(false);
// https://crbug.com/841602.
TEST_P(PrintContextFrameTest, DISABLED_SubframePrintPageLayout) {
  SetBodyInnerHTML(R"HTML(
      <div style='border: 0px; margin: 0px; background-color: #0000FF;
      width:800px; height:400px'></div>
      <iframe id="target" src='http://b.com/' width='100%' height='100%'
      style='border: 0px; margin: 0px; position: absolute; top: 0px;
      left: 0px'></iframe>)HTML");
  gfx::SizeF page_size(400, 400);
  float maximum_shrink_ratio = 1.1;
  auto* parent = GetDocument().documentElement();
  // The child document element inside iframe.
  auto* child = ChildDocument().documentElement();
  // The iframe element in the document.
  auto* target = GetDocument().getElementById("target");

  GetDocument().GetFrame()->StartPrinting(page_size, page_size,
                                          maximum_shrink_ratio);
  EXPECT_EQ(parent->OffsetWidth(), 440);
  EXPECT_EQ(child->OffsetWidth(), 800);
  EXPECT_EQ(target->OffsetWidth(), 440);
  GetDocument().GetFrame()->EndPrinting();
  EXPECT_EQ(parent->OffsetWidth(), 800);
  EXPECT_EQ(child->OffsetWidth(), 800);
  EXPECT_EQ(target->OffsetWidth(), 800);

  GetDocument().GetFrame()->StartPrinting();
  EXPECT_EQ(parent->OffsetWidth(), 800);
  EXPECT_EQ(child->OffsetWidth(), 800);
  EXPECT_EQ(target->OffsetWidth(), 800);
  GetDocument().GetFrame()->EndPrinting();
  EXPECT_EQ(parent->OffsetWidth(), 800);
  EXPECT_EQ(child->OffsetWidth(), 800);
  EXPECT_EQ(target->OffsetWidth(), 800);

  ASSERT_TRUE(ChildDocument() != GetDocument());
  ChildDocument().GetFrame()->StartPrinting(page_size, page_size,
                                            maximum_shrink_ratio);
  EXPECT_EQ(parent->OffsetWidth(), 800);
  EXPECT_EQ(child->OffsetWidth(), 400);
  EXPECT_EQ(target->OffsetWidth(), 800);
  GetDocument().GetFrame()->EndPrinting();
  EXPECT_EQ(parent->OffsetWidth(), 800);
  //  The child frame should return to the original size.
  EXPECT_EQ(child->OffsetWidth(), 800);
  EXPECT_EQ(target->OffsetWidth(), 800);
}

}  // namespace blink
