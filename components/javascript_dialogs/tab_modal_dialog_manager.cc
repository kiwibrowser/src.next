// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/javascript_dialogs/tab_modal_dialog_manager.h"

#include <utility>

#include "base/bind.h"
#include "base/callback.h"
#include "base/feature_list.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/stringprintf.h"
#include "components/javascript_dialogs/app_modal_dialog_manager.h"
#include "components/javascript_dialogs/tab_modal_dialog_view.h"
#include "components/navigation_metrics/navigation_metrics.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "services/metrics/public/cpp/ukm_builders.h"
#include "services/metrics/public/cpp/ukm_recorder.h"
#include "ui/gfx/text_elider.h"
#include "url/origin.h"

namespace javascript_dialogs {

namespace {

AppModalDialogManager* GetAppModalDialogManager() {
  return AppModalDialogManager::GetInstance();
}

// The relationship between origins in displayed dialogs.
//
// This is used for a UMA histogram. Please never alter existing values, only
// append new ones.
//
// Note that "HTTP" in these enum names refers to a scheme that is either HTTP
// or HTTPS.
enum class DialogOriginRelationship {
  // The dialog was shown by a main frame with a non-HTTP(S) scheme, or by a
  // frame within a non-HTTP(S) main frame.
  NON_HTTP_MAIN_FRAME = 1,

  // The dialog was shown by a main frame with an HTTP(S) scheme.
  HTTP_MAIN_FRAME = 2,

  // The dialog was displayed by an HTTP(S) frame which shared the same origin
  // as the main frame.
  HTTP_MAIN_FRAME_HTTP_SAME_ORIGIN_ALERTING_FRAME = 3,

  // The dialog was displayed by an HTTP(S) frame which had a different origin
  // from the main frame.
  HTTP_MAIN_FRAME_HTTP_DIFFERENT_ORIGIN_ALERTING_FRAME = 4,

  // The dialog was displayed by a non-HTTP(S) frame whose nearest HTTP(S)
  // ancestor shared the same origin as the main frame.
  HTTP_MAIN_FRAME_NON_HTTP_ALERTING_FRAME_SAME_ORIGIN_ANCESTOR = 5,

  // The dialog was displayed by a non-HTTP(S) frame whose nearest HTTP(S)
  // ancestor was a different origin than the main frame.
  HTTP_MAIN_FRAME_NON_HTTP_ALERTING_FRAME_DIFFERENT_ORIGIN_ANCESTOR = 6,

