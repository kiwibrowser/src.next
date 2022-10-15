// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/utility_sandbox_delegate.h"

#include "base/check.h"
#include "base/feature_list.h"
#include "base/strings/utf_string_conversions.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/common/content_client.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/sandboxed_process_launcher_delegate.h"
#include "printing/buildflags/buildflags.h"
#include "sandbox/policy/features.h"
#include "sandbox/policy/mojom/sandbox.mojom.h"
#include "sandbox/policy/win/sandbox_win.h"
#include "sandbox/win/src/app_container.h"
#include "sandbox/win/src/sandbox_policy.h"
#include "sandbox/win/src/sandbox_types.h"
#include "services/network/public/mojom/network_service.mojom.h"

namespace content {
namespace {
// Audio:
//  lockdown_level_(sandbox::USER_LOCKDOWN),
//  initial_level_(sandbox::USER_RESTRICTED_SAME_ACCESS),
//
//  job_level_(sandbox::JobLevel::kLockdown),
//
//  integrity_level_(sandbox::INTEGRITY_LEVEL_LOW),
//  delayed_integrity_level_(sandbox::INTEGRITY_LEVEL_UNTRUSTED),
bool AudioPreSpawnTarget(sandbox::TargetConfig* config) {
  // Audio process privilege requirements:
  //  - Lockdown level of USER_NON_ADMIN
  //  - Delayed integrity level of INTEGRITY_LEVEL_LOW
  //
  // For audio streams to create shared memory regions, lockdown level must be
  // at least USER_LIMITED and delayed integrity level INTEGRITY_LEVEL_LOW,
  // otherwise CreateFileMapping() will fail with error code ERROR_ACCESS_DENIED
  // (0x5).
  //
  // For audio input streams to use ISimpleAudioVolume interface, lockdown
  // level must be set to USER_NON_ADMIN, otherwise
  // WASAPIAudioInputStream::Open() will fail with error code E_ACCESSDENIED
  // (0x80070005) when trying to get a reference to ISimpleAudioVolume
  // interface. See
  // https://cs.chromium.org/chromium/src/media/audio/win/audio_low_latency_input_win.cc
  // Use USER_RESTRICTED_NON_ADMIN over USER_NON_ADMIN to prevent failures when
  // AppLocker and similar application whitelisting solutions are in place.
  DCHECK(!config->IsConfigured());

  // Custom default policy allowing audio drivers to read device properties
  // (https://crbug.com/883326).
  config->SetIntegrityLevel(sandbox::INTEGRITY_LEVEL_LOW);
  config->SetLockdownDefaultDacl();
  config->SetDelayedIntegrityLevel(sandbox::INTEGRITY_LEVEL_LOW);
  config->SetTokenLevel(sandbox::USER_RESTRICTED_SAME_ACCESS,
                        sandbox::USER_RESTRICTED_NON_ADMIN);
  config->SetDesktop(sandbox::Desktop::kAlternateWinstation);

  return true;
}

// Sets the sandbox policy for the network service process.
bool NetworkPreSpawnTarget(sandbox::TargetConfig* config) {
  DCHECK(!config->IsConfigured());
  // LPAC sandbox is enabled, so do not use a restricted token.
  if (sandbox::SBOX_ALL_OK !=
      config->SetTokenLevel(sandbox::USER_UNPROTECTED,
                            sandbox::USER_UNPROTECTED)) {
    return false;
  }
  // Network Sandbox in LPAC sandbox needs access to its data files. These
  // files are marked on disk with an ACE that permits this access.
  auto lpac_capability =
      GetContentClient()->browser()->GetLPACCapabilityNameForNetworkService();
  if (lpac_capability.empty())
    return false;
  auto app_container = config->GetAppContainer();
  if (!app_container)
    return false;
  app_container->AddCapability(lpac_capability.c_str());

  // Add capability SID for 'network_service' for loopback access for testing.
  // Run 'checkNetIsolation.exe loopbackExempt -a -n=network_service' while
  // elevated to allow network service loopback access.
  // TODO(wfh): Remove this once the socket broker has landed. See
  // https://crbug.com/841001.
  app_container->AddCapabilitySddl(
      L"S-1-15-3-893703388-718787801-2109771152-172907555-2119217564-716812919-"
      L"652991501");

  // All other app container policies are set in
  // SandboxWin::StartSandboxedProcess.
  return true;
}

// Sets the sandbox policy for the print backend service process.
bool PrintBackendPreSpawnTarget(sandbox::TargetConfig* config) {
  DCHECK(!config->IsConfigured());
  // Print Backend policy lockdown level must be at least USER_LIMITED and
  // delayed integrity level INTEGRITY_LEVEL_LOW, otherwise ::OpenPrinter()
  // will fail with error code ERROR_ACCESS_DENIED (0x5).
  config->SetTokenLevel(sandbox::USER_RESTRICTED_SAME_ACCESS,
                        sandbox::USER_LIMITED);
  config->SetDelayedIntegrityLevel(sandbox::INTEGRITY_LEVEL_LOW);
  return true;
}

std::string UtilityAppContainerId(base::CommandLine& cmd_line) {
  return base::WideToUTF8(cmd_line.GetProgram().value());
}

bool IconReaderPreSpawnTarget(sandbox::TargetConfig* config) {
  DCHECK(!config->IsConfigured());

  config->SetTokenLevel(sandbox::USER_RESTRICTED_SAME_ACCESS,
                        sandbox::USER_LOCKDOWN);
  config->SetDelayedIntegrityLevel(sandbox::INTEGRITY_LEVEL_UNTRUSTED);
  config->SetIntegrityLevel(sandbox::INTEGRITY_LEVEL_LOW);
  config->SetLockdownDefaultDacl();
  config->SetDesktop(sandbox::Desktop::kAlternateWinstation);

  sandbox::MitigationFlags flags = config->GetDelayedProcessMitigations();
  flags |= sandbox::MITIGATION_DYNAMIC_CODE_DISABLE;
  if (sandbox::SBOX_ALL_OK != config->SetDelayedProcessMitigations(flags))
    return false;

  // Allow file read. These should match IconLoader::GroupForFilepath().
  config->AddRule(sandbox::SubSystem::kFiles,
                  sandbox::Semantics::kFilesAllowReadonly, L"\\??\\*.exe");
  config->AddRule(sandbox::SubSystem::kFiles,
                  sandbox::Semantics::kFilesAllowReadonly, L"\\??\\*.dll");
  config->AddRule(sandbox::SubSystem::kFiles,
                  sandbox::Semantics::kFilesAllowReadonly, L"\\??\\*.ico");
  return true;
}

bool XrCompositingPreSpawnTarget(sandbox::TargetConfig* config,
                                 base::CommandLine& cmd_line,
                                 sandbox::mojom::Sandbox sandbox_type) {
  DCHECK(!config->IsConfigured());
  if (!base::FeatureList::IsEnabled(sandbox::policy::features::kXRSandbox))
    return true;
  // TODO(https://crbug.com/881919): Try to harden the XR Compositor
  // sandbox to use mitigations and restrict the token.

  // Unprotected token/job.
  config->SetTokenLevel(sandbox::USER_UNPROTECTED, sandbox::USER_UNPROTECTED);
  sandbox::policy::SandboxWin::SetJobLevel(
      sandbox_type, sandbox::JobLevel::kUnprotected, 0, config);

  // There were issues with some mitigations, causing an inability
  // to load OpenVR and Oculus APIs.
  config->SetProcessMitigations(0);
  config->SetDelayedProcessMitigations(0);

  std::string appcontainer_id = UtilityAppContainerId(cmd_line);
  auto result = sandbox::policy::SandboxWin::AddAppContainerProfileToConfig(
      cmd_line, sandbox_type, appcontainer_id, config);
  if (result != sandbox::SBOX_ALL_OK)
    return false;

  return true;
}
}  // namespace

std::string UtilitySandboxedProcessLauncherDelegate::GetSandboxTag() {
  return sandbox::policy::SandboxWin::GetSandboxTagForDelegate(
      "utility", GetSandboxType());
}

bool UtilitySandboxedProcessLauncherDelegate::GetAppContainerId(
    std::string* appcontainer_id) {
  switch (sandbox_type_) {
    case sandbox::mojom::Sandbox::kMediaFoundationCdm:
    case sandbox::mojom::Sandbox::kNetwork:
    case sandbox::mojom::Sandbox::kWindowsSystemProxyResolver:
      *appcontainer_id = UtilityAppContainerId(cmd_line_);
      return true;
    case sandbox::mojom::Sandbox::kXrCompositing:
      if (base::FeatureList::IsEnabled(sandbox::policy::features::kXRSandbox)) {
        *appcontainer_id = UtilityAppContainerId(cmd_line_);
        return true;
      }
      return false;
    default:
      return false;
  }
}

bool UtilitySandboxedProcessLauncherDelegate::DisableDefaultPolicy() {
  switch (sandbox_type_) {
    case sandbox::mojom::Sandbox::kAudio:
      // Default policy is disabled for audio process to allow audio drivers
      // to read device properties (https://crbug.com/883326).
      return true;
    case sandbox::mojom::Sandbox::kXrCompositing:
      return base::FeatureList::IsEnabled(
          sandbox::policy::features::kXRSandbox);
    case sandbox::mojom::Sandbox::kMediaFoundationCdm:
      // Default policy is disabled for MF Cdm process to allow the application
      // of specific LPAC sandbox policies.
      return true;
    case sandbox::mojom::Sandbox::kNetwork:
      // An LPAC specific policy for network service is set elsewhere.
      return true;
    case sandbox::mojom::Sandbox::kWindowsSystemProxyResolver:
      // Default policy is disabled for Windows System Proxy Resolver process to
      // allow the application of specific LPAC sandbox policies.
      return true;
    default:
      return false;
  }
}

bool UtilitySandboxedProcessLauncherDelegate::ShouldLaunchElevated() {
  return sandbox_type_ ==
         sandbox::mojom::Sandbox::kNoSandboxAndElevatedPrivileges;
}

bool UtilitySandboxedProcessLauncherDelegate::PreSpawnTarget(
    sandbox::TargetPolicy* policy) {
  sandbox::TargetConfig* config = policy->GetConfig();
  if (!config->IsConfigured()) {
    if (sandbox_type_ == sandbox::mojom::Sandbox::kAudio) {
      if (!AudioPreSpawnTarget(config))
        return false;
    }
    if (sandbox_type_ == sandbox::mojom::Sandbox::kNetwork) {
      if (!NetworkPreSpawnTarget(config))
        return false;
    }
    if (sandbox_type_ == sandbox::mojom::Sandbox::kIconReader) {
      if (!IconReaderPreSpawnTarget(config))
        return false;
    }

    if (sandbox_type_ == sandbox::mojom::Sandbox::kXrCompositing) {
      if (!XrCompositingPreSpawnTarget(config, cmd_line_, sandbox_type_))
        return false;
    }

    if (sandbox_type_ == sandbox::mojom::Sandbox::kSpeechRecognition) {
      config->SetDelayedIntegrityLevel(sandbox::INTEGRITY_LEVEL_LOW);
      config->SetIntegrityLevel(sandbox::INTEGRITY_LEVEL_LOW);
      config->SetTokenLevel(sandbox::USER_RESTRICTED_SAME_ACCESS,
                            sandbox::USER_LIMITED);
    }

    if (sandbox_type_ == sandbox::mojom::Sandbox::kMediaFoundationCdm ||
        sandbox_type_ == sandbox::mojom::Sandbox::kWindowsSystemProxyResolver) {
      config->SetTokenLevel(sandbox::USER_UNPROTECTED,
                            sandbox::USER_UNPROTECTED);
    }

    if (sandbox_type_ == sandbox::mojom::Sandbox::kService ||
        sandbox_type_ == sandbox::mojom::Sandbox::kServiceWithJit) {
      auto result =
          sandbox::policy::SandboxWin::AddWin32kLockdownPolicy(config);
      if (result != sandbox::SBOX_ALL_OK)
        return false;
    }

    if (sandbox_type_ == sandbox::mojom::Sandbox::kService) {
      auto delayed_flags = config->GetDelayedProcessMitigations();
      delayed_flags |= sandbox::MITIGATION_DYNAMIC_CODE_DISABLE;
      auto result = config->SetDelayedProcessMitigations(delayed_flags);
      if (result != sandbox::SBOX_ALL_OK)
        return false;
    }
#if BUILDFLAG(ENABLE_PRINTING)
    if (sandbox_type_ == sandbox::mojom::Sandbox::kPrintBackend) {
      if (!PrintBackendPreSpawnTarget(config))
        return false;
    }
#endif
  }

  return GetContentClient()->browser()->PreSpawnChild(
      policy, sandbox_type_,
      ContentBrowserClient::ChildSpawnFlags::kChildSpawnFlagNone);
}

bool UtilitySandboxedProcessLauncherDelegate::ShouldUnsandboxedRunInJob() {
  auto utility_sub_type =
      cmd_line_.GetSwitchValueASCII(switches::kUtilitySubType);
  if (utility_sub_type == network::mojom::NetworkService::Name_)
    return true;
  return false;
}

bool UtilitySandboxedProcessLauncherDelegate::CetCompatible() {
  // TODO(1268074) can remove once v8 is cet-compatible.
  if (sandbox_type_ == sandbox::mojom::Sandbox::kServiceWithJit)
    return false;
  auto utility_sub_type =
      cmd_line_.GetSwitchValueASCII(switches::kUtilitySubType);
  return GetContentClient()->browser()->IsUtilityCetCompatible(
      utility_sub_type);
}
}  // namespace content
