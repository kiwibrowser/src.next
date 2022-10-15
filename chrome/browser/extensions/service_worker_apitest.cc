// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include <utility>

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/json/json_reader.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/scoped_observation.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/bind.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "base/threading/thread_restrictions.h"
#include "build/build_config.h"
#include "chrome/browser/extensions/api/permissions/permissions_api.h"
#include "chrome/browser/extensions/browsertest_util.h"
#include "chrome/browser/extensions/chrome_test_extension_loader.h"
#include "chrome/browser/extensions/crx_installer.h"
#include "chrome/browser/extensions/error_console/error_console.h"
#include "chrome/browser/extensions/extension_action_runner.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/unpacked_installer.h"
#include "chrome/browser/gcm/gcm_profile_service_factory.h"
#include "chrome/browser/notifications/notification_display_service_factory.h"
#include "chrome/browser/notifications/notification_display_service_tester.h"
#include "chrome/browser/notifications/notification_permission_context.h"
#include "chrome/browser/notifications/stub_notification_display_service.h"
#include "chrome/browser/push_messaging/push_messaging_app_identifier.h"
#include "chrome/browser/push_messaging/push_messaging_features.h"
#include "chrome/browser/push_messaging/push_messaging_service_factory.h"
#include "chrome/browser/push_messaging/push_messaging_service_impl.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/api/web_navigation.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "components/gcm_driver/fake_gcm_profile_service.h"
#include "components/gcm_driver/instance_id/fake_gcm_driver_for_instance_id.h"
#include "components/version_info/channel.h"
#include "components/version_info/version_info.h"
#include "content/public/browser/console_message.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/service_worker_context.h"
#include "content/public/browser/service_worker_context_observer.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/page_type.h"
#include "content/public/common/result_codes.h"
#include "content/public/test/background_sync_test_util.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/service_worker_test_helpers.h"
#include "extensions/browser/browsertest_util.h"
#include "extensions/browser/event_router.h"
#include "extensions/browser/extension_function_histogram_value.h"
#include "extensions/browser/extension_host.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/process_manager.h"
#include "extensions/browser/process_map.h"
#include "extensions/browser/service_worker/service_worker_test_utils.h"
#include "extensions/browser/service_worker_task_queue.h"
#include "extensions/common/api/test.h"
#include "extensions/common/extensions_client.h"
#include "extensions/common/features/feature_channel.h"
#include "extensions/common/manifest_handlers/background_info.h"
#include "extensions/common/mojom/event_dispatcher.mojom.h"
#include "extensions/common/permissions/permissions_data.h"
#include "extensions/common/value_builder.h"
#include "extensions/common/verifier_formats.h"
#include "extensions/test/extension_background_page_waiter.h"
#include "extensions/test/extension_test_message_listener.h"
#include "extensions/test/result_catcher.h"
#include "extensions/test/test_extension_dir.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "services/network/public/cpp/is_potentially_trustworthy.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/public/common/storage_key/storage_key.h"
#include "ui/message_center/public/cpp/notification.h"
#include "url/url_constants.h"

namespace extensions {

namespace {

using ::testing::HasSubstr;

content::ServiceWorkerContext* GetServiceWorkerContext(
    content::BrowserContext* browser_context) {
  return browser_context->GetDefaultStoragePartition()
      ->GetServiceWorkerContext();
}

class WebContentsLoadStopObserver : content::WebContentsObserver {
 public:
  explicit WebContentsLoadStopObserver(content::WebContents* web_contents)
      : content::WebContentsObserver(web_contents),
        load_stop_observed_(false) {}

  WebContentsLoadStopObserver(const WebContentsLoadStopObserver&) = delete;
  WebContentsLoadStopObserver& operator=(const WebContentsLoadStopObserver&) =
      delete;

  void WaitForLoadStop() {
    if (load_stop_observed_)
      return;
    message_loop_runner_ = new content::MessageLoopRunner;
    message_loop_runner_->Run();
  }

 private:
  void DidStopLoading() override {
    load_stop_observed_ = true;
    if (message_loop_runner_)
      message_loop_runner_->Quit();
  }

  bool load_stop_observed_;
  scoped_refptr<content::MessageLoopRunner> message_loop_runner_;
};

// Extension ID for tests that use
// "worker_based_background/test_extension.pem" key.
constexpr char kTestExtensionId[] = "ogdbpbegnmindpdjfafpmpicikegejdj";

}  // namespace

class ErrorObserver : public ErrorConsole::Observer {
 public:
  ErrorObserver(size_t errors_expected, ErrorConsole* error_console)
      : errors_expected_(errors_expected),
        error_console_(error_console),
        errors_observed_(0) {
    observation_.Observe(error_console_.get());
  }

  // ErrorConsole::Observer implementation.
  void OnErrorAdded(const ExtensionError* error) override {
    ++errors_observed_;
    if (errors_observed_ >= errors_expected_) {
      run_loop_.Quit();
    }
  }

  // Spin until the appropriate number of errors have been observed.
  void WaitForErrors() {
    if (errors_observed_ < errors_expected_) {
      run_loop_.Run();
    }
  }

 private:
  size_t errors_expected_;
  raw_ptr<ErrorConsole> error_console_;
  size_t errors_observed_;
  base::ScopedObservation<ErrorConsole, ErrorConsole::Observer> observation_{
      this};
  base::RunLoop run_loop_;
};

class ServiceWorkerTest : public ExtensionApiTest {
 public:
  ServiceWorkerTest(const ServiceWorkerTest&) = delete;
  ServiceWorkerTest& operator=(const ServiceWorkerTest&) = delete;

 protected:
  ServiceWorkerTest() = default;
  ~ServiceWorkerTest() override = default;

  void SetUpOnMainThread() override {
    ExtensionApiTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
  }

  // Returns the ProcessManager for the test's profile.
  ProcessManager* process_manager() { return ProcessManager::Get(profile()); }

  // Starts running a test from the background page test extension.
  //
  // This registers a service worker with |script_name|, and fetches the
  // registration result.
  const Extension* StartTestFromBackgroundPage(const char* script_name) {
    ExtensionTestMessageListener ready_listener("ready");
    const Extension* extension =
        LoadExtension(test_data_dir_.AppendASCII("service_worker/background"));
    CHECK(extension);
    CHECK(ready_listener.WaitUntilSatisfied());

    ExtensionHost* background_host =
        process_manager()->GetBackgroundHostForExtension(extension->id());
    CHECK(background_host);

    std::string error;
    CHECK(content::ExecuteScriptAndExtractString(
        background_host->host_contents(),
        base::StringPrintf("test.registerServiceWorker('%s')", script_name),
        &error));
    if (!error.empty())
      ADD_FAILURE() << "Got unexpected error " << error;
    return extension;
  }

  // Navigates the browser to a new tab at |url|, waits for it to load, then
  // returns it.
  content::WebContents* Navigate(const GURL& url) {
    ui_test_utils::NavigateToURLWithDisposition(
        browser(), url, WindowOpenDisposition::NEW_FOREGROUND_TAB,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
    content::WebContents* web_contents =
        browser()->tab_strip_model()->GetActiveWebContents();
    content::WaitForLoadStop(web_contents);
    return web_contents;
  }

  // Navigates the browser to |url| and returns the new tab's page type.
  content::PageType NavigateAndGetPageType(const GURL& url) {
    return Navigate(url)
        ->GetController()
        .GetLastCommittedEntry()
        ->GetPageType();
  }

  // Extracts the innerText from |contents|.
  std::string ExtractInnerText(content::WebContents* contents) {
    std::string inner_text;
    if (!content::ExecuteScriptAndExtractString(
            contents,
            "window.domAutomationController.send(document.body.innerText)",
            &inner_text)) {
      ADD_FAILURE() << "Failed to get inner text for "
                    << contents->GetVisibleURL();
    }
    return inner_text;
  }

  // Navigates the browser to |url|, then returns the innerText of the new
  // tab's WebContents' main frame.
  std::string NavigateAndExtractInnerText(const GURL& url) {
    return ExtractInnerText(Navigate(url));
  }

  size_t GetWorkerRefCount(const blink::StorageKey& key) {
    content::ServiceWorkerContext* sw_context =
        GetServiceWorkerContext(browser()->profile());
    return sw_context->CountExternalRequestsForTest(key);
  }
};

class ServiceWorkerBasedBackgroundTest : public ServiceWorkerTest {
 public:
  ServiceWorkerBasedBackgroundTest() = default;

  ServiceWorkerBasedBackgroundTest(const ServiceWorkerBasedBackgroundTest&) =
      delete;
  ServiceWorkerBasedBackgroundTest& operator=(
      const ServiceWorkerBasedBackgroundTest&) = delete;

  ~ServiceWorkerBasedBackgroundTest() override {}

  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
    ServiceWorkerTest::SetUpOnMainThread();
  }

  // Returns the only running worker id for |extension_id|.
  // Returns absl::nullopt if there isn't any worker running or more than one
  // worker is running for |extension_id|.
  absl::optional<WorkerId> GetUniqueRunningWorkerId(
      const ExtensionId& extension_id) {
    ProcessManager* process_manager = ProcessManager::Get(profile());
    std::vector<WorkerId> all_workers =
        process_manager->GetAllWorkersIdsForTesting();
    absl::optional<WorkerId> running_worker_id;
    for (const WorkerId& worker_id : all_workers) {
      if (worker_id.extension_id == extension_id) {
        if (running_worker_id)  // More than one worker present.
          return absl::nullopt;
        running_worker_id = worker_id;
      }
    }
    return running_worker_id;
  }

  bool ExtensionHasRenderProcessHost(const ExtensionId& extension_id) {
    ProcessMap* process_map = ProcessMap::Get(browser()->profile());
    content::RenderProcessHost::iterator it =
        content::RenderProcessHost::AllHostsIterator();
    while (!it.IsAtEnd()) {
      if (process_map->Contains(extension_id, it.GetCurrentValue()->GetID())) {
        return true;
      }
      it.Advance();
    }
    return false;
  }
};

class ServiceWorkerBasedBackgroundTestWithNotification
    : public ServiceWorkerBasedBackgroundTest {
 public:
  ServiceWorkerBasedBackgroundTestWithNotification() {}

  ServiceWorkerBasedBackgroundTestWithNotification(
      const ServiceWorkerBasedBackgroundTestWithNotification&) = delete;
  ServiceWorkerBasedBackgroundTestWithNotification& operator=(
      const ServiceWorkerBasedBackgroundTestWithNotification&) = delete;

  ~ServiceWorkerBasedBackgroundTestWithNotification() override = default;

  void SetUpOnMainThread() override {
    ServiceWorkerBasedBackgroundTest::SetUpOnMainThread();
    display_service_tester_ =
        std::make_unique<NotificationDisplayServiceTester>(
            browser()->profile());
  }

  void TearDownOnMainThread() override {
    display_service_tester_.reset();
    ServiceWorkerBasedBackgroundTest::TearDownOnMainThread();
  }

 protected:
  // Returns a vector with the Notification objects that are being displayed
  // by the notification display service. Synchronous.
  std::vector<message_center::Notification> GetDisplayedNotifications() const {
    return display_service_tester_->GetDisplayedNotificationsForType(
        NotificationHandler::Type::WEB_PERSISTENT);
  }

  std::unique_ptr<NotificationDisplayServiceTester> display_service_tester_;
};

enum class ManifestVersion { kTwo, kThree };
class ServiceWorkerWithManifestVersionTest
    : public ServiceWorkerBasedBackgroundTest,
      public testing::WithParamInterface<ManifestVersion> {
 public:
  ServiceWorkerWithManifestVersionTest() = default;
  ~ServiceWorkerWithManifestVersionTest() override = default;

  const Extension* LoadExtensionInternal(const base::FilePath& path) {
    LoadOptions options;
    if (GetParam() == ManifestVersion::kThree)
      options.load_as_manifest_version_3 = true;

    return LoadExtension(path, options);
  }
};

// Tests that Service Worker based background pages can be loaded and they can
// receive extension events.
// The extension is installed and loaded during this step and it registers
// an event listener for tabs.onCreated event. The step also verifies that tab
// creation correctly fires the listener.
IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest, PRE_Basic) {
  ExtensionTestMessageListener newtab_listener("CREATED");
  newtab_listener.set_failure_message("CREATE_FAILED");
  ExtensionTestMessageListener worker_listener("WORKER_RUNNING");
  worker_listener.set_failure_message("NON_WORKER_SCOPE");
  const Extension* extension = LoadExtension(test_data_dir_.AppendASCII(
      "service_worker/worker_based_background/basic"));
  ASSERT_TRUE(extension);
  const ExtensionId extension_id = extension->id();
  EXPECT_TRUE(worker_listener.WaitUntilSatisfied());

  const GURL url = embedded_test_server()->GetURL("/extensions/test_file.html");
  content::WebContents* new_web_contents =
      browsertest_util::AddTab(browser(), url);
  EXPECT_TRUE(new_web_contents);
  EXPECT_TRUE(newtab_listener.WaitUntilSatisfied());

  // Service Worker extension does not have ExtensionHost.
  EXPECT_FALSE(process_manager()->GetBackgroundHostForExtension(extension_id));
}

// After browser restarts, this test step ensures that opening a tab fires
// tabs.onCreated event listener to the extension without explicitly loading the
// extension. This is because the extension registered a listener before browser
// restarted in PRE_Basic.
IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest, Basic) {
  ExtensionTestMessageListener newtab_listener("CREATED");
  newtab_listener.set_failure_message("CREATE_FAILED");
  const GURL url = embedded_test_server()->GetURL("/extensions/test_file.html");
  content::WebContents* new_web_contents =
      browsertest_util::AddTab(browser(), url);
  EXPECT_TRUE(new_web_contents);
  EXPECT_TRUE(newtab_listener.WaitUntilSatisfied());
}

// Tests that an extension with a service worker script registered in non-root
// directory can successfully be registered.
IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest, NonRootDirectory) {
  ASSERT_TRUE(RunExtensionTest(
      "service_worker/worker_based_background/non_root_directory"))
      << message_;
}

// Tests that a module service worker with static import can successfully be
// registered.
IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest,
                       ModuleServiceWorkerWithStaticImport) {
  ASSERT_TRUE(
      RunExtensionTest("service_worker/worker_based_background/"
                       "module_service_worker_static_import"))
      << message_;
}

// Tests that registering a module service worker with dynamic import fails.
IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest,
                       ModuleServiceWorkerWithDynamicImport) {
  ErrorConsole* error_console = ErrorConsole::Get(profile());
  profile()->GetPrefs()->SetBoolean(prefs::kExtensionsUIDeveloperMode, true);
  constexpr size_t kErrorsExpected = 1u;
  ErrorObserver observer(kErrorsExpected, error_console);

  const Extension* extension = LoadExtension(
      test_data_dir_.AppendASCII("service_worker/worker_based_background/"
                                 "module_service_worker_dynamic_import"));

  observer.WaitForErrors();
  const ErrorList& error_list =
      error_console->GetErrorsForExtension(extension->id());
  ASSERT_EQ(kErrorsExpected, error_list.size());
  ASSERT_EQ(
      error_list[0]->message(),
      std::u16string(u"Uncaught (in promise) TypeError: import() is disallowed "
                     u"on ServiceWorkerGlobalScope by the HTML specification. "
                     u"See https://github.com/w3c/ServiceWorker/issues/1356."));
}

