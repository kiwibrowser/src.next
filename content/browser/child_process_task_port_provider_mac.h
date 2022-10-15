// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_CHILD_PROCESS_TASK_PORT_PROVIDER_MAC_H_
#define CONTENT_BROWSER_CHILD_PROCESS_TASK_PORT_PROVIDER_MAC_H_

#include <map>

#include "base/mac/dispatch_source_mach.h"
#include "base/mac/scoped_mach_port.h"
#include "base/no_destructor.h"
#include "base/process/port_provider_mac.h"
#include "base/process/process_handle.h"
#include "base/synchronization/lock.h"
#include "content/common/child_process.mojom-forward.h"
#include "content/common/content_export.h"
#include "mojo/public/cpp/platform/platform_handle.h"

namespace content {

// The ChildProcessTaskPortProvider keeps an association between a PID and the
// process's task port. This association is needed for the browser to manipulate
// certain aspects of its child processes.
class CONTENT_EXPORT ChildProcessTaskPortProvider : public base::PortProvider {
 public:
  // Returns the singleton instance.
  static ChildProcessTaskPortProvider* GetInstance();

  ChildProcessTaskPortProvider(const ChildProcessTaskPortProvider&) = delete;
  ChildProcessTaskPortProvider& operator=(const ChildProcessTaskPortProvider&) =
      delete;

  // Called by BrowserChildProcessHostImpl and RenderProcessHostImpl when
  // a new child has been created. This will invoke the GetTaskPort() method
  // on |child_control| and will store the returned port as being associated to
  // |pid|.
  //
  // When the kernel sends a notification that the port has become a dead name,
  // indicating that the child process has died, the association will be
  // removed.
  void OnChildProcessLaunched(base::ProcessHandle pid,
                              mojom::ChildProcess* child_process);

  // base::PortProvider:
  mach_port_t TaskForPid(base::ProcessHandle process) const override;

 private:
  friend class ChildProcessTaskPortProviderTest;
  friend base::NoDestructor<ChildProcessTaskPortProvider>;

  ChildProcessTaskPortProvider();
  ~ChildProcessTaskPortProvider() override;

  // Tests if the macOS system supports collecting task ports. Starting with
  // macOS 12.3, running in the unsupported configuration with the
  // amfi_get_out_of_my_way=1 kernel boot argument set, task ports are
  // immovable. Trying to collect the task ports from child processes will
  // result in the child process crashing in mach_msg(). See
  // https://crbug.com/1291789 for details.
  bool ShouldRequestTaskPorts() const;

  // Callback for mojom::ChildProcess::GetTaskPort reply.
  void OnTaskPortReceived(base::ProcessHandle pid,
                          mojo::PlatformHandle task_port);

  // Event handler for |notification_source_|, invoked for
  // MACH_NOTIFY_DEAD_NAME.
  void OnTaskPortDied();

  // Lock that protects the map below.
  mutable base::Lock lock_;

  // Maps a PID to the corresponding task port.
  using PidToTaskPortMap =
      std::map<base::ProcessHandle, base::mac::ScopedMachSendRight>;
  PidToTaskPortMap pid_to_task_port_;

  // A Mach port that is used to register for dead name notifications from
  // the kernel. All the ports in |pid_to_task_port_| have a notification set
  // up to send to this port.
  base::mac::ScopedMachReceiveRight notification_port_;

  // Dispatch source for |notification_port_|.
  std::unique_ptr<base::DispatchSourceMach> notification_source_;
};

}  // namespace content

#endif  // CONTENT_BROWSER_CHILD_PROCESS_TASK_PORT_PROVIDER_MAC_H_
