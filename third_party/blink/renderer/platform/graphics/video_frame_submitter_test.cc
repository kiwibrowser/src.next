// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/platform/graphics/video_frame_submitter.h"

#include <memory>
#include <tuple>
#include <utility>

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/memory/ptr_util.h"
#include "base/memory/read_only_shared_memory_region.h"
#include "base/test/bind.h"
#include "base/test/simple_test_tick_clock.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "cc/layers/video_frame_provider.h"
#include "cc/metrics/video_playback_roughness_reporter.h"
#include "cc/test/layer_test_common.h"
#include "cc/trees/layer_tree_settings.h"
#include "cc/trees/task_runner_provider.h"
#include "components/viz/test/fake_external_begin_frame_source.h"
#include "components/viz/test/test_context_provider.h"
#include "media/base/video_frame.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/viz/public/mojom/compositing/compositor_frame_sink.mojom-blink.h"
#include "services/viz/public/mojom/hit_test/hit_test_region_list.mojom-blink.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/renderer/platform/graphics/test/mock_compositor_frame_sink.h"
#include "third_party/blink/renderer/platform/graphics/test/mock_embedded_frame_sink_provider.h"
#include "third_party/blink/renderer/platform/graphics/video_frame_resource_provider.h"
#include "third_party/blink/renderer/platform/wtf/functional.h"

using testing::_;
using testing::AnyNumber;
using testing::Invoke;
using testing::Return;
using testing::StrictMock;

namespace blink {

namespace {

class MockVideoFrameProvider : public cc::VideoFrameProvider {
 public:
  MockVideoFrameProvider() = default;
  MockVideoFrameProvider(const MockVideoFrameProvider&) = delete;
  MockVideoFrameProvider& operator=(const MockVideoFrameProvider&) = delete;
  ~MockVideoFrameProvider() override = default;

  MOCK_METHOD1(SetVideoFrameProviderClient, void(Client*));
  MOCK_METHOD2(UpdateCurrentFrame, bool(base::TimeTicks, base::TimeTicks));
  MOCK_METHOD0(HasCurrentFrame, bool());
  MOCK_METHOD0(GetCurrentFrame, scoped_refptr<media::VideoFrame>());
  MOCK_METHOD0(PutCurrentFrame, void());
  MOCK_METHOD0(OnContextLost, void());

  base::TimeDelta GetPreferredRenderInterval() override {
    return preferred_interval;
  }

  base::TimeDelta preferred_interval;
};

class VideoMockCompositorFrameSink
    : public viz::mojom::blink::CompositorFrameSink {
 public:
  VideoMockCompositorFrameSink(
      mojo::PendingReceiver<viz::mojom::blink::CompositorFrameSink> receiver) {
    receiver_.Bind(std::move(receiver));
  }
  VideoMockCompositorFrameSink(const VideoMockCompositorFrameSink&) = delete;
  VideoMockCompositorFrameSink& operator=(const VideoMockCompositorFrameSink&) =
      delete;
  ~VideoMockCompositorFrameSink() override = default;

  const viz::CompositorFrame& last_submitted_compositor_frame() const {
    return last_submitted_compositor_frame_;
  }

  MOCK_METHOD1(SetNeedsBeginFrame, void(bool));
  MOCK_METHOD0(SetWantsAnimateOnlyBeginFrames, void());

  MOCK_METHOD2(DoSubmitCompositorFrame,
               void(const viz::LocalSurfaceId&, viz::CompositorFrame*));
  void SubmitCompositorFrame(
      const viz::LocalSurfaceId& id,
      viz::CompositorFrame frame,
      absl::optional<viz::HitTestRegionList> hit_test_region_list,
      uint64_t submit_time) override {
    last_submitted_compositor_frame_ = std::move(frame);
    DoSubmitCompositorFrame(id, &last_submitted_compositor_frame_);
  }
  void SubmitCompositorFrameSync(
      const viz::LocalSurfaceId& id,
      viz::CompositorFrame frame,
      absl::optional<viz::HitTestRegionList> hit_test_region_list,
      uint64_t submit_time,
      const SubmitCompositorFrameSyncCallback callback) override {
    last_submitted_compositor_frame_ = std::move(frame);
    DoSubmitCompositorFrame(id, &last_submitted_compositor_frame_);
  }

  MOCK_METHOD1(DidNotProduceFrame, void(const viz::BeginFrameAck&));
  MOCK_METHOD2(DidAllocateSharedBitmap,
               void(base::ReadOnlySharedMemoryRegion region,
                    const gpu::Mailbox& id));
  MOCK_METHOD1(DidDeleteSharedBitmap, void(const gpu::Mailbox& id));
  MOCK_METHOD1(InitializeCompositorFrameSinkType,
               void(viz::mojom::CompositorFrameSinkType));
  MOCK_METHOD1(SetThreadIds, void(const WTF::Vector<int32_t>&));

 private:
  mojo::Receiver<viz::mojom::blink::CompositorFrameSink> receiver_{this};

  viz::CompositorFrame last_submitted_compositor_frame_;
};

class MockVideoFrameResourceProvider
    : public blink::VideoFrameResourceProvider {
 public:
  MockVideoFrameResourceProvider(
      viz::RasterContextProvider* context_provider,
      viz::SharedBitmapReporter* shared_bitmap_reporter)
      : blink::VideoFrameResourceProvider(cc::LayerTreeSettings(), false) {
    blink::VideoFrameResourceProvider::Initialize(context_provider,
                                                  shared_bitmap_reporter);
  }
  MockVideoFrameResourceProvider(const MockVideoFrameResourceProvider&) =
      delete;
  MockVideoFrameResourceProvider& operator=(
      const MockVideoFrameResourceProvider&) = delete;
  ~MockVideoFrameResourceProvider() override = default;

  MOCK_METHOD2(Initialize,
               void(viz::RasterContextProvider*, viz::SharedBitmapReporter*));
  MOCK_METHOD4(AppendQuads,
               void(viz::CompositorRenderPass*,
                    scoped_refptr<media::VideoFrame>,
                    media::VideoTransformation,
                    bool));
  MOCK_METHOD0(ReleaseFrameResources, void());
  MOCK_METHOD2(PrepareSendToParent,
               void(const WebVector<viz::ResourceId>&,
                    WebVector<viz::TransferableResource>*));
  MOCK_METHOD1(ReceiveReturnsFromParent,
               void(Vector<viz::ReturnedResource> transferable_resources));
  MOCK_METHOD0(ObtainContextProvider, void());
};
}  // namespace

class VideoFrameSubmitterTest : public testing::Test {
 public:
  VideoFrameSubmitterTest()
      : now_src_(new base::SimpleTestTickClock()),
        begin_frame_source_(new viz::FakeExternalBeginFrameSource(0.f, false)),
        video_frame_provider_(new StrictMock<MockVideoFrameProvider>()),
        context_provider_(viz::TestContextProvider::Create()) {
    context_provider_->BindToCurrentThread();
    MakeSubmitter();
    task_environment_.RunUntilIdle();
  }

