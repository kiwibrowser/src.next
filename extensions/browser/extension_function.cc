// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/browser/extension_function.h"

#include <memory>
#include <numeric>
#include <tuple>
#include <utility>

#include "base/bind.h"
#include "base/dcheck_is_on.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/singleton.h"
#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"
#include "base/metrics/user_metrics.h"
#include "base/no_destructor.h"
#include "base/synchronization/lock.h"
#include "base/threading/thread_checker.h"
#include "base/trace_event/memory_allocator_dump.h"
#include "base/trace_event/memory_dump_manager.h"
#include "base/trace_event/memory_dump_provider.h"
#include "base/trace_event/trace_event.h"
#include "components/keyed_service/content/browser_context_keyed_service_shutdown_notifier_factory.h"
#include "components/keyed_service/core/keyed_service_shutdown_notifier.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "extensions/browser/bad_message.h"
#include "extensions/browser/blob_holder.h"
#include "extensions/browser/extension_function_dispatcher.h"
#include "extensions/browser/extension_function_registry.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_util.h"
#include "extensions/browser/extensions_browser_client.h"
#include "extensions/browser/kiosk/kiosk_delegate.h"
#include "extensions/browser/renderer_startup_helper.h"
#include "extensions/common/constants.h"
#include "extensions/common/error_utils.h"
#include "extensions/common/extension_api.h"
#include "extensions/common/extension_messages.h"
#include "extensions/common/manifest_handlers/kiosk_mode_info.h"
#include "extensions/common/mojom/renderer.mojom.h"
#include "third_party/blink/public/mojom/service_worker/service_worker_object.mojom-forward.h"

using content::BrowserThread;
using content::WebContents;
using extensions::ErrorUtils;
using extensions::ExtensionAPI;
using extensions::Feature;

