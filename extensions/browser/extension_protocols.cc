// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/browser/extension_protocols.h"

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/bind.h"
#include "base/compiler_specific.h"
#include "base/feature_list.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/format_macros.h"
#include "base/hash/sha1.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/metrics/field_trial.h"
#include "base/metrics/histogram.h"
#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"
#include "base/metrics/user_metrics.h"
#include "base/no_destructor.h"
#include "base/numerics/safe_conversions.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/task/thread_pool.h"
#include "base/threading/thread_restrictions.h"
#include "base/timer/elapsed_timer.h"
#include "build/build_config.h"
#include "components/keyed_service/content/browser_context_keyed_service_shutdown_notifier_factory.h"
#include "components/keyed_service/core/keyed_service_shutdown_notifier.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/file_url_loader.h"
#include "content/public/browser/navigation_ui_data.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/common/child_process_host.h"
#include "crypto/secure_hash.h"
#include "crypto/sha2.h"
#include "extensions/browser/content_verifier.h"
#include "extensions/browser/content_verify_job.h"
#include "extensions/browser/extension_navigation_ui_data.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_registry_factory.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/extension_util.h"
#include "extensions/browser/extensions_browser_client.h"
#include "extensions/browser/guest_view/web_view/web_view_guest.h"
#include "extensions/browser/guest_view/web_view/web_view_renderer_state.h"
#include "extensions/browser/info_map.h"
#include "extensions/browser/process_map.h"
#include "extensions/browser/process_map_factory.h"
#include "extensions/browser/url_request_util.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_features.h"
#include "extensions/common/extension_resource.h"
#include "extensions/common/file_util.h"
#include "extensions/common/identifiability_metrics.h"
#include "extensions/common/manifest_handlers/background_info.h"
#include "extensions/common/manifest_handlers/cross_origin_isolation_info.h"
#include "extensions/common/manifest_handlers/csp_info.h"
#include "extensions/common/manifest_handlers/icons_handler.h"
#include "extensions/common/manifest_handlers/incognito_info.h"
#include "extensions/common/manifest_handlers/shared_module_info.h"
#include "extensions/common/manifest_handlers/web_accessible_resources_info.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"
#include "net/base/filename_util.h"
#include "net/base/io_buffer.h"
#include "net/base/mime_util.h"
#include "net/base/net_errors.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_response_info.h"
#include "net/http/http_status_code.h"
#include "services/metrics/public/cpp/ukm_source_id.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/self_deleting_url_loader_factory.h"
#include "services/network/public/cpp/url_loader_completion_status.h"
#include "services/network/public/mojom/early_hints.mojom.h"
#include "services/network/public/mojom/fetch_api.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/common/loader/resource_type_util.h"
#include "url/origin.h"
#include "url/url_util.h"

using content::BrowserContext;
using extensions::Extension;
using extensions::SharedModuleInfo;

namespace extensions {
namespace {

ExtensionProtocolTestHandler* g_test_handler = nullptr;

// This is used to collect some metrics of load results, by wrapping the actual
// URLLoaderClient and observing success or failure.
//
// This approach is taken because loading can happen via things like
// content::CreateFileURLLoaderBypassingSecurityChecks(), and
// LoadResourceFromResourceBundle and it avoids having to modify all those
// places for a temporary study.
class ResultRecordingClient : public network::mojom::URLLoaderClient {
 public:
  ResultRecordingClient(
      const GURL& url,
      ukm::SourceIdObj ukm_source_id,
      mojo::PendingRemote<network::mojom::URLLoaderClient> real_client)
      : url_(url),
        ukm_source_id_(ukm_source_id),
        real_client_(std::move(real_client)) {}

  void OnReceiveEarlyHints(network::mojom::EarlyHintsPtr early_hints) override {
    real_client_->OnReceiveEarlyHints(std::move(early_hints));
  }

  void OnReceiveResponse(network::mojom::URLResponseHeadPtr response_head,
                         mojo::ScopedDataPipeConsumerHandle body) override {
    real_client_->OnReceiveResponse(std::move(response_head), std::move(body));
  }

  void OnReceiveRedirect(
      const net::RedirectInfo& redirect_info,
      network::mojom::URLResponseHeadPtr response_head) override {
    real_client_->OnReceiveRedirect(redirect_info, std::move(response_head));
  }

  void OnUploadProgress(int64_t current_position,
                        int64_t total_size,
                        OnUploadProgressCallback ack_callback) override {
    real_client_->OnUploadProgress(current_position, total_size,
                                   std::move(ack_callback));
  }

  void OnReceiveCachedMetadata(mojo_base::BigBuffer data) override {
    real_client_->OnReceiveCachedMetadata(std::move(data));
  }

  void OnTransferSizeUpdated(int32_t transfer_size_diff) override {
    real_client_->OnTransferSizeUpdated(transfer_size_diff);
  }

  void OnComplete(const network::URLLoaderCompletionStatus& status) override {
    RecordExtensionResourceAccessResult(
        ukm_source_id_, url_,
        status.error_code == net::OK ? ExtensionResourceAccessResult::kSuccess
                                     : ExtensionResourceAccessResult::kFailure);
    real_client_->OnComplete(status);
  }

