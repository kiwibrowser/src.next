// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_PPAPI_PLUGIN_SANDBOXED_PROCESS_LAUNCHER_DELEGATE_H_
#define CONTENT_BROWSER_PPAPI_PLUGIN_SANDBOXED_PROCESS_LAUNCHER_DELEGATE_H_

#include "build/build_config.h"

#include "content/public/common/sandboxed_process_launcher_delegate.h"
#include "content/public/common/zygote/zygote_buildflags.h"

#if BUILDFLAG(USE_ZYGOTE_HANDLE)
#include "content/public/common/zygote/zygote_handle.h"  // nogncheck
#endif

namespace content {
// NOTE: changes to this class need to be reviewed by the security team.
class CONTENT_EXPORT PpapiPluginSandboxedProcessLauncherDelegate
    : public content::SandboxedProcessLauncherDelegate {
 public:
  PpapiPluginSandboxedProcessLauncherDelegate() = default;

  PpapiPluginSandboxedProcessLauncherDelegate(
      const PpapiPluginSandboxedProcessLauncherDelegate&) = delete;
  PpapiPluginSandboxedProcessLauncherDelegate& operator=(
      const PpapiPluginSandboxedProcessLauncherDelegate&) = delete;

  ~PpapiPluginSandboxedProcessLauncherDelegate() override = default;

#if BUILDFLAG(IS_WIN)
  std::string GetSandboxTag() override;
  bool PreSpawnTarget(sandbox::TargetPolicy* policy) override;
#endif  // BUILDFLAG(IS_WIN)

#if BUILDFLAG(USE_ZYGOTE_HANDLE)
  ZygoteHandle GetZygote() override;
#endif  // BUILDFLAG(USE_ZYGOTE_HANDLE)

  sandbox::mojom::Sandbox GetSandboxType() override;

#if BUILDFLAG(IS_MAC)
  bool DisclaimResponsibility() override;
  bool EnableCpuSecurityMitigations() override;
#endif
};
}  // namespace content

#endif