// Tests a service worker registration that fails due to the worker script
// synchronously throwing a runtime error.
IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest,
                       ServiceWorkerWithRegistrationFailure) {
  // The error console only captures errors if the user is in dev mode.
  profile()->GetPrefs()->SetBoolean(prefs::kExtensionsUIDeveloperMode, true);

  constexpr size_t kErrorsExpected = 2u;
  ErrorConsole* error_console = ErrorConsole::Get(profile());
  ErrorObserver observer(kErrorsExpected, error_console);

  const Extension* extension = LoadExtension(
      test_data_dir_.AppendASCII("service_worker/worker_based_background/"
                                 "service_worker_registration_failure"),
                                 {.wait_for_renderers = false});

  ASSERT_TRUE(extension);
  observer.WaitForErrors();
  const ErrorList& error_list =
      error_console->GetErrorsForExtension(extension->id());
  ASSERT_EQ(kErrorsExpected, error_list.size());

  std::vector<std::u16string> error_message_list;
  for (std::string::size_type i = 0; i < error_list.size(); i++) {
    error_message_list.push_back(error_list[i]->message());
  }
  // status code 15: kErrorScriptEvaluateFailed
  EXPECT_THAT(error_message_list,
              testing::UnorderedElementsAre(
                  u"Uncaught Error: lol",
                  u"Service worker registration failed. Status code: 15"));
}

// Tests that an error is generated if there is a syntax error in the service
// worker script.
IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest, SyntaxError) {
  ErrorConsole* error_console = ErrorConsole::Get(profile());
  // Error is observed on extension UI for developer mode only.
  profile()->GetPrefs()->SetBoolean(prefs::kExtensionsUIDeveloperMode, true);
  const size_t kErrorsExpected = 1u;
  ErrorObserver observer(kErrorsExpected, error_console);

  ExtensionTestMessageListener test_listener("ready",
                                             ReplyBehavior::kWillReply);
  const Extension* extension = LoadExtension(test_data_dir_.AppendASCII(
      "service_worker/worker_based_background/syntax_error"));
  ASSERT_TRUE(extension);

  ASSERT_TRUE(test_listener.WaitUntilSatisfied());
  test_listener.Reply("");
  observer.WaitForErrors();

  const ErrorList& error_list =
      error_console->GetErrorsForExtension(extension->id());
  ASSERT_EQ(kErrorsExpected, error_list.size());
  EXPECT_EQ(ExtensionError::RUNTIME_ERROR, error_list[0]->type());
  EXPECT_THAT(base::UTF16ToUTF8(error_list[0]->message()),
              HasSubstr("Error handling response: TypeError: "
                        "console.lg is not a function"));
}

// Tests that an error is generated if there is an undefined variable in the
// service worker script.
IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest, UndefinedVariable) {
  ErrorConsole* error_console = ErrorConsole::Get(profile());
  // Error is observed on extension UI for developer mode only.
  profile()->GetPrefs()->SetBoolean(prefs::kExtensionsUIDeveloperMode, true);
  const size_t kErrorsExpected = 1u;
  ErrorObserver observer(kErrorsExpected, error_console);

  ExtensionTestMessageListener test_listener("ready",
                                             ReplyBehavior::kWillReply);
  const Extension* extension = LoadExtension(test_data_dir_.AppendASCII(
      "service_worker/worker_based_background/undefined_variable"));
  ASSERT_TRUE(extension);

  ASSERT_TRUE(test_listener.WaitUntilSatisfied());
  test_listener.Reply("");
  observer.WaitForErrors();

  const ErrorList& error_list =
      error_console->GetErrorsForExtension(extension->id());
  ASSERT_EQ(kErrorsExpected, error_list.size());
  EXPECT_EQ(ExtensionError::RUNTIME_ERROR, error_list[0]->type());
  EXPECT_THAT(base::UTF16ToUTF8(error_list[0]->message()),
              HasSubstr("Error handling response: ReferenceError: "
                        "undefined_variable is not defined"));
}

// Tests that an error is generated if console.error() is called from an
// extension's service worker.
IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest, ConsoleError) {
  ErrorConsole* error_console = ErrorConsole::Get(profile());
  // Error is observed on extension UI for developer mode only.
  profile()->GetPrefs()->SetBoolean(prefs::kExtensionsUIDeveloperMode, true);
  const size_t kErrorsExpected = 1u;
  ErrorObserver observer(kErrorsExpected, error_console);

  ASSERT_TRUE(
      RunExtensionTest("service_worker/worker_based_background/console_error"))
      << message_;

  observer.WaitForErrors();
  const ErrorList& error_list = error_console->GetErrorsForExtension(
      ExtensionBrowserTest::last_loaded_extension_id());
  ASSERT_EQ(kErrorsExpected, error_list.size());
  EXPECT_EQ(ExtensionError::RUNTIME_ERROR, error_list[0]->type());
  EXPECT_THAT(base::UTF16ToUTF8(error_list[0]->message()),
              HasSubstr("Logged from MV3 service worker"));
}

// Tests that an extension can fetch a file scheme URL from the service worker,
// if it has file access.
IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest,
                       FetchFileSchemeURLWithFileAccess) {
  ASSERT_TRUE(
      RunExtensionTest("service_worker/worker_based_background/"
                       "fetch_file_scheme_url_with_file_access",
                       {}, {.allow_file_access = true}))
      << message_;
}

// Tests that an extension can not fetch a file scheme URL from the service
// worker, if it does not have file access.
IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest,
                       FetchFileSchemeURLWithNoFileAccess) {
  ASSERT_TRUE(
      RunExtensionTest("service_worker/worker_based_background/"
                       "fetch_file_scheme_url_with_no_file_access",
                       {}))
      << message_;
}

// Tests chrome.runtime.onInstalled fires for extension service workers.
IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest, OnInstalledEvent) {
  ASSERT_TRUE(RunExtensionTest(
      "service_worker/worker_based_background/events_on_installed"))
      << message_;
}

// Tests chrome.runtime.id and chrome.runtime.getURL().
IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest, RuntimeMisc) {
  ASSERT_TRUE(
      RunExtensionTest("service_worker/worker_based_background/runtime_misc"))
      << message_;
}

// Tests chrome.app namespace is undefined for service worker-based extensions.
IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest, ChromeAppUndefined) {
  ASSERT_TRUE(RunExtensionTest(
      "service_worker/worker_based_background/chrome_app_undefined"))
      << message_;
}

// Tests chrome.storage APIs.
IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest, StorageSetAndGet) {
  ASSERT_TRUE(
      RunExtensionTest("service_worker/worker_based_background/storage"))
      << message_;
}

// Tests chrome.storage APIs are only enabled with permission.
IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest, StorageNoPermissions) {
  ASSERT_TRUE(RunExtensionTest(
      "service_worker/worker_based_background/storage_no_permissions"))
      << message_;
}

// Tests chrome.tabs APIs.
IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest, TabsBasic) {
  base::HistogramTester histogram_tester;
  ASSERT_TRUE(
      RunExtensionTest("service_worker/worker_based_background/tabs_basic"))
      << message_;
  // Extension should issue two chrome.tabs.create calls, verify that we logged
  // histograms for them.
  EXPECT_EQ(2, histogram_tester.GetBucketCount(
                   "Extensions.Functions.ExtensionServiceWorkerCalls",
                   functions::HistogramValue::TABS_CREATE));
}

// Tests chrome.tabs events.
IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest, TabsEvents) {
  ASSERT_TRUE(
      RunExtensionTest("service_worker/worker_based_background/tabs_events"))
      << message_;
}

// Tests chrome.tabs APIs.
IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest, TabsExecuteScript) {
  ASSERT_TRUE(RunExtensionTest(
      "service_worker/worker_based_background/tabs_execute_script"))
      << message_;
}

// Tests chrome.windows APIs.
IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest, WindowsBasic) {
  base::HistogramTester histogram_tester;
  ASSERT_TRUE(
      RunExtensionTest("service_worker/worker_based_background/windows_basic"))
      << message_;
  // Extension should issue one chrome.windows.create call and two
  // chrome.windows.getAll, verify that we logged histogram for it.
  EXPECT_EQ(1, histogram_tester.GetBucketCount(
                   "Extensions.Functions.ExtensionServiceWorkerCalls",
                   functions::HistogramValue::WINDOWS_CREATE));
  EXPECT_EQ(2, histogram_tester.GetBucketCount(
                   "Extensions.Functions.ExtensionServiceWorkerCalls",
                   functions::HistogramValue::WINDOWS_GETALL));
  EXPECT_EQ(1, histogram_tester.GetBucketCount(
                   "Extensions.Functions.ExtensionServiceWorkerCalls",
                   functions::HistogramValue::WINDOWS_GET));
}

// Tests chrome.webRequest APIs.
IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest, WebRequest) {
  ASSERT_TRUE(
      RunExtensionTest("service_worker/worker_based_background/web_request"))
      << message_;
}

// Tests more chrome.webRequest APIs. Any potentially flaky tests are isolated
// here.
// Flaky (crbug.com/1072715).
IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest, DISABLED_WebRequest2) {
  ASSERT_TRUE(
      RunExtensionTest("service_worker/worker_based_background/web_request2"))
      << message_;
}

// Tests chrome.webRequest APIs in blocking mode.
IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest, WebRequestBlocking) {
  // Try to load the page before installing the extension, which should work.
  const GURL url = embedded_test_server()->GetURL("/extensions/test_file.html");
  EXPECT_EQ(content::PAGE_TYPE_NORMAL, NavigateAndGetPageType(url));

  // Install the extension and navigate again to the page.
  ExtensionTestMessageListener ready_listener("ready");
  ASSERT_TRUE(LoadExtension(test_data_dir_.AppendASCII(
      "service_worker/worker_based_background/web_request_blocking")));
  ASSERT_TRUE(ready_listener.WaitUntilSatisfied());
  EXPECT_EQ(content::PAGE_TYPE_ERROR, NavigateAndGetPageType(url));
}

// Tests chrome.webNavigation APIs.
IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest, FilteredEvents) {
  ASSERT_TRUE(RunExtensionTest(
      "service_worker/worker_based_background/filtered_events"))
      << message_;
}

// Listens for |message| from extension Service Worker early so that tests can
// wait for the message on startup (and not miss it).
template <const char message[]>
class ServiceWorkerWithEarlyMessageListenerTest
    : public ServiceWorkerBasedBackgroundTest {
 public:
  ServiceWorkerWithEarlyMessageListenerTest() : test_message_(message) {}

  ServiceWorkerWithEarlyMessageListenerTest(
      const ServiceWorkerWithEarlyMessageListenerTest&) = delete;
  ServiceWorkerWithEarlyMessageListenerTest& operator=(
      const ServiceWorkerWithEarlyMessageListenerTest&) = delete;

  ~ServiceWorkerWithEarlyMessageListenerTest() override = default;

  bool WaitForMessage() { return listener_->WaitUntilSatisfied(); }

  void CreatedBrowserMainParts(content::BrowserMainParts* main_parts) override {
    // At this point, the notification service is initialized but the profile
    // and extensions have not.
    listener_ = std::make_unique<ExtensionTestMessageListener>(test_message_);
    ServiceWorkerBasedBackgroundTest::CreatedBrowserMainParts(main_parts);
  }

 private:
  const std::string test_message_;
  std::unique_ptr<ExtensionTestMessageListener> listener_;
};

constexpr char kMsgOnStartup[] = "onStartup event";
using ServiceWorkerOnStartupEventTest =
    ServiceWorkerWithEarlyMessageListenerTest<kMsgOnStartup>;

// Tests "runtime.onStartup" for extension SW.
IN_PROC_BROWSER_TEST_F(ServiceWorkerOnStartupEventTest, PRE_Event) {
  ASSERT_TRUE(RunExtensionTest(
      "service_worker/worker_based_background/on_startup_event"))
      << message_;
}

// Flaky (crbug.com/1243815).
IN_PROC_BROWSER_TEST_F(ServiceWorkerOnStartupEventTest, DISABLED_Event) {
  EXPECT_TRUE(WaitForMessage());
}

constexpr char kMsgWorkerRunning[] = "WORKER_RUNNING";
class ServiceWorkerRegistrationAtStartupTest
    : public ServiceWorkerWithEarlyMessageListenerTest<kMsgWorkerRunning>,
      public ServiceWorkerTaskQueue::TestObserver {
 public:
  ServiceWorkerRegistrationAtStartupTest() {
    ServiceWorkerTaskQueue::SetObserverForTest(this);
  }

  ServiceWorkerRegistrationAtStartupTest(
      const ServiceWorkerRegistrationAtStartupTest&) = delete;
  ServiceWorkerRegistrationAtStartupTest& operator=(
      const ServiceWorkerRegistrationAtStartupTest&) = delete;

  ~ServiceWorkerRegistrationAtStartupTest() override {
    ServiceWorkerTaskQueue::SetObserverForTest(nullptr);
  }

  // ServiceWorkerTaskQueue::TestObserver:
  void OnActivateExtension(const ExtensionId& extension_id,
                           bool will_register_service_worker) override {
    if (extension_id != kExtensionId)
      return;

    will_register_service_worker_ = will_register_service_worker;

    extension_activated_ = true;
    if (run_loop_)
      run_loop_->Quit();
  }

  void WaitForOnActivateExtension() {
    if (extension_activated_)
      return;
    run_loop_ = std::make_unique<base::RunLoop>();
    run_loop_->Run();
  }

  bool WillRegisterServiceWorker() {
    return will_register_service_worker_.value();
  }

 protected:
  static const char kExtensionId[];

 private:
  bool extension_activated_ = false;
  absl::optional<bool> will_register_service_worker_;
  std::unique_ptr<base::RunLoop> run_loop_;
};

// Observes ServiceWorkerTaskQueue::DidStartWorkerFail.
class ServiceWorkerStartFailureObserver
    : public ServiceWorkerTaskQueue::TestObserver {
 public:
  explicit ServiceWorkerStartFailureObserver(const ExtensionId& extension_id)
      : extension_id_(extension_id) {
    ServiceWorkerTaskQueue::SetObserverForTest(this);
  }
  ~ServiceWorkerStartFailureObserver() override {
    ServiceWorkerTaskQueue::SetObserverForTest(nullptr);
  }

  ServiceWorkerStartFailureObserver(const ServiceWorkerStartFailureObserver&) =
      delete;
  ServiceWorkerStartFailureObserver& operator=(
      const ServiceWorkerStartFailureObserver&) = delete;

  size_t WaitForDidStartWorkerFailAndGetTaskCount() {
    if (pending_tasks_count_at_worker_failure_)
      return *pending_tasks_count_at_worker_failure_;

    run_loop_.Run();
    return *pending_tasks_count_at_worker_failure_;
  }

  // ServiceWorkerTaskQueue::TestObserver:
  void DidStartWorkerFail(const ExtensionId& extension_id,
                          size_t num_pending_tasks,
                          blink::ServiceWorkerStatusCode status_code) override {
    if (extension_id == extension_id_) {
      pending_tasks_count_at_worker_failure_ = num_pending_tasks;
      status_code_ = status_code;
      run_loop_.Quit();
    }
  }

  absl::optional<blink::ServiceWorkerStatusCode> status_code() {
    return status_code_;
  }

 private:
  // Holds number of pending tasks for worker at the time DidStartWorkerFail is
  // observed.
  absl::optional<size_t> pending_tasks_count_at_worker_failure_;

  ExtensionId extension_id_;
  base::RunLoop run_loop_;
  absl::optional<blink::ServiceWorkerStatusCode> status_code_;
};

