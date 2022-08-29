// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/utility_process_host.h"

#include <memory>
#include <utility>

#include "base/base_switches.h"
#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/i18n/base_i18n_switches.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/threading/thread.h"
#include "build/build_config.h"
#include "components/network_session_configurator/common/network_switches.h"
#include "content/browser/browser_child_process_host_impl.h"
#include "content/browser/gpu/gpu_data_manager_impl.h"
#include "content/browser/renderer_host/render_process_host_impl.h"
#include "content/browser/utility_sandbox_delegate.h"
#include "content/browser/v8_snapshot_files.h"
#include "content/common/child_process_host_impl.h"
#include "content/common/in_process_child_thread_params.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/common/content_client.h"
#include "content/public/common/content_features.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/process_type.h"
#include "content/public/common/sandboxed_process_launcher_delegate.h"
#include "media/base/media_switches.h"
#include "media/webrtc/webrtc_features.h"
#include "sandbox/policy/mojom/sandbox.mojom.h"
#include "sandbox/policy/sandbox_type.h"
#include "sandbox/policy/switches.h"
#include "services/network/public/cpp/network_switches.h"
#include "services/service_manager/public/cpp/interface_provider.h"
#include "ui/base/ui_base_switches.h"
#include "ui/gl/gl_switches.h"

#if BUILDFLAG(IS_ANDROID)
#include "services/network/public/mojom/network_service.mojom.h"
#endif

#if BUILDFLAG(IS_MAC)
#include "components/os_crypt/os_crypt_switches.h"
#endif

#if BUILDFLAG(IS_WIN)
#include "media/capture/capture_switches.h"
#endif

#if BUILDFLAG(IS_CHROMEOS_LACROS)
#include "base/posix/global_descriptors.h"
#include "chromeos/startup/browser_init_params.h"
#include "chromeos/startup/startup_switches.h"  // nogncheck
#include "content/public/common/content_descriptors.h"
#endif

namespace content {

UtilityMainThreadFactoryFunction g_utility_main_thread_factory = nullptr;

void UtilityProcessHost::RegisterUtilityMainThreadFactory(
    UtilityMainThreadFactoryFunction create) {
  g_utility_main_thread_factory = create;
}

UtilityProcessHost::UtilityProcessHost()
    : UtilityProcessHost(nullptr /* client */) {}

UtilityProcessHost::UtilityProcessHost(std::unique_ptr<Client> client)
    : sandbox_type_(sandbox::mojom::Sandbox::kUtility),
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
      child_flags_(ChildProcessHost::CHILD_ALLOW_SELF),
#else
      child_flags_(ChildProcessHost::CHILD_NORMAL),
#endif
      started_(false),
      name_(u"utility process"),
      client_(std::move(client)) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  process_ = std::make_unique<BrowserChildProcessHostImpl>(
      PROCESS_TYPE_UTILITY, this, ChildProcessHost::IpcMode::kNormal);
}

UtilityProcessHost::~UtilityProcessHost() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (client_ && launch_state_ == LaunchState::kLaunchComplete)
    client_->OnProcessTerminatedNormally();
}

base::WeakPtr<UtilityProcessHost> UtilityProcessHost::AsWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

void UtilityProcessHost::SetSandboxType(sandbox::mojom::Sandbox sandbox_type) {
  sandbox_type_ = sandbox_type;
}

const ChildProcessData& UtilityProcessHost::GetData() {
  return process_->GetData();
}

#if BUILDFLAG(IS_POSIX)
void UtilityProcessHost::SetEnv(const base::EnvironmentMap& env) {
  env_ = env;
}
#endif

bool UtilityProcessHost::Start() {
  return StartProcess();
}

