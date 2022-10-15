/*
 * Copyright (C) 2006, 2007, 2008 Apple Inc. All rights reserved.
 * Copyright (C) 2011 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Computer, Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "third_party/blink/renderer/core/loader/document_loader.h"

#include <memory>
#include <utility>

#include "base/auto_reset.h"
#include "base/containers/flat_map.h"
#include "base/debug/dump_without_crashing.h"
#include "base/feature_list.h"
#include "base/metrics/field_trial_params.h"
#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"
#include "base/numerics/safe_conversions.h"
#include "base/time/default_tick_clock.h"
#include "base/types/optional_util.h"
#include "build/chromeos_buildflags.h"
#include "services/network/public/cpp/client_hints.h"
#include "services/network/public/cpp/web_sandbox_flags.h"
#include "services/network/public/mojom/web_sandbox_flags.mojom-blink.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/public/common/client_hints/client_hints.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/common/metrics/accept_language_and_content_language_usage.h"
#include "third_party/blink/public/common/permissions_policy/permissions_policy.h"
#include "third_party/blink/public/common/scheme_registry.h"
#include "third_party/blink/public/mojom/commit_result/commit_result.mojom-blink.h"
#include "third_party/blink/public/mojom/fetch/fetch_api_request.mojom-blink.h"
#include "third_party/blink/public/mojom/frame/frame.mojom-blink.h"
#include "third_party/blink/public/mojom/page/page.mojom-blink.h"
#include "third_party/blink/public/mojom/service_worker/service_worker_fetch_handler_type.mojom-blink.h"
#include "third_party/blink/public/platform/modules/service_worker/web_service_worker_network_provider.h"
#include "third_party/blink/public/platform/platform.h"
#include "third_party/blink/public/platform/web_content_security_policy_struct.h"
#include "third_party/blink/public/platform/web_url_request.h"
#include "third_party/blink/public/web/blink.h"
#include "third_party/blink/public/web/web_navigation_type.h"
#include "third_party/blink/renderer/bindings/core/v8/script_controller.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/dom/document_init.h"
#include "third_party/blink/renderer/core/dom/document_parser.h"
#include "third_party/blink/renderer/core/dom/events/event.h"
#include "third_party/blink/renderer/core/dom/scriptable_document_parser.h"
#include "third_party/blink/renderer/core/dom/weak_identifier_map.h"
#include "third_party/blink/renderer/core/execution_context/security_context_init.h"
#include "third_party/blink/renderer/core/execution_context/window_agent.h"
#include "third_party/blink/renderer/core/execution_context/window_agent_factory.h"
#include "third_party/blink/renderer/core/fragment_directive/text_fragment_anchor.h"
#include "third_party/blink/renderer/core/frame/csp/content_security_policy.h"
#include "third_party/blink/renderer/core/frame/deprecation/deprecation.h"
#include "third_party/blink/renderer/core/frame/frame_console.h"
#include "third_party/blink/renderer/core/frame/intervention.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/local_frame_client.h"
#include "third_party/blink/renderer/core/frame/navigator.h"
#include "third_party/blink/renderer/core/frame/settings.h"
#include "third_party/blink/renderer/core/html/html_document.h"
#include "third_party/blink/renderer/core/html/html_frame_owner_element.h"
#include "third_party/blink/renderer/core/html/html_object_element.h"
#include "third_party/blink/renderer/core/html/parser/html_parser_idioms.h"
#include "third_party/blink/renderer/core/input/event_handler.h"
#include "third_party/blink/renderer/core/inspector/console_message.h"
#include "third_party/blink/renderer/core/inspector/inspector_trace_events.h"
#include "third_party/blink/renderer/core/inspector/main_thread_debugger.h"
#include "third_party/blink/renderer/core/loader/alternate_signed_exchange_resource_info.h"
#include "third_party/blink/renderer/core/loader/frame_client_hints_preferences_context.h"
#include "third_party/blink/renderer/core/loader/frame_fetch_context.h"
#include "third_party/blink/renderer/core/loader/frame_loader.h"
#include "third_party/blink/renderer/core/loader/idleness_detector.h"
#include "third_party/blink/renderer/core/loader/interactive_detector.h"
#include "third_party/blink/renderer/core/loader/old_document_info_for_commit.h"
#include "third_party/blink/renderer/core/loader/prefetched_signed_exchange_manager.h"
#include "third_party/blink/renderer/core/loader/preload_helper.h"
#include "third_party/blink/renderer/core/loader/progress_tracker.h"
#include "third_party/blink/renderer/core/loader/subresource_filter.h"
#include "third_party/blink/renderer/core/mobile_metrics/mobile_friendliness_checker.h"
#include "third_party/blink/renderer/core/navigation_api/navigation_api.h"
#include "third_party/blink/renderer/core/origin_trials/origin_trial_context.h"
#include "third_party/blink/renderer/core/page/frame_tree.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "third_party/blink/renderer/core/permissions_policy/document_policy_parser.h"
#include "third_party/blink/renderer/core/probe/core_probes.h"
#include "third_party/blink/renderer/core/timing/dom_window_performance.h"
#include "third_party/blink/renderer/core/timing/profiler_group.h"
#include "third_party/blink/renderer/core/timing/window_performance.h"
#include "third_party/blink/renderer/core/xml/document_xslt.h"
#include "third_party/blink/renderer/platform/bindings/microtask.h"
#include "third_party/blink/renderer/platform/bindings/script_forbidden_scope.h"
#include "third_party/blink/renderer/platform/bindings/v8_per_isolate_data.h"
#include "third_party/blink/renderer/platform/fonts/font_performance.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/loader/cors/cors.h"
#include "third_party/blink/renderer/platform/loader/fetch/fetch_initiator_type_names.h"
#include "third_party/blink/renderer/platform/loader/fetch/fetch_parameters.h"
#include "third_party/blink/renderer/platform/loader/fetch/loader_freeze_mode.h"
#include "third_party/blink/renderer/platform/loader/fetch/memory_cache.h"
#include "third_party/blink/renderer/platform/loader/fetch/resource_fetcher.h"
#include "third_party/blink/renderer/platform/loader/fetch/resource_loader_options.h"
#include "third_party/blink/renderer/platform/loader/fetch/resource_timing_info.h"
#include "third_party/blink/renderer/platform/loader/fetch/source_keyed_cached_metadata_handler.h"
#include "third_party/blink/renderer/platform/loader/fetch/unique_identifier.h"
#include "third_party/blink/renderer/platform/loader/static_data_navigation_body_loader.h"
#include "third_party/blink/renderer/platform/mhtml/archive_resource.h"
#include "third_party/blink/renderer/platform/mhtml/mhtml_archive.h"
#include "third_party/blink/renderer/platform/network/content_security_policy_response_headers.h"
#include "third_party/blink/renderer/platform/network/encoded_form_data.h"
#include "third_party/blink/renderer/platform/network/http_names.h"
#include "third_party/blink/renderer/platform/network/http_parsers.h"
#include "third_party/blink/renderer/platform/network/network_utils.h"
#include "third_party/blink/renderer/platform/runtime_enabled_features.h"
#include "third_party/blink/renderer/platform/scheduler/public/frame_scheduler.h"
#include "third_party/blink/renderer/platform/storage/blink_storage_key.h"
#include "third_party/blink/renderer/platform/web_test_support.h"
#include "third_party/blink/renderer/platform/weborigin/kurl.h"
#include "third_party/blink/renderer/platform/weborigin/scheme_registry.h"
#include "third_party/blink/renderer/platform/weborigin/security_policy.h"
#include "third_party/blink/renderer/platform/wtf/hash_map.h"
#include "third_party/blink/renderer/platform/wtf/text/string_builder.h"
#include "third_party/blink/renderer/platform/wtf/text/string_utf8_adaptor.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"
#include "third_party/blink/renderer/platform/wtf/vector.h"

namespace blink {

const base::Feature kCacheInlineScriptCode{"CacheInlineScriptCode",
                                           base::FEATURE_DISABLED_BY_DEFAULT};

namespace {

// Controls whether caching of inline script is wired up correctly.
constexpr base::FeatureParam<bool> kCacheInlineScriptCodeFixConfiguring{
    &kCacheInlineScriptCode, "fix_configuring", false};

Vector<OriginTrialFeature> CopyInitiatorOriginTrials(
    const WebVector<int>& initiator_origin_trial_features) {
  Vector<OriginTrialFeature> result;
  for (auto feature : initiator_origin_trial_features) {
    // Convert from int to OriginTrialFeature. These values are passed between
    // blink navigations. OriginTrialFeature isn't visible outside of blink (and
    // doesn't need to be) so the values are transferred outside of blink as
    // ints and casted to OriginTrialFeature once being processed in blink.
    result.push_back(static_cast<OriginTrialFeature>(feature));
  }
  return result;
}

WebVector<int> CopyInitiatorOriginTrials(
    const Vector<OriginTrialFeature>& initiator_origin_trial_features) {
  WebVector<int> result;
  for (auto feature : initiator_origin_trial_features) {
    // Convert from OriginTrialFeature to int. These values are passed between
    // blink navigations. OriginTrialFeature isn't visible outside of blink (and
    // doesn't need to be) so the values are transferred outside of blink as
    // ints and casted to OriginTrialFeature once being processed in blink.
    result.emplace_back(static_cast<int>(feature));
  }
  return result;
}

Vector<String> CopyForceEnabledOriginTrials(
    const WebVector<WebString>& force_enabled_origin_trials) {
  Vector<String> result;
  result.ReserveInitialCapacity(
      base::checked_cast<wtf_size_t>(force_enabled_origin_trials.size()));
  for (const auto& trial : force_enabled_origin_trials)
    result.push_back(trial);
  return result;
}

WebVector<WebString> CopyForceEnabledOriginTrials(
    const Vector<String>& force_enabled_origin_trials) {
  WebVector<String> result;
  for (const auto& trial : force_enabled_origin_trials)
    result.emplace_back(trial);
  return result;
}

bool IsPagePopupRunningInWebTest(LocalFrame* frame) {
  return frame && frame->GetPage()->GetChromeClient().IsPopup() &&
         WebTestSupport::IsRunningWebTest();
}

struct SameSizeAsDocumentLoader
    : public GarbageCollected<SameSizeAsDocumentLoader>,
      public WebDocumentLoader,
      public UseCounter,
      public WebNavigationBodyLoader::Client {
  Member<MHTMLArchive> archive;
  std::unique_ptr<WebNavigationParams> params;
  std::unique_ptr<PolicyContainer> policy_container;
  absl::optional<ParsedPermissionsPolicy> isolated_app_permissions_policy;
  KURL url;
  AtomicString http_method;
  AtomicString referrer;
  scoped_refptr<EncodedFormData> http_body;
  AtomicString http_content_type;
  scoped_refptr<const SecurityOrigin> requestor_origin;
  KURL unreachable_url;
  KURL pre_redirect_url_for_failed_navigations;
  std::unique_ptr<WebNavigationBodyLoader> body_loader;
  bool grant_load_local_resources;
  absl::optional<blink::mojom::FetchCacheMode> force_fetch_cache_mode;
  FramePolicy frame_policy;
  Member<LocalFrame> frame;
  Member<HistoryItem> history_item;
  Member<DocumentParser> parser;
  Member<SubresourceFilter> subresource_filter;
  AtomicString original_referrer;
  ResourceResponse response;
  mutable WrappedResourceResponse response_wrapper;
  WebFrameLoadType load_type;
  bool is_client_redirect;
  bool replaces_current_history_item;
  bool data_received;
  bool is_error_page_for_failed_navigation;
  mojo::Remote<mojom::blink::ContentSecurityNotifier> content_security_notifier;
  scoped_refptr<SecurityOrigin> origin_to_commit;
  AtomicString origin_calculation_debug_info;
  BlinkStorageKey storage_key;
  WebNavigationType navigation_type;
  DocumentLoadTiming document_load_timing;
  base::TimeTicks time_of_last_data_received;
  std::unique_ptr<WebServiceWorkerNetworkProvider>
      service_worker_network_provider;
  DocumentPolicy::ParsedDocumentPolicy document_policy;
  bool was_blocked_by_document_policy;
  Vector<PolicyParserMessageBuffer::Message> document_policy_parsing_messages;
  ClientHintsPreferences client_hints_preferences;
  DocumentLoader::InitialScrollState initial_scroll_state;
  DocumentLoader::State state;
  int parser_blocked_count;
  bool finish_loading_when_parser_resumed;
  bool in_commit_data;
  scoped_refptr<SharedBuffer> data_buffer;
  base::UnguessableToken devtools_navigation_token;
  LoaderFreezeMode defers_loading;
  bool last_navigation_had_transient_user_activation;
  bool had_sticky_activation;
  bool is_browser_initiated;
  bool is_prerendering;
  bool is_same_origin_navigation;
  bool has_text_fragment_token;
  bool was_discarded;
  bool loading_main_document_from_mhtml_archive;
  bool loading_srcdoc;
  bool loading_url_as_empty_document;
  bool is_static_data;
  CommitReason commit_reason;
  uint64_t main_resource_identifier;
  scoped_refptr<ResourceTimingInfo> navigation_timing_info;
  bool report_timing_info_to_parent;
  WebScopedVirtualTimePauser virtual_time_pauser;
  Member<SourceKeyedCachedMetadataHandler> cached_metadata_handler;
  Member<PrefetchedSignedExchangeManager> prefetched_signed_exchange_manager;
  const KURL web_bundle_physical_url;
  const KURL web_bundle_claimed_url;
  ukm::SourceId ukm_source_id;
  UseCounterImpl use_counter;
  Dactyloscoper dactyloscoper;
  const base::TickClock* clock;
  const Vector<OriginTrialFeature> initiator_origin_trial_features;
  const Vector<String> force_enabled_origin_trials;
  bool navigation_scroll_allowed;
  bool origin_agent_cluster;
  bool origin_agent_cluster_left_as_default;
  bool is_cross_site_cross_browsing_context_group;
  WebVector<WebHistoryItem> navigation_api_back_entries;
  WebVector<WebHistoryItem> navigation_api_forward_entries;
  std::unique_ptr<CodeCacheHost> code_cache_host;
  HashMap<KURL, EarlyHintsPreloadEntry> early_hints_preloaded_resources;
  absl::optional<Vector<KURL>> ad_auction_components;
  mojom::blink::FencedFrameReportingPtr fenced_frame_reporting;
  bool waiting_for_document_loader;
  bool waiting_for_code_cache;
  std::unique_ptr<ExtraData> extra_data;
  AtomicString reduced_accept_language;
};

// Asserts size of DocumentLoader, so that whenever a new attribute is added to
// DocumentLoader, the assert will fail. When hitting this assert failure,
// please ensure that the attribute is copied correctly (if appropriate) in
// DocumentLoader::CreateWebNavigationParamsToCloneDocument().
ASSERT_SIZE(DocumentLoader, SameSizeAsDocumentLoader);

}  // namespace

DocumentLoader::DocumentLoader(
    LocalFrame* frame,
    WebNavigationType navigation_type,
    std::unique_ptr<WebNavigationParams> navigation_params,
    std::unique_ptr<PolicyContainer> policy_container,
    std::unique_ptr<ExtraData> extra_data)
    : params_(std::move(navigation_params)),
      policy_container_(std::move(policy_container)),
      initial_permissions_policy_(params_->permissions_policy_override),
      url_(params_->url),
      http_method_(static_cast<String>(params_->http_method)),
      referrer_(static_cast<String>(params_->referrer)),
      http_body_(params_->http_body),
      http_content_type_(static_cast<String>(params_->http_content_type)),
      requestor_origin_(params_->requestor_origin),
      unreachable_url_(params_->unreachable_url),
      pre_redirect_url_for_failed_navigations_(
          params_->pre_redirect_url_for_failed_navigations),
      grant_load_local_resources_(params_->grant_load_local_resources),
      force_fetch_cache_mode_(params_->force_fetch_cache_mode),
      frame_policy_(params_->frame_policy.value_or(FramePolicy())),
      frame_(frame),
      // For back/forward navigations, the browser passed a history item to use
      // at commit time in |params_|. Set it as the current history item of this
      // DocumentLoader. For other navigations, |history_item_| will be created
      // when the FrameLoader calls SetHistoryItemStateForCommit.
      history_item_(IsBackForwardLoadType(params_->frame_load_type)
                        ? params_->history_item
                        : nullptr),
      original_referrer_(referrer_),
      response_(params_->response.ToResourceResponse()),
      response_wrapper_(response_),
      load_type_(params_->frame_load_type),
      is_client_redirect_(params_->is_client_redirect),
      data_received_(false),
      is_error_page_for_failed_navigation_(
          SchemeRegistry::ShouldTreatURLSchemeAsError(
              response_.ResponseUrl().Protocol())),
      // Loading the document was blocked by the CSP check. Pretend that this
      // was an empty document instead and don't reuse the original URL. More
      // details in: https://crbug.com/622385.
      // TODO(https://crbug.com/555418) Remove this once XFO moves to the
      // browser.

      // Update |origin_to_commit_| to contain an opaque origin with precursor
      // information that is consistent with the final request URL.
      // Note: this doesn't use |url_| for the origin calculation, because
      // redirects are not yet accounted for (this happens later in
      // StartLoadingInternal).
      origin_to_commit_(params_->origin_to_commit.IsNull()
                            ? nullptr
                            : params_->origin_to_commit.Get()->IsolatedCopy()),
      storage_key_(std::move(params_->storage_key)),
      navigation_type_(navigation_type),
      document_load_timing_(*this),
      service_worker_network_provider_(
          std::move(params_->service_worker_network_provider)),
      was_blocked_by_document_policy_(false),
      state_(kNotStarted),
      in_commit_data_(false),
      data_buffer_(SharedBuffer::Create()),
      devtools_navigation_token_(params_->devtools_navigation_token),
      last_navigation_had_transient_user_activation_(
          params_->had_transient_user_activation),
      had_sticky_activation_(params_->is_user_activated),
      is_browser_initiated_(params_->is_browser_initiated),
      was_discarded_(params_->was_discarded),
      loading_srcdoc_(url_.IsAboutSrcdocURL()),
      loading_url_as_empty_document_(!params_->is_static_data &&
                                     WillLoadUrlAsEmpty(url_)),
      is_static_data_(params_->is_static_data),
      web_bundle_physical_url_(params_->web_bundle_physical_url),
      web_bundle_claimed_url_(params_->web_bundle_claimed_url),
      ukm_source_id_(params_->document_ukm_source_id),
      clock_(params_->tick_clock ? params_->tick_clock
                                 : base::DefaultTickClock::GetInstance()),
      initiator_origin_trial_features_(
          CopyInitiatorOriginTrials(params_->initiator_origin_trial_features)),
      force_enabled_origin_trials_(
          CopyForceEnabledOriginTrials(params_->force_enabled_origin_trials)),
      origin_agent_cluster_(params_->origin_agent_cluster),
      origin_agent_cluster_left_as_default_(
          params_->origin_agent_cluster_left_as_default),
      is_cross_site_cross_browsing_context_group_(
          params_->is_cross_site_cross_browsing_context_group),
      navigation_api_back_entries_(params_->navigation_api_back_entries),
      navigation_api_forward_entries_(params_->navigation_api_forward_entries),
      extra_data_(std::move(extra_data)),
      reduced_accept_language_(params_->reduced_accept_language) {
  DCHECK(frame_);

  // TODO(dgozman): we should get rid of this boolean field, and make client
  // responsible for it's own view of "replaces current item", based on the
  // frame load type.
  replaces_current_history_item_ =
      (load_type_ == WebFrameLoadType::kReplaceCurrentItem);
  if (!features::IsInitialNavigationEntryEnabled()) {
    // TODO(https://crbug.com/524208): This is needed because the browser
    // process DCHECKs if the first entry we commit in a new frame has
    // replacement set. It's unclear whether the DCHECK is right. Once we always
    // have initial NavigationEntries, we can remove this check.
    replaces_current_history_item_ &=
        (!frame_->Loader().Opener() || !url_.IsEmpty());
  }

  // See `archive_` attribute documentation.
  if (!frame_->IsMainFrame()) {
    if (auto* parent = DynamicTo<LocalFrame>(frame_->Tree().Parent()))
      archive_ = parent->Loader().GetDocumentLoader()->archive_;
  }

  // Determine if this document should have a text fragment permission token.
  // We can either generate a new one from this navigation, if it's user
  // activated, or receive one propagated from the prior navigation that didn't
  // consume its token.
  has_text_fragment_token_ = TextFragmentAnchor::GenerateNewToken(*this) ||
                             params_->has_text_fragment_token;

  document_policy_ = CreateDocumentPolicy();

  WebNavigationTimings& timings = params_->navigation_timings;
  if (!timings.input_start.is_null())
    document_load_timing_.SetInputStart(timings.input_start);
  if (timings.navigation_start.is_null()) {
    // If we don't have any navigation timings yet, it starts now.
    document_load_timing_.SetNavigationStart(clock_->NowTicks());
  } else {
    document_load_timing_.SetNavigationStart(timings.navigation_start);
    if (!timings.redirect_start.is_null()) {
      document_load_timing_.SetRedirectStart(timings.redirect_start);
      document_load_timing_.SetRedirectEnd(timings.redirect_end);
    }
    if (!timings.fetch_start.is_null()) {
      // If we started fetching, we should have started the navigation.
      DCHECK(!timings.navigation_start.is_null());
      document_load_timing_.SetFetchStart(timings.fetch_start);
    }
  }

  if (was_blocked_by_document_policy_)
    ReplaceWithEmptyDocument();

  for (const auto& resource : params_->early_hints_preloaded_resources)
    early_hints_preloaded_resources_.insert(resource, EarlyHintsPreloadEntry());

  if (IsBackForwardLoadType(params_->frame_load_type))
    DCHECK(history_item_);

  if (params_->ad_auction_components) {
    ad_auction_components_.emplace();
    for (const WebURL& url : *params_->ad_auction_components) {
      ad_auction_components_->emplace_back(KURL(url));
    }
  }

  if (params_->fenced_frame_reporting) {
    fenced_frame_reporting_ = mojom::blink::FencedFrameReporting::New();
    for (const auto& [destination, metadata] :
         params_->fenced_frame_reporting->metadata) {
      HashMap<String, KURL> data;
      for (const auto& [event_type, url] : metadata) {
        data.insert(event_type, url);
      }
      fenced_frame_reporting_->metadata.insert(destination, std::move(data));
    }
  }

  frame_->SetAncestorOrSelfHasCSPEE(params_->ancestor_or_self_has_cspee);
  frame_->Client()->DidCreateDocumentLoader(this);
}

std::unique_ptr<WebNavigationParams>
DocumentLoader::CreateWebNavigationParamsToCloneDocument() {
  // From the browser process point of view, committing the result of evaluating
  // a javascript URL or an XSLT document are all a no-op. Since we will use the
  // resulting |params| to create a clone of this DocumentLoader, many
  // attributes of DocumentLoader should be copied/inherited to the new
  // DocumentLoader's WebNavigationParams. The current heuristic is largely
  // based on copying fields that are populated in the DocumentLoader
  // constructor. Some exclusions:
  // |history_item_| is set in SetHistoryItemStateForCommit().
  // |response_| will use the newly committed response.
  // |load_type_| will use default kStandard value.
  // |replaces_current_history_item_| will be false.
  // |permissions_policy_| and |document_policy_| are set in CommitNavigation(),
  // with the sandbox flags set in CalculateSandboxFlags().
  // |is_client_redirect_| is not copied since future same-document navigations
  // will reset the state anyways.
  // |archive_| and other states might need to be copied, but we need to add
  // fields to WebNavigationParams and create WebMHTMLArchive, etc.
  // TODO(https://crbug.com/1151954): Copy |archive_| and other attributes.
  auto params = std::make_unique<WebNavigationParams>();
  LocalDOMWindow* window = frame_->DomWindow();
  params->url = window->Url();
  params->unreachable_url = unreachable_url_;
  params->referrer = referrer_;
  // All the security properties of the document must be preserved. Note that
  // sandbox flags and various policies are copied separately during commit in
  // CommitNavigation() and CalculateSandboxFlags().
  // TODO(dcheng): Is it a problem that origin_to_commit_ is copied with
  // isolated copy? This probably has observable side effects (e.g. executing a
  // javascript: URL in an about:blank frame that inherited an origin will cause
  // the origin to no longer be aliased).
  params->origin_to_commit = window->GetSecurityOrigin();
  params->storage_key = window->GetStorageKey();
  params->origin_agent_cluster = origin_agent_cluster_;
  params->origin_agent_cluster_left_as_default =
      origin_agent_cluster_left_as_default_;
  params->grant_load_local_resources = grant_load_local_resources_;
  // Various attributes that relates to the last "real" navigation that is known
  // by the browser must be carried over.
  params->http_method = http_method_;
  params->http_status_code = GetResponse().HttpStatusCode();
  params->http_body = http_body_;
  params->pre_redirect_url_for_failed_navigations =
      pre_redirect_url_for_failed_navigations_;
  params->force_fetch_cache_mode = force_fetch_cache_mode_;
  params->service_worker_network_provider =
      std::move(service_worker_network_provider_);
  params->devtools_navigation_token = devtools_navigation_token_;
  params->is_user_activated = had_sticky_activation_;
  params->had_transient_user_activation =
      last_navigation_had_transient_user_activation_;
  params->is_browser_initiated = is_browser_initiated_;
  params->was_discarded = was_discarded_;
  params->web_bundle_physical_url = web_bundle_physical_url_;
  params->web_bundle_claimed_url = web_bundle_claimed_url_;
  params->document_ukm_source_id = ukm_source_id_;
  params->is_cross_site_cross_browsing_context_group =
      is_cross_site_cross_browsing_context_group_;
  params->has_text_fragment_token = has_text_fragment_token_;
  // Origin trials must still work on the cloned document.
  params->initiator_origin_trial_features =
      CopyInitiatorOriginTrials(initiator_origin_trial_features_);
  params->force_enabled_origin_trials =
      CopyForceEnabledOriginTrials(force_enabled_origin_trials_);
  for (const auto& pair : early_hints_preloaded_resources_)
    params->early_hints_preloaded_resources.push_back(pair.key);
  if (ad_auction_components_) {
    params->ad_auction_components.emplace();
    for (const KURL& url : *ad_auction_components_) {
      params->ad_auction_components->emplace_back(KURL(url));
    }
  }
  if (fenced_frame_reporting_) {
    params->fenced_frame_reporting.emplace();
    for (const auto& [destination, metadata] :
         fenced_frame_reporting_->metadata) {
      base::flat_map<WebString, WebURL> data;
      for (const auto& [event_type, url] : metadata) {
        data.emplace(event_type, url);
      }
      params->fenced_frame_reporting->metadata.emplace(destination,
                                                       std::move(data));
    }
  }
  params->reduced_accept_language = reduced_accept_language_;
  return params;
}

FrameLoader& DocumentLoader::GetFrameLoader() const {
  DCHECK(frame_);
  return frame_->Loader();
}

LocalFrameClient& DocumentLoader::GetLocalFrameClient() const {
  DCHECK(frame_);
  LocalFrameClient* client = frame_->Client();
  // LocalFrame clears its |m_client| only after detaching all DocumentLoaders
  // (i.e. calls detachFromFrame() which clears |frame_|) owned by the
  // LocalFrame's FrameLoader. So, if |frame_| is non nullptr, |client| is
  // also non nullptr.
  DCHECK(client);
  return *client;
}

DocumentLoader::~DocumentLoader() {
  DCHECK(!frame_);
  DCHECK_EQ(state_, kSentDidFinishLoad);
}

void DocumentLoader::Trace(Visitor* visitor) const {
  visitor->Trace(archive_);
  visitor->Trace(frame_);
  visitor->Trace(history_item_);
  visitor->Trace(parser_);
  visitor->Trace(subresource_filter_);
  visitor->Trace(document_load_timing_);
  visitor->Trace(cached_metadata_handler_);
  visitor->Trace(prefetched_signed_exchange_manager_);
  visitor->Trace(use_counter_);
}

uint64_t DocumentLoader::MainResourceIdentifier() const {
  return main_resource_identifier_;
}

ResourceTimingInfo* DocumentLoader::GetNavigationTimingInfo() const {
  return navigation_timing_info_.get();
}

WebString DocumentLoader::OriginalReferrer() const {
  return original_referrer_;
}

const KURL& DocumentLoader::Url() const {
  return url_;
}

WebString DocumentLoader::HttpMethod() const {
  return http_method_;
}

const AtomicString& DocumentLoader::GetReferrer() const {
  return referrer_;
}

const SecurityOrigin* DocumentLoader::GetRequestorOrigin() const {
  return requestor_origin_.get();
}

void DocumentLoader::SetServiceWorkerNetworkProvider(
    std::unique_ptr<WebServiceWorkerNetworkProvider> provider) {
  service_worker_network_provider_ = std::move(provider);
}

void DocumentLoader::DispatchLinkHeaderPreloads(
    const ViewportDescription* viewport,
    PreloadHelper::MediaPreloadPolicy media_policy) {
  DCHECK_GE(state_, kCommitted);
  PreloadHelper::LoadLinksFromHeader(
      GetResponse().HttpHeaderField(http_names::kLink),
      GetResponse().CurrentRequestUrl(), *frame_, frame_->GetDocument(),
      PreloadHelper::kOnlyLoadResources, media_policy, viewport,
      nullptr /* alternate_resource_info */,
      nullptr /* recursive_prefetch_token */);
}