// Test extension id at
// api_test/service_worker/worker_based_background/registration_at_startup/.
const char ServiceWorkerRegistrationAtStartupTest::kExtensionId[] =
    "gnchfmandajfaiajniicagenfmhdjila";

// Tests that Service Worker registration for existing extension isn't issued
// upon browser restart.
// Regression test for https://crbug.com/889687.
IN_PROC_BROWSER_TEST_F(ServiceWorkerRegistrationAtStartupTest,
                       PRE_ExtensionActivationDoesNotReregister) {
  const Extension* extension = LoadExtension(test_data_dir_.AppendASCII(
      "service_worker/worker_based_background/registration_at_startup"));
  ASSERT_TRUE(extension);
  EXPECT_EQ(kExtensionId, extension->id());
  // Wait for "WORKER_RUNNING" message from the Service Worker.
  EXPECT_TRUE(WaitForMessage());
  EXPECT_TRUE(WillRegisterServiceWorker());
}

// Flaky on all platforms (https://crbug.com/1169238).
IN_PROC_BROWSER_TEST_F(ServiceWorkerRegistrationAtStartupTest,
                       DISABLED_ExtensionActivationDoesNotReregister) {
  // Since the extension has onStartup listener, the Service Worker will run on
  // browser start and we'll see "WORKER_RUNNING" message from the worker.
  EXPECT_TRUE(WaitForMessage());
  // As the extension activated during first run on PRE_ step, it shouldn't
  // re-register the Service Worker upon browser restart.
  EXPECT_FALSE(WillRegisterServiceWorker());
}

// Class that dispatches an event to |extension_id| right after a
// non-lazy listener to the event is added from the extension's Service Worker.
class EarlyWorkerMessageSender : public EventRouter::Observer {
 public:
  EarlyWorkerMessageSender(content::BrowserContext* browser_context,
                           const ExtensionId& extension_id,
                           std::unique_ptr<Event> event)
      : browser_context_(browser_context),
        event_router_(EventRouter::EventRouter::Get(browser_context_)),
        extension_id_(extension_id),
        event_(std::move(event)),
        listener_("PASS") {
    DCHECK(browser_context_);
    listener_.set_failure_message("FAIL");
    event_router_->RegisterObserver(this, event_->event_name);
  }

  EarlyWorkerMessageSender(const EarlyWorkerMessageSender&) = delete;
  EarlyWorkerMessageSender& operator=(const EarlyWorkerMessageSender&) = delete;

  ~EarlyWorkerMessageSender() override {
    event_router_->UnregisterObserver(this);
  }

  // EventRouter::Observer:
  void OnListenerAdded(const EventListenerInfo& details) override {
    if (!event_ || extension_id_ != details.extension_id ||
        event_->event_name != details.event_name) {
      return;
    }

    const bool is_lazy_listener = details.browser_context == nullptr;
    if (is_lazy_listener) {
      // Wait for the non-lazy listener as we want to exercise the code to
      // dispatch the event right after the Service Worker registration is
      // completing.
      return;
    }
    DispatchEvent(std::move(event_));
  }

  bool SendAndWait() { return listener_.WaitUntilSatisfied(); }

 private:
  void DispatchEvent(std::unique_ptr<Event> event) {
    EventRouter::Get(browser_context_)
        ->DispatchEventToExtension(extension_id_, std::move(event));
  }

  const raw_ptr<content::BrowserContext> browser_context_ = nullptr;
  const raw_ptr<EventRouter> event_router_ = nullptr;
  const ExtensionId extension_id_;
  std::unique_ptr<Event> event_;
  ExtensionTestMessageListener listener_;
};

// Tests that extension event dispatch works correctly right after extension
// installation registers its Service Worker.
// Regression test for: https://crbug.com/850792.
IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest, EarlyEventDispatch) {
  const ExtensionId kId("pkplfbidichfdicaijlchgnapepdginl");

  // Build "test.onMessage" event for dispatch.
  auto event = std::make_unique<Event>(
      events::FOR_TEST, extensions::api::test::OnMessage::kEventName,
      std::move(
          base::JSONReader::Read(R"([{"data": "hello", "lastMessage": true}])")
              .value()
              .GetList()),
      profile());

  EarlyWorkerMessageSender sender(profile(), kId, std::move(event));
  // pkplfbidichfdicaijlchgnapepdginl
  const Extension* extension = LoadExtension(test_data_dir_.AppendASCII(
      "service_worker/worker_based_background/early_event_dispatch"));
  CHECK(extension);
  EXPECT_EQ(kId, extension->id());
  EXPECT_TRUE(sender.SendAndWait());
}

// Tests that filtered events dispatches correctly right after a non-lazy
// listener is registered for that event (and before the corresponding lazy
// listener is registered).
IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest,
                       EarlyFilteredEventDispatch) {
  const ExtensionId kId("pkplfbidichfdicaijlchgnapepdginl");

  // Add minimal details required to dispatch webNavigation.onCommitted event:
  extensions::api::web_navigation::OnCommitted::Details details;
  details.transition_type =
      extensions::api::web_navigation::TRANSITION_TYPE_TYPED;
  details.frame_type = api::extension_types::FRAME_TYPE_OUTERMOST_FRAME;
  details.document_lifecycle = api::extension_types::DOCUMENT_LIFECYCLE_ACTIVE;

  // Build a dummy onCommited event to dispatch.
  auto on_committed_event = std::make_unique<Event>(
      events::WEB_NAVIGATION_ON_COMMITTED, "webNavigation.onCommitted",
      api::web_navigation::OnCommitted::Create(details), profile());
  // The filter will match the listener filter registered from the extension.
  mojom::EventFilteringInfoPtr info = mojom::EventFilteringInfo::New();
  info->url = GURL("http://foo.com/a.html");
  on_committed_event->filter_info = std::move(info);

  EarlyWorkerMessageSender sender(profile(), kId,
                                  std::move(on_committed_event));

  // pkplfbidichfdicaijlchgnapepdginl
  const Extension* extension = LoadExtension(test_data_dir_.AppendASCII(
      "service_worker/worker_based_background/early_filtered_event_dispatch"));
  ASSERT_TRUE(extension);
  EXPECT_EQ(kId, extension->id());
  EXPECT_TRUE(sender.SendAndWait());
}

class ServiceWorkerBackgroundSyncTest : public ServiceWorkerTest {
 public:
  ServiceWorkerBackgroundSyncTest() {}

  ServiceWorkerBackgroundSyncTest(const ServiceWorkerBackgroundSyncTest&) =
      delete;
  ServiceWorkerBackgroundSyncTest& operator=(
      const ServiceWorkerBackgroundSyncTest&) = delete;

  ~ServiceWorkerBackgroundSyncTest() override {}

  void SetUpCommandLine(base::CommandLine* command_line) override {
    // ServiceWorkerRegistration.sync requires experimental flag.
    command_line->AppendSwitch(
        ::switches::kEnableExperimentalWebPlatformFeatures);
    ServiceWorkerTest::SetUpCommandLine(command_line);
  }

  void SetUp() override {
    content::background_sync_test_util::SetIgnoreNetworkChanges(true);
    ServiceWorkerTest::SetUp();
  }
};

class ServiceWorkerPushMessagingTest : public ServiceWorkerTest {
 public:
  ServiceWorkerPushMessagingTest()
      : scoped_testing_factory_installer_(
            base::BindRepeating(&gcm::FakeGCMProfileService::Build)),
        gcm_driver_(nullptr),
        push_service_(nullptr) {
    feature_list_.InitAndDisableFeature(
        features::kPushMessagingDisallowSenderIDs);
  }

  ServiceWorkerPushMessagingTest(const ServiceWorkerPushMessagingTest&) =
      delete;
  ServiceWorkerPushMessagingTest& operator=(
      const ServiceWorkerPushMessagingTest&) = delete;

  ~ServiceWorkerPushMessagingTest() override {}

  void GrantNotificationPermissionForTest(const GURL& url) {
    NotificationPermissionContext::UpdatePermission(
        profile(), url.DeprecatedGetOriginAsURL(), CONTENT_SETTING_ALLOW);
  }

  PushMessagingAppIdentifier GetAppIdentifierForServiceWorkerRegistration(
      int64_t service_worker_registration_id,
      const GURL& origin) {
    PushMessagingAppIdentifier app_identifier =
        PushMessagingAppIdentifier::FindByServiceWorker(
            profile(), origin, service_worker_registration_id);

    EXPECT_FALSE(app_identifier.is_null());
    return app_identifier;
  }

  // ExtensionApiTest overrides.
  void SetUpCommandLine(base::CommandLine* command_line) override {
    command_line->AppendSwitch(
        ::switches::kEnableExperimentalWebPlatformFeatures);
    ServiceWorkerTest::SetUpCommandLine(command_line);
  }

  void SetUpOnMainThread() override {
    NotificationDisplayServiceFactory::GetInstance()->SetTestingFactory(
        profile(),
        base::BindRepeating(&StubNotificationDisplayService::FactoryForTests));

    gcm::FakeGCMProfileService* gcm_service =
        static_cast<gcm::FakeGCMProfileService*>(
            gcm::GCMProfileServiceFactory::GetForProfile(profile()));
    gcm_driver_ = static_cast<instance_id::FakeGCMDriverForInstanceID*>(
        gcm_service->driver());
    push_service_ = PushMessagingServiceFactory::GetForProfile(profile());

    ServiceWorkerTest::SetUpOnMainThread();
  }

  instance_id::FakeGCMDriverForInstanceID* gcm_driver() const {
    return gcm_driver_;
  }
  PushMessagingServiceImpl* push_service() const { return push_service_; }

 private:
  base::test::ScopedFeatureList feature_list_;
  gcm::GCMProfileServiceFactory::ScopedTestingFactoryInstaller
      scoped_testing_factory_installer_;

  raw_ptr<instance_id::FakeGCMDriverForInstanceID> gcm_driver_;
  raw_ptr<PushMessagingServiceImpl> push_service_;
};

class ServiceWorkerLazyBackgroundTest : public ServiceWorkerTest {
 public:
  ServiceWorkerLazyBackgroundTest() = default;

  ServiceWorkerLazyBackgroundTest(const ServiceWorkerLazyBackgroundTest&) =
      delete;
  ServiceWorkerLazyBackgroundTest& operator=(
      const ServiceWorkerLazyBackgroundTest&) = delete;

  ~ServiceWorkerLazyBackgroundTest() override {}

  void SetUpCommandLine(base::CommandLine* command_line) override {
    ServiceWorkerTest::SetUpCommandLine(command_line);
    // Disable background network activity as it can suddenly bring the Lazy
    // Background Page alive.
    command_line->AppendSwitch(::switches::kDisableBackgroundNetworking);
    command_line->AppendSwitch(::switches::kNoProxyServer);
  }

  void SetUpInProcessBrowserTestFixture() override {
    ServiceWorkerTest::SetUpInProcessBrowserTestFixture();
    // Set shorter delays to prevent test timeouts.
    ProcessManager::SetEventPageIdleTimeForTesting(1);
    ProcessManager::SetEventPageSuspendingTimeForTesting(1);
  }
};

IN_PROC_BROWSER_TEST_F(ServiceWorkerTest, RegisterSucceeds) {
  StartTestFromBackgroundPage("register.js");
}

IN_PROC_BROWSER_TEST_F(ServiceWorkerTest, UpdateRefreshesServiceWorker) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir scoped_temp_dir;
  ASSERT_TRUE(scoped_temp_dir.CreateUniqueTempDir());
  base::FilePath pem_path = test_data_dir_.AppendASCII("service_worker")
                                .AppendASCII("update")
                                .AppendASCII("service_worker.pem");
  base::FilePath path_v1 =
      PackExtensionWithOptions(test_data_dir_.AppendASCII("service_worker")
                                   .AppendASCII("update")
                                   .AppendASCII("v1"),
                               scoped_temp_dir.GetPath().AppendASCII("v1.crx"),
                               pem_path, base::FilePath());
  base::FilePath path_v2 =
      PackExtensionWithOptions(test_data_dir_.AppendASCII("service_worker")
                                   .AppendASCII("update")
                                   .AppendASCII("v2"),
                               scoped_temp_dir.GetPath().AppendASCII("v2.crx"),
                               pem_path, base::FilePath());
  const char* kId = "hfaanndiiilofhfokeanhddpkfffchdi";

  ExtensionTestMessageListener listener_v1("Pong from version 1");
  listener_v1.set_failure_message("FAILURE_V1");
  // Install version 1.0 of the extension.
  ASSERT_TRUE(InstallExtension(path_v1, 1));
  EXPECT_TRUE(extensions::ExtensionRegistry::Get(profile())
                  ->enabled_extensions()
                  .GetByID(kId));
  EXPECT_TRUE(listener_v1.WaitUntilSatisfied());

  ExtensionTestMessageListener listener_v2("Pong from version 2");
  listener_v2.set_failure_message("FAILURE_V2");

  // Update to version 2.0.
  EXPECT_TRUE(UpdateExtension(kId, path_v2, 0));
  EXPECT_TRUE(extensions::ExtensionRegistry::Get(profile())
                  ->enabled_extensions()
                  .GetByID(kId));
  EXPECT_TRUE(listener_v2.WaitUntilSatisfied());
}