 private:
  GURL url_;
  ukm::SourceIdObj ukm_source_id_;
  mojo::Remote<network::mojom::URLLoaderClient> real_client_;
};

mojo::PendingRemote<network::mojom::URLLoaderClient> WrapWithMetricsIfNeeded(
    const GURL& url,
    ukm::SourceIdObj ukm_source_id,
    mojo::PendingRemote<network::mojom::URLLoaderClient> in_client) {
  if (ukm_source_id == ukm::kInvalidSourceIdObj)
    return in_client;

  mojo::PendingRemote<network::mojom::URLLoaderClient> proxy_client_remote;
  auto proxy_client = std::make_unique<ResultRecordingClient>(
      url, ukm_source_id, std::move(in_client));

  mojo::MakeSelfOwnedReceiver(
      std::move(proxy_client),
      proxy_client_remote.InitWithNewPipeAndPassReceiver());
  return proxy_client_remote;
}

void GenerateBackgroundPageContents(const Extension* extension,
                                    std::string* mime_type,
                                    std::string* charset,
                                    std::string* data) {
  *mime_type = "text/html";
  *charset = "utf-8";
  *data = "<!DOCTYPE html>\n<body>\n";
  for (const auto& script : BackgroundInfo::GetBackgroundScripts(extension)) {
    *data += "<script src=\"";
    *data += script;
    *data += "\"></script>\n";
  }
}

base::Time GetFileLastModifiedTime(const base::FilePath& filename) {
  if (base::PathExists(filename)) {
    base::File::Info info;
    if (base::GetFileInfo(filename, &info))
      return info.last_modified;
  }
  return base::Time();
}

base::Time GetFileCreationTime(const base::FilePath& filename) {
  if (base::PathExists(filename)) {
    base::File::Info info;
    if (base::GetFileInfo(filename, &info))
      return info.creation_time;
  }
  return base::Time();
}

std::pair<base::FilePath, base::Time> ReadResourceFilePathAndLastModifiedTime(
    const extensions::ExtensionResource& resource,
    const base::FilePath& directory) {
  // NOTE: ExtensionResource::GetFilePath() must be called on a sequence which
  // tolerates blocking operations.
  base::FilePath file_path = resource.GetFilePath();
  base::Time last_modified_time = GetFileLastModifiedTime(file_path);
  base::Time dir_creation_time = GetFileCreationTime(directory);
  int64_t delta_seconds = (last_modified_time - dir_creation_time).InSeconds();
  if (delta_seconds >= 0) {
    UMA_HISTOGRAM_CUSTOM_COUNTS("Extensions.ResourceLastModifiedDelta",
                                delta_seconds, 1, base::Days(30).InSeconds(),
                                50);
  } else {
    UMA_HISTOGRAM_CUSTOM_COUNTS("Extensions.ResourceLastModifiedNegativeDelta",
                                -delta_seconds, 1, base::Days(30).InSeconds(),
                                50);
  }
  return std::make_pair(file_path, last_modified_time);
}

bool ExtensionCanLoadInIncognito(bool is_main_frame,
                                 const Extension* extension,
                                 bool extension_enabled_in_incognito) {
  if (!extension || !extension_enabled_in_incognito)
    return false;
  if (!is_main_frame || extension->is_login_screen_extension())
    return true;

  // Only allow incognito toplevel navigations to extension resources in
  // split mode. In spanning mode, the extension must run in a single process,
  // and an incognito tab prevents that.
  return IncognitoInfo::IsSplitMode(extension);
}

// Returns true if an chrome-extension:// resource should be allowed to load.
// Pass true for |is_incognito| only for incognito profiles and not Chrome OS
// guest mode profiles.
//
// Called on the UI thread.
bool AllowExtensionResourceLoad(const network::ResourceRequest& request,
                                network::mojom::RequestDestination destination,
                                ui::PageTransition page_transition,
                                int child_id,
                                bool is_incognito,
                                const Extension* extension,
                                bool extension_enabled_in_incognito,
                                const ExtensionSet& extensions,
                                const ProcessMap& process_map) {
  const bool is_main_frame =
      destination == network::mojom::RequestDestination::kDocument;
  if (is_incognito &&
      !ExtensionCanLoadInIncognito(is_main_frame, extension,
                                   extension_enabled_in_incognito)) {
    return false;
  }

  // The following checks are meant to replicate similar set of checks in the
  // renderer process, performed by ResourceRequestPolicy::CanRequestResource.
  // These are not exactly equivalent, because we don't have the same bits of
  // information. The two checks need to be kept in sync as much as possible, as
  // an exploited renderer can bypass the checks in ResourceRequestPolicy.

  // Check if the extension for which this request is made is indeed loaded in
  // the process sending the request. If not, we need to explicitly check if
  // the resource is explicitly accessible or fits in a set of exception cases.
  // Note: This allows a case where two extensions execute in the same renderer
  // process to request each other's resources. We can't do a more precise
  // check, since the renderer can lie about which extension has made the
  // request.
  if (process_map.Contains(request.url.host(), child_id))
    return true;

  // Frame navigations to extensions have already been checked in
  // the ExtensionNavigationThrottle.
  // Dedicated Worker (with PlzDedicatedWorker) and Shared Worker main scripts
  // can be loaded with extension URLs in browser process.
  // Service Worker and the imported scripts can be loaded with extension URLs
  // in browser process when PlzServiceWorker is enabled or during update check.
  if (child_id == content::ChildProcessHost::kInvalidUniqueID &&
      (blink::IsRequestDestinationFrame(destination) ||
       (base::FeatureList::IsEnabled(blink::features::kPlzDedicatedWorker) &&
        destination == network::mojom::RequestDestination::kWorker) ||
       destination == network::mojom::RequestDestination::kSharedWorker ||
       destination == network::mojom::RequestDestination::kScript ||
       destination == network::mojom::RequestDestination::kServiceWorker)) {
    return true;
  }

  // Allow the extension module embedder to grant permission for loads.
  if (ExtensionsBrowserClient::Get()->AllowCrossRendererResourceLoad(
          request, destination, page_transition, child_id, is_incognito,
          extension, extensions, process_map)) {
    return true;
  }

  // No special exceptions for cross-process loading. Block the load.
  return false;
}

// Returns true if the given URL references an icon in the given extension.
bool URLIsForExtensionIcon(const GURL& url, const Extension* extension) {
  DCHECK(url.SchemeIs(extensions::kExtensionScheme));
  if (!extension)
    return false;

  DCHECK_EQ(url.host(), extension->id());
  base::StringPiece path = url.path_piece();
  DCHECK(path.length() > 0 && path[0] == '/');
  base::StringPiece path_without_slash = path.substr(1);
  return IconsInfo::GetIcons(extension).ContainsPath(path_without_slash);
}

// Retrieves the path corresponding to an extension on disk. Returns |true| on
// success and populates |*path|; otherwise returns |false|.
bool GetDirectoryForExtensionURL(const GURL& url,
                                 const std::string& extension_id,
                                 const Extension* extension,
                                 const ExtensionSet& disabled_extensions,
                                 base::FilePath* out_path) {
  base::FilePath path;
  if (extension)
    path = extension->path();
  const Extension* disabled_extension =
      disabled_extensions.GetByID(extension_id);
  if (path.empty()) {
    // For disabled extensions, we only resolve the directory path to service
    // extension icon URL requests.
    if (URLIsForExtensionIcon(url, disabled_extension))
      path = disabled_extension->path();
  }

  if (!path.empty()) {
    *out_path = path;
    return true;
  }

  DLOG_IF(WARNING, !disabled_extension)
      << "Failed to get directory for extension " << extension_id;

  return false;
}

void GetSecurityPolicyForURL(const network::ResourceRequest& request,
                             const Extension& extension,
                             bool is_web_view_request,
                             std::string* content_security_policy,
                             const std::string** cross_origin_embedder_policy,
                             const std::string** cross_origin_opener_policy,
                             bool* send_cors_header,
                             bool* follow_symlinks_anywhere) {
  std::string resource_path = request.url.path();

  // Use default CSP for <webview>.
  if (!is_web_view_request) {
    *content_security_policy =
        CSPInfo::GetResourceContentSecurityPolicy(&extension, resource_path);
  }

  *cross_origin_embedder_policy =
      CrossOriginIsolationHeader::GetCrossOriginEmbedderPolicy(extension);
  *cross_origin_opener_policy =
      CrossOriginIsolationHeader::GetCrossOriginOpenerPolicy(extension);

  if (WebAccessibleResourcesInfo::IsResourceWebAccessible(
          &extension, resource_path, request.request_initiator)) {
    *send_cors_header = true;
  }

  *follow_symlinks_anywhere =
      (extension.creation_flags() & Extension::FOLLOW_SYMLINKS_ANYWHERE) != 0;
}

bool IsPathEqualTo(const GURL& url, base::StringPiece test) {
  base::StringPiece path_piece = url.path_piece();
  return path_piece.size() > 1 && path_piece.substr(1) == test;
}

bool IsFaviconURL(const GURL& url) {
  return base::FeatureList::IsEnabled(
             extensions_features::kNewExtensionFaviconHandling) &&
         (IsPathEqualTo(url, kFaviconSourcePath) ||
          IsPathEqualTo(url, base::StrCat({kFaviconSourcePath, "/"})));
}

bool IsBackgroundPageURL(const GURL& url) {
  return IsPathEqualTo(url, kGeneratedBackgroundPageFilename);
}

scoped_refptr<net::HttpResponseHeaders> BuildHttpHeaders(
    const std::string& content_security_policy,
    const std::string* cross_origin_embedder_policy,
    const std::string* cross_origin_opener_policy,
    bool send_cors_header,
    bool include_allow_service_worker_header) {
  std::string raw_headers;
  raw_headers.append("HTTP/1.1 200 OK");
  if (!content_security_policy.empty()) {
    raw_headers.append(1, '\0');
    raw_headers.append("Content-Security-Policy: ");
    raw_headers.append(content_security_policy);
  }

  if (cross_origin_embedder_policy) {
    raw_headers.append(1, '\0');
    raw_headers.append("Cross-Origin-Embedder-Policy: ");
    raw_headers.append(*cross_origin_embedder_policy);
  }

  if (cross_origin_opener_policy) {
    raw_headers.append(1, '\0');
    raw_headers.append("Cross-Origin-Opener-Policy: ");
    raw_headers.append(*cross_origin_opener_policy);
  }

  if (send_cors_header) {
    raw_headers.append(1, '\0');
    raw_headers.append("Access-Control-Allow-Origin: *");
    raw_headers.append(1, '\0');
    raw_headers.append("Cross-Origin-Resource-Policy: cross-origin");
  }

  if (include_allow_service_worker_header) {
    raw_headers.append(1, '\0');
    raw_headers.append("Service-Worker-Allowed: /");
  }

  raw_headers.append(2, '\0');
  return base::MakeRefCounted<net::HttpResponseHeaders>(raw_headers);
}

void AddCacheHeaders(net::HttpResponseHeaders& headers,
                     base::Time last_modified_time) {
  // On Fuchsia, some resources are served from read-only filesystems which
  // don't manage creation timestamps. Cache-control headers should still
  // be generated for those resources.
#if !BUILDFLAG(IS_FUCHSIA)
  if (last_modified_time.is_null()) {
    return;
  }
#endif  // !BUILDFLAG(IS_FUCHSIA)

  // Hash the time and make an etag to avoid exposing the exact
  // user installation time of the extension.
  std::string hash =
      base::StringPrintf("%" PRId64, last_modified_time.ToInternalValue());
  hash = base::SHA1HashString(hash);
  std::string etag;
  base::Base64Encode(hash, &etag);
  etag = "\"" + etag + "\"";
  headers.SetHeader("ETag", etag);

  // Also force revalidation.
  headers.SetHeader("cache-control", "no-cache");
}

class FileLoaderObserver : public content::FileURLLoaderObserver {
 public:
  explicit FileLoaderObserver(scoped_refptr<ContentVerifyJob> verify_job)
      : verify_job_(std::move(verify_job)) {}