void DocumentLoader::DidChangePerformanceTiming() {
  if (frame_ && state_ >= kCommitted) {
    GetLocalFrameClient().DidChangePerformanceTiming();
  }
}

void DocumentLoader::DidObserveInputDelay(base::TimeDelta input_delay) {
  if (frame_ && state_ >= kCommitted) {
    GetLocalFrameClient().DidObserveInputDelay(input_delay);
  }
}

void DocumentLoader::DidObserveLoadingBehavior(LoadingBehaviorFlag behavior) {
  if (frame_) {
    DCHECK_GE(state_, kCommitted);
    GetLocalFrameClient().DidObserveLoadingBehavior(behavior);
  }
}

// static
WebHistoryCommitType LoadTypeToCommitType(WebFrameLoadType type) {
  switch (type) {
    case WebFrameLoadType::kStandard:
      return kWebStandardCommit;
    case WebFrameLoadType::kBackForward:
      return kWebBackForwardCommit;
    case WebFrameLoadType::kReload:
    case WebFrameLoadType::kReplaceCurrentItem:
    case WebFrameLoadType::kReloadBypassingCache:
      return kWebHistoryInertCommit;
  }
  NOTREACHED();
  return kWebHistoryInertCommit;
}

static SinglePageAppNavigationType CategorizeSinglePageAppNavigation(
    mojom::blink::SameDocumentNavigationType same_document_navigation_type,
    WebFrameLoadType frame_load_type) {
  // |SinglePageAppNavigationType| falls into this grid according to different
  // combinations of |WebFrameLoadType| and |SameDocumentNavigationType|:
  //
  //                 HistoryApi           Default
  //  kBackForward   illegal              otherFragmentNav
  // !kBackForward   sameDocBack/Forward  historyPushOrReplace
  switch (same_document_navigation_type) {
    case mojom::blink::SameDocumentNavigationType::kFragment:
      if (frame_load_type == WebFrameLoadType::kBackForward) {
        return kSPANavTypeSameDocumentBackwardOrForward;
      }
      return kSPANavTypeOtherFragmentNavigation;
    case mojom::blink::SameDocumentNavigationType::kHistoryApi:
      // It's illegal to have both kHistoryApi and
      // WebFrameLoadType::kBackForward.
      DCHECK(frame_load_type != WebFrameLoadType::kBackForward);
      return kSPANavTypeHistoryPushStateOrReplaceState;
    case mojom::blink::SameDocumentNavigationType::
        kNavigationApiTransitionWhile:
      return kSPANavTypeNavigationApiTransitionWhile;
  }
  NOTREACHED();
  return kSPANavTypeSameDocumentBackwardOrForward;
}

void DocumentLoader::RunURLAndHistoryUpdateSteps(
    const KURL& new_url,
    HistoryItem* history_item,
    mojom::blink::SameDocumentNavigationType same_document_navigation_type,
    scoped_refptr<SerializedScriptValue> data,
    WebFrameLoadType type,
    bool is_browser_initiated,
    bool is_synchronously_committed) {
  // We use the security origin of this frame since callers of this method must
  // already have performed same origin checks.
  // is_browser_initiated is false and is_synchronously_committed is true
  // because anything invoking this algorithm is a renderer-initiated navigation
  // in this process.
  UpdateForSameDocumentNavigation(
      new_url, history_item, same_document_navigation_type, std::move(data),
      type, frame_->DomWindow()->GetSecurityOrigin(), is_browser_initiated,
      is_synchronously_committed);
}

