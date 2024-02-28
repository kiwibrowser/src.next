// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chrome_content_browser_client_binder_policies.h"

#include "components/autofill/content/common/mojom/autofill_driver.mojom.h"
#include "components/page_load_metrics/common/page_load_metrics.mojom.h"
#include "components/subresource_filter/content/mojom/subresource_filter.mojom.h"
#include "extensions/buildflags/buildflags.h"
#include "third_party/blink/public/common/features.h"

#if !BUILDFLAG(IS_CHROMEOS_LACROS)
#include "chrome/browser/ui/webui/bluetooth_internals/bluetooth_internals.mojom.h"  // nogncheck
#endif

#if BUILDFLAG(ENABLE_EXTENSIONS)
#include "extensions/common/mojom/event_router.mojom.h"
#include "extensions/common/mojom/frame.mojom.h"
#endif

namespace {

// Registers policies for interfaces registered in
// `RegisterBrowserInterfaceBindersForFrame()`.
void RegisterPoliciesForNonAssociatedInterfaces(
    content::MojoBinderPolicyMap& policy_map) {
  // Prerendering does not happen for WebUI pages, so set kUnexpected as the
  // policy for interfaces registered by WebUI.
#if !BUILDFLAG(IS_CHROMEOS_LACROS)
  policy_map.SetNonAssociatedPolicy<::mojom::BluetoothInternalsHandler>(
      content::MojoBinderNonAssociatedPolicy::kUnexpected);
#endif
}

// Registers policies for channel-associated interfaces registered in
// `RegisterAssociatedInterfaceBindersForRenderFrameHost()`.
void RegisterPoliciesForChannelAssociatedInterfaces(
    content::MojoBinderPolicyMap& policy_map) {
  policy_map.SetAssociatedPolicy<page_load_metrics::mojom::PageLoadMetrics>(
      content::MojoBinderAssociatedPolicy::kGrant);
  policy_map
      .SetAssociatedPolicy<subresource_filter::mojom::SubresourceFilterHost>(
          content::MojoBinderAssociatedPolicy::kGrant);
#if BUILDFLAG(ENABLE_EXTENSIONS)
  // LocalFrameHost supports content scripts related APIs, which are
  // RequestScriptInjectionPermission, GetInstallState, SendRequestIPC, and
  // notifying CSS selector updates. These APIs are used by Chrome Extensions
  // under proper permission managements beyond the page boundaries.
  policy_map.SetAssociatedPolicy<extensions::mojom::LocalFrameHost>(
      content::MojoBinderAssociatedPolicy::kGrant);

  // Grants Prerendering to use EventRouter, and sensitive behaviors are
  // prohibited by permission request boundary.
  policy_map.SetAssociatedPolicy<extensions::mojom::EventRouter>(
      content::MojoBinderAssociatedPolicy::kGrant);
#endif
}

}  // namespace

void RegisterChromeMojoBinderPoliciesForSameOriginPrerendering(
    content::MojoBinderPolicyMap& policy_map) {
  RegisterPoliciesForNonAssociatedInterfaces(policy_map);
  RegisterPoliciesForChannelAssociatedInterfaces(policy_map);
}

void RegisterChromeMojoBinderPoliciesForPreview(
    content::MojoBinderPolicyMap& policy_map) {
  RegisterPoliciesForNonAssociatedInterfaces(policy_map);
  RegisterPoliciesForChannelAssociatedInterfaces(policy_map);
}