  FileLoaderObserver(const FileLoaderObserver&) = delete;
  FileLoaderObserver& operator=(const FileLoaderObserver&) = delete;

  ~FileLoaderObserver() override {
    base::AutoLock auto_lock(lock_);
    UMA_HISTOGRAM_COUNTS_1M("ExtensionUrlRequest.TotalKbRead",
                            bytes_read_ / 1024);
    UMA_HISTOGRAM_COUNTS_1M("ExtensionUrlRequest.SeekPosition", seek_position_);
    if (request_timer_.get())
      UMA_HISTOGRAM_TIMES("ExtensionUrlRequest.Latency",
                          request_timer_->Elapsed());
  }

  void OnStart() override {
    base::AutoLock auto_lock(lock_);
    request_timer_ = std::make_unique<base::ElapsedTimer>();
  }

  void OnSeekComplete(int64_t result) override {
    DCHECK_EQ(seek_position_, 0);
    base::AutoLock auto_lock(lock_);
    seek_position_ = result;
    // TODO(asargent) - we'll need to add proper support for range headers.
    // crbug.com/369895.
    const bool is_seek_contiguous = result == bytes_read_;
    if (result > 0 && verify_job_.get() && !is_seek_contiguous)
      verify_job_ = nullptr;
  }

  void OnRead(base::span<char> buffer,
              mojo::DataPipeProducer::DataSource::ReadResult* result) override {
    DCHECK(result);
    {
      base::AutoLock auto_lock(lock_);
      bytes_read_ += result->bytes_read;
      if (verify_job_) {
        // Note: We still pass the data to |verify_job_|, even if there was a
        // read error, because some errors are ignorable. See
        // ContentVerifyJob::BytesRead() for more details.
        verify_job_->Read(static_cast<const char*>(buffer.data()),
                          result->bytes_read, result->result);
      }
    }
  }