void DocumentLoader::UpdateForSameDocumentNavigation(
    const KURL& new_url,
    HistoryItem* history_item,
    mojom::blink::SameDocumentNavigationType same_document_navigation_type,
    scoped_refptr<SerializedScriptValue> data,
    WebFrameLoadType type,
    const SecurityOrigin* initiator_origin,
    bool is_browser_initiated,
    bool is_synchronously_committed) {
  DCHECK_EQ(IsBackForwardLoadType(type), !!history_item);

  SinglePageAppNavigationType single_page_app_navigation_type =
      CategorizeSinglePageAppNavigation(same_document_navigation_type, type);
  UMA_HISTOGRAM_ENUMERATION(
      "RendererScheduler.UpdateForSameDocumentNavigationCount",
      single_page_app_navigation_type, kSPANavTypeCount);

  TRACE_EVENT1("blink", "FrameLoader::updateForSameDocumentNavigation", "url",
               new_url.GetString().Ascii());

  bool same_item_sequence_number =
      history_item_ && history_item &&
      history_item_->ItemSequenceNumber() == history_item->ItemSequenceNumber();
  if (history_item)
    history_item_ = history_item;

  // Generate start and stop notifications only when loader is completed so that
  // we don't fire them for fragment redirection that happens in window.onload
  // handler. See https://bugs.webkit.org/show_bug.cgi?id=31838
  // Do not fire the notifications if the frame is concurrently navigating away
  // from the document, since a new document is already loading.
  bool was_loading = frame_->IsLoading();
  if (!was_loading)
    GetFrameLoader().Progress().ProgressStarted();

  // Update the data source's request with the new URL to fake the URL change
  frame_->GetDocument()->SetURL(new_url);

  KURL old_url = url_;
  url_ = new_url;
  replaces_current_history_item_ = type != WebFrameLoadType::kStandard;
  bool is_history_api_or_app_history_navigation =
      (same_document_navigation_type !=
       mojom::blink::SameDocumentNavigationType::kFragment);
  if (is_history_api_or_app_history_navigation) {
    // See spec:
    // https://html.spec.whatwg.org/multipage/history.html#url-and-history-update-steps
    http_method_ = http_names::kGET;
    http_body_ = nullptr;
  }

  last_navigation_had_trusted_initiator_ =
      initiator_origin ? initiator_origin->IsSameOriginWith(
                             frame_->DomWindow()->GetSecurityOrigin()) &&
                             Url().ProtocolIsInHTTPFamily()
                       : true;

  // We want to allow same-document text fragment navigations if they're coming
  // from the browser or same-origin. Do this only on a standard navigation so
  // that we don't unintentionally clear the token when we reach here from the
  // history API.
  if (type == WebFrameLoadType::kStandard ||
      same_document_navigation_type ==
          mojom::blink::SameDocumentNavigationType::kFragment) {
    has_text_fragment_token_ =
        TextFragmentAnchor::GenerateNewTokenForSameDocument(
            *this, type, same_document_navigation_type);
  }

  SetHistoryItemStateForCommit(history_item_.Get(), type,
                               is_history_api_or_app_history_navigation
                                   ? HistoryNavigationType::kHistoryApi
                                   : HistoryNavigationType::kFragment,
                               CommitReason::kRegular);
  history_item_->SetDocumentState(frame_->GetDocument()->GetDocumentState());
  if (is_history_api_or_app_history_navigation)
    history_item_->SetStateObject(std::move(data));

  WebHistoryCommitType commit_type = LoadTypeToCommitType(type);
  frame_->GetFrameScheduler()->DidCommitProvisionalLoad(
      commit_type == kWebHistoryInertCommit,
      FrameScheduler::NavigationType::kSameDocument);

  GetLocalFrameClient().DidFinishSameDocumentNavigation(
      history_item_.Get(), commit_type, is_synchronously_committed,
      same_document_navigation_type, is_client_redirect_, is_browser_initiated);
  probe::DidNavigateWithinDocument(frame_);

  // If transitionWhile() was called during this same-document navigation's
  // NavigateEvent, the navigation will finish asynchronously, so
  // don't immediately call DidStopLoading() in that case.
  bool should_send_stop_notification =
      !was_loading && same_document_navigation_type !=
                          mojom::blink::SameDocumentNavigationType::
                              kNavigationApiTransitionWhile;
  if (should_send_stop_notification)
    GetFrameLoader().Progress().ProgressCompleted();

  if (auto* navigation_api = NavigationApi::navigation(*frame_->DomWindow()))
    navigation_api->UpdateForNavigation(*history_item_, type);
  if (!frame_)
    return;

  // Aything except a history.pushState/replaceState is considered a new
  // navigation that resets whether the user has scrolled and fires popstate.
  if (same_document_navigation_type !=
      mojom::blink::SameDocumentNavigationType::kHistoryApi) {
    initial_scroll_state_.was_scrolled_by_user = false;

    // If the item sequence number didn't change, there's no need to trigger
    // popstate. It's possible to get a same-document navigation
    // to a same ISN when a history navigation targets a frame that no longer
    // exists (https://crbug.com/705550).
    if (!same_item_sequence_number) {
      scoped_refptr<SerializedScriptValue> state_object =
          history_item ? history_item->StateObject()
                       : SerializedScriptValue::NullValue();
      frame_->DomWindow()->DispatchPopstateEvent(std::move(state_object));
    }
  }
}

const KURL& DocumentLoader::UrlForHistory() const {
  return UnreachableURL().IsEmpty() ? Url() : UnreachableURL();
}

void DocumentLoader::DidOpenDocumentInputStream(const KURL& url) {
  url_ = url;
  // Let the browser know that we have done a document.open().
  GetLocalFrameClient().DispatchDidOpenDocumentInputStream(url_);
}

void DocumentLoader::SetHistoryItemStateForCommit(
    HistoryItem* old_item,
    WebFrameLoadType load_type,
    HistoryNavigationType navigation_type,
    CommitReason commit_reason) {
  if (!history_item_ || !IsBackForwardLoadType(load_type))
    history_item_ = MakeGarbageCollected<HistoryItem>();

  history_item_->SetURL(UrlForHistory());
  history_item_->SetReferrer(referrer_.GetString());
  if (EqualIgnoringASCIICase(http_method_, "POST")) {
    // FIXME: Eventually we have to make this smart enough to handle the case
    // where we have a stream for the body to handle the "data interspersed with
    // files" feature.
    history_item_->SetFormData(http_body_);
    history_item_->SetFormContentType(http_content_type_);
  } else {
    history_item_->SetFormData(nullptr);
    history_item_->SetFormContentType(g_null_atom);
  }

  // Don't propagate state from the old item to the new item if there isn't an
  // old item (obviously), or if this is a back/forward navigation, since we
  // explicitly want to restore the state we just committed.
  if (!old_item || IsBackForwardLoadType(load_type))
    return;

  // The navigation API key corresponds to a "slot" in the back/forward list,
  // and should be shared for all replacing navigations so long as the
  // navigation isn't cross-origin.
  WebHistoryCommitType history_commit_type = LoadTypeToCommitType(load_type);
  if (history_commit_type == kWebHistoryInertCommit &&
      SecurityOrigin::Create(old_item->Url())
          ->CanAccess(SecurityOrigin::Create(history_item_->Url()).get())) {
    history_item_->SetNavigationApiKey(old_item->GetNavigationApiKey());
  }

  // The navigation API id corresponds to a "session history entry", and so
  // should be carried over across reloads.
  if (IsReloadLoadType(load_type))
    history_item_->SetNavigationApiId(old_item->GetNavigationApiId());

  // The navigation API's state is stickier than the legacy History state. It
  // always propagates by default to a same-document navigation.
  if (navigation_type == HistoryNavigationType::kFragment ||
      IsReloadLoadType(load_type)) {
    history_item_->SetNavigationApiState(old_item->GetNavigationApiState());
  }

  // Don't propagate state from the old item if this is a different-document
  // navigation, unless the before and after pages are logically related. This
  // means they have the same url (ignoring fragment) and the new item was
  // loaded via reload or client redirect.
  if (navigation_type == HistoryNavigationType::kDifferentDocument &&
      (history_commit_type != kWebHistoryInertCommit ||
       !EqualIgnoringFragmentIdentifier(old_item->Url(), history_item_->Url())))
    return;
  history_item_->SetDocumentSequenceNumber(old_item->DocumentSequenceNumber());

  history_item_->CopyViewStateFrom(old_item);
  history_item_->SetScrollRestorationType(old_item->ScrollRestorationType());

  // The item sequence number determines whether items are "the same", such
  // back/forward navigation between items with the same item sequence number is
  // a no-op. Only treat this as identical if the navigation did not create a
  // back/forward entry and the url is identical or it was loaded via
  // history.replaceState().
  if (history_commit_type == kWebHistoryInertCommit &&
      (navigation_type == HistoryNavigationType::kHistoryApi ||
       old_item->Url() == history_item_->Url())) {
    history_item_->SetStateObject(old_item->StateObject());
    history_item_->SetItemSequenceNumber(old_item->ItemSequenceNumber());
  }
}

void DocumentLoader::BodyCodeCacheReceived(mojo_base::BigBuffer data) {
  if (cached_metadata_handler_) {
    cached_metadata_handler_->SetSerializedCachedMetadata(std::move(data));
  }

  if (base::FeatureList::IsEnabled(features::kEarlyBodyLoad)) {
    waiting_for_code_cache_ = false;
    if (!waiting_for_document_loader_ && parser_)
      parser_->CommitPreloadedData();
  }
}

void DocumentLoader::BodyDataReceived(base::span<const char> data) {
  TRACE_EVENT0("loading", "DocumentLoader::BodyDataReceived");
  GetFrameLoader().Progress().IncrementProgress(main_resource_identifier_,
                                                data.size());
  probe::DidReceiveData(probe::ToCoreProbeSink(GetFrame()),
                        main_resource_identifier_, this, data.data(),
                        data.size());

  TRACE_EVENT1("loading", "DocumentLoader::HandleData", "length", data.size());

  DCHECK(data.data());
  DCHECK(data.size());
  DCHECK(!frame_->GetPage()->Paused());
  time_of_last_data_received_ = clock_->NowTicks();

  if (loading_main_document_from_mhtml_archive_) {
    // 1) Ftp directory listings accumulate data buffer and transform it later
    //    to the actual document content.
    // 2) Mhtml archives accumulate data buffer and parse it as mhtml later
    //    to retrieve the actual document content.
    data_buffer_->Append(data.data(), data.size());
    return;
  }

  ProcessDataBuffer(data.data(), data.size());
}

void DocumentLoader::BodyLoadingFinished(
    base::TimeTicks completion_time,
    int64_t total_encoded_data_length,
    int64_t total_encoded_body_length,
    int64_t total_decoded_body_length,
    bool should_report_corb_blocking,
    const absl::optional<WebURLError>& error) {
  TRACE_EVENT0("loading", "DocumentLoader::BodyLoadingFinished");
  response_.SetEncodedDataLength(total_encoded_data_length);
  response_.SetEncodedBodyLength(total_encoded_body_length);
  response_.SetDecodedBodyLength(total_decoded_body_length);

  if (!error) {
    GetFrameLoader().Progress().CompleteProgress(main_resource_identifier_);
    probe::DidFinishLoading(
        probe::ToCoreProbeSink(GetFrame()), main_resource_identifier_, this,
        completion_time, total_encoded_data_length, total_decoded_body_length,
        should_report_corb_blocking);
    if (response_.ShouldPopulateResourceTiming() ||
        is_error_page_for_failed_navigation_) {
      // The response is being copied here to pass the Encoded and Decoded
      // sizes.
      // TODO(yoav): copy the sizes info directly.
      navigation_timing_info_->SetFinalResponse(response_);
      if (report_timing_info_to_parent_) {
        navigation_timing_info_->SetLoadResponseEnd(completion_time);
        if (state_ >= kCommitted) {
          // Note that we currently lose timing info for empty documents,
          // which will be fixed with synchronous commit.
          // Main resource timing information is reported through the owner
          // to be passed to the parent frame, if appropriate.

          frame_->Owner()->AddResourceTiming(*navigation_timing_info_);
        }
        frame_->SetShouldSendResourceTimingInfoToParent(false);
      }
    }
    FinishedLoading(completion_time);
    return;
  }

  ResourceError resource_error(*error);
  if (network_utils::IsCertificateTransparencyRequiredError(
          resource_error.ErrorCode())) {
    CountUse(WebFeature::kCertificateTransparencyRequiredErrorOnResourceLoad);
  }
  GetFrameLoader().Progress().CompleteProgress(main_resource_identifier_);
  probe::DidFailLoading(probe::ToCoreProbeSink(GetFrame()),
                        main_resource_identifier_, this, resource_error,
                        frame_->GetDevToolsFrameToken());
  GetFrame()->Console().DidFailLoading(this, main_resource_identifier_,
                                       resource_error);
  LoadFailed(resource_error);
}

void DocumentLoader::LoadFailed(const ResourceError& error) {
  TRACE_EVENT1("navigation,rail", "DocumentLoader::LoadFailed", "error",
               error.ErrorCode());
  body_loader_.reset();
  virtual_time_pauser_.UnpauseVirtualTime();

  // `LoadFailed()` should never be called for a navigation failure in a frame
  // owned by <object>. Browser-side navigation must handle these (whether
  // network errors, blocked by CSP/XFO, or otherwise) and never delegate to the
  // renderer.
  //
  // `LoadFailed()` *can* be called for a frame owned by <object> if the
  // navigation body load is cancelled, e.g.:
  // - `StartLoadingResponse()` calls `StopLoading()` when loading a
  //   `MediaDocument`.
  // - `LocalFrame::Detach()` calls `StopLoading()`.
  // - `window.stop()` calls `StopAllLoaders()` which calls `StopLoading()`.
  DCHECK(!IsA<HTMLObjectElement>(frame_->Owner()) || error.IsCancellation());

  WebHistoryCommitType history_commit_type = LoadTypeToCommitType(load_type_);
  DCHECK_EQ(kCommitted, state_);
  if (frame_->GetDocument()->Parser())
    frame_->GetDocument()->Parser()->StopParsing();
  state_ = kSentDidFinishLoad;
  GetLocalFrameClient().DispatchDidFailLoad(error, history_commit_type);
  GetFrameLoader().DidFinishNavigation(
      FrameLoader::NavigationFinishState::kFailure);
  DCHECK_EQ(kSentDidFinishLoad, state_);
  params_ = nullptr;
}

void DocumentLoader::FinishedLoading(base::TimeTicks finish_time) {
  body_loader_.reset();
  virtual_time_pauser_.UnpauseVirtualTime();

  DCHECK(commit_reason_ == CommitReason::kInitialization ||
         !frame_->GetPage()->Paused() ||
         MainThreadDebugger::Instance()->IsPaused());

  if (loading_main_document_from_mhtml_archive_ && state_ < kCommitted) {
    // The browser process should block any navigation to an MHTML archive
    // inside iframes. See NavigationRequest::OnResponseStarted().
    CHECK(frame_->IsMainFrame());

    archive_ = MHTMLArchive::Create(url_, std::move(data_buffer_));
  }

  // We should not call FinishedLoading before committing navigation,
  // except for the mhtml case. When loading an MHTML archive, the whole archive
  // has to be validated before committing the navigation. The validation
  // process loads the entire body of the archive, which will move the state to
  // FinishedLoading.
  if (!loading_main_document_from_mhtml_archive_)
    DCHECK_GE(state_, kCommitted);

  base::TimeTicks response_end_time = finish_time;
  if (response_end_time.is_null())
    response_end_time = time_of_last_data_received_;
  if (response_end_time.is_null())
    response_end_time = clock_->NowTicks();
  GetTiming().SetResponseEnd(response_end_time);

  if (!frame_)
    return;

  if (parser_) {
    if (parser_blocked_count_) {
      finish_loading_when_parser_resumed_ = true;
    } else {
      parser_->Finish();
      parser_.Clear();
    }
  }
}