  COUNT,
};

DialogOriginRelationship GetDialogOriginRelationship(
    content::WebContents* web_contents,
    content::RenderFrameHost* alerting_frame) {
  url::Origin main_frame_origin =
      web_contents->GetPrimaryMainFrame()->GetLastCommittedOrigin();

  if (!main_frame_origin.GetURL().SchemeIsHTTPOrHTTPS())
    return DialogOriginRelationship::NON_HTTP_MAIN_FRAME;

  if (alerting_frame == web_contents->GetPrimaryMainFrame())
    return DialogOriginRelationship::HTTP_MAIN_FRAME;

  url::Origin alerting_frame_origin = alerting_frame->GetLastCommittedOrigin();

  if (alerting_frame_origin.GetURL().SchemeIsHTTPOrHTTPS()) {
    if (main_frame_origin == alerting_frame_origin) {
      return DialogOriginRelationship::
          HTTP_MAIN_FRAME_HTTP_SAME_ORIGIN_ALERTING_FRAME;
    }
    return DialogOriginRelationship::
        HTTP_MAIN_FRAME_HTTP_DIFFERENT_ORIGIN_ALERTING_FRAME;
  }

  // Walk up the tree to find the nearest ancestor frame of the alerting frame
  // that has an HTTP(S) scheme. Note that this is guaranteed to terminate
  // because the main frame has an HTTP(S) scheme.
  content::RenderFrameHost* nearest_http_ancestor_frame =
      alerting_frame->GetParent();
  while (!nearest_http_ancestor_frame->GetLastCommittedOrigin()
              .GetURL()
              .SchemeIsHTTPOrHTTPS()) {
    nearest_http_ancestor_frame = nearest_http_ancestor_frame->GetParent();
  }

  url::Origin nearest_http_ancestor_frame_origin =
      nearest_http_ancestor_frame->GetLastCommittedOrigin();

  if (main_frame_origin == nearest_http_ancestor_frame_origin) {
    return DialogOriginRelationship::
        HTTP_MAIN_FRAME_NON_HTTP_ALERTING_FRAME_SAME_ORIGIN_ANCESTOR;
  }
  return DialogOriginRelationship::
      HTTP_MAIN_FRAME_NON_HTTP_ALERTING_FRAME_DIFFERENT_ORIGIN_ANCESTOR;
}

}  // namespace

TabModalDialogManager::~TabModalDialogManager() {
  CloseDialog(DismissalCause::kTabHelperDestroyed, false, std::u16string());
}

void TabModalDialogManager::BrowserActiveStateChanged() {
  if (delegate_->IsWebContentsForemost())
    OnVisibilityChanged(content::Visibility::VISIBLE);
  else
    HandleTabSwitchAway(DismissalCause::kBrowserSwitched);
}

void TabModalDialogManager::CloseDialogWithReason(DismissalCause reason) {
  CloseDialog(reason, false, std::u16string());
}

void TabModalDialogManager::SetDialogShownCallbackForTesting(
    base::OnceClosure callback) {
  dialog_shown_ = std::move(callback);
}

bool TabModalDialogManager::IsShowingDialogForTesting() const {
  return !!dialog_;
}

void TabModalDialogManager::ClickDialogButtonForTesting(
    bool accept,
    const std::u16string& user_input) {
  DCHECK(!!dialog_);
  CloseDialog(DismissalCause::kDialogButtonClicked, accept, user_input);
}

void TabModalDialogManager::SetDialogDismissedCallbackForTesting(
    DialogDismissedCallback callback) {
  dialog_dismissed_ = std::move(callback);
}

void TabModalDialogManager::RunJavaScriptDialog(
    content::WebContents* alerting_web_contents,
    content::RenderFrameHost* render_frame_host,
    content::JavaScriptDialogType dialog_type,
    const std::u16string& message_text,
    const std::u16string& default_prompt_text,
    DialogClosedCallback callback,
    bool* did_suppress_message) {
  DCHECK_EQ(alerting_web_contents,
            content::WebContents::FromRenderFrameHost(render_frame_host));

  content::WebContents* web_contents = WebContentsObserver::web_contents();
  DialogOriginRelationship origin_relationship =
      GetDialogOriginRelationship(alerting_web_contents, render_frame_host);
  navigation_metrics::Scheme scheme =
      navigation_metrics::GetScheme(render_frame_host->GetLastCommittedURL());
  switch (dialog_type) {
    case content::JAVASCRIPT_DIALOG_TYPE_ALERT:
      UMA_HISTOGRAM_ENUMERATION("JSDialogs.OriginRelationship.Alert",
                                origin_relationship,
                                DialogOriginRelationship::COUNT);
      UMA_HISTOGRAM_ENUMERATION("JSDialogs.Scheme.Alert", scheme,
                                navigation_metrics::Scheme::COUNT);
      break;
    case content::JAVASCRIPT_DIALOG_TYPE_CONFIRM:
      UMA_HISTOGRAM_ENUMERATION("JSDialogs.OriginRelationship.Confirm",
                                origin_relationship,
                                DialogOriginRelationship::COUNT);
      UMA_HISTOGRAM_ENUMERATION("JSDialogs.Scheme.Confirm", scheme,
                                navigation_metrics::Scheme::COUNT);
      break;
    case content::JAVASCRIPT_DIALOG_TYPE_PROMPT:
      UMA_HISTOGRAM_ENUMERATION("JSDialogs.OriginRelationship.Prompt",
                                origin_relationship,
                                DialogOriginRelationship::COUNT);
      UMA_HISTOGRAM_ENUMERATION("JSDialogs.Scheme.Prompt", scheme,
                                navigation_metrics::Scheme::COUNT);
      break;
  }

  // Close any dialog already showing.
  CloseDialog(DismissalCause::kSubsequentDialogShown, false, std::u16string());

  bool make_pending = false;
  if (!delegate_->IsWebContentsForemost() &&
      !content::DevToolsAgentHost::IsDebuggerAttached(web_contents)) {
    static const char kDialogSuppressedConsoleMessageFormat[] =
        "A window.%s() dialog generated by this page was suppressed "
        "because this page is not the active tab of the front window. "
        "Please make sure your dialogs are triggered by user interactions "
        "to avoid this situation. https://www.chromestatus.com/feature/%s";

    switch (dialog_type) {
      case content::JAVASCRIPT_DIALOG_TYPE_ALERT: {
        // When an alert fires in the background, make the callback so that the
        // render process can continue.
        std::move(callback).Run(true, std::u16string());
        callback.Reset();

        delegate_->SetTabNeedsAttention(true);

        make_pending = true;
        break;
      }
      case content::JAVASCRIPT_DIALOG_TYPE_CONFIRM: {
        *did_suppress_message = true;
        render_frame_host->AddMessageToConsole(
            blink::mojom::ConsoleMessageLevel::kWarning,
            base::StringPrintf(kDialogSuppressedConsoleMessageFormat, "confirm",
                               "5140698722467840"));
        return;
      }
      case content::JAVASCRIPT_DIALOG_TYPE_PROMPT: {
        *did_suppress_message = true;
        render_frame_host->AddMessageToConsole(
            blink::mojom::ConsoleMessageLevel::kWarning,
            base::StringPrintf(kDialogSuppressedConsoleMessageFormat, "prompt",
                               "5637107137642496"));
        return;
      }
    }
  }

  // Enforce sane sizes. ElideRectangleString breaks horizontally, which isn't
  // strictly needed, but it restricts the vertical size, which is crucial.
  // This gives about 2000 characters, which is about the same as the
  // AppModalDialogManager provides, but allows no more than 24 lines.
  const int kMessageTextMaxRows = 24;
  const int kMessageTextMaxCols = 80;
  const size_t kDefaultPromptMaxSize = 2000;
  std::u16string truncated_message_text;
  gfx::ElideRectangleString(message_text, kMessageTextMaxRows,
                            kMessageTextMaxCols, false,
                            &truncated_message_text);
  std::u16string truncated_default_prompt_text;
  gfx::ElideString(default_prompt_text, kDefaultPromptMaxSize,
                   &truncated_default_prompt_text);

  std::u16string title = GetAppModalDialogManager()->GetTitle(
      alerting_web_contents, render_frame_host->GetLastCommittedOrigin());
  dialog_callback_ = std::move(callback);
  dialog_type_ = dialog_type;
  if (make_pending) {
    DCHECK(!dialog_);
    pending_dialog_ = base::BindOnce(
        &TabModalDialogManagerDelegate::CreateNewDialog,
        base::Unretained(delegate_.get()), alerting_web_contents, title,
        dialog_type, truncated_message_text, truncated_default_prompt_text,
        base::BindOnce(&TabModalDialogManager::CloseDialog,
                       base::Unretained(this),
                       DismissalCause::kDialogButtonClicked),
        base::BindOnce(&TabModalDialogManager::CloseDialog,
                       base::Unretained(this), DismissalCause::kDialogClosed,
                       false, std::u16string()));
  } else {
    DCHECK(!pending_dialog_);
    dialog_ = delegate_->CreateNewDialog(
        alerting_web_contents, title, dialog_type, truncated_message_text,
        truncated_default_prompt_text,
        base::BindOnce(&TabModalDialogManager::CloseDialog,
                       base::Unretained(this),
                       DismissalCause::kDialogButtonClicked),
        base::BindOnce(&TabModalDialogManager::CloseDialog,
                       base::Unretained(this), DismissalCause::kDialogClosed,
                       false, std::u16string()));
  }

  delegate_->WillRunDialog();

  // Message suppression is something that we don't give the user a checkbox
  // for any more. It was useful back in the day when dialogs were app-modal
  // and clicking the checkbox was the only way to escape a loop that the page
  // was doing, but now the user can just close the page.
  *did_suppress_message = false;

  if (!dialog_shown_.is_null())
    std::move(dialog_shown_).Run();
}

void TabModalDialogManager::RunBeforeUnloadDialog(
    content::WebContents* web_contents,
    content::RenderFrameHost* render_frame_host,
    bool is_reload,
    DialogClosedCallback callback) {
  DCHECK_EQ(web_contents,
            content::WebContents::FromRenderFrameHost(render_frame_host));

  DialogOriginRelationship origin_relationship =
      GetDialogOriginRelationship(web_contents, render_frame_host);
  navigation_metrics::Scheme scheme =
      navigation_metrics::GetScheme(render_frame_host->GetLastCommittedURL());
  UMA_HISTOGRAM_ENUMERATION("JSDialogs.OriginRelationship.BeforeUnload",
                            origin_relationship,
                            DialogOriginRelationship::COUNT);
  UMA_HISTOGRAM_ENUMERATION("JSDialogs.Scheme.BeforeUnload", scheme,
                            navigation_metrics::Scheme::COUNT);

  // onbeforeunload dialogs are always handled with an app-modal dialog, because
  // - they are critical to the user not losing data
  // - they can be requested for tabs that are not foremost
  // - they can be requested for many tabs at the same time
  // and therefore auto-dismissal is inappropriate for them.

  return GetAppModalDialogManager()->RunBeforeUnloadDialogWithOptions(
      web_contents, render_frame_host, is_reload, delegate_->IsApp(),
      std::move(callback));
}

bool TabModalDialogManager::HandleJavaScriptDialog(
    content::WebContents* web_contents,
    bool accept,
    const std::u16string* prompt_override) {
  if (dialog_ || pending_dialog_) {
    CloseDialog(DismissalCause::kHandleDialogCalled, accept,
                prompt_override ? *prompt_override : dialog_->GetUserInput());
    return true;
  }

  // Handle any app-modal dialogs being run by the app-modal dialog system.
  return GetAppModalDialogManager()->HandleJavaScriptDialog(
      web_contents, accept, prompt_override);
}

void TabModalDialogManager::CancelDialogs(content::WebContents* web_contents,
                                          bool reset_state) {
  CloseDialog(DismissalCause::kCancelDialogsCalled, false, std::u16string());

  // Cancel any app-modal dialogs being run by the app-modal dialog system.
  return GetAppModalDialogManager()->CancelDialogs(web_contents, reset_state);
}

void TabModalDialogManager::OnVisibilityChanged(
    content::Visibility visibility) {
  if (visibility == content::Visibility::HIDDEN) {
    HandleTabSwitchAway(DismissalCause::kTabHidden);
  } else if (pending_dialog_) {
    dialog_ = std::move(pending_dialog_).Run();
    pending_dialog_.Reset();
    delegate_->SetTabNeedsAttention(false);
  }
}

void TabModalDialogManager::DidStartNavigation(
    content::NavigationHandle* navigation_handle) {
  if (!navigation_handle->IsInPrimaryMainFrame())
    return;

  // Close the dialog if the user started a new navigation. This allows reloads
  // and history navigations to proceed.
  CloseDialog(DismissalCause::kTabNavigated, false, std::u16string());
}

TabModalDialogManager::TabModalDialogManager(
    content::WebContents* web_contents,
    std::unique_ptr<TabModalDialogManagerDelegate> delegate)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<TabModalDialogManager>(*web_contents),
      delegate_(std::move(delegate)) {}

void TabModalDialogManager::LogDialogDismissalCause(DismissalCause cause) {
  if (dialog_dismissed_)
    std::move(dialog_dismissed_).Run(cause);

  // Log to UKM.
  //
  // Note that this will return the outermost WebContents, not necessarily the
  // WebContents that had the alert call in it. For 99.9999% of cases they're
  // the same, but for instances like the <webview> tag in extensions and PDF
  // files that alert they may differ.
  ukm::SourceId source_id = WebContentsObserver::web_contents()
                                ->GetPrimaryMainFrame()
                                ->GetPageUkmSourceId();
  if (source_id != ukm::kInvalidSourceId) {
    ukm::builders::AbusiveExperienceHeuristic_JavaScriptDialog(source_id)
        .SetDismissalCause(static_cast<int64_t>(cause))
        .Record(ukm::UkmRecorder::Get());
  }
}

void TabModalDialogManager::HandleTabSwitchAway(DismissalCause cause) {
  if (!dialog_ || content::DevToolsAgentHost::IsDebuggerAttached(
                      WebContentsObserver::web_contents())) {
    return;
  }

  if (dialog_type_ == content::JAVASCRIPT_DIALOG_TYPE_ALERT) {
    // When the user switches tabs, make the callback so that the render process
    // can continue.
    if (dialog_callback_) {
      std::move(dialog_callback_).Run(true, std::u16string());
      dialog_callback_.Reset();
    }
  } else {
    CloseDialog(cause, false, std::u16string());
  }
}

void TabModalDialogManager::CloseDialog(DismissalCause cause,
                                        bool success,
                                        const std::u16string& user_input) {
  if (!dialog_ && !pending_dialog_)
    return;

  LogDialogDismissalCause(cause);

  // CloseDialog() can be called two ways. It can be called from within
  // TabModalDialogManager, in which case the dialog needs to be closed.
  // However, it can also be called, bound, from the JavaScriptDialog. In that
  // case, the dialog is already closing, so the JavaScriptDialog doesn't need
  // to be told to close.
  //
  // Using the |cause| to distinguish a call from JavaScriptDialog vs from
  // within TabModalDialogManager is a bit hacky, but is the simplest way.
  if (dialog_ && cause != DismissalCause::kDialogButtonClicked &&
      cause != DismissalCause::kDialogClosed)
    dialog_->CloseDialogWithoutCallback();

  // If there is a callback, call it. There might not be one, if a tab-modal
  // alert() dialog is showing.
  if (dialog_callback_)
    std::move(dialog_callback_).Run(success, user_input);

  // If there's a pending dialog, then the tab is still in the "needs attention"
  // state; clear it out. However, if the tab was switched out, the turning off
  // of the "needs attention" state was done in OnTabStripModelChanged()
  // SetTabNeedsAttention won't work, so don't call it.
  if (pending_dialog_ && cause != DismissalCause::kTabSwitchedOut &&
      cause != DismissalCause::kTabHelperDestroyed) {
    delegate_->SetTabNeedsAttention(false);
  }

  dialog_.reset();
  pending_dialog_.Reset();
  dialog_callback_.Reset();

  delegate_->DidCloseDialog();
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(TabModalDialogManager);

}  // namespace javascript_dialogs