namespace {

class ExtensionFunctionMemoryDumpProvider
    : public base::trace_event::MemoryDumpProvider {
 public:
  ExtensionFunctionMemoryDumpProvider() {
    base::trace_event::MemoryDumpManager::GetInstance()->RegisterDumpProvider(
        this, "ExtensionFunctions", base::ThreadTaskRunnerHandle::Get());
  }

  ExtensionFunctionMemoryDumpProvider(
      const ExtensionFunctionMemoryDumpProvider&) = delete;
  ExtensionFunctionMemoryDumpProvider& operator=(
      const ExtensionFunctionMemoryDumpProvider&) = delete;
  ~ExtensionFunctionMemoryDumpProvider() override {
    base::trace_event::MemoryDumpManager::GetInstance()->UnregisterDumpProvider(
        this);
  }

  void AddFunctionName(const char* function_name) {
    DCHECK(thread_checker_.CalledOnValidThread());
    DCHECK(function_name);
    auto it = function_map_.emplace(function_name, 0);
    it.first->second++;
  }

  void RemoveFunctionName(const char* function_name) {
    DCHECK(thread_checker_.CalledOnValidThread());
    DCHECK(function_name);
    auto it = function_map_.find(function_name);
    DCHECK(it != function_map_.end());
    DCHECK_GE(it->second, static_cast<uint64_t>(1));
    if (it->second == 1)
      function_map_.erase(it);
    else
      it->second--;
  }

  static ExtensionFunctionMemoryDumpProvider& GetInstance() {
    static base::NoDestructor<ExtensionFunctionMemoryDumpProvider> tracker;
    return *tracker;
  }

 private:
  // base::trace_event::MemoryDumpProvider:
  bool OnMemoryDump(const base::trace_event::MemoryDumpArgs& args,
                    base::trace_event::ProcessMemoryDump* pmd) override {
    DCHECK(thread_checker_.CalledOnValidThread());
    auto* dump = pmd->CreateAllocatorDump("extensions/functions");
    uint64_t function_count =
        std::accumulate(function_map_.begin(), function_map_.end(), 0,
                        [](uint64_t total, auto& function_pair) {
                          return total + function_pair.second;
                        });
    dump->AddScalar(base::trace_event::MemoryAllocatorDump::kNameObjectCount,
                    base::trace_event::MemoryAllocatorDump::kUnitsObjects,
                    function_count);
    // Collects the top 5 ExtensionFunctions with the most instances on memory
    // dump.
    std::vector<std::pair<const char*, uint64_t>> results(5);
    std::partial_sort_copy(function_map_.begin(), function_map_.end(),
                           results.begin(), results.end(),
                           [](const auto& lhs, const auto& rhs) {
                             return lhs.second > rhs.second;
                           });
    for (const auto& function_pair : results) {
      if (function_pair.first) {
        TRACE_EVENT2(TRACE_DISABLED_BY_DEFAULT("memory-infra"),
                     "ExtensionFunction::OnMemoryDump", "function",
                     function_pair.first, "count", function_pair.second);
      }
    }
    return true;
  }

  // This map is keyed based on const char* pointer since all the strings used
  // here are defined in the registry held by the caller. The value needs to be
  // stored as pointer to be able to add privacy safe trace events.
  std::map<const char*, uint64_t> function_map_;

  // Makes sure all methods are called from the same thread.
  base::ThreadChecker thread_checker_;
};

void EnsureMemoryDumpProviderExists() {
  std::ignore = ExtensionFunctionMemoryDumpProvider::GetInstance();
}

// Adds Kiosk. prefix to uma histograms if running in a kiosk extension.
std::string WrapUma(const std::string& uma, bool is_kiosk_enabled) {
  if (is_kiosk_enabled)
    return uma + ".Kiosk";
  return uma;
}

// Logs UMA about the performance for a given extension function run.
void LogUma(bool success,
            base::TimeDelta elapsed_time,
            bool is_kiosk_enabled,
            extensions::functions::HistogramValue histogram_value) {
  // Note: Certain functions perform actions that are inherently slow - such as
  // anything waiting on user action. As such, we can't always assume that a
  // long execution time equates to a poorly-performing function.
  if (success) {
    if (elapsed_time < base::Milliseconds(1)) {
      base::UmaHistogramSparse("Extensions.Functions.SucceededTime.LessThan1ms",
                               histogram_value);
    } else if (elapsed_time < base::Milliseconds(5)) {
      base::UmaHistogramSparse("Extensions.Functions.SucceededTime.1msTo5ms",
                               histogram_value);
    } else if (elapsed_time < base::Milliseconds(10)) {
      base::UmaHistogramSparse("Extensions.Functions.SucceededTime.5msTo10ms",
                               histogram_value);
    } else {
      base::UmaHistogramSparse("Extensions.Functions.SucceededTime.Over10ms",
                               histogram_value);
    }
    UMA_HISTOGRAM_TIMES("Extensions.Functions.SucceededTotalExecutionTime",
                        elapsed_time);
  } else {
    if (elapsed_time < base::Milliseconds(1)) {
      base::UmaHistogramSparse("Extensions.Functions.FailedTime.LessThan1ms",
                               histogram_value);
    } else if (elapsed_time < base::Milliseconds(5)) {
      base::UmaHistogramSparse("Extensions.Functions.FailedTime.1msTo5ms",
                               histogram_value);
    } else if (elapsed_time < base::Milliseconds(10)) {
      base::UmaHistogramSparse("Extensions.Functions.FailedTime.5msTo10ms",
                               histogram_value);
    } else {
      base::UmaHistogramSparse("Extensions.Functions.FailedTime.Over10ms",
                               histogram_value);
    }
    base::UmaHistogramTimes(
        WrapUma("Extensions.Functions.FailedTotalExecutionTime",
                is_kiosk_enabled),
        elapsed_time);
  }
}

void LogBadMessage(bool is_kiosk_enabled,
                   extensions::functions::HistogramValue histogram_value) {
  base::RecordAction(base::UserMetricsAction("BadMessageTerminate_EFD"));
  // Track the specific function's |histogram_value|, as this may indicate a
  // bug in that API's implementation.
  base::UmaHistogramSparse(
      WrapUma("Extensions.BadMessageFunctionName", is_kiosk_enabled),
      histogram_value);
}

bool IsKiosk(const extensions::Extension* extension) {
  extensions::ExtensionsBrowserClient* const browser_client =
      extensions::ExtensionsBrowserClient::Get();
  if (!extension || !browser_client)
    return false;
  extensions::KioskDelegate* const kiosk_delegate =
      browser_client->GetKioskDelegate();
  return kiosk_delegate &&
         kiosk_delegate->IsAutoLaunchedKioskApp(extension->id());
}

template <class T>
void ReceivedBadMessage(T* bad_message_sender,
                        extensions::bad_message::BadMessageReason reason,
                        bool is_kiosk_enabled,
                        extensions::functions::HistogramValue histogram_value) {
  LogBadMessage(is_kiosk_enabled, histogram_value);
  // The renderer has done validation before sending extension api requests.
  // Therefore, we should never receive a request that is invalid in a way
  // that JSON validation in the renderer should have caught. It could be an
  // attacker trying to exploit the browser, so we crash the renderer instead.
  extensions::bad_message::ReceivedBadMessage(bad_message_sender, reason);
}

class ArgumentListResponseValue
    : public ExtensionFunction::ResponseValueObject {
 public:
  ArgumentListResponseValue(ExtensionFunction* function,
                            base::Value::List result) {
    SetFunctionResults(function, std::move(result));
    // It would be nice to DCHECK(error.empty()) but some legacy extension
    // function implementations... I'm looking at chrome.input.ime... do this
    // for some reason.
  }

  ~ArgumentListResponseValue() override = default;

  bool Apply() override { return true; }
};

class ErrorWithArgumentsResponseValue : public ArgumentListResponseValue {
 public:
  ErrorWithArgumentsResponseValue(ExtensionFunction* function,
                                  base::Value::List result,
                                  const std::string& error)
      : ArgumentListResponseValue(function, std::move(result)) {
    SetFunctionError(function, error);
  }

