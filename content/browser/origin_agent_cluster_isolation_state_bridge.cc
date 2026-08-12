// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/origin_agent_cluster_isolation_state_bridge.h"

#include "base/notreached.h"

namespace content {

rust::child_process_security_policy::OriginAgentClusterIsolationState
ToRustOriginAgentClusterIsolationState(
    const OriginAgentClusterIsolationState& oac_state) {
  using OACStatus = AgentClusterKey::OACStatus;
  using RustOacState =
      rust::child_process_security_policy::OriginAgentClusterIsolationState;

  // First, cover cases where there is no OAC, either by default or via an
  // opt-out header.
  if (oac_state.logical_oac_status() == OACStatus::kSiteKeyedByDefault) {
    return RustOacState::SiteKeyedByDefault;
  }
  if (oac_state.logical_oac_status() == OACStatus::kSiteKeyedByHeader) {
    return RustOacState::SiteKeyedByHeader;
  }

  // Second, cover cases where OAC applies with process isolation.
  if (oac_state.process_isolation_oac_status() ==
      OACStatus::kOriginKeyedByHeader) {
    return RustOacState::OriginKeyedProcessIsolatedByHeader;
  }
  if (oac_state.process_isolation_oac_status() ==
      OACStatus::kOriginKeyedByDefault) {
    return RustOacState::OriginKeyedProcessIsolatedByDefault;
  }

  // Now, cover the remaining cases where OAC applies only logically, without
  // process isolation.
  if (oac_state.logical_oac_status() == OACStatus::kOriginKeyedByHeader) {
    return RustOacState::OriginKeyedLogicalOnlyByHeader;
  }

  return RustOacState::OriginKeyedLogicalOnlyByDefault;
}

OriginAgentClusterIsolationState FromRustOriginAgentClusterIsolationState(
    rust::child_process_security_policy::OriginAgentClusterIsolationState
        rust_oac_state) {
  using RustOacState =
      rust::child_process_security_policy::OriginAgentClusterIsolationState;

  switch (rust_oac_state) {
    case RustOacState::SiteKeyedByDefault:
      return OriginAgentClusterIsolationState::CreateNonIsolatedByDefault();
    case RustOacState::SiteKeyedByHeader:
      return OriginAgentClusterIsolationState::CreateNonIsolatedByHeader();
    case RustOacState::OriginKeyedLogicalOnlyByDefault:
      return OriginAgentClusterIsolationState::CreateForOriginAgentCluster(
          /*had_oac_request=*/false, /*requires_origin_keyed_process=*/false);
    case RustOacState::OriginKeyedLogicalOnlyByHeader:
      return OriginAgentClusterIsolationState::CreateForOriginAgentCluster(
          /*had_oac_request=*/true, /*requires_origin_keyed_process=*/false);
    case RustOacState::OriginKeyedProcessIsolatedByDefault:
      return OriginAgentClusterIsolationState::CreateForOriginAgentCluster(
          /*had_oac_request=*/false, /*requires_origin_keyed_process=*/true);
    case RustOacState::OriginKeyedProcessIsolatedByHeader:
      return OriginAgentClusterIsolationState::CreateForOriginAgentCluster(
          /*had_oac_request=*/true, /*requires_origin_keyed_process=*/true);
  }
  NOTREACHED();
}

}  // namespace content
