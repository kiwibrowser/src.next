// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/loader/prefetched_signed_exchange_manager.h"

#include <queue>
#include <utility>

#include "base/callback.h"
#include "base/memory/weak_ptr.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/mojom/url_loader_factory.mojom-blink.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/public/mojom/devtools/console_message.mojom-blink.h"
#include "third_party/blink/public/platform/platform.h"
#include "third_party/blink/public/platform/resource_load_info_notifier_wrapper.h"
#include "third_party/blink/public/platform/task_type.h"
#include "third_party/blink/public/platform/web_back_forward_cache_loader_helper.h"
#include "third_party/blink/public/platform/web_url_loader.h"
#include "third_party/blink/public/platform/web_url_loader_client.h"
#include "third_party/blink/public/platform/web_url_loader_factory.h"
#include "third_party/blink/public/platform/web_url_request_extra_data.h"
#include "third_party/blink/public/platform/web_url_response.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/navigator.h"
#include "third_party/blink/renderer/core/inspector/console_message.h"
#include "third_party/blink/renderer/core/loader/alternate_signed_exchange_resource_info.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/loader/fetch/loader_freeze_mode.h"
#include "third_party/blink/renderer/platform/loader/link_header.h"
#include "third_party/blink/renderer/platform/scheduler/public/frame_scheduler.h"
#include "third_party/blink/renderer/platform/weborigin/kurl.h"
#include "third_party/blink/renderer/platform/weborigin/kurl_hash.h"
#include "third_party/blink/renderer/platform/wtf/functional.h"
#include "third_party/blink/renderer/platform/wtf/hash_map.h"
#include "third_party/blink/renderer/platform/wtf/hash_set.h"
#include "third_party/blink/renderer/platform/wtf/text/string_hash.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"
#include "third_party/blink/renderer/platform/wtf/vector.h"