  ~ErrorWithArgumentsResponseValue() override = default;

  bool Apply() override { return false; }
};

class ErrorResponseValue : public ExtensionFunction::ResponseValueObject {
 public:
  ErrorResponseValue(ExtensionFunction* function, std::string error) {
    // It would be nice to DCHECK(!error.empty()) but too many legacy extension
    // function implementations don't set error but signal failure.
    SetFunctionError(function, std::move(error));
  }

  ~ErrorResponseValue() override {}

  bool Apply() override { return false; }
};

class BadMessageResponseValue : public ExtensionFunction::ResponseValueObject {
 public:
  explicit BadMessageResponseValue(ExtensionFunction* function) {
    function->SetBadMessage();
    NOTREACHED() << function->name() << ": bad message";
  }

  ~BadMessageResponseValue() override {}

  bool Apply() override { return false; }
};

class RespondNowAction : public ExtensionFunction::ResponseActionObject {
 public:
  typedef base::OnceCallback<void(bool)> SendResponseCallback;
  RespondNowAction(ExtensionFunction::ResponseValue result,
                   SendResponseCallback send_response)
      : result_(std::move(result)), send_response_(std::move(send_response)) {}
  ~RespondNowAction() override = default;

  void Execute() override { std::move(send_response_).Run(result_->Apply()); }

 private:
  ExtensionFunction::ResponseValue result_;
  SendResponseCallback send_response_;
};

class RespondLaterAction : public ExtensionFunction::ResponseActionObject {
 public:
  ~RespondLaterAction() override {}

  void Execute() override {}
};

class AlreadyRespondedAction : public ExtensionFunction::ResponseActionObject {
 public:
  ~AlreadyRespondedAction() override {}

  void Execute() override {}
};

// Used in implementation of ScopedUserGestureForTests.
class UserGestureForTests {
 public:
  static UserGestureForTests* GetInstance();

  // Returns true if there is at least one ScopedUserGestureForTests object
  // alive.
  bool HaveGesture();

  // These should be called when a ScopedUserGestureForTests object is
  // created/destroyed respectively.
  void IncrementCount();
  void DecrementCount();

 private:
  UserGestureForTests();
  friend struct base::DefaultSingletonTraits<UserGestureForTests>;