  void OnDone() override {
    base::AutoLock auto_lock(lock_);
    if (verify_job_.get())
      verify_job_->Done();
  }

 private:
  int64_t bytes_read_ = 0;
  int64_t seek_position_ = 0;
  std::unique_ptr<base::ElapsedTimer> request_timer_;
  scoped_refptr<ContentVerifyJob> verify_job_;
  // To synchronize access to all members.
  base::Lock lock_;
};

class ExtensionURLLoaderFactory;

class ExtensionURLLoader : public network::mojom::URLLoader {
 public:
  static void CreateAndStart(
      mojo::PendingReceiver<network::mojom::URLLoader> loader,
      mojo::PendingRemote<network::mojom::URLLoaderClient> client,
      const network::ResourceRequest& request,
      bool is_web_view_request,
      int render_process_id,
      scoped_refptr<extensions::InfoMap> extension_info_map,
      content::BrowserContext* browser_context,
      ukm::SourceIdObj ukm_source_id) {
    DCHECK(browser_context);
    // A raw `new` is okay because `ExtensionURLLoader` is "self-owned". It
    // will delete itself when needed (when the request is completed, or when
    // the URLLoader or the URLLoaderClient connection gets dropped).
    auto* url_loader = new ExtensionURLLoader(
        std::move(loader), std::move(client), request, is_web_view_request,
        render_process_id, extension_info_map, browser_context, ukm_source_id);
    url_loader->Start();
  }

  ExtensionURLLoader(const ExtensionURLLoader&) = delete;
  ExtensionURLLoader& operator=(const ExtensionURLLoader&) = delete;

  // network::mojom::URLLoader:
  void FollowRedirect(
      const std::vector<std::string>& removed_headers,
      const net::HttpRequestHeaders& modified_headers,
      const net::HttpRequestHeaders& modified_cors_exempt_headers,
      const absl::optional<GURL>& new_url) override {
    // new_url isn't expected to have a value, but prefer it if it's populated.
    if (new_url.has_value())
      request_.url = new_url.value();

    Start();
  }
  void SetPriority(net::RequestPriority priority,
                   int32_t intra_priority_value) override {}
  void PauseReadingBodyFromNet() override {}
  void ResumeReadingBodyFromNet() override {}