namespace blink {

class PrefetchedSignedExchangeManager::PrefetchedSignedExchangeLoader
    : public WebURLLoader {
 public:
  PrefetchedSignedExchangeLoader(
      const WebURLRequest& request,
      scoped_refptr<base::SingleThreadTaskRunner> task_runner)
      : task_runner_(std::move(task_runner)) {
    request_.CopyFrom(request);
  }

  PrefetchedSignedExchangeLoader(const PrefetchedSignedExchangeLoader&) =
      delete;
  PrefetchedSignedExchangeLoader& operator=(
      const PrefetchedSignedExchangeLoader&) = delete;

  ~PrefetchedSignedExchangeLoader() override = default;

  base::WeakPtr<PrefetchedSignedExchangeLoader> GetWeakPtr() {
    return weak_ptr_factory_.GetWeakPtr();
  }

  void SetURLLoader(std::unique_ptr<WebURLLoader> url_loader) {
    DCHECK(!url_loader_);
    url_loader_ = std::move(url_loader);
    ExecutePendingMethodCalls();
  }

  const WebURLRequest& request() const { return request_; }

  // WebURLLoader methods:
  void LoadSynchronously(
      std::unique_ptr<network::ResourceRequest> request,
      scoped_refptr<WebURLRequestExtraData> url_request_extra_data,
      bool pass_response_pipe_to_client,
      bool no_mime_sniffing,
      base::TimeDelta timeout_interval,
      WebURLLoaderClient* client,
      WebURLResponse& response,
      absl::optional<WebURLError>& error,
      WebData& data,
      int64_t& encoded_data_length,
      int64_t& encoded_body_length,
      WebBlobInfo& downloaded_blob,
      std::unique_ptr<blink::ResourceLoadInfoNotifierWrapper>
          resource_load_info_notifier_wrapper) override {
    NOTREACHED();
  }
  void LoadAsynchronously(
      std::unique_ptr<network::ResourceRequest> request,
      scoped_refptr<WebURLRequestExtraData> url_request_extra_data,
      bool no_mime_sniffing,
      std::unique_ptr<blink::ResourceLoadInfoNotifierWrapper>
          resource_load_info_notifier_wrapper,
      WebURLLoaderClient* client) override {
    if (url_loader_) {
      url_loader_->LoadAsynchronously(
          std::move(request), std::move(url_request_extra_data),
          no_mime_sniffing, std::move(resource_load_info_notifier_wrapper),
          client);
      return;
    }
    // It is safe to use Unretained(client), because |client| is a
    // ResourceLoader which owns |this|, and we are binding with weak ptr of
    // |this| here.
    pending_method_calls_.push(WTF::Bind(
        &PrefetchedSignedExchangeLoader::LoadAsynchronously, GetWeakPtr(),
        std::move(request), std::move(url_request_extra_data), no_mime_sniffing,
        std::move(resource_load_info_notifier_wrapper),
        WTF::Unretained(client)));
  }
  void Freeze(LoaderFreezeMode value) override {
    if (url_loader_) {
      url_loader_->Freeze(value);
      return;
    }
    pending_method_calls_.push(WTF::Bind(
        &PrefetchedSignedExchangeLoader::Freeze, GetWeakPtr(), value));
  }
  void DidChangePriority(WebURLRequest::Priority new_priority,
                         int intra_priority_value) override {
    if (url_loader_) {
      url_loader_->DidChangePriority(new_priority, intra_priority_value);
      return;
    }
    pending_method_calls_.push(
        WTF::Bind(&PrefetchedSignedExchangeLoader::DidChangePriority,
                  GetWeakPtr(), new_priority, intra_priority_value));
  }
  scoped_refptr<base::SingleThreadTaskRunner> GetTaskRunnerForBodyLoader()
      override {
    return task_runner_;
  }

 private:
  void ExecutePendingMethodCalls() {
    std::queue<base::OnceClosure> pending_calls =
        std::move(pending_method_calls_);
    while (!pending_calls.empty()) {
      std::move(pending_calls.front()).Run();
      pending_calls.pop();
    }
  }

  WebURLRequest request_;
  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;
  std::unique_ptr<WebURLLoader> url_loader_;
  std::queue<base::OnceClosure> pending_method_calls_;

  base::WeakPtrFactory<PrefetchedSignedExchangeLoader> weak_ptr_factory_{this};
};

// static
PrefetchedSignedExchangeManager* PrefetchedSignedExchangeManager::MaybeCreate(
    LocalFrame* frame,
    const String& outer_link_header,
    const String& inner_link_header,
    WebVector<std::unique_ptr<WebNavigationParams::PrefetchedSignedExchange>>
        prefetched_signed_exchanges) {
  if (prefetched_signed_exchanges.empty())
    return nullptr;
  std::unique_ptr<AlternateSignedExchangeResourceInfo> alternative_resources =
      AlternateSignedExchangeResourceInfo::CreateIfValid(outer_link_header,
                                                         inner_link_header);
  if (!alternative_resources) {
    // There is no "allowed-alt-sxg" link header for this resource.
    return nullptr;
  }

  HashMap<KURL, std::unique_ptr<WebNavigationParams::PrefetchedSignedExchange>>
      prefetched_exchanges_map;
  for (auto& exchange : prefetched_signed_exchanges) {
    const KURL outer_url = exchange->outer_url;
    prefetched_exchanges_map.Set(outer_url, std::move(exchange));
  }

  return MakeGarbageCollected<PrefetchedSignedExchangeManager>(
      frame, std::move(alternative_resources),
      std::move(prefetched_exchanges_map));
}

PrefetchedSignedExchangeManager::PrefetchedSignedExchangeManager(
    LocalFrame* frame,
    std::unique_ptr<AlternateSignedExchangeResourceInfo> alternative_resources,
    HashMap<KURL,
            std::unique_ptr<WebNavigationParams::PrefetchedSignedExchange>>
        prefetched_exchanges_map)
    : frame_(frame),
      alternative_resources_(std::move(alternative_resources)),
      prefetched_exchanges_map_(std::move(prefetched_exchanges_map)) {
}

PrefetchedSignedExchangeManager::~PrefetchedSignedExchangeManager() {}

void PrefetchedSignedExchangeManager::Trace(Visitor* visitor) const {
  visitor->Trace(frame_);
}

void PrefetchedSignedExchangeManager::StartPrefetchedLinkHeaderPreloads() {
  DCHECK(!started_);
  started_ = true;
  TriggerLoad();
  // Clears |prefetched_exchanges_map_| to release URLLoaderFactory in the
  // browser process.
  prefetched_exchanges_map_.clear();
  // Clears |alternative_resources_| which will not be used forever.
  alternative_resources_.reset();
}

std::unique_ptr<WebURLLoader>
PrefetchedSignedExchangeManager::MaybeCreateURLLoader(
    const WebURLRequest& request) {
  if (started_)
    return nullptr;
  const auto* matching_resource = alternative_resources_->FindMatchingEntry(
      request.Url(), request.GetRequestContext(),
      frame_->DomWindow()->navigator()->languages());
  if (!matching_resource)
    return nullptr;

  std::unique_ptr<PrefetchedSignedExchangeLoader> loader =
      std::make_unique<PrefetchedSignedExchangeLoader>(
          request, frame_->GetFrameScheduler()->GetTaskRunner(
                       TaskType::kInternalLoading));
  loaders_.emplace_back(loader->GetWeakPtr());
  return loader;
}

std::unique_ptr<WebURLLoader>
PrefetchedSignedExchangeManager::CreateDefaultURLLoader(
    const WebURLRequest& request) {
  return frame_->GetURLLoaderFactory()->CreateURLLoader(
      request,
      frame_->GetFrameScheduler()->CreateResourceLoadingTaskRunnerHandle(),
      frame_->GetFrameScheduler()
          ->CreateResourceLoadingMaybeUnfreezableTaskRunnerHandle(),
      /*keep_alive_handle=*/mojo::NullRemote(),
      WebBackForwardCacheLoaderHelper());
}

std::unique_ptr<WebURLLoader>
PrefetchedSignedExchangeManager::CreatePrefetchedSignedExchangeURLLoader(
    const WebURLRequest& request,
    mojo::PendingRemote<network::mojom::blink::URLLoaderFactory>
        loader_factory) {
  return Platform::Current()
      ->WrapURLLoaderFactory(std::move(loader_factory))
      ->CreateURLLoader(
          request,
          frame_->GetFrameScheduler()->CreateResourceLoadingTaskRunnerHandle(),
          frame_->GetFrameScheduler()
              ->CreateResourceLoadingMaybeUnfreezableTaskRunnerHandle(),
          /*keep_alive_handle=*/mojo::NullRemote(),
          WebBackForwardCacheLoaderHelper());
}

void PrefetchedSignedExchangeManager::TriggerLoad() {
  Vector<WebNavigationParams::PrefetchedSignedExchange*>
      maching_prefetched_exchanges;
  for (auto loader : loaders_) {
    if (!loader) {
      // The loader has been canceled.
      maching_prefetched_exchanges.emplace_back(nullptr);
      // We can continue the matching, because the distributor can't send
      // arbitrary information to the publisher using this resource.
      continue;
    }
    const auto* matching_resource = alternative_resources_->FindMatchingEntry(
        loader->request().Url(), loader->request().GetRequestContext(),
        frame_->DomWindow()->navigator()->languages());
    const auto alternative_url = matching_resource->alternative_url();
    if (!alternative_url.IsValid()) {
      // There is no matching "alternate" link header in outer response header.
      break;
    }
    const auto exchange_it = prefetched_exchanges_map_.find(alternative_url);
    if (exchange_it == prefetched_exchanges_map_.end()) {
      // There is no matching prefetched exchange.
      break;
    }
    if (String(exchange_it->value->header_integrity) !=
        matching_resource->header_integrity()) {
      // The header integrity doesn't match.
      break;
    }
    if (KURL(exchange_it->value->inner_url) !=
        matching_resource->anchor_url()) {
      // The inner URL doesn't match.
      break;
    }
    maching_prefetched_exchanges.emplace_back(exchange_it->value.get());
  }
  if (loaders_.size() != maching_prefetched_exchanges.size()) {
    // Need to load the all original resources in this case to prevent the
    // distributor from sending arbitrary information to the publisher.
    String message =
        "Failed to match prefetched alternative signed exchange subresources. "
        "Requesting the all original resources ignoreing all alternative signed"
        " exchange responses.";
    frame_->GetDocument()->AddConsoleMessage(
        MakeGarbageCollected<ConsoleMessage>(
            mojom::ConsoleMessageSource::kNetwork,
            mojom::ConsoleMessageLevel::kError, message));
    for (auto loader : loaders_) {
      if (!loader)
        continue;
      loader->SetURLLoader(CreateDefaultURLLoader(loader->request()));
    }
    return;
  }
  for (wtf_size_t i = 0; i < loaders_.size(); ++i) {
    auto loader = loaders_.at(i);
    if (!loader)
      continue;
    auto* prefetched_exchange = maching_prefetched_exchanges.at(i);
    mojo::Remote<network::mojom::blink::URLLoaderFactory> loader_factory(
        std::move(prefetched_exchange->loader_factory));
    mojo::PendingRemote<network::mojom::blink::URLLoaderFactory>
        loader_factory_clone;
    loader_factory->Clone(
        loader_factory_clone.InitWithNewPipeAndPassReceiver());
    // Reset loader_factory_handle to support loading the same resource again.
    prefetched_exchange->loader_factory = std::move(loader_factory_clone);
    loader->SetURLLoader(CreatePrefetchedSignedExchangeURLLoader(
        loader->request(), loader_factory.Unbind()));
  }
}

}  // namespace blink