  base::Lock lock_;  // for protecting access to |count_|
  int count_;
};

// static
UserGestureForTests* UserGestureForTests::GetInstance() {
  return base::Singleton<UserGestureForTests>::get();
}

UserGestureForTests::UserGestureForTests() : count_(0) {}

bool UserGestureForTests::HaveGesture() {
  base::AutoLock autolock(lock_);
  return count_ > 0;
}

void UserGestureForTests::IncrementCount() {
  base::AutoLock autolock(lock_);
  ++count_;
}

void UserGestureForTests::DecrementCount() {
  base::AutoLock autolock(lock_);
  --count_;
}

class BrowserContextShutdownNotifierFactory
    : public BrowserContextKeyedServiceShutdownNotifierFactory {
 public:
  static BrowserContextShutdownNotifierFactory* GetInstance() {
    static base::NoDestructor<BrowserContextShutdownNotifierFactory> s_factory;
    return s_factory.get();
  }

  // No copying.
  BrowserContextShutdownNotifierFactory(
      const BrowserContextShutdownNotifierFactory&) = delete;
  BrowserContextShutdownNotifierFactory& operator=(
      const BrowserContextShutdownNotifierFactory&) = delete;

 private:
  friend class base::NoDestructor<BrowserContextShutdownNotifierFactory>;
  BrowserContextShutdownNotifierFactory()
      : BrowserContextKeyedServiceShutdownNotifierFactory("ExtensionFunction") {
  }
};

}  // namespace

// static
void ExtensionFunction::EnsureShutdownNotifierFactoryBuilt() {
  BrowserContextShutdownNotifierFactory::GetInstance();
}

void ExtensionFunction::ResponseValueObject::SetFunctionResults(
    ExtensionFunction* function,
    base::Value::List results) {
  DCHECK(!function->results_)
      << "Function " << function->name_ << " already has results set.";
  function->results_ = std::move(results);
}

void ExtensionFunction::ResponseValueObject::SetFunctionError(
    ExtensionFunction* function,
    std::string error) {
  DCHECK(function->error_.empty()) << "Function " << function->name_
                                   << "already has an error.";
  function->error_ = std::move(error);
}

// static
bool ExtensionFunction::ignore_all_did_respond_for_testing_do_not_use = false;

// static
const char ExtensionFunction::kUnknownErrorDoNotUse[] = "Unknown error.";

// Helper class to track the lifetime of ExtensionFunction's RenderFrameHost and
// notify the function when it is deleted, as well as forwarding any messages
// to the ExtensionFunction.
class ExtensionFunction::RenderFrameHostTracker
    : public content::WebContentsObserver {
 public:
  explicit RenderFrameHostTracker(ExtensionFunction* function)
      : content::WebContentsObserver(
            WebContents::FromRenderFrameHost(function->render_frame_host())),
        function_(function) {}

  RenderFrameHostTracker(const RenderFrameHostTracker&) = delete;
  RenderFrameHostTracker& operator=(const RenderFrameHostTracker&) = delete;

 private:
  // content::WebContentsObserver:
  void RenderFrameDeleted(
      content::RenderFrameHost* render_frame_host) override {
    if (render_frame_host == function_->render_frame_host())
      function_->SetRenderFrameHost(nullptr);
  }

  bool OnMessageReceived(const IPC::Message& message,
                         content::RenderFrameHost* render_frame_host) override {
    return render_frame_host == function_->render_frame_host() &&
        function_->OnMessageReceived(message);
  }

  raw_ptr<ExtensionFunction> function_;  // Owns us.
};

ExtensionFunction::ExtensionFunction() {
  EnsureMemoryDumpProviderExists();
}

ExtensionFunction::~ExtensionFunction() {
  if (name())  // name_ may not be set in unit tests.
    ExtensionFunctionMemoryDumpProvider::GetInstance().RemoveFunctionName(
        name());
  if (dispatcher() && (render_frame_host() || is_from_service_worker())) {
    dispatcher()->OnExtensionFunctionCompleted(
        extension(), is_from_service_worker(), name());
  }

// The extension function should always respond to avoid leaks in the
// renderer, dangling callbacks, etc. The exception is if the system is
// shutting down or if the extension has been unloaded.
#if DCHECK_IS_ON()
  auto can_be_destroyed_before_responding = [this]() {
    extensions::ExtensionsBrowserClient* browser_client =
        extensions::ExtensionsBrowserClient::Get();
    if (!browser_client || browser_client->IsShuttingDown())
      return true;

    if (ignore_all_did_respond_for_testing_do_not_use)
      return true;

    if (!browser_context())
      return true;

    auto* registry = extensions::ExtensionRegistry::Get(browser_context());
    if (registry && extension() &&
        !registry->enabled_extensions().Contains(extension_id())) {
      return true;
    }

    return false;
  };

  DCHECK(did_respond() || can_be_destroyed_before_responding()) << name();

  // If ignore_did_respond_for_testing() has been called it could cause another
  // DCHECK about not calling Mojo callback.
  // Since the ExtensionFunction request on the frame is a Mojo message
  // which has a reply callback, it should be called before it's destroyed.
  if (!response_callback_.is_null()) {
    constexpr char kShouldCallMojoCallback[] = "Ignored did_respond()";
    std::move(response_callback_)
        .Run(ResponseType::FAILED, base::Value::List(),
             kShouldCallMojoCallback);
  }
#endif  // DCHECK_IS_ON()
}