void DocumentLoader::HandleRedirect(
    WebNavigationParams::RedirectInfo& redirect) {
  ResourceResponse redirect_response =
      redirect.redirect_response.ToResourceResponse();
  const KURL& url_before_redirect = redirect_response.CurrentRequestUrl();
  url_ = redirect.new_url;
  const KURL& url_after_redirect = url_;

  // Browser process should have already checked that redirecting url is
  // allowed to display content from the target origin.
  // When the referrer page is in an unsigned Web Bundle file in local
  // (eg: file:///tmp/a.wbn), Chrome internally redirects the navigation to the
  // page (eg: https://example.com/page.html) inside the Web Bundle file
  // to the file's URL (file:///tmp/a.wbn?https://example.com/page.html). In
  // this case, CanDisplay() returns false, and web_bundle_claimed_url must not
  // be null.
  CHECK(SecurityOrigin::Create(url_before_redirect)
            ->CanDisplay(url_after_redirect) ||
        !params_->web_bundle_claimed_url.IsNull());

  // Update the HTTP method of this document to the method used by the redirect.
  AtomicString new_http_method = redirect.new_http_method;
  if (http_method_ != new_http_method) {
    http_body_ = nullptr;
    http_content_type_ = g_null_atom;
    http_method_ = new_http_method;
  }

  referrer_ = redirect.new_referrer;

  probe::WillSendNavigationRequest(
      probe::ToCoreProbeSink(GetFrame()), main_resource_identifier_, this,
      url_after_redirect, http_method_, http_body_.get());

  navigation_timing_info_->AddRedirect(redirect_response, url_after_redirect);

  DCHECK(!GetTiming().FetchStart().is_null());
  GetTiming().AddRedirect(url_before_redirect, url_after_redirect);
}

bool DocumentLoader::ShouldReportTimingInfoToParent() {
  DCHECK(frame_);
  // <iframe>s should report the initial navigation requested by the parent
  // document, but not subsequent navigations.
  if (!frame_->Owner())
    return false;
  // Note that this can be racy since this information is forwarded over IPC
  // when crossing process boundaries.
  if (!frame_->should_send_resource_timing_info_to_parent())
    return false;
  // Do not report iframe navigation that restored from history, since its
  // location may have been changed after initial navigation,
  if (load_type_ == WebFrameLoadType::kBackForward) {
    // ...and do not report subsequent navigations in the iframe too.
    frame_->SetShouldSendResourceTimingInfoToParent(false);
    return false;
  }
  return true;
}

void DocumentLoader::ConsoleError(const String& message) {
  auto* console_message = MakeGarbageCollected<ConsoleMessage>(
      mojom::ConsoleMessageSource::kSecurity,
      mojom::ConsoleMessageLevel::kError, message,
      response_.CurrentRequestUrl(), this, MainResourceIdentifier());
  frame_->DomWindow()->AddConsoleMessage(console_message);
}

void DocumentLoader::ReplaceWithEmptyDocument() {
  DCHECK(params_);
  KURL blocked_url = SecurityOrigin::UrlWithUniqueOpaqueOrigin();
  url_ = blocked_url;
  params_->url = blocked_url;
  WebNavigationParams::FillStaticResponse(params_.get(), "text/html", "UTF-8",
                                          "");
}

DocumentPolicy::ParsedDocumentPolicy DocumentLoader::CreateDocumentPolicy() {
  // For URLs referring to local content to parent frame, they have no way to
  // specify the document policy they use. If the parent frame requires a
  // document policy on them, use the required policy as effective policy.
  if (url_.IsEmpty() || url_.ProtocolIsAbout() || url_.ProtocolIsData() ||
      url_.ProtocolIs("blob") || url_.ProtocolIs("filesystem"))
    return {frame_policy_.required_document_policy, {} /* endpoint_map */};

  PolicyParserMessageBuffer header_logger("Document-Policy HTTP header: ");
  PolicyParserMessageBuffer require_header_logger(
      "Require-Document-Policy HTTP header: ");

  // Filtering out features that are disabled by origin trial is done
  // in SecurityContextInit when origin trial context is available.
  auto parsed_policy =
      DocumentPolicyParser::Parse(
          response_.HttpHeaderField(http_names::kDocumentPolicy), header_logger)
          .value_or(DocumentPolicy::ParsedDocumentPolicy{});

  // |parsed_policy| can have policies that are disabled by origin trial,
  // but |frame_policy_.required_document_policy| cannot.
  // It is safe to call |IsPolicyCompatible| as long as required policy is
  // checked against origin trial.
  if (!DocumentPolicy::IsPolicyCompatible(
          frame_policy_.required_document_policy,
          parsed_policy.feature_state)) {
    was_blocked_by_document_policy_ = true;
    // When header policy is less strict than required policy, use required
    // policy to initialize document policy for the document.
    parsed_policy = {frame_policy_.required_document_policy,
                     {} /* endpoint_map */};
  }

  // Initialize required document policy for subtree.
  //
  // If the document is blocked by document policy, there won't be content
  // in the sub-frametree, thus no need to initialize required_policy for
  // subtree.
  if (!was_blocked_by_document_policy_) {
    // Require-Document-Policy header only affects subtree of current document,
    // but not the current document.
    const DocumentPolicyFeatureState header_required_policy =
        DocumentPolicyParser::Parse(
            response_.HttpHeaderField(http_names::kRequireDocumentPolicy),
            require_header_logger)
            .value_or(DocumentPolicy::ParsedDocumentPolicy{})
            .feature_state;
    frame_->SetRequiredDocumentPolicy(DocumentPolicy::MergeFeatureState(
        header_required_policy, frame_policy_.required_document_policy));
  }

  document_policy_parsing_messages_.AppendVector(header_logger.GetMessages());
  document_policy_parsing_messages_.AppendVector(
      require_header_logger.GetMessages());

  return parsed_policy;
}

void DocumentLoader::HandleResponse() {
  DCHECK(frame_);

  if (response_.IsHTTP() && !cors::IsOkStatus(response_.HttpStatusCode())) {
    DCHECK(!IsA<HTMLObjectElement>(frame_->Owner()));
  }
}

void DocumentLoader::CommitData(const char* bytes, size_t length) {
  TRACE_EVENT1("loading", "DocumentLoader::CommitData", "length", length);

  // This can happen if document.close() is called by an event handler while
  // there's still pending incoming data.
  // TODO(dgozman): we should stop body loader when stopping the parser to
  // avoid unnecessary work. This may happen, for example, when we abort current
  // committed document which is still loading when initiating a new navigation.
  if (!frame_ || !frame_->GetDocument()->Parsing())
    return;

  base::AutoReset<bool> reentrancy_protector(&in_commit_data_, true);
  if (length)
    data_received_ = true;
  parser_->AppendBytes(bytes, length);
}

mojom::CommitResult DocumentLoader::CommitSameDocumentNavigation(
    const KURL& url,
    WebFrameLoadType frame_load_type,
    HistoryItem* history_item,
    ClientRedirectPolicy client_redirect_policy,
    bool has_transient_user_activation,
    const SecurityOrigin* initiator_origin,
    bool is_synchronously_committed,
    mojom::blink::TriggeringEventInfo triggering_event_info,
    bool is_browser_initiated) {
  DCHECK(!IsReloadLoadType(frame_load_type));
  DCHECK(frame_->GetDocument());
  DCHECK(!is_browser_initiated || !is_synchronously_committed);
  CHECK(frame_->IsNavigationAllowed());

  if (Page* page = frame_->GetPage())
    page->HistoryNavigationVirtualTimePauser().UnpauseVirtualTime();

  if (frame_->GetDocument()->IsFrameSet()) {
    // Navigations in a frameset are always cross-document. Renderer-initiated
    // navigations in a frameset will be deferred to the browser, and all
    // renderer-initiated navigations are treated as cross-document. So this one
    // must have been browser-initiated, where it was not aware that the
    // document is a frameset. In that case we just restart the navigation,
    // making it cross-document. This gives a consistent outcome for all
    // navigations in a frameset.
    return mojom::blink::CommitResult::RestartCrossDocument;
  }

  if (!IsBackForwardLoadType(frame_load_type)) {
    // For the browser to send a same-document navigation, it will always have a
    // fragment. When no fragment is present, the browser loads a new document.
    CHECK(url.HasFragmentIdentifier());
    if (!EqualIgnoringFragmentIdentifier(frame_->GetDocument()->Url(), url)) {
      // A race condition has occurred! The renderer has changed the current
      // document's URL through history.pushState(). This change was performed
      // as a synchronous same-document navigation in the renderer process,
      // though the URL of that document is changed as a result. The browser
      // will hear about this and update its current URL too, but there's a time
      // window before it hears about it. During that time, it may try to
      // perform a same-document navigation based on the old URL. That would
      // arrive here. There are effectively 2 incompatible navigations in flight
      // at the moment, and the history.pushState() one was already performed.
      // We will reorder the incoming navigation from the browser to be
      // performed after the history.pushState() by bouncing it back through the
      // browser. The way we do that is by sending RestartCrossDocument, which
      // is not strictly what we want. We just want the browser to restart the
      // navigation. However, since the document address has changed, the
      // restarted navigation will probably be cross-document, and this prevents
      // a resulting same-document navigation from getting bounced and restarted
      // yet again by a renderer performing another history.pushState(). See
      // https://crbug.com/1209772.
      return mojom::blink::CommitResult::RestartCrossDocument;
    }
  }

  mojom::blink::SameDocumentNavigationType same_document_navigation_type =
      mojom::blink::SameDocumentNavigationType::kFragment;
  if (auto* navigation_api = NavigationApi::navigation(*frame_->DomWindow())) {
    auto* params = MakeGarbageCollected<NavigateEventDispatchParams>(
        url, NavigateEventType::kFragment, frame_load_type);
    if (is_browser_initiated) {
      params->involvement = UserNavigationInvolvement::kBrowserUI;
    } else if (triggering_event_info ==
               mojom::blink::TriggeringEventInfo::kFromTrustedEvent) {
      params->involvement = UserNavigationInvolvement::kActivation;
    }
    params->destination_item = history_item;
    params->is_browser_initiated = is_browser_initiated;
    params->is_synchronously_committed_same_document =
        is_synchronously_committed;
    auto dispatch_result = navigation_api->DispatchNavigateEvent(params);
    if (dispatch_result == NavigationApi::DispatchResult::kAbort)
      return mojom::blink::CommitResult::Aborted;
    if (dispatch_result == NavigationApi::DispatchResult::kTransitionWhile)
      return mojom::blink::CommitResult::Ok;
  }

  // If the requesting document is cross-origin, perform the navigation
  // asynchronously to minimize the navigator's ability to execute timing
  // attacks. If |is_synchronously_committed| is false, the navigation is
  // already asynchronous since it's coming from the browser so there's no need
  // to post it again.
  if (is_synchronously_committed && initiator_origin &&
      !initiator_origin->CanAccess(frame_->DomWindow()->GetSecurityOrigin())) {
    frame_->GetTaskRunner(TaskType::kInternalLoading)
        ->PostTask(
            FROM_HERE,
            WTF::Bind(&DocumentLoader::CommitSameDocumentNavigationInternal,
                      WrapWeakPersistent(this), url, frame_load_type,
                      WrapPersistent(history_item),
                      same_document_navigation_type, client_redirect_policy,
                      has_transient_user_activation,
                      WTF::RetainedRef(initiator_origin), is_browser_initiated,
                      is_synchronously_committed, triggering_event_info));
  } else {
    // Treat a navigation to the same url as replacing only if it did not
    // originate from a cross-origin iframe. If |is_synchronously_committed| is
    // false, the browser process already enforced this policy.
    if (is_synchronously_committed && !IsBackForwardLoadType(frame_load_type) &&
        history_item_ && url == history_item_->Url()) {
      frame_load_type = WebFrameLoadType::kReplaceCurrentItem;
    }
    CommitSameDocumentNavigationInternal(
        url, frame_load_type, history_item, same_document_navigation_type,
        client_redirect_policy, has_transient_user_activation, initiator_origin,
        is_browser_initiated, is_synchronously_committed,
        triggering_event_info);
  }
  return mojom::CommitResult::Ok;
}

void DocumentLoader::CommitSameDocumentNavigationInternal(
    const KURL& url,
    WebFrameLoadType frame_load_type,
    HistoryItem* history_item,
    mojom::blink::SameDocumentNavigationType same_document_navigation_type,
    ClientRedirectPolicy client_redirect,
    bool has_transient_user_activation,
    const SecurityOrigin* initiator_origin,
    bool is_browser_initiated,
    bool is_synchronously_committed,
    mojom::blink::TriggeringEventInfo triggering_event_info) {
  // If this function was scheduled to run asynchronously, this DocumentLoader
  // might have been detached before the task ran.
  if (!frame_)
    return;

  if (!IsBackForwardLoadType(frame_load_type)) {
    SetNavigationType(triggering_event_info !=
                              mojom::blink::TriggeringEventInfo::kNotFromEvent
                          ? kWebNavigationTypeLinkClicked
                          : kWebNavigationTypeOther);
  }

  // If we have a client navigation for a different document, a fragment
  // scroll should cancel it.
  // Note: see fragment-change-does-not-cancel-pending-navigation, where
  // this does not actually happen.
  GetFrameLoader().DidFinishNavigation(
      FrameLoader::NavigationFinishState::kSuccess);

  // GetFrameLoader().DidFinishNavigation can lead to DetachFromFrame so need
  // to check again if frame_ is null.
  if (!frame_ || !frame_->GetPage())
    return;
  GetFrameLoader().SaveScrollState();

  KURL old_url = frame_->GetDocument()->Url();
  bool hash_change = EqualIgnoringFragmentIdentifier(url, old_url) &&
                     url.FragmentIdentifier() != old_url.FragmentIdentifier();
  if (hash_change) {
    // If we were in the autoscroll/middleClickAutoscroll mode we want to stop
    // it before following the link to the anchor
    frame_->GetEventHandler().StopAutoscroll();
    frame_->DomWindow()->EnqueueHashchangeEvent(old_url, url);
  }
  is_client_redirect_ =
      client_redirect == ClientRedirectPolicy::kClientRedirect;

  last_navigation_had_transient_user_activation_ =
      has_transient_user_activation;

  // Events fired in UpdateForSameDocumentNavigation() might change view state,
  // so stash for later restore.
  absl::optional<HistoryItem::ViewState> view_state;
  mojom::blink::ScrollRestorationType scroll_restoration_type =
      mojom::blink::ScrollRestorationType::kAuto;
  if (history_item) {
    view_state = history_item->GetViewState();
    scroll_restoration_type = history_item->ScrollRestorationType();
  }

  UpdateForSameDocumentNavigation(
      url, history_item, same_document_navigation_type, nullptr,
      frame_load_type, initiator_origin, is_browser_initiated,
      is_synchronously_committed);
  if (!frame_)
    return;

  if (!frame_->GetDocument()->LoadEventStillNeeded() && frame_->Owner() &&
      initiator_origin &&
      !initiator_origin->CanAccess(frame_->DomWindow()->GetSecurityOrigin()) &&
      frame_->Tree().Parent()->GetSecurityContext()->GetSecurityOrigin()) {
    // If this same-document navigation was initiated by a cross-origin iframe
    // and is cross-origin to its parent, fire onload on the owner iframe.
    // Normally, the owner iframe's onload fires if and only if the window's
    // onload fires (i.e., when a navigation to a different document completes).
    // However, a cross-origin initiator can use the presence or absence of a
    // load event to detect whether the navigation was same- or cross-document,
    // and can therefore try to guess the url of a cross-origin iframe. Fire the
    // iframe's onload to prevent this technique. https://crbug.com/1248444
    frame_->Owner()->DispatchLoad();
  }

  GetFrameLoader().ProcessScrollForSameDocumentNavigation(
      url, frame_load_type, view_state, scroll_restoration_type);
}

void DocumentLoader::ProcessDataBuffer(const char* bytes, size_t length) {
  DCHECK_GE(state_, kCommitted);
  if (parser_blocked_count_ || in_commit_data_) {
    // 1) If parser is blocked, we buffer data and process it upon resume.
    // 2) If this function is reentered, we defer processing of the additional
    //    data to the top-level invocation. Reentrant calls can occur because
    //    of web platform (mis-)features that require running a nested run loop:
    //    - alert(), confirm(), prompt()
    //    - Detach of plugin elements.
    //    - Synchronous XMLHTTPRequest
    if (bytes)
      data_buffer_->Append(bytes, length);
    return;
  }

  if (bytes)
    CommitData(bytes, length);
  // Process data received in reentrant invocations. Note that the invocations
  // of CommitData() may queue more data in reentrant invocations, so iterate
  // until it's empty.
  for (const auto& span : *data_buffer_)
    CommitData(span.data(), span.size());
  // All data has been consumed, so flush the buffer.
  data_buffer_->Clear();
}

void DocumentLoader::StopLoading() {
  if (frame_ && GetFrameLoader().GetDocumentLoader() == this)
    frame_->GetDocument()->Fetcher()->StopFetching();
  body_loader_.reset();
  virtual_time_pauser_.UnpauseVirtualTime();
  if (!SentDidFinishLoad())
    LoadFailed(ResourceError::CancelledError(Url()));
}

