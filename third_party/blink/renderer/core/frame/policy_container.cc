// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/frame/policy_container.h"

#include <tuple>

#include "services/network/public/cpp/web_sandbox_flags.h"
#include "third_party/blink/renderer/core/frame/csp/conversion_util.h"

namespace blink {

PolicyContainer::PolicyContainer(
    mojo::PendingAssociatedRemote<mojom::blink::PolicyContainerHost> remote,
    mojom::blink::PolicyContainerPoliciesPtr policies)
    : policies_(std::move(policies)),
      policy_container_host_remote_(std::move(remote)) {}

// static
std::unique_ptr<PolicyContainer> PolicyContainer::CreateEmpty() {
  // Create a dummy PolicyContainerHost remote. All the messages will be
  // ignored.
  mojo::AssociatedRemote<mojom::blink::PolicyContainerHost> dummy_host;
  std::ignore = dummy_host.BindNewEndpointAndPassDedicatedReceiver();

  return std::make_unique<PolicyContainer>(
      dummy_host.Unbind(), mojom::blink::PolicyContainerPolicies::New());
}

// static
std::unique_ptr<PolicyContainer> PolicyContainer::CreateFromWebPolicyContainer(
    std::unique_ptr<WebPolicyContainer> container) {
  if (!container)
    return nullptr;
  mojom::blink::PolicyContainerPoliciesPtr policies =
      mojom::blink::PolicyContainerPolicies::New(
          container->policies.cross_origin_embedder_policy,
          container->policies.referrer_policy,
          ConvertToMojoBlink(
              std::move(container->policies.content_security_policies)),
          container->policies.is_anonymous, container->policies.sandbox_flags);

  return std::make_unique<PolicyContainer>(std::move(container->remote),
                                           std::move(policies));
}

network::mojom::blink::ReferrerPolicy PolicyContainer::GetReferrerPolicy()
    const {
  return policies_->referrer_policy;
}

void PolicyContainer::UpdateReferrerPolicy(
    network::mojom::blink::ReferrerPolicy policy) {
  policies_->referrer_policy = policy;
  policy_container_host_remote_->SetReferrerPolicy(policy);
}

const mojom::blink::PolicyContainerPolicies& PolicyContainer::GetPolicies()
    const {
  return *policies_;
}

void PolicyContainer::AddContentSecurityPolicies(
    Vector<network::mojom::blink::ContentSecurityPolicyPtr> policies) {
  for (const auto& policy : policies) {
    policies_->content_security_policies.push_back(policy->Clone());
  }
  policy_container_host_remote_->AddContentSecurityPolicies(
      std::move(policies));
}

mojo::PendingRemote<mojom::blink::PolicyContainerHostKeepAliveHandle>
PolicyContainer::IssueKeepAliveHandle() {
  mojo::PendingRemote<mojom::blink::PolicyContainerHostKeepAliveHandle>
      keep_alive_remote;
  policy_container_host_remote_->IssueKeepAliveHandle(
      keep_alive_remote.InitWithNewPipeAndPassReceiver());
  return keep_alive_remote;
}

}  // namespace blink
