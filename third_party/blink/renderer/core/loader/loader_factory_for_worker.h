// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_LOADER_LOADER_FACTORY_FOR_WORKER_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_LOADER_LOADER_FACTORY_FOR_WORKER_H_

#include <memory>
#include <utility>
#include "third_party/blink/renderer/platform/loader/fetch/resource_fetcher.h"

namespace blink {

class WorkerOrWorkletGlobalScope;
class WebWorkerFetchContext;

namespace scheduler {
class WebResourceLoadingTaskRunnerHandle;
}  // namespace scheduler

// ResourceFetcher::LoaderFactory implementation for workers and worklets.
class LoaderFactoryForWorker : public ResourceFetcher::LoaderFactory {
 public:
  LoaderFactoryForWorker(WorkerOrWorkletGlobalScope& global_scope,
                         scoped_refptr<WebWorkerFetchContext> web_context)
      : global_scope_(global_scope), web_context_(std::move(web_context)) {}

  void Trace(Visitor* visitor) const override;

  // LoaderFactory implementations
  std::unique_ptr<WebURLLoader> CreateURLLoader(
      const ResourceRequest& request,
      const ResourceLoaderOptions& options,
      scoped_refptr<base::SingleThreadTaskRunner> freezable_task_runner,
      scoped_refptr<base::SingleThreadTaskRunner> unfreezable_task_runner,
      WebBackForwardCacheLoaderHelper) override;
  std::unique_ptr<WebCodeCacheLoader> CreateCodeCacheLoader() override;

 private:
  std::unique_ptr<blink::scheduler::WebResourceLoadingTaskRunnerHandle>
  CreateTaskRunnerHandle(
      scoped_refptr<base::SingleThreadTaskRunner> task_runner);

  const Member<WorkerOrWorkletGlobalScope> global_scope_;
  const scoped_refptr<WebWorkerFetchContext> web_context_;
};

}  // namespace blink
#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_LOADER_LOADER_FACTORY_FOR_WORKER_H_