IN_PROC_BROWSER_TEST_F(ServiceWorkerTest, UpdateWithoutSkipWaiting) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir scoped_temp_dir;
  ASSERT_TRUE(scoped_temp_dir.CreateUniqueTempDir());
  base::FilePath pem_path = test_data_dir_.AppendASCII("service_worker")
                                .AppendASCII("update_without_skip_waiting")
                                .AppendASCII("update_without_skip_waiting.pem");
  base::FilePath path_v1 =
      PackExtensionWithOptions(test_data_dir_.AppendASCII("service_worker")
                                   .AppendASCII("update_without_skip_waiting")
                                   .AppendASCII("v1"),
                               scoped_temp_dir.GetPath().AppendASCII("v1.crx"),
                               pem_path, base::FilePath());
  base::FilePath path_v2 =
      PackExtensionWithOptions(test_data_dir_.AppendASCII("service_worker")
                                   .AppendASCII("update_without_skip_waiting")
                                   .AppendASCII("v2"),
                               scoped_temp_dir.GetPath().AppendASCII("v2.crx"),
                               pem_path, base::FilePath());
  const char* kId = "mhnnnflgagdakldgjpfcofkiocpdmogl";

  // Install version 1.0 of the extension.
  ASSERT_TRUE(InstallExtension(path_v1, 1));
  EXPECT_TRUE(extensions::ExtensionRegistry::Get(profile())
                  ->enabled_extensions()
                  .GetByID(kId));
  const Extension* extension = extensions::ExtensionRegistry::Get(profile())
                                   ->enabled_extensions()
                                   .GetByID(kId);

  ExtensionTestMessageListener listener1("Pong from version 1");
  listener1.set_failure_message("FAILURE");
  content::WebContents* web_contents = browsertest_util::AddTab(
      browser(), extension->GetResourceURL("page.html"));
  EXPECT_TRUE(listener1.WaitUntilSatisfied());

  // Update to version 2.0.
  EXPECT_TRUE(UpdateExtension(kId, path_v2, 0));
  EXPECT_TRUE(extensions::ExtensionRegistry::Get(profile())
                  ->enabled_extensions()
                  .GetByID(kId));
  const Extension* extension_after_update =
      extensions::ExtensionRegistry::Get(profile())
          ->enabled_extensions()
          .GetByID(kId);

  // Service worker version 2 would be installed but it won't be controlling
  // the extension page yet.
  ExtensionTestMessageListener listener2("Pong from version 1");
  listener2.set_failure_message("FAILURE");
  web_contents = browsertest_util::AddTab(
      browser(), extension_after_update->GetResourceURL("page.html"));
  EXPECT_TRUE(listener2.WaitUntilSatisfied());

  // Navigate the tab away from the extension page so that no clients are
  // using the service worker.
  // Note that just closing the tab with WebContentsDestroyedWatcher doesn't
  // seem to be enough because it returns too early.
  WebContentsLoadStopObserver navigate_away_observer(web_contents);
  web_contents->GetController().LoadURL(
      GURL(url::kAboutBlankURL), content::Referrer(), ui::PAGE_TRANSITION_TYPED,
      std::string());
  navigate_away_observer.WaitForLoadStop();

  // Now expect service worker version 2 to control the extension page.
  ExtensionTestMessageListener listener3("Pong from version 2");
  listener3.set_failure_message("FAILURE");
  web_contents = browsertest_util::AddTab(
      browser(), extension_after_update->GetResourceURL("page.html"));
  EXPECT_TRUE(listener3.WaitUntilSatisfied());
}

IN_PROC_BROWSER_TEST_F(ServiceWorkerTest, FetchArbitraryPaths) {
  const Extension* extension = StartTestFromBackgroundPage("fetch.js");

  // Open some arbirary paths. Their contents should be what the service worker
  // responds with, which in this case is the path of the fetch.
  EXPECT_EQ(
      "Caught a fetch for /index.html",
      NavigateAndExtractInnerText(extension->GetResourceURL("index.html")));
  EXPECT_EQ("Caught a fetch for /path/to/other.html",
            NavigateAndExtractInnerText(
                extension->GetResourceURL("path/to/other.html")));
  EXPECT_EQ("Caught a fetch for /some/text/file.txt",
            NavigateAndExtractInnerText(
                extension->GetResourceURL("some/text/file.txt")));
  EXPECT_EQ("Caught a fetch for /no/file/extension",
            NavigateAndExtractInnerText(
                extension->GetResourceURL("no/file/extension")));
  EXPECT_EQ("Caught a fetch for /",
            NavigateAndExtractInnerText(extension->GetResourceURL("")));
}

IN_PROC_BROWSER_TEST_F(ServiceWorkerTest,
                       FetchExtensionResourceFromServiceWorker) {
  const Extension* extension = StartTestFromBackgroundPage("fetch_from_sw.js");
  ASSERT_TRUE(extension);

  // The service worker in this test tries to load 'hello.txt' via fetch()
  // and sends back the content of the file, which should be 'hello'.
  const char* kScript = R"(
    let channel = new MessageChannel();
    test.waitForMessage(channel.port1).then(message => {
      window.domAutomationController.send(message);
    });
    test.registeredServiceWorker.postMessage(
        {port: channel.port2}, [channel.port2]);
  )";
  EXPECT_EQ("hello", ExecuteScriptInBackgroundPage(extension->id(), kScript));
}

// Tests that fetch() from service worker and network fallback
// go through webRequest.onBeforeRequest API.
IN_PROC_BROWSER_TEST_F(ServiceWorkerTest, OnBeforeRequest) {
  const Extension* extension =
      LoadExtension(test_data_dir_.AppendASCII("service_worker/webrequest"));
  ASSERT_TRUE(extension);
  ASSERT_TRUE(StartEmbeddedTestServer());

  // Start a service worker and make it control the page.
  GURL page_url = embedded_test_server()->GetURL(
      "/extensions/api_test/service_worker/"
      "webrequest/webpage.html");
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page_url));
  EXPECT_TRUE(content::WaitForLoadStop(web_contents));

  std::string result;
  ASSERT_TRUE(content::ExecuteScriptAndExtractString(web_contents,
                                                     "register();", &result));
  EXPECT_EQ("ready", result);

  // Initiate a fetch that the service worker doesn't intercept
  // (network fallback).
  result.clear();
  ASSERT_TRUE(content::ExecuteScriptAndExtractString(
      web_contents, "doFetch('hello.txt?fallthrough');", &result));
  EXPECT_EQ("hello", result);
  EXPECT_EQ(
      "/extensions/api_test/service_worker/webrequest/hello.txt?fallthrough",
      ExecuteScriptInBackgroundPage(extension->id(), "getLastHookedPath()"));

  // Initiate a fetch that results in calling fetch() in the service worker.
  result.clear();
  ASSERT_TRUE(content::ExecuteScriptAndExtractString(
      web_contents, "doFetch('hello.txt?respondWithFetch');", &result));
  EXPECT_EQ("hello", result);
  EXPECT_EQ(
      "/extensions/api_test/service_worker/webrequest/"
      "hello.txt?respondWithFetch",
      ExecuteScriptInBackgroundPage(extension->id(), "getLastHookedPath()"));
}

IN_PROC_BROWSER_TEST_F(ServiceWorkerTest, SWServedBackgroundPageReceivesEvent) {
  const Extension* extension =
      StartTestFromBackgroundPage("replace_background.js");
  ExtensionHost* background_page =
      process_manager()->GetBackgroundHostForExtension(extension->id());
  ASSERT_TRUE(background_page);

  // Close the background page and start it again so that the service worker
  // will start controlling pages.
  background_page->Close();
  ExtensionBackgroundPageWaiter(profile(), *extension)
      .WaitForBackgroundClosed();
  background_page = nullptr;
  process_manager()->WakeEventPage(extension->id(), base::DoNothing());
  ExtensionBackgroundPageWaiter(profile(), *extension).WaitForBackgroundOpen();

  // Since the SW is now controlling the extension, the SW serves the background
  // script. page.html sends a message to the background script and we verify
  // that the SW served background script correctly receives the message/event.
  ExtensionTestMessageListener listener("onMessage/SW BG.");
  listener.set_failure_message("onMessage/original BG.");
  content::WebContents* web_contents = browsertest_util::AddTab(
      browser(), extension->GetResourceURL("page.html"));
  ASSERT_TRUE(web_contents);
  EXPECT_TRUE(listener.WaitUntilSatisfied());
}

IN_PROC_BROWSER_TEST_F(ServiceWorkerTest, SWServedBackgroundPage) {
  const Extension* extension = StartTestFromBackgroundPage("fetch.js");

  std::string kExpectedInnerText = "background.html contents for testing.";

  // Sanity check that the background page has the expected content.
  ExtensionHost* background_page =
      process_manager()->GetBackgroundHostForExtension(extension->id());
  ASSERT_TRUE(background_page);
  EXPECT_EQ(kExpectedInnerText,
            ExtractInnerText(background_page->host_contents()));

  // Close the background page.
  background_page->Close();
  ExtensionBackgroundPageWaiter(profile(), *extension)
      .WaitForBackgroundClosed();
  background_page = nullptr;

  // Start it again.
  process_manager()->WakeEventPage(extension->id(), base::DoNothing());
  ExtensionBackgroundPageWaiter(profile(), *extension).WaitForBackgroundOpen();

  // The service worker should get a fetch event for the background page.
  background_page =
      process_manager()->GetBackgroundHostForExtension(extension->id());
  ASSERT_TRUE(background_page);
  EXPECT_TRUE(content::WaitForLoadStop(background_page->host_contents()));

  EXPECT_EQ("Caught a fetch for /background.html",
            ExtractInnerText(background_page->host_contents()));
}

IN_PROC_BROWSER_TEST_F(ServiceWorkerTest,
                       ServiceWorkerPostsMessageToBackgroundClient) {
  const Extension* extension =
      StartTestFromBackgroundPage("post_message_to_background_client.js");

  // The service worker in this test simply posts a message to the background
  // client it receives from getBackgroundClient().
  const char* kScript =
      "var messagePromise = null;\n"
      "if (test.lastMessageFromServiceWorker) {\n"
      "  messagePromise = Promise.resolve(test.lastMessageFromServiceWorker);\n"
      "} else {\n"
      "  messagePromise = test.waitForMessage(navigator.serviceWorker);\n"
      "}\n"
      "messagePromise.then(function(message) {\n"
      "  window.domAutomationController.send(String(message == 'success'));\n"
      "})\n";
  EXPECT_EQ("true", ExecuteScriptInBackgroundPage(extension->id(), kScript));
}

IN_PROC_BROWSER_TEST_F(ServiceWorkerTest,
                       BackgroundPagePostsMessageToServiceWorker) {
  const Extension* extension =
      StartTestFromBackgroundPage("post_message_to_sw.js");

  // The service worker in this test waits for a message, then echoes it back
  // by posting a message to the background page via getBackgroundClient().
  const char* kScript =
      "var mc = new MessageChannel();\n"
      "test.waitForMessage(mc.port1).then(function(message) {\n"
      "  window.domAutomationController.send(String(message == 'hello'));\n"
      "});\n"
      "test.registeredServiceWorker.postMessage(\n"
      "    {message: 'hello', port: mc.port2}, [mc.port2])\n";
  EXPECT_EQ("true", ExecuteScriptInBackgroundPage(extension->id(), kScript));
}

IN_PROC_BROWSER_TEST_F(ServiceWorkerTest,
                       ServiceWorkerSuspensionOnExtensionUnload) {
  // For this test, only hold onto the extension's ID and URL + a function to
  // get a resource URL, because we're going to be disabling and uninstalling
  // it, which will invalidate the pointer.
  std::string extension_id;
  GURL extension_url;
  {
    const Extension* extension = StartTestFromBackgroundPage("fetch.js");
    extension_id = extension->id();
    extension_url = extension->url();
  }
  auto get_resource_url = [&extension_url](const std::string& path) {
    return Extension::GetResourceURL(extension_url, path);
  };

  // Fetch should route to the service worker.
  EXPECT_EQ("Caught a fetch for /index.html",
            NavigateAndExtractInnerText(get_resource_url("index.html")));

  // Disable the extension. Opening the page should fail.
  extension_service()->DisableExtension(extension_id,
                                        disable_reason::DISABLE_USER_ACTION);
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(content::PAGE_TYPE_ERROR,
            NavigateAndGetPageType(get_resource_url("index.html")));
  EXPECT_EQ(content::PAGE_TYPE_ERROR,
            NavigateAndGetPageType(get_resource_url("other.html")));

  // Re-enable the extension. Opening pages should immediately start to succeed
  // again.
  extension_service()->EnableExtension(extension_id);
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ("Caught a fetch for /index.html",
            NavigateAndExtractInnerText(get_resource_url("index.html")));
  EXPECT_EQ("Caught a fetch for /other.html",
            NavigateAndExtractInnerText(get_resource_url("other.html")));
  EXPECT_EQ("Caught a fetch for /another.html",
            NavigateAndExtractInnerText(get_resource_url("another.html")));

  // Uninstall the extension. Opening pages should fail again.
  std::u16string error;
  extension_service()->UninstallExtension(
      extension_id, UninstallReason::UNINSTALL_REASON_FOR_TESTING, &error);
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(content::PAGE_TYPE_ERROR,
            NavigateAndGetPageType(get_resource_url("index.html")));
  EXPECT_EQ(content::PAGE_TYPE_ERROR,
            NavigateAndGetPageType(get_resource_url("other.html")));
  EXPECT_EQ(content::PAGE_TYPE_ERROR,
            NavigateAndGetPageType(get_resource_url("anotherother.html")));
  EXPECT_EQ(content::PAGE_TYPE_ERROR,
            NavigateAndGetPageType(get_resource_url("final.html")));
}

IN_PROC_BROWSER_TEST_F(ServiceWorkerTest, BackgroundPageIsWokenIfAsleep) {
  const Extension* extension = StartTestFromBackgroundPage("wake_on_fetch.js");

  // Navigate to special URLs that this test's service worker recognises, each
  // making a check then populating the response with either "true" or "false".
  EXPECT_EQ("true", NavigateAndExtractInnerText(extension->GetResourceURL(
                        "background-client-is-awake")));
  EXPECT_EQ("true", NavigateAndExtractInnerText(
                        extension->GetResourceURL("ping-background-client")));
  // Ping more than once for good measure.
  EXPECT_EQ("true", NavigateAndExtractInnerText(
                        extension->GetResourceURL("ping-background-client")));

  // Shut down the event page. The SW should detect that it's closed, but still
  // be able to ping it.
  ExtensionHost* background_page =
      process_manager()->GetBackgroundHostForExtension(extension->id());
  ASSERT_TRUE(background_page);
  background_page->Close();
  ExtensionBackgroundPageWaiter(profile(), *extension)
      .WaitForBackgroundClosed();

  EXPECT_EQ("false", NavigateAndExtractInnerText(extension->GetResourceURL(
                         "background-client-is-awake")));
  EXPECT_EQ("true", NavigateAndExtractInnerText(
                        extension->GetResourceURL("ping-background-client")));
  EXPECT_EQ("true", NavigateAndExtractInnerText(
                        extension->GetResourceURL("ping-background-client")));
  EXPECT_EQ("true", NavigateAndExtractInnerText(extension->GetResourceURL(
                        "background-client-is-awake")));
}

IN_PROC_BROWSER_TEST_F(ServiceWorkerTest,
                       GetBackgroundClientFailsWithNoBackgroundPage) {
  // This extension doesn't have a background page, only a tab at page.html.
  // The service worker it registers tries to call getBackgroundClient() and
  // should fail.
  // Note that this also tests that service workers can be registered from tabs.
  EXPECT_TRUE(RunExtensionTest("service_worker/no_background",
                               {.page_url = "page.html"}));
}

IN_PROC_BROWSER_TEST_F(ServiceWorkerTest, NotificationAPI) {
  EXPECT_TRUE(RunExtensionTest("service_worker/notifications/has_permission",
                               {.page_url = "page.html"}));
}

IN_PROC_BROWSER_TEST_F(ServiceWorkerTest, WebAccessibleResourcesFetch) {
  EXPECT_TRUE(RunExtensionTest("service_worker/web_accessible_resources/fetch/",
                               {.page_url = "page.html"}));
}