void ExtensionFunction::AddWorkerResponseTarget() {
  DCHECK(is_from_service_worker());

  if (dispatcher())
    dispatcher()->AddWorkerResponseTarget(this);
}

bool ExtensionFunction::HasPermission() const {
  Feature::Availability availability =
      ExtensionAPI::GetSharedInstance()->IsAvailable(
          name_, extension_.get(), source_context_type_, source_url(),
          extensions::CheckAliasStatus::ALLOWED, context_id_);
  return availability.is_available();
}

void ExtensionFunction::RespondWithError(std::string error) {
  Respond(Error(std::move(error)));
}

bool ExtensionFunction::PreRunValidation(std::string* error) {
  // TODO(crbug.com/625646) This is a partial fix to avoid crashes when certain
  // extension functions run during shutdown. Browser or Notification creation
  // for example create a ScopedKeepAlive, which hit a CHECK if the browser is
  // shutting down. This fixes the current problem as the known issues happen
  // through synchronous calls from Run(), but posted tasks will not be covered.
  // A possible fix would involve refactoring ExtensionFunction: unrefcount
  // here and use weakptrs for the tasks, then have it owned by something that
  // will be destroyed naturally in the course of shut down.
  if (extensions::ExtensionsBrowserClient::Get()->IsShuttingDown()) {
    *error = "The browser is shutting down.";
    return false;
  }

  return true;
}

ExtensionFunction::ResponseAction ExtensionFunction::RunWithValidation() {
#if DCHECK_IS_ON()
  DCHECK(!did_run_);
  did_run_ = true;
#endif

  std::string error;
  if (!PreRunValidation(&error)) {
    DCHECK(!error.empty() || bad_message_);
    return bad_message_ ? ValidationFailure(this) : RespondNow(Error(error));
  }
  return Run();
}

bool ExtensionFunction::ShouldSkipQuotaLimiting() const {
  return false;
}

void ExtensionFunction::OnQuotaExceeded(std::string violation_error) {
  RespondWithError(std::move(violation_error));
}

void ExtensionFunction::SetArgs(base::Value args) {
  DCHECK(args.is_list());
  DCHECK(!args_.has_value());
  args_ = std::move(args.GetList());
}

const base::Value::List* ExtensionFunction::GetResultList() const {
  return results_ ? &(*results_) : nullptr;
}

const std::string& ExtensionFunction::GetError() const {
  return error_;
}

void ExtensionFunction::SetName(const char* name) {
  DCHECK_EQ(nullptr, name_) << "SetName() called twice!";
  DCHECK_NE(nullptr, name) << "Passed in nullptr to SetName()!";
  name_ = name;
  ExtensionFunctionMemoryDumpProvider::GetInstance().AddFunctionName(name);
}

void ExtensionFunction::SetBadMessage() {
  bad_message_ = true;

  if (render_frame_host()) {
    ReceivedBadMessage(render_frame_host()->GetProcess(),
                       is_from_service_worker()
                           ? extensions::bad_message::EFD_BAD_MESSAGE_WORKER
                           : extensions::bad_message::EFD_BAD_MESSAGE,
                       IsKiosk(extension_.get()), histogram_value());
  }
}

bool ExtensionFunction::user_gesture() const {
  return user_gesture_ || UserGestureForTests::GetInstance()->HaveGesture();
}

bool ExtensionFunction::OnMessageReceived(const IPC::Message& message) {
  return false;
}

void ExtensionFunction::SetBrowserContextForTesting(
    content::BrowserContext* context) {
  browser_context_for_testing_ = context;
}

