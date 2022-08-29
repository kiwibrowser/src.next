// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_BROWSER_EXTENSION_FUNCTION_H_
#define EXTENSIONS_BROWSER_EXTENSION_FUNCTION_H_

#include <stddef.h>

#include <memory>
#include <string>
#include <vector>

#include "base/callback.h"
#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/process/process.h"
#include "base/task/sequenced_task_runner_helpers.h"
#include "base/timer/elapsed_timer.h"
#include "content/public/browser/browser_thread.h"
#include "extensions/browser/extension_function_histogram_value.h"
#include "extensions/browser/quota_service.h"
#include "extensions/common/constants.h"
#include "extensions/common/extension.h"
#include "extensions/common/features/feature.h"
#include "ipc/ipc_message.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/public/mojom/devtools/console_message.mojom.h"
#include "third_party/blink/public/mojom/service_worker/service_worker_database.mojom-forward.h"
#include "third_party/blink/public/mojom/service_worker/service_worker_object.mojom-forward.h"

namespace base {
class Value;
}

namespace content {
class BrowserContext;
class RenderFrameHost;
class WebContents;
}

namespace extensions {
class ExtensionFunctionDispatcher;
}

#ifdef NDEBUG
#define EXTENSION_FUNCTION_VALIDATE(test) \
  do {                                    \
    if (!(test)) {                        \
      this->SetBadMessage();              \
      return ValidationFailure(this);     \
    }                                     \
  } while (0)
#else   // NDEBUG
#define EXTENSION_FUNCTION_VALIDATE(test) CHECK(test)
#endif  // NDEBUG

#ifdef NDEBUG
#define EXTENSION_FUNCTION_PRERUN_VALIDATE(test) \
  do {                                           \
    if (!(test)) {                               \
      this->SetBadMessage();                     \
      return false;                              \
    }                                            \
  } while (0)
#else  // NDEBUG
#define EXTENSION_FUNCTION_PRERUN_VALIDATE(test) CHECK(test)
#endif  // NDEBUG

// Declares a callable extension function with the given |name|. You must also
// supply a unique |histogramvalue| used for histograms of extension function
// invocation (add new ones at the end of the enum in
// extension_function_histogram_value.h).
// TODO(devlin): This would be nicer if instead we defined the constructor
// for the ExtensionFunction since the histogram value and name should never
// change. Then, we could get rid of the set_ methods for those values on
// ExtensionFunction, and there'd be no possibility of having them be
// "wrong" for a given function. Unfortunately, that would require updating
// each ExtensionFunction and construction site, which, while possible, is
// quite costly.
#define DECLARE_EXTENSION_FUNCTION(name, histogramvalue)               \
 public:                                                               \
  static constexpr const char* static_function_name() { return name; } \
                                                                       \
 public:                                                               \
  static constexpr extensions::functions::HistogramValue               \
  static_histogram_value() {                                           \
    return extensions::functions::histogramvalue;                      \
  }