  void MakeSubmitter() { MakeSubmitter(base::DoNothing()); }

  void MakeSubmitter(
      cc::VideoPlaybackRoughnessReporter::ReportingCallback reporting_cb) {
    resource_provider_ = new StrictMock<MockVideoFrameResourceProvider>(
        context_provider_.get(), nullptr);
    submitter_ = std::make_unique<VideoFrameSubmitter>(
        base::DoNothing(), reporting_cb,
        base::WrapUnique<MockVideoFrameResourceProvider>(resource_provider_));

    submitter_->Initialize(video_frame_provider_.get(), false);
    mojo::PendingRemote<viz::mojom::blink::CompositorFrameSink> submitter_sink;
    sink_ = std::make_unique<StrictMock<VideoMockCompositorFrameSink>>(
        submitter_sink.InitWithNewPipeAndPassReceiver());

    // By setting the submission state before we set the sink, we can make
    // testing easier without having to worry about the first sent frame.
    submitter_->SetIsSurfaceVisible(true);
    submitter_->remote_frame_sink_.Bind(std::move(submitter_sink));
    submitter_->compositor_frame_sink_ = submitter_->remote_frame_sink_.get();
    mojo::Remote<mojom::blink::SurfaceEmbedder> embedder;
    std::ignore = embedder.BindNewPipeAndPassReceiver();
    submitter_->surface_embedder_ = std::move(embedder);
    auto surface_id = viz::SurfaceId(
        viz::FrameSinkId(1, 1),
        viz::LocalSurfaceId(11,
                            base::UnguessableToken::Deserialize(0x111111, 0)));
    submitter_->frame_sink_id_ = surface_id.frame_sink_id();
    submitter_->child_local_surface_id_allocator_.UpdateFromParent(
        surface_id.local_surface_id());
  }

  bool IsRendering() const { return submitter_->is_rendering_; }

  cc::VideoFrameProvider* GetProvider() const {
    return submitter_->video_frame_provider_;
  }

  bool ShouldSubmit() const { return submitter_->ShouldSubmit(); }

  void SubmitSingleFrame() { submitter_->SubmitSingleFrame(); }

  const viz::ChildLocalSurfaceIdAllocator& child_local_surface_id_allocator()
      const {
    return submitter_->child_local_surface_id_allocator_;
  }

  gfx::Size frame_size() const { return submitter_->frame_size_; }

  void OnReceivedContextProvider(
      bool use_gpu_compositing,
      scoped_refptr<viz::RasterContextProvider> context_provider) {
    submitter_->OnReceivedContextProvider(use_gpu_compositing,
                                          std::move(context_provider));
  }

  void AckSubmittedFrame() {
    WTF::Vector<viz::ReturnedResource> resources;
    EXPECT_CALL(*resource_provider_, ReceiveReturnsFromParent(_));
    submitter_->DidReceiveCompositorFrameAck(std::move(resources));
  }

