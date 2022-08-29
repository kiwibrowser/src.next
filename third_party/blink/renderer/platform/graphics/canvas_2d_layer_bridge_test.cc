/*
 * Copyright (C) 2011 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

#include "third_party/blink/renderer/platform/graphics/canvas_2d_layer_bridge.h"

#include <utility>

#include "base/location.h"
#include "base/memory/scoped_refptr.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_runner.h"
#include "build/build_config.h"
#include "cc/layers/texture_layer.h"
#include "cc/paint/paint_flags.h"
#include "cc/test/skia_common.h"
#include "cc/test/stub_decode_cache.h"
#include "components/viz/common/resources/release_callback.h"
#include "components/viz/common/resources/transferable_resource.h"
#include "components/viz/test/test_context_provider.h"
#include "components/viz/test/test_gles2_interface.h"
#include "components/viz/test/test_gpu_memory_buffer_manager.h"
#include "gpu/GLES2/gl2extchromium.h"
#include "gpu/command_buffer/client/gles2_interface.h"
#include "gpu/command_buffer/common/capabilities.h"
#include "gpu/command_buffer/common/gpu_memory_buffer_support.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/platform/platform.h"
#include "third_party/blink/renderer/platform/graphics/canvas_resource_host.h"
#include "third_party/blink/renderer/platform/graphics/canvas_resource_provider.h"
#include "third_party/blink/renderer/platform/graphics/gpu/shared_gpu_context.h"
#include "third_party/blink/renderer/platform/graphics/static_bitmap_image.h"
#include "third_party/blink/renderer/platform/graphics/test/fake_canvas_resource_host.h"
#include "third_party/blink/renderer/platform/graphics/test/gpu_memory_buffer_test_platform.h"
#include "third_party/blink/renderer/platform/graphics/test/gpu_test_utils.h"
#include "third_party/blink/renderer/platform/graphics/web_graphics_context_3d_provider_wrapper.h"
#include "third_party/blink/renderer/platform/scheduler/public/thread.h"
#include "third_party/blink/renderer/platform/scheduler/public/thread_scheduler.h"
#include "third_party/blink/renderer/platform/testing/runtime_enabled_features_test_helpers.h"
#include "third_party/blink/renderer/platform/wtf/cross_thread_functional.h"
#include "third_party/skia/include/core/SkSurface.h"
#include "third_party/skia/include/gpu/GrDirectContext.h"
#include "third_party/skia/include/gpu/gl/GrGLTypes.h"

#include <memory>

using testing::_;
using testing::AnyNumber;
using testing::AtLeast;
using testing::InSequence;
using testing::Pointee;
using testing::Return;
using testing::SetArgPointee;
using testing::Test;

namespace blink {

namespace {

class ImageTrackingDecodeCache : public cc::StubDecodeCache {
 public:
  ImageTrackingDecodeCache() = default;
  ~ImageTrackingDecodeCache() override { EXPECT_EQ(num_locked_images_, 0); }

  cc::DecodedDrawImage GetDecodedImageForDraw(
      const cc::DrawImage& image) override {
    EXPECT_FALSE(disallow_cache_use_);

    num_locked_images_++;
    decoded_images_.push_back(image);
    SkBitmap bitmap;
    bitmap.allocPixelsFlags(SkImageInfo::MakeN32Premul(10, 10),
                            SkBitmap::kZeroPixels_AllocFlag);
    sk_sp<SkImage> sk_image = SkImage::MakeFromBitmap(bitmap);
    return cc::DecodedDrawImage(
        sk_image, nullptr, SkSize::Make(0, 0), SkSize::Make(1, 1),
        cc::PaintFlags::FilterQuality::kLow, !budget_exceeded_);
  }

  void set_budget_exceeded(bool exceeded) { budget_exceeded_ = exceeded; }
  void set_disallow_cache_use(bool disallow) { disallow_cache_use_ = disallow; }

  void DrawWithImageFinished(
      const cc::DrawImage& image,
      const cc::DecodedDrawImage& decoded_image) override {
    EXPECT_FALSE(disallow_cache_use_);
    num_locked_images_--;
  }

  const Vector<cc::DrawImage>& decoded_images() const {
    return decoded_images_;
  }
  int num_locked_images() const { return num_locked_images_; }

 private:
  Vector<cc::DrawImage> decoded_images_;
  int num_locked_images_ = 0;
  bool budget_exceeded_ = false;
  bool disallow_cache_use_ = false;
};

}  // anonymous namespace

class Canvas2DLayerBridgeTest : public Test {
 public:
  std::unique_ptr<Canvas2DLayerBridge> MakeBridge(
      const gfx::Size& size,
      RasterMode raster_mode,
      OpacityMode opacity_mode,
      std::unique_ptr<FakeCanvasResourceHost> custom_host = nullptr) {
    std::unique_ptr<Canvas2DLayerBridge> bridge =
        std::make_unique<Canvas2DLayerBridge>(size, raster_mode, opacity_mode);
    bridge->DontUseIdleSchedulingForTesting();
    if (custom_host)
      host_ = std::move(custom_host);
    if (!host_)
      host_ = std::make_unique<FakeCanvasResourceHost>(size);
    bridge->SetCanvasResourceHost(host_.get());
    return bridge;
  }

  void SetUp() override {
    test_context_provider_ = viz::TestContextProvider::Create();
    InitializeSharedGpuContext(test_context_provider_.get(),
                               &image_decode_cache_);
  }

  virtual bool NeedsMockGL() { return false; }

  void TearDown() override {
    SharedGpuContext::ResetForTesting();
    test_context_provider_.reset();
  }

  FakeCanvasResourceHost* Host() {
    DCHECK(host_);
    return host_.get();
  }

 protected:
  scoped_refptr<viz::TestContextProvider> test_context_provider_;
  ImageTrackingDecodeCache image_decode_cache_;
  std::unique_ptr<FakeCanvasResourceHost> host_;
};

TEST_F(Canvas2DLayerBridgeTest, DisableAcceleration) {
  std::unique_ptr<Canvas2DLayerBridge> bridge =
      MakeBridge(gfx::Size(300, 150), RasterMode::kCPU, kNonOpaque);

  bool has_backend_texture =
      bridge->NewImageSnapshot()->PaintImageForCurrentFrame().IsTextureBacked();

  EXPECT_FALSE(has_backend_texture);
}

TEST_F(Canvas2DLayerBridgeTest, NoDrawOnContextLost) {
  std::unique_ptr<Canvas2DLayerBridge> bridge =
      MakeBridge(gfx::Size(300, 150), RasterMode::kGPU, kNonOpaque);
  EXPECT_TRUE(bridge->IsValid());
  cc::PaintFlags flags;
  uint32_t gen_id = bridge->GetOrCreateResourceProvider()->ContentUniqueID();
  bridge->GetPaintCanvas()->drawRect(SkRect::MakeXYWH(0, 0, 1, 1), flags);
  EXPECT_EQ(gen_id, bridge->GetOrCreateResourceProvider()->ContentUniqueID());
  test_context_provider_->TestContextGL()->set_context_lost(true);
  EXPECT_EQ(nullptr, bridge->GetOrCreateResourceProvider());
  // The following passes by not crashing
  bridge->NewImageSnapshot();
}

TEST_F(Canvas2DLayerBridgeTest, PrepareMailboxWhenContextIsLost) {
  std::unique_ptr<Canvas2DLayerBridge> bridge =
      MakeBridge(gfx::Size(300, 150), RasterMode::kGPU, kNonOpaque);

  EXPECT_TRUE(bridge->IsAccelerated());
  bridge->FinalizeFrame();  // Trigger the creation of a backing store
  // When the context is lost we are not sure if we should still be producing
  // GL frames for the compositor or not, so fail to generate frames.
  test_context_provider_->TestContextGL()->set_context_lost(true);

  viz::TransferableResource resource;
  viz::ReleaseCallback release_callback;
  EXPECT_FALSE(bridge->PrepareTransferableResource(nullptr, &resource,
                                                   &release_callback));
}

TEST_F(Canvas2DLayerBridgeTest,
       PrepareMailboxWhenContextIsLostWithFailedRestore) {
  std::unique_ptr<Canvas2DLayerBridge> bridge =
      MakeBridge(gfx::Size(300, 150), RasterMode::kGPU, kNonOpaque);

  bridge->GetOrCreateResourceProvider();
  EXPECT_TRUE(bridge->IsValid());
  // When the context is lost we are not sure if we should still be producing
  // GL frames for the compositor or not, so fail to generate frames.
  test_context_provider_->TestContextGL()->set_context_lost(true);
  EXPECT_FALSE(bridge->IsValid());

  // Restoration will fail because
  // Platform::createSharedOffscreenGraphicsContext3DProvider() is stubbed
  // in unit tests.  This simulates what would happen when attempting to
  // restore while the GPU process is down.
  bridge->Restore();

  viz::TransferableResource resource;
  viz::ReleaseCallback release_callback;
  EXPECT_FALSE(bridge->PrepareTransferableResource(nullptr, &resource,
                                                   &release_callback));
}

TEST_F(Canvas2DLayerBridgeTest, PrepareMailboxAndLoseResource) {
  // Prepare a mailbox, then report the resource as lost.
  // This test passes by not crashing and not triggering assertions.
  {
    std::unique_ptr<Canvas2DLayerBridge> bridge =
        MakeBridge(gfx::Size(300, 150), RasterMode::kGPU, kNonOpaque);
    bridge->FinalizeFrame();
    viz::TransferableResource resource;
    viz::ReleaseCallback release_callback;
    EXPECT_TRUE(bridge->PrepareTransferableResource(nullptr, &resource,
                                                    &release_callback));

    bool lost_resource = true;
    std::move(release_callback).Run(gpu::SyncToken(), lost_resource);
  }

  // Retry with mailbox released while bridge destruction is in progress.
  {
    viz::TransferableResource resource;
    viz::ReleaseCallback release_callback;

    {
      std::unique_ptr<Canvas2DLayerBridge> bridge =
          MakeBridge(gfx::Size(300, 150), RasterMode::kGPU, kNonOpaque);
      bridge->FinalizeFrame();
      bridge->PrepareTransferableResource(nullptr, &resource,
                                          &release_callback);
      // |bridge| goes out of scope and would normally be destroyed, but
      // object is kept alive by self references.
    }

    // This should cause the bridge to be destroyed.
    bool lost_resource = true;
    // Before fixing crbug.com/411864, the following line would cause a memory
    // use after free that sometimes caused a crash in normal builds and
    // crashed consistently with ASAN.
    std::move(release_callback).Run(gpu::SyncToken(), lost_resource);
  }
}

TEST_F(Canvas2DLayerBridgeTest, ReleaseCallbackWithNullContextProviderWrapper) {
  viz::TransferableResource resource;
  viz::ReleaseCallback release_callback;

  {
    std::unique_ptr<Canvas2DLayerBridge> bridge =
        MakeBridge(gfx::Size(300, 150), RasterMode::kGPU, kNonOpaque);
    bridge->FinalizeFrame();
    EXPECT_TRUE(bridge->PrepareTransferableResource(nullptr, &resource,
                                                    &release_callback));
  }

  bool lost_resource = true;
  test_context_provider_->TestContextGL()->set_context_lost(true);
  // Get a new context provider so that the WeakPtr to the old one is null.
  // This is the test to make sure that ReleaseMailboxImageResource() handles
  // null context_provider_wrapper properly.
  SharedGpuContext::ContextProviderWrapper();
  std::move(release_callback).Run(gpu::SyncToken(), lost_resource);
}

TEST_F(Canvas2DLayerBridgeTest, RasterModeHint) {
  {
    std::unique_ptr<Canvas2DLayerBridge> bridge =
        MakeBridge(gfx::Size(300, 300), RasterMode::kGPU, kNonOpaque);
    cc::PaintFlags flags;
    bridge->GetPaintCanvas()->drawRect(SkRect::MakeXYWH(0, 0, 1, 1), flags);
    scoped_refptr<StaticBitmapImage> image = bridge->NewImageSnapshot();
    EXPECT_TRUE(bridge->IsValid());
    EXPECT_TRUE(bridge->IsAccelerated());
  }

  {
    std::unique_ptr<Canvas2DLayerBridge> bridge =
        MakeBridge(gfx::Size(300, 300), RasterMode::kGPU, kNonOpaque);
    cc::PaintFlags flags;
    bridge->GetPaintCanvas()->drawRect(SkRect::MakeXYWH(0, 0, 1, 1), flags);
    scoped_refptr<StaticBitmapImage> image = bridge->NewImageSnapshot();
    EXPECT_TRUE(bridge->IsValid());
    EXPECT_TRUE(bridge->IsAccelerated());
  }

  {
    std::unique_ptr<Canvas2DLayerBridge> bridge =
        MakeBridge(gfx::Size(300, 300), RasterMode::kCPU, kNonOpaque);
    cc::PaintFlags flags;
    bridge->GetPaintCanvas()->drawRect(SkRect::MakeXYWH(0, 0, 1, 1), flags);
    scoped_refptr<StaticBitmapImage> image = bridge->NewImageSnapshot();
    EXPECT_TRUE(bridge->IsValid());
    EXPECT_FALSE(bridge->IsAccelerated());
  }

  {
    std::unique_ptr<Canvas2DLayerBridge> bridge =
        MakeBridge(gfx::Size(300, 300), RasterMode::kCPU, kNonOpaque);
    cc::PaintFlags flags;
    bridge->GetPaintCanvas()->drawRect(SkRect::MakeXYWH(0, 0, 1, 1), flags);
    scoped_refptr<StaticBitmapImage> image = bridge->NewImageSnapshot();
    EXPECT_TRUE(bridge->IsValid());
    EXPECT_FALSE(bridge->IsAccelerated());
  }
}

TEST_F(Canvas2DLayerBridgeTest, FallbackToSoftwareIfContextLost) {
  test_context_provider_->TestContextGL()->set_context_lost(true);
  std::unique_ptr<Canvas2DLayerBridge> bridge =
      MakeBridge(gfx::Size(300, 150), RasterMode::kGPU, kNonOpaque);
  EXPECT_TRUE(bridge->IsValid());
  EXPECT_FALSE(bridge->IsAccelerated());
}

void DrawSomething(Canvas2DLayerBridge* bridge) {
  bridge->DidDraw();
  bridge->FinalizeFrame();
  // Grabbing an image forces a flush
  bridge->NewImageSnapshot();
}

TEST_F(Canvas2DLayerBridgeTest, FallbackToSoftwareOnFailedTextureAlloc) {
  {
    // No fallback case.
    std::unique_ptr<Canvas2DLayerBridge> bridge =
        MakeBridge(gfx::Size(300, 150), RasterMode::kGPU, kNonOpaque);
    EXPECT_TRUE(bridge->IsValid());
    EXPECT_TRUE(bridge->IsAccelerated());
    scoped_refptr<StaticBitmapImage> snapshot = bridge->NewImageSnapshot();
    EXPECT_TRUE(bridge->IsAccelerated());
    EXPECT_TRUE(snapshot->IsTextureBacked());
  }

  {
    // Fallback case.
    GrDirectContext* gr = SharedGpuContext::ContextProviderWrapper()
                              ->ContextProvider()
                              ->GetGrContext();
    std::unique_ptr<Canvas2DLayerBridge> bridge =
        std::make_unique<Canvas2DLayerBridge>(gfx::Size(300, 150),
                                              RasterMode::kGPU, kNonOpaque);
    bridge->DontUseIdleSchedulingForTesting();
    EXPECT_TRUE(bridge->IsValid());
    EXPECT_TRUE(bridge->IsAccelerated());  // We don't yet know that
                                           // allocation will fail.
    // This will cause SkSurface_Gpu creation to fail without
    // Canvas2DLayerBridge otherwise detecting that anything was disabled.
    gr->abandonContext();
    host_ = std::make_unique<FakeCanvasResourceHost>(gfx::Size(300, 150));
    bridge->SetCanvasResourceHost(host_.get());
    DrawSomething(bridge.get());
    scoped_refptr<StaticBitmapImage> snapshot = bridge->NewImageSnapshot();
    EXPECT_FALSE(bridge->IsAccelerated());
    EXPECT_FALSE(snapshot->IsTextureBacked());
  }
}

class MockLogger : public Canvas2DLayerBridge::Logger {
 public:
  MOCK_METHOD1(ReportHibernationEvent,
               void(Canvas2DLayerBridge::HibernationEvent));
  MOCK_METHOD0(DidStartHibernating, void());
  ~MockLogger() override = default;
};

TEST_F(Canvas2DLayerBridgeTest, HibernationLifeCycle) {
  if (!Canvas2DLayerBridge::IsHibernationEnabled())
    GTEST_SKIP();

  ScopedTestingPlatformSupport<GpuMemoryBufferTestPlatform> platform;
  std::unique_ptr<Canvas2DLayerBridge> bridge =
      MakeBridge(gfx::Size(300, 300), RasterMode::kGPU, kNonOpaque);
  bridge->DontUseIdleSchedulingForTesting();
  DrawSomething(bridge.get());
  EXPECT_TRUE(bridge->IsAccelerated());

  // Register an alternate Logger for tracking hibernation events
  std::unique_ptr<MockLogger> mock_logger = std::make_unique<MockLogger>();
  MockLogger* mock_logger_ptr = mock_logger.get();
  bridge->SetLoggerForTesting(std::move(mock_logger));

  // Test entering hibernation
  EXPECT_CALL(
      *mock_logger_ptr,
      ReportHibernationEvent(Canvas2DLayerBridge::kHibernationScheduled));
  EXPECT_CALL(*mock_logger_ptr, DidStartHibernating()).Times(1);

  bridge->SetIsInHiddenPage(true);
  platform->RunUntilIdle();

  testing::Mock::VerifyAndClearExpectations(mock_logger_ptr);
  EXPECT_FALSE(bridge->IsAccelerated());
  EXPECT_TRUE(bridge->IsHibernating());
  EXPECT_TRUE(bridge->IsValid());

  // Test exiting hibernation
  EXPECT_CALL(
      *mock_logger_ptr,
      ReportHibernationEvent(Canvas2DLayerBridge::kHibernationEndedNormally));

  bridge->SetIsInHiddenPage(false);

  testing::Mock::VerifyAndClearExpectations(mock_logger_ptr);
  EXPECT_TRUE(bridge->IsAccelerated());
  EXPECT_FALSE(bridge->IsHibernating());
  EXPECT_TRUE(bridge->IsValid());
}

TEST_F(Canvas2DLayerBridgeTest, HibernationReEntry) {
  if (!Canvas2DLayerBridge::IsHibernationEnabled())
    GTEST_SKIP();

  ScopedTestingPlatformSupport<GpuMemoryBufferTestPlatform> platform;
  std::unique_ptr<Canvas2DLayerBridge> bridge =
      MakeBridge(gfx::Size(300, 300), RasterMode::kGPU, kNonOpaque);
  bridge->DontUseIdleSchedulingForTesting();
  DrawSomething(bridge.get());

  // Register an alternate Logger for tracking hibernation events
  std::unique_ptr<MockLogger> mock_logger = std::make_unique<MockLogger>();
  MockLogger* mock_logger_ptr = mock_logger.get();
  bridge->SetLoggerForTesting(std::move(mock_logger));

  // Test entering hibernation
  EXPECT_CALL(
      *mock_logger_ptr,
      ReportHibernationEvent(Canvas2DLayerBridge::kHibernationScheduled));
  EXPECT_CALL(*mock_logger_ptr, DidStartHibernating()).Times(1);
  bridge->SetIsInHiddenPage(true);
  // Toggle visibility before the task that enters hibernation gets a
  // chance to run.
  bridge->SetIsInHiddenPage(false);
  bridge->SetIsInHiddenPage(true);
  platform->RunUntilIdle();

  testing::Mock::VerifyAndClearExpectations(mock_logger_ptr);
  EXPECT_FALSE(bridge->IsAccelerated());
  EXPECT_TRUE(bridge->IsHibernating());
  EXPECT_TRUE(bridge->IsValid());

  // Test exiting hibernation
  EXPECT_CALL(
      *mock_logger_ptr,
      ReportHibernationEvent(Canvas2DLayerBridge::kHibernationEndedNormally));

  bridge->SetIsInHiddenPage(false);

  testing::Mock::VerifyAndClearExpectations(mock_logger_ptr);
  EXPECT_TRUE(bridge->IsAccelerated());
  EXPECT_FALSE(bridge->IsHibernating());
  EXPECT_TRUE(bridge->IsValid());
}

TEST_F(Canvas2DLayerBridgeTest, TeardownWhileHibernating) {
  if (!Canvas2DLayerBridge::IsHibernationEnabled())
    GTEST_SKIP();

  ScopedTestingPlatformSupport<GpuMemoryBufferTestPlatform> platform;
  std::unique_ptr<Canvas2DLayerBridge> bridge =
      MakeBridge(gfx::Size(300, 300), RasterMode::kGPU, kNonOpaque);
  bridge->DontUseIdleSchedulingForTesting();
  DrawSomething(bridge.get());

  // Register an alternate Logger for tracking hibernation events
  std::unique_ptr<MockLogger> mock_logger = std::make_unique<MockLogger>();
  MockLogger* mock_logger_ptr = mock_logger.get();
  bridge->SetLoggerForTesting(std::move(mock_logger));

  // Test entering hibernation
  EXPECT_CALL(
      *mock_logger_ptr,
      ReportHibernationEvent(Canvas2DLayerBridge::kHibernationScheduled));
  EXPECT_CALL(*mock_logger_ptr, DidStartHibernating()).Times(1);
  bridge->SetIsInHiddenPage(true);
  platform->RunUntilIdle();
  testing::Mock::VerifyAndClearExpectations(mock_logger_ptr);
  EXPECT_FALSE(bridge->IsAccelerated());
  EXPECT_TRUE(bridge->IsHibernating());
  EXPECT_TRUE(bridge->IsValid());

  // Tear down the bridge while hibernating
  EXPECT_CALL(*mock_logger_ptr,
              ReportHibernationEvent(
                  Canvas2DLayerBridge::kHibernationEndedWithTeardown));
  bridge.reset();
  testing::Mock::VerifyAndClearExpectations(mock_logger_ptr);
}

TEST_F(Canvas2DLayerBridgeTest, SnapshotWhileHibernating) {
  if (!Canvas2DLayerBridge::IsHibernationEnabled())
    GTEST_SKIP();

  ScopedTestingPlatformSupport<GpuMemoryBufferTestPlatform> platform;
  std::unique_ptr<Canvas2DLayerBridge> bridge =
      MakeBridge(gfx::Size(300, 300), RasterMode::kGPU, kNonOpaque);
  bridge->DontUseIdleSchedulingForTesting();
  DrawSomething(bridge.get());

  // Register an alternate Logger for tracking hibernation events
  std::unique_ptr<MockLogger> mock_logger = std::make_unique<MockLogger>();
  MockLogger* mock_logger_ptr = mock_logger.get();
  bridge->SetLoggerForTesting(std::move(mock_logger));

  // Test entering hibernation
  EXPECT_CALL(
      *mock_logger_ptr,
      ReportHibernationEvent(Canvas2DLayerBridge::kHibernationScheduled));
  EXPECT_CALL(*mock_logger_ptr, DidStartHibernating()).Times(1);
  bridge->SetIsInHiddenPage(true);
  platform->RunUntilIdle();
  testing::Mock::VerifyAndClearExpectations(mock_logger_ptr);
  EXPECT_FALSE(bridge->IsAccelerated());
  EXPECT_TRUE(bridge->IsHibernating());
  EXPECT_TRUE(bridge->IsValid());

  // Take a snapshot and verify that it is not accelerated due to hibernation
  scoped_refptr<StaticBitmapImage> image = bridge->NewImageSnapshot();
  EXPECT_FALSE(image->IsTextureBacked());
  image = nullptr;

  // Verify that taking a snapshot did not affect the state of bridge
  EXPECT_FALSE(bridge->IsAccelerated());
  EXPECT_TRUE(bridge->IsHibernating());
  EXPECT_TRUE(bridge->IsValid());

  // End hibernation normally
  EXPECT_CALL(
      *mock_logger_ptr,
      ReportHibernationEvent(Canvas2DLayerBridge::kHibernationEndedNormally))
      .Times(1);
  bridge->SetIsInHiddenPage(false);
}

TEST_F(Canvas2DLayerBridgeTest, TeardownWhileHibernationIsPending) {
  if (!Canvas2DLayerBridge::IsHibernationEnabled())
    GTEST_SKIP();

  ScopedTestingPlatformSupport<GpuMemoryBufferTestPlatform> platform;
  std::unique_ptr<Canvas2DLayerBridge> bridge =
      MakeBridge(gfx::Size(300, 300), RasterMode::kGPU, kNonOpaque);
  bridge->DontUseIdleSchedulingForTesting();
  DrawSomething(bridge.get());

  // Register an alternate Logger for tracking hibernation events
  std::unique_ptr<MockLogger> mock_logger = std::make_unique<MockLogger>();
  MockLogger* mock_logger_ptr = mock_logger.get();
  bridge->SetLoggerForTesting(std::move(mock_logger));

  // Test entering hibernation
  EXPECT_CALL(
      *mock_logger_ptr,
      ReportHibernationEvent(Canvas2DLayerBridge::kHibernationScheduled));
  bridge->SetIsInHiddenPage(true);
  bridge.reset();
  // In production, we would expect a
  // HibernationAbortedDueToDestructionWhileHibernatePending event to be
  // fired, but that signal is lost in the unit test due to no longer having
  // a bridge to hold the mockLogger.
  platform->RunUntilIdle();
  // This test passes by not crashing, which proves that the WeakPtr logic
  // is sound.
}

TEST_F(Canvas2DLayerBridgeTest, HibernationAbortedDueToVisibilityChange) {
  if (!Canvas2DLayerBridge::IsHibernationEnabled())
    GTEST_SKIP();

  ScopedTestingPlatformSupport<GpuMemoryBufferTestPlatform> platform;
  std::unique_ptr<Canvas2DLayerBridge> bridge =
      MakeBridge(gfx::Size(300, 300), RasterMode::kGPU, kNonOpaque);
  bridge->DontUseIdleSchedulingForTesting();
  DrawSomething(bridge.get());

  // Register an alternate Logger for tracking hibernation events
  std::unique_ptr<MockLogger> mock_logger = std::make_unique<MockLogger>();
  MockLogger* mock_logger_ptr = mock_logger.get();
  bridge->SetLoggerForTesting(std::move(mock_logger));

  // Test entering hibernation
  EXPECT_CALL(
      *mock_logger_ptr,
      ReportHibernationEvent(Canvas2DLayerBridge::kHibernationScheduled));
  EXPECT_CALL(
      *mock_logger_ptr,
      ReportHibernationEvent(
          Canvas2DLayerBridge::kHibernationAbortedDueToVisibilityChange))
      .Times(1);
  bridge->SetIsInHiddenPage(true);
  bridge->SetIsInHiddenPage(false);
  platform->RunUntilIdle();
  testing::Mock::VerifyAndClearExpectations(mock_logger_ptr);
  EXPECT_TRUE(bridge->IsAccelerated());
  EXPECT_FALSE(bridge->IsHibernating());
  EXPECT_TRUE(bridge->IsValid());
}

TEST_F(Canvas2DLayerBridgeTest, HibernationAbortedDueToLostContext) {
  if (!Canvas2DLayerBridge::IsHibernationEnabled())
    GTEST_SKIP();

  ScopedTestingPlatformSupport<GpuMemoryBufferTestPlatform> platform;
  std::unique_ptr<Canvas2DLayerBridge> bridge =
      MakeBridge(gfx::Size(300, 300), RasterMode::kGPU, kNonOpaque);
  bridge->DontUseIdleSchedulingForTesting();
  DrawSomething(bridge.get());

  // Register an alternate Logger for tracking hibernation events
  std::unique_ptr<MockLogger> mock_logger = std::make_unique<MockLogger>();
  MockLogger* mock_logger_ptr = mock_logger.get();
  bridge->SetLoggerForTesting(std::move(mock_logger));

  test_context_provider_->TestContextGL()->set_context_lost(true);

  // Test entering hibernation
  EXPECT_CALL(
      *mock_logger_ptr,
      ReportHibernationEvent(Canvas2DLayerBridge::kHibernationScheduled));
  EXPECT_CALL(*mock_logger_ptr,
              ReportHibernationEvent(
                  Canvas2DLayerBridge::kHibernationAbortedDueGpuContextLoss))
      .Times(1);

  bridge->SetIsInHiddenPage(true);
  platform->RunUntilIdle();
  testing::Mock::VerifyAndClearExpectations(mock_logger_ptr);
  EXPECT_FALSE(bridge->IsHibernating());
}

TEST_F(Canvas2DLayerBridgeTest, PrepareMailboxWhileHibernating) {
  if (!Canvas2DLayerBridge::IsHibernationEnabled())
    GTEST_SKIP();

  ScopedTestingPlatformSupport<GpuMemoryBufferTestPlatform> platform;
  std::unique_ptr<Canvas2DLayerBridge> bridge =
      MakeBridge(gfx::Size(300, 300), RasterMode::kGPU, kNonOpaque);
  bridge->DontUseIdleSchedulingForTesting();
  DrawSomething(bridge.get());

  // Register an alternate Logger for tracking hibernation events
  std::unique_ptr<MockLogger> mock_logger = std::make_unique<MockLogger>();
  MockLogger* mock_logger_ptr = mock_logger.get();
  bridge->SetLoggerForTesting(std::move(mock_logger));

  // Test entering hibernation
  EXPECT_CALL(
      *mock_logger_ptr,
      ReportHibernationEvent(Canvas2DLayerBridge::kHibernationScheduled));
  EXPECT_CALL(*mock_logger_ptr, DidStartHibernating()).Times(1);
  bridge->SetIsInHiddenPage(true);
  platform->RunUntilIdle();
  testing::Mock::VerifyAndClearExpectations(mock_logger_ptr);

  // Test PrepareTransferableResource() while hibernating
  viz::TransferableResource resource;
  viz::ReleaseCallback release_callback;
  EXPECT_FALSE(bridge->PrepareTransferableResource(nullptr, &resource,
                                                   &release_callback));
  EXPECT_TRUE(bridge->IsValid());

  // Tear down the bridge on the thread so that 'bridge' can go out of scope
  // without crashing due to thread checks
  EXPECT_CALL(*mock_logger_ptr,
              ReportHibernationEvent(
                  Canvas2DLayerBridge::kHibernationEndedWithTeardown));
  bridge.reset();
  testing::Mock::VerifyAndClearExpectations(mock_logger_ptr);
}

TEST_F(Canvas2DLayerBridgeTest, ResourceRecycling) {
  ScopedCanvas2dImageChromiumForTest canvas_2d_image_chromium(true);
  const_cast<gpu::Capabilities&>(SharedGpuContext::ContextProviderWrapper()
                                     ->ContextProvider()
                                     ->GetCapabilities())
      .gpu_memory_buffer_formats.Add(gfx::BufferFormat::BGRA_8888);

  viz::TransferableResource resources[3];
  viz::ReleaseCallback callbacks[3];
  cc::PaintFlags flags;

  std::unique_ptr<Canvas2DLayerBridge> bridge =
      MakeBridge(gfx::Size(300, 150), RasterMode::kGPU, kNonOpaque);
  bridge->GetPaintCanvas()->drawLine(0, 0, 2, 2, flags);
  DrawSomething(bridge.get());
  ASSERT_TRUE(bridge->PrepareTransferableResource(nullptr, &resources[0],
                                                  &callbacks[0]));

  bridge->GetPaintCanvas()->drawLine(0, 0, 2, 2, flags);
  DrawSomething(bridge.get());
  ASSERT_TRUE(bridge->PrepareTransferableResource(nullptr, &resources[1],
                                                  &callbacks[1]));
  EXPECT_NE(resources[0].mailbox_holder.mailbox,
            resources[1].mailbox_holder.mailbox);

  // Now release the first resource and draw again. It should be reused due to
  // recycling.
  std::move(callbacks[0]).Run(gpu::SyncToken(), false);
  bridge->GetPaintCanvas()->drawLine(0, 0, 2, 2, flags);
  DrawSomething(bridge.get());
  ASSERT_TRUE(bridge->PrepareTransferableResource(nullptr, &resources[2],
                                                  &callbacks[2]));
  EXPECT_EQ(resources[0].mailbox_holder.mailbox,
            resources[2].mailbox_holder.mailbox);

  std::move(callbacks[1]).Run(gpu::SyncToken(), false);
  std::move(callbacks[2]).Run(gpu::SyncToken(), false);
}

TEST_F(Canvas2DLayerBridgeTest, NoResourceRecyclingWhenPageHidden) {
  ScopedCanvas2dImageChromiumForTest canvas_2d_image_chromium(true);
  const_cast<gpu::Capabilities&>(SharedGpuContext::ContextProviderWrapper()
                                     ->ContextProvider()
                                     ->GetCapabilities())
      .gpu_memory_buffer_formats.Add(gfx::BufferFormat::BGRA_8888);

  viz::TransferableResource resources[2];
  viz::ReleaseCallback callbacks[2];
  cc::PaintFlags flags;

  std::unique_ptr<Canvas2DLayerBridge> bridge =
      MakeBridge(gfx::Size(300, 150), RasterMode::kGPU, kNonOpaque);
  bridge->GetPaintCanvas()->drawLine(0, 0, 2, 2, flags);
  DrawSomething(bridge.get());
  ASSERT_TRUE(bridge->PrepareTransferableResource(nullptr, &resources[0],
                                                  &callbacks[0]));
  bridge->GetPaintCanvas()->drawLine(0, 0, 2, 2, flags);
  DrawSomething(bridge.get());
  ASSERT_TRUE(bridge->PrepareTransferableResource(nullptr, &resources[1],
                                                  &callbacks[1]));
  EXPECT_NE(resources[0].mailbox_holder.mailbox,
            resources[1].mailbox_holder.mailbox);

  // Now release the first resource and mark the page hidden. The recycled
  // resource should be dropped.
  std::move(callbacks[0]).Run(gpu::SyncToken(), false);
  EXPECT_EQ(test_context_provider_->TestContextGL()->NumTextures(), 2u);
  bridge->SetIsInHiddenPage(true);
  EXPECT_EQ(test_context_provider_->TestContextGL()->NumTextures(), 1u);

  // Release second frame, this resource is not released because its the current
  // render target for the canvas. It should only be released if the canvas is
  // hibernated.
  std::move(callbacks[1]).Run(gpu::SyncToken(), false);
  EXPECT_EQ(test_context_provider_->TestContextGL()->NumTextures(), 1u);
}

TEST_F(Canvas2DLayerBridgeTest, ReleaseResourcesAfterBridgeDestroyed) {
  ScopedCanvas2dImageChromiumForTest canvas_2d_image_chromium(true);
  const_cast<gpu::Capabilities&>(SharedGpuContext::ContextProviderWrapper()
                                     ->ContextProvider()
                                     ->GetCapabilities())
      .gpu_memory_buffer_formats.Add(gfx::BufferFormat::BGRA_8888);

  viz::TransferableResource resource;
  viz::ReleaseCallback release_callback;

  std::unique_ptr<Canvas2DLayerBridge> bridge =
      MakeBridge(gfx::Size(300, 150), RasterMode::kGPU, kNonOpaque);
  DrawSomething(bridge.get());
  bridge->PrepareTransferableResource(nullptr, &resource, &release_callback);

  // Tearing down the bridge does not destroy unreleased resources.
  bridge.reset();
  EXPECT_EQ(test_context_provider_->TestContextGL()->NumTextures(), 1u);
  constexpr bool lost_resource = false;
  std::move(release_callback).Run(gpu::SyncToken(), lost_resource);
  EXPECT_EQ(test_context_provider_->TestContextGL()->NumTextures(), 0u);
}

TEST_F(Canvas2DLayerBridgeTest, EnsureCCImageCacheUse) {
  std::unique_ptr<Canvas2DLayerBridge> bridge =
      MakeBridge(gfx::Size(300, 300), RasterMode::kGPU, kOpaque);
  Vector<cc::DrawImage> images = {
      cc::DrawImage(cc::CreateDiscardablePaintImage(gfx::Size(10, 10)), false,
                    SkIRect::MakeWH(10, 10),
                    cc::PaintFlags::FilterQuality::kNone, SkM44(), 0u,
                    cc::TargetColorParams()),
      cc::DrawImage(cc::CreateDiscardablePaintImage(gfx::Size(20, 20)), false,
                    SkIRect::MakeWH(5, 5), cc::PaintFlags::FilterQuality::kNone,
                    SkM44(), 0u, cc::TargetColorParams())};

  bridge->GetPaintCanvas()->drawImage(images[0].paint_image(), 0u, 0u);
  bridge->GetPaintCanvas()->drawImageRect(
      images[1].paint_image(), SkRect::MakeWH(5u, 5u), SkRect::MakeWH(5u, 5u),
      SkCanvas::kFast_SrcRectConstraint);
  bridge->NewImageSnapshot();

  EXPECT_EQ(image_decode_cache_.decoded_images(), images);
}

TEST_F(Canvas2DLayerBridgeTest, EnsureCCImageCacheUseWithColorConversion) {
  std::unique_ptr<Canvas2DLayerBridge> bridge =
      MakeBridge(gfx::Size(300, 300), RasterMode::kGPU, kOpaque);
  Vector<cc::DrawImage> images = {
      cc::DrawImage(cc::CreateDiscardablePaintImage(gfx::Size(10, 10)), false,
                    SkIRect::MakeWH(10, 10),
                    cc::PaintFlags::FilterQuality::kNone, SkM44(), 0u,
                    cc::TargetColorParams()),
      cc::DrawImage(cc::CreateDiscardablePaintImage(gfx::Size(20, 20)), false,
                    SkIRect::MakeWH(5, 5), cc::PaintFlags::FilterQuality::kNone,
                    SkM44(), 0u, cc::TargetColorParams())};

  bridge->GetPaintCanvas()->drawImage(images[0].paint_image(), 0u, 0u);
  bridge->GetPaintCanvas()->drawImageRect(
      images[1].paint_image(), SkRect::MakeWH(5u, 5u), SkRect::MakeWH(5u, 5u),
      SkCanvas::kFast_SrcRectConstraint);
  bridge->NewImageSnapshot();

  EXPECT_EQ(image_decode_cache_.decoded_images(), images);
}

TEST_F(Canvas2DLayerBridgeTest, ImagesLockedUntilCacheLimit) {
  std::unique_ptr<Canvas2DLayerBridge> bridge =
      MakeBridge(gfx::Size(300, 300), RasterMode::kGPU, kOpaque);

  Vector<cc::DrawImage> images = {
      cc::DrawImage(cc::CreateDiscardablePaintImage(gfx::Size(10, 10)), false,
                    SkIRect::MakeWH(10, 10),
                    cc::PaintFlags::FilterQuality::kNone, SkM44(), 0u,
                    cc::TargetColorParams()),
      cc::DrawImage(cc::CreateDiscardablePaintImage(gfx::Size(20, 20)), false,
                    SkIRect::MakeWH(5, 5), cc::PaintFlags::FilterQuality::kNone,
                    SkM44(), 0u, cc::TargetColorParams()),
      cc::DrawImage(cc::CreateDiscardablePaintImage(gfx::Size(20, 20)), false,
                    SkIRect::MakeWH(5, 5), cc::PaintFlags::FilterQuality::kNone,
                    SkM44(), 0u, cc::TargetColorParams())};

  // First 2 images are budgeted, they should remain locked after the op.
  bridge->GetPaintCanvas()->drawImage(images[0].paint_image(), 0u, 0u);
  bridge->GetPaintCanvas()->drawImage(images[1].paint_image(), 0u, 0u);
  bridge->GetOrCreateResourceProvider()->FlushCanvas();
  EXPECT_EQ(image_decode_cache_.num_locked_images(), 2);

  // Next image is not budgeted, we should unlock all images other than the last
  // image.
  image_decode_cache_.set_budget_exceeded(true);
  bridge->GetPaintCanvas()->drawImage(images[2].paint_image(), 0u, 0u);
  bridge->GetOrCreateResourceProvider()->FlushCanvas();
  EXPECT_EQ(image_decode_cache_.num_locked_images(), 1);

  // Ask the provider to release everything, no locked images should remain.
  bridge->GetOrCreateResourceProvider()->ReleaseLockedImages();
  EXPECT_EQ(image_decode_cache_.num_locked_images(), 0);
}

TEST_F(Canvas2DLayerBridgeTest, QueuesCleanupTaskForLockedImages) {
  std::unique_ptr<Canvas2DLayerBridge> bridge =
      MakeBridge(gfx::Size(300, 300), RasterMode::kGPU, kOpaque);

  auto image = cc::DrawImage(cc::CreateDiscardablePaintImage(gfx::Size(10, 10)),
                             false, SkIRect::MakeWH(10, 10),
                             cc::PaintFlags::FilterQuality::kNone, SkM44(), 0u,
                             cc::TargetColorParams());
  bridge->GetPaintCanvas()->drawImage(image.paint_image(), 0u, 0u);

  bridge->GetOrCreateResourceProvider()->FlushCanvas();
  EXPECT_EQ(image_decode_cache_.num_locked_images(), 1);

  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(image_decode_cache_.num_locked_images(), 0);
}

TEST_F(Canvas2DLayerBridgeTest, ImageCacheOnContextLost) {
  std::unique_ptr<Canvas2DLayerBridge> bridge =
      MakeBridge(gfx::Size(300, 300), RasterMode::kGPU, kOpaque);
  Vector<cc::DrawImage> images = {
      cc::DrawImage(cc::CreateDiscardablePaintImage(gfx::Size(10, 10)), false,
                    SkIRect::MakeWH(10, 10),
                    cc::PaintFlags::FilterQuality::kNone, SkM44(), 0u,
                    cc::TargetColorParams()),
      cc::DrawImage(cc::CreateDiscardablePaintImage(gfx::Size(20, 20)), false,
                    SkIRect::MakeWH(5, 5), cc::PaintFlags::FilterQuality::kNone,
                    SkM44(), 0u, cc::TargetColorParams())};
  bridge->GetPaintCanvas()->drawImage(images[0].paint_image(), 0u, 0u);

  // Lose the context and ensure that the image provider is not used.
  bridge->GetOrCreateResourceProvider()->OnContextDestroyed();
  // We should unref all images on the cache when the context is destroyed.
  EXPECT_EQ(image_decode_cache_.num_locked_images(), 0);
  image_decode_cache_.set_disallow_cache_use(true);
  bridge->GetPaintCanvas()->drawImage(images[1].paint_image(), 0u, 0u);
}

TEST_F(Canvas2DLayerBridgeTest,
       PrepareTransferableResourceTracksCanvasChanges) {
  gfx::Size size(300, 300);
  std::unique_ptr<Canvas2DLayerBridge> bridge =
      MakeBridge(size, RasterMode::kGPU, kNonOpaque);

  bridge->GetPaintCanvas()->clear(SkColors::kRed);
  DrawSomething(bridge.get());
  ASSERT_TRUE(bridge->layer_for_testing());

  viz::TransferableResource resource;
  viz::ReleaseCallback release_callback;
  EXPECT_TRUE(bridge->PrepareTransferableResource(nullptr, &resource,
                                                  &release_callback));
  bridge->layer_for_testing()->SetTransferableResource(
      resource, std::move(release_callback));

  viz::ReleaseCallback release_callback2;
  EXPECT_FALSE(bridge->PrepareTransferableResource(nullptr, &resource,
                                                   &release_callback2));
  EXPECT_FALSE(release_callback2);
}
class CustomFakeCanvasResourceHost : public FakeCanvasResourceHost {
 public:
  explicit CustomFakeCanvasResourceHost(const gfx::Size& size)
      : FakeCanvasResourceHost(size) {}
  void RestoreCanvasMatrixClipStack(cc::PaintCanvas* canvas) const override {
    // Alter canvas' matrix to emulate a restore
    canvas->translate(5, 0);
  }
};

TEST_F(Canvas2DLayerBridgeTest, WritePixelsRestoresClipStack) {
  gfx::Size size(300, 300);
  auto host = std::make_unique<CustomFakeCanvasResourceHost>(size);
  std::unique_ptr<Canvas2DLayerBridge> bridge =
      MakeBridge(size, RasterMode::kGPU, kOpaque, std::move(host));
  cc::PaintFlags flags;

  // MakeBridge() results in a call to restore the matrix. So we already have 1.
  EXPECT_EQ(bridge->GetPaintCanvas()->getTotalMatrix().get(SkMatrix::kMTransX),
            5);
  // Drawline so WritePixels has something to flush
  bridge->GetPaintCanvas()->drawLine(0, 0, 2, 2, flags);
  bridge->DidDraw();

  // WritePixels flushes recording. Post flush, a new drawing canvas is created
  // that should have the matrix restored onto it.
  bridge->WritePixels(SkImageInfo::MakeN32Premul(10, 10), nullptr, 10, 0, 0);
  EXPECT_EQ(bridge->GetPaintCanvas()->getTotalMatrix().get(SkMatrix::kMTransX),
            5);

  bridge->GetPaintCanvas()->drawLine(0, 0, 2, 2, flags);
  // Standard flush recording. Post flush, a new drawing canvas is created that
  // should have the matrix restored onto it.
  DrawSomething(bridge.get());

  EXPECT_EQ(bridge->GetPaintCanvas()->getTotalMatrix().get(SkMatrix::kMTransX),
            5);
}

TEST_F(Canvas2DLayerBridgeTest, DisplayedCanvasIsRateLimited) {
  std::unique_ptr<Canvas2DLayerBridge> bridge =
      MakeBridge(gfx::Size(300, 150), RasterMode::kGPU, kNonOpaque);
  EXPECT_TRUE(bridge->IsValid());
  bridge->SetIsBeingDisplayed(true);
  EXPECT_FALSE(bridge->HasRateLimiterForTesting());
  bridge->FinalizeFrame();
  bridge->FinalizeFrame();
  EXPECT_TRUE(bridge->HasRateLimiterForTesting());
}

TEST_F(Canvas2DLayerBridgeTest, NonDisplayedCanvasIsNotRateLimited) {
  std::unique_ptr<Canvas2DLayerBridge> bridge =
      MakeBridge(gfx::Size(300, 150), RasterMode::kGPU, kNonOpaque);
  EXPECT_TRUE(bridge->IsValid());
  bridge->SetIsBeingDisplayed(true);
  bridge->FinalizeFrame();
  bridge->FinalizeFrame();
  EXPECT_TRUE(bridge->HasRateLimiterForTesting());
  bridge->SetIsBeingDisplayed(false);
  EXPECT_FALSE(bridge->HasRateLimiterForTesting());
  bridge->FinalizeFrame();
  bridge->FinalizeFrame();
  EXPECT_FALSE(bridge->HasRateLimiterForTesting());
}

}  // namespace blink