content::BrowserContext* ExtensionFunction::browser_context() const {
  if (browser_context_for_testing_)
    return browser_context_for_testing_;
  return browser_context_;
}

void ExtensionFunction::SetDispatcher(
    const base::WeakPtr<extensions::ExtensionFunctionDispatcher>& dispatcher) {
  dispatcher_ = dispatcher;

  // Update |browser_context_| to the one from the dispatcher. Make it reset to
  // nullptr on shutdown.
  if (!dispatcher_ || !dispatcher_->browser_context()) {
    browser_context_ = nullptr;
    shutdown_subscription_ = base::CallbackListSubscription();
    return;
  }
  browser_context_ = dispatcher_->browser_context();
  context_id_ = extensions::util::GetBrowserContextId(browser_context_);
  shutdown_subscription_ =
      BrowserContextShutdownNotifierFactory::GetInstance()
          ->Get(browser_context_)
          ->Subscribe(base::BindRepeating(&ExtensionFunction::Shutdown,
                                          base::Unretained(this)));
}

void ExtensionFunction::Shutdown() {
  // Wait until the end of this function to delete |this|, in case
  // OnBrowserContextShutdown() decrements the refcount.
  scoped_refptr<ExtensionFunction> keep_alive{this};

  // Allow the extension function to perform any cleanup before nulling out
  // `browser_context_`.
  OnBrowserContextShutdown();
  browser_context_ = nullptr;
}

void ExtensionFunction::SetRenderFrameHost(
    content::RenderFrameHost* render_frame_host) {
  // An extension function from Service Worker does not have a RenderFrameHost.
  if (is_from_service_worker()) {
    DCHECK(!render_frame_host);
    return;
  }

  DCHECK_NE(render_frame_host_ == nullptr, render_frame_host == nullptr);
  render_frame_host_ = render_frame_host;
  tracker_.reset(render_frame_host ? new RenderFrameHostTracker(this)
                                   : nullptr);
}

content::WebContents* ExtensionFunction::GetSenderWebContents() {
  return render_frame_host_
             ? content::WebContents::FromRenderFrameHost(render_frame_host_)
             : nullptr;
}

void ExtensionFunction::OnServiceWorkerAck() {
  // Derived classes must override this if they require and implement an
  // ACK from the Service Worker.
  NOTREACHED();
}

ExtensionFunction::ResponseValue ExtensionFunction::NoArguments() {
  return ResponseValue(
      new ArgumentListResponseValue(this, base::Value::List()));
}

ExtensionFunction::ResponseValue ExtensionFunction::OneArgument(
    base::Value arg) {
  base::Value::List args;
  args.Append(std::move(arg));
  return ResponseValue(new ArgumentListResponseValue(this, std::move(args)));
}

ExtensionFunction::ResponseValue ExtensionFunction::TwoArguments(
    base::Value arg1,
    base::Value arg2) {
  base::Value::List args;
  args.Append(std::move(arg1));
  args.Append(std::move(arg2));
  return ResponseValue(new ArgumentListResponseValue(this, std::move(args)));
}

ExtensionFunction::ResponseValue ExtensionFunction::ArgumentList(
    base::Value::List results) {
  return ResponseValue(new ArgumentListResponseValue(this, std::move(results)));
}

ExtensionFunction::ResponseValue ExtensionFunction::Error(std::string error) {
  return ResponseValue(new ErrorResponseValue(this, std::move(error)));
}

ExtensionFunction::ResponseValue ExtensionFunction::Error(
    const std::string& format,
    const std::string& s1) {
  return ResponseValue(
      new ErrorResponseValue(this, ErrorUtils::FormatErrorMessage(format, s1)));
}

ExtensionFunction::ResponseValue ExtensionFunction::Error(
    const std::string& format,
    const std::string& s1,
    const std::string& s2) {
  return ResponseValue(new ErrorResponseValue(
      this, ErrorUtils::FormatErrorMessage(format, s1, s2)));
}

ExtensionFunction::ResponseValue ExtensionFunction::Error(
    const std::string& format,
    const std::string& s1,
    const std::string& s2,
    const std::string& s3) {
  return ResponseValue(new ErrorResponseValue(
      this, ErrorUtils::FormatErrorMessage(format, s1, s2, s3)));
}