// TODO(crbug.com/1328879): Remove this method when fixing the bug.
#if BUILDFLAG(IS_CASTOS) || BUILDFLAG(IS_CAST_ANDROID)
void UtilityProcessHost::RunServiceDeprecated(
    const std::string& service_name,
    mojo::ScopedMessagePipeHandle service_pipe,
    RunServiceDeprecatedCallback callback) {
  if (launch_state_ == LaunchState::kLaunchFailed) {
    std::move(callback).Run(absl::nullopt);
    return;
  }

  process_->GetHost()->RunServiceDeprecated(service_name,
                                            std::move(service_pipe));
  if (launch_state_ == LaunchState::kLaunchComplete) {
    std::move(callback).Run(process_->GetProcess().Pid());
  } else {
    DCHECK_EQ(launch_state_, LaunchState::kLaunchInProgress);
    pending_run_service_callbacks_.push_back(std::move(callback));
  }
}
#endif

void UtilityProcessHost::SetMetricsName(const std::string& metrics_name) {
  metrics_name_ = metrics_name;
}

void UtilityProcessHost::SetName(const std::u16string& name) {
  name_ = name;
}

void UtilityProcessHost::SetExtraCommandLineSwitches(
    std::vector<std::string> switches) {
  extra_switches_ = std::move(switches);
}

mojom::ChildProcess* UtilityProcessHost::GetChildProcess() {
  return static_cast<ChildProcessHostImpl*>(process_->GetHost())
      ->child_process();
}