// Tests that updating a packed extension with modified scripts works
// properly -- we expect that the new script will execute, rather than the
// previous one.
IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest,
                       UpdatePackedExtension) {
  constexpr char kManifest1[] =
      R"({
           "name": "Test Extension",
           "manifest_version": 2,
           "version": "0.1",
           "background": {"service_worker": "script.js"}
         })";
  constexpr char kNewVersionString[] = "0.2";

  // This script installs an event listener for updates to the extension with
  // a callback that forces itself to reload.
  constexpr char kScript1[] =
      R"(
         chrome.runtime.onUpdateAvailable.addListener(function(details) {
           chrome.test.assertEq('%s', details.version);
           chrome.runtime.reload();
         });
         chrome.test.sendMessage('ready1');
        )";

  std::string id;
  TestExtensionDir test_dir;

  // Write the manifest and script files and load the extension.
  test_dir.WriteManifest(kManifest1);
  test_dir.WriteFile(FILE_PATH_LITERAL("script.js"),
                     base::StringPrintf(kScript1, kNewVersionString));

  {
    ExtensionTestMessageListener ready_listener("ready1");
    base::FilePath path = test_dir.Pack();
    const Extension* extension = LoadExtension(path);
    ASSERT_TRUE(extension);

    EXPECT_TRUE(ready_listener.WaitUntilSatisfied());
    id = extension->id();
  }

  constexpr char kManifest2[] =
      R"({
           "name": "Test Extension",
           "manifest_version": 2,
           "version": "%s",
           "background": {"service_worker": "script.js"}
         })";
  constexpr char kScript2[] =
      R"(
         chrome.runtime.onInstalled.addListener(function(details) {
           chrome.test.assertEq('update', details.reason);
           chrome.test.sendMessage('onInstalled');
         });
         chrome.test.sendMessage('ready2');
        )";
  // Rewrite the manifest and script files with a version change in the manifest
  // file. After reloading the extension, the old version of the extension
  // should detect the update, force the reload, and the new script should
  // execute.
  test_dir.WriteManifest(base::StringPrintf(kManifest2, kNewVersionString));
  test_dir.WriteFile(FILE_PATH_LITERAL("script.js"), kScript2);
  {
    ExtensionTestMessageListener ready_listener("ready2");
    ExtensionTestMessageListener on_installed_listener("onInstalled");
    base::FilePath path = test_dir.Pack();
    ExtensionService* const extension_service =
        ExtensionSystem::Get(profile())->extension_service();
    CRXFileInfo crx_info(path, GetTestVerifierFormat());
    crx_info.extension_id = id;
    EXPECT_TRUE(extension_service->UpdateExtension(crx_info, true, nullptr));
    EXPECT_TRUE(ready_listener.WaitUntilSatisfied());
    EXPECT_EQ("0.2", ExtensionRegistry::Get(profile())
                         ->enabled_extensions()
                         .GetByID(id)
                         ->version()
                         .GetString());
    EXPECT_TRUE(on_installed_listener.WaitUntilSatisfied());
  }
}

// Tests that updating an unpacked extension with modified scripts works
// properly -- we expect that the new script will execute, rather than the
// previous one.
IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest,
                       UpdateUnpackedExtension) {
  constexpr char kManifest1[] =
      R"({
           "name": "Test Extension",
           "manifest_version": 2,
           "version": "0.1",
           "background": {"service_worker": "script.js"}
         })";
  constexpr char kManifest2[] =
      R"({
           "name": "Test Extension",
           "manifest_version": 2,
           "version": "0.2",
           "background": {"service_worker": "script.js"}
         })";
  constexpr char kScript[] =
      R"(
         chrome.runtime.onInstalled.addListener(function(details) {
           chrome.test.assertEq('%s', details.reason);
           chrome.test.sendMessage('%s');
           chrome.test.sendMessage('onInstalled');
         });
        )";

  std::string id;

  ExtensionService* const extension_service =
      ExtensionSystem::Get(profile())->extension_service();
  scoped_refptr<UnpackedInstaller> installer =
      UnpackedInstaller::Create(extension_service);

  // Set a completion callback so we can get the ID of the extension.
  installer->set_completion_callback(base::BindLambdaForTesting(
      [&id](const Extension* extension, const base::FilePath& path,
            const std::string& error) {
        ASSERT_TRUE(extension);
        ASSERT_TRUE(error.empty());
        id = extension->id();
      }));

  TestExtensionDir test_dir;

  // Write the manifest and script files and load the extension.
  test_dir.WriteManifest(kManifest1);
  test_dir.WriteFile(FILE_PATH_LITERAL("script.js"),
                     base::StringPrintf(kScript, "install", "ready1"));
  {
    ExtensionTestMessageListener ready_listener("ready1");
    ExtensionTestMessageListener on_installed_listener("onInstalled");

    installer->Load(test_dir.UnpackedPath());
    EXPECT_TRUE(ready_listener.WaitUntilSatisfied());
    EXPECT_TRUE(on_installed_listener.WaitUntilSatisfied());
    ASSERT_FALSE(id.empty());
  }

  // Rewrite the script file without a version change in the manifest and reload
  // the extension. The new script should execute.
  test_dir.WriteFile(FILE_PATH_LITERAL("script.js"),
                     base::StringPrintf(kScript, "update", "ready2"));
  {
    ExtensionTestMessageListener ready_listener("ready2");
    ExtensionTestMessageListener on_installed_listener("onInstalled");

    extension_service->ReloadExtension(id);
    EXPECT_TRUE(ready_listener.WaitUntilSatisfied());
    EXPECT_TRUE(on_installed_listener.WaitUntilSatisfied());
  }

  // Rewrite the manifest and script files with a version change in the manifest
  // file. After reloading the extension, the new script should execute.
  test_dir.WriteManifest(kManifest2);
  test_dir.WriteFile(FILE_PATH_LITERAL("script.js"),
                     base::StringPrintf(kScript, "update", "ready3"));
  {
    ExtensionTestMessageListener ready_listener("ready3");
    ExtensionTestMessageListener on_installed_listener("onInstalled");

    extension_service->ReloadExtension(id);
    EXPECT_TRUE(ready_listener.WaitUntilSatisfied());
    EXPECT_TRUE(on_installed_listener.WaitUntilSatisfied());
  }
}

// This test loads a web page that has an iframe pointing to a
// chrome-extension:// URL. The URL is listed in the extension's
// web_accessible_resources. Initially the iframe is served from the extension's
// resource file. After verifying that, we register a Service Worker that
// controls the extension. Further requests to the same resource as before
// should now be served by the Service Worker.
// This test also verifies that if the requested resource exists in the manifest
// but is not present in the extension directory, the Service Worker can still
// serve the resource file.
IN_PROC_BROWSER_TEST_F(ServiceWorkerTest, WebAccessibleResourcesIframeSrc) {
  const Extension* extension = LoadExtension(test_data_dir_.AppendASCII(
      "service_worker/web_accessible_resources/iframe_src"));
  ASSERT_TRUE(extension);
  ASSERT_TRUE(StartEmbeddedTestServer());

  // Service workers can only control secure contexts
  // (https://w3c.github.io/webappsec-secure-contexts/). For documents, this
  // typically means the document must have a secure origin AND all its ancestor
  // frames must have documents with secure origins.  However, extension pages
  // are considered secure, even if they have an ancestor document that is an
  // insecure context (see GetSchemesBypassingSecureContextCheckAllowlist). So
  // extension service workers must be able to control an extension page
  // embedded in an insecure context. To test this, set up an insecure
  // (non-localhost, non-https) URL for the web page. This page will create
  // iframes that load extension pages that must be controllable by service
  // worker.
  GURL page_url =
      embedded_test_server()->GetURL("a.com",
                                     "/extensions/api_test/service_worker/"
                                     "web_accessible_resources/webpage.html");
  EXPECT_FALSE(network::IsUrlPotentiallyTrustworthy(page_url));

  content::WebContents* web_contents =
      browsertest_util::AddTab(browser(), page_url);
  std::string result;
  // webpage.html will create an iframe pointing to a resource from |extension|.
  // Expect the resource to be served by the extension.
  EXPECT_TRUE(content::ExecuteScriptAndExtractString(
      web_contents,
      base::StringPrintf("window.testIframe('%s', 'iframe.html')",
                         extension->id().c_str()),
      &result));
  EXPECT_EQ("FROM_EXTENSION_RESOURCE", result);

  ExtensionTestMessageListener service_worker_ready_listener("SW_READY");
  EXPECT_TRUE(ExecuteScriptInBackgroundPageNoWait(
      extension->id(), "window.registerServiceWorker()"));
  EXPECT_TRUE(service_worker_ready_listener.WaitUntilSatisfied());

  result.clear();
  // webpage.html will create another iframe pointing to a resource from
  // |extension| as before. But this time, the resource should be be served
  // from the Service Worker.
  EXPECT_TRUE(content::ExecuteScriptAndExtractString(
      web_contents,
      base::StringPrintf("window.testIframe('%s', 'iframe.html')",
                         extension->id().c_str()),
      &result));
  EXPECT_EQ("FROM_SW_RESOURCE", result);

  result.clear();
  // webpage.html will create yet another iframe pointing to a resource that
  // exists in the extension manifest's web_accessible_resources, but is not
  // present in the extension directory. Expect the resources of the iframe to
  // be served by the Service Worker.
  EXPECT_TRUE(content::ExecuteScriptAndExtractString(
      web_contents,
      base::StringPrintf("window.testIframe('%s', 'iframe_non_existent.html')",
                         extension->id().c_str()),
      &result));
  EXPECT_EQ("FROM_SW_RESOURCE", result);
}

// Verifies that service workers that aren't specified as the background script
// for the extension do not have extension API bindings.
IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest, VerifyNoApiBindings) {
  const Extension* extension = LoadExtension(
      test_data_dir_.AppendASCII("service_worker/verify_no_api_bindings"));
  ASSERT_TRUE(extension);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), extension->GetResourceURL("page.html")));
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();

  // Have the page script start the service worker and wait for that to
  // succeed.
  ExtensionTestMessageListener worker_start_listener("WORKER STARTED");
  worker_start_listener.set_failure_message("FAILURE");
  ASSERT_TRUE(
      content::ExecuteScript(web_contents, "window.runServiceWorker()"));
  ASSERT_TRUE(worker_start_listener.WaitUntilSatisfied());

  // Kick off the test, which will check the available bindings and fail if
  // there is anything unexpected.
  ExtensionTestMessageListener worker_listener("SUCCESS");
  worker_listener.set_failure_message("FAILURE");
  ASSERT_TRUE(content::ExecuteScript(web_contents, "window.testSendMessage()"));
  EXPECT_TRUE(worker_listener.WaitUntilSatisfied());
}

IN_PROC_BROWSER_TEST_F(ServiceWorkerBackgroundSyncTest, Sync) {
  const Extension* extension =
      LoadExtension(test_data_dir_.AppendASCII("service_worker/sync"));
  ASSERT_TRUE(extension);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), extension->GetResourceURL("page.html")));
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();

  // Prevent firing by going offline.
  content::background_sync_test_util::SetOnline(web_contents, false);

  ExtensionTestMessageListener sync_listener("SYNC: send-chats");
  sync_listener.set_failure_message("FAIL");

  std::string result;
  ASSERT_TRUE(content::ExecuteScriptAndExtractString(
      web_contents, "window.runServiceWorker()", &result));
  ASSERT_EQ("SERVICE_WORKER_READY", result);

  EXPECT_FALSE(sync_listener.was_satisfied());
  // Resume firing by going online.
  content::background_sync_test_util::SetOnline(web_contents, true);
  EXPECT_TRUE(sync_listener.WaitUntilSatisfied());
}

IN_PROC_BROWSER_TEST_F(ServiceWorkerTest,
                       FetchFromContentScriptShouldNotGoToServiceWorkerOfPage) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  GURL page_url = embedded_test_server()->GetURL(
      "/extensions/api_test/service_worker/content_script_fetch/"
      "controlled_page/index.html");
  content::WebContents* tab =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page_url));
  EXPECT_TRUE(content::WaitForLoadStop(tab));

  std::string value;
  ASSERT_TRUE(
      content::ExecuteScriptAndExtractString(tab, "register();", &value));
  EXPECT_EQ("SW controlled", value);

  ASSERT_TRUE(RunExtensionTest("service_worker/content_script_fetch"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(ServiceWorkerPushMessagingTest, OnPush) {
  const Extension* extension = LoadExtension(
      test_data_dir_.AppendASCII("service_worker/push_messaging"));
  ASSERT_TRUE(extension);
  GURL extension_url = extension->url();

  GrantNotificationPermissionForTest(extension_url);

  GURL url = extension->GetResourceURL("page.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));

  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();

  // Start the ServiceWorker.
  ExtensionTestMessageListener ready_listener("SERVICE_WORKER_READY");
  ready_listener.set_failure_message("SERVICE_WORKER_FAILURE");
  const char* kScript = "window.runServiceWorker()";
  EXPECT_TRUE(
      content::ExecuteScript(web_contents->GetPrimaryMainFrame(), kScript));
  EXPECT_TRUE(ready_listener.WaitUntilSatisfied());

  PushMessagingAppIdentifier app_identifier =
      GetAppIdentifierForServiceWorkerRegistration(0LL, extension_url);
  ASSERT_EQ(app_identifier.app_id(), gcm_driver()->last_gettoken_app_id());
  EXPECT_EQ("1234567890", gcm_driver()->last_gettoken_authorized_entity());

  base::RunLoop run_loop;
  // Send a push message via gcm and expect the ServiceWorker to receive it.
  ExtensionTestMessageListener push_message_listener("OK");
  push_message_listener.set_failure_message("FAIL");
  gcm::IncomingMessage message;
  message.sender_id = "1234567890";
  message.raw_data = "testdata";
  message.decrypted = true;
  push_service()->SetMessageCallbackForTesting(run_loop.QuitClosure());
  push_service()->OnMessage(app_identifier.app_id(), message);
  EXPECT_TRUE(push_message_listener.WaitUntilSatisfied());
  run_loop.Run();  // Wait until the message is handled by push service.
}
IN_PROC_BROWSER_TEST_F(ServiceWorkerTest, MimeHandlerView) {
  ASSERT_TRUE(RunExtensionTest("service_worker/mime_handler_view"));
}

// Observer for an extension service worker to start and stop.
class TestWorkerObserver : public content::ServiceWorkerContextObserver {
 public:
  TestWorkerObserver(content::ServiceWorkerContext* context,
                     const ExtensionId& extension_id)
      : context_(context),
        extension_url_(Extension::GetBaseURLFromExtensionId(extension_id)) {
    context_->AddObserver(this);
  }
  ~TestWorkerObserver() override {
    if (context_) {
      context_->RemoveObserver(this);
    }
  }

  TestWorkerObserver(const TestWorkerObserver&) = delete;
  TestWorkerObserver& operator=(const TestWorkerObserver&) = delete;

  void WaitForWorkerStart() {
    if (running_version_id_.has_value())
      return;
    started_run_loop_.Run();
  }

  void WaitForWorkerStop() {
    DCHECK(running_version_id_.has_value()) << "Worker hasn't started";
    stopped_run_loop_.Run();
  }

 private:
  // ServiceWorkerContextObserver:
  void OnVersionStartedRunning(
      int64_t version_id,
      const content::ServiceWorkerRunningInfo& running_info) override {
    if (running_info.scope != extension_url_)
      return;

    running_version_id_ = version_id;
    started_run_loop_.Quit();
  }
  void OnVersionStoppedRunning(int64_t version_id) override {
    if (running_version_id_ == version_id)
      stopped_run_loop_.Quit();
  }
  void OnDestruct(content::ServiceWorkerContext* context) override {
    context_->RemoveObserver(this);
    context_ = nullptr;
  }

  base::RunLoop started_run_loop_;
  base::RunLoop stopped_run_loop_;
  // Holds version id of an extension worker once OnVersionStartedRunning is
  // observed.
  absl::optional<int64_t> running_version_id_;
  raw_ptr<content::ServiceWorkerContext> context_ = nullptr;
  GURL extension_url_;
};

IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest,
                       EventsToStoppedWorker) {
  ExtensionTestMessageListener event_listener_added("ready");
  event_listener_added.set_failure_message("ERROR");

  // Note: Extension is packed to avoid reloading while loading.
  const Extension* extension = LoadExtension(
      PackExtension(test_data_dir_.AppendASCII(
          "service_worker/worker_based_background/events_to_stopped_worker")),
      // Wait for the registration to be stored so that it's persistent
      // before the test terminates.
      {.wait_for_registration_stored = true});
  ASSERT_TRUE(extension);
  EXPECT_TRUE(event_listener_added.WaitUntilSatisfied());

  // Stop the service worker.
  {
    base::RunLoop run_loop;
    content::ServiceWorkerContext* context =
        GetServiceWorkerContext(browser()->profile());
    // The service worker is registered at the root scope.
    content::StopServiceWorkerForScope(context, extension->url(),
                                       run_loop.QuitClosure());
    run_loop.Run();
  }

  // Navigate to a URL, which should wake up the service worker.
  ExtensionTestMessageListener finished_listener("finished");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), extension->GetResourceURL("page.html")));
  EXPECT_TRUE(finished_listener.WaitUntilSatisfied());
}

