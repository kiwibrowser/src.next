// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_UTILITY_PROCESS_HOST_H_
#define CONTENT_BROWSER_UTILITY_PROCESS_HOST_H_

#include <memory>
#include <string>
#include <vector>

#include "base/environment.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/process/launch.h"
#include "build/build_config.h"
#include "build/chromecast_buildflags.h"
#include "content/common/child_process.mojom.h"
#include "content/common/content_export.h"
#include "content/public/browser/browser_child_process_host_delegate.h"
#include "mojo/public/cpp/bindings/generic_pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "sandbox/policy/mojom/sandbox.mojom.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

// TODO(crbug.com/1328879): Remove this when fixing the bug.
#if BUILDFLAG(IS_CASTOS) || BUILDFLAG(IS_CAST_ANDROID)
#include "base/callback.h"
#include "mojo/public/cpp/system/message_pipe.h"
#endif

namespace base {
class Thread;
}  // namespace base

namespace content {
class BrowserChildProcessHostImpl;
class InProcessChildThreadParams;
struct ChildProcessData;

typedef base::Thread* (*UtilityMainThreadFactoryFunction)(
    const InProcessChildThreadParams&);

// This class acts as the browser-side host to a utility child process.  A
// utility process is a short-lived process that is created to run a specific
// task.  This class lives solely on the IO thread.
// If you need a single method call in the process, use StartFooBar(p).
// If you need multiple batches of work to be done in the process, use
// StartBatchMode(), then multiple calls to StartFooBar(p), then finish with
// EndBatchMode().
// If you need to bind Mojo interfaces, use Start() to start the child
// process and then call BindInterface().
//
// Note: If your class keeps a ptr to an object of this type, grab a weak ptr to
// avoid a use after free since this object is deleted synchronously but the
// client notification is asynchronous.  See http://crbug.com/108871.
class CONTENT_EXPORT UtilityProcessHost
    : public BrowserChildProcessHostDelegate {
 public:
  static void RegisterUtilityMainThreadFactory(
      UtilityMainThreadFactoryFunction create);

  // Interface which may be passed to a UtilityProcessHost on construction. All
  // methods are called from the IO thread.
  class Client {
   public:
    virtual ~Client() {}

    virtual void OnProcessLaunched(const base::Process& process) {}
    virtual void OnProcessTerminatedNormally() {}
    virtual void OnProcessCrashed() {}
  };

  UtilityProcessHost();
  explicit UtilityProcessHost(std::unique_ptr<Client> client);

  UtilityProcessHost(const UtilityProcessHost&) = delete;
  UtilityProcessHost& operator=(const UtilityProcessHost&) = delete;

  ~UtilityProcessHost() override;

  base::WeakPtr<UtilityProcessHost> AsWeakPtr();

  // Makes the process run with a specific sandbox type, or unsandboxed if
  // Sandbox::kNoSandbox is specified.
  void SetSandboxType(sandbox::mojom::Sandbox sandbox_type);

  // Returns information about the utility child process.
  const ChildProcessData& GetData();
#if BUILDFLAG(IS_POSIX)
  void SetEnv(const base::EnvironmentMap& env);
#endif

  // Starts the utility process.
  bool Start();

// TODO(crbug.com/1328879): Remove this method when fixing the bug.
#if BUILDFLAG(IS_CASTOS) || BUILDFLAG(IS_CAST_ANDROID)
  // Instructs the utility process to run an instance of the named service,
  // bound to |service_pipe|. This is DEPRECATED and should never be used.
  using RunServiceDeprecatedCallback =
      base::OnceCallback<void(absl::optional<base::ProcessId>)>;
  void RunServiceDeprecated(const std::string& service_name,
                            mojo::ScopedMessagePipeHandle service_pipe,
                            RunServiceDeprecatedCallback callback);
#endif

  // Sets the name of the process to appear in the task manager.
  void SetName(const std::u16string& name);

  // Sets the name used for metrics reporting. This should not be a localized
  // name. This is recorded to metrics, so update UtilityProcessNameHash enum in
  // enums.xml if new values are passed here.
  void SetMetricsName(const std::string& metrics_name);

  void set_child_flags(int flags) { child_flags_ = flags; }

  // Provides extra switches to append to the process's command line.
  void SetExtraCommandLineSwitches(std::vector<std::string> switches);

  // Returns a control interface for the running child process.
  mojom::ChildProcess* GetChildProcess();

 private:
  // Starts the child process if needed, returns true on success.
  bool StartProcess();

  // BrowserChildProcessHostDelegate:
  void OnProcessLaunched() override;
  void OnProcessLaunchFailed(int error_code) override;
  void OnProcessCrashed(int exit_code) override;
  absl::optional<std::string> GetServiceName() override;
  void BindHostReceiver(mojo::GenericPendingReceiver receiver) override;

  // Launch the child process with switches that will setup this sandbox type.
  sandbox::mojom::Sandbox sandbox_type_;

  // ChildProcessHost flags to use when starting the child process.
  int child_flags_;

  // Map of environment variables to values.
  base::EnvironmentMap env_;

  // True if StartProcess() has been called.
  bool started_;

  // The process name used to identify the process in task manager.
  std::u16string name_;

  // The non-localized name used for metrics reporting.
  std::string metrics_name_;

  // Child process host implementation.
  std::unique_ptr<BrowserChildProcessHostImpl> process_;

  // Used in single-process mode instead of |process_|.
  std::unique_ptr<base::Thread> in_process_thread_;

  // Extra command line switches to append.
  std::vector<std::string> extra_switches_;

  // Indicates whether the process has been successfully launched yet, or if
  // launch failed.
  enum class LaunchState {
    kLaunchInProgress,
    kLaunchComplete,
    kLaunchFailed,
  };
  LaunchState launch_state_ = LaunchState::kLaunchInProgress;

// TODO(crbug.com/1328879): Remove this when fixing the bug.
#if BUILDFLAG(IS_CASTOS) || BUILDFLAG(IS_CAST_ANDROID)
  // Collection of callbacks to be run once the process is actually started (or
  // fails to start).
  std::vector<RunServiceDeprecatedCallback> pending_run_service_callbacks_;
#endif

  std::unique_ptr<Client> client_;

  // Used to vend weak pointers, and should always be declared last.
  base::WeakPtrFactory<UtilityProcessHost> weak_ptr_factory_{this};
};

}  // namespace content

#endif  // CONTENT_BROWSER_UTILITY_PROCESS_HOST_H_
