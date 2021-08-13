/*
 * Copyright (C) 2006, 2007, 2008, 2010 Apple Inc. All rights reserved.
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "third_party/blink/renderer/core/page/create_window.h"

#include "base/feature_list.h"
#include "base/metrics/histogram_macros.h"
#include "services/metrics/public/cpp/ukm_builders.h"
#include "third_party/blink/public/common/dom_storage/session_storage_namespace_id.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/common/frame/from_ad_state.h"
#include "third_party/blink/public/mojom/loader/request_context_frame_type.mojom-blink.h"
#include "third_party/blink/public/web/web_view_client.h"
#include "third_party/blink/public/web/web_window_features.h"
#include "third_party/blink/renderer/core/core_initializer.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/exported/web_view_impl.h"
#include "third_party/blink/renderer/core/frame/ad_tracker.h"
#include "third_party/blink/renderer/core/frame/csp/content_security_policy.h"
#include "third_party/blink/renderer/core/frame/frame_client.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/local_frame_client.h"
#include "third_party/blink/renderer/core/html/conversion_measurement_parsing.h"
#include "third_party/blink/renderer/core/inspector/console_message.h"
#include "third_party/blink/renderer/core/loader/frame_load_request.h"
#include "third_party/blink/renderer/core/page/chrome_client.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "third_party/blink/renderer/core/probe/core_probes.h"
#include "third_party/blink/renderer/platform/heap/heap.h"
#include "third_party/blink/renderer/platform/loader/fetch/resource_request.h"
#include "third_party/blink/renderer/platform/weborigin/kurl.h"
#include "third_party/blink/renderer/platform/wtf/text/number_parsing_options.h"
#include "third_party/blink/renderer/platform/wtf/text/string_to_number.h"
#include "third_party/blink/renderer/platform/wtf/text/string_view.h"

namespace blink {

// Though isspace() considers \t and \v to be whitespace, Win IE doesn't when
// parsing window features.
static bool IsWindowFeaturesSeparator(UChar c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '=' ||
         c == ',' || c == '\f';
}

WebWindowFeatures GetWindowFeaturesFromString(const String& feature_string,
                                              LocalDOMWindow* dom_window) {
  WebWindowFeatures window_features;

  ImpressionFeatures impression_features;
  bool conversion_measurement_enabled =
      dom_window &&
      RuntimeEnabledFeatures::ConversionMeasurementEnabled(dom_window);

  // This code follows the HTML spec, specifically
  // https://html.spec.whatwg.org/C/#concept-window-open-features-tokenize
  if (feature_string.IsEmpty())
    return window_features;

  bool ui_features_were_disabled = false;

  // See crbug.com/1192701 for details, but we're working on changing the
  // popup-triggering conditions for window.open. This bool represents the "new"
  // state after this change.
  bool is_popup_with_new_behavior = false;

  unsigned key_begin, key_end;
  unsigned value_begin, value_end;

  String buffer = feature_string.LowerASCII();
  unsigned length = buffer.length();
  for (unsigned i = 0; i < length;) {
    // skip to first non-separator (start of key name), but don't skip
    // past the end of the string
    while (i < length && IsWindowFeaturesSeparator(buffer[i]))
      i++;
    key_begin = i;

    // skip to first separator (end of key name), but don't skip past
    // the end of the string
    while (i < length && !IsWindowFeaturesSeparator(buffer[i]))
      i++;
    key_end = i;

    SECURITY_DCHECK(i <= length);

    // skip separators past the key name, except '=', and don't skip past
    // the end of the string
    while (i < length && buffer[i] != '=') {
      if (buffer[i] == ',' || !IsWindowFeaturesSeparator(buffer[i]))
        break;

      i++;
    }

    if (i < length && IsWindowFeaturesSeparator(buffer[i])) {
      // skip to first non-separator (start of value), but don't skip
      // past a ',' or the end of the string.
      while (i < length && IsWindowFeaturesSeparator(buffer[i])) {
        if (buffer[i] == ',')
          break;

        i++;
      }

      value_begin = i;

      SECURITY_DCHECK(i <= length);

      // skip to first separator (end of value)
      while (i < length && !IsWindowFeaturesSeparator(buffer[i]))
        i++;

      value_end = i;

      SECURITY_DCHECK(i <= length);
    } else {
      // No value given.
      value_begin = i;
      value_end = i;
    }

    if (key_begin == key_end)
      continue;

    StringView key_string(buffer, key_begin, key_end - key_begin);
    StringView value_string(buffer, value_begin, value_end - value_begin);

    // Listing a key with no value is shorthand for key=yes
    int value;
    if (value_string.IsEmpty() || value_string == "yes") {
      value = 1;
    } else if (value_string.Is8Bit()) {
      value = CharactersToInt(value_string.Characters8(), value_string.length(),
                              WTF::NumberParsingOptions::kLoose, nullptr);
    } else {
      value =
          CharactersToInt(value_string.Characters16(), value_string.length(),
                          WTF::NumberParsingOptions::kLoose, nullptr);
    }

    if (!ui_features_were_disabled && key_string != "noopener" &&
        key_string != "noreferrer" &&
        (!conversion_measurement_enabled ||
         (key_string != "attributionsourceeventid" &&
          key_string != "attributiondestination" &&
          key_string != "attributionreportto" &&
          key_string != "attributionexpiry" &&
          key_string != "attributionsourcepriority"))) {
      ui_features_were_disabled = true;
      window_features.menu_bar_visible = false;
      window_features.status_bar_visible = false;
      window_features.tool_bar_visible = false;
      window_features.scrollbars_visible = false;
    }

    if (key_string == "left" || key_string == "screenx") {
      window_features.x_set = true;
      window_features.x = value;
    } else if (key_string == "top" || key_string == "screeny") {
      window_features.y_set = true;
      window_features.y = value;
    } else if (key_string == "width" || key_string == "innerwidth") {
      window_features.width_set = true;
      window_features.width = value;
      // Width will be the only trigger for a popup.
      is_popup_with_new_behavior = true;
    } else if (key_string == "height" || key_string == "innerheight") {
      window_features.height_set = true;
      window_features.height = value;
    } else if (key_string == "menubar") {
      window_features.menu_bar_visible = value;
    } else if (key_string == "toolbar" || key_string == "location") {
      window_features.tool_bar_visible |= static_cast<bool>(value);
    } else if (key_string == "status") {
      window_features.status_bar_visible = value;
    } else if (key_string == "scrollbars") {
      window_features.scrollbars_visible = value;
    } else if (key_string == "resizable") {
      window_features.resizable = value;
    } else if (key_string == "noopener") {
      window_features.noopener = value;
    } else if (key_string == "noreferrer") {
      window_features.noreferrer = value;
    } else if (key_string == "background") {
      window_features.background = true;
    } else if (key_string == "persistent") {
      window_features.persistent = true;
    } else if (conversion_measurement_enabled) {
      if (key_string == "attributionsourceeventid") {
        impression_features.impression_data = value_string.ToString();
      } else if (key_string == "attributiondestination") {
        impression_features.conversion_destination = value_string.ToString();
      } else if (key_string == "attributionreportto") {
        impression_features.reporting_origin = value_string.ToString();
      } else if (key_string == "attributionexpiry") {
        impression_features.expiry = value_string.ToString();
      } else if (key_string == "attributionsourcepriority") {
        impression_features.priority = value_string.ToString();
      }
    }
  }

  // Existing logic from NavigationPolicy::NavigationPolicyForCreateWindow():
  if (dom_window && dom_window->document()) {
    bool is_popup_with_current_behavior = !window_features.tool_bar_visible ||
                                          !window_features.status_bar_visible ||
                                          !window_features.scrollbars_visible ||
                                          !window_features.menu_bar_visible ||
                                          !window_features.resizable;
    if (is_popup_with_current_behavior != is_popup_with_new_behavior) {
      UseCounter::Count(dom_window->document(),
                        WebFeature::kWindowOpenNewPopupBehaviorMismatch);
    }
  }

  if (window_features.noreferrer)
    window_features.noopener = true;

  if (conversion_measurement_enabled) {
    window_features.impression =
        GetImpressionFromWindowFeatures(dom_window, impression_features);
  }

  return window_features;
}

static void MaybeLogWindowOpen(LocalFrame& opener_frame) {
  AdTracker* ad_tracker = opener_frame.GetAdTracker();
  if (!ad_tracker)
    return;

  bool is_ad_subframe = opener_frame.IsAdSubframe();
  bool is_ad_script_in_stack =
      ad_tracker->IsAdScriptInStack(AdTracker::StackType::kBottomAndTop);
  FromAdState state =
      blink::GetFromAdState(is_ad_subframe, is_ad_script_in_stack);

  // Log to UMA.
  UMA_HISTOGRAM_ENUMERATION("Blink.WindowOpen.FromAdState", state);

  // Log to UKM.
  ukm::UkmRecorder* ukm_recorder = opener_frame.GetDocument()->UkmRecorder();
  ukm::SourceId source_id = opener_frame.GetDocument()->UkmSourceID();
  if (source_id != ukm::kInvalidSourceId) {
    ukm::builders::AbusiveExperienceHeuristic_WindowOpen(source_id)
        .SetFromAdSubframe(is_ad_subframe)
        .SetFromAdScript(is_ad_script_in_stack)
        .Record(ukm_recorder);
  }
}

Frame* CreateNewWindow(LocalFrame& opener_frame,
                       FrameLoadRequest& request,
                       const AtomicString& frame_name) {
  LocalDOMWindow& opener_window = *opener_frame.DomWindow();
  DCHECK(request.GetResourceRequest().RequestorOrigin() ||
         opener_window.Url().IsEmpty());
  DCHECK_EQ(kNavigationPolicyCurrentTab, request.GetNavigationPolicy());

  if (opener_window.document()->PageDismissalEventBeingDispatched() !=
      Document::kNoDismissal) {
    return nullptr;
  }

  request.SetFrameType(mojom::RequestContextFrameType::kAuxiliary);

  const KURL& url = request.GetResourceRequest().Url();
  auto* csp_for_world = opener_window.GetContentSecurityPolicyForCurrentWorld();
  if (url.ProtocolIsJavaScript() && csp_for_world) {
    String script_source = DecodeURLEscapeSequences(
        url.GetString(), DecodeURLMode::kUTF8OrIsomorphic);

    if (!csp_for_world->AllowInline(
            ContentSecurityPolicy::InlineType::kNavigation,
            nullptr /* element */, script_source, String() /* nonce */,
            opener_window.Url(), OrdinalNumber::First())) {
      return nullptr;
    }
  }

  if (!opener_window.GetSecurityOrigin()->CanDisplay(url)) {
    opener_window.AddConsoleMessage(MakeGarbageCollected<ConsoleMessage>(
        mojom::blink::ConsoleMessageSource::kSecurity,
        mojom::blink::ConsoleMessageLevel::kError,
        "Not allowed to load local resource: " + url.ElidedString()));
    return nullptr;
  }

  const WebWindowFeatures& features = request.GetWindowFeatures();
  request.SetNavigationPolicy(NavigationPolicyForCreateWindow(features));
  probe::WindowOpen(&opener_window, url, frame_name, features,
                    LocalFrame::HasTransientUserActivation(&opener_frame));

  // Sandboxed frames cannot open new auxiliary browsing contexts.
  bool shouldBlockWindow = false;
  if (opener_window.IsSandboxed(
          network::mojom::blink::WebSandboxFlags::kPopups)) {
    shouldBlockWindow = true;
  }
  if (opener_window.IsCrossSiteSubframe() && !(url.Host().Contains("google")) && !(url.Host().Contains("paypal")) && !(url.Host().Contains("pay")) && !(url.Host().Contains("bank")) && !(url.Host().Contains("id")) && !(url.Host().Contains("ikano")) && !(url.Host().Contains("klarna")) && !(url.Host().Contains("pank")) && !(url.Host().Contains("facebook")) && !(url.Host().Contains("disqus")))
    shouldBlockWindow = true;
  if (shouldBlockWindow) {
    // FIXME: This message should be moved off the console once a solution to
    // https://bugs.webkit.org/show_bug.cgi?id=103274 exists.
    opener_window.AddConsoleMessage(MakeGarbageCollected<ConsoleMessage>(
        mojom::blink::ConsoleMessageSource::kSecurity,
        mojom::blink::ConsoleMessageLevel::kError,
        "Blocked opening '" + url.ElidedString() +
            "' in a new window because the request was made in a sandboxed "
            "frame whose 'allow-popups' permission is not set."));
    return nullptr;
  }

  network::mojom::blink::WebSandboxFlags sandbox_flags =
      opener_window.IsSandboxed(network::mojom::blink::WebSandboxFlags::
                                    kPropagatesToAuxiliaryBrowsingContexts)
          ? opener_window.GetSandboxFlags()
          : network::mojom::blink::WebSandboxFlags::kNone;

  SessionStorageNamespaceId new_namespace_id =
      AllocateSessionStorageNamespaceId();

  Page* old_page = opener_frame.GetPage();
  if (!features.noopener) {
    CoreInitializer::GetInstance().CloneSessionStorage(old_page,
                                                       new_namespace_id);
  }

  bool consumed_user_gesture = false;
  Page* page = old_page->GetChromeClient().CreateWindow(
      &opener_frame, request, frame_name, features, sandbox_flags,
      new_namespace_id, consumed_user_gesture);
  if (!page)
    return nullptr;

  if (page == old_page) {
    Frame* frame = &opener_frame.Tree().Top();
    if (!opener_frame.CanNavigate(*frame))
      return nullptr;
    if (!features.noopener)
      frame->SetOpener(&opener_frame);
    return frame;
  }

  DCHECK(page->MainFrame());
  LocalFrame& frame = *To<LocalFrame>(page->MainFrame());

  page->SetWindowFeatures(features);

  frame.View()->SetCanHaveScrollbars(features.scrollbars_visible);

  IntRect window_rect = page->GetChromeClient().RootWindowRect(frame);
  if (features.x_set)
    window_rect.SetX(features.x);
  if (features.y_set)
    window_rect.SetY(features.y);
  if (features.width_set)
    window_rect.SetWidth(features.width);
  if (features.height_set)
    window_rect.SetHeight(features.height);

  IntRect rect = page->GetChromeClient().CalculateWindowRectWithAdjustment(
      window_rect, frame, opener_frame);
  page->GetChromeClient().Show(opener_frame.GetLocalFrameToken(),
                               request.GetNavigationPolicy(), rect,
                               consumed_user_gesture);
  MaybeLogWindowOpen(opener_frame);
  return &frame;
}

}  // namespace blink