 private:
  ~ExtensionURLLoader() override = default;
  ExtensionURLLoader(
      mojo::PendingReceiver<network::mojom::URLLoader> loader,
      mojo::PendingRemote<network::mojom::URLLoaderClient> client,
      const network::ResourceRequest& request,
      bool is_web_view_request,
      int render_process_id,
      scoped_refptr<extensions::InfoMap> extension_info_map,
      content::BrowserContext* browser_context,
      ukm::SourceIdObj ukm_source_id)
      : request_(request),
        browser_context_(browser_context),
        is_web_view_request_(is_web_view_request),
        ukm_source_id_(ukm_source_id),
        render_process_id_(render_process_id),
        extension_info_map_(extension_info_map) {
    client =
        WrapWithMetricsIfNeeded(request.url, ukm_source_id, std::move(client));
    client_.Bind(std::move(client));
    loader_.Bind(std::move(loader));
    loader_.set_disconnect_handler(base::BindOnce(
        &ExtensionURLLoader::OnMojoDisconnect, weak_ptr_factory_.GetWeakPtr()));
  }

  // `this` instance should only be `delete`ed after completing handling of the
  // `request_` (e.g. after sending the response back to the `client_` or after
  // encountering an error and communicating the error to the the `client_`).
  void DeleteThis() { delete this; }

  void CompleteRequestAndDeleteThis(int status) {
    client_->OnComplete(network::URLLoaderCompletionStatus(status));
    DeleteThis();
  }

  void Start() {
    if (browser_context_->ShutdownStarted()) {
      CompleteRequestAndDeleteThis(net::ERR_FAILED);
      return;
    }

    const std::string extension_id = request_.url.host();
    ExtensionRegistry* registry = ExtensionRegistry::Get(browser_context_);
    scoped_refptr<const Extension> extension =
        registry->GenerateInstalledExtensionsSet()->GetByIDorGUID(extension_id);
    const ExtensionSet& enabled_extensions = registry->enabled_extensions();
    const ProcessMap* process_map = ProcessMap::Get(browser_context_);
    bool incognito_enabled =
        extensions::util::IsIncognitoEnabled(extension_id, browser_context_);

    // Redirect guid to id.
    if (base::FeatureList::IsEnabled(
            extensions_features::kExtensionDynamicURLRedirection) &&
        extension && request_.url.host() == extension->guid()) {
      GURL::Replacements replace_host;
      replace_host.SetHostStr(extension->id());
      GURL new_url = request_.url.ReplaceComponents(replace_host);
      request_.url = new_url;
      net::RedirectInfo redirect_info;
      redirect_info.new_method = request_.method,
      redirect_info.new_url = request_.url;
      redirect_info.status_code = net::HTTP_TEMPORARY_REDIRECT;
      network::mojom::URLResponseHeadPtr response_head(
          ::network::mojom::URLResponseHead::New());
      client_->OnReceiveRedirect(redirect_info, std::move(response_head));
      return;
    }

    if (!AllowExtensionResourceLoad(
            request_, request_.destination,
            static_cast<ui::PageTransition>(request_.transition_type),
            render_process_id_, browser_context_->IsOffTheRecord(),
            extension.get(), incognito_enabled, enabled_extensions,
            *process_map)) {
      CompleteRequestAndDeleteThis(net::ERR_BLOCKED_BY_CLIENT);
      return;
    }

    base::FilePath directory_path;
    if (!GetDirectoryForExtensionURL(
            request_.url, extension_id, extension.get(),
            registry->disabled_extensions(), &directory_path)) {
      CompleteRequestAndDeleteThis(net::ERR_FAILED);
      return;
    }

    LoadExtension(extension, std::move(directory_path));
  }

  static void StartVerifyJob(
      network::ResourceRequest request,
      mojo::PendingReceiver<network::mojom::URLLoader> loader,
      mojo::PendingRemote<network::mojom::URLLoaderClient> client,
      scoped_refptr<ContentVerifier> content_verifier,
      const ExtensionResource& resource,
      scoped_refptr<net::HttpResponseHeaders> response_headers) {
    scoped_refptr<ContentVerifyJob> verify_job;
    if (content_verifier) {
      verify_job = content_verifier->CreateAndStartJobFor(
          resource.extension_id(), resource.extension_root(),
          resource.relative_path());
    }

    content::CreateFileURLLoaderBypassingSecurityChecks(
        request, std::move(loader), std::move(client),
        std::make_unique<FileLoaderObserver>(std::move(verify_job)),
        /* allow_directory_listing */ false, std::move(response_headers));
  }

  void OnFilePathAndLastModifiedTimeRead(
      const extensions::ExtensionResource& resource,
      scoped_refptr<net::HttpResponseHeaders> headers,
      std::pair<base::FilePath, base::Time> file_path_and_time) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    const auto& read_file_path = file_path_and_time.first;
    const auto& last_modified_time = file_path_and_time.second;
    request_.url = net::FilePathToFileURL(read_file_path);
    scoped_refptr<ContentVerifier> content_verifier =
        extension_info_map_->content_verifier();

    AddCacheHeaders(*headers, last_modified_time);
    content::GetIOThreadTaskRunner({})->PostTask(
        FROM_HERE,
        base::BindOnce(&StartVerifyJob, std::move(request_), loader_.Unbind(),
                       client_.Unbind(), std::move(content_verifier), resource,
                       std::move(headers)));
    DeleteThis();
  }