// Abstract base class for extension functions the ExtensionFunctionDispatcher
// knows how to dispatch to.
class ExtensionFunction : public base::RefCountedThreadSafe<
                              ExtensionFunction,
                              content::BrowserThread::DeleteOnUIThread> {
 public:
  enum ResponseType {
    // The function has succeeded.
    SUCCEEDED,
    // The function has failed.
    FAILED,
    // The input message is malformed.
    BAD_MESSAGE
  };

  using ResponseCallback = base::OnceCallback<void(ResponseType type,
                                                   base::Value::List results,
                                                   const std::string& error)>;

  ExtensionFunction();

  ExtensionFunction(const ExtensionFunction&) = delete;
  ExtensionFunction& operator=(const ExtensionFunction&) = delete;

  static void EnsureShutdownNotifierFactoryBuilt();

  // Returns true if the function has permission to run.
  //
  // This checks the Extension's permissions against the features declared in
  // the *_features.json files. Note that some functions may perform additional
  // checks in Run(), such as for specific host permissions or user gestures.
  bool HasPermission() const;

  // Sends |error| as an error response.
  void RespondWithError(std::string error);

  // The result of a function call.
  //
  // Use NoArguments(), OneArgument(), ArgumentList(), or Error()
  // rather than this class directly.
  class ResponseValueObject {
   public:
    virtual ~ResponseValueObject() {}

    // Returns true for success, false for failure.
    virtual bool Apply() = 0;

   protected:
    void SetFunctionResults(ExtensionFunction* function,
                            base::Value::List results);
    void SetFunctionError(ExtensionFunction* function, std::string error);
  };
  typedef std::unique_ptr<ResponseValueObject> ResponseValue;

  // The action to use when returning from RunAsync.
  //
  // Use RespondNow() or RespondLater() or AlreadyResponded() rather than this
  // class directly.
  class ResponseActionObject {
   public:
    virtual ~ResponseActionObject() {}

    virtual void Execute() = 0;
  };
  typedef std::unique_ptr<ResponseActionObject> ResponseAction;

  // Helper class for tests to force all ExtensionFunction::user_gesture()
  // calls to return true as long as at least one instance of this class
  // exists.
  class ScopedUserGestureForTests {
   public:
    ScopedUserGestureForTests();
    ~ScopedUserGestureForTests();
  };

  // A string used in the case of an unknown error being detected.
  // DON'T USE THIS. It's only here during conversion to flag cases where errors
  // aren't already set.
  // TODO(devlin): Remove this if/when all functions are updated to return real
  // errors.
  static const char kUnknownErrorDoNotUse[];

  // Called before Run() in order to perform a common verification check so that
  // APIs subclassing this don't have to roll their own RunSafe() variants.
  // If this returns false, then Run() is never called, and the function
  // responds immediately with an error (note that error must be non-empty in
  // this case). If this returns true, execution continues on to Run().
  virtual bool PreRunValidation(std::string* error);

  // Runs the extension function if PreRunValidation() succeeds. This should be
  // called at most once over the lifetime of an ExtensionFunction.
  ResponseAction RunWithValidation();

  // Runs the function and returns the action to take when the caller is ready
  // to respond. Callers can expect this is called at most once for the lifetime
  // of an ExtensionFunction.
  //
  // Typical return values might be:
  //   * RespondNow(NoArguments())
  //   * RespondNow(OneArgument(42))
  //   * RespondNow(ArgumentList(my_result.ToValue()))
  //   * RespondNow(Error("Warp core breach"))
  //   * RespondNow(Error("Warp core breach on *", GetURL()))
  //   * RespondLater(), then later,
  //     * Respond(NoArguments())
  //     * ... etc.
  //
  //
  // Callers must call Execute() on the return ResponseAction at some point,
  // exactly once.
  //
  // ExtensionFunction implementations are encouraged to just implement Run.
  [[nodiscard]] virtual ResponseAction Run() = 0;

  // Gets whether quota should be applied to this individual function
  // invocation. This is different to GetQuotaLimitHeuristics which is only
  // invoked once and then cached.
  //
  // Returns false by default.
  virtual bool ShouldSkipQuotaLimiting() const;

  // Optionally adds one or multiple QuotaLimitHeuristic instances suitable for
  // this function to |heuristics|. The ownership of the new QuotaLimitHeuristic
  // instances is passed to the owner of |heuristics|.
  // No quota limiting by default.
  //
  // Only called once per lifetime of the QuotaService.
  virtual void GetQuotaLimitHeuristics(
      extensions::QuotaLimitHeuristics* heuristics) const {}

  // Called when the quota limit has been exceeded. The default implementation
  // returns an error.
  virtual void OnQuotaExceeded(std::string violation_error);

  // Specifies the raw arguments to the function, as a JSON value. Expects a
  // base::Value of type LIST.
  void SetArgs(base::Value args);

  // Retrieves the results of the function as a base::Value::List.
  const base::Value::List* GetResultList() const;

  // Retrieves any error string from the function.
  virtual const std::string& GetError() const;

  virtual void SetBadMessage();

  // Specifies the name of the function. A long-lived string (such as a string
  // literal) must be provided.
  virtual void SetName(const char* name);
  const char* name() const { return name_; }

  int context_id() const { return context_id_; }

  void set_profile_id(void* profile_id) { profile_id_ = profile_id; }
  void* profile_id() const { return profile_id_; }

  void set_extension(
      const scoped_refptr<const extensions::Extension>& extension) {
    extension_ = extension;
  }
  const extensions::Extension* extension() const { return extension_.get(); }
  const std::string& extension_id() const {
    DCHECK(extension())
        << "extension_id() called without an Extension. If " << name()
        << " is allowed to be called without any Extension then you should "
        << "check extension() first. If not, there is a bug in the Extension "
        << "platform, so page somebody in extensions/OWNERS";
    return extension_->id();
  }

  void set_request_id(int request_id) { request_id_ = request_id; }
  int request_id() { return request_id_; }

  void set_source_url(const GURL& source_url) { source_url_ = source_url; }
  const GURL& source_url() const { return source_url_; }

  void set_has_callback(bool has_callback) { has_callback_ = has_callback; }
  bool has_callback() const { return has_callback_; }

  void set_include_incognito_information(bool include) {
    include_incognito_information_ = include;
  }
  bool include_incognito_information() const {
    return include_incognito_information_;
  }

  // Note: consider using ScopedUserGestureForTests instead of calling
  // set_user_gesture directly.
  void set_user_gesture(bool user_gesture) { user_gesture_ = user_gesture; }
  bool user_gesture() const;

  void set_histogram_value(
      extensions::functions::HistogramValue histogram_value) {
    histogram_value_ = histogram_value; }
  extensions::functions::HistogramValue histogram_value() const {
    return histogram_value_; }

  void set_response_callback(ResponseCallback callback) {
    response_callback_ = std::move(callback);
  }

  void set_source_context_type(extensions::Feature::Context type) {
    source_context_type_ = type;
  }
  extensions::Feature::Context source_context_type() const {
    return source_context_type_;
  }

  void set_source_process_id(int source_process_id) {
    source_process_id_ = source_process_id;
  }
  int source_process_id() const {
    return source_process_id_;
  }

  void set_service_worker_version_id(int64_t service_worker_version_id) {
    service_worker_version_id_ = service_worker_version_id;
  }
  int64_t service_worker_version_id() const {
    return service_worker_version_id_;
  }

  bool is_from_service_worker() const {
    return service_worker_version_id_ !=
           blink::mojom::kInvalidServiceWorkerVersionId;
  }

  ResponseType* response_type() const { return response_type_.get(); }

  bool did_respond() const { return did_respond_; }

  // Called when a message was received.
  // Should return true if it processed the message.
  virtual bool OnMessageReceived(const IPC::Message& message);

  // Set the browser context which contains the extension that has originated
  // this function call. Only meant for testing; if unset, uses the
  // BrowserContext from dispatcher().
  void SetBrowserContextForTesting(content::BrowserContext* context);
  content::BrowserContext* browser_context() const;

  void SetRenderFrameHost(content::RenderFrameHost* render_frame_host);
  content::RenderFrameHost* render_frame_host() const {
    return render_frame_host_;
  }

  void SetDispatcher(
      const base::WeakPtr<extensions::ExtensionFunctionDispatcher>& dispatcher);
  extensions::ExtensionFunctionDispatcher* dispatcher() const {
    return dispatcher_.get();
  }

  void set_worker_thread_id(int worker_thread_id) {
    worker_thread_id_ = worker_thread_id;
  }
  int worker_thread_id() const { return worker_thread_id_; }

  // Returns the web contents associated with the sending |render_frame_host_|.
  // This can be null.
  content::WebContents* GetSenderWebContents();

  // Sets did_respond_ to true so that the function won't DCHECK if it never
  // sends a response. Typically, this shouldn't be used, even in testing. It's
  // only for when you want to test functionality that doesn't exercise the
  // Run() aspect of an extension function.
  void ignore_did_respond_for_testing() { did_respond_ = true; }

  void preserve_results_for_testing() { preserve_results_for_testing_ = true; }

  // Same as above, but global. Yuck. Do not add any more uses of this.
  static bool ignore_all_did_respond_for_testing_do_not_use;

  // Called when the service worker in the renderer ACKS the function's
  // response.
  virtual void OnServiceWorkerAck();

 protected:
  // ResponseValues.
  //
  // Success, no arguments to pass to caller.
  ResponseValue NoArguments();
  // Success, a single argument |arg| to pass to caller.
  ResponseValue OneArgument(base::Value arg);
  // Success, two arguments |arg1| and |arg2| to pass to caller.
  // Note that use of this function may imply you
  // should be using the generated Result struct and ArgumentList.
  ResponseValue TwoArguments(base::Value arg1, base::Value arg2);
  // Success, a list of arguments |results| to pass to caller.
  ResponseValue ArgumentList(base::Value::List results);
  // Error. chrome.runtime.lastError.message will be set to |error|.
  ResponseValue Error(std::string error);
  // Error with formatting. Args are processed using
  // ErrorUtils::FormatErrorMessage, that is, each occurrence of * is replaced
  // by the corresponding |s*|:
  // Error("Error in *: *", "foo", "bar") <--> Error("Error in foo: bar").
  ResponseValue Error(const std::string& format, const std::string& s1);
  ResponseValue Error(const std::string& format,
                      const std::string& s1,
                      const std::string& s2);
  ResponseValue Error(const std::string& format,
                      const std::string& s1,
                      const std::string& s2,
                      const std::string& s3);
  // Error with a list of arguments |args| to pass to caller.
  // Using this ResponseValue indicates something is wrong with the API.
  // It shouldn't be possible to have both an error *and* some arguments.
  // Some legacy APIs do rely on it though, like webstorePrivate.
  ResponseValue ErrorWithArguments(base::Value::List args,
                                   const std::string& error);
  // Bad message. A ResponseValue equivalent to EXTENSION_FUNCTION_VALIDATE(),
  // so this will actually kill the renderer and not respond at all.
  ResponseValue BadMessage();

  // ResponseActions.
  //
  // These are exclusively used as return values from Run(). Call Respond(...)
  // to respond at any other time - but as described below, only after Run()
  // has already executed, and only if it returned RespondLater().
  //
  // Respond to the extension immediately with |result|.
  [[nodiscard]] ResponseAction RespondNow(ResponseValue result);
  // Don't respond now, but promise to call Respond(...) later.
  [[nodiscard]] ResponseAction RespondLater();
  // Respond() was already called before Run() finished executing.
  //
  // Assume Run() uses some helper system that accepts callback that Respond()s.
  // If that helper system calls the synchronously in some cases, then use
  // this return value in those cases.
  //
  // FooExtensionFunction::Run() {
  //   Helper::FetchResults(..., base::BindOnce(&Success));
  //   if (did_respond()) return AlreadyResponded();
  //   return RespondLater();
  // }
  // FooExtensionFunction::Success() {
  //   Respond(...);
  // }
  //
  // Helper::FetchResults(..., base::OnceCallback callback) {
  //   if (...)
  //     std::move(callback).Run(..);  // Synchronously call |callback|.
  //   else
  //     // Asynchronously call |callback|.
  // }
  [[nodiscard]] ResponseAction AlreadyResponded();

  // This is the return value of the EXTENSION_FUNCTION_VALIDATE macro, which
  // needs to work from Run(), RunAsync(), and RunSync(). The former of those
  // has a different return type (ResponseAction) than the latter two (bool).
  [[nodiscard]] static ResponseAction ValidationFailure(
      ExtensionFunction* function);

  // If RespondLater() was returned from Run(), functions must at some point
  // call Respond() with |result| as their result.
  //
  // More specifically: call this iff Run() has already executed, it returned
  // RespondLater(), and Respond(...) hasn't already been called.
  void Respond(ResponseValue result);

  // Adds this instance to the set of targets waiting for an ACK from the
  // renderer.
  void AddWorkerResponseTarget();

  virtual ~ExtensionFunction();

  // Called after the response is sent, allowing the function to perform any
  // additional work or cleanup.
  virtual void OnResponded();

  // Called when the `browser_context_` associated with this ExtensionFunction
  // is shutting down. Immediately after this call, `browser_context_` will be
  // set to null. Subclasses should override this method to perform any cleanup
  // that needs to happen before the context shuts down, such as removing
  // observers of KeyedServices.
  virtual void OnBrowserContextShutdown() {}

  // Return true if the argument to this function at |index| was provided and
  // is non-null.
  bool HasOptionalArgument(size_t index);

  // Emits a message to the extension's devtools console.
  void WriteToConsole(blink::mojom::ConsoleMessageLevel level,
                      const std::string& message);

  // Sets the Blob UUIDs whose ownership is being transferred to the renderer.
  void SetTransferredBlobUUIDs(const std::vector<std::string>& blob_uuids);

  bool has_args() const { return args_.has_value(); }

  const base::Value::List& args() const {
    DCHECK(args_);
    return *args_;
  }

  base::Value::List& mutable_args() {
    DCHECK(args_);
    return *args_;
  }

  // The extension that called this function.
  scoped_refptr<const extensions::Extension> extension_;

 private:
  friend struct content::BrowserThread::DeleteOnThread<
      content::BrowserThread::UI>;
  friend class base::DeleteHelper<ExtensionFunction>;
  friend class ResponseValueObject;
  class RenderFrameHostTracker;

  // Called on BrowserContext shutdown.
  void Shutdown();

  // Call with true to indicate success, false to indicate failure. If this
  // failed, |error_| should be set.
  void SendResponseImpl(bool success);

  // The callback for mojom::Renderer::TransferBlobs().
  void OnTransferBlobsAck(int process_id,
                          const std::vector<std::string>& blob_uuids);

  // The arguments to the API. Only non-null if arguments were specified.
  absl::optional<base::Value::List> args_;

  base::ElapsedTimer timer_;

  // The results of the API. This should be populated through the Respond()/
  // RespondNow() methods. In legacy implementations, this is set directly, and
  // should be set before calling SendResponse().
  absl::optional<base::Value::List> results_;

  // Any detailed error from the API. This should be populated by the derived
  // class before Run() returns.
  std::string error_;

  // The callback to run once the function has done execution.
  ResponseCallback response_callback_;

  // Id of this request, used to map the response back to the caller.
  int request_id_ = -1;

  // The id of the profile of this function's extension.
  raw_ptr<void> profile_id_ = nullptr;

  // The name of this function.
  const char* name_ = nullptr;

  // The URL of the frame which is making this request
  GURL source_url_;

  // True if the js caller provides a callback function to receive the response
  // of this call.
  bool has_callback_ = false;

  // True if this callback should include information from incognito contexts
  // even if our profile_ is non-incognito. Note that in the case of a "split"
  // mode extension, this will always be false, and we will limit access to
  // data from within the same profile_ (either incognito or not).
  bool include_incognito_information_ = false;

  // True if the call was made in response of user gesture.
  bool user_gesture_ = false;

  // Any class that gets a malformed message should set this to true before
  // returning.  Usually we want to kill the message sending process.
  bool bad_message_ = false;

#if DCHECK_IS_ON()
  // Set to true when RunWithValidation() is called, to look for callers using
  // the method more than once on a single ExtensionFunction.
  bool did_run_ = false;
#endif

  // The sample value to record with the histogram API when the function
  // is invoked.
  extensions::functions::HistogramValue histogram_value_ =
      extensions::functions::UNKNOWN;

  // The type of the JavaScript context where this call originated.
  extensions::Feature::Context source_context_type_ =
      extensions::Feature::UNSPECIFIED_CONTEXT;

  // The context ID of the browser context where this call originated.
  int context_id_ = extensions::kUnspecifiedContextId;

  // The process ID of the page that triggered this function call, or -1
  // if unknown.
  int source_process_id_ = -1;

  // If this ExtensionFunction was called by an extension Service Worker, then
  // this contains the worker's version id.
  int64_t service_worker_version_id_ =
      blink::mojom::kInvalidServiceWorkerVersionId;

  // The response type of the function, if the response has been sent.
  std::unique_ptr<ResponseType> response_type_;

  // Whether this function has responded.
  // TODO(devlin): Replace this with response_type_ != null.
  bool did_respond_ = false;

  // If set to true, preserves |results_|, even after SendResponseImpl() was
  // called.
  //
  // SendResponseImpl() moves the results out of |this| through
  // ResponseCallback, and calling this method avoids that. This is nececessary
  // for tests that use test_utils::RunFunction*(), as those tests typically
  // retrieve the result afterwards through GetResultList().
  // TODO(https://crbug.com/1268112): Remove this once GetResultList() is
  // removed after ensuring consumers only use RunFunctionAndReturnResult() to
  // retrieve the results.
  bool preserve_results_for_testing_ = false;

  // The dispatcher that will service this extension function call.
  base::WeakPtr<extensions::ExtensionFunctionDispatcher> dispatcher_;

  // Obtained via |dispatcher_| when it is set. It automatically resets to
  // nullptr when the BrowserContext is shutdown (much like a WeakPtr).
  raw_ptr<content::BrowserContext> browser_context_ = nullptr;
  raw_ptr<content::BrowserContext> browser_context_for_testing_ = nullptr;

  // Subscription for a callback that runs when the BrowserContext* is
  // destroyed.
  base::CallbackListSubscription shutdown_subscription_;

  // The RenderFrameHost we will send responses to.
  raw_ptr<content::RenderFrameHost> render_frame_host_ = nullptr;

  std::unique_ptr<RenderFrameHostTracker> tracker_;

  // The blobs transferred to the renderer process.
  std::vector<std::string> transferred_blob_uuids_;

  int worker_thread_id_ = -1;
};

#endif  // EXTENSIONS_BROWSER_EXTENSION_FUNCTION_H_
