// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/after_startup_task_utils.h"

#include "base/containers/circular_deque.h"
#include "base/lazy_instance.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/histogram_macros.h"
#include "base/process/process.h"
#include "base/synchronization/atomic_flag.h"
#include "base/task/sequenced_task_runner.h"
#include "base/trace_event/trace_event.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "components/performance_manager/performance_manager_impl.h"
#include "components/performance_manager/public/graph/graph.h"
#include "components/performance_manager/public/graph/page_node.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"

#if BUILDFLAG(IS_CHROMEOS_ASH)
#include "chrome/browser/ash/login/ui/login_display_host.h"
#endif

#if BUILDFLAG(IS_CHROMEOS_LACROS)
#include "chromeos/startup/browser_params_proxy.h"
#endif  // BUILDFLAG(IS_CHROMEOS_LACROS)

using content::BrowserThread;

namespace {

struct AfterStartupTask {
  AfterStartupTask(const base::Location& from_here,
                   const scoped_refptr<base::SequencedTaskRunner>& task_runner,
                   base::OnceClosure task)
      : from_here(from_here), task_runner(task_runner), task(std::move(task)) {}
  ~AfterStartupTask() {}

  const base::Location from_here;
  const scoped_refptr<base::SequencedTaskRunner> task_runner;
  base::OnceClosure task;
};

// The flag may be read on any thread, but must only be set on the UI thread.
base::LazyInstance<base::AtomicFlag>::Leaky g_startup_complete_flag;

// The queue may only be accessed on the UI thread.
base::LazyInstance<base::circular_deque<AfterStartupTask*>>::Leaky
    g_after_startup_tasks;

bool IsBrowserStartupComplete() {
  // Be sure to initialize the LazyInstance on the main thread since the flag
  // may only be set on it's initializing thread.
  if (!g_startup_complete_flag.IsCreated())
    return false;
  return g_startup_complete_flag.Get().IsSet();
}

void RunTask(std::unique_ptr<AfterStartupTask> queued_task) {
  // We're careful to delete the caller's |task| on the target runner's thread.
  DCHECK(queued_task->task_runner->RunsTasksInCurrentSequence());
  std::move(queued_task->task).Run();
}

void ScheduleTask(std::unique_ptr<AfterStartupTask> queued_task) {
  scoped_refptr<base::SequencedTaskRunner> target_runner =
      queued_task->task_runner;
  base::Location from_here = queued_task->from_here;
  target_runner->PostTask(from_here,
                          base::BindOnce(&RunTask, std::move(queued_task)));
}

void QueueTask(std::unique_ptr<AfterStartupTask> queued_task) {
  DCHECK(queued_task);

  // Use CHECK instead of DCHECK to crash earlier. See http://crbug.com/711167
  // for details.
  CHECK(queued_task->task);

  if (!BrowserThread::CurrentlyOn(BrowserThread::UI)) {
    // Posted with USER_VISIBLE priority to avoid this becoming an after startup
    // task itself.
    content::GetUIThreadTaskRunner({base::TaskPriority::USER_VISIBLE})
        ->PostTask(FROM_HERE,
                   base::BindOnce(QueueTask, std::move(queued_task)));
    return;
  }

  // The flag may have been set while the task to invoke this method
  // on the UI thread was inflight.
  if (IsBrowserStartupComplete()) {
    ScheduleTask(std::move(queued_task));
    return;
  }
  g_after_startup_tasks.Get().push_back(queued_task.release());
}

void SetBrowserStartupIsComplete() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  if (IsBrowserStartupComplete())
    return;

  TRACE_EVENT0("startup", "SetBrowserStartupIsComplete");
  g_startup_complete_flag.Get().Set();
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS)
  // Process::Current().CreationTime() is not available on all platforms.
  const base::Time process_creation_time =
      base::Process::Current().CreationTime();
  if (!process_creation_time.is_null()) {
    UMA_HISTOGRAM_LONG_TIMES("Startup.AfterStartupTaskDelayedUntilTime",
                             base::Time::Now() - process_creation_time);
  }
#endif  // BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX) ||
        // BUILDFLAG(IS_CHROMEOS)
  UMA_HISTOGRAM_COUNTS_10000("Startup.AfterStartupTaskCount",
                             g_after_startup_tasks.Get().size());
  for (AfterStartupTask* queued_task : g_after_startup_tasks.Get())
    ScheduleTask(base::WrapUnique(queued_task));
  g_after_startup_tasks.Get().clear();
  g_after_startup_tasks.Get().shrink_to_fit();
}

