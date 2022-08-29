/*
 * Copyright (C) 2006, 2007, 2008, 2009, 2011 Apple Inc. All rights reserved.
 * Copyright (C) 2008, 2009 Torch Mobile Inc. All rights reserved.
 * (http://www.torchmobile.com/)
 * Copyright (C) Research In Motion Limited 2009. All rights reserved.
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

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_LOADER_FRAME_LOADER_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_LOADER_FRAME_LOADER_H_

#include <memory>

#include "base/callback_helpers.h"
#include "services/network/public/mojom/web_sandbox_flags.mojom-blink-forward.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/public/common/user_agent/user_agent_metadata.h"
#include "third_party/blink/public/mojom/loader/request_context_frame_type.mojom-blink-forward.h"
#include "third_party/blink/public/mojom/page_state/page_state.mojom-blink-forward.h"
#include "third_party/blink/public/platform/scheduler/web_scoped_virtual_time_pauser.h"
#include "third_party/blink/public/web/web_document_loader.h"
#include "third_party/blink/public/web/web_frame_load_type.h"
#include "third_party/blink/public/web/web_navigation_type.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/frame/frame_types.h"
#include "third_party/blink/renderer/core/loader/frame_loader_types.h"
#include "third_party/blink/renderer/core/loader/history_item.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/loader/fetch/loader_freeze_mode.h"
#include "third_party/blink/renderer/platform/wtf/forward.h"
#include "third_party/blink/renderer/platform/wtf/hash_set.h"

namespace blink {

class DocumentLoader;
class FetchClientSettingsObject;
class Frame;
class LocalFrame;
class LocalFrameClient;
class PolicyContainer;
class ProgressTracker;
class ResourceRequest;
class TracedValue;
struct FrameLoadRequest;
struct UnloadEventTimingInfo;
struct WebNavigationInfo;
struct WebNavigationParams;

CORE_EXPORT bool IsBackForwardLoadType(WebFrameLoadType);
CORE_EXPORT bool IsReloadLoadType(WebFrameLoadType);

class CORE_EXPORT FrameLoader final {
  DISALLOW_NEW();

 public:
  explicit FrameLoader(LocalFrame*);
  FrameLoader(const FrameLoader&) = delete;
  FrameLoader& operator=(const FrameLoader&) = delete;
  ~FrameLoader();

  void Init(std::unique_ptr<PolicyContainer> policy_container);

  ResourceRequest ResourceRequestForReload(
      WebFrameLoadType,
      ClientRedirectPolicy = ClientRedirectPolicy::kNotClientRedirect);

  ProgressTracker& Progress() const { return *progress_tracker_; }

  // This is the entry-point for all renderer-initiated navigations except
  // history traversals. It will eventually send the navigation to the browser
  // process, or call DocumentLoader::CommitSameDocumentNavigation for
  // same-document navigation. For reloads, an appropriate WebFrameLoadType
  // should be given. Otherwise, kStandard should be used (and the final
  // WebFrameLoadType will be computed).
  void StartNavigation(FrameLoadRequest&,
                       WebFrameLoadType = WebFrameLoadType::kStandard);

  // Called when the browser process has asked this renderer process to commit
  // a navigation in this frame. This method skips most of the checks assuming
  // that browser process has already performed any checks necessary.
  // See WebNavigationParams for details.
  void CommitNavigation(
      std::unique_ptr<WebNavigationParams> navigation_params,
      std::unique_ptr<WebDocumentLoader::ExtraData> extra_data,
      CommitReason = CommitReason::kRegular);

  // Called before the browser process is asked to navigate this frame, to mark
  // the frame as loading and save some navigation information for later use.
  bool WillStartNavigation(const WebNavigationInfo& info);

  // This runs the "stop document loading" algorithm in HTML:
  // https://html.spec.whatwg.org/C/browsing-the-web.html#stop-document-loading
  // Note, this function only cancels ongoing navigation handled through
  // FrameLoader.
  //
  // If |abort_client| is true, then the frame's client will have
  // AbortClientNavigation() called if a navigation was aborted. Normally this
  // should be passed as true, unless the navigation has been migrated to a
  // provisional frame, while this frame is going away, so the navigation isn't
  // actually being aborted.
  //
  // Warning: StopAllLoaders() may detach the LocalFrame to which this
  // FrameLoader belongs. Callers need to be careful about checking the
  // existence of the frame after StopAllLoaders() returns.
  void StopAllLoaders(bool abort_client);

  // Notifies the client that the initial empty document has been accessed, and
  // thus it is no longer safe to show a provisional URL above the document
  // without risking a URL spoof. The client must not call back into JavaScript.
  void DidAccessInitialDocument();

  DocumentLoader* GetDocumentLoader() const { return document_loader_.Get(); }

  void SetDefersLoading(LoaderFreezeMode mode);

  void DidExplicitOpen();

  String UserAgent() const;
  String FullUserAgent() const;
  String ReducedUserAgent() const;
  absl::optional<blink::UserAgentMetadata> UserAgentMetadata() const;

  void DispatchDidClearWindowObjectInMainWorld();
  void DispatchDidClearDocumentOfWindowObject();
  void DispatchDocumentElementAvailable();
  void RunScriptsAtDocumentElementAvailable();

  // See content/browser/renderer_host/sandbox_flags.md
  // This contains the sandbox flags to commit for new documents.
  // - For main documents, it contains the sandbox inherited from the opener.
  // - For nested documents, it contains the sandbox flags inherited from the
  //   parent and the one defined in the <iframe>'s sandbox attribute.
  network::mojom::blink::WebSandboxFlags PendingEffectiveSandboxFlags() const;

  // Modifying itself is done based on |fetch_client_settings_object|.
  // |document_for_logging| is used only for logging, use counters,
  // UKM-related things.
  void ModifyRequestForCSP(
      ResourceRequest&,
      const FetchClientSettingsObject* fetch_client_settings_object,
      LocalDOMWindow* window_for_logging,
      mojom::RequestContextFrameType) const;
  void ReportLegacyTLSVersion(const KURL& url,
                              bool is_subresource,
                              bool is_ad_resource);

  Frame* Opener();
  void SetOpener(LocalFrame*);

  void Detach();

  void FinishedParsing();
  enum class NavigationFinishState { kSuccess, kFailure };
  void DidFinishNavigation(NavigationFinishState);

  void ProcessScrollForSameDocumentNavigation(
      const KURL&,
      WebFrameLoadType,
      absl::optional<HistoryItem::ViewState>,
      mojom::blink::ScrollRestorationType);

  // This will attempt to detach the current document. It will dispatch unload
  // events and abort XHR requests. Returns true if the frame is ready to
  // receive the next document commit, or false otherwise.
  bool DetachDocument();

  bool ShouldClose(bool is_reload = false);

  // Dispatches the Unload event for the current document and fills in this
  // document's info in OldDocumentInfoForCommit if
  // `will_commit_new_document_in_this_frame` is true (which will only be
  // the case when the current document in this frame is being unloaded for
  // committing a new document).
  void DispatchUnloadEventAndFillOldDocumentInfoIfNeeded(
      bool will_commit_new_document_in_this_frame);

  bool AllowPlugins();

  void SaveScrollAnchor();
  void SaveScrollState();
  void RestoreScrollPositionAndViewState();

  bool HasProvisionalNavigation() const {
    return committing_navigation_ || client_navigation_.get();
  }

  // Like ClearClientNavigation, but also notifies the client to actually cancel
  // the navigation.
  void CancelClientNavigation(
      CancelNavigationReason reason = CancelNavigationReason::kOther);

  void Trace(Visitor*) const;

  void DidDropNavigation();

  bool HasAccessedInitialDocument() { return has_accessed_initial_document_; }

  void SetIsNotOnInitialEmptyDocument() {
    // The "initial empty document" state can be false if the frame has loaded
    // a non-initial/synchronous about:blank document, or if the document has
    // done a document.open() before. However, this function can only be called
    // when a frame is first re-created in a new renderer, which can only be
    // caused by a new document load. So, we know that the state must be set to
    // kNotInitialOrSynchronousAboutBlank instead of
    // kInitialOrSynchronousAboutBlankButExplicitlyOpened here.
    initial_empty_document_status_ =
        InitialEmptyDocumentStatus::kNotInitialOrSynchronousAboutBlank;
  }

  // Whether the frame's current document is still considered as the "initial
  // empty document" or not. Might be false even when
  // HasLoadedNonInitialEmptyDocument() is false, if the frame is still on the
  // first about:blank document that loaded in the frame, but it has done
  // a document.open(), causing it to lose its "initial empty document"-ness
  // even though it's still on the same document.
  bool IsOnInitialEmptyDocument() {
    return initial_empty_document_status_ ==
           InitialEmptyDocumentStatus::kInitialOrSynchronousAboutBlank;
  }

  // Whether the frame has loaded a document that is not the initial empty
  // document. Might be false even when IsOnInitialEmptyDocument() is false (see
  // comment for IsOnInitialEmptyDocument() for details).
  bool HasLoadedNonInitialEmptyDocument() {
    return initial_empty_document_status_ ==
           InitialEmptyDocumentStatus::kNotInitialOrSynchronousAboutBlank;
  }

  static bool NeedsHistoryItemRestore(WebFrameLoadType type);

  void WriteIntoTrace(perfetto::TracedValue context) const;

  mojo::PendingRemote<blink::mojom::CodeCacheHost> CreateWorkerCodeCacheHost();

  // Contains information related to the previous document in the frame, to be
  // given to the next document that is going to commit in this FrameLoader.
  // Note that the "previous document" might not necessarily use the same
  // FrameLoader as this one, e.g. in case of local RenderFrame swap.
  struct OldDocumentInfoForCommit : GarbageCollected<OldDocumentInfoForCommit> {
    explicit OldDocumentInfoForCommit(
        scoped_refptr<SecurityOrigin> new_document_origin);
    void Trace(Visitor* visitor) const;
    // The unload timing info of the previous document in the frame. The new
    // document can access this information if it is a same-origin, to be
    // exposed through the Navigation Timing API.
    UnloadEventTimingInfo unload_timing_info;
    // The HistoryItem of the previous document in the frame. Some of the state
    // from the old document's HistoryItem will be copied to the new document
    // e.g. history.state will be copied on same-URL navigations. See also
    // https://github.com/whatwg/html/issues/6213.
    Member<HistoryItem> history_item;
  };

 private:
  bool AllowRequestForThisFrame(const FrameLoadRequest&);

  WebFrameLoadType HandleInitialEmptyDocumentReplacementIfNeeded(
      const KURL& url,
      WebFrameLoadType);

  bool ShouldPerformFragmentNavigation(bool is_form_submission,
                                       const String& http_method,
                                       WebFrameLoadType,
                                       const KURL&);
  void ProcessFragment(const KURL&, WebFrameLoadType, LoadStartType);

  // Returns whether we should continue with new navigation.
  bool CancelProvisionalLoaderForNewNavigation();

  // Clears any information about client navigation, see client_navigation_.
  void ClearClientNavigation();

  void RestoreScrollPositionAndViewState(WebFrameLoadType,
                                         const HistoryItem::ViewState&,
                                         mojom::blink::ScrollRestorationType);

  void DetachDocumentLoader(Member<DocumentLoader>&,
                            bool flush_microtask_queue = false);

  void TakeObjectSnapshot() const;

  // Commits the given |document_loader|.
  void CommitDocumentLoader(DocumentLoader* document_loader,
                            HistoryItem* previous_history_item,
                            CommitReason);

  LocalFrameClient* Client() const;

  String ApplyUserAgentOverrideAndLog(const String& user_agent) const;

  Member<LocalFrame> frame_;

  Member<ProgressTracker> progress_tracker_;

  // Document loader for frame loading.
  Member<DocumentLoader> document_loader_;

  // This struct holds information about a navigation, which is being
  // initiated by the client through the browser process, until the navigation
  // is either committed or cancelled.
  struct ClientNavigationState {
    KURL url;
  };
  std::unique_ptr<ClientNavigationState> client_navigation_;

  // The state is set to kInitialized when Init() completes, and kDetached
  // during teardown in Detach().
  enum class State { kUninitialized, kInitialized, kDetached };
  State state_ = State::kUninitialized;

  bool dispatching_did_clear_window_object_in_main_world_;
  bool committing_navigation_ = false;
  bool has_accessed_initial_document_ = false;

  // Enum to determine the frame's "initial empty document"-ness.
  // NOTE: we treat both the "initial about:blank document" and the
  // "synchronously committed about:blank document" as the initial empty
  // document. In the future, we plan to remove the synchronous about:blank
  // commit so that this enum only considers the true "initial about:blank"
  // document. See also:
  // - https://github.com/whatwg/html/issues/6863
  // - https://crbug.com/1215096
  enum class InitialEmptyDocumentStatus {
    // The document is the initial about:blank document or the synchronously
    // committed about:blank document.
    kInitialOrSynchronousAboutBlank,
    // The document is the initial about:blank document or the synchronously
    // committed about:blank document, but the document's input stream has been
    // opened with document.open(), so the document lost its "initial empty
    // document" status, per the spec:
    // https://html.spec.whatwg.org/multipage/dynamic-markup-insertion.html#opening-the-input-stream:is-initial-about:blank
    kInitialOrSynchronousAboutBlankButExplicitlyOpened,
    // The document is neither the initial about:blank document nor the
    // synchronously committed about:blank document.
    kNotInitialOrSynchronousAboutBlank
  };
  InitialEmptyDocumentStatus initial_empty_document_status_ =
      InitialEmptyDocumentStatus::kInitialOrSynchronousAboutBlank;

  WebScopedVirtualTimePauser virtual_time_pauser_;

  // The origins for which a legacy TLS version warning has been printed. The
  // size of this set is capped, after which no more warnings are printed.
  HashSet<String> tls_version_warning_origins_;

  // Owns the OldDocumentInfoForCommit and exposes it through `info_`
  // so that both the unloading old document and the committing new document
  // can access and modify the value, without explicitly passing it between
  // them on unload/commit time.
  class ScopedOldDocumentInfoForCommitCapturer {
    STACK_ALLOCATED();

   public:
    explicit ScopedOldDocumentInfoForCommitCapturer(
        OldDocumentInfoForCommit* info)
        : info_(info), previous_capturer_(current_capturer_) {
      current_capturer_ = this;
    }

    ~ScopedOldDocumentInfoForCommitCapturer();

    // The last OldDocumentInfoForCommit set for `info_` that is still in scope.
    static OldDocumentInfoForCommit* CurrentInfo() {
      return current_capturer_ ? current_capturer_->info_ : nullptr;
    }

   private:
    OldDocumentInfoForCommit* info_;
    ScopedOldDocumentInfoForCommitCapturer* previous_capturer_;
    static ScopedOldDocumentInfoForCommitCapturer* current_capturer_;
  };
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_LOADER_FRAME_LOADER_H_
