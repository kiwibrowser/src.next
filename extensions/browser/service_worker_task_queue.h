// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_BROWSER_SERVICE_WORKER_TASK_QUEUE_H_
#define EXTENSIONS_BROWSER_SERVICE_WORKER_TASK_QUEUE_H_

#include <map>
#include <optional>
#include <vector>
#include "base/containers/flat_map.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_util.h"
#include "base/unguessable_token.h"
#include "base/version.h"
#include "components/keyed_service/core/keyed_service.h"
#include "content/public/browser/service_worker_context.h"
#include "content/public/browser/service_worker_context_observer.h"
#include "extensions/browser/lazy_context_id.h"
#include "extensions/browser/lazy_context_task_queue.h"
#include "extensions/browser/service_worker/worker_id.h"
#include "extensions/common/extension_id.h"
#include "third_party/blink/public/common/service_worker/service_worker_status_code.h"
#include "url/gurl.h"

namespace content {
class BrowserContext;
}

namespace extensions {
class Extension;

// A service worker based background specific LazyContextTaskQueue.
//
// This class queues up and runs tasks added through AddPendingTask, after
// registering and starting extension's background Service Worker script if
// necessary.
//
// There are two sets of concepts/events that are important to this class:
//
// C1) Registering and starting a background worker:
//   Upon extension activation, this class registers the extension's
//   background worker if necessary. After that, if it has queued up tasks
//   in |pending_tasks_|, then it moves on to starting the worker. Registration
//   and start are initiated from this class. Once started, the worker is
//   considered browser process ready. These workers are stored in
//   |worker_state_map_| with |browser_ready| = false until we run tasks.
//
// C2) Listening for worker's state update from the renderer:
//   - Init (DidInitializeServiceWorkerContext) when the worker is initialized,
//       JavaScript starts running after this.
//   - Start (DidStartServiceWorkerContext) when the worker has reached
//       loadstop. The worker is considered ready to run tasks from this task
//       queue. The worker's entry in |worker_state_map_| will carry
//       |renderer_ready| = true.
//   - Stop (DidStopServiceWorkerContext) when the worker is destroyed, we clear
//       its |renderer_ready| status from |worker_state_map_|.
//
// Once a worker reaches readiness in both browser process
// (DidStartWorkerForScope) and worker process (DidStartServiceWorkerContext),
// we consider the worker to be ready to run tasks from |pending_tasks_|.
// Note that events from #C1 and #C2 are somewhat independent, e.g. it is
// possible to see an Init state update from #C2 before #C1 has seen a start
// worker completion.
//
// Sequences of extension activation:
//   This class also assigns a unique activation token to an extension
//   activation so that it can differentiate between two activations of a
//   particular extension (e.g. reloading an extension can cause two
//   activations). |pending_tasks_|, worker registration and start (#C1) have
//   activation tokens attached to them.
//   The activation expires upon extension deactivation, and tasks are dropped
//   from |pending_tasks_|.
//
// TODO(lazyboy): Clean up queue when extension is unloaded/uninstalled.
class ServiceWorkerTaskQueue : public KeyedService,
                               public LazyContextTaskQueue,
                               public content::ServiceWorkerContextObserver {
 public:
  explicit ServiceWorkerTaskQueue(content::BrowserContext* browser_context);

  ServiceWorkerTaskQueue(const ServiceWorkerTaskQueue&) = delete;
  ServiceWorkerTaskQueue& operator=(const ServiceWorkerTaskQueue&) = delete;

  ~ServiceWorkerTaskQueue() override;

  // Convenience method to return the ServiceWorkerTaskQueue for a given
  // |context|.
  static ServiceWorkerTaskQueue* Get(content::BrowserContext* context);

  // Always returns true since we currently request a worker to start for every
  // task sent to it.
  bool ShouldEnqueueTask(content::BrowserContext* context,
                         const Extension* extension) const override;

  // Returns true if the service worker is blink::EmbeddedWorkerStatus::Running.
  // This could, once service worker start logic is refactored in
  // crbug.com/1467015, be refactored into `ShouldEnqueueTask()`.
  bool IsReadyToRunTasks(content::BrowserContext* context,
                         const Extension* extension) const override;

  void AddPendingTask(const LazyContextId& context_id,
                      PendingTask task) override;

  // Performs Service Worker related tasks upon |extension| activation,
  // e.g. registering |extension|'s worker, executing any pending tasks.
  void ActivateExtension(const Extension* extension);
  // Performs Service Worker related tasks upon |extension| deactivation,
  // e.g. unregistering |extension|'s worker.
  void DeactivateExtension(const Extension* extension);

  // Called once an extension Service Worker context was initialized but not
  // necessarily started executing its JavaScript.
  void DidInitializeServiceWorkerContext(int render_process_id,
                                         const ExtensionId& extension_id,
                                         int64_t service_worker_version_id,
                                         int thread_id);
  // Called once an extension Service Worker started running.
  // This can be thought as "loadstop", i.e. the global JS script of the worker
  // has completed executing.
  void DidStartServiceWorkerContext(
      int render_process_id,
      const ExtensionId& extension_id,
      const base::UnguessableToken& activation_token,
      const GURL& service_worker_scope,
      int64_t service_worker_version_id,
      int thread_id);
  // Called once an extension Service Worker was destroyed.
  void DidStopServiceWorkerContext(
      int render_process_id,
      const ExtensionId& extension_id,
      const base::UnguessableToken& activation_token,
      const GURL& service_worker_scope,
      int64_t service_worker_version_id,
      int thread_id);

  // Returns the current activation token for an extension, if the extension
  // is currently activated. Returns std::nullopt if the extension isn't
  // activated.
  std::optional<base::UnguessableToken> GetCurrentActivationToken(
      const ExtensionId& extension_id) const;

  // Activates incognito split mode extensions that are activated in |other|
  // task queue.
  void ActivateIncognitoSplitModeExtensions(ServiceWorkerTaskQueue* other);

  // Retrieves the version of the extension that has currently registered
  // and stored an extension service worker. If there is no such version (if
  // there was never a service worker or if the worker was unregistered),
  // returns an invalid version.
  base::Version RetrieveRegisteredServiceWorkerVersion(
      const ExtensionId& extension_id);

  // content::ServiceWorkerContextObserver:
  void OnRegistrationStored(int64_t registration_id,
                            const GURL& scope) override;
  void OnReportConsoleMessage(int64_t version_id,
                              const GURL& scope,
                              const content::ConsoleMessage& message) override;
  void OnDestruct(content::ServiceWorkerContext* context) override;

  class TestObserver {
   public:
    TestObserver();

    TestObserver(const TestObserver&) = delete;
    TestObserver& operator=(const TestObserver&) = delete;

    virtual ~TestObserver();

    // Called when an extension with id |extension_id| is going to be activated.
    // |will_register_service_worker| is true if a Service Worker will be
    // registered.
    virtual void OnActivateExtension(const ExtensionId& extension_id,
                                     bool will_register_service_worker) {}

    // Called immediately after we send a request to start the worker (whether
    // it ultimately succeeds or fails).
    virtual void RequestedWorkerStart(const ExtensionId& extension_id) {}

    virtual void DidStartWorkerFail(
        const ExtensionId& extension_id,
        size_t num_pending_tasks,
        blink::ServiceWorkerStatusCode status_code) {}

    // Called when SW was re-registered to fix missing registration, and that
    // step finished to mitigate the problem.
    virtual void RegistrationMismatchMitigated(bool mitigation_succeeded) {}

    // Called when a service worker is registered for the extension with the
    // associated `extension_id`.
    virtual void DidInitializeServiceWorkerContext(
        const ExtensionId& extension_id) {}
  };

  static void SetObserverForTest(TestObserver* observer);

  size_t GetNumPendingTasksForTest(const LazyContextId& lazy_context_id);

 private:
  struct SequencedContextId {
    ExtensionId extension_id;
    raw_ptr<content::BrowserContext> browser_context;
    base::UnguessableToken token;

    bool operator<(const SequencedContextId& rhs) const {
      return std::tie(extension_id, browser_context, token) <
             std::tie(rhs.extension_id, rhs.browser_context, rhs.token);
    }
  };

  class WorkerState;

  enum class RegistrationReason {
    REGISTER_ON_EXTENSION_LOAD,
    RE_REGISTER_ON_STATE_MISMATCH,
  };

  void RegisterServiceWorker(RegistrationReason reason,
                             const SequencedContextId& context_id,
                             const Extension& extension);

  void RunTasksAfterStartWorker(const SequencedContextId& context_id);

  void DidRegisterServiceWorker(const SequencedContextId& context_id,
                                RegistrationReason reason,
                                base::Time start_time,
                                blink::ServiceWorkerStatusCode status);
  void DidUnregisterServiceWorker(
      const ExtensionId& extension_id,
      const base::UnguessableToken& activation_token,
      bool success);

  void DidStartWorkerForScope(const SequencedContextId& context_id,
                              base::Time start_time,
                              int64_t version_id,
                              int process_id,
                              int thread_id);
  void DidStartWorkerFail(const SequencedContextId& context_id,
                          base::Time start_time,
                          blink::ServiceWorkerStatusCode status_code);

  // Records that the extension with |extension_id| and |version| successfully
  // registered a Service Worker.
  void SetRegisteredServiceWorkerInfo(const ExtensionId& extension_id,
                                      const base::Version& version);

  // Clears any record of registered Service Worker for the given extension with
  // |extension_id|.
  void RemoveRegisteredServiceWorkerInfo(const ExtensionId& extension_id);

  // If the worker with |context_id| has seen worker start
  // (DidStartWorkerForScope) and load (DidStartServiceWorkerContext) then runs
  // all pending tasks for that worker.
  void RunPendingTasksIfWorkerReady(const SequencedContextId& context_id);

  // Returns true if |activation_token| is the current activation for
  // |extension_id|.
  bool IsCurrentActivation(
      const ExtensionId& extension_id,
      const base::UnguessableToken& activation_token) const;

  const WorkerState* GetWorkerState(const SequencedContextId& context_id) const;
  WorkerState* GetWorkerState(const SequencedContextId& context_id);

  content::ServiceWorkerContext* GetServiceWorkerContext(
      const ExtensionId& extension_id);

  // Starts and stops observing |service_worker_context|.
  //
  // The methods ensure that many:1 relationship of SWContext:SWContextObserver
  // is preserved correctly.
  void StartObserving(content::ServiceWorkerContext* service_worker_context);
  void StopObserving(content::ServiceWorkerContext* service_worker_context);

  // Asynchronously verifies whether an expected SW registration (denoted by
  // |scope|) is there.
  void VerifyRegistration(content::ServiceWorkerContext* service_worker_context,
                          const SequencedContextId& context_id,
                          const GURL& scope);
  void DidVerifyRegistration(const SequencedContextId& context_id,
                             content::ServiceWorkerCapability capability);

  // Emit histograms when we know we're going to start the worker.
  void EmitWorkerWillBeStartedHistograms(const ExtensionId& extension_id);

  std::map<content::ServiceWorkerContext*, int> observing_worker_contexts_;

  // The state of worker of each activated extension.
  std::map<SequencedContextId, WorkerState> worker_state_map_;

  const raw_ptr<content::BrowserContext> browser_context_ = nullptr;

  // A map of Service Worker registrations if this instance is for an
  // off-the-record BrowserContext. These are stored in the ExtensionPrefs
  // for a regular profile.
  // TODO(crbug.com/939664): Make this better by passing in something that
  // will manage storing and retrieving this data.
  base::flat_map<ExtensionId, base::Version> off_the_record_registrations_;

  // Current activation tokens for each activated extensions.
  std::map<ExtensionId, base::UnguessableToken> activation_tokens_;

  // A set of pending service worker registrations. These are registrations that
  // succeeded in the first step (triggering `DidRegisterServiceWorker`), but
  // have not yet been stored. They are cleared out (and the registration state
  // is stored) in response to `OnRegistrationStored`.
  // The key is the extension's ID and the value is the activation token
  // expected for that registration.
  std::map<ExtensionId, base::UnguessableToken> pending_registrations_;

  base::WeakPtrFactory<ServiceWorkerTaskQueue> weak_factory_{this};
};

}  // namespace extensions

#endif  // EXTENSIONS_BROWSER_SERVICE_WORKER_TASK_QUEUE_H_
