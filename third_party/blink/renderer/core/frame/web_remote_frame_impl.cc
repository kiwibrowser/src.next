// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/frame/web_remote_frame_impl.h"

#include <utility>

#include "third_party/blink/public/common/frame/frame_visual_properties.h"
#include "third_party/blink/public/common/permissions_policy/permissions_policy.h"
#include "third_party/blink/public/mojom/frame/frame_replication_state.mojom.h"
#include "third_party/blink/public/mojom/frame/tree_scope_type.mojom-blink.h"
#include "third_party/blink/public/mojom/security_context/insecure_request_policy.mojom-blink.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_element.h"
#include "third_party/blink/public/web/web_frame_owner_properties.h"
#include "third_party/blink/public/web/web_performance.h"
#include "third_party/blink/public/web/web_range.h"
#include "third_party/blink/renderer/bindings/core/v8/window_proxy.h"
#include "third_party/blink/renderer/core/execution_context/remote_security_context.h"
#include "third_party/blink/renderer/core/execution_context/security_context.h"
#include "third_party/blink/renderer/core/exported/web_view_impl.h"
#include "third_party/blink/renderer/core/frame/csp/conversion_util.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/frame/remote_frame_client_impl.h"
#include "third_party/blink/renderer/core/frame/remote_frame_owner.h"
#include "third_party/blink/renderer/core/frame/settings.h"
#include "third_party/blink/renderer/core/frame/web_frame_widget_impl.h"
#include "third_party/blink/renderer/core/frame/web_local_frame_impl.h"
#include "third_party/blink/renderer/core/html/fenced_frame/html_fenced_frame_element.h"
#include "third_party/blink/renderer/core/html/html_frame_owner_element.h"
#include "third_party/blink/renderer/core/html/portal/html_portal_element.h"
#include "third_party/blink/renderer/core/html_names.h"
#include "third_party/blink/renderer/core/layout/layout_object.h"
#include "third_party/blink/renderer/core/layout/layout_view.h"
#include "third_party/blink/renderer/core/page/chrome_client.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "third_party/blink/renderer/platform/bindings/dom_wrapper_world.h"
#include "third_party/blink/renderer/platform/geometry/layout_rect.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/wtf/text/atomic_string.h"
#include "ui/gfx/geometry/quad_f.h"
#include "v8/include/v8.h"

