// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/chrome_content_browser_client_extensions_part.h"

#include "base/functional/bind.h"
#include "content/public/browser/service_worker_version_base_info.h"
#include "extensions/browser/api/automation_internal/automation_event_router.h"
#include "extensions/browser/event_router.h"
#include "extensions/browser/guest_view/extensions_guest_view.h"
#include "extensions/browser/renderer_startup_helper.h"
#include "extensions/browser/service_worker/service_worker_host.h"
#include "extensions/buildflags/buildflags.h"
#include "extensions/common/mojom/automation_registry.mojom.h"
#include "extensions/common/mojom/event_router.mojom.h"
#include "extensions/common/mojom/guest_view.mojom.h"
#include "extensions/common/mojom/renderer_host.mojom.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_registry.h"

namespace extensions {

void ChromeContentBrowserClientExtensionsPart::ExposeInterfacesToRenderer(
    service_manager::BinderRegistry* registry,
    blink::AssociatedInterfaceRegistry* associated_registry,
    content::RenderProcessHost* host) {
  associated_registry->AddInterface<mojom::RendererHost>(base::BindRepeating(
      &RendererStartupHelper::BindForRenderer, host->GetID()));
#if BUILDFLAG(ENABLE_EXTENSIONS_LEGACY_IPC)
  associated_registry->AddInterface<mojom::EventRouter>(
      base::BindRepeating(&EventRouter::BindForRenderer, host->GetID()));
  associated_registry->AddInterface<mojom::ServiceWorkerHost>(
      base::BindRepeating(&ServiceWorkerHost::BindReceiver, host->GetID()));
  associated_registry->AddInterface<mojom::RendererAutomationRegistry>(
      base::BindRepeating(&AutomationEventRouter::BindForRenderer,
                          host->GetID()));
#endif
}

void ChromeContentBrowserClientExtensionsPart::
    ExposeInterfacesToRendererForServiceWorker(
        const content::ServiceWorkerVersionBaseInfo&
            service_worker_version_info,
        blink::AssociatedInterfaceRegistry& associated_registry) {
  CHECK(service_worker_version_info.process_id !=
        content::ChildProcessHost::kInvalidUniqueID);
  associated_registry.AddInterface<mojom::RendererHost>(
      base::BindRepeating(&RendererStartupHelper::BindForRenderer,
                          service_worker_version_info.process_id));
#if !BUILDFLAG(ENABLE_EXTENSIONS_LEGACY_IPC)
  associated_registry.AddInterface<mojom::ServiceWorkerHost>(
      base::BindRepeating(&ServiceWorkerHost::BindReceiver,
                          service_worker_version_info.process_id));
  associated_registry.AddInterface<mojom::RendererAutomationRegistry>(
      base::BindRepeating(&AutomationEventRouter::BindForRenderer,
                          service_worker_version_info.process_id));
  associated_registry.AddInterface<mojom::EventRouter>(base::BindRepeating(
      &EventRouter::BindForRenderer, service_worker_version_info.process_id));
#endif
}

void ChromeContentBrowserClientExtensionsPart::
    ExposeInterfacesToRendererForRenderFrameHost(
        content::RenderFrameHost& frame_host,
        blink::AssociatedInterfaceRegistry& associated_registry) {
  int render_process_id = frame_host.GetProcess()->GetID();
  associated_registry.AddInterface<mojom::RendererHost>(base::BindRepeating(
      &RendererStartupHelper::BindForRenderer, render_process_id));
  associated_registry.AddInterface<mojom::RendererAutomationRegistry>(
      base::BindRepeating(&AutomationEventRouter::BindForRenderer,
                          render_process_id));
  associated_registry.AddInterface<mojom::EventRouter>(
      base::BindRepeating(&EventRouter::BindForRenderer, render_process_id));
  associated_registry.AddInterface<guest_view::mojom::GuestViewHost>(
      base::BindRepeating(&ExtensionsGuestView::CreateForComponents,
                          frame_host.GetGlobalId()));
  associated_registry.AddInterface<mojom::GuestView>(base::BindRepeating(
      &ExtensionsGuestView::CreateForExtensions, frame_host.GetGlobalId()));
}

}  // namespace extensions