bool UtilityProcessHost::StartProcess() {
  if (started_)
    return true;

  started_ = true;
  process_->SetName(name_);
  process_->SetMetricsName(metrics_name_);

  if (RenderProcessHost::run_renderer_in_process()) {
    DCHECK(g_utility_main_thread_factory);
    // See comment in RenderProcessHostImpl::Init() for the background on why we
    // support single process mode this way.
    in_process_thread_.reset(g_utility_main_thread_factory(
        InProcessChildThreadParams(GetIOThreadTaskRunner({}),
                                   process_->GetInProcessMojoInvitation())));
    in_process_thread_->Start();
  } else {
    const base::CommandLine& browser_command_line =
        *base::CommandLine::ForCurrentProcess();

    bool has_cmd_prefix =
        browser_command_line.HasSwitch(switches::kUtilityCmdPrefix);

#if BUILDFLAG(IS_ANDROID)
    // readlink("/prof/self/exe") sometimes fails on Android at startup.
    // As a workaround skip calling it here, since the executable name is
    // not needed on Android anyway. See crbug.com/500854.
    std::unique_ptr<base::CommandLine> cmd_line =
        std::make_unique<base::CommandLine>(base::CommandLine::NO_PROGRAM);
    if (metrics_name_ == network::mojom::NetworkService::Name_ &&
        base::FeatureList::IsEnabled(features::kWarmUpNetworkProcess)) {
      process_->EnableWarmUpConnection();
    }
#else
#if BUILDFLAG(IS_MAC)
    if (sandbox_type_ == sandbox::mojom::Sandbox::kServiceWithJit)
      DCHECK_EQ(child_flags_, ChildProcessHost::CHILD_RENDERER);
#endif  // BUILDFLAG(IS_MAC)
    int child_flags = child_flags_;

    // When running under gdb, forking /proc/self/exe ends up forking the gdb
    // executable instead of Chromium. It is almost safe to assume that no
    // updates will happen while a developer is running with
    // |switches::kUtilityCmdPrefix|. See ChildProcessHost::GetChildPath() for
    // a similar case with Valgrind.
    if (has_cmd_prefix)
      child_flags = ChildProcessHost::CHILD_NORMAL;

    base::FilePath exe_path = ChildProcessHost::GetChildPath(child_flags);
    if (exe_path.empty()) {
      NOTREACHED() << "Unable to get utility process binary name.";
      return false;
    }

    std::unique_ptr<base::CommandLine> cmd_line =
        std::make_unique<base::CommandLine>(exe_path);
#endif

    cmd_line->AppendSwitchASCII(switches::kProcessType,
                                switches::kUtilityProcess);
    // Specify the type of utility process for debugging/profiling purposes.
    cmd_line->AppendSwitchASCII(switches::kUtilitySubType, metrics_name_);
    BrowserChildProcessHostImpl::CopyTraceStartupFlags(cmd_line.get());
    std::string locale = GetContentClient()->browser()->GetApplicationLocale();
    cmd_line->AppendSwitchASCII(switches::kLang, locale);

#if BUILDFLAG(IS_WIN)
    cmd_line->AppendArg(switches::kPrefetchArgumentOther);
#endif  // BUILDFLAG(IS_WIN)

    sandbox::policy::SetCommandLineFlagsForSandboxType(cmd_line.get(),
                                                       sandbox_type_);

    // Browser command-line switches to propagate to the utility process.
    static const char* const kSwitchNames[] = {
      network::switches::kAdditionalTrustTokenKeyCommitments,
      network::switches::kForceEffectiveConnectionType,
      network::switches::kHostResolverRules,
      network::switches::kIgnoreCertificateErrorsSPKIList,
      network::switches::kIgnoreUrlFetcherCertRequests,
      network::switches::kLogNetLog,
      network::switches::kNetLogCaptureMode,
      sandbox::policy::switches::kNoSandbox,
#if BUILDFLAG(IS_LINUX) && !BUILDFLAG(IS_CHROMEOS)
      switches::kDisableDevShmUsage,
#endif
#if BUILDFLAG(IS_MAC)
      sandbox::policy::switches::kEnableSandboxLogging,
      os_crypt::switches::kUseMockKeychain,
#endif
      switches::kDisableTestCerts,
      switches::kEnableBackgroundThreadPool,
      switches::kEnableExperimentalCookieFeatures,
      switches::kEnableLogging,
      switches::kForceTextDirection,
      switches::kForceUIDirection,
      switches::kIgnoreCertificateErrors,
      switches::kLoggingLevel,
      switches::kOverrideUseSoftwareGLForTests,
      switches::kOverrideEnabledCdmInterfaceVersion,
      switches::kProxyServer,
      switches::kDisableAcceleratedMjpegDecode,
      switches::kUseFakeDeviceForMediaStream,
      switches::kUseFakeMjpegDecodeAccelerator,
      switches::kUseFileForFakeVideoCapture,
      switches::kUseMockCertVerifierForTesting,
      switches::kMockCertVerifierDefaultResultForTesting,
      switches::kTimeZoneForTesting,
      switches::kUtilityStartupDialog,
      switches::kUseANGLE,
      switches::kUseGL,
      switches::kV,
      switches::kVModule,
#if BUILDFLAG(IS_ANDROID)
      switches::kEnableReachedCodeProfiler,
      switches::kReachedCodeSamplingIntervalUs,
#endif
      switches::kEnableExperimentalWebPlatformFeatures,
      // These flags are used by the audio service:
      switches::kAudioBufferSize,
      switches::kAudioServiceQuitTimeoutMs,
      switches::kDisableAudioOutput,
      switches::kFailAudioStreamCreation,
      switches::kMuteAudio,
      switches::kUseFileForFakeAudioCapture,
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_FREEBSD) || \
    BUILDFLAG(IS_SOLARIS)
      switches::kAlsaInputDevice,
      switches::kAlsaOutputDevice,
#endif
#if defined(USE_CRAS)
      switches::kUseCras,
#endif
#if BUILDFLAG(IS_WIN)
      switches::kDisableHighResTimer,
      switches::kEnableExclusiveAudio,
      switches::kForceWaveAudio,
      switches::kRaiseTimerFrequency,
      switches::kTrySupportedChannelLayouts,
      switches::kUseFakeAudioCaptureTimestamps,
      switches::kWaveOutBuffers,
      switches::kWebXrForceRuntime,
      sandbox::policy::switches::kAddXrAppContainerCaps,
#endif
      network::switches::kUseFirstPartySet,
      network::switches::kIpAddressSpaceOverrides,
#if BUILDFLAG(IS_CHROMEOS)
      switches::kSchedulerBoostUrgent,
#endif
#if BUILDFLAG(IS_CHROMEOS_LACROS)
      switches::kEnableResourcesFileSharing,
#endif
    };
    cmd_line->CopySwitchesFrom(browser_command_line, kSwitchNames,
                               std::size(kSwitchNames));

    network_session_configurator::CopyNetworkSwitches(browser_command_line,
                                                      cmd_line.get());

    if (has_cmd_prefix) {
      // Launch the utility child process with some prefix
      // (usually "xterm -e gdb --args").
      cmd_line->PrependWrapper(browser_command_line.GetSwitchValueNative(
          switches::kUtilityCmdPrefix));
    }

    for (const auto& extra_switch : extra_switches_)
      cmd_line->AppendSwitch(extra_switch);

#if BUILDFLAG(IS_WIN)
    if (media::IsMediaFoundationD3D11VideoCaptureEnabled()) {
      // MediaFoundationD3D11VideoCapture requires Gpu memory buffers,
      // which are unavailable if the GPU process isn't running.
      if (!GpuDataManagerImpl::GetInstance()->IsGpuCompositingDisabled()) {
        cmd_line->AppendSwitch(switches::kVideoCaptureUseGpuMemoryBuffer);
      }
    }
#endif

    auto file_data = std::make_unique<ChildProcessLauncherFileData>();
#if BUILDFLAG(IS_POSIX)
    file_data->files_to_preload = GetV8SnapshotFilesToPreload();
#endif  // BUILDFLAG(IS_POSIX)

#if BUILDFLAG(IS_CHROMEOS_LACROS)
    // Create the file descriptor for Cros startup data and pass it.
    // This FD will be used to obtain BrowserInitParams in Utility process.
    base::ScopedFD cros_startup_fd =
        chromeos::BrowserInitParams::CreateStartupData();
    if (cros_startup_fd.is_valid()) {
      constexpr int kStartupDataFD =
          kCrosStartupDataDescriptor + base::GlobalDescriptors::kBaseDescriptor;
      cmd_line->AppendSwitchASCII(chromeos::switches::kCrosStartupDataFD,
                                  base::NumberToString(kStartupDataFD));
      file_data->additional_remapped_fds.emplace(kStartupDataFD,
                                                 std::move(cros_startup_fd));
    }
#endif  // BUILDFLAG(IS_CHROMEOS_LACROS)

    std::unique_ptr<UtilitySandboxedProcessLauncherDelegate> delegate =
        std::make_unique<UtilitySandboxedProcessLauncherDelegate>(
            sandbox_type_, env_, *cmd_line);

    process_->LaunchWithFileData(std::move(delegate), std::move(cmd_line),
                                 std::move(file_data), true);
  }

  return true;
}