ExtensionFunction::ResponseValue ExtensionFunction::ErrorWithArguments(
    base::Value::List args,
    const std::string& error) {
  return ResponseValue(
      new ErrorWithArgumentsResponseValue(this, std::move(args), error));
}

ExtensionFunction::ResponseValue ExtensionFunction::BadMessage() {
  return ResponseValue(new BadMessageResponseValue(this));
}

ExtensionFunction::ResponseAction ExtensionFunction::RespondNow(
    ResponseValue result) {
  return ResponseAction(new RespondNowAction(
      std::move(result),
      base::BindOnce(&ExtensionFunction::SendResponseImpl, this)));
}

ExtensionFunction::ResponseAction ExtensionFunction::RespondLater() {
  return ResponseAction(new RespondLaterAction());
}

ExtensionFunction::ResponseAction ExtensionFunction::AlreadyResponded() {
  DCHECK(did_respond()) << "ExtensionFunction did not call Respond(),"
                           " but Run() returned AlreadyResponded()";
  return ResponseAction(new AlreadyRespondedAction());
}

// static
ExtensionFunction::ResponseAction ExtensionFunction::ValidationFailure(
    ExtensionFunction* function) {
  return function->RespondNow(function->BadMessage());
}

void ExtensionFunction::Respond(ResponseValue result) {
  SendResponseImpl(result->Apply());
}

void ExtensionFunction::OnResponded() {
  if (!transferred_blob_uuids_.empty()) {
    extensions::mojom::Renderer* renderer =
        extensions::RendererStartupHelperFactory::GetForBrowserContext(
            browser_context())
            ->GetRenderer(
                content::RenderProcessHost::FromID(source_process_id()));
    if (renderer) {
      renderer->TransferBlobs(
          base::BindOnce(&ExtensionFunction::OnTransferBlobsAck, this,
                         source_process_id(), transferred_blob_uuids_));
    }
  }
}

bool ExtensionFunction::HasOptionalArgument(size_t index) {
  DCHECK(args_);
  return index < args_->size() && !(*args_)[index].is_none();
}

void ExtensionFunction::WriteToConsole(blink::mojom::ConsoleMessageLevel level,
                                       const std::string& message) {
  // TODO(crbug.com/1096166): Service Worker-based extensions don't have a
  // RenderFrameHost.
  if (!render_frame_host_)
    return;
  render_frame_host_->AddMessageToConsole(level, message);
}

void ExtensionFunction::SetTransferredBlobUUIDs(
    const std::vector<std::string>& blob_uuids) {
  DCHECK(transferred_blob_uuids_.empty());  // Should only be called once.
  transferred_blob_uuids_ = blob_uuids;
}

void ExtensionFunction::SendResponseImpl(bool success) {
  DCHECK(!response_callback_.is_null());
  DCHECK(!did_respond_) << name_;
  did_respond_ = true;

  ResponseType response = success ? SUCCEEDED : FAILED;
  if (bad_message_) {
    response = BAD_MESSAGE;
    LOG(ERROR) << "Bad extension message " << name_;
  }
  response_type_ = std::make_unique<ResponseType>(response);

  // If results were never set, we send an empty argument list.
  if (!results_)
    results_.emplace();

  base::Value::List results;
  if (preserve_results_for_testing_) {
    // Keep |results_| untouched.
    results = results_->Clone();
  } else {
    results = std::move(*results_);
  }

  std::move(response_callback_).Run(response, std::move(results), GetError());
  LogUma(success, timer_.Elapsed(), IsKiosk(extension_.get()),
         histogram_value_);

  OnResponded();
}

void ExtensionFunction::OnTransferBlobsAck(
    int process_id,
    const std::vector<std::string>& blob_uuids) {
  content::RenderProcessHost* process =
      content::RenderProcessHost::FromID(process_id);
  if (!process)
    return;

  extensions::BlobHolder::FromRenderProcessHost(process)->DropBlobs(blob_uuids);
}

ExtensionFunction::ScopedUserGestureForTests::ScopedUserGestureForTests() {
  UserGestureForTests::GetInstance()->IncrementCount();
}

ExtensionFunction::ScopedUserGestureForTests::~ScopedUserGestureForTests() {
  UserGestureForTests::GetInstance()->DecrementCount();
}