  void OnFaviconRetrieved(mojo::StructPtr<network::mojom::URLResponseHead> head,
                          scoped_refptr<base::RefCountedMemory> bitmap_data) {
    if (bitmap_data) {
      head->mime_type = "image/bmp";
      WriteData(std::move(head),
                base::as_bytes(
                    base::make_span(bitmap_data->data(), bitmap_data->size())));
    } else {
      CompleteRequestAndDeleteThis(net::ERR_FAILED);
    }
  }

  void WriteData(mojo::StructPtr<network::mojom::URLResponseHead> head,
                 base::span<const uint8_t> contents) {
    DCHECK(contents.data());
    uint32_t size = base::saturated_cast<uint32_t>(contents.size());
    mojo::ScopedDataPipeProducerHandle producer_handle;
    mojo::ScopedDataPipeConsumerHandle consumer_handle;
    if (mojo::CreateDataPipe(size, producer_handle, consumer_handle) !=
        MOJO_RESULT_OK) {
      CompleteRequestAndDeleteThis(net::ERR_FAILED);
      return;
    }
    MojoResult result = producer_handle->WriteData(contents.data(), &size,
                                                   MOJO_WRITE_DATA_FLAG_NONE);
    if (result != MOJO_RESULT_OK || size < contents.size()) {
      CompleteRequestAndDeleteThis(net::ERR_FAILED);
      return;
    }

    client_->OnReceiveResponse(std::move(head), std::move(consumer_handle));

    CompleteRequestAndDeleteThis(net::OK);
  }

  void LoadExtension(scoped_refptr<const Extension> extension,
                     base::FilePath directory_path) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    std::string content_security_policy;
    const std::string* cross_origin_embedder_policy = nullptr;
    const std::string* cross_origin_opener_policy = nullptr;
    bool send_cors_header = false;
    bool follow_symlinks_anywhere = false;
    bool include_allow_service_worker_header = false;

    // Log if loading an extension resource not listed as a web accessible
    // resource from a sandboxed page.
    if (request_.request_initiator.has_value() &&
        request_.request_initiator->opaque() &&
        request_.request_initiator->GetTupleOrPrecursorTupleIfOpaque()
                .scheme() == kExtensionScheme) {
      // Surface opaque origin for web accessible resource verification.
      auto origin = url::Origin::Create(
          request_.request_initiator->GetTupleOrPrecursorTupleIfOpaque()
              .GetURL());
      bool is_web_accessible_resource =
          WebAccessibleResourcesInfo::IsResourceWebAccessible(
              extension.get(), request_.url.path(), origin);
      base::UmaHistogramBoolean(
          "Extensions.SandboxedPageLoad.IsWebAccessibleResource",
          is_web_accessible_resource);
    }

    if (extension) {
      GetSecurityPolicyForURL(
          request_, *extension, is_web_view_request_, &content_security_policy,
          &cross_origin_embedder_policy, &cross_origin_opener_policy,
          &send_cors_header, &follow_symlinks_anywhere);
      if (BackgroundInfo::IsServiceWorkerBased(extension.get())) {
        include_allow_service_worker_header =
            request_.destination ==
                network::mojom::RequestDestination::kServiceWorker &&
            request_.url ==
                extension->GetResourceURL(
                    BackgroundInfo::GetBackgroundServiceWorkerScript(
                        extension.get()));
      }
    }

    const bool is_background_page_url = IsBackgroundPageURL(request_.url);
    const bool is_favicon_url = IsFaviconURL(request_.url);
    if (is_background_page_url || is_favicon_url) {
      // Handle background page requests immediately with a simple generated
      // chunk of HTML.

      // Leave cache headers out of generated background page jobs.
      auto head = network::mojom::URLResponseHead::New();
      head->headers = BuildHttpHeaders(
          content_security_policy, cross_origin_embedder_policy,
          cross_origin_opener_policy, false /* send_cors_headers */,
          include_allow_service_worker_header);
      if (is_background_page_url) {
        std::string contents;
        GenerateBackgroundPageContents(extension.get(), &head->mime_type,
                                       &head->charset, &contents);
        WriteData(std::move(head), base::as_bytes(base::make_span(contents)));
      } else if (is_favicon_url) {
        tracker_ = std::make_unique<base::CancelableTaskTracker>();
        ExtensionsBrowserClient::Get()->GetFavicon(
            browser_context_, extension.get(), request_.url, tracker_.get(),
            base::BindOnce(&ExtensionURLLoader::OnFaviconRetrieved,
                           weak_ptr_factory_.GetWeakPtr(), std::move(head)));
      }
      return;
    }

    auto headers =
        BuildHttpHeaders(content_security_policy, cross_origin_embedder_policy,
                         cross_origin_opener_policy, send_cors_header,
                         include_allow_service_worker_header);
    // Component extension resources may be part of the embedder's resource
    // files, for example component_extension_resources.pak in Chrome.
    int resource_id = 0;
    const base::FilePath bundle_resource_path =
        ExtensionsBrowserClient::Get()->GetBundleResourcePath(
            request_, directory_path, &resource_id);
    if (!bundle_resource_path.empty()) {
      ExtensionsBrowserClient::Get()->LoadResourceFromResourceBundle(
          request_, loader_.Unbind(), bundle_resource_path, resource_id,
          std::move(headers), client_.Unbind());
      DeleteThis();
      return;
    }