namespace {

constexpr char kIncognitoManifest[] =
    R"({
          "name": "Incognito Test Extension",
          "version": "0.1",
          "manifest_version": 2,
          "permissions": ["tabs"],
          "background": {"service_worker": "worker.js"},
          "incognito": "%s"
        })";

constexpr char kQueryWorkerScript[] =
    R"(var inIncognitoContext = chrome.extension.inIncognitoContext;
       var incognitoStr =
           inIncognitoContext ? 'incognito' : 'regular';
       chrome.test.sendMessage('Script started ' + incognitoStr, function() {
         chrome.tabs.query({}, function(tabs) {
           let urls = tabs.map(tab => tab.url);
           chrome.test.sendMessage(JSON.stringify(urls));
         });
       });)";

constexpr char kTabsOnUpdatedSplitScript[] =
    R"(var inIncognitoContext = chrome.extension.inIncognitoContext;
       var incognitoStr =
           inIncognitoContext ? 'incognito' : 'regular';
       var urls = [];

       chrome.tabs.onUpdated.addListener(function localListener(tabId,
                                                                changeInfo,
                                                                tab) {
         if (changeInfo.status === 'loading') {
           chrome.tabs.onUpdated.removeListener(localListener);
           urls.push(changeInfo.url);
         }
       });

       chrome.test.sendMessage('Script started ' + incognitoStr, function() {
           chrome.test.sendMessage(JSON.stringify(urls));
       });)";

constexpr char kTabsOnUpdatedSpanningScript[] =
    R"(var inIncognitoContext = chrome.extension.inIncognitoContext;
       var incognitoStr =
           inIncognitoContext ? 'incognito' : 'regular';
       var urls = [];
       var expectedCount = 0;

       chrome.tabs.onUpdated.addListener(function(tabId, changeInfo, tab) {
         if (changeInfo.status === 'complete') {
           urls.push(tab.url);
           if (urls.length == expectedCount) {
             chrome.test.sendMessage(JSON.stringify(urls));
           }
         }
       });

       chrome.test.sendMessage('Script started ' + incognitoStr,
                               function(expected) {
           expectedCount = expected;
       });)";

}  // anonymous namespace

IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest, TabsQuerySplit) {
  ExtensionTestMessageListener ready_regular("Script started regular",
                                             ReplyBehavior::kWillReply);
  ExtensionTestMessageListener ready_incognito("Script started incognito",
                                               ReplyBehavior::kWillReply);
  // Open an incognito window.
  Browser* browser_incognito =
      OpenURLOffTheRecord(browser()->profile(), GURL("about:blank"));
  ASSERT_TRUE(browser_incognito);

  TestExtensionDir test_dir;
  test_dir.WriteManifest(base::StringPrintf(kIncognitoManifest, "split"));
  test_dir.WriteFile(FILE_PATH_LITERAL("worker.js"), kQueryWorkerScript);

  const Extension* extension =
      LoadExtension(test_dir.UnpackedPath(), {.allow_in_incognito = true});
  ASSERT_TRUE(extension);

  // Wait for the extension's service workers to be ready.
  ASSERT_TRUE(ready_regular.WaitUntilSatisfied());
  ASSERT_TRUE(ready_incognito.WaitUntilSatisfied());

  // Load a new tab in both browsers.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("chrome:version")));
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser_incognito, GURL("chrome:about")));

  {
    ExtensionTestMessageListener tabs_listener;
    // The extension waits for the reply to the "ready" sendMessage call
    // and replies with the URLs of the tabs.
    ready_regular.Reply("");
    EXPECT_TRUE(tabs_listener.WaitUntilSatisfied());
    EXPECT_EQ(R"(["chrome://version/"])", tabs_listener.message());
  }
  {
    ExtensionTestMessageListener tabs_listener;
    // Reply to the original message and wait for the return message.
    ready_incognito.Reply("");
    EXPECT_TRUE(tabs_listener.WaitUntilSatisfied());
    EXPECT_EQ(R"(["chrome://about/"])", tabs_listener.message());
  }
}

// Tests already-loaded extension activation in incognito profile.
IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest,
                       AlreadyLoadedSplitExtensionActivationInIncognito) {
  ExtensionTestMessageListener ready_regular("Script started regular",
                                             ReplyBehavior::kWillReply);
  ExtensionTestMessageListener ready_incognito("Script started incognito",
                                               ReplyBehavior::kWillReply);
  TestExtensionDir test_dir;
  test_dir.WriteManifest(base::StringPrintf(kIncognitoManifest, "split"));
  test_dir.WriteFile(FILE_PATH_LITERAL("worker.js"), kQueryWorkerScript);

  const Extension* extension =
      LoadExtension(test_dir.UnpackedPath(), {.allow_in_incognito = true});
  ASSERT_TRUE(extension);

  // Open an incognito window.
  // Note: It is important that we create incognito profile _after_ loading
  // |extension| above as we want to test how extensions that already has been
  // activated in the main profile are activated in incognito (see
  // |ServiceWorkerTaskQueue::ActivateIncognitoSplitModeExtensions|).
  Browser* browser_incognito =
      OpenURLOffTheRecord(browser()->profile(), GURL("about:blank"));
  ASSERT_TRUE(browser_incognito);

  // Wait for the extension's service workers to be ready.
  ASSERT_TRUE(ready_regular.WaitUntilSatisfied());
  ASSERT_TRUE(ready_incognito.WaitUntilSatisfied());

  // Load a new tab in both browsers.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("chrome:version")));
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser_incognito, GURL("chrome:about")));

  {
    ExtensionTestMessageListener tabs_listener;
    // The extension waits for the reply to the "ready" sendMessage call
    // and replies with the URLs of the tabs.
    ready_regular.Reply("");
    EXPECT_TRUE(tabs_listener.WaitUntilSatisfied());
    EXPECT_EQ(R"(["chrome://version/"])", tabs_listener.message());
  }
  {
    ExtensionTestMessageListener tabs_listener;
    // Reply to the original message and wait for the return message.
    ready_incognito.Reply("");
    EXPECT_TRUE(tabs_listener.WaitUntilSatisfied());
    EXPECT_EQ(R"(["chrome://about/"])", tabs_listener.message());
  }
}

IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest, TabsQuerySpanning) {
  ExtensionTestMessageListener ready_listener("Script started regular",
                                              ReplyBehavior::kWillReply);

  // Open an incognito window.
  Browser* browser_incognito =
      OpenURLOffTheRecord(browser()->profile(), GURL("about:blank"));
  ASSERT_TRUE(browser_incognito);

  TestExtensionDir test_dir;
  test_dir.WriteManifest(base::StringPrintf(kIncognitoManifest, "spanning"));
  test_dir.WriteFile(FILE_PATH_LITERAL("worker.js"), kQueryWorkerScript);

  const Extension* extension =
      LoadExtension(test_dir.UnpackedPath(), {.allow_in_incognito = true});
  ASSERT_TRUE(extension);

  // Wait for the extension's service worker to be ready.
  ASSERT_TRUE(ready_listener.WaitUntilSatisfied());

  // Load a new tab in both browsers.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("chrome:version")));
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser_incognito, GURL("chrome:about")));

  ExtensionTestMessageListener tabs_listener;
  // The extension waits for the reply to the "ready" sendMessage call
  // and replies with the URLs of the tabs.
  ready_listener.Reply("");
  EXPECT_TRUE(tabs_listener.WaitUntilSatisfied());
  EXPECT_EQ(R"(["chrome://version/","chrome://about/"])",
            tabs_listener.message());
}

IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest, TabsOnUpdatedSplit) {
  ExtensionTestMessageListener ready_regular("Script started regular",
                                             ReplyBehavior::kWillReply);
  ExtensionTestMessageListener ready_incognito("Script started incognito",
                                               ReplyBehavior::kWillReply);
  // Open an incognito window.
  Browser* browser_incognito =
      OpenURLOffTheRecord(browser()->profile(), GURL("about:blank"));
  ASSERT_TRUE(browser_incognito);

  TestExtensionDir test_dir;
  test_dir.WriteManifest(base::StringPrintf(kIncognitoManifest, "split"));
  test_dir.WriteFile(FILE_PATH_LITERAL("worker.js"), kTabsOnUpdatedSplitScript);

  const Extension* extension =
      LoadExtension(test_dir.UnpackedPath(), {.allow_in_incognito = true});
  ASSERT_TRUE(extension);

  // Wait for the extension's service workers to be ready.
  ASSERT_TRUE(ready_regular.WaitUntilSatisfied());
  ASSERT_TRUE(ready_incognito.WaitUntilSatisfied());

  // Load a new tab in both browsers.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("chrome:version")));
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser_incognito, GURL("chrome:about")));

  {
    ExtensionTestMessageListener tabs_listener;
    // The extension waits for the reply to the "ready" sendMessage call
    // and replies with the URLs of the tabs.
    ready_regular.Reply("");
    EXPECT_TRUE(tabs_listener.WaitUntilSatisfied());
    EXPECT_EQ(R"(["chrome://version/"])", tabs_listener.message());
  }
  {
    ExtensionTestMessageListener tabs_listener;
    // The extension waits for the reply to the "ready" sendMessage call
    // and replies with the URLs of the tabs.
    ready_incognito.Reply("");
    EXPECT_TRUE(tabs_listener.WaitUntilSatisfied());
    EXPECT_EQ(R"(["chrome://about/"])", tabs_listener.message());
  }
}

IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest,
                       TabsOnUpdatedSpanning) {
  // The spanning test differs from the Split test because it lets the
  // renderer send the URLs once the expected number of onUpdated
  // events have completed. This solves flakiness in the previous
  // implementation, where the browser pulled the URLs from the
  // renderer.
  ExtensionTestMessageListener ready_listener("Script started regular",
                                              ReplyBehavior::kWillReply);

  // Open an incognito window.
  Browser* browser_incognito =
      OpenURLOffTheRecord(browser()->profile(), GURL("about:blank"));
  ASSERT_TRUE(browser_incognito);

  TestExtensionDir test_dir;
  test_dir.WriteManifest(base::StringPrintf(kIncognitoManifest, "spanning"));
  test_dir.WriteFile(FILE_PATH_LITERAL("worker.js"),
                     kTabsOnUpdatedSpanningScript);

  const Extension* extension =
      LoadExtension(test_dir.UnpackedPath(), {.allow_in_incognito = true});
  ASSERT_TRUE(extension);

  // Wait for the extension's service worker to be ready.
  ASSERT_TRUE(ready_listener.WaitUntilSatisfied());

  // Let the JavaScript side know the number of expected URLs.
  ready_listener.Reply(2);

  // This listener will catch the URLs coming back.
  ExtensionTestMessageListener tabs_listener;

  // Load a new tab in both browsers.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("chrome:version")));
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser_incognito, GURL("chrome:about")));

  EXPECT_TRUE(tabs_listener.WaitUntilSatisfied());
  EXPECT_EQ(R"(["chrome://version/","chrome://about/"])",
            tabs_listener.message());
}

// Tests the restriction on registering service worker scripts at root scope.
IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest,
                       ServiceWorkerScriptRootScope) {
  // Set up an observer to track all SW registrations. We expect only
  // one for the extension's root scope. This test attempts to register
  // an additional service worker, which will fail.
  service_worker_test_utils::TestRegistrationObserver observer(
      browser()->profile());
  ExtensionTestMessageListener registration_listener("REGISTRATION_FAILED");
  registration_listener.set_failure_message("WORKER_STARTED");
  const Extension* extension = LoadExtension(test_data_dir_.AppendASCII(
      "service_worker/worker_based_background/script_root_scope"));
  ASSERT_TRUE(extension);

  EXPECT_TRUE(registration_listener.WaitUntilSatisfied());
  // We expect exactly one registration, which is the one specified in the
  // manifest.
  EXPECT_EQ(1, observer.GetCompletedCount(extension->url()));
}

