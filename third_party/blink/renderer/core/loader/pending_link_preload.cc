// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/loader/pending_link_preload.h"

#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/loader/link_loader.h"
#include "third_party/blink/renderer/core/loader/preload_helper.h"
#include "third_party/blink/renderer/core/loader/render_blocking_resource_manager.h"
#include "third_party/blink/renderer/platform/heap/prefinalizer.h"
#include "third_party/blink/renderer/platform/loader/fetch/resource_finish_observer.h"

namespace blink {

class PendingLinkPreload::FinishObserver final : public ResourceFinishObserver {
  USING_PRE_FINALIZER(FinishObserver, Dispose);

 public:
  FinishObserver(PendingLinkPreload* pending_preload, Resource* resource)
      : pending_preload_(pending_preload), resource_(resource) {
    resource_->AddFinishObserver(
        this, pending_preload_->GetLoadingTaskRunner().get());
  }

  // ResourceFinishObserver implementation
  void NotifyFinished() override {
    if (!resource_)
      return;
    pending_preload_->NotifyFinished();
    Dispose();
  }
  String DebugName() const override {
    return "PendingLinkPreload::FinishObserver";
  }

  Resource* GetResource() { return resource_; }
  void Dispose() {
    if (!resource_)
      return;
    resource_->RemoveFinishObserver(this);
    resource_ = nullptr;
  }

  void Trace(Visitor* visitor) const override {
    visitor->Trace(pending_preload_);
    visitor->Trace(resource_);
    blink::ResourceFinishObserver::Trace(visitor);
  }

 private:
  Member<PendingLinkPreload> pending_preload_;
  Member<Resource> resource_;
};

PendingLinkPreload::PendingLinkPreload(Document& document, LinkLoader* loader)
    : document_(document), loader_(loader) {}

PendingLinkPreload::~PendingLinkPreload() = default;

void PendingLinkPreload::AddResource(Resource* resource) {
  DCHECK(!finish_observer_);
  if (resource)
    finish_observer_ = MakeGarbageCollected<FinishObserver>(this, resource);
}

// https://html.spec.whatwg.org/C/#link-type-modulepreload
void PendingLinkPreload::NotifyModuleLoadFinished(ModuleScript* module) {
  if (loader_)
    loader_->NotifyModuleLoadFinished(module);
  document_->RemovePendingLinkHeaderPreloadIfNeeded(*this);
}

void PendingLinkPreload::NotifyFinished() {
  UnblockRendering();
  DCHECK(finish_observer_);
  if (loader_)
    loader_->NotifyFinished(finish_observer_->GetResource());
  document_->RemovePendingLinkHeaderPreloadIfNeeded(*this);
}

void PendingLinkPreload::UnblockRendering() {
  if (RenderBlockingResourceManager* manager =
          document_->GetRenderBlockingResourceManager()) {
    manager->RemovePendingPreload(*this);
  }
}

scoped_refptr<base::SingleThreadTaskRunner>
PendingLinkPreload::GetLoadingTaskRunner() {
  return document_->GetTaskRunner(TaskType::kNetworking);
}

void PendingLinkPreload::Dispose() {
  UnblockRendering();
  if (finish_observer_)
    finish_observer_->Dispose();
  finish_observer_ = nullptr;
  document_->RemovePendingLinkHeaderPreloadIfNeeded(*this);
}

Resource* PendingLinkPreload::GetResourceForTesting() {
  return finish_observer_ ? finish_observer_->GetResource() : nullptr;
}

void PendingLinkPreload::Trace(Visitor* visitor) const {
  visitor->Trace(document_);
  visitor->Trace(loader_);
  visitor->Trace(finish_observer_);
  SingleModuleClient::Trace(visitor);
}

}  // namespace blink