void UtilityProcessHost::OnProcessLaunched() {
  launch_state_ = LaunchState::kLaunchComplete;
// TODO(crbug.com/1328879): Remove this when fixing the bug.
#if BUILDFLAG(IS_CASTOS) || BUILDFLAG(IS_CAST_ANDROID)
  for (auto& callback : pending_run_service_callbacks_)
    std::move(callback).Run(process_->GetProcess().Pid());
  pending_run_service_callbacks_.clear();
#endif
  if (client_)
    client_->OnProcessLaunched(process_->GetProcess());
}

void UtilityProcessHost::OnProcessLaunchFailed(int error_code) {
  launch_state_ = LaunchState::kLaunchFailed;
// TODO(crbug.com/1328879): Remove this when fixing the bug.
#if BUILDFLAG(IS_CASTOS) || BUILDFLAG(IS_CAST_ANDROID)
  for (auto& callback : pending_run_service_callbacks_)
    std::move(callback).Run(absl::nullopt);
  pending_run_service_callbacks_.clear();
#endif
}

void UtilityProcessHost::OnProcessCrashed(int exit_code) {
  if (!client_)
    return;

  // Take ownership of |client_| so the destructor doesn't notify it of
  // termination.
  auto client = std::move(client_);
  client->OnProcessCrashed();
}

absl::optional<std::string> UtilityProcessHost::GetServiceName() {
  return metrics_name_;
}

}  // namespace content
