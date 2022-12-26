// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/gpu/browser_gpu_client_delegate.h"

#include "content/browser/gpu/gpu_memory_buffer_manager_singleton.h"
#include "content/browser/gpu/gpu_process_host.h"

namespace content {

BrowserGpuClientDelegate::BrowserGpuClientDelegate() = default;

BrowserGpuClientDelegate::~BrowserGpuClientDelegate() = default;

viz::GpuHostImpl* BrowserGpuClientDelegate::EnsureGpuHost() {
  if (auto* host = GpuProcessHost::Get())
    return host->gpu_host();
  return nullptr;
}

viz::HostGpuMemoryBufferManager*
BrowserGpuClientDelegate::GetGpuMemoryBufferManager() {
  return GpuMemoryBufferManagerSingleton::GetInstance();
}

}  // namespace content