    base::FilePath relative_path =
        file_util::ExtensionURLToRelativeFilePath(request_.url);

    // Do not allow requests for resources in the _metadata folder, since any
    // files there are internal implementation details that should not be
    // considered part of the extension.
    if (base::FilePath(kMetadataFolder).IsParent(relative_path)) {
      CompleteRequestAndDeleteThis(net::ERR_FILE_NOT_FOUND);
      return;
    }

    // Handle shared resources (extension A loading resources out of extension
    // B).
    std::string extension_id = extension->id();
    std::string path = request_.url.path();
    if (SharedModuleInfo::IsImportedPath(path)) {
      std::string new_extension_id;
      std::string new_relative_path;
      SharedModuleInfo::ParseImportedPath(path, &new_extension_id,
                                          &new_relative_path);
      ExtensionRegistry* registry = ExtensionRegistry::Get(browser_context_);
      const Extension* new_extension =
          registry->enabled_extensions().GetByID(new_extension_id);
      if (SharedModuleInfo::ImportsExtensionById(extension.get(),
                                                 new_extension_id) &&
          new_extension) {
        directory_path = new_extension->path();
        extension_id = new_extension_id;
        relative_path = base::FilePath::FromUTF8Unsafe(new_relative_path);
      } else {
        CompleteRequestAndDeleteThis(net::ERR_BLOCKED_BY_CLIENT);
        return;
      }
    }

    if (g_test_handler)
      g_test_handler->Run(&directory_path, &relative_path);

    extensions::ExtensionResource resource(extension_id, directory_path,
                                           relative_path);
    if (follow_symlinks_anywhere)
      resource.set_follow_symlinks_anywhere();

    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock()},
        base::BindOnce(&ReadResourceFilePathAndLastModifiedTime, resource,
                       directory_path),
        base::BindOnce(&ExtensionURLLoader::OnFilePathAndLastModifiedTimeRead,
                       weak_ptr_factory_.GetWeakPtr(), resource,
                       std::move(headers)));
  }

  void OnMojoDisconnect() { DeleteThis(); }

  mojo::Receiver<network::mojom::URLLoader> loader_{this};
  mojo::Remote<network::mojom::URLLoaderClient> client_;
  network::ResourceRequest request_;
  const raw_ptr<content::BrowserContext> browser_context_;
  const bool is_web_view_request_;
  const ukm::SourceIdObj ukm_source_id_;

  // We store the ID and get RenderProcessHost each time it's needed. This is to
  // avoid holding on to stale pointers if we get requests past the lifetime of
  // the objects.
  const int render_process_id_;
  const scoped_refptr<extensions::InfoMap> extension_info_map_;

  // Tracker for favicon callback.
  std::unique_ptr<base::CancelableTaskTracker> tracker_;

  base::WeakPtrFactory<ExtensionURLLoader> weak_ptr_factory_{this};
};

class ExtensionURLLoaderFactory : public network::SelfDeletingURLLoaderFactory {
 public:
  ExtensionURLLoaderFactory(const ExtensionURLLoaderFactory&) = delete;
  ExtensionURLLoaderFactory& operator=(const ExtensionURLLoaderFactory&) =
      delete;

  static mojo::PendingRemote<network::mojom::URLLoaderFactory> Create(
      content::BrowserContext* browser_context,
      ukm::SourceIdObj ukm_source_id,
      bool is_web_view_request,
      int render_process_id) {
    DCHECK(browser_context);

    mojo::PendingRemote<network::mojom::URLLoaderFactory> pending_remote;

    // Return an unbound |pending_remote| if the |browser_context| has already
    // started shutting down.
    if (browser_context->ShutdownStarted())
      return pending_remote;

    // Manages its own lifetime.
    new ExtensionURLLoaderFactory(
        browser_context, ukm_source_id, is_web_view_request, render_process_id,
        pending_remote.InitWithNewPipeAndPassReceiver());

    return pending_remote;
  }

  static void EnsureShutdownNotifierFactoryBuilt() {
    BrowserContextShutdownNotifierFactory::GetInstance();
  }

 private:
  // Constructs ExtensionURLLoaderFactory bound to the |factory_receiver|.
  //
  // The factory is self-owned - it will delete itself once there are no more
  // receivers (including the receiver associated with the returned
  // mojo::PendingRemote and the receivers bound by the Clone method).  See also
  // the network::SelfDeletingURLLoaderFactory::OnDisconnect method.
  ExtensionURLLoaderFactory(
      content::BrowserContext* browser_context,
      ukm::SourceIdObj ukm_source_id,
      bool is_web_view_request,
      int render_process_id,
      mojo::PendingReceiver<network::mojom::URLLoaderFactory> factory_receiver)
      : network::SelfDeletingURLLoaderFactory(std::move(factory_receiver)),
        browser_context_(browser_context),
        is_web_view_request_(is_web_view_request),
        ukm_source_id_(ukm_source_id),
        render_process_id_(render_process_id) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    extension_info_map_ =
        extensions::ExtensionSystem::Get(browser_context_)->info_map();

