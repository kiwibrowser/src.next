// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_CANVAS_HIBERNATION_HANDLER_H_
#define THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_CANVAS_HIBERNATION_HANDLER_H_

#include "base/no_destructor.h"
#include "base/task/single_thread_task_runner.h"
#include "base/trace_event/memory_dump_provider.h"
#include "third_party/blink/renderer/platform/graphics/memory_managed_paint_recorder.h"
#include "third_party/blink/renderer/platform/graphics/paint/paint_canvas.h"
#include "third_party/blink/renderer/platform/platform_export.h"
#include "third_party/blink/renderer/platform/wtf/hash_set.h"
#include "third_party/blink/renderer/platform/wtf/wtf.h"

namespace blink {

// All the fields are main-thread only. See DCheckInvariant() for invariants.
class PLATFORM_EXPORT CanvasHibernationHandler {
 public:
  ~CanvasHibernationHandler();
  // Semi-arbitrary threshold. Some past experiments (e.g. tile discard) have
  // shown that taking action after 5 minutes has a positive impact on memory,
  // and a minimal impact on tab switching latency (and on needless
  // compression).
  static constexpr base::TimeDelta kBeforeCompressionDelay = base::Minutes(5);

  void SaveForHibernation(sk_sp<SkImage>&& image,
                          std::unique_ptr<MemoryManagedPaintRecorder> recorder);
  // Returns the uncompressed image for this hibernation image. Does not
  // invalidate the hibernated image. Must call `Clear()` if invalidation is
  // required.
  sk_sp<SkImage> GetImage();
  std::unique_ptr<MemoryManagedPaintRecorder> ReleaseRecorder() {
    return std::move(recorder_);
  }
  // Invalidate the hibernated image.
  void Clear();

  bool IsHibernating() const {
    DCheckInvariant();
    return image_ != nullptr || encoded_ != nullptr;
  }
  bool is_encoded() const {
    DCheckInvariant();
    return encoded_ != nullptr;
  }
  size_t memory_size() const;
  size_t original_memory_size() const;
  int width() const { return width_; }
  int height() const { return height_; }

  void SetTaskRunnersForTesting(
      scoped_refptr<base::SingleThreadTaskRunner> main_thread_task_runner,
      scoped_refptr<base::SingleThreadTaskRunner>
          background_thread_task_runner) {
    main_thread_task_runner_for_testing_ = main_thread_task_runner;
    background_thread_task_runner_for_testing_ = background_thread_task_runner;
  }

 private:
  struct BackgroundTaskParams final {
    BackgroundTaskParams(
        sk_sp<SkImage> image,
        uint64_t epoch,
        base::WeakPtr<CanvasHibernationHandler> weak_instance,
        scoped_refptr<base::SingleThreadTaskRunner> reply_task_runner)
        : image(image),
          epoch(epoch),
          weak_instance(weak_instance),
          reply_task_runner(reply_task_runner) {}

    BackgroundTaskParams(const BackgroundTaskParams&) = delete;
    BackgroundTaskParams& operator=(const BackgroundTaskParams&) = delete;
    ~BackgroundTaskParams() { DCHECK(IsMainThread()); }

    const sk_sp<SkImage> image;
    const uint64_t epoch;
    const base::WeakPtr<CanvasHibernationHandler> weak_instance;
    const scoped_refptr<base::SingleThreadTaskRunner> reply_task_runner;
  };

  void DCheckInvariant() const {
    DCHECK(IsMainThread());
    DCHECK(!((image_ != nullptr) && (encoded_ != nullptr)));
  }
  void OnAfterHibernation(uint64_t initial_epoch);
  static void Encode(std::unique_ptr<BackgroundTaskParams> params);
  void OnEncoded(
      std::unique_ptr<CanvasHibernationHandler::BackgroundTaskParams> params,
      sk_sp<SkData> encoded);
  scoped_refptr<base::SingleThreadTaskRunner> GetMainThreadTaskRunner() const;
  static size_t ImageMemorySize(const SkImage& image);

  // Incremented each time the canvas is hibernated.
  uint64_t epoch_ = 0;
  // Uncompressed hibernation image.
  sk_sp<SkImage> image_ = nullptr;
  // Compressed hibernation image.
  sk_sp<SkData> encoded_ = nullptr;
  std::unique_ptr<MemoryManagedPaintRecorder> recorder_;
  scoped_refptr<base::SingleThreadTaskRunner>
      main_thread_task_runner_for_testing_;
  scoped_refptr<base::SingleThreadTaskRunner>
      background_thread_task_runner_for_testing_;
  int width_;
  int height_;
  int bytes_per_pixel_;

  base::WeakPtrFactory<CanvasHibernationHandler> weak_ptr_factory_{this};
};

// memory-infra metrics for all hibernated canvases in this process. Main thread
// only.
class PLATFORM_EXPORT HibernatedCanvasMemoryDumpProvider
    : public base::trace_event::MemoryDumpProvider {
 public:
  static HibernatedCanvasMemoryDumpProvider& GetInstance();
  void Register(CanvasHibernationHandler* handler);
  void Unregister(CanvasHibernationHandler* handler);

  bool OnMemoryDump(const base::trace_event::MemoryDumpArgs& args,
                    base::trace_event::ProcessMemoryDump* pmd) override;

 private:
  friend class base::NoDestructor<HibernatedCanvasMemoryDumpProvider>;
  HibernatedCanvasMemoryDumpProvider();

  base::Lock lock_;
  WTF::HashSet<CanvasHibernationHandler*> handlers_ GUARDED_BY(lock_);
};

}  // namespace blink

#endif