void DocumentLoader::SetDefersLoading(LoaderFreezeMode mode) {
  freeze_mode_ = mode;
  if (body_loader_)
    body_loader_->SetDefersLoading(mode);
}

void DocumentLoader::DetachFromFrame(bool flush_microtask_queue) {
  DCHECK(frame_);
  StopLoading();
  if (flush_microtask_queue) {
    // Flush microtask queue so that they all run on pre-navigation context.
    // TODO(dcheng): This is a temporary hack that should be removed. This is
    // only here because it's currently not possible to drop the microtasks
    // queued for a Document when the Document is navigated away; instead, the
    // entire microtask queue needs to be flushed. Unfortunately, running the
    // microtasks any later results in violating internal invariants, since
    // Blink does not expect the DocumentLoader for a not-yet-detached Document
    // to be null. It is also not possible to flush microtasks any earlier,
    // since flushing microtasks can only be done after any other JS (which can
    // queue additional microtasks) has run. Once it is possible to associate
    // microtasks with a v8::Context, remove this hack.
    Microtask::PerformCheckpoint(V8PerIsolateData::MainThreadIsolate());
  }
  ScriptForbiddenScope forbid_scripts;

  // If that load cancellation triggered another detach, leave.
  // (fast/frames/detach-frame-nested-no-crash.html is an example of this.)
  if (!frame_)
    return;

  extra_data_.reset();
  service_worker_network_provider_ = nullptr;
  WeakIdentifierMap<DocumentLoader>::NotifyObjectDestroyed(this);
  frame_ = nullptr;
}

const KURL& DocumentLoader::UnreachableURL() const {
  return unreachable_url_;
}

const absl::optional<blink::mojom::FetchCacheMode>&
DocumentLoader::ForceFetchCacheMode() const {
  return force_fetch_cache_mode_;
}

bool DocumentLoader::WillLoadUrlAsEmpty(const KURL& url) {
  if (url.IsEmpty())
    return true;
  // Usually, we load urls with about: scheme as empty.
  // However, about:srcdoc is only used as a marker for non-existent
  // url of iframes with srcdoc attribute, which have possibly non-empty
  // content of the srcdoc attribute used as document's html.
  if (url.IsAboutSrcdocURL())
    return false;
  return SchemeRegistry::ShouldLoadURLSchemeAsEmptyDocument(url.Protocol());
}

bool WebDocumentLoader::WillLoadUrlAsEmpty(const WebURL& url) {
  return DocumentLoader::WillLoadUrlAsEmpty(url);
}

void DocumentLoader::InitializeEmptyResponse() {
  response_ = ResourceResponse(url_);
  response_.SetMimeType("text/html");
  response_.SetTextEncodingName("utf-8");
}

void DocumentLoader::StartLoading() {
  probe::LifecycleEvent(frame_, this, "init",
                        base::TimeTicks::Now().since_origin().InSecondsF());
  StartLoadingInternal();
  params_ = nullptr;
}

void DocumentLoader::StartLoadingInternal() {
  GetTiming().MarkNavigationStart();
  DCHECK_EQ(state_, kNotStarted);
  DCHECK(params_);
  state_ = kProvisional;

  if (url_.IsEmpty() && commit_reason_ != CommitReason::kInitialization)
    url_ = BlankURL();

  if (loading_url_as_empty_document_) {
    InitializeEmptyResponse();
    return;
  }

  body_loader_ = std::move(params_->body_loader);
  DCHECK(body_loader_);
  DCHECK(!GetTiming().NavigationStart().is_null());
  // The fetch has already started in the browser,
  // so we don't MarkFetchStart here.
  main_resource_identifier_ = CreateUniqueIdentifier();

  navigation_timing_info_ = ResourceTimingInfo::Create(
      fetch_initiator_type_names::kDocument, GetTiming().NavigationStart(),
      mojom::blink::RequestContextType::IFRAME,
      network::mojom::RequestDestination::kIframe,
      network::mojom::RequestMode::kNavigate);
  navigation_timing_info_->SetInitialURL(url_);
  report_timing_info_to_parent_ = ShouldReportTimingInfoToParent();

  virtual_time_pauser_ =
      frame_->GetFrameScheduler()->CreateWebScopedVirtualTimePauser(
          url_.GetString(),
          WebScopedVirtualTimePauser::VirtualTaskDuration::kNonInstant);
  virtual_time_pauser_.PauseVirtualTime();

  // Many parties are interested in resource loading, so we will notify
  // them through various DispatchXXX methods on FrameFetchContext.

  GetFrameLoader().Progress().WillStartLoading(main_resource_identifier_,
                                               ResourceLoadPriority::kVeryHigh);
  probe::WillSendNavigationRequest(probe::ToCoreProbeSink(GetFrame()),
                                   main_resource_identifier_, this, url_,
                                   http_method_, http_body_.get());

  for (WebNavigationParams::RedirectInfo& redirect : params_->redirects) {
    HandleRedirect(redirect);
  }

  ApplyClientHintsConfig(params_->enabled_client_hints);
  PreloadHelper::LoadLinksFromHeader(
      response_.HttpHeaderField(http_names::kLink),
      response_.CurrentRequestUrl(), *GetFrame(), nullptr,
      PreloadHelper::kDoNotLoadResources, PreloadHelper::kLoadAll,
      nullptr /* viewport_description */, nullptr /* alternate_resource_info */,
      nullptr /* recursive_prefetch_token */);
  GetFrameLoader().Progress().IncrementProgress(main_resource_identifier_,
                                                response_);
  probe::DidReceiveResourceResponse(probe::ToCoreProbeSink(GetFrame()),
                                    main_resource_identifier_, this, response_,
                                    nullptr /* resource */);

  HandleResponse();

  loading_main_document_from_mhtml_archive_ =
      EqualIgnoringASCIICase("multipart/related", response_.MimeType()) ||
      EqualIgnoringASCIICase("message/rfc822", response_.MimeType());
  if (loading_main_document_from_mhtml_archive_) {
    // The browser process should block any navigation to an MHTML archive
    // inside iframes. See NavigationRequest::OnResponseStarted().
    CHECK(frame_->IsMainFrame());

    // To commit an mhtml archive synchronously we have to load the whole body
    // synchronously and parse it, and it's already loaded in a buffer usually.
    // This means we should not defer, and we'll finish loading synchronously
    // from StartLoadingBody().
    body_loader_->StartLoadingBody(this, nullptr /*code_cache_host*/);
    return;
  }

  InitializePrefetchedSignedExchangeManager();

  body_loader_->SetDefersLoading(freeze_mode_);
}

void DocumentLoader::StartLoadingResponse() {
  // TODO(dcheng): Clean up the null checks in this helper.
  if (!frame_)
    return;

  CHECK_GE(state_, kCommitted);

  CreateParserPostCommit();

  // The main document from an MHTML archive is not loaded from its HTTP
  // response, but from the main resource within the archive (in the response).
  if (loading_main_document_from_mhtml_archive_) {
    // If the `archive_` contains a main resource, load the main document from
    // the archive, else it will remain empty.
    if (ArchiveResource* resource = archive_->MainResource()) {
      DCHECK_EQ(archive_->LoadResult(),
                mojom::blink::MHTMLLoadResult::kSuccess);

      data_buffer_ = resource->Data();
      ProcessDataBuffer();
      FinishedLoading(base::TimeTicks::Now());
      return;
    }

    // Log attempts loading a malformed archive.
    DCHECK_NE(archive_->LoadResult(), mojom::blink::MHTMLLoadResult::kSuccess);
    frame_->Console().AddMessage(MakeGarbageCollected<ConsoleMessage>(
        mojom::blink::ConsoleMessageSource::kJavaScript,
        mojom::blink::ConsoleMessageLevel::kError,
        "Malformed multipart archive: " + url_.GetString()));
    FinishedLoading(base::TimeTicks::Now());
    return;
  }

  // Empty documents are empty by definition. Nothing to load.
  if (loading_url_as_empty_document_) {
    FinishedLoading(base::TimeTicks::Now());
    return;
  }

  // Implements "Then, the user agent must act as if it had stopped parsing."
  // from https://html.spec.whatwg.org/C/browsing-the-web.html#read-media
  //
  // This is an oddity of navigating to a media resource: the original request
  // for the media resource—which resulted in a committed navigation—is simply
  // discarded, while the media element created inside the MediaDocument then
  // makes *another new* request for the same media resource.
  //
  // TODO(dcheng): Barring something really strange and unusual, there should
  // always be a frame here.
  if (frame_ && frame_->GetDocument()->IsMediaDocument()) {
    parser_->Finish();
    StopLoading();
    return;
  }

  // Committing can run unload handlers, which can detach this frame or
  // stop this loader.
  if (!frame_ || !body_loader_)
    return;

  if (!url_.ProtocolIsInHTTPFamily()) {
    // We only support code cache for http family, and browser insists on not
    // event asking for code cache with other schemes.
    body_loader_->StartLoadingBody(this, nullptr /*code_cache_host*/);
    return;
  }

  ScriptableDocumentParser* scriptable_parser =
      parser_->AsScriptableDocumentParser();
  if (scriptable_parser) {
    auto cached_metadata_sender = CachedMetadataSender::Create(
        response_, blink::mojom::CodeCacheType::kJavascript,
        frame_->DomWindow()->GetSecurityOrigin());
    cached_metadata_handler_ =
        MakeGarbageCollected<SourceKeyedCachedMetadataHandler>(
            WTF::TextEncoding(), std::move(cached_metadata_sender));
    if (kCacheInlineScriptCodeFixConfiguring.Get())
      scriptable_parser->SetInlineScriptCacheHandler(cached_metadata_handler_);
  }

  if (waiting_for_document_loader_) {
    // If we were just waiting for the document loader, the body has already
    // started loading and it is safe to continue parsing if the code cache is
    // already available.
    waiting_for_document_loader_ = false;
    if (!waiting_for_code_cache_)
      parser_->CommitPreloadedData();
  } else {
    StartLoadingBodyWithCodeCache();
  }
}

void DocumentLoader::StartLoadingBodyWithCodeCache() {
  CodeCacheHost* code_cache_host =
      UseIsolatedCodeCache() ? GetCodeCacheHost() : nullptr;
  if (base::FeatureList::IsEnabled(features::kEarlyBodyLoad)) {
    waiting_for_code_cache_ = true;
    // If the body can load in parallel with the code cache, we need to enter
    // preload mode until the code cache is received. The data will be committed
    // once the code cache is available.
    parser_->SetIsPreloading(true);
  }
  body_loader_->StartLoadingBody(this, code_cache_host);
}

void DocumentLoader::DidInstallNewDocument(Document* document) {
  // This was called already during `InitializeWindow`, but it could be that we
  // didn't have a Document then (which happens when `InitializeWindow` reuses
  // the window and calls `LocalDOMWindow::ClearForReuse()`). This is
  // idempotent, so it is safe to do it again (in fact, it will be called again
  // also when parsing origin trials delivered in meta tags).
  frame_->DomWindow()->GetOriginTrialContext()->InitializePendingFeatures();

  frame_->DomWindow()->BindContentSecurityPolicy();

  if (history_item_ && IsBackForwardLoadType(load_type_))
    document->SetStateForNewControls(history_item_->GetDocumentState());

  DCHECK(document->GetFrame());
  // TODO(dgozman): modify frame's client hints directly once we commit
  // synchronously.
  document->GetFrame()->GetClientHintsPreferences().UpdateFrom(
      client_hints_preferences_);

  document->GetFrame()->SetReducedAcceptLanguage(reduced_accept_language_);

  const AtomicString& dns_prefetch_control =
      response_.HttpHeaderField(http_names::kXDNSPrefetchControl);
  if (!dns_prefetch_control.IsEmpty())
    document->ParseDNSPrefetchControlHeader(dns_prefetch_control);

  String header_content_language =
      response_.HttpHeaderField(http_names::kContentLanguage);
  if (!header_content_language.IsEmpty()) {
    wtf_size_t comma_index = header_content_language.find(',');
    // kNotFound == -1 == don't truncate
    header_content_language.Truncate(comma_index);
    header_content_language =
        header_content_language.StripWhiteSpace(IsHTMLSpace<UChar>);
    if (!header_content_language.IsEmpty())
      document->SetContentLanguage(AtomicString(header_content_language));
  }

  for (const auto& message : document_policy_parsing_messages_) {
    document->AddConsoleMessage(MakeGarbageCollected<ConsoleMessage>(
        mojom::blink::ConsoleMessageSource::kOther, message.level,
        message.content));
  }
  document_policy_parsing_messages_.clear();
}

void DocumentLoader::WillCommitNavigation() {
  if (commit_reason_ != CommitReason::kRegular)
    return;
  probe::WillCommitLoad(frame_, this);
  frame_->GetIdlenessDetector()->WillCommitLoad();
}

void DocumentLoader::DidCommitNavigation() {
  if (commit_reason_ != CommitReason::kRegular)
    return;

  WebHistoryCommitType commit_type = LoadTypeToCommitType(load_type_);
  frame_->GetFrameScheduler()->DidCommitProvisionalLoad(
      commit_type == kWebHistoryInertCommit,
      load_type_ == WebFrameLoadType::kReload
          ? FrameScheduler::NavigationType::kReload
          : FrameScheduler::NavigationType::kOther);
  if (response_.CacheControlContainsNoCache()) {
    GetFrame()->GetFrameScheduler()->RegisterStickyFeature(
        SchedulingPolicy::Feature::kMainResourceHasCacheControlNoCache,
        {SchedulingPolicy::DisableBackForwardCache()});
  }
  if (response_.CacheControlContainsNoStore()) {
    GetFrame()->GetFrameScheduler()->RegisterStickyFeature(
        SchedulingPolicy::Feature::kMainResourceHasCacheControlNoStore,
        {SchedulingPolicy::DisableBackForwardCache()});
  }

  // Reset the global |FontPerformance| counter.
  if (GetFrame()->IsMainFrame() &&
      GetFrame()->GetDocument()->ShouldMarkFontPerformance())
    FontPerformance::Reset();

  // When a new navigation commits in the frame, subresource loading should be
  // resumed.
  frame_->ResumeSubresourceLoading();

  Document* document = frame_->GetDocument();
  InteractiveDetector* interactive_detector =
      InteractiveDetector::From(*document);
  if (interactive_detector)
    interactive_detector->SetNavigationStartTime(GetTiming().NavigationStart());

  DEVTOOLS_TIMELINE_TRACE_EVENT("CommitLoad", inspector_commit_load_event::Data,
                                frame_);

  // Needs to run before dispatching preloads, as it may evict the memory cache.
  probe::DidCommitLoad(frame_, this);

  frame_->GetPage()->DidCommitLoad(frame_);

  // Report legacy TLS versions after Page::DidCommitLoad, because the latter
  // clears the console.
  if (response_.IsLegacyTLSVersion()) {
    GetFrameLoader().ReportLegacyTLSVersion(response_.CurrentRequestUrl(),
                                            false /* is_subresource */,
                                            frame_->IsAdFrame());
  }
}

Frame* DocumentLoader::CalculateOwnerFrame() {
  // For "about:srcdoc", the parent is the owner frame.
  if (url_.IsAboutSrcdocURL())
    return frame_->Tree().Parent();

  // Consider the parent or the opener for 1) about:blank" (including
  // "about:mumble" - see https://crbug.com/1220186) and 2) the initial empty
  // document (with an empty `url_`)..
  DCHECK(url_.ProtocolIsAbout() || url_.IsEmpty()) << "url_ = " << url_;
  Frame* owner_frame = frame_->Tree().Parent();
  if (!owner_frame)
    owner_frame = frame_->Loader().Opener();

  // No other checks are needed for the initial empty document.
  if (url_.IsEmpty())
    return owner_frame;

  // For about:blank the owner frame should be the actual initiator/requestor of
  // the navigation - see:
  // https://html.spec.whatwg.org/multipage/browsers.html#determining-the-origin
  //
  // This requires a few extra checks below.
  DCHECK(url_.ProtocolIsAbout()) << "url_ = " << url_;

  // Browser-initiated navigations to about:blank should always commit with an
  // opaque origin (i.e. they should not inherit the origin and other properties
  // of the `owner_frame`).
  if (!requestor_origin_)
    return nullptr;

  // The parent-or-owner heuristic above might not find the actual initiator of
  // the navigation (e.g. see the SameSiteSiblingToAboutBlank_CrossSiteTop
  // testcase).  To limit (but not eliminate :-/) incorrect cases we require
  // that `owner_frame`'s origin is same origin with `requestor_origin_`.
  //
  // TODO(https://crbug.com/1176291): Improve heuristics for finding the
  // correct initiator, to properly inherit/alias `document.domain` in more
  // cases.
  if (owner_frame &&
      owner_frame->GetSecurityContext()->GetSecurityOrigin()->IsSameOriginWith(
          requestor_origin_.get())) {
    return owner_frame;
  } else {
    return nullptr;
  }
}