    // base::Unretained is safe below, because lifetime of
    // |browser_context_shutdown_subscription_| guarantees that
    // OnBrowserContextDestroyed won't be called after |this| is destroyed.
    browser_context_shutdown_subscription_ =
        BrowserContextShutdownNotifierFactory::GetInstance()
            ->Get(browser_context)
            ->Subscribe(base::BindRepeating(
                &ExtensionURLLoaderFactory::OnBrowserContextDestroyed,
                base::Unretained(this)));
  }

  ~ExtensionURLLoaderFactory() override = default;

  // network::mojom::URLLoaderFactory:
  void CreateLoaderAndStart(
      mojo::PendingReceiver<network::mojom::URLLoader> loader,
      int32_t request_id,
      uint32_t options,
      const network::ResourceRequest& request,
      mojo::PendingRemote<network::mojom::URLLoaderClient> client,
      const net::MutableNetworkTrafficAnnotationTag& traffic_annotation)
      override {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    DCHECK_EQ(kExtensionScheme, request.url.scheme());
    ExtensionURLLoader::CreateAndStart(std::move(loader), std::move(client),
                                       request, is_web_view_request_,
                                       render_process_id_, extension_info_map_,
                                       browser_context_, ukm_source_id_);
  }

  void OnBrowserContextDestroyed() {
    // When |browser_context_| gets destroyed, |this| factory is not able to
    // serve any more requests.
    DisconnectReceiversAndDestroy();
  }

  class BrowserContextShutdownNotifierFactory
      : public BrowserContextKeyedServiceShutdownNotifierFactory {
   public:
    static BrowserContextShutdownNotifierFactory* GetInstance() {
      static base::NoDestructor<BrowserContextShutdownNotifierFactory>
          s_factory;
      return s_factory.get();
    }

    // No copying.
    BrowserContextShutdownNotifierFactory(
        const BrowserContextShutdownNotifierFactory&) = delete;
    BrowserContextShutdownNotifierFactory& operator=(
        const BrowserContextShutdownNotifierFactory&) = delete;

   private:
    friend class base::NoDestructor<BrowserContextShutdownNotifierFactory>;
    BrowserContextShutdownNotifierFactory()
        : BrowserContextKeyedServiceShutdownNotifierFactory(
              "ExtensionURLLoaderFactory::"
              "BrowserContextShutdownNotifierFactory") {
      DependsOn(ExtensionRegistryFactory::GetInstance());
      DependsOn(ProcessMapFactory::GetInstance());
    }
  };

  raw_ptr<content::BrowserContext> browser_context_;
  bool is_web_view_request_;
  ukm::SourceIdObj ukm_source_id_;

  // We store the ID and get RenderProcessHost each time it's needed. This is to
  // avoid holding on to stale pointers if we get requests past the lifetime of
  // the objects.
  const int render_process_id_;
  scoped_refptr<extensions::InfoMap> extension_info_map_;

  base::CallbackListSubscription browser_context_shutdown_subscription_;
};

}  // namespace

void SetExtensionProtocolTestHandler(ExtensionProtocolTestHandler* handler) {
  g_test_handler = handler;
}

mojo::PendingRemote<network::mojom::URLLoaderFactory>
CreateExtensionNavigationURLLoaderFactory(
    content::BrowserContext* browser_context,
    ukm::SourceIdObj ukm_source_id,
    bool is_web_view_request) {
  return ExtensionURLLoaderFactory::Create(
      browser_context, ukm_source_id, is_web_view_request,
      content::ChildProcessHost::kInvalidUniqueID);
}

mojo::PendingRemote<network::mojom::URLLoaderFactory>
CreateExtensionWorkerMainResourceURLLoaderFactory(
    content::BrowserContext* browser_context) {
  return ExtensionURLLoaderFactory::Create(
      browser_context, ukm::kInvalidSourceIdObj,
      /*is_web_view_request=*/false,
      content::ChildProcessHost::kInvalidUniqueID);
}

mojo::PendingRemote<network::mojom::URLLoaderFactory>
CreateExtensionServiceWorkerScriptURLLoaderFactory(
    content::BrowserContext* browser_context) {
  return ExtensionURLLoaderFactory::Create(
      browser_context, ukm::kInvalidSourceIdObj,
      /*is_web_view_request=*/false,
      content::ChildProcessHost::kInvalidUniqueID);
}

mojo::PendingRemote<network::mojom::URLLoaderFactory>
CreateExtensionURLLoaderFactory(int render_process_id, int render_frame_id) {
  content::RenderProcessHost* process_host =
      content::RenderProcessHost::FromID(render_process_id);
  content::BrowserContext* browser_context = process_host->GetBrowserContext();
  bool is_web_view_request =
      WebViewGuest::FromFrameID(render_process_id, render_frame_id) != nullptr;

  content::RenderFrameHost* rfh =
      content::RenderFrameHost::FromID(render_process_id, render_frame_id);
  ukm::SourceIdObj ukm_source_id = ukm::kInvalidSourceIdObj;
  if (rfh)
    ukm_source_id = ukm::SourceIdObj::FromInt64(rfh->GetPageUkmSourceId());

  return ExtensionURLLoaderFactory::Create(
      browser_context, ukm_source_id, is_web_view_request, render_process_id);
}

void EnsureExtensionURLLoaderFactoryShutdownNotifierFactoryBuilt() {
  ExtensionURLLoaderFactory::EnsureShutdownNotifierFactoryBuilt();
}

}  // namespace extensions
