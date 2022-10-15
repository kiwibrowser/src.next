// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/utility_sandbox_delegate.h"

#include "base/check.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "components/services/screen_ai/buildflags/buildflags.h"
#include "content/public/common/sandboxed_process_launcher_delegate.h"
#include "content/public/common/zygote/zygote_buildflags.h"
#include "ppapi/buildflags/buildflags.h"
#include "printing/buildflags/buildflags.h"
#include "sandbox/policy/mojom/sandbox.mojom.h"

#if BUILDFLAG(USE_ZYGOTE_HANDLE)
#include "content/common/zygote/zygote_handle_impl_linux.h"
#include "sandbox/policy/sandbox_type.h"
#endif

#if BUILDFLAG(IS_CHROMEOS_ASH)
#include "chromeos/ash/components/assistant/buildflags.h"
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

namespace content {

UtilitySandboxedProcessLauncherDelegate::
    UtilitySandboxedProcessLauncherDelegate(
        sandbox::mojom::Sandbox sandbox_type,
        const base::EnvironmentMap& env,
        const base::CommandLine& cmd_line)
    :
#if BUILDFLAG(IS_POSIX)
      env_(env),
#endif
      sandbox_type_(sandbox_type),
      cmd_line_(cmd_line) {
#if DCHECK_IS_ON()
  bool supported_sandbox_type =
      sandbox_type_ == sandbox::mojom::Sandbox::kNoSandbox ||
#if BUILDFLAG(IS_WIN)
      sandbox_type_ ==
          sandbox::mojom::Sandbox::kNoSandboxAndElevatedPrivileges ||
      sandbox_type_ == sandbox::mojom::Sandbox::kXrCompositing ||
      sandbox_type_ == sandbox::mojom::Sandbox::kPdfConversion ||
      sandbox_type_ == sandbox::mojom::Sandbox::kIconReader ||
      sandbox_type_ == sandbox::mojom::Sandbox::kMediaFoundationCdm ||
      sandbox_type_ == sandbox::mojom::Sandbox::kWindowsSystemProxyResolver ||
#endif
#if BUILDFLAG(IS_MAC)
      sandbox_type_ == sandbox::mojom::Sandbox::kMirroring ||
#endif
      sandbox_type_ == sandbox::mojom::Sandbox::kUtility ||
      sandbox_type_ == sandbox::mojom::Sandbox::kService ||
      sandbox_type_ == sandbox::mojom::Sandbox::kServiceWithJit ||
      sandbox_type_ == sandbox::mojom::Sandbox::kNetwork ||
      sandbox_type_ == sandbox::mojom::Sandbox::kCdm ||
#if BUILDFLAG(ENABLE_OOP_PRINTING)
      sandbox_type_ == sandbox::mojom::Sandbox::kPrintBackend ||
#endif
      sandbox_type_ == sandbox::mojom::Sandbox::kPrintCompositor ||
#if BUILDFLAG(ENABLE_PPAPI)
      sandbox_type_ == sandbox::mojom::Sandbox::kPpapi ||
#endif
#if BUILDFLAG(IS_FUCHSIA)
      sandbox_type_ == sandbox::mojom::Sandbox::kVideoCapture ||
#endif
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS_ASH)
      sandbox_type_ == sandbox::mojom::Sandbox::kHardwareVideoDecoding ||
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS_ASH)
#if BUILDFLAG(IS_CHROMEOS_ASH)
      sandbox_type_ == sandbox::mojom::Sandbox::kIme ||
      sandbox_type_ == sandbox::mojom::Sandbox::kTts ||
#if BUILDFLAG(ENABLE_CROS_LIBASSISTANT)
      sandbox_type_ == sandbox::mojom::Sandbox::kLibassistant ||
#endif  // BUILDFLAG(ENABLE_CROS_LIBASSISTANT)
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)
#if BUILDFLAG(ENABLE_SCREEN_AI_SERVICE)
      sandbox_type_ == sandbox::mojom::Sandbox::kScreenAI ||
#endif
      sandbox_type_ == sandbox::mojom::Sandbox::kAudio ||
      sandbox_type_ == sandbox::mojom::Sandbox::kSpeechRecognition;
  DCHECK(supported_sandbox_type);
#endif  // DCHECK_IS_ON()
}

UtilitySandboxedProcessLauncherDelegate::
    ~UtilitySandboxedProcessLauncherDelegate() {}

sandbox::mojom::Sandbox
UtilitySandboxedProcessLauncherDelegate::GetSandboxType() {
  return sandbox_type_;
}

#if BUILDFLAG(IS_POSIX)
base::EnvironmentMap UtilitySandboxedProcessLauncherDelegate::GetEnvironment() {
  return env_;
}
#endif  // BUILDFLAG(IS_POSIX)

#if BUILDFLAG(USE_ZYGOTE_HANDLE)
ZygoteHandle UtilitySandboxedProcessLauncherDelegate::GetZygote() {
  // If the sandbox has been disabled for a given type, don't use a zygote.
  if (sandbox::policy::IsUnsandboxedSandboxType(sandbox_type_))
    return nullptr;

  // Utility processes which need specialized sandboxes fork from the
  // unsandboxed zygote and then apply their actual sandboxes in the forked
  // process upon startup.
  if (sandbox_type_ == sandbox::mojom::Sandbox::kNetwork ||
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS_ASH)
      sandbox_type_ == sandbox::mojom::Sandbox::kHardwareVideoDecoding ||
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS_ASH)
#if BUILDFLAG(IS_CHROMEOS_ASH)
      sandbox_type_ == sandbox::mojom::Sandbox::kIme ||
      sandbox_type_ == sandbox::mojom::Sandbox::kTts ||
#if BUILDFLAG(ENABLE_CROS_LIBASSISTANT)
      sandbox_type_ == sandbox::mojom::Sandbox::kLibassistant ||
#endif  // BUILDFLAG(ENABLE_CROS_LIBASSISTANT)
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)
      sandbox_type_ == sandbox::mojom::Sandbox::kAudio ||
#if BUILDFLAG(ENABLE_OOP_PRINTING)
      sandbox_type_ == sandbox::mojom::Sandbox::kPrintBackend ||
#endif
#if BUILDFLAG(ENABLE_SCREEN_AI_SERVICE)
      sandbox_type_ == sandbox::mojom::Sandbox::kScreenAI ||
#endif
      sandbox_type_ == sandbox::mojom::Sandbox::kSpeechRecognition) {
    return GetUnsandboxedZygote();
  }

  // All other types use the pre-sandboxed zygote.
  return GetGenericZygote();
}
#endif  // BUILDFLAG(USE_ZYGOTE_HANDLE)

}  // namespace content