// Tests that a worker that failed to start due to 'install' error, clears its
// PendingTasks correctly. Also tests that subsequent tasks are properly
// cleared.
// Regression test for https://crbug.com/1019161.
IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest,
                       WorkerStartFailureClearsPendingTasks) {
  content::ServiceWorkerContext* context =
      GetServiceWorkerContext(browser()->profile());

  const ExtensionId test_extension_id("iegclhlplifhodhkoafiokenjoapiobj");
  // Set up an observer to wait for worker to start and then stop.
  TestWorkerObserver observer(context, test_extension_id);

  TestExtensionDir test_dir;
  // Key for extension id |test_extension_id|.
  constexpr const char kKey[] =
      "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAjzv7dI7Ygyh67VHE1DdidudpYf8P"
      "Ffv8iucWvzO+3xpF/Dm5xNo7aQhPNiEaNfHwJQ7lsp4gc+C+4bbaVewBFspTruoSJhZc5uEf"
      "qxwovJwN+v1/SUFXTXQmQBv6gs0qZB4gBbl4caNQBlqrFwAMNisnu1V6UROna8rOJQ90D7Nv"
      "7TCwoVPKBfVshpFjdDOTeBg4iLctO3S/06QYqaTDrwVceSyHkVkvzBY6tc6mnYX0RZu78J9i"
      "L8bdqwfllOhs69cqoHHgrLdI6JdOyiuh6pBP6vxMlzSKWJ3YTNjaQTPwfOYaLMuzdl0v+Ydz"
      "afIzV9zwe4Xiskk+5JNGt8b2rQIDAQAB";

  test_dir.WriteManifest(base::StringPrintf(
      R"({
           "name": "Test Extension",
           "manifest_version": 2,
           "version": "0.1",
           "key": "%s",
           "permissions": ["tabs"],
           "background": {"service_worker": "script.js"}
         })",
      kKey));
  constexpr char kScript[] =
      R"(self.oninstall = function(event) {
           event.waitUntil(Promise.reject(new Error('foo')));
         };)";
  test_dir.WriteFile(FILE_PATH_LITERAL("script.js"), kScript);
  const Extension* extension = LoadExtension(test_dir.UnpackedPath());
  ASSERT_TRUE(extension);
  ASSERT_EQ(test_extension_id, extension->id());
  LazyContextId context_id(browser()->profile(), extension->id(),
                           extension->url());
  // Let the worker start so it rejects 'install' event. This causes the worker
  // to stop.
  observer.WaitForWorkerStart();
  observer.WaitForWorkerStop();

  ServiceWorkerStartFailureObserver worker_start_failure_observer(
      extension->id());

  ServiceWorkerTaskQueue* service_worker_task_queue =
      ServiceWorkerTaskQueue::Get(browser()->profile());
  // Adding a pending task to ServiceWorkerTaskQueue will try to start the
  // worker that failed during installation before. This enables us to ensure
  // that this pending task is cleared on failure.
  service_worker_task_queue->AddPendingTask(context_id, base::DoNothing());

  // Since the worker rejects installation, it will fail to start now. Ensure
  // that the queue sees pending tasks while the error is observed.
  EXPECT_GT(
      worker_start_failure_observer.WaitForDidStartWorkerFailAndGetTaskCount(),
      0u);
  // Ensure DidStartWorkerFail finished clearing tasks.
  base::RunLoop().RunUntilIdle();

  // And the task count will be reset to zero afterwards.
  EXPECT_EQ(0u,
            service_worker_task_queue->GetNumPendingTasksForTest(context_id));
  EXPECT_EQ(blink::ServiceWorkerStatusCode::kErrorNotFound,
            worker_start_failure_observer.status_code());
}

IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest,
                       ProcessManagerRegistrationOnShutdown) {
  // Note that StopServiceWorkerForScope call below expects the worker to be
  // completely installed, so wait for the |extension| worker to see "activate"
  // event.
  ExtensionTestMessageListener activated_listener("WORKER_ACTIVATED");
  const Extension* extension = LoadExtension(test_data_dir_.AppendASCII(
      "service_worker/worker_based_background/process_manager"));
  ASSERT_TRUE(extension);
  EXPECT_TRUE(activated_listener.WaitUntilSatisfied());

  absl::optional<WorkerId> worker_id =
      GetUniqueRunningWorkerId(extension->id());
  ASSERT_TRUE(worker_id);
  browsertest_util::StopServiceWorkerForExtensionGlobalScope(
      browser()->profile(), extension->id());

  EXPECT_FALSE(ProcessManager::Get(profile())->HasServiceWorker(*worker_id));
}

IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest,
                       ProcessManagerRegistrationOnTerminate) {
  // NOTE: It is not necessary to wait for "activate" event from the worker
  // for this test, but we're lazily reusing the extension from
  // ProcessManagerRegistrationOnShutdown test.
  ExtensionTestMessageListener activated_listener("WORKER_ACTIVATED");
  const Extension* extension = LoadExtension(test_data_dir_.AppendASCII(
      "service_worker/worker_based_background/process_manager"));
  ASSERT_TRUE(extension);
  EXPECT_TRUE(activated_listener.WaitUntilSatisfied());

  absl::optional<WorkerId> worker_id =
      GetUniqueRunningWorkerId(extension->id());
  ASSERT_TRUE(worker_id);
  {
    // Terminate worker's RenderProcessHost.
    content::RenderProcessHost* worker_render_process_host =
        content::RenderProcessHost::FromID(worker_id->render_process_id);
    ASSERT_TRUE(worker_render_process_host);
    content::RenderProcessHostWatcher process_exit_observer(
        worker_render_process_host,
        content::RenderProcessHostWatcher::WATCH_FOR_PROCESS_EXIT);
    worker_render_process_host->Shutdown(content::RESULT_CODE_KILLED);
    process_exit_observer.Wait();
  }

  EXPECT_FALSE(ProcessManager::Get(profile())->HasServiceWorker(*worker_id));
}

// Tests that worker ref count increments while extension API function is
// active.
IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest, WorkerRefCount) {
  ExtensionTestMessageListener worker_start_listener("WORKER STARTED");

  const Extension* extension = LoadExtension(test_data_dir_.AppendASCII(
      "service_worker/worker_based_background/worker_ref_count"));
  ASSERT_TRUE(extension);
  ASSERT_TRUE(worker_start_listener.WaitUntilSatisfied());

  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), extension->GetResourceURL("page.html")));
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();

  url::Origin extension_origin = url::Origin::Create(extension->url());
  blink::StorageKey extension_key(extension_origin);

  // Service worker should have no pending requests because it hasn't performed
  // any extension API request yet.
  EXPECT_EQ(0u, GetWorkerRefCount(extension_key));

  ExtensionTestMessageListener worker_listener("CHECK_REF_COUNT",
                                               ReplyBehavior::kWillReply);
  worker_listener.set_failure_message("FAILURE");
  ASSERT_TRUE(content::ExecuteScript(web_contents, "window.testSendMessage()"));
  ASSERT_TRUE(worker_listener.WaitUntilSatisfied());

  // Service worker should have exactly one pending request because
  // chrome.test.sendMessage() API call is in-flight.
  EXPECT_EQ(1u, GetWorkerRefCount(extension_key));

  // Perform another extension API request while one is ongoing.
  {
    ExtensionTestMessageListener listener("CHECK_REF_COUNT",
                                          ReplyBehavior::kWillReply);
    listener.set_failure_message("FAILURE");
    ASSERT_TRUE(
        content::ExecuteScript(web_contents, "window.testSendMessage()"));
    ASSERT_TRUE(listener.WaitUntilSatisfied());

    // Service worker currently has two extension API requests in-flight.
    EXPECT_EQ(2u, GetWorkerRefCount(extension_key));
    // Finish executing the nested chrome.test.sendMessage() first.
    listener.Reply("Hello world");
  }

  ExtensionTestMessageListener worker_completion_listener(
      "SUCCESS_FROM_WORKER");
  // Finish executing chrome.test.sendMessage().
  worker_listener.Reply("Hello world");
  EXPECT_TRUE(worker_completion_listener.WaitUntilSatisfied());

  // The following block makes sure we have received all the IPCs related to
  // ref-count from the worker.
  {
    // The following roundtrip:
    // browser->extension->worker->extension->browser
    // will ensure that the worker sent the relevant ref count IPCs.
    std::string result;
    EXPECT_TRUE(content::ExecuteScriptAndExtractString(
        web_contents, "window.roundtripToWorker();", &result));
    EXPECT_EQ("roundtrip-succeeded", result);

    // Ensure IO thread IPCs run.
    content::RunAllTasksUntilIdle();
  }

  // The ref count should drop to 0.
  EXPECT_EQ(0u, GetWorkerRefCount(extension_key));
}

IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest,
                       PRE_EventsAfterRestart) {
  ExtensionTestMessageListener event_added_listener("ready");

  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir scoped_temp_dir;
  ASSERT_TRUE(scoped_temp_dir.CreateUniqueTempDir());
  base::FilePath pem_path = test_data_dir_.AppendASCII("service_worker")
                                .AppendASCII("worker_based_background")
                                .AppendASCII("test_extension.pem");
  // Note: Extension is packed to avoid reloading while loading.
  base::FilePath extension_path = PackExtensionWithOptions(
      test_data_dir_.AppendASCII("service_worker/worker_based_background/"
                                 "events_to_stopped_extension"),
      scoped_temp_dir.GetPath().AppendASCII("v1.crx"), pem_path,
      base::FilePath());
  const Extension* extension =
      LoadExtension(extension_path, {.wait_for_registration_stored = true});
  ASSERT_TRUE(extension);
  EXPECT_EQ(kTestExtensionId, extension->id());
  ProcessManager* pm = ProcessManager::Get(browser()->profile());
  // TODO(crbug.com/969884): This will break once keep alive counts
  // for service workers are tracked by the Process Manager.
  EXPECT_LT(pm->GetLazyKeepaliveCount(extension), 1);
  EXPECT_TRUE(pm->GetLazyKeepaliveActivities(extension).empty());
  EXPECT_TRUE(event_added_listener.WaitUntilSatisfied());
}

// After browser restarts, this test step ensures that opening a tab fires
// tabs.onCreated event listener to the extension without explicitly loading the
// extension. This is because the extension registered a listener for
// tabs.onMoved before browser restarted in PRE_EventsAfterRestart.
IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest, EventsAfterRestart) {
  // Verify there is no RenderProcessHost for the extension.
  EXPECT_FALSE(ExtensionHasRenderProcessHost(kTestExtensionId));

  ExtensionTestMessageListener moved_tab_listener("moved-tab");
  // Add a tab, then move it.
  content::WebContents* new_web_contents =
      browsertest_util::AddTab(browser(), GURL(url::kAboutBlankURL));
  EXPECT_TRUE(new_web_contents);
  browser()->tab_strip_model()->MoveWebContentsAt(
      browser()->tab_strip_model()->count() - 1, 0, false);
  EXPECT_TRUE(moved_tab_listener.WaitUntilSatisfied());
}

// TODO(crbug.com/1319942): Test flaky on Linux.
#if BUILDFLAG(IS_LINUX)
#define MAYBE_PRE_WebRequestAfterRestart DISABLED_PRE_WebRequestAfterRestart
#define MAYBE_WebRequestAfterRestart DISABLED_WebRequestAfterRestart
#else
#define MAYBE_PRE_WebRequestAfterRestart PRE_WebRequestAfterRestart
#define MAYBE_WebRequestAfterRestart DISABLED_WebRequestAfterRestart
#endif

IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest,
                       MAYBE_PRE_WebRequestAfterRestart) {
  ExtensionTestMessageListener event_added_listener("listener-added");

  base::FilePath extension_path = test_data_dir_.AppendASCII("service_worker")
                                      .AppendASCII("worker_based_background")
                                      .AppendASCII("web_request_after_restart");
  const Extension* extension =
      LoadExtension(extension_path, {.wait_for_registration_stored = true});
  ASSERT_TRUE(extension);
  EXPECT_TRUE(event_added_listener.WaitUntilSatisfied());
}

// After browser restarts, this test step ensures that navigating a tab fires
// the webRequest listener.
IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest,
                       MAYBE_WebRequestAfterRestart) {
  ExtensionTestMessageListener event_added_listener("listener-added");
  EXPECT_TRUE(event_added_listener.WaitUntilSatisfied());
  // Navigate and expect the listener in the extension to be triggered.
  ResultCatcher catcher;
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/empty.html")));
  EXPECT_TRUE(catcher.GetNextResult()) << message_;
}

IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest, TabsOnCreated) {
  ASSERT_TRUE(RunExtensionTest("tabs/lazy_background_on_created", {},
                               {.context_type = ContextType::kServiceWorker}))
      << message_;
}

// Disabled on win due to flakiness: https://crbug.com/1127126.
#if BUILDFLAG(IS_WIN)
#define MAYBE_PRE_FilteredEventsAfterRestart \
  DISABLED_PRE_FilteredEventsAfterRestart
#define MAYBE_FilteredEventsAfterRestart DISABLED_FilteredEventsAfterRestart
#else
#define MAYBE_PRE_FilteredEventsAfterRestart PRE_FilteredEventsAfterRestart
#define MAYBE_FilteredEventsAfterRestart FilteredEventsAfterRestart
#endif
IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest,
                       MAYBE_PRE_FilteredEventsAfterRestart) {
  ExtensionTestMessageListener listener_added("ready");
  base::FilePath test_dir =
      test_data_dir_.AppendASCII("service_worker/worker_based_background");
  base::FilePath pem_path = test_dir.AppendASCII("test_extension.pem");

  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir scoped_temp_dir;
  ASSERT_TRUE(scoped_temp_dir.CreateUniqueTempDir());

  const Extension* extension = LoadExtension(
      PackExtensionWithOptions(
          test_dir.AppendASCII("filtered_events_after_restart"),
          scoped_temp_dir.GetPath().AppendASCII("test_extension.crx"), pem_path,
          base::FilePath()),
      // Wait for the registration to be stored so that it's persistent
      // before the test terminates.
      {.wait_for_registration_stored = true});
  ASSERT_TRUE(extension);
  EXPECT_EQ(kTestExtensionId, extension->id());
  ProcessManager* pm = ProcessManager::Get(browser()->profile());
  // TODO(crbug.com/969884): This will break once keep alive counts
  // for service workers are tracked by the Process Manager.
  EXPECT_LT(pm->GetLazyKeepaliveCount(extension), 1);
  EXPECT_TRUE(pm->GetLazyKeepaliveActivities(extension).empty());
  EXPECT_TRUE(listener_added.WaitUntilSatisfied());
}

// After browser restarts, this test step ensures that opening a tab fires
// tabs.onCreated event listener to the extension without explicitly loading the
// extension. This is because the extension registered a listener for
// tabs.onMoved before browser restarted in PRE_EventsAfterRestart.
//
// Disabled on win due to flakiness: https://crbug.com/1127126.
IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest,
                       MAYBE_FilteredEventsAfterRestart) {
  // Verify there is no RenderProcessHost for the extension.
  EXPECT_FALSE(ExtensionHasRenderProcessHost(kTestExtensionId));

  // Create a tab to a.html, expect it to navigate to b.html. The service worker
  // will see two webNavigation.onCommitted events.
  GURL page_url = embedded_test_server()->GetURL(
      "/extensions/api_test/service_worker/worker_based_background/"
      "filtered_events_after_restart/"
      "a.html");
  ExtensionTestMessageListener worker_filtered_event_listener(
      "PASS_FROM_WORKER");
  worker_filtered_event_listener.set_failure_message("FAIL_FROM_WORKER");
  content::WebContents* web_contents =
      browsertest_util::AddTab(browser(), page_url);
  EXPECT_TRUE(web_contents);
  EXPECT_TRUE(worker_filtered_event_listener.WaitUntilSatisfied());
}

// Tests that chrome.browserAction.onClicked sees user gesture.
IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest,
                       BrowserActionUserGesture) {
  // First, load |extension| first so that it has browserAction.onClicked
  // listener registered.
  ExtensionTestMessageListener listener_added("ready");
  const Extension* extension = LoadExtension(
      test_data_dir_.AppendASCII("service_worker/worker_based_background/"
                                 "browser_action"));
  ASSERT_TRUE(extension);
  EXPECT_TRUE(listener_added.WaitUntilSatisfied());

  ResultCatcher catcher;
  // Click on browser action to start the test.
  {
    content::WebContents* web_contents =
        browsertest_util::AddTab(browser(), GURL("about:blank"));
    ASSERT_TRUE(web_contents);
    ExtensionActionRunner::GetForWebContents(
        browser()->tab_strip_model()->GetActiveWebContents())
        ->RunAction(extension, true);
  }
  EXPECT_TRUE(catcher.GetNextResult()) << message_;
}

