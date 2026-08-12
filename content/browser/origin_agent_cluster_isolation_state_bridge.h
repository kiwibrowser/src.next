// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_ORIGIN_AGENT_CLUSTER_ISOLATION_STATE_BRIDGE_H_
#define CONTENT_BROWSER_ORIGIN_AGENT_CLUSTER_ISOLATION_STATE_BRIDGE_H_

#include "content/browser/origin_agent_cluster_isolation_state.h"
#include "content/browser/security/cpsp/child_process_security_policy_impl.rs.h"

namespace content {

// Translates a C++ OriginAgentClusterIsolationState into its Rust FFI enum
// representation.
//
// TODO(crbug.com/482216433): Consider refactoring C++
// OriginAgentClusterIsolationState and AgentClusterKey logic to to use a single
// enum for tracking OAC state like Rust, which would eliminate these
// conversions.
rust::child_process_security_policy::OriginAgentClusterIsolationState
ToRustOriginAgentClusterIsolationState(
    const OriginAgentClusterIsolationState& oac_state);

// Translates a Rust FFI enum representation back into a C++
// OriginAgentClusterIsolationState.
//
// TODO(crbug.com/482216433): Consider refactoring C++
// OriginAgentClusterIsolationState and AgentClusterKey logic to to use a single
// enum for tracking OAC state like Rust, which would eliminate these
// conversions.
OriginAgentClusterIsolationState FromRustOriginAgentClusterIsolationState(
    rust::child_process_security_policy::OriginAgentClusterIsolationState
        rust_oac_state);

}  // namespace content

#endif  // CONTENT_BROWSER_ORIGIN_AGENT_CLUSTER_ISOLATION_STATE_BRIDGE_H_
