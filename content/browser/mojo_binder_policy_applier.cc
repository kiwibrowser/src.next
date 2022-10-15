// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/mojo_binder_policy_applier.h"

#include "content/public/browser/mojo_binder_policy_map.h"
#include "mojo/public/cpp/bindings/message.h"

namespace content {

MojoBinderPolicyApplier::MojoBinderPolicyApplier(
    const MojoBinderPolicyMapImpl* policy_map,
    base::OnceCallback<void(const std::string& interface_name)> cancel_callback)
    : policy_map_(*policy_map), cancel_callback_(std::move(cancel_callback)) {}

MojoBinderPolicyApplier::~MojoBinderPolicyApplier() = default;

// static
std::unique_ptr<MojoBinderPolicyApplier>
MojoBinderPolicyApplier::CreateForSameOriginPrerendering(
    base::OnceCallback<void(const std::string& interface_name)>
        cancel_callback) {
  return std::make_unique<MojoBinderPolicyApplier>(
      MojoBinderPolicyMapImpl::GetInstanceForSameOriginPrerendering(),
      std::move(cancel_callback));
}

void MojoBinderPolicyApplier::ApplyPolicyToNonAssociatedBinder(
    const std::string& interface_name,
    base::OnceClosure binder_callback) {
  if (mode_ == Mode::kGrantAll) {
    std::move(binder_callback).Run();
    return;
  }
  const MojoBinderNonAssociatedPolicy policy =
      GetNonAssociatedMojoBinderPolicy(interface_name);

  // Run in the kPrepareToGrantAll mode before the renderer sends back a
  // DidCommitActivation. In this mode, MojoBinderPolicyApplier loosens
  // policies, but still defers binders to ensure that the renderer does not
  // receive unexpected messages before CommitActivation arrives.
  if (mode_ == Mode::kPrepareToGrantAll) {
    switch (policy) {
      case MojoBinderNonAssociatedPolicy::kGrant:
      // Grant these two kinds of interfaces because:
      // - kCancel and kUnexpected interfaces may have sync methods, so grant
      // them to avoid deadlocks.
      // - Renderer might request these interfaces during the prerenderingchange
      // event, because from the page's point of view it is no longer
      // prerendering.
      case MojoBinderNonAssociatedPolicy::kCancel:
      case MojoBinderNonAssociatedPolicy::kUnexpected:
        std::move(binder_callback).Run();
        break;
      case MojoBinderNonAssociatedPolicy::kDefer:
        deferred_binders_.push_back(std::move(binder_callback));
        break;
    }
    return;
  }

  DCHECK_EQ(mode_, Mode::kEnforce);
  switch (policy) {
    case MojoBinderNonAssociatedPolicy::kGrant:
      std::move(binder_callback).Run();
      break;
    case MojoBinderNonAssociatedPolicy::kCancel:
      if (cancel_callback_) {
        std::move(cancel_callback_).Run(interface_name);
      }
      break;
    case MojoBinderNonAssociatedPolicy::kDefer:
      deferred_binders_.push_back(std::move(binder_callback));
      break;
    case MojoBinderNonAssociatedPolicy::kUnexpected:
      mojo::ReportBadMessage("MBPA_BAD_INTERFACE: " + interface_name);
      if (cancel_callback_) {
        std::move(cancel_callback_).Run(interface_name);
      }
      break;
  }
}

bool MojoBinderPolicyApplier::ApplyPolicyToAssociatedBinder(
    const std::string& interface_name) {
  MojoBinderAssociatedPolicy policy = MojoBinderAssociatedPolicy::kCancel;
  switch (mode_) {
    // Always allow binders to run.
    case Mode::kGrantAll:
    case Mode::kPrepareToGrantAll:
      return true;
    case Mode::kEnforce:
      policy = policy_map_.GetAssociatedMojoBinderPolicy(
          interface_name, MojoBinderAssociatedPolicy::kCancel);
      if (policy != MojoBinderAssociatedPolicy::kGrant) {
        if (cancel_callback_)
          std::move(cancel_callback_).Run(interface_name);
        return false;
      }
  }
  return true;
}

void MojoBinderPolicyApplier::PrepareToGrantAll() {
  DCHECK_EQ(mode_, Mode::kEnforce);
  mode_ = Mode::kPrepareToGrantAll;
}

void MojoBinderPolicyApplier::GrantAll() {
  DCHECK_NE(mode_, Mode::kGrantAll);

  // Check that we are in a Mojo message dispatch, since the deferred binders
  // might call mojo::ReportBadMessage().
  //
  // TODO(https://crbug.com/1217977): Give the deferred_binders_ a
  // BadMessageCallback and forbid them from using mojo::ReportBadMessage()
  // directly. We are currently in the message stack of one of the PageBroadcast
  // Mojo callbacks handled by RenderViewHost, so if a binder calls
  // mojo::ReportBadMessage() it kills possibly the wrong renderer. Even if we
  // only run the binders associated with the RVH for each message per-RVH,
  // there are still subtle problems with running all these callbacks at once:
  // for example, mojo::GetMessageCallback()/mojo::ReportBadMessage() can only
  // be called once per message dispatch.
  DCHECK(mojo::IsInMessageDispatch());

  mode_ = Mode::kGrantAll;

  // It's safe to iterate over `deferred_binders_` because no more callbacks
  // will be added to it once `grant_all_` is true."
  for (auto& deferred_binder : deferred_binders_)
    std::move(deferred_binder).Run();
  deferred_binders_.clear();
}

void MojoBinderPolicyApplier::DropDeferredBinders() {
  deferred_binders_.clear();
}

MojoBinderNonAssociatedPolicy
MojoBinderPolicyApplier::GetNonAssociatedMojoBinderPolicy(
    const std::string& interface_name) const {
  return policy_map_.GetNonAssociatedMojoBinderPolicy(interface_name,
                                                      default_policy_);
}

}  // namespace content