// Tests that Service Worker notification handlers can call extension APIs that
// require user gesture to be present.
IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTestWithNotification,
                       ServiceWorkerNotificationClick) {
  ResultCatcher catcher;
  const Extension* extension = LoadExtension(
      test_data_dir_.AppendASCII("service_worker/worker_based_background/"
                                 "notification_click"));
  ASSERT_TRUE(extension);
  EXPECT_TRUE(catcher.GetNextResult()) << message_;

  // Click on the Service Worker notification.
  {
    std::vector<message_center::Notification> notifications =
        GetDisplayedNotifications();
    ASSERT_EQ(1u, notifications.size());
    display_service_tester_->SimulateClick(
        NotificationHandler::Type::WEB_PERSISTENT, notifications[0].id(),
        absl::nullopt, absl::nullopt);
  }

  EXPECT_TRUE(catcher.GetNextResult()) << message_;
}

// Tests chrome.permissions.request API.
IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest, PermissionsAPI) {
  // First, load |extension| first so that it has browserAction.onClicked
  // listener registered.
  ExtensionTestMessageListener worker_listener("ready");
  const Extension* extension = LoadExtension(test_data_dir_.AppendASCII(
      "service_worker/worker_based_background/permissions_api"));
  ASSERT_TRUE(extension);
  const ExtensionId extension_id = extension->id();
  EXPECT_TRUE(worker_listener.WaitUntilSatisfied());

  // "storage" permission is optional in |extension|, and isn't available right
  // away.
  EXPECT_FALSE(extension->permissions_data()->HasAPIPermission(
      mojom::APIPermissionID::kStorage));

  PermissionsRequestFunction::SetAutoConfirmForTests(true);

  ResultCatcher catcher;
  // Click on browser action to start the test.
  {
    content::WebContents* web_contents =
        browsertest_util::AddTab(browser(), GURL("about:blank"));
    ASSERT_TRUE(web_contents);
    ExtensionActionRunner::GetForWebContents(
        browser()->tab_strip_model()->GetActiveWebContents())
        ->RunAction(extension, true);
  }
  EXPECT_TRUE(catcher.GetNextResult()) << message_;

  // Expect the permission ("storage") to be available now.
  EXPECT_TRUE(extension->permissions_data()->HasAPIPermission(
      mojom::APIPermissionID::kStorage));
}

// Tests that loading a component MV3 extension succeeds.
IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest, Component) {
  ASSERT_TRUE(
      RunExtensionTest("service_worker/worker_based_background/component", {},
                       {.load_as_component = true}))
      << message_;
}

// Tests that two extensions with the same ServiceWorkerContext* can be
// disabled successfully. This test ensures that the DCHECK in
// ServiceWorkerTaskQueue::StopObserving does not fail in such a scenario.

// Regression test for https://crbug.com/1223476
IN_PROC_BROWSER_TEST_F(ServiceWorkerBasedBackgroundTest,
                       ObserveServiceWorkerContext) {
  static constexpr char kManifest[] = R"({
    "name": "Test",
    "manifest_version": 3,
    "version": "0.1",
    "background": {"service_worker": "worker.js"}
  })";
  static constexpr char kScript[] = "";

  TestExtensionDir test_dir1;
  test_dir1.WriteManifest(kManifest);
  test_dir1.WriteFile(FILE_PATH_LITERAL("worker.js"), kScript);

  scoped_refptr<const Extension> extension1 =
      LoadExtension(test_dir1.UnpackedPath());
  ASSERT_TRUE(extension1);

  TestExtensionDir test_dir2;
  test_dir2.WriteManifest(kManifest);
  test_dir2.WriteFile(FILE_PATH_LITERAL("worker.js"), kScript);

  scoped_refptr<const Extension> extension2 =
      LoadExtension(test_dir2.UnpackedPath());
  ASSERT_TRUE(extension2);
  EXPECT_NE(extension1->id(), extension2->id());

  extension_service()->DisableExtension(extension1->id(),
                                        disable_reason::DISABLE_USER_ACTION);
  extension_service()->DisableExtension(extension2->id(),
                                        disable_reason::DISABLE_USER_ACTION);
}

constexpr char kReady[] = "ready";
class ServiceWorkerTestWithEarlyReadyMesssage
    : public ServiceWorkerWithEarlyMessageListenerTest<kReady>,
      public ServiceWorkerTaskQueue::TestObserver {
 public:
  ServiceWorkerTestWithEarlyReadyMesssage() {
    ServiceWorkerTaskQueue::SetObserverForTest(this);
  }

  // ServiceWorkerTaskQueue::TestObserver:
  void RegistrationMismatchMitigated(bool success) override {
    registration_mismatch_mitigated_ = success;
    if (run_loop_)
      run_loop_->Quit();
  }

  // Waits and returns whether registration mismatch mitigation succeeded.
  bool WaitForRegistrationMismatchMitigation() {
    if (!registration_mismatch_mitigated_) {
      run_loop_ = std::make_unique<base::RunLoop>();
      run_loop_->Run();
    }
    return *registration_mismatch_mitigated_;
  }

  base::HistogramTester& histogram_tester() { return histogram_tester_; }

 private:
  absl::optional<bool> registration_mismatch_mitigated_;
  base::HistogramTester histogram_tester_;
  std::unique_ptr<base::RunLoop> run_loop_;
};

// Tests that missing SW registration is re-registered upon extension
// activation.
//
// In PRE_ test, extension is loaded and then its SW is unregistered. After
// browser restart, the tests verifies that the extension still runs after
// browser restart.
//
// Regression test for crbug.com/1271154.
IN_PROC_BROWSER_TEST_F(ServiceWorkerTestWithEarlyReadyMesssage,
                       PRE_MissingRegistrationMitigated) {
  const Extension* extension = LoadExtension(test_data_dir_.AppendASCII(
      "service_worker/worker_based_background/activate_ensures_register"));
  ASSERT_TRUE(extension);
  EXPECT_TRUE(WaitForMessage());

  // Unregister the extension service worker.
  {
    base::RunLoop run_loop;
    content::ServiceWorkerContext* context = GetServiceWorkerContext(profile());

    // The service worker is registered at the root scope.
    context->UnregisterServiceWorker(
        extension->url(), blink::StorageKey(extension->origin()),
        base::BindLambdaForTesting(
            [&run_loop](bool success) { run_loop.Quit(); }));
    run_loop.Run();
  }
}

IN_PROC_BROWSER_TEST_F(ServiceWorkerTestWithEarlyReadyMesssage,
                       MissingRegistrationMitigated) {
  WaitForMessage();

  EXPECT_EQ(1,
            histogram_tester().GetBucketCount(
                "Extensions.ServiceWorkerBackground.RegistrationWhenExpected",
                false));
  EXPECT_EQ(
      1, histogram_tester().GetBucketCount(
             "Extensions.ServiceWorkerBackground.RegistrationMismatchLocation",
             mojom::ManifestLocation::kUnpacked));

  bool mitigation_succeeded = WaitForRegistrationMismatchMitigation();
  EXPECT_TRUE(mitigation_succeeded);
  EXPECT_EQ(
      1, histogram_tester().GetBucketCount(
             "Extensions.ServiceWorkerBackground.RegistrationMismatchMitigated",
             true));
}

// Tests that an extension's service worker can't be used to relax the extension
// CSP.
IN_PROC_BROWSER_TEST_P(ServiceWorkerWithManifestVersionTest,
                       ExtensionCSPModification) {
  ExtensionTestMessageListener worker_listener("ready");
  const Extension* extension = LoadExtensionInternal(test_data_dir_.AppendASCII(
      "service_worker/worker_based_background/extension_csp_modification"));
  ASSERT_TRUE(extension);
  const ExtensionId extension_id = extension->id();
  ASSERT_TRUE(worker_listener.WaitUntilSatisfied());

  const char* kDefaultCSP = GetParam() == ManifestVersion::kTwo
                                ? "script-src 'self' blob: filesystem:; "
                                  "object-src 'self' blob: filesystem:;"
                                : "script-src 'self'; "
                                  "object-src 'self';";
  ExtensionTestMessageListener csp_modified_listener(kDefaultCSP);
  csp_modified_listener.set_extension_id(extension_id);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), extension->GetResourceURL("extension_page.html")));
  EXPECT_TRUE(csp_modified_listener.WaitUntilSatisfied());

  // Ensure the inline script is not executed because we ensure that the
  // extension's CSP is applied in the renderer (even though the service worker
  // removed it).
  constexpr char kScript[] = R"(
    (() => {
      try {
        scriptExecuted;
        window.domAutomationController.send('FAIL');
      } catch (e) {
        const result = e.message.includes('scriptExecuted is not defined')
          ? 'PASS' : 'FAIL: ' + e.message;
        window.domAutomationController.send(result);
      }
    })();
  )";
  std::string result;
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(
      content::ExecuteScriptAndExtractString(web_contents, kScript, &result));
  EXPECT_EQ("PASS", result);

  // Also ensure that a local scheme subframe in the extension page correctly
  // inherits the extension CSP.
  result = "";
  content::RenderFrameHost* iframe =
      content::ChildFrameAt(web_contents->GetPrimaryMainFrame(), 0);
  ASSERT_TRUE(iframe);
  ASSERT_TRUE(content::ExecuteScriptAndExtractString(iframe, kScript, &result));
  EXPECT_EQ("PASS", result);
}

INSTANTIATE_TEST_SUITE_P(,
                         ServiceWorkerWithManifestVersionTest,
                         ::testing::Values(ManifestVersion::kTwo,
                                           ManifestVersion::kThree));

// Tests that console messages logged by extension service workers, both via
// the typical console.* methods and via our custom bindings console, are
// passed through the normal ServiceWorker console messaging and are
// observable.
IN_PROC_BROWSER_TEST_F(ServiceWorkerLazyBackgroundTest, ConsoleLogging) {
  // A helper class to wait for a particular message to be logged from a
  // ServiceWorker.
  class ConsoleMessageObserver : public content::ServiceWorkerContextObserver {
   public:
    ConsoleMessageObserver(content::BrowserContext* browser_context,
                           std::u16string expected_message)
        : expected_message_(std::move(expected_message)) {
      scoped_observation_.Observe(GetServiceWorkerContext(browser_context));
    }

    ConsoleMessageObserver(const ConsoleMessageObserver&) = delete;
    ConsoleMessageObserver& operator=(const ConsoleMessageObserver&) = delete;

    ~ConsoleMessageObserver() override = default;

    void Wait() { run_loop_.Run(); }

   private:
    // ServiceWorkerContextObserver:
    void OnReportConsoleMessage(
        int64_t version_id,
        const GURL& scope,
        const content::ConsoleMessage& message) override {
      // NOTE: We could check the version_id, but it shouldn't be necessary with
      // the expected messages we're verifying (they're uncommon enough).
      if (message.message != expected_message_)
        return;
      scoped_observation_.Reset();
      run_loop_.QuitWhenIdle();
    }

    std::u16string expected_message_;
    base::RunLoop run_loop_;
    base::ScopedObservation<content::ServiceWorkerContext,
                            content::ServiceWorkerContextObserver>
        scoped_observation_{this};
  };

  TestExtensionDir test_dir;
  test_dir.WriteManifest(
      R"({
           "name": "Test Extension",
           "manifest_version": 2,
           "version": "0.1",
           "background": {"service_worker": "script.js"}
         })");
  constexpr char kScript[] =
      R"(// First, log a message using the normal, built-in blink console.
         console.log('test message');
         chrome.test.runTests([
           function justATest() {
             // Next, we use the "Console" object from
             // extensions/renderer/console.cc, which is used by custom bindings
             // so that it isn't tampered with by untrusted script. The test
             // custom bindings log a message whenever a test is passed, so we
             // force a log by just passing this test.
             chrome.test.succeed();
           }
         ]);)";
  test_dir.WriteFile(FILE_PATH_LITERAL("script.js"), kScript);

  // The observer for the built-in blink console.
  ConsoleMessageObserver default_console_observer(profile(), u"test message");
  // The observer for our custom extensions bindings console.
  ConsoleMessageObserver custom_console_observer(profile(),
                                                 u"[SUCCESS] justATest");

  const Extension* extension = LoadExtension(test_dir.UnpackedPath());
  ASSERT_TRUE(extension);

  default_console_observer.Wait();
  custom_console_observer.Wait();
  // If we receive both messages, we passed!
}

class ServiceWorkerCheckBindingsTest
    : public ServiceWorkerTest,
      public testing::WithParamInterface<version_info::Channel> {
 public:
  ServiceWorkerCheckBindingsTest() = default;

  ServiceWorkerCheckBindingsTest(const ServiceWorkerCheckBindingsTest&) =
      delete;
  ServiceWorkerCheckBindingsTest& operator=(
      const ServiceWorkerCheckBindingsTest&) = delete;

  ~ServiceWorkerCheckBindingsTest() override = default;
};

// Load an extension in each allowed channel and check that the expected
// bindings are available.
IN_PROC_BROWSER_TEST_P(ServiceWorkerCheckBindingsTest, BindingsAvailability) {
  static constexpr char kManifest[] =
      R"({
           "name": "Service Worker-based background script",
           "version": "0.1",
           "manifest_version": 2,
           "description": "Test that bindings are available.",
           "permissions": ["storage"],
           "background": {"service_worker": "worker.js"}
         })";
  static constexpr char kScript[] =
      R"(var chromeAPIAvailable = !!chrome;
         var storageAPIAvailable = chromeAPIAvailable && !!chrome.storage;
         var tabsAPIAvailable = chromeAPIAvailable && !!chrome.tabs;
         var testAPIAvailable = chromeAPIAvailable && !!chrome.test;

         if (chromeAPIAvailable && storageAPIAvailable && tabsAPIAvailable &&
             testAPIAvailable) {
           chrome.test.sendMessage('SUCCESS');
         } else {
           console.log('chromeAPIAvailable: ' + chromeAPIAvailable);
           console.log('storageAPIAvailable: ' + storageAPIAvailable);
           console.log('tabsAPIAvailable: ' + tabsAPIAvailable);
           console.log('testAPIAvailable: ' + testAPIAvailable);
           chrome.test.sendMessage('FAILURE');
         })";

  TestExtensionDir test_dir;
  test_dir.WriteManifest(kManifest);
  test_dir.WriteFile(FILE_PATH_LITERAL("worker.js"), kScript);
  const base::FilePath path = test_dir.UnpackedPath();

  // Wait for the extension to load and the script to finish.
  ExtensionTestMessageListener result_listener("SUCCESS");
  result_listener.set_failure_message("FAILURE");

  scoped_refptr<const Extension> extension =
      LoadExtension(test_dir.UnpackedPath());
  ASSERT_TRUE(extension.get());

  EXPECT_TRUE(BackgroundInfo::IsServiceWorkerBased(extension.get()));
  EXPECT_TRUE(result_listener.WaitUntilSatisfied());
}

INSTANTIATE_TEST_SUITE_P(Unknown,
                         ServiceWorkerCheckBindingsTest,
                         ::testing::Values(version_info::Channel::UNKNOWN,
                                           version_info::Channel::CANARY,
                                           version_info::Channel::DEV,
                                           version_info::Channel::BETA,
                                           version_info::Channel::STABLE));

}  // namespace extensions