// Observes the first visible page load and sets the startup complete
// flag accordingly. Ownership is passed to the Performance Manager
// after creation.
class StartupObserver
    : public performance_manager::GraphOwned,
      public performance_manager::PageNode::ObserverDefaultImpl {
 public:
  StartupObserver(const StartupObserver&) = delete;
  StartupObserver& operator=(const StartupObserver&) = delete;

  ~StartupObserver() override = default;

  static void Start();

 private:
  StartupObserver() = default;

  void OnStartupComplete() {
    if (!performance_manager::PerformanceManagerImpl::IsAvailable()) {
      // Already shutting down before startup finished. Do not notify.
      return;
    }

    // This should only be called once.
    if (!startup_complete_) {
      startup_complete_ = true;
      content::GetUIThreadTaskRunner({})->PostTask(
          FROM_HERE, base::BindOnce(&SetBrowserStartupIsComplete));
      // This will result in delete getting called.
      TakeFromGraph();
    }
  }

  // GraphOwned overrides
  void OnPassedToGraph(performance_manager::Graph* graph) override {
    graph->AddPageNodeObserver(this);
  }

  void OnTakenFromGraph(performance_manager::Graph* graph) override {
    graph->RemovePageNodeObserver(this);
  }

  // PageNodeObserver overrides
  void OnLoadingStateChanged(
      const performance_manager::PageNode* page_node,
      performance_manager::PageNode::LoadingState previous_state) override {
    // Only interested in visible PageNodes
    if (page_node->IsVisible()) {
      if (page_node->GetLoadingState() ==
              performance_manager::PageNode::LoadingState::kLoadedIdle ||
          page_node->GetLoadingState() ==
              performance_manager::PageNode::LoadingState::kLoadingTimedOut)
        OnStartupComplete();
    }
  }

  void PassToGraph() {
    // Pass to the performance manager so we can get notified when
    // loading completes.  Ownership of this object is passed to the
    // performance manager.
    DCHECK(performance_manager::PerformanceManagerImpl::IsAvailable());
    performance_manager::PerformanceManagerImpl::PassToGraph(
        FROM_HERE, base::WrapUnique(this));
  }

  void TakeFromGraph() {
    // Remove this object from the performance manager.  This will
    // cause the object to be deleted.
    DCHECK(performance_manager::PerformanceManagerImpl::IsAvailable());
    performance_manager::PerformanceManager::CallOnGraph(
        FROM_HERE, base::BindOnce(
                       [](performance_manager::GraphOwned* observer,
                          performance_manager::Graph* graph) {
                         graph->TakeFromGraph(observer);
                       },
                       base::Unretained(this)));
  }

  bool startup_complete_ = false;
};

// static
void StartupObserver::Start() {
  // Create the StartupObserver and pass it to the Performance Manager which
  // will own it going forward.
  (new StartupObserver)->PassToGraph();
}

}  // namespace

void AfterStartupTaskUtils::StartMonitoringStartup() {
  // For Android, startup completion is signaled via
  // AfterStartupTaskUtils.java. We do not use the StartupObserver.
#if !BUILDFLAG(IS_ANDROID)
#if BUILDFLAG(IS_CHROMEOS_LACROS)
  // For Lacros, there may not be a Browser created at startup.
  if (chromeos::BrowserParamsProxy::Get()->InitialBrowserAction() ==
      crosapi::mojom::InitialBrowserAction::kDoNotOpenWindow) {
    content::GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE, base::BindOnce(&SetBrowserStartupIsComplete));
    return;
  }
#endif

#if BUILDFLAG(IS_CHROMEOS_ASH)
  // If we are on a login screen which does not expect WebUI to be loaded,
  // Browser won't be created at startup.
  if (ash::LoginDisplayHost::default_host() &&
      !ash::LoginDisplayHost::default_host()->IsWebUIStarted()) {
    content::GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE, base::BindOnce(&SetBrowserStartupIsComplete));
    return;
  }
#endif

  StartupObserver::Start();
#endif  // !BUILDFLAG(IS_ANDROID)

  // Add failsafe timeout
  content::GetUIThreadTaskRunner({})->PostDelayedTask(
      FROM_HERE, base::BindOnce(&SetBrowserStartupIsComplete),
      base::Minutes(3));
}

void AfterStartupTaskUtils::PostTask(
    const base::Location& from_here,
    const scoped_refptr<base::SequencedTaskRunner>& destination_runner,
    base::OnceClosure task) {
  if (IsBrowserStartupComplete()) {
    destination_runner->PostTask(from_here, std::move(task));
    return;
  }

  std::unique_ptr<AfterStartupTask> queued_task(
      new AfterStartupTask(from_here, destination_runner, std::move(task)));
  QueueTask(std::move(queued_task));
}

void AfterStartupTaskUtils::SetBrowserStartupIsCompleteForTesting() {
  ::SetBrowserStartupIsComplete();
}

void AfterStartupTaskUtils::SetBrowserStartupIsComplete() {
  ::SetBrowserStartupIsComplete();
}

bool AfterStartupTaskUtils::IsBrowserStartupComplete() {
  return ::IsBrowserStartupComplete();
}

void AfterStartupTaskUtils::UnsafeResetForTesting() {
  DCHECK(g_after_startup_tasks.Get().empty());
  if (!IsBrowserStartupComplete())
    return;
  g_startup_complete_flag.Get().UnsafeResetForTesting();
  DCHECK(!IsBrowserStartupComplete());
}