namespace blink {

WebRemoteFrame* WebRemoteFrame::FromFrameToken(
    const RemoteFrameToken& frame_token) {
  auto* frame = RemoteFrame::FromFrameToken(frame_token);
  if (!frame)
    return nullptr;
  return WebRemoteFrameImpl::FromFrame(*frame);
}

WebRemoteFrame* WebRemoteFrame::Create(mojom::blink::TreeScopeType scope,
                                       WebRemoteFrameClient* client,
                                       const RemoteFrameToken& frame_token) {
  return MakeGarbageCollected<WebRemoteFrameImpl>(scope, client, frame_token);
}

// static
WebRemoteFrame* WebRemoteFrame::CreateMainFrame(
    WebView* web_view,
    WebRemoteFrameClient* client,
    const RemoteFrameToken& frame_token,
    const base::UnguessableToken& devtools_frame_token,
    WebFrame* opener,
    CrossVariantMojoAssociatedRemote<mojom::blink::RemoteFrameHostInterfaceBase>
        remote_frame_host,
    CrossVariantMojoAssociatedReceiver<mojom::blink::RemoteFrameInterfaceBase>
        receiver,
    mojom::FrameReplicationStatePtr replicated_state) {
  return WebRemoteFrameImpl::CreateMainFrame(
      web_view, client, frame_token, devtools_frame_token, opener,
      std::move(remote_frame_host), std::move(receiver),
      std::move(replicated_state));
}

// static
WebRemoteFrame* WebRemoteFrame::CreateForPortalOrFencedFrame(
    mojom::blink::TreeScopeType scope,
    WebRemoteFrameClient* client,
    const RemoteFrameToken& frame_token,
    const base::UnguessableToken& devtools_frame_token,
    const WebElement& frame_owner,
    CrossVariantMojoAssociatedRemote<mojom::blink::RemoteFrameHostInterfaceBase>
        remote_frame_host,
    CrossVariantMojoAssociatedReceiver<mojom::blink::RemoteFrameInterfaceBase>
        receiver,
    mojom::FrameReplicationStatePtr replicated_state) {
  return WebRemoteFrameImpl::CreateForPortalOrFencedFrame(
      scope, client, frame_token, devtools_frame_token, frame_owner,
      std::move(remote_frame_host), std::move(receiver),
      std::move(replicated_state));
}

// static
WebRemoteFrameImpl* WebRemoteFrameImpl::CreateMainFrame(
    WebView* web_view,
    WebRemoteFrameClient* client,
    const RemoteFrameToken& frame_token,
    const base::UnguessableToken& devtools_frame_token,
    WebFrame* opener,
    mojo::PendingAssociatedRemote<mojom::blink::RemoteFrameHost>
        remote_frame_host,
    mojo::PendingAssociatedReceiver<mojom::blink::RemoteFrame> receiver,
    mojom::FrameReplicationStatePtr replicated_state) {
  WebRemoteFrameImpl* frame = MakeGarbageCollected<WebRemoteFrameImpl>(
      mojom::blink::TreeScopeType::kDocument, client, frame_token);
  Page& page = *To<WebViewImpl>(web_view)->GetPage();
  // It would be nice to DCHECK that the main frame is not set yet here.
  // Unfortunately, there is an edge case with a pending RenderFrameHost that
  // violates this: the embedder may create a pending RenderFrameHost for
  // navigating to a new page in a popup. If the navigation ends up redirecting
  // to a site that requires a process swap, it doesn't go through the standard
  // swapping path and instead directly overwrites the main frame.
  // TODO(dcheng): Remove the need for this and strongly enforce this condition
  // with a DCHECK.
  frame->InitializeCoreFrame(
      page, nullptr, nullptr, nullptr, FrameInsertType::kInsertInConstructor,
      g_null_atom,
      opener ? &ToCoreFrame(*opener)->window_agent_factory() : nullptr,
      devtools_frame_token, std::move(remote_frame_host), std::move(receiver));
  frame->SetReplicatedState(std::move(replicated_state));
  Frame* opener_frame = opener ? ToCoreFrame(*opener) : nullptr;
  ToCoreFrame(*frame)->SetOpenerDoNotNotify(opener_frame);
  return frame;
}

WebRemoteFrameImpl* WebRemoteFrameImpl::CreateForPortalOrFencedFrame(
    mojom::blink::TreeScopeType scope,
    WebRemoteFrameClient* client,
    const RemoteFrameToken& frame_token,
    const base::UnguessableToken& devtools_frame_token,
    const WebElement& frame_owner,
    mojo::PendingAssociatedRemote<mojom::blink::RemoteFrameHost>
        remote_frame_host,
    mojo::PendingAssociatedReceiver<mojom::blink::RemoteFrame> receiver,
    mojom::FrameReplicationStatePtr replicated_state) {
  auto* frame =
      MakeGarbageCollected<WebRemoteFrameImpl>(scope, client, frame_token);

  // We first convert this to a raw blink::Element*, and manually convert this
  // to an HTMLElement*. That is the only way the IsA<> and To<> casts below
  // will work.
  Element* element = frame_owner;
  DCHECK(IsA<HTMLPortalElement>(element) ||
         IsA<HTMLFencedFrameElement>(element));
  ExecutionContext* execution_context = element->GetExecutionContext();
  DCHECK(RuntimeEnabledFeatures::PortalsEnabled(execution_context) ||
         RuntimeEnabledFeatures::FencedFramesEnabled(execution_context));
  HTMLFrameOwnerElement* frame_owner_element =
      To<HTMLFrameOwnerElement>(element);
  LocalFrame* host_frame = frame_owner_element->GetDocument().GetFrame();
  frame->InitializeCoreFrame(
      *host_frame->GetPage(), frame_owner_element, /*parent=*/nullptr,
      /*previous_sibling=*/nullptr, FrameInsertType::kInsertInConstructor,
      g_null_atom, &host_frame->window_agent_factory(), devtools_frame_token,
      std::move(remote_frame_host), std::move(receiver));
  frame->SetReplicatedState(std::move(replicated_state));
  return frame;
}

WebRemoteFrameImpl::~WebRemoteFrameImpl() = default;

void WebRemoteFrameImpl::Trace(Visitor* visitor) const {
  visitor->Trace(frame_client_);
  visitor->Trace(frame_);
}

bool WebRemoteFrameImpl::IsWebLocalFrame() const {
  return false;
}

WebLocalFrame* WebRemoteFrameImpl::ToWebLocalFrame() {
  NOTREACHED();
  return nullptr;
}

const WebLocalFrame* WebRemoteFrameImpl::ToWebLocalFrame() const {
  NOTREACHED();
  return nullptr;
}

bool WebRemoteFrameImpl::IsWebRemoteFrame() const {
  return true;
}

WebRemoteFrame* WebRemoteFrameImpl::ToWebRemoteFrame() {
  return this;
}

const WebRemoteFrame* WebRemoteFrameImpl::ToWebRemoteFrame() const {
  return this;
}

void WebRemoteFrameImpl::Close() {
  WebRemoteFrame::Close();

  self_keep_alive_.Clear();
}

WebView* WebRemoteFrameImpl::View() const {
  if (!GetFrame()) {
    return nullptr;
  }
  DCHECK(GetFrame()->GetPage());
  return GetFrame()->GetPage()->GetChromeClient().GetWebView();
}

WebLocalFrame* WebRemoteFrameImpl::CreateLocalChild(
    mojom::blink::TreeScopeType scope,
    const WebString& name,
    const FramePolicy& frame_policy,
    WebLocalFrameClient* client,
    InterfaceRegistry* interface_registry,
    WebFrame* previous_sibling,
    const WebFrameOwnerProperties& frame_owner_properties,
    const LocalFrameToken& frame_token,
    WebFrame* opener,
    std::unique_ptr<WebPolicyContainer> policy_container) {
  auto* child = MakeGarbageCollected<WebLocalFrameImpl>(
      base::PassKey<WebRemoteFrameImpl>(), scope, client, interface_registry,
      frame_token);
  auto* owner = MakeGarbageCollected<RemoteFrameOwner>(frame_policy,
                                                       frame_owner_properties);

  WindowAgentFactory* window_agent_factory = nullptr;
  if (opener) {
    window_agent_factory = &ToCoreFrame(*opener)->window_agent_factory();
  } else {
    window_agent_factory = &GetFrame()->window_agent_factory();
  }

  child->InitializeCoreFrame(
      *GetFrame()->GetPage(), owner, this, previous_sibling,
      FrameInsertType::kInsertInConstructor, name, window_agent_factory, opener,
      std::move(policy_container));
  DCHECK(child->GetFrame());
  return child;
}

void WebRemoteFrameImpl::InitializeCoreFrame(
    Page& page,
    FrameOwner* owner,
    WebFrame* parent,
    WebFrame* previous_sibling,
    FrameInsertType insert_type,
    const AtomicString& name,
    WindowAgentFactory* window_agent_factory,
    const base::UnguessableToken& devtools_frame_token,
    mojo::PendingAssociatedRemote<mojom::blink::RemoteFrameHost>
        remote_frame_host,
    mojo::PendingAssociatedReceiver<mojom::blink::RemoteFrame>
        remote_frame_receiver) {
  Frame* parent_frame = parent ? ToCoreFrame(*parent) : nullptr;
  Frame* previous_sibling_frame =
      previous_sibling ? ToCoreFrame(*previous_sibling) : nullptr;

  // If this is not a top-level frame, we need to send FrameVisualProperties to
  // the remote renderer process. Some of the properties are inherited from the
  // WebFrameWidget containing this frame, and this is true for regular frames
  // in the frame tree as well as for portals, which are not in the frame tree;
  // hence the code to traverse up through FrameOwner.
  WebFrameWidget* ancestor_widget = nullptr;
  if (parent) {
    if (parent->IsWebLocalFrame()) {
      ancestor_widget =
          To<WebLocalFrameImpl>(parent)->LocalRoot()->FrameWidget();
    }
  } else if (owner && owner->IsLocal()) {
    // Never gets to this point unless |owner| is a <portal> or <fencedframe>
    // element.
    HTMLFrameOwnerElement* owner_element = To<HTMLFrameOwnerElement>(owner);
    DCHECK(owner_element->IsHTMLPortalElement() ||
           owner_element->IsHTMLFencedFrameElement());
    LocalFrame& local_frame =
        owner_element->GetDocument().GetFrame()->LocalFrameRoot();
    ancestor_widget = WebLocalFrameImpl::FromFrame(local_frame)->FrameWidget();
  }

  DCHECK(remote_frame_host && remote_frame_receiver);
  SetCoreFrame(MakeGarbageCollected<RemoteFrame>(
      frame_client_.Get(), page, owner, parent_frame, previous_sibling_frame,
      insert_type, GetRemoteFrameToken(), window_agent_factory, ancestor_widget,
      devtools_frame_token, std::move(remote_frame_host),
      std::move(remote_frame_receiver)));

  if (ancestor_widget)
    InitializeFrameVisualProperties(ancestor_widget, View());

  GetFrame()->CreateView();
  frame_->Tree().SetName(name);
}

WebRemoteFrame* WebRemoteFrameImpl::CreateRemoteChild(
    mojom::blink::TreeScopeType scope,
    WebRemoteFrameClient* client,
    const RemoteFrameToken& frame_token,
    const base::UnguessableToken& devtools_frame_token,
    WebFrame* opener,
    CrossVariantMojoAssociatedRemote<mojom::blink::RemoteFrameHostInterfaceBase>
        remote_frame_host,
    CrossVariantMojoAssociatedReceiver<mojom::blink::RemoteFrameInterfaceBase>
        receiver,
    mojom::FrameReplicationStatePtr replicated_state) {
  auto* child =
      MakeGarbageCollected<WebRemoteFrameImpl>(scope, client, frame_token);
  auto* owner = MakeGarbageCollected<RemoteFrameOwner>(
      replicated_state->frame_policy, WebFrameOwnerProperties());
  WindowAgentFactory* window_agent_factory = nullptr;
  if (opener) {
    window_agent_factory = &ToCoreFrame(*opener)->window_agent_factory();
  } else {
    window_agent_factory = &GetFrame()->window_agent_factory();
  }

  child->InitializeCoreFrame(*GetFrame()->GetPage(), owner, this, LastChild(),
                             FrameInsertType::kInsertInConstructor,
                             WebString::FromUTF8(replicated_state->name),
                             window_agent_factory, devtools_frame_token,
                             std::move(remote_frame_host), std::move(receiver));
  child->SetReplicatedState(std::move(replicated_state));
  Frame* opener_frame = opener ? ToCoreFrame(*opener) : nullptr;
  ToCoreFrame(*child)->SetOpenerDoNotNotify(opener_frame);
  return child;
}

void WebRemoteFrameImpl::SetCoreFrame(RemoteFrame* frame) {
  frame_ = frame;
}

void WebRemoteFrameImpl::InitializeFrameVisualProperties(
    WebFrameWidget* ancestor_widget,
    WebView* web_view) {
  FrameVisualProperties visual_properties;
  visual_properties.zoom_level = web_view->ZoomLevel();
  visual_properties.page_scale_factor = ancestor_widget->PageScaleInMainFrame();
  visual_properties.is_pinch_gesture_active =
      ancestor_widget->PinchGestureActiveInMainFrame();
  visual_properties.screen_infos = ancestor_widget->GetOriginalScreenInfos();
  visual_properties.visible_viewport_size =
      ancestor_widget->VisibleViewportSizeInDIPs();
  const WebVector<gfx::Rect>& window_segments =
      ancestor_widget->WindowSegments();
  visual_properties.root_widget_window_segments.assign(window_segments.begin(),
                                                       window_segments.end());
  GetFrame()->InitializeFrameVisualProperties(visual_properties);
}

WebRemoteFrameImpl* WebRemoteFrameImpl::FromFrame(RemoteFrame& frame) {
  if (!frame.Client())
    return nullptr;
  RemoteFrameClientImpl* client =
      static_cast<RemoteFrameClientImpl*>(frame.Client());
  return client->GetWebFrame();
}

void WebRemoteFrameImpl::SetReplicatedOrigin(
    const WebSecurityOrigin& origin,
    bool is_potentially_trustworthy_opaque_origin) {
  DCHECK(GetFrame());
  GetFrame()->SetReplicatedOrigin(origin,
                                  is_potentially_trustworthy_opaque_origin);
}

void WebRemoteFrameImpl::DidStartLoading() {
  GetFrame()->DidStartLoading();
}

v8::Local<v8::Object> WebRemoteFrameImpl::GlobalProxy() const {
  return GetFrame()
      ->GetWindowProxy(DOMWrapperWorld::MainWorld())
      ->GlobalProxyIfNotDetached();
}

gfx::Rect WebRemoteFrameImpl::GetCompositingRect() {
  return GetFrame()->View()->GetCompositingRect();
}

WebString WebRemoteFrameImpl::UniqueName() const {
  return GetFrame()->UniqueName();
}

const FrameVisualProperties&
WebRemoteFrameImpl::GetPendingVisualPropertiesForTesting() const {
  return GetFrame()->GetPendingVisualPropertiesForTesting();
}

bool WebRemoteFrameImpl::IsAdSubframe() const {
  return GetFrame()->IsAdSubframe();
}

WebRemoteFrameImpl::WebRemoteFrameImpl(mojom::blink::TreeScopeType scope,
                                       WebRemoteFrameClient* client,
                                       const RemoteFrameToken& frame_token)
    : WebRemoteFrame(scope, frame_token),
      client_(client),
      frame_client_(MakeGarbageCollected<RemoteFrameClientImpl>(this)) {
  DCHECK(client);
}

void WebRemoteFrameImpl::SetReplicatedState(
    mojom::FrameReplicationStatePtr state) {
  RemoteFrame* remote_frame = GetFrame();
  DCHECK(remote_frame);

  remote_frame->SetReplicatedOrigin(
      SecurityOrigin::CreateFromUrlOrigin(state->origin),
      state->has_potentially_trustworthy_unique_origin);

#if DCHECK_IS_ON()
  scoped_refptr<const SecurityOrigin> security_origin_before_sandbox_flags =
      remote_frame->GetSecurityContext()->GetSecurityOrigin();
#endif

  remote_frame->SetReplicatedSandboxFlags(state->active_sandbox_flags);

#if DCHECK_IS_ON()
  // If |state->has_potentially_trustworthy_unique_origin| is set,
  // - |state->origin| should be unique (this is checked in
  //   blink::SecurityOrigin::SetUniqueOriginIsPotentiallyTrustworthy() in
  //   SetReplicatedOrigin()), and thus
  // - The security origin is not updated by SetReplicatedSandboxFlags() and
  //   thus we don't have to apply |has_potentially_trustworthy_unique_origin|
  //   flag after SetReplicatedSandboxFlags().
  if (state->has_potentially_trustworthy_unique_origin) {
    DCHECK(security_origin_before_sandbox_flags ==
           remote_frame->GetSecurityContext()->GetSecurityOrigin());
  }
#endif

  remote_frame->SetReplicatedName(
      blink::WebString::FromUTF8(state->name),
      blink::WebString::FromUTF8(state->unique_name));
  remote_frame->SetInsecureRequestPolicy(state->insecure_request_policy);
  remote_frame->SetInsecureNavigationsSet(state->insecure_navigations_set);
  remote_frame->SetReplicatedIsAdSubframe(state->is_ad_subframe);
  remote_frame->SetReplicatedPermissionsPolicyHeader(
      state->permissions_policy_header);
  if (state->has_active_user_gesture) {
    // TODO(crbug.com/1087963): This should be hearing about sticky activations
    // and setting those (as well as the active one?). But the call to
    // UpdateUserActivationState sets the transient activation.
    remote_frame->UpdateUserActivationState(
        mojom::UserActivationUpdateType::kNotifyActivation,
        mojom::UserActivationNotificationType::kMedia);
  }
  remote_frame->SetHadStickyUserActivationBeforeNavigation(
      state->has_received_user_gesture_before_nav);
}

}  // namespace blink