scoped_refptr<SecurityOrigin> DocumentLoader::CalculateOrigin(
    Document* owner_document) {
  scoped_refptr<SecurityOrigin> origin;
  if (origin_to_commit_) {
    // Origin to commit is specified by the browser process, it must be taken
    // and used directly. It is currently supplied only for session history
    // navigations, where the origin was already calculated previously and
    // stored on the session history entry.
    origin = origin_to_commit_;
    origin_calculation_debug_info_ = "use_origin_to_commit";
  } else if (IsPagePopupRunningInWebTest(frame_)) {
    // If we are a page popup in LayoutTests ensure we use the popup
    // owner's security origin so the tests can possibly access the
    // document via internals API.
    auto* owner_context = frame_->PagePopupOwner()->GetExecutionContext();
    origin = owner_context->GetSecurityOrigin()->IsolatedCopy();
    origin_calculation_debug_info_ = "use_popup_owner_origin";
  } else if (owner_document && owner_document->domWindow()) {
    // Prefer taking `origin` from `owner_document` if one is available - this
    // will correctly inherit/alias `SecurityOrigin::domain_` from the
    // `owner_document` (note that the
    // `SecurityOrigin::CreateWithReferenceOrigin` fallback below A) doesn't
    // preserve `domain_` via `url::Origin` and B) doesn't alias the origin /
    // `domain_` - changes in the "about:blank" document do not affect the
    // initiator document).
    //
    // TODO(dcheng): if we're aliasing an origin, do we need to go through any
    // of the other checks below? This seems like it could have potentially
    // surprising side effects: for example, if the web security setting toggle
    // is disabled, this will affect the owner document's origin too...
    //
    // TODO(dcheng): maybe FrameLoader::Init() should specify origin_to_commit_?
    // But origin_to_commit_ is currently cloned with IsolatedCopy() which
    // breaks aliasing...
    origin = owner_document->domWindow()->GetMutableSecurityOrigin();
    origin_calculation_debug_info_ = "use_owner_document_origin";
  } else {
    origin_calculation_debug_info_ = "use_url_with_precursor";
    // Otherwise, create an origin that propagates precursor information
    // as needed. For non-opaque origins, this creates a standard tuple
    // origin, but for opaque origins, it creates an origin with the
    // initiator origin as the precursor.
    scoped_refptr<const SecurityOrigin> precursor = requestor_origin_;
    // For uuid-in-package: resources served from WebBundles, use the Bundle's
    // origin as the precursor.
    if (url_.ProtocolIs("uuid-in-package") &&
        response_.WebBundleURL().IsValid()) {
      precursor = SecurityOrigin::Create(response_.WebBundleURL());
      origin_calculation_debug_info_ =
          origin_calculation_debug_info_ + "_web_bundle";
    }
    origin = SecurityOrigin::CreateWithReferenceOrigin(url_, precursor.get());
  }

  if ((policy_container_->GetPolicies().sandbox_flags &
       network::mojom::blink::WebSandboxFlags::kOrigin) !=
      network::mojom::blink::WebSandboxFlags::kNone) {
    origin_calculation_debug_info_ =
        origin_calculation_debug_info_ + ", add_sandbox";
    auto sandbox_origin = origin->DeriveNewOpaqueOrigin();

    // If we're supposed to inherit our security origin from our
    // owner, but we're also sandboxed, the only things we inherit are
    // the origin's potential trustworthiness and the ability to
    // load local resources. The latter lets about:blank iframes in
    // file:// URL documents load images and other resources from
    // the file system.
    //
    // Note: Sandboxed about:srcdoc iframe without "allow-same-origin" aren't
    // allowed to load user's file, even if its parent can.
    if (owner_document) {
      if (origin->IsPotentiallyTrustworthy()) {
        sandbox_origin->SetOpaqueOriginIsPotentiallyTrustworthy(true);
        origin_calculation_debug_info_ =
            origin_calculation_debug_info_ + "_potentially_trustworthy";
      }
      if (origin->CanLoadLocalResources() && !loading_srcdoc_) {
        sandbox_origin->GrantLoadLocalResources();
        origin_calculation_debug_info_ =
            origin_calculation_debug_info_ + "_load_local";
      }
    }
    origin = sandbox_origin;
  }

  if (commit_reason_ == CommitReason::kInitialization &&
      frame_->GetSettings()->GetShouldReuseGlobalForUnownedMainFrame() &&
      !frame_->Parent() && !frame_->Opener()) {
    // For legacy reasons, grant universal access to a top-level initial empty
    // Document in Android WebView. This allows the WebView embedder to inject
    // arbitrary script into about:blank and have it persist when the frame is
    // navigated.
    CHECK(origin->IsOpaque());
    origin->GrantUniversalAccess();
    origin_calculation_debug_info_ =
        origin_calculation_debug_info_ + ", universal_access_webview";
  } else if (!frame_->GetSettings()->GetWebSecurityEnabled()) {
    // Web security is turned off. We should let this document access
    // every other document. This is used primary by testing harnesses for
    // web sites.
    origin->GrantUniversalAccess();
    origin_calculation_debug_info_ =
        origin_calculation_debug_info_ + ", universal_access_no_web_security";
  } else if (origin->IsLocal()) {
    if (frame_->GetSettings()->GetAllowUniversalAccessFromFileURLs()) {
      // Some clients want local URLs to have universal access, but that
      // setting is dangerous for other clients.
      origin->GrantUniversalAccess();
      origin_calculation_debug_info_ =
          origin_calculation_debug_info_ + ", universal_access_allow_file";
    } else if (!frame_->GetSettings()->GetAllowFileAccessFromFileURLs()) {
      // Some clients do not want local URLs to have access to other local
      // URLs.
      origin->BlockLocalAccessFromLocalOrigin();
      origin_calculation_debug_info_ =
          origin_calculation_debug_info_ + ", universal_access_block_file";
    }
  }

  if (grant_load_local_resources_) {
    origin->GrantLoadLocalResources();
    origin_calculation_debug_info_ =
        origin_calculation_debug_info_ + ", grant_load_local_resources";
  }

  if (origin->IsOpaque()) {
    KURL url = url_.IsEmpty() ? BlankURL() : url_;
    if (SecurityOrigin::Create(url)->IsPotentiallyTrustworthy()) {
      origin->SetOpaqueOriginIsPotentiallyTrustworthy(true);
      origin_calculation_debug_info_ =
          origin_calculation_debug_info_ + ", is_potentially_trustworthy";
    }
  }

  return origin;
}

bool ShouldReuseDOMWindow(LocalDOMWindow* window,
                          SecurityOrigin* security_origin,
                          bool window_anonymous_matching) {
  if (!window) {
    return false;
  }

  // Anonymous is tracked per-Window, so if it does not match, do not reuse it.
  if (!window_anonymous_matching) {
    return false;
  }

  // Only navigations from the initial empty document can reuse the window.
  if (!window->document()->IsInitialEmptyDocument()) {
    return false;
  }

  // The new origin must match the origin of the initial empty document.
  return window->GetSecurityOrigin()->CanAccess(security_origin);
}

namespace {

bool HasPotentialUniversalAccessPrivilege(LocalFrame* frame) {
  return !frame->GetSettings()->GetWebSecurityEnabled() ||
         frame->GetSettings()->GetAllowUniversalAccessFromFileURLs();
}

}  // namespace

WindowAgent* GetWindowAgentForOrigin(
    LocalFrame* frame,
    SecurityOrigin* origin,
    bool is_origin_agent_cluster,
    bool origin_agent_cluster_left_as_default) {
  // TODO(keishi): Also check if AllowUniversalAccessFromFileURLs might
  // dynamically change.
  return frame->window_agent_factory().GetAgentForOrigin(
      HasPotentialUniversalAccessPrivilege(frame),
      V8PerIsolateData::MainThreadIsolate(), origin, is_origin_agent_cluster,
      origin_agent_cluster_left_as_default);
}

// Inheriting cases use their agent's "is origin-keyed" value, which is set
// by whatever they're inheriting from.
//
// javascript: URLs use the calling page as their Url() value, so we need to
// include them explicitly.
bool ShouldInheritExplicitOriginKeying(const KURL& url, CommitReason reason) {
  return Document::ShouldInheritSecurityOriginFromOwner(url) ||
         reason == CommitReason::kJavascriptUrl;
}

void DocumentLoader::InitializeWindow(Document* owner_document) {
  // Javascript URLs and XSLT committed document must not pass a new
  // policy_container_, since they must keep the previous document one.
  DCHECK((commit_reason_ != CommitReason::kJavascriptUrl &&
          commit_reason_ != CommitReason::kXSLT) ||
         !policy_container_);

  bool did_have_policy_container = (policy_container_ != nullptr);

  // The old window's PolicyContainer must be accessed before being potentially
  // extracted below.
  const bool old_window_is_anonymous =
      frame_->DomWindow() &&
      frame_->DomWindow()->GetPolicyContainer()->GetPolicies().is_anonymous;

  // DocumentLoader::InitializeWindow is called either on FrameLoader::Init or
  // on FrameLoader::CommitNavigation. FrameLoader::Init always initializes a
  // non null |policy_container_|. If |policy_container_| is null, this is
  // committing a navigation without a policy container. This can happen in a
  // few circumstances:
  // 1. for a javascript or a xslt document,
  // 2. when loading html in a page for testing,
  // 3. this is the synchronous navigation to 'about:blank'.
  // (On the other side notice that all navigations committed by the browser
  // have a non null |policy_container_|). In all the cases 1-3 above, we should
  // keep the PolicyContainer of the previous document (since the browser does
  // not know about this and is not changing the RenderFrameHost's
  // PolicyContainerHost).
  if (frame_->DomWindow() && !policy_container_) {
    policy_container_ = frame_->DomWindow()->TakePolicyContainer();
  }

  // Every window must have a policy container.
  DCHECK(policy_container_);

  const bool window_anonymous_matching =
      old_window_is_anonymous == policy_container_->GetPolicies().is_anonymous;

  ContentSecurityPolicy* csp = CreateCSP();

  // Provisional frames shouldn't be doing anything other than act as a
  // placeholder. Enforce a strict sandbox and ensure a unique opaque origin.
  // TODO(dcheng): Actually enforce strict sandbox flags for provisional frame.
  // For some reason, doing so breaks some random devtools tests.
  auto security_origin = frame_->IsProvisional()
                             ? SecurityOrigin::CreateUniqueOpaque()
                             : CalculateOrigin(owner_document);

  bool origin_agent_cluster = origin_agent_cluster_;
  // Note: this code must be kept in sync with
  // WindowAgentFactory::GetAgentForOrigin(), as the two conditions below hand
  // out universal WindowAgent objects, and thus override OAC.
  if (HasPotentialUniversalAccessPrivilege(frame_.Get()) ||
      security_origin->IsLocal()) {
    // In this case we either have AllowUniversalAccessFromFileURLs enabled, or
    // WebSecurity is disabled, or it's a local scheme such as file://; any of
    // these cases forces us to use a common WindowAgent for all origins, so
    // don't attempt to use OriginAgentCluster. Note:
    // AllowUniversalAccessFromFileURLs is deprecated as of Android R, so
    // eventually this use case will diminish.
    origin_agent_cluster = false;
  } else if (ShouldInheritExplicitOriginKeying(Url(), commit_reason_) &&
             owner_document && owner_document->domWindow()) {
    // Since we're inheriting the owner document's origin, we should also use
    // its OriginAgentCluster (OAC) in determining which WindowAgent to use,
    // overriding the OAC value sent in the commit params. For example, when
    // about:blank is loaded, it has OAC = false, but if we have an owner, then
    // we are using the owner's SecurityOrigin, we should match the OAC value
    // also. JavaScript URLs also use their owner's SecurityOrigins, and don't
    // set OAC as part of their commit params.
    // TODO(wjmaclean,domenic): we're currently verifying that the OAC
    // inheritance is correct for both XSLT documents and non-initial
    // about:blank cases. Given the relationship between OAC, SecurityOrigin,
    // and COOP/COEP, a single inheritance pathway would make sense; this work
    // is being tracked in https://crbug.com/1183935.
    origin_agent_cluster =
        owner_document->domWindow()->GetAgent()->IsOriginKeyedForInheritance();
  }

  // In some rare cases, we'll re-use a LocalDOMWindow for a new Document. For
  // example, when a script calls window.open("..."), the browser gives
  // JavaScript a window synchronously but kicks off the load in the window
  // asynchronously. Web sites expect that modifications that they make to the
  // window object synchronously won't be blown away when the network load
  // commits. To make that happen, we "securely transition" the existing
  // LocalDOMWindow to the Document that results from the network load. See also
  // Document::IsSecureTransitionTo.
  if (!ShouldReuseDOMWindow(frame_->DomWindow(), security_origin.get(),
                            window_anonymous_matching)) {
    auto* agent = GetWindowAgentForOrigin(
        frame_.Get(), security_origin.get(), origin_agent_cluster,
        origin_agent_cluster_left_as_default_);
    frame_->SetDOMWindow(MakeGarbageCollected<LocalDOMWindow>(*frame_, agent));

    // TODO(https://crbug.com/1111897): This call is likely to happen happen
    // multiple times per agent, since navigations can happen multiple times per
    // agent. This is subpar.
    if (!ShouldInheritExplicitOriginKeying(Url(), commit_reason_) &&
        origin_agent_cluster) {
      agent->ForceOriginKeyedBecauseOfInheritance();
    }
  } else {
    if (frame_->GetSettings()->GetShouldReuseGlobalForUnownedMainFrame() &&
        frame_->IsMainFrame()) {
      // When GetShouldReuseGlobalForUnownedMainFrame() causes a main frame's
      // window to be reused, we should not inherit the initial empty document's
      // Agent, which was a universal access Agent.
      // This happens only in android webview.
      frame_->DomWindow()->ResetWindowAgent(GetWindowAgentForOrigin(
          frame_.Get(), security_origin.get(), origin_agent_cluster,
          origin_agent_cluster_left_as_default_));
    }
    frame_->DomWindow()->ClearForReuse();

    // If one of the two following things is true:
    // 1. JS called window.open(), Blink created a new auxiliary browsing
    //    context, and the target URL is resolved to 'about:blank'.
    // 2. A new iframe is attached, and the target URL is resolved to
    //    'about:blank'.
    // then Blink immediately synchronously navigates to about:blank after
    // creating the new browsing context and has initialized it with the initial
    // empty document. In those cases, we must not pass a PolicyContainer, as
    // this does not trigger a corresponding browser-side navigation, and we
    // must reuse the PolicyContainer.
    //
    // TODO(antoniosartori): Improve this DCHECK to match exactly the condition
    // above.
    DCHECK(did_have_policy_container || WillLoadUrlAsEmpty(Url()));
  }

  frame_->DomWindow()->SetPolicyContainer(std::move(policy_container_));
  frame_->DomWindow()->SetContentSecurityPolicy(csp);

  // Now that we have the final window and Agent, ensure the security origin has
  // the appropriate agent cluster id. This may derive a new security origin.
  security_origin = security_origin->GetOriginForAgentCluster(
      frame_->DomWindow()->GetAgent()->cluster_id());

  // TODO(https://crbug.com/888079): Just use the storage key sent by the
  // browser once the browser will be able to compute the origin in all cases.
  frame_->DomWindow()->SetStorageKey(
      BlinkStorageKey(security_origin, storage_key_.GetTopLevelSite(),
                      base::OptionalToPtr(storage_key_.GetNonce()),
                      storage_key_.GetAncestorChainBit()));

  // Conceptually, SecurityOrigin doesn't have to be initialized after sandbox
  // flags are applied, but there's a UseCounter in SetSecurityOrigin() that
  // wants to inspect sandbox flags.
  SecurityContext& security_context = frame_->DomWindow()->GetSecurityContext();
  security_context.SetSecurityOrigin(std::move(security_origin));
  // Requires SecurityOrigin to be initialized.
  OriginTrialContext::AddTokensFromHeader(
      frame_->DomWindow(), response_.HttpHeaderField(http_names::kOriginTrial));

  if (auto* parent = frame_->Tree().Parent(FrameTreeBoundary::kFenced)) {
    const SecurityContext* parent_context = parent->GetSecurityContext();
    security_context.SetInsecureRequestPolicy(
        parent_context->GetInsecureRequestPolicy());
    for (auto to_upgrade : parent_context->InsecureNavigationsToUpgrade())
      security_context.AddInsecureNavigationUpgrade(to_upgrade);
  }

  String referrer_policy_header =
      response_.HttpHeaderField(http_names::kReferrerPolicy);
  if (!referrer_policy_header.IsNull()) {
    CountUse(WebFeature::kReferrerPolicyHeader);
    frame_->DomWindow()->ParseAndSetReferrerPolicy(referrer_policy_header,
                                                   kPolicySourceHttpHeader);
  }
}