 protected:
  base::test::TaskEnvironment task_environment_;
  std::unique_ptr<base::SimpleTestTickClock> now_src_;
  std::unique_ptr<viz::FakeExternalBeginFrameSource> begin_frame_source_;
  std::unique_ptr<StrictMock<VideoMockCompositorFrameSink>> sink_;
  std::unique_ptr<StrictMock<MockVideoFrameProvider>> video_frame_provider_;
  StrictMock<MockVideoFrameResourceProvider>* resource_provider_;
  scoped_refptr<viz::TestContextProvider> context_provider_;
  std::unique_ptr<VideoFrameSubmitter> submitter_;
};

enum class SubmissionType {
  kBeginFrame,
  kStateChange,
  kManual,
};

#define EXPECT_GET_PUT_FRAME()                                                 \
  do {                                                                         \
    EXPECT_CALL(*video_frame_provider_, GetCurrentFrame())                     \
        .WillOnce(Return(media::VideoFrame::CreateFrame(                       \
            media::PIXEL_FORMAT_YV12, gfx::Size(8, 8),                         \
            gfx::Rect(gfx::Size(8, 8)), gfx::Size(8, 8), base::TimeDelta()))); \
    EXPECT_CALL(*video_frame_provider_, PutCurrentFrame());                    \
  } while (0)

// Create submission state macro to ease complexity. Use a macro instead of a
// function so that line numbers are useful in test failures.
#define EXPECT_SUBMISSION(type)                                     \
  do {                                                              \
    if (type == SubmissionType::kBeginFrame) {                      \
      EXPECT_CALL(*video_frame_provider_, UpdateCurrentFrame(_, _)) \
          .WillOnce(Return(true));                                  \
    }                                                               \
    EXPECT_GET_PUT_FRAME();                                         \
    EXPECT_CALL(*sink_, DoSubmitCompositorFrame(_, _));             \
    EXPECT_CALL(*resource_provider_, AppendQuads(_, _, _, _));      \
    EXPECT_CALL(*resource_provider_, PrepareSendToParent(_, _));    \
    EXPECT_CALL(*resource_provider_, ReleaseFrameResources());      \
  } while (0)

TEST_F(VideoFrameSubmitterTest, StatRenderingFlipsBits) {
  EXPECT_FALSE(IsRendering());
  EXPECT_CALL(*sink_, SetNeedsBeginFrame(true));

  submitter_->StartRendering();

  task_environment_.RunUntilIdle();

  EXPECT_TRUE(IsRendering());
}

TEST_F(VideoFrameSubmitterTest, StopRenderingSkipsUpdateCurrentFrame) {
  EXPECT_FALSE(IsRendering());
  EXPECT_CALL(*sink_, SetNeedsBeginFrame(true));

  submitter_->StartRendering();

  task_environment_.RunUntilIdle();

  EXPECT_TRUE(IsRendering());

  // OnBeginFrame() submits one frame.
  EXPECT_SUBMISSION(SubmissionType::kBeginFrame);
  viz::BeginFrameArgs args = begin_frame_source_->CreateBeginFrameArgs(
      BEGINFRAME_FROM_HERE, now_src_.get());
  submitter_->OnBeginFrame(args, {});
  task_environment_.RunUntilIdle();
  AckSubmittedFrame();

  // StopRendering submits one more frame.
  EXPECT_SUBMISSION(SubmissionType::kStateChange);
  EXPECT_CALL(*sink_, SetNeedsBeginFrame(false));
  submitter_->StopRendering();
  task_environment_.RunUntilIdle();
  AckSubmittedFrame();

  // No frames should be produced after StopRendering().
  EXPECT_CALL(*sink_, DidNotProduceFrame(_));
  args = begin_frame_source_->CreateBeginFrameArgs(BEGINFRAME_FROM_HERE,
                                                   now_src_.get());
  submitter_->OnBeginFrame(args, {});
  task_environment_.RunUntilIdle();
}

TEST_F(VideoFrameSubmitterTest, StopUsingProviderNullsProvider) {
  EXPECT_FALSE(IsRendering());
  EXPECT_EQ(video_frame_provider_.get(), GetProvider());

  submitter_->StopUsingProvider();

  EXPECT_EQ(nullptr, GetProvider());
}

TEST_F(VideoFrameSubmitterTest,
       StopUsingProviderSubmitsFrameAndStopsRendering) {
  EXPECT_CALL(*sink_, SetNeedsBeginFrame(true));
  submitter_->StartRendering();
  task_environment_.RunUntilIdle();

  EXPECT_TRUE(IsRendering());

  EXPECT_CALL(*sink_, SetNeedsBeginFrame(false));
  EXPECT_SUBMISSION(SubmissionType::kStateChange);
  submitter_->StopUsingProvider();

  task_environment_.RunUntilIdle();

  EXPECT_FALSE(IsRendering());
}

TEST_F(VideoFrameSubmitterTest, DidReceiveFrameStillSubmitsIfRendering) {
  EXPECT_CALL(*sink_, SetNeedsBeginFrame(true));

  submitter_->StartRendering();
  task_environment_.RunUntilIdle();

  EXPECT_TRUE(IsRendering());

  EXPECT_SUBMISSION(SubmissionType::kManual);
  submitter_->DidReceiveFrame();
  task_environment_.RunUntilIdle();
}

TEST_F(VideoFrameSubmitterTest, DidReceiveFrameSubmitsFrame) {
  EXPECT_FALSE(IsRendering());

  EXPECT_SUBMISSION(SubmissionType::kManual);
  submitter_->DidReceiveFrame();
  task_environment_.RunUntilIdle();
}

TEST_F(VideoFrameSubmitterTest, ShouldSubmitPreventsSubmission) {
  EXPECT_CALL(*sink_, SetNeedsBeginFrame(false));
  submitter_->SetIsSurfaceVisible(false);
  task_environment_.RunUntilIdle();

  EXPECT_FALSE(ShouldSubmit());

  EXPECT_CALL(*sink_, SetNeedsBeginFrame(false));
  submitter_->StartRendering();
  task_environment_.RunUntilIdle();

  EXPECT_SUBMISSION(SubmissionType::kStateChange);
  EXPECT_CALL(*sink_, SetNeedsBeginFrame(true));
  submitter_->SetIsSurfaceVisible(true);
  task_environment_.RunUntilIdle();
  AckSubmittedFrame();

  EXPECT_TRUE(ShouldSubmit());

  EXPECT_CALL(*sink_, SetNeedsBeginFrame(false));
  EXPECT_CALL(*video_frame_provider_, GetCurrentFrame()).Times(0);
  submitter_->SetIsSurfaceVisible(false);
  task_environment_.RunUntilIdle();

  EXPECT_FALSE(ShouldSubmit());

  // We should only see a GetCurrentFrame() without a PutCurrentFrame() since
  // we drop the submission because !ShouldSubmit().
  EXPECT_CALL(*video_frame_provider_, GetCurrentFrame())
      .WillOnce(Return(media::VideoFrame::CreateFrame(
          media::PIXEL_FORMAT_YV12, gfx::Size(8, 8), gfx::Rect(gfx::Size(8, 8)),
          gfx::Size(8, 8), base::TimeDelta())));

  SubmitSingleFrame();
}

// Tests that when set to true SetForceSubmit forces frame submissions.
// regardless of the internal submit state.
TEST_F(VideoFrameSubmitterTest, SetForceSubmitForcesSubmission) {
  EXPECT_CALL(*sink_, SetNeedsBeginFrame(false));
  submitter_->SetIsSurfaceVisible(false);
  task_environment_.RunUntilIdle();

  EXPECT_FALSE(ShouldSubmit());

  EXPECT_CALL(*sink_, SetNeedsBeginFrame(false));
  EXPECT_SUBMISSION(SubmissionType::kStateChange);
  submitter_->SetForceSubmit(true);
  AckSubmittedFrame();

  EXPECT_CALL(*sink_, SetNeedsBeginFrame(true));
  submitter_->StartRendering();
  task_environment_.RunUntilIdle();

  EXPECT_CALL(*sink_, SetNeedsBeginFrame(true));
  EXPECT_SUBMISSION(SubmissionType::kStateChange);
  submitter_->SetIsSurfaceVisible(true);
  task_environment_.RunUntilIdle();
  EXPECT_TRUE(ShouldSubmit());
  AckSubmittedFrame();

  EXPECT_CALL(*sink_, SetNeedsBeginFrame(true));
  EXPECT_SUBMISSION(SubmissionType::kStateChange);
  submitter_->SetIsSurfaceVisible(false);
  task_environment_.RunUntilIdle();
  EXPECT_TRUE(ShouldSubmit());
  AckSubmittedFrame();

  EXPECT_SUBMISSION(SubmissionType::kManual);
  SubmitSingleFrame();
  task_environment_.RunUntilIdle();
}

TEST_F(VideoFrameSubmitterTest, RotationInformationPassedToResourceProvider) {
  // Check to see if rotation is communicated pre-rendering.
  EXPECT_FALSE(IsRendering());

  submitter_->SetTransform(media::VideoRotation::VIDEO_ROTATION_90);

  EXPECT_CALL(*video_frame_provider_, GetCurrentFrame())
      .WillOnce(Return(media::VideoFrame::CreateFrame(
          media::PIXEL_FORMAT_YV12, gfx::Size(8, 8), gfx::Rect(gfx::Size(8, 8)),
          gfx::Size(8, 8), base::TimeDelta())));
  EXPECT_CALL(*sink_, DoSubmitCompositorFrame(_, _));
  EXPECT_CALL(*video_frame_provider_, PutCurrentFrame());
  EXPECT_CALL(*resource_provider_,
              AppendQuads(_, _,
                          media::VideoTransformation(
                              media::VideoRotation::VIDEO_ROTATION_90),
                          _));
  EXPECT_CALL(*resource_provider_, PrepareSendToParent(_, _));
  EXPECT_CALL(*resource_provider_, ReleaseFrameResources());

  submitter_->DidReceiveFrame();
  task_environment_.RunUntilIdle();
  AckSubmittedFrame();

  // Check to see if an update to rotation just before rendering is
  // communicated.
  submitter_->SetTransform(media::VideoRotation::VIDEO_ROTATION_180);

  EXPECT_CALL(*sink_, SetNeedsBeginFrame(true));
  submitter_->StartRendering();
  task_environment_.RunUntilIdle();
  AckSubmittedFrame();

  EXPECT_CALL(*video_frame_provider_, UpdateCurrentFrame(_, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*video_frame_provider_, GetCurrentFrame())
      .WillOnce(Return(media::VideoFrame::CreateFrame(
          media::PIXEL_FORMAT_YV12, gfx::Size(8, 8), gfx::Rect(gfx::Size(8, 8)),
          gfx::Size(8, 8), base::TimeDelta())));
  EXPECT_CALL(*sink_, DoSubmitCompositorFrame(_, _));
  EXPECT_CALL(*video_frame_provider_, PutCurrentFrame());
  EXPECT_CALL(*resource_provider_,
              AppendQuads(_, _,
                          media::VideoTransformation(
                              media::VideoRotation::VIDEO_ROTATION_180),
                          _));
  EXPECT_CALL(*resource_provider_, PrepareSendToParent(_, _));
  EXPECT_CALL(*resource_provider_, ReleaseFrameResources());

  viz::BeginFrameArgs args = begin_frame_source_->CreateBeginFrameArgs(
      BEGINFRAME_FROM_HERE, now_src_.get());
  submitter_->OnBeginFrame(args, {});
  task_environment_.RunUntilIdle();
  AckSubmittedFrame();

  // Check to see if changing rotation while rendering is handled.
  submitter_->SetTransform(media::VideoRotation::VIDEO_ROTATION_270);

  EXPECT_CALL(*video_frame_provider_, UpdateCurrentFrame(_, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*video_frame_provider_, GetCurrentFrame())
      .WillOnce(Return(media::VideoFrame::CreateFrame(
          media::PIXEL_FORMAT_YV12, gfx::Size(8, 8), gfx::Rect(gfx::Size(8, 8)),
          gfx::Size(8, 8), base::TimeDelta())));
  EXPECT_CALL(*sink_, DoSubmitCompositorFrame(_, _));
  EXPECT_CALL(*video_frame_provider_, PutCurrentFrame());
  EXPECT_CALL(*resource_provider_,
              AppendQuads(_, _,
                          media::VideoTransformation(
                              media::VideoRotation::VIDEO_ROTATION_270),
                          _));
  EXPECT_CALL(*resource_provider_, PrepareSendToParent(_, _));
  EXPECT_CALL(*resource_provider_, ReleaseFrameResources());

  args = begin_frame_source_->CreateBeginFrameArgs(BEGINFRAME_FROM_HERE,
                                                   now_src_.get());
  submitter_->OnBeginFrame(args, {});
  task_environment_.RunUntilIdle();
}

TEST_F(VideoFrameSubmitterTest, FrameTransformTakesPrecedent) {
  EXPECT_FALSE(IsRendering());

  submitter_->SetTransform(media::VideoRotation::VIDEO_ROTATION_90);

  EXPECT_CALL(*video_frame_provider_, GetCurrentFrame())
      .WillOnce(Return(media::VideoFrame::CreateFrame(
          media::PIXEL_FORMAT_YV12, gfx::Size(8, 8), gfx::Rect(gfx::Size(8, 8)),
          gfx::Size(8, 8), base::TimeDelta())));
  EXPECT_CALL(*sink_, DoSubmitCompositorFrame(_, _));
  EXPECT_CALL(*video_frame_provider_, PutCurrentFrame());
  EXPECT_CALL(*resource_provider_,
              AppendQuads(_, _,
                          media::VideoTransformation(
                              media::VideoRotation::VIDEO_ROTATION_90),
                          _));
  EXPECT_CALL(*resource_provider_, PrepareSendToParent(_, _));
  EXPECT_CALL(*resource_provider_, ReleaseFrameResources());

  submitter_->DidReceiveFrame();
  task_environment_.RunUntilIdle();
  AckSubmittedFrame();

  EXPECT_CALL(*sink_, SetNeedsBeginFrame(true));
  submitter_->StartRendering();
  task_environment_.RunUntilIdle();
  AckSubmittedFrame();

  auto frame = media::VideoFrame::CreateFrame(
      media::PIXEL_FORMAT_YV12, gfx::Size(8, 8), gfx::Rect(gfx::Size(8, 8)),
      gfx::Size(8, 8), base::TimeDelta());
  frame->metadata().transformation = media::VideoTransformation(
      media::VideoRotation::VIDEO_ROTATION_180, /*mirrored=*/true);

  EXPECT_CALL(*video_frame_provider_, UpdateCurrentFrame(_, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*video_frame_provider_, GetCurrentFrame())
      .WillOnce(Return(frame));
  EXPECT_CALL(*sink_, DoSubmitCompositorFrame(_, _));
  EXPECT_CALL(*video_frame_provider_, PutCurrentFrame());
  EXPECT_CALL(*resource_provider_,
              AppendQuads(_, _, frame->metadata().transformation.value(), _));
  EXPECT_CALL(*resource_provider_, PrepareSendToParent(_, _));
  EXPECT_CALL(*resource_provider_, ReleaseFrameResources());

  viz::BeginFrameArgs args = begin_frame_source_->CreateBeginFrameArgs(
      BEGINFRAME_FROM_HERE, now_src_.get());
  submitter_->OnBeginFrame(args, {});
  task_environment_.RunUntilIdle();
  AckSubmittedFrame();
}

TEST_F(VideoFrameSubmitterTest, OnBeginFrameSubmitsFrame) {
  EXPECT_CALL(*sink_, SetNeedsBeginFrame(true));

  submitter_->StartRendering();
  task_environment_.RunUntilIdle();

  EXPECT_SUBMISSION(SubmissionType::kBeginFrame);
  viz::BeginFrameArgs args = begin_frame_source_->CreateBeginFrameArgs(
      BEGINFRAME_FROM_HERE, now_src_.get());
  submitter_->OnBeginFrame(args, {});
  task_environment_.RunUntilIdle();
}

TEST_F(VideoFrameSubmitterTest, MissedFrameArgDoesNotProduceFrame) {
  EXPECT_CALL(*sink_, DidNotProduceFrame(_));

  viz::BeginFrameArgs args = begin_frame_source_->CreateBeginFrameArgs(
      BEGINFRAME_FROM_HERE, now_src_.get());
  args.type = viz::BeginFrameArgs::MISSED;
  submitter_->OnBeginFrame(args, {});
  task_environment_.RunUntilIdle();
}

TEST_F(VideoFrameSubmitterTest, MissingProviderDoesNotProduceFrame) {
  submitter_->StopUsingProvider();

  EXPECT_CALL(*sink_, DidNotProduceFrame(_));

  viz::BeginFrameArgs args = begin_frame_source_->CreateBeginFrameArgs(
      BEGINFRAME_FROM_HERE, now_src_.get());
  submitter_->OnBeginFrame(args, {});
  task_environment_.RunUntilIdle();
}

TEST_F(VideoFrameSubmitterTest, NoUpdateOnFrameDoesNotProduceFrame) {
  EXPECT_CALL(*sink_, SetNeedsBeginFrame(true));
  submitter_->StartRendering();

  EXPECT_CALL(*video_frame_provider_, UpdateCurrentFrame(_, _))
      .WillOnce(Return(false));
  EXPECT_CALL(*sink_, DidNotProduceFrame(_));

  viz::BeginFrameArgs args = begin_frame_source_->CreateBeginFrameArgs(
      BEGINFRAME_FROM_HERE, now_src_.get());
  submitter_->OnBeginFrame(args, {});
  task_environment_.RunUntilIdle();
}

TEST_F(VideoFrameSubmitterTest, NotRenderingDoesNotProduceFrame) {
  // We don't care if UpdateCurrentFrame is called or not; it doesn't matter
  // if we're not rendering.
  EXPECT_CALL(*video_frame_provider_, UpdateCurrentFrame(_, _))
      .Times(AnyNumber());
  EXPECT_CALL(*sink_, DidNotProduceFrame(_));

  viz::BeginFrameArgs args = begin_frame_source_->CreateBeginFrameArgs(
      BEGINFRAME_FROM_HERE, now_src_.get());
  submitter_->OnBeginFrame(args, {});
  task_environment_.RunUntilIdle();
}

TEST_F(VideoFrameSubmitterTest, ReturnsResourceOnCompositorAck) {
  AckSubmittedFrame();
  task_environment_.RunUntilIdle();
}

// Tests that after submitting a frame, no frame will be submitted until an ACK
// was received. This is tested by simulating another BeginFrame message.
TEST_F(VideoFrameSubmitterTest, WaitingForAckPreventsNewFrame) {
  EXPECT_CALL(*sink_, SetNeedsBeginFrame(true));

  submitter_->StartRendering();
  task_environment_.RunUntilIdle();

  EXPECT_SUBMISSION(SubmissionType::kBeginFrame);
  viz::BeginFrameArgs args = begin_frame_source_->CreateBeginFrameArgs(
      BEGINFRAME_FROM_HERE, now_src_.get());
  submitter_->OnBeginFrame(args, {});
  task_environment_.RunUntilIdle();

  // DidNotProduceFrame should be called because no frame will be submitted
  // given that the ACK is still pending.
  EXPECT_CALL(*sink_, DidNotProduceFrame(_)).Times(1);

  // UpdateCurrentFrame should still be called, however, so that the compositor
  // knows that we missed a frame.
  EXPECT_CALL(*video_frame_provider_, UpdateCurrentFrame(_, _)).Times(1);

  std::unique_ptr<base::SimpleTestTickClock> new_time =
      std::make_unique<base::SimpleTestTickClock>();
  args = begin_frame_source_->CreateBeginFrameArgs(BEGINFRAME_FROM_HERE,
                                                   new_time.get());
  submitter_->OnBeginFrame(args, {});
  task_environment_.RunUntilIdle();
}

// Similar to above but verifies the single-frame paint path.
TEST_F(VideoFrameSubmitterTest, WaitingForAckPreventsSubmitSingleFrame) {
  EXPECT_CALL(*sink_, SetNeedsBeginFrame(true));

  submitter_->StartRendering();
  task_environment_.RunUntilIdle();

  EXPECT_SUBMISSION(SubmissionType::kManual);
  submitter_->DidReceiveFrame();
  task_environment_.RunUntilIdle();

  // GetCurrentFrame() should be called, but PutCurrentFrame() should not, since
  // the frame is dropped waiting for the ack.
  EXPECT_CALL(*video_frame_provider_, GetCurrentFrame())
      .WillOnce(Return(media::VideoFrame::CreateFrame(
          media::PIXEL_FORMAT_YV12, gfx::Size(8, 8), gfx::Rect(gfx::Size(8, 8)),
          gfx::Size(8, 8), base::TimeDelta())));

  submitter_->DidReceiveFrame();
  task_environment_.RunUntilIdle();
}

// Test that after context is lost, the CompositorFrameSink is recreated but the
// SurfaceEmbedder isn't.
TEST_F(VideoFrameSubmitterTest, RecreateCompositorFrameSinkAfterContextLost) {
  MockEmbeddedFrameSinkProvider mock_embedded_frame_sink_provider;
  mojo::ReceiverSet<mojom::blink::EmbeddedFrameSinkProvider>
      embedded_frame_sink_provider_receivers;
  auto override =
      mock_embedded_frame_sink_provider.CreateScopedOverrideMojoInterface(
          embedded_frame_sink_provider_receivers);

  EXPECT_CALL(*resource_provider_, Initialize(_, _));
  EXPECT_CALL(mock_embedded_frame_sink_provider, ConnectToEmbedder(_, _))
      .Times(0);
  EXPECT_CALL(mock_embedded_frame_sink_provider, CreateCompositorFrameSink_(_))
      .Times(1);
  EXPECT_CALL(*video_frame_provider_, OnContextLost()).Times(1);
  submitter_->OnContextLost();
  OnReceivedContextProvider(true, context_provider_);
  task_environment_.RunUntilIdle();
}

// Test that after context is lost, the CompositorFrameSink is recreated but the
// SurfaceEmbedder isn't even with software compositing.
TEST_F(VideoFrameSubmitterTest,
       RecreateCompositorFrameSinkAfterContextLostSoftwareCompositing) {
  MockEmbeddedFrameSinkProvider mock_embedded_frame_sink_provider;
  mojo::ReceiverSet<mojom::blink::EmbeddedFrameSinkProvider>
      embedded_frame_sink_provider_receivers;
  auto override =
      mock_embedded_frame_sink_provider.CreateScopedOverrideMojoInterface(
          embedded_frame_sink_provider_receivers);

  EXPECT_CALL(*resource_provider_, Initialize(_, _));
  EXPECT_CALL(mock_embedded_frame_sink_provider, ConnectToEmbedder(_, _))
      .Times(0);
  EXPECT_CALL(mock_embedded_frame_sink_provider, CreateCompositorFrameSink_(_))
      .Times(1);
  EXPECT_CALL(*video_frame_provider_, OnContextLost()).Times(1);
  submitter_->OnContextLost();
  OnReceivedContextProvider(false, nullptr);
  task_environment_.RunUntilIdle();
}

// This test simulates a race condition in which the |video_frame_provider_| is
// destroyed before OnReceivedContextProvider returns.
TEST_F(VideoFrameSubmitterTest, StopUsingProviderDuringContextLost) {
  EXPECT_CALL(*sink_, SetNeedsBeginFrame(true));

  submitter_->StartRendering();
  task_environment_.RunUntilIdle();

  EXPECT_SUBMISSION(SubmissionType::kStateChange);
  EXPECT_CALL(*sink_, SetNeedsBeginFrame(false));
  submitter_->StopUsingProvider();

  task_environment_.RunUntilIdle();

  // OnReceivedContextProvider returns. We don't run the actual function
  // because it would overwrite our fake |sink_| with a real one.
  SubmitSingleFrame();

  task_environment_.RunUntilIdle();
}

// Test the behaviour of the ChildLocalSurfaceIdAllocator instance. It checks
// that the LocalSurfaceId is properly set at creation and updated when the
// video frames change.
TEST_F(VideoFrameSubmitterTest, FrameSizeChangeUpdatesLocalSurfaceId) {
  {
    viz::LocalSurfaceId local_surface_id =
        child_local_surface_id_allocator().GetCurrentLocalSurfaceId();
    EXPECT_TRUE(local_surface_id.is_valid());
    EXPECT_EQ(11u, local_surface_id.parent_sequence_number());
    EXPECT_EQ(viz::kInitialChildSequenceNumber,
              local_surface_id.child_sequence_number());
    EXPECT_TRUE(frame_size().IsEmpty());
  }

  EXPECT_CALL(*sink_, SetNeedsBeginFrame(true));

  submitter_->StartRendering();
  task_environment_.RunUntilIdle();

  EXPECT_SUBMISSION(SubmissionType::kManual);
  SubmitSingleFrame();
  task_environment_.RunUntilIdle();

  {
    viz::LocalSurfaceId local_surface_id =
        child_local_surface_id_allocator().GetCurrentLocalSurfaceId();
    EXPECT_TRUE(local_surface_id.is_valid());
    EXPECT_EQ(11u, local_surface_id.parent_sequence_number());
    EXPECT_EQ(viz::kInitialChildSequenceNumber,
              local_surface_id.child_sequence_number());
    EXPECT_EQ(gfx::Size(8, 8), frame_size());
    AckSubmittedFrame();
  }

  EXPECT_CALL(*video_frame_provider_, GetCurrentFrame())
      .WillOnce(Return(media::VideoFrame::CreateFrame(
          media::PIXEL_FORMAT_YV12, gfx::Size(2, 2), gfx::Rect(gfx::Size(2, 2)),
          gfx::Size(2, 2), base::TimeDelta())));
  EXPECT_CALL(*sink_, DoSubmitCompositorFrame(_, _));
  EXPECT_CALL(*video_frame_provider_, PutCurrentFrame());
  EXPECT_CALL(*resource_provider_, AppendQuads(_, _, _, _));
  EXPECT_CALL(*resource_provider_, PrepareSendToParent(_, _));
  EXPECT_CALL(*resource_provider_, ReleaseFrameResources());

  SubmitSingleFrame();
  task_environment_.RunUntilIdle();

  {
    viz::LocalSurfaceId local_surface_id =
        child_local_surface_id_allocator().GetCurrentLocalSurfaceId();
    EXPECT_TRUE(local_surface_id.is_valid());
    EXPECT_EQ(11u, local_surface_id.parent_sequence_number());
    EXPECT_EQ(viz::kInitialChildSequenceNumber + 1,
              local_surface_id.child_sequence_number());
    EXPECT_EQ(gfx::Size(2, 2), frame_size());
  }
}

TEST_F(VideoFrameSubmitterTest, VideoRotationOutputRect) {
  MakeSubmitter();
  EXPECT_CALL(*sink_, SetNeedsBeginFrame(true));
  submitter_->StartRendering();
  EXPECT_TRUE(IsRendering());

  gfx::Size coded_size(1280, 720);
  gfx::Size natural_size(1280, 1024);
  gfx::Size rotated_size(1024, 1280);

  {
    submitter_->SetTransform(media::VideoRotation::VIDEO_ROTATION_90);

    EXPECT_CALL(*video_frame_provider_, UpdateCurrentFrame(_, _))
        .WillOnce(Return(true));
    EXPECT_CALL(*video_frame_provider_, GetCurrentFrame())
        .WillOnce(Return(media::VideoFrame::CreateFrame(
            media::PIXEL_FORMAT_YV12, coded_size, gfx::Rect(coded_size),
            natural_size, base::TimeDelta())));
    EXPECT_CALL(*sink_, DoSubmitCompositorFrame(_, _));
    EXPECT_CALL(*video_frame_provider_, PutCurrentFrame());
    EXPECT_CALL(*resource_provider_,
                AppendQuads(_, _,
                            media::VideoTransformation(
                                media::VideoRotation::VIDEO_ROTATION_90),
                            _));
    EXPECT_CALL(*resource_provider_, PrepareSendToParent(_, _));
    EXPECT_CALL(*resource_provider_, ReleaseFrameResources());

    viz::BeginFrameArgs args = begin_frame_source_->CreateBeginFrameArgs(
        BEGINFRAME_FROM_HERE, now_src_.get());
    submitter_->OnBeginFrame(args, {});
    task_environment_.RunUntilIdle();

    EXPECT_EQ(sink_->last_submitted_compositor_frame().size_in_pixels(),
              rotated_size);

    AckSubmittedFrame();
  }

  {
    submitter_->SetTransform(media::VideoRotation::VIDEO_ROTATION_180);

    EXPECT_CALL(*video_frame_provider_, UpdateCurrentFrame(_, _))
        .WillOnce(Return(true));
    EXPECT_CALL(*video_frame_provider_, GetCurrentFrame())
        .WillOnce(Return(media::VideoFrame::CreateFrame(
            media::PIXEL_FORMAT_YV12, coded_size, gfx::Rect(coded_size),
            natural_size, base::TimeDelta())));
    EXPECT_CALL(*sink_, DoSubmitCompositorFrame(_, _));
    EXPECT_CALL(*video_frame_provider_, PutCurrentFrame());
    EXPECT_CALL(*resource_provider_,
                AppendQuads(_, _,
                            media::VideoTransformation(
                                media::VideoRotation::VIDEO_ROTATION_180),
                            _));
    EXPECT_CALL(*resource_provider_, PrepareSendToParent(_, _));
    EXPECT_CALL(*resource_provider_, ReleaseFrameResources());

    viz::BeginFrameArgs args = begin_frame_source_->CreateBeginFrameArgs(
        BEGINFRAME_FROM_HERE, now_src_.get());
    submitter_->OnBeginFrame(args, {});
    task_environment_.RunUntilIdle();

    // 180 deg rotation has same size.
    EXPECT_EQ(sink_->last_submitted_compositor_frame().size_in_pixels(),
              natural_size);

    AckSubmittedFrame();
  }

  {
    submitter_->SetTransform(media::VideoRotation::VIDEO_ROTATION_270);

    EXPECT_CALL(*video_frame_provider_, UpdateCurrentFrame(_, _))
        .WillOnce(Return(true));
    EXPECT_CALL(*video_frame_provider_, GetCurrentFrame())
        .WillOnce(Return(media::VideoFrame::CreateFrame(
            media::PIXEL_FORMAT_YV12, coded_size, gfx::Rect(coded_size),
            natural_size, base::TimeDelta())));
    EXPECT_CALL(*sink_, DoSubmitCompositorFrame(_, _));
    EXPECT_CALL(*video_frame_provider_, PutCurrentFrame());
    EXPECT_CALL(*resource_provider_,
                AppendQuads(_, _,
                            media::VideoTransformation(
                                media::VideoRotation::VIDEO_ROTATION_270),
                            _));
    EXPECT_CALL(*resource_provider_, PrepareSendToParent(_, _));
    EXPECT_CALL(*resource_provider_, ReleaseFrameResources());

    viz::BeginFrameArgs args = begin_frame_source_->CreateBeginFrameArgs(
        BEGINFRAME_FROM_HERE, now_src_.get());
    submitter_->OnBeginFrame(args, {});
    task_environment_.RunUntilIdle();

    EXPECT_EQ(sink_->last_submitted_compositor_frame().size_in_pixels(),
              rotated_size);

    AckSubmittedFrame();
  }
}

TEST_F(VideoFrameSubmitterTest, PageVisibilityControlsSubmission) {
  // Hide the page and ensure no begin frames are issued.
  EXPECT_CALL(*sink_, SetNeedsBeginFrame(false));
  submitter_->SetIsPageVisible(false);
  task_environment_.RunUntilIdle();
  EXPECT_FALSE(ShouldSubmit());

  // Start rendering, but since page is hidden nothing should start yet.
  EXPECT_CALL(*sink_, SetNeedsBeginFrame(false));
  submitter_->StartRendering();
  task_environment_.RunUntilIdle();

  // Mark the page as visible and confirm frame submission.
  EXPECT_CALL(*sink_, SetNeedsBeginFrame(true));
  EXPECT_SUBMISSION(SubmissionType::kStateChange);
  submitter_->SetIsPageVisible(true);
  task_environment_.RunUntilIdle();

  // Transition back to the page being hidden and ensure begin frames stop.
  EXPECT_TRUE(ShouldSubmit());
  EXPECT_CALL(*sink_, SetNeedsBeginFrame(false));
  EXPECT_CALL(*video_frame_provider_, GetCurrentFrame()).Times(0);
  submitter_->SetIsPageVisible(false);
  task_environment_.RunUntilIdle();
}

TEST_F(VideoFrameSubmitterTest, PreferredInterval) {
  video_frame_provider_->preferred_interval = base::Seconds(1);

  EXPECT_CALL(*sink_, SetNeedsBeginFrame(true));

  submitter_->StartRendering();
  task_environment_.RunUntilIdle();

  EXPECT_SUBMISSION(SubmissionType::kBeginFrame);
  viz::BeginFrameArgs args = begin_frame_source_->CreateBeginFrameArgs(
      BEGINFRAME_FROM_HERE, now_src_.get());
  submitter_->OnBeginFrame(args, {});
  task_environment_.RunUntilIdle();

  EXPECT_EQ(sink_->last_submitted_compositor_frame()
                .metadata.preferred_frame_interval,
            video_frame_provider_->preferred_interval);
}

TEST_F(VideoFrameSubmitterTest, NoDuplicateFramesOnBeginFrame) {
  EXPECT_CALL(*sink_, SetNeedsBeginFrame(true));
  submitter_->StartRendering();
  task_environment_.RunUntilIdle();
  EXPECT_TRUE(IsRendering());

  auto vf = media::VideoFrame::CreateFrame(
      media::PIXEL_FORMAT_YV12, gfx::Size(8, 8), gfx::Rect(gfx::Size(8, 8)),
      gfx::Size(8, 8), base::TimeDelta());

  EXPECT_CALL(*video_frame_provider_, UpdateCurrentFrame(_, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*video_frame_provider_, GetCurrentFrame()).WillOnce(Return(vf));
  EXPECT_CALL(*video_frame_provider_, PutCurrentFrame());
  EXPECT_CALL(*sink_, DoSubmitCompositorFrame(_, _));
  EXPECT_CALL(*resource_provider_, AppendQuads(_, _, _, _));
  EXPECT_CALL(*resource_provider_, PrepareSendToParent(_, _));
  EXPECT_CALL(*resource_provider_, ReleaseFrameResources());
  viz::BeginFrameArgs args = begin_frame_source_->CreateBeginFrameArgs(
      BEGINFRAME_FROM_HERE, now_src_.get());
  submitter_->OnBeginFrame(args, {});
  task_environment_.RunUntilIdle();
  AckSubmittedFrame();

  // Trying to submit the same frame again does nothing... even if
  // UpdateCurrentFrame() lies about there being a new frame.
  EXPECT_CALL(*video_frame_provider_, UpdateCurrentFrame(_, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*video_frame_provider_, GetCurrentFrame()).WillOnce(Return(vf));
  EXPECT_CALL(*sink_, DidNotProduceFrame(_));
  args = begin_frame_source_->CreateBeginFrameArgs(BEGINFRAME_FROM_HERE,
                                                   now_src_.get());
  submitter_->OnBeginFrame(args, {});
  task_environment_.RunUntilIdle();
}

TEST_F(VideoFrameSubmitterTest, NoDuplicateFramesDidReceiveFrame) {
  auto vf = media::VideoFrame::CreateFrame(
      media::PIXEL_FORMAT_YV12, gfx::Size(8, 8), gfx::Rect(gfx::Size(8, 8)),
      gfx::Size(8, 8), base::TimeDelta());

  EXPECT_CALL(*video_frame_provider_, GetCurrentFrame()).WillOnce(Return(vf));
  EXPECT_CALL(*video_frame_provider_, PutCurrentFrame());
  EXPECT_CALL(*sink_, DoSubmitCompositorFrame(_, _));
  EXPECT_CALL(*resource_provider_, AppendQuads(_, _, _, _));
  EXPECT_CALL(*resource_provider_, PrepareSendToParent(_, _));
  EXPECT_CALL(*resource_provider_, ReleaseFrameResources());
  submitter_->DidReceiveFrame();
  task_environment_.RunUntilIdle();
  AckSubmittedFrame();

  // Trying to submit the same frame again does nothing...
  EXPECT_CALL(*video_frame_provider_, GetCurrentFrame()).WillOnce(Return(vf));
  submitter_->DidReceiveFrame();
  task_environment_.RunUntilIdle();
}

TEST_F(VideoFrameSubmitterTest, ZeroSizedFramesAreNotSubmitted) {
  auto vf = media::VideoFrame::CreateEOSFrame();
  ASSERT_TRUE(vf->natural_size().IsEmpty());

  EXPECT_CALL(*video_frame_provider_, GetCurrentFrame()).WillOnce(Return(vf));
  EXPECT_CALL(*sink_, DoSubmitCompositorFrame(_, _)).Times(0);
  submitter_->DidReceiveFrame();
  task_environment_.RunUntilIdle();
}

// Check that given enough frames with wallclock duration and enough
// presentation feedback data, VideoFrameSubmitter will call the video roughness
// reporting callback.
TEST_F(VideoFrameSubmitterTest, ProcessTimingDetails) {
  int fps = 24;
  int reports = 0;
  base::TimeDelta frame_duration = base::Seconds(1.0 / fps);
  int frames_to_run =
      fps * (cc::VideoPlaybackRoughnessReporter::kMinWindowsBeforeSubmit + 1);
  WTF::HashMap<uint32_t, viz::FrameTimingDetails> timing_details;

  MakeSubmitter(base::BindLambdaForTesting(
      [&](const cc::VideoPlaybackRoughnessReporter::Measurement& measurement) {
        ASSERT_EQ(measurement.frame_size.width(), 8);
        ASSERT_EQ(measurement.frame_size.height(), 8);
        reports++;
      }));
  EXPECT_CALL(*sink_, SetNeedsBeginFrame(true));
  submitter_->StartRendering();
  task_environment_.RunUntilIdle();
  EXPECT_TRUE(IsRendering());

  auto sink_submit = [&](const viz::LocalSurfaceId&,
                         viz::CompositorFrame* frame) {
    auto token = frame->metadata.frame_token;
    viz::FrameTimingDetails details;
    details.presentation_feedback.timestamp =
        base::TimeTicks() + frame_duration * token;
    details.presentation_feedback.flags =
        gfx::PresentationFeedback::kHWCompletion;
    timing_details.clear();
    timing_details.Set(token, details);
  };

  EXPECT_CALL(*video_frame_provider_, UpdateCurrentFrame)
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*video_frame_provider_, PutCurrentFrame).Times(AnyNumber());
  EXPECT_CALL(*sink_, DoSubmitCompositorFrame)
      .WillRepeatedly(Invoke(sink_submit));
  EXPECT_CALL(*resource_provider_, AppendQuads).Times(AnyNumber());
  EXPECT_CALL(*resource_provider_, PrepareSendToParent).Times(AnyNumber());
  EXPECT_CALL(*resource_provider_, ReleaseFrameResources).Times(AnyNumber());

  for (int i = 0; i < frames_to_run; i++) {
    auto frame = media::VideoFrame::CreateFrame(
        media::PIXEL_FORMAT_YV12, gfx::Size(8, 8), gfx::Rect(gfx::Size(8, 8)),
        gfx::Size(8, 8), i * frame_duration);
    frame->metadata().wallclock_frame_duration = frame_duration;
    EXPECT_CALL(*video_frame_provider_, GetCurrentFrame())
        .WillRepeatedly(Return(frame));

    auto args = begin_frame_source_->CreateBeginFrameArgs(BEGINFRAME_FROM_HERE,
                                                          now_src_.get());
    submitter_->OnBeginFrame(args, timing_details);
    task_environment_.RunUntilIdle();
    AckSubmittedFrame();
  }
  submitter_->StopRendering();
  EXPECT_EQ(reports, 1);
}

}  // namespace blink