void DocumentLoader::CommitNavigation() {
  DCHECK_LT(state_, kCommitted);
  DCHECK(frame_->GetPage());
  DCHECK(!frame_->GetDocument() || !frame_->GetDocument()->IsActive());
  DCHECK_EQ(frame_->Tree().ChildCount(), 0u);
  DCHECK(!frame_->GetDocument() ||
         frame_->GetDocument()->ConnectedSubframeCount() == 0);
  state_ = kCommitted;

  if (body_loader_ && !loading_main_document_from_mhtml_archive_ &&
      !loading_url_as_empty_document_ && url_.ProtocolIsInHTTPFamily() &&
      UseIsolatedCodeCache() &&
      base::FeatureList::IsEnabled(features::kEarlyCodeCache)) {
    body_loader_->StartLoadingCodeCache(GetCodeCacheHost());
  }

  // Prepare a DocumentInit before clearing the frame, because it may need to
  // inherit an aliased security context.
  Document* owner_document = nullptr;

  // Calculate `owner_document` from which the committing navigation should
  // inherit the cookie URL and inherit/alias the SecurityOrigin.
  if (Document::ShouldInheritSecurityOriginFromOwner(Url())) {
    Frame* owner_frame = CalculateOwnerFrame();
    if (auto* owner_local_frame = DynamicTo<LocalFrame>(owner_frame))
      owner_document = owner_local_frame->GetDocument();
  }

  LocalDOMWindow* previous_window = frame_->DomWindow();
  InitializeWindow(owner_document);

  // Record if we have navigated to a non-secure page served from a IP address
  // in the private address space.
  //
  // Use response_.AddressSpace() instead of frame_->DomWindow()->AddressSpace()
  // since the latter isn't populated in unit tests.
  if (frame_->IsOutermostMainFrame()) {
    auto address_space = response_.AddressSpace();
    if ((address_space == network::mojom::blink::IPAddressSpace::kPrivate ||
         address_space == network::mojom::blink::IPAddressSpace::kLocal) &&
        !frame_->DomWindow()->IsSecureContext()) {
      CountUse(WebFeature::kMainFrameNonSecurePrivateAddressSpace);
    }
  }

  SecurityContextInit security_init(frame_->DomWindow());

  // The document constructed by XSLTProcessor and ScriptController should
  // inherit Permissions Policy and Document Policy from the previous Document.
  // Note: In XSLT commit and JavaScript commit, |response_| no longer holds
  // header fields. Going through regular initialization will cause empty policy
  // even if there is header on xml document.
  if (commit_reason_ == CommitReason::kXSLT ||
      commit_reason_ == CommitReason::kJavascriptUrl) {
    DCHECK(response_.HttpHeaderField(http_names::kFeaturePolicy).IsEmpty());
    DCHECK(response_.HttpHeaderField(http_names::kPermissionsPolicy).IsEmpty());
    DCHECK(response_.HttpHeaderField(http_names::kDocumentPolicy).IsEmpty());
    security_init.InitPermissionsPolicyFrom(
        previous_window->GetSecurityContext());
    security_init.InitDocumentPolicyFrom(previous_window->GetSecurityContext());
  } else {
    // PermissionsPolicy and DocumentPolicy require SecurityOrigin and origin
    // trials to be initialized.
    // TODO(iclelland): Add Permissions-Policy-Report-Only to Origin Policy.
    security_init.ApplyPermissionsPolicy(
        *frame_.Get(), response_, frame_policy_,
        Agent::IsIsolatedApplication() ? initial_permissions_policy_
                                       : absl::nullopt);

    // |document_policy_| is parsed in document loader because it is
    // compared with |frame_policy.required_document_policy| to decide
    // whether to block the document load or not.
    // |report_only_document_policy| does not block the page load. Its
    // initialization is delayed to
    // SecurityContextInit::InitializeDocumentPolicy(), similar to
    // |report_only_permissions_policy|.
    security_init.ApplyDocumentPolicy(
        document_policy_,
        response_.HttpHeaderField(http_names::kDocumentPolicyReportOnly));
  }

  navigation_scroll_allowed_ = !frame_->DomWindow()->IsFeatureEnabled(
      mojom::blink::DocumentPolicyFeature::kForceLoadAtTop);

  WillCommitNavigation();

  is_prerendering_ = frame_->GetPage()->IsPrerendering();
  Document* document = frame_->DomWindow()->InstallNewDocument(
      DocumentInit::Create()
          .WithWindow(frame_->DomWindow(), owner_document)
          .ForInitialEmptyDocument(commit_reason_ ==
                                   CommitReason::kInitialization)
          .ForPrerendering(is_prerendering_)
          .WithURL(Url())
          .WithTypeFrom(MimeType())
          .WithSrcdocDocument(loading_srcdoc_)
          .WithWebBundleClaimedUrl(web_bundle_claimed_url_)
          .WithUkmSourceId(ukm_source_id_));

  RecordUseCountersForCommit();
  RecordConsoleMessagesForCommit();
  if (!response_.HttpHeaderField(http_names::kExpectCT).IsEmpty()) {
    Deprecation::CountDeprecation(frame_->DomWindow(),
                                  mojom::blink::WebFeature::kExpectCTHeader);
  }

  // Clear the user activation state.
  // TODO(crbug.com/736415): Clear this bit unconditionally for all frames.
  if (frame_->IsMainFrame())
    frame_->ClearUserActivation();

  // The DocumentLoader was flagged as activated if it needs to notify the frame
  // that it was activated before navigation. Update the frame state based on
  // the new value.
  OldDocumentInfoForCommit* old_document_info_for_commit =
      ScopedOldDocumentInfoForCommitCapturer::CurrentInfo();
  bool had_sticky_activation_before_navigation =
      old_document_info_for_commit
          ? old_document_info_for_commit
                ->had_sticky_activation_before_navigation
          : false;
  if (had_sticky_activation_before_navigation != had_sticky_activation_) {
    frame_->SetHadStickyUserActivationBeforeNavigation(had_sticky_activation_);
    frame_->GetLocalFrameHostRemote()
        .HadStickyUserActivationBeforeNavigationChanged(had_sticky_activation_);
  }

  bool should_clear_window_name =
      previous_window && frame_->IsOutermostMainFrame() &&
      !frame_->Loader().Opener() &&
      !frame_->DomWindow()->GetSecurityOrigin()->IsSameOriginWith(
          previous_window->GetSecurityOrigin());
  if (should_clear_window_name) {
    // TODO(andypaicu): experimentalSetNullName will just record the fact
    // that the name would be nulled and if the name is accessed after we will
    // fire a UseCounter. If we decide to move forward with this change, we'd
    // actually clean the name here.
    // frame_->tree().setName(g_null_atom);
    frame_->Tree().ExperimentalSetNulledName();
  }

  bool should_clear_cross_site_cross_browsing_context_group_window_name =
      previous_window && frame_->IsOutermostMainFrame() &&
      is_cross_site_cross_browsing_context_group_;
  if (should_clear_cross_site_cross_browsing_context_group_window_name) {
    // TODO(shuuran): CrossSiteCrossBrowsingContextGroupSetNulledName will just
    // record the fact that the name would be nulled and if the name is accessed
    // after we will fire a UseCounter.
    frame_->Tree().CrossSiteCrossBrowsingContextGroupSetNulledName();
  }

  // MHTML archive's URL is usually a local file. However the main resource
  // within the archive has a public URL and must be used to resolve all the
  // relative links.
  if (loading_main_document_from_mhtml_archive_) {
    ArchiveResource* main_resource = archive_->MainResource();
    KURL main_resource_url = main_resource ? main_resource->Url() : KURL();
    if (!main_resource_url.IsEmpty())
      document->SetBaseURLOverride(main_resource_url);
  }

  // The navigation API is not initialized on the initial about:blank document
  // or opaque-origin documents.
  if (commit_reason_ != CommitReason::kInitialization &&
      !frame_->DomWindow()->GetSecurityOrigin()->IsOpaque()) {
    NavigationApi::From(*frame_->DomWindow())
        ->InitializeForNewWindow(*history_item_, load_type_, commit_reason_,
                                 NavigationApi::navigation(*previous_window),
                                 navigation_api_back_entries_,
                                 navigation_api_forward_entries_);
    // Now that the navigation API's entries array is initialized, we don't need
    // to retain the state from which it was initialized.
    navigation_api_back_entries_.Clear();
    navigation_api_forward_entries_.Clear();
  }

  if (commit_reason_ == CommitReason::kXSLT)
    DocumentXSLT::SetHasTransformSource(*document);

  DidInstallNewDocument(document);

  // This must be called before the document is opened, otherwise HTML parser
  // will use stale values from HTMLParserOption.
  DidCommitNavigation();

  // This must be called after DidInstallNewDocument which sets the content
  // language for the document.
  if (url_.ProtocolIsInHTTPFamily()) {
    RecordAcceptLanguageAndContentLanguageMetric();
    RecordParentAndChildContentLanguageMetric();
  }

  bool is_same_origin_initiator = false;
  if (requestor_origin_) {
    const scoped_refptr<const SecurityOrigin> url_origin =
        SecurityOrigin::Create(Url());

    is_same_origin_initiator =
        requestor_origin_->IsSameOriginWith(url_origin.get()) &&
        Url().ProtocolIsInHTTPFamily();
  }

  // No requestor origin means it's browser-initiated (which includes *all*
  // history navigations, including those initiated from `window.history`
  // API).
  last_navigation_had_trusted_initiator_ =
      !requestor_origin_ || is_same_origin_initiator;

  // The PaintHolding feature defers compositor commits until content has
  // been painted or 500ms have passed, whichever comes first. The additional
  // PaintHoldingCrossOrigin feature allows PaintHolding even for cross-origin
  // navigations, otherwise only same-origin navigations have deferred commits.
  // We also require that this be an html document served via http.
  if (base::FeatureList::IsEnabled(blink::features::kPaintHolding) &&
      IsA<HTMLDocument>(document) && Url().ProtocolIsInHTTPFamily() &&
      (is_same_origin_initiator ||
       base::FeatureList::IsEnabled(
           blink::features::kPaintHoldingCrossOrigin))) {
    document->SetDeferredCompositorCommitIsAllowed(true);
  } else {
    document->SetDeferredCompositorCommitIsAllowed(false);
  }

  if ((response_.IsHTTP() || is_error_page_for_failed_navigation_) &&
      navigation_timing_info_) {
    // The response is being copied here to pass the ServerTiming info.
    // TODO(yoav): copy the ServerTiming info directly.
    navigation_timing_info_->SetFinalResponse(response_);
    // Make sure we're properly reporting error documents.
    if (is_error_page_for_failed_navigation_) {
      navigation_timing_info_->SetInitialURL(
          pre_redirect_url_for_failed_navigations_);
    }
  }

  {
    // Notify the browser process about the commit.
    FrameNavigationDisabler navigation_disabler(*frame_);
    if (commit_reason_ == CommitReason::kInitialization) {
      // There's no observers yet so nothing to notify.
    } else if (IsJavaScriptURLOrXSLTCommit()) {
      GetLocalFrameClient().DidCommitDocumentReplacementNavigation(this);
    } else {
      GetLocalFrameClient().DispatchDidCommitLoad(
          history_item_.Get(), LoadTypeToCommitType(load_type_),
          previous_window != frame_->DomWindow(),
          security_init.PermissionsPolicyHeader(),
          document_policy_.feature_state);
    }
    // TODO(dgozman): make DidCreateScriptContext notification call currently
    // triggered by installing new document happen here, after commit.
  }
  // Note: this must be called after DispatchDidCommitLoad() for
  // metrics to be correctly sent to the browser process.
  if (commit_reason_ != CommitReason::kInitialization)
    use_counter_.DidCommitLoad(frame_);
  if (load_type_ == WebFrameLoadType::kBackForward) {
    if (Page* page = frame_->GetPage())
      page->HistoryNavigationVirtualTimePauser().UnpauseVirtualTime();
  }

  // If profiling is enabled by document policy, ensure that profiling metadata
  // is available by tracking the execution context's lifetime.
  ProfilerGroup::InitializeIfEnabled(frame_->DomWindow());

  // Load the document if needed.
  StartLoadingResponse();
}

void DocumentLoader::CreateParserPostCommit() {
  // DidObserveLoadingBehavior() must be called after DispatchDidCommitLoad() is
  // called for the metrics tracking logic to handle it properly.
  if (service_worker_network_provider_ &&
      service_worker_network_provider_->GetControllerServiceWorkerMode() ==
          mojom::blink::ControllerServiceWorkerMode::kControlled) {
    LoadingBehaviorFlag loading_behavior =
        kLoadingBehaviorServiceWorkerControlled;
    if (service_worker_network_provider_->GetFetchHandlerType() !=
        mojom::blink::ServiceWorkerFetchHandlerType::kNotSkippable) {
      DCHECK_NE(service_worker_network_provider_->GetFetchHandlerType(),
                mojom::blink::ServiceWorkerFetchHandlerType::kNoHandler);
      // LoadingBehaviorFlag is a bit stream, and `|` should work.
      loading_behavior = static_cast<LoadingBehaviorFlag>(
          loading_behavior |
          kLoadingBehaviorServiceWorkerFetchHandlerSkippable);
    }
    GetLocalFrameClient().DidObserveLoadingBehavior(loading_behavior);
  }

  // Links with media values need more information (like viewport information).
  // This happens after the first chunk is parsed in HTMLDocumentParser.
  DispatchLinkHeaderPreloads(nullptr /* viewport */,
                             PreloadHelper::kOnlyLoadNonMedia);

  // Initializing origin trials might force window proxy initialization,
  // which later triggers CHECK when swapping in via WebFrame::Swap().
  // We can safely omit installing original trials on initial empty document
  // and wait for the real load.
  if (commit_reason_ != CommitReason::kInitialization) {
    LocalDOMWindow* window = frame_->DomWindow();
    if (frame_->GetSettings()
            ->GetForceTouchEventFeatureDetectionForInspector()) {
      window->GetOriginTrialContext()->AddFeature(
          OriginTrialFeature::kTouchEventFeatureDetection);
    }

    // Enable any origin trials that have been force enabled for this commit.
    window->GetOriginTrialContext()->AddForceEnabledTrials(
        force_enabled_origin_trials_);

    OriginTrialContext::ActivateNavigationFeaturesFromInitiator(
        window, &initiator_origin_trial_features_);
  }

  ParserSynchronizationPolicy parsing_policy = kAllowDeferredParsing;
  if (IsJavaScriptURLOrXSLTCommit() ||
      Document::ForceSynchronousParsingForTesting()) {
    parsing_policy = kForceSynchronousParsing;
  }
  const AtomicString& encoding = commit_reason_ == CommitReason::kXSLT
                                     ? "UTF-8"
                                     : response_.TextEncodingName();

  Document* document = frame_->GetDocument();
  parser_ = document->OpenForNavigation(parsing_policy, MimeType(), encoding);

  // XSLT processing converts the response into UTF-8 before sending it through
  // the DocumentParser, but we should still report the original encoding when
  // script queries it via document.characterSet.
  if (commit_reason_ == CommitReason::kXSLT) {
    DocumentEncodingData data;
    data.SetEncoding(WTF::TextEncoding(response_.TextEncodingName()));
    document->SetEncodingData(data);
  }

  if (base::FeatureList::IsEnabled(features::kEarlyBodyLoad)) {
    if (frame_ && body_loader_ && !loading_main_document_from_mhtml_archive_ &&
        !loading_url_as_empty_document_ && url_.ProtocolIsInHTTPFamily() &&
        !is_static_data_ && frame_->IsMainFrame() &&
        !document->IsPrefetchOnly() && MimeType() == "text/html") {
      waiting_for_document_loader_ = true;
      StartLoadingBodyWithCodeCache();

      if (!frame_ || !body_loader_)
        return;
    }

    frame_->DomWindow()->GetScriptController().UpdateDocument();
  }

  if (!kCacheInlineScriptCodeFixConfiguring.Get()) {
    // If this is a scriptable parser and there is a resource, register the
    // resource's cache handler with the parser.
    ScriptableDocumentParser* scriptable_parser =
        parser_->AsScriptableDocumentParser();
    if (scriptable_parser && cached_metadata_handler_)
      scriptable_parser->SetInlineScriptCacheHandler(cached_metadata_handler_);
  }

  GetFrameLoader().DispatchDidClearDocumentOfWindowObject();

  parser_->SetDocumentWasLoadedAsPartOfNavigation();
  if (was_discarded_)
    document->SetWasDiscarded(true);
  document->MaybeHandleHttpRefresh(
      response_.HttpHeaderField(http_names::kRefresh),
      Document::kHttpRefreshFromHeader);

  // The parser may have collected preloads in the background, flush them now.
  parser_->FlushPendingPreloads();
}

const AtomicString& DocumentLoader::MimeType() const {
  // In the case of mhtml archive, |response_| has an archive mime type,
  // while the document has a different mime type.
  if (loading_main_document_from_mhtml_archive_) {
    if (ArchiveResource* main_resource = archive_->MainResource())
      return main_resource->MimeType();
  }

  return response_.MimeType();
}

void DocumentLoader::BlockParser() {
  parser_blocked_count_++;
}

void DocumentLoader::ResumeParser() {
  parser_blocked_count_--;
  DCHECK_GE(parser_blocked_count_, 0);

  if (parser_blocked_count_ != 0)
    return;

  ProcessDataBuffer();

  if (finish_loading_when_parser_resumed_) {
    finish_loading_when_parser_resumed_ = false;
    parser_->Finish();
    parser_.Clear();
  }
}

void DocumentLoader::CountUse(mojom::WebFeature feature) {
  return use_counter_.Count(feature, GetFrame());
}

void DocumentLoader::CountDeprecation(mojom::WebFeature feature) {
  return use_counter_.Count(feature, GetFrame());
}

void DocumentLoader::RecordAcceptLanguageAndContentLanguageMetric() {
  // Get document Content-Language value, which has been set as the top-most
  // content language value from http head.
  constexpr const char language_histogram_name[] =
      "LanguageUsage.AcceptLanguageAndContentLanguageUsage";

  const AtomicString& content_language =
      frame_->GetDocument()->ContentLanguage();
  if (!content_language) {
    base::UmaHistogramEnumeration(
        language_histogram_name,
        AcceptLanguageAndContentLanguageUsage::kContentLanguageEmpty);
    return;
  }

  if (content_language == "*") {
    base::UmaHistogramEnumeration(
        language_histogram_name,
        AcceptLanguageAndContentLanguageUsage::kContentLanguageWildcard);
    return;
  }

  // Get Accept-Language header value from Prefs
  bool is_accept_language_dirty =
      frame_->DomWindow()->navigator()->IsLanguagesDirty();
  const Vector<String>& accept_languages =
      frame_->DomWindow()->navigator()->languages();

  // Match content languages and accept languages list:
  // 1. If any value in content languages matches the top-most accept languages
  // 2. If there are any overlap between content languages and accept languages
  if (accept_languages.front() == content_language) {
    base::UmaHistogramEnumeration(
        language_histogram_name,
        AcceptLanguageAndContentLanguageUsage::
            kContentLanguageMatchesPrimaryAcceptLanguage);
  }

  if (base::Contains(accept_languages, content_language)) {
    base::UmaHistogramEnumeration(language_histogram_name,
                                  AcceptLanguageAndContentLanguageUsage::
                                      kContentLanguageMatchesAnyAcceptLanguage);
  }

  // navigator()->languages() is a potential update operation, it could set
  // |is_dirty_language| to false which causes future override operations
  // can't update the accep_language list. We should reset the language to
  // dirty if accept language is dirty before we read from Prefs.
  if (is_accept_language_dirty) {
    frame_->DomWindow()->navigator()->SetLanguagesDirty();
  }
}

void DocumentLoader::RecordParentAndChildContentLanguageMetric() {
  // Check child frame and parent frame content language value.
  if (auto* parent = DynamicTo<LocalFrame>(frame_->Tree().Parent())) {
    const AtomicString& content_language =
        frame_->GetDocument()->ContentLanguage();

    const AtomicString& parent_content_language =
        parent->GetDocument()->ContentLanguage();

    if (parent_content_language != content_language) {
      base::UmaHistogramEnumeration(
          "LanguageUsage.AcceptLanguageAndContentLanguageUsage",
          AcceptLanguageAndContentLanguageUsage::
              kContentLanguageSubframeDiffers);
    }
  }
}

void DocumentLoader::RecordUseCountersForCommit() {
  // Pre-commit state, count usage the use counter associated with "this"
  // (provisional document loader) instead of frame_'s document loader.
  if (response_.DidServiceWorkerNavigationPreload())
    CountUse(WebFeature::kServiceWorkerNavigationPreload);
  if (frame_->DomWindow()->IsFeatureEnabled(
          mojom::blink::DocumentPolicyFeature::kForceLoadAtTop)) {
    CountUse(WebFeature::kForceLoadAtTop);
  }

  if (response_.IsSignedExchangeInnerResponse()) {
    CountUse(WebFeature::kSignedExchangeInnerResponse);
    CountUse(frame_->IsOutermostMainFrame()
                 ? WebFeature::kSignedExchangeInnerResponseInMainFrame
                 : WebFeature::kSignedExchangeInnerResponseInSubFrame);
  }

  if (!response_.HttpHeaderField(http_names::kRequireDocumentPolicy).IsNull())
    CountUse(WebFeature::kRequireDocumentPolicyHeader);

  if (was_blocked_by_document_policy_)
    CountUse(WebFeature::kDocumentPolicyCausedPageUnload);

  // Required document policy can either come from iframe attribute or HTTP
  // header 'Require-Document-Policy'.
  if (!frame_policy_.required_document_policy.empty())
    CountUse(WebFeature::kRequiredDocumentPolicy);

  FrameClientHintsPreferencesContext hints_context(frame_);
  for (const auto& elem : network::GetClientHintToNameMap()) {
    const auto& type = elem.first;
    if (client_hints_preferences_.ShouldSend(type))
      hints_context.CountClientHints(type);
  }

  if (RuntimeEnabledFeatures::EarlyHintsPreloadForNavigationOptInEnabled(
          frame_->DomWindow()) &&
      !early_hints_preloaded_resources_.IsEmpty()) {
    CountUse(WebFeature::kEarlyHintsPreload);
  }
}

void DocumentLoader::RecordConsoleMessagesForCommit() {
  if (was_blocked_by_document_policy_) {
    // TODO(chenleihu): Add which document policy violated in error string,
    // instead of just displaying serialized required document policy.
    ConsoleError(
        "Refused to display '" + response_.CurrentRequestUrl().ElidedString() +
        "' because it violates the following document policy "
        "required by its embedder: '" +
        DocumentPolicy::Serialize(frame_policy_.required_document_policy)
            .value_or("[Serialization Error]")
            .c_str() +
        "'.");
  }

  // Report the ResourceResponse now that the new Document has been created and
  // console messages will be properly displayed.
  frame_->Console().ReportResourceResponseReceived(
      this, main_resource_identifier_, response_);
}

void DocumentLoader::ApplyClientHintsConfig(
    const WebVector<network::mojom::WebClientHintsType>& enabled_client_hints) {
  for (auto ch : enabled_client_hints) {
    client_hints_preferences_.SetShouldSend(ch);
  }
}

void DocumentLoader::InitializePrefetchedSignedExchangeManager() {
  if (params_->prefetched_signed_exchanges.empty())
    return;
  // |prefetched_signed_exchanges| is set only when the page is loaded from a
  // signed exchange.
  DCHECK(GetResponse().IsSignedExchangeInnerResponse());
  // When the page is loaded from a signed exchange, |last_redirect| must be the
  // synthesized redirect for the signed exchange.
  DCHECK(params_->redirects.size());
  const WebNavigationParams::RedirectInfo& last_redirect =
      params_->redirects[params_->redirects.size() - 1];
  prefetched_signed_exchange_manager_ =
      PrefetchedSignedExchangeManager::MaybeCreate(
          GetFrame(),
          last_redirect.redirect_response.HttpHeaderField(http_names::kLink),
          GetResponse().HttpHeaderField(http_names::kLink),
          std::move(params_->prefetched_signed_exchanges));
}

PrefetchedSignedExchangeManager*
DocumentLoader::GetPrefetchedSignedExchangeManager() const {
  return prefetched_signed_exchange_manager_;
}

base::TimeDelta DocumentLoader::RemainingTimeToLCPLimit() const {
  // We shouldn't call this function before navigation start
  DCHECK(!document_load_timing_.NavigationStart().is_null());
  base::TimeTicks lcp_limit =
      document_load_timing_.NavigationStart() +
      base::Milliseconds(
          features::kAlignFontDisplayAutoTimeoutWithLCPGoalTimeoutParam.Get());
  base::TimeTicks now = clock_->NowTicks();
  if (now < lcp_limit)
    return lcp_limit - now;
  return base::TimeDelta();
}

mojom::blink::ContentSecurityNotifier&
DocumentLoader::GetContentSecurityNotifier() {
  if (!content_security_notifier_.is_bound()) {
    GetFrame()->Client()->GetBrowserInterfaceBroker().GetInterface(
        content_security_notifier_.BindNewPipeAndPassReceiver());
  }
  return *content_security_notifier_;
}

bool DocumentLoader::ConsumeTextFragmentToken() {
  bool token_value = has_text_fragment_token_;
  has_text_fragment_token_ = false;
  return token_value;
}

void DocumentLoader::NotifyPrerenderingDocumentActivated(
    const mojom::blink::PrerenderPageActivationParams& params) {
  DCHECK(!frame_->GetDocument()->IsPrerendering());
  DCHECK(is_prerendering_);
  is_prerendering_ = false;

  // A prerendered document won't have user activation, but when it gets moved
  // to the primary frame, the primary frame might have sticky user activation.
  // In that case, propagate the sticky user activation to the activated
  // prerendered document
  bool had_sticky_activation =
      params.was_user_activated == mojom::blink::WasActivatedOption::kYes;
  if (frame_->IsMainFrame() && had_sticky_activation) {
    DCHECK(!had_sticky_activation_);
    had_sticky_activation_ = had_sticky_activation;

    // Update Frame::had_sticky_user_activation_before_nav_. On regular
    // navigation, this is updated on DocumentLoader::CommitNavigation, but
    // that function is not called on prerender page activation.
    DCHECK(!frame_->HadStickyUserActivationBeforeNavigation());
    frame_->SetHadStickyUserActivationBeforeNavigation(had_sticky_activation);

    // Unlike CommitNavigation, there's no need to call
    // HadStickyUserActivationBeforeNavigationChanged here as the browser
    // process already knows it.
  }

  GetTiming().SetActivationStart(params.activation_start);
}

HashMap<KURL, EarlyHintsPreloadEntry>
DocumentLoader::GetEarlyHintsPreloadedResources() {
  return early_hints_preloaded_resources_;
}

bool DocumentLoader::IsReloadedOrFormSubmitted() const {
  switch (navigation_type_) {
    case WebNavigationType::kWebNavigationTypeReload:
    case WebNavigationType::kWebNavigationTypeFormSubmitted:
    case WebNavigationType::kWebNavigationTypeFormResubmitted:
      return true;
    default:
      return false;
  }
}

ContentSecurityPolicy* DocumentLoader::CreateCSP() {
  ContentSecurityPolicy* csp = MakeGarbageCollected<ContentSecurityPolicy>();

  if (GetFrame()->GetSettings()->GetBypassCSP())
    return csp;  // Empty CSP.

  // Add policies from the policy container. If this is a XSLT or javascript:
  // document, this will just keep the current policies. If this is a local
  // scheme document, the policy container contains the right policies (as
  // inherited in the NavigationRequest in the browser). If this is a network
  // scheme document, the policy container will contain the parsed CSP from the
  // response. If CSP Embedded Enforcement was used on this frame and the
  // response allowed blanket enforcement, the policy container includes the
  // enforced policy.
  csp->AddPolicies(
      mojo::Clone(policy_container_->GetPolicies().content_security_policies));

  // Check if the embedder wants to add any default policies, and add them.
  WebVector<WebContentSecurityPolicyHeader> embedder_default_csp;
  Platform::Current()->AppendContentSecurityPolicy(WebURL(Url()),
                                                   &embedder_default_csp);
  for (const auto& header : embedder_default_csp) {
    Vector<network::mojom::blink::ContentSecurityPolicyPtr>
        parsed_embedder_policies = ParseContentSecurityPolicies(
            header.header_value, header.type, header.source, Url());
    policy_container_->AddContentSecurityPolicies(
        mojo::Clone(parsed_embedder_policies));
    csp->AddPolicies(std::move(parsed_embedder_policies));
  }

  return csp;
}

bool DocumentLoader::UseIsolatedCodeCache() {
  return base::FeatureList::IsEnabled(kCacheInlineScriptCode) &&
         ShouldUseIsolatedCodeCache(mojom::blink::RequestContextType::HYPERLINK,
                                    response_);
}

bool& GetDisableCodeCacheForTesting() {
  static bool disable_code_cache_for_testing = false;
  return disable_code_cache_for_testing;
}

CodeCacheHost* DocumentLoader::GetCodeCacheHost() {
  if (!code_cache_host_) {
    if (GetDisableCodeCacheForTesting()) {
      return nullptr;
    }
    // TODO(crbug.com/1083097) When NavigationThreadingOptimizations feature is
    // enabled by default CodeCacheHost interface will be sent along with
    // CommitNavigation message and the following code would not be required and
    // we should just return nullptr here.
    mojo::Remote<mojom::CodeCacheHost> remote;
    GetLocalFrameClient().GetBrowserInterfaceBroker().GetInterface(
        remote.BindNewPipeAndPassReceiver());
    code_cache_host_ = std::make_unique<CodeCacheHost>(std::move(remote));
  }
  return code_cache_host_.get();
}

mojo::PendingRemote<blink::mojom::CodeCacheHost>
DocumentLoader::CreateWorkerCodeCacheHost() {
  if (GetDisableCodeCacheForTesting())
    return mojo::NullRemote();
  mojo::PendingRemote<blink::mojom::CodeCacheHost> pending_code_cache_host;
  GetLocalFrameClient().GetBrowserInterfaceBroker().GetInterface(
      pending_code_cache_host.InitWithNewPipeAndPassReceiver());
  return pending_code_cache_host;
}

void DocumentLoader::SetCodeCacheHost(
    mojo::PendingRemote<mojom::CodeCacheHost> code_cache_host) {
  code_cache_host_.reset();
  // When NavigationThreadingOptimizations feature is disabled, code_cache_host
  // can be a nullptr. When this feature is turned off the CodeCacheHost
  // interface is requested via BrowserBrokerInterface when required.
  if (code_cache_host) {
    code_cache_host_ = std::make_unique<CodeCacheHost>(
        mojo::Remote<mojom::CodeCacheHost>(std::move(code_cache_host)));
  }
}

void DocumentLoader::SetSubresourceFilter(
    WebDocumentSubresourceFilter* subresource_filter) {
  DCHECK(subresource_filter);
  subresource_filter_ = MakeGarbageCollected<SubresourceFilter>(
      frame_->DomWindow(), base::WrapUnique(subresource_filter));
}

WebDocumentLoader::ExtraData* DocumentLoader::GetExtraData() const {
  return extra_data_.get();
}

std::unique_ptr<WebDocumentLoader::ExtraData> DocumentLoader::TakeExtraData() {
  return std::move(extra_data_);
}

void DocumentLoader::SetExtraData(std::unique_ptr<ExtraData> extra_data) {
  extra_data_ = std::move(extra_data);
}

WebArchiveInfo DocumentLoader::GetArchiveInfo() const {
  if (archive_ &&
      archive_->LoadResult() == mojom::blink::MHTMLLoadResult::kSuccess) {
    return {
        archive_->LoadResult(),
        archive_->MainResource()->Url(),
        archive_->Date(),
    };
  }

  // TODO(arthursonzogni): Returning MHTMLLoadResult::kSuccess when there are no
  // archive is very misleading. Consider adding a new enum value to
  // discriminate success versus no archive.
  return {
      archive_ ? archive_->LoadResult()
               : mojom::blink::MHTMLLoadResult::kSuccess,
      WebURL(),
      base::Time(),
  };
}

// static
void DocumentLoader::DisableCodeCacheForTesting() {
  GetDisableCodeCacheForTesting() = true;
}

DEFINE_WEAK_IDENTIFIER_MAP(DocumentLoader)

}  // namespace blink
