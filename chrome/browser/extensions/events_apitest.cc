// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/strings/stringprintf.h"
#include "build/build_config.h"
#include "chrome/browser/extensions/chrome_extension_test_notification_observer.h"
#include "chrome/browser/extensions/chrome_extensions_browser_client.h"
#include "chrome/browser/extensions/chrome_test_extension_loader.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/extensions/extension_browsertest.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/profiles/profile_observer.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "extensions/browser/event_router.h"
#include "extensions/browser/extension_event_histogram_value.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/process_manager.h"
#include "extensions/browser/process_manager_observer.h"
#include "extensions/test/extension_background_page_waiter.h"
#include "extensions/test/result_catcher.h"
#include "extensions/test/test_extension_dir.h"

namespace extensions {

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, Events) {
  ASSERT_TRUE(RunExtensionTest("events")) << message_;
}

// Tests that events are unregistered when an extension page shuts down.
IN_PROC_BROWSER_TEST_F(ExtensionApiTest, EventsAreUnregistered) {
  // In this test, page1.html registers for a number of events, then navigates
  // to page2.html, which should unregister those events. page2.html notifies
  // pass, by which point the event should have been unregistered.

  EventRouter* event_router = EventRouter::Get(profile());
  ExtensionRegistry* registry = ExtensionRegistry::Get(profile());

  constexpr char test_extension_name[] = "events_are_unregistered";
  ASSERT_TRUE(RunExtensionTest(test_extension_name, {.page_url = "page1.html"}))
      << message_;

  // Find the extension we just installed by looking for the path.
  base::FilePath extension_path =
      test_data_dir_.AppendASCII(test_extension_name);
  const Extension* extension =
      GetExtensionByPath(registry->enabled_extensions(), extension_path);
  ASSERT_TRUE(extension) << "No extension found at \"" << extension_path.value()
                         << "\" (absolute path \""
                         << base::MakeAbsoluteFilePath(extension_path).value()
                         << "\")";
  const std::string& id = extension->id();

  // The page has closed, so no matter what all events are no longer listened
  // to. Assertions for normal events:
  EXPECT_FALSE(
      event_router->ExtensionHasEventListener(id, "browserAction.onClicked"));
  EXPECT_FALSE(
      event_router->ExtensionHasEventListener(id, "runtime.onStartup"));
  EXPECT_FALSE(
      event_router->ExtensionHasEventListener(id, "runtime.onSuspend"));
  EXPECT_FALSE(
      event_router->ExtensionHasEventListener(id, "runtime.onInstalled"));
  // Assertions for filtered events:
  EXPECT_FALSE(event_router->ExtensionHasEventListener(
      id, "webNavigation.onBeforeNavigate"));
  EXPECT_FALSE(
      event_router->ExtensionHasEventListener(id, "webNavigation.onCommitted"));
  EXPECT_FALSE(event_router->ExtensionHasEventListener(
      id, "webNavigation.onDOMContentLoaded"));
  EXPECT_FALSE(
      event_router->ExtensionHasEventListener(id, "webNavigation.onCompleted"));
}

// Test that listeners for webview-related events are not stored (even for lazy
// contexts). See crbug.com/736381.
IN_PROC_BROWSER_TEST_F(ExtensionApiTest, WebViewEventRegistration) {
  ASSERT_TRUE(RunExtensionTest("events/webview_events",
                               {.launch_as_platform_app = true}))
      << message_;
  EventRouter* event_router = EventRouter::Get(profile());
  // We should not register lazy listeners for any webview-related events.
  EXPECT_FALSE(
      event_router->HasLazyEventListenerForTesting("webViewInternal.onClose"));
  EXPECT_FALSE(event_router->HasLazyEventListenerForTesting("webview.close"));
  EXPECT_FALSE(event_router->HasLazyEventListenerForTesting(
      "chromeWebViewInternal.onContextMenuShow"));
  EXPECT_FALSE(event_router->HasLazyEventListenerForTesting(
      "chromeWebViewInternal.onClicked"));
  EXPECT_FALSE(event_router->HasLazyEventListenerForTesting(
      "webViewInternal.contextMenus"));
  // Chrome webview context menu events also use a "subevent" pattern, so we
  // need to look for suffixed events. These seem to always be suffixed with
  // "3" and "4", but look for the first 10 to be a bit safer.
  for (int i = 0; i < 10; ++i) {
    EXPECT_FALSE(event_router->HasLazyEventListenerForTesting(
        base::StringPrintf("chromeWebViewInternal.onClicked/%d", i)));
    EXPECT_FALSE(event_router->HasLazyEventListenerForTesting(
        base::StringPrintf("chromeWebViewInternal.onContextMenuShow/%d", i)));
    EXPECT_FALSE(
        event_router->HasLazyEventListenerForTesting(base::StringPrintf(
            "webViewInternal.declarativeWebRequest.onMessage/%d", i)));
  }

  // Sanity check: app.runtime.onLaunched should have a lazy listener.
  EXPECT_TRUE(
      event_router->HasLazyEventListenerForTesting("app.runtime.onLaunched"));
}

namespace {

class ProfileDestructionWatcher : public ProfileObserver {
 public:
  ProfileDestructionWatcher() = default;

  ProfileDestructionWatcher(const ProfileDestructionWatcher&) = delete;
  ProfileDestructionWatcher& operator=(const ProfileDestructionWatcher&) =
      delete;

  ~ProfileDestructionWatcher() override = default;

  void Watch(Profile* profile) { observed_profiles_.AddObservation(profile); }
  void WaitForDestruction() { run_loop_.Run(); }
  bool will_be_destroyed() const { return will_be_destroyed_; }

 private:
  // ProfileObserver:
  void OnProfileWillBeDestroyed(Profile* profile) override {
    DCHECK(!will_be_destroyed_) << "Double profile destruction";
    will_be_destroyed_ = true;
    observed_profiles_.RemoveObservation(profile);
    run_loop_.Quit();

    // Broadcast an event to the event router. Since a shutdown is occurring, it
    // should be ignored and cause no problems.
    EventRouter* event_router = EventRouter::Get(profile);
    event_router->BroadcastEvent(std::make_unique<Event>(
        events::FOR_TEST, "tabs.onActivated", base::Value::List()));
  }

  bool will_be_destroyed_ = false;
  base::RunLoop run_loop_;
  base::ScopedMultiSourceObservation<Profile, ProfileObserver>
      observed_profiles_{this};
};

}  // namespace

// Tests that events broadcast right after a profile has started to be destroyed
// do not cause a crash. Regression test for crbug.com/1335837.
IN_PROC_BROWSER_TEST_F(ExtensionApiTest, DispatchEventDuringShutdown) {
  // Minimize background page expiration time for testing purposes.
  ProcessManager::SetEventPageIdleTimeForTesting(1);
  ProcessManager::SetEventPageSuspendingTimeForTesting(1);

  // Load extension.
  constexpr char kManifest[] = R"({
    "name": "Test",
    "manifest_version": 2,
    "version": "1.0",
    "background": {"scripts": ["background.js"], "persistent": false}
  })";
  constexpr char kBackground[] = R"(
    chrome.tabs.onActivated.addListener(activeInfo => {});
    chrome.test.notifyPass();
  )";
  TestExtensionDir test_dir;
  test_dir.WriteManifest(kManifest);
  test_dir.WriteFile(FILE_PATH_LITERAL("background.js"), kBackground);
  ChromeTestExtensionLoader loader(profile());
  loader.set_pack_extension(true);
  ResultCatcher catcher;
  auto extension = loader.LoadExtension(test_dir.UnpackedPath());
  ASSERT_TRUE(extension);
  EXPECT_TRUE(catcher.GetNextResult());

  // Verify that an event was registered.
  EventRouter* event_router = EventRouter::Get(profile());
  EXPECT_TRUE(event_router->ExtensionHasEventListener(extension->id(),
                                                      "tabs.onActivated"));
  ExtensionBackgroundPageWaiter(profile(), *extension)
      .WaitForBackgroundClosed();

  // Dispatch event after starting profile destruction.
  ProfileDestructionWatcher watcher;
  watcher.Watch(profile());
  profile()->MaybeSendDestroyedNotification();
  watcher.WaitForDestruction();
  ASSERT_TRUE(watcher.will_be_destroyed());
}

class EventsApiTest : public ExtensionApiTest {
 public:
  EventsApiTest() {}

  EventsApiTest(const EventsApiTest&) = delete;
  EventsApiTest& operator=(const EventsApiTest&) = delete;

 protected:
  void SetUpOnMainThread() override {
    ExtensionApiTest::SetUpOnMainThread();
    EXPECT_TRUE(scoped_temp_dir_.CreateUniqueTempDir());
  }

  struct ExtensionCRXData {
    std::string unpacked_relative_path;
    base::FilePath crx_path;
    explicit ExtensionCRXData(const std::string& unpacked_relative_path)
        : unpacked_relative_path(unpacked_relative_path) {}
  };

  void SetUpCRX(const std::string& root_dir,
                const std::string& pem_filename,
                std::vector<ExtensionCRXData>* crx_data_list) {
    const base::FilePath test_dir = test_data_dir_.AppendASCII(root_dir);
    const base::FilePath pem_path = test_dir.AppendASCII(pem_filename);
    for (ExtensionCRXData& crx_data : *crx_data_list) {
      crx_data.crx_path = PackExtensionWithOptions(
          test_dir.AppendASCII(crx_data.unpacked_relative_path),
          scoped_temp_dir_.GetPath().AppendASCII(
              crx_data.unpacked_relative_path + ".crx"),
          pem_path, base::FilePath());
    }
  }

 private:
  base::ScopedTempDir scoped_temp_dir_;
};

// Tests that updating an extension sends runtime.onInstalled event to the
// updated extension.
IN_PROC_BROWSER_TEST_F(EventsApiTest, ExtensionUpdateSendsOnInstalledEvent) {
  std::vector<ExtensionCRXData> data;
  data.emplace_back("v1");
  data.emplace_back("v2");
  SetUpCRX("lazy_events/on_installed", "pem.pem", &data);

  ExtensionId extension_id;
  {
    // Install version 1 of the extension and expect runtime.onInstalled.
    ResultCatcher catcher;
    const int expected_change = 1;
    const Extension* extension_v1 =
        InstallExtension(data[0].crx_path, expected_change);
    extension_id = extension_v1->id();
    ASSERT_TRUE(extension_v1);
    EXPECT_TRUE(catcher.GetNextResult());
  }
  {
    // Update to version 2, also expect runtime.onInstalled.
    ResultCatcher catcher;
    const int expected_change = 0;
    const Extension* extension_v2 =
        UpdateExtension(extension_id, data[1].crx_path, expected_change);
    ASSERT_TRUE(extension_v2);
    EXPECT_TRUE(catcher.GetNextResult());
  }
}

// Tests that if updating an extension makes the extension disabled (due to
// permissions increase), then enabling the extension fires runtime.onInstalled
// correctly to the updated extension.
IN_PROC_BROWSER_TEST_F(EventsApiTest,
                       UpdateDispatchesOnInstalledAfterEnablement) {
  std::vector<ExtensionCRXData> data;
  data.emplace_back("v1");
  data.emplace_back("v2");
  SetUpCRX("lazy_events/on_installed_permissions_increase", "pem.pem", &data);

  ExtensionRegistry* registry = ExtensionRegistry::Get(profile());
  ExtensionId extension_id;
  {
    // Install version 1 of the extension and expect runtime.onInstalled.
    ResultCatcher catcher;
    const int expected_change = 1;
    const Extension* extension_v1 =
        InstallExtension(data[0].crx_path, expected_change);
    extension_id = extension_v1->id();
    ASSERT_TRUE(extension_v1);
    EXPECT_TRUE(catcher.GetNextResult());
  }
  {
    // Update to version 2, which will be disabled due to permissions increase.
    ResultCatcher catcher;
    const int expected_change = -1;  // Expect extension to be disabled.
    ASSERT_FALSE(
        UpdateExtension(extension_id, data[1].crx_path, expected_change));

    const Extension* extension_v2 =
        registry->disabled_extensions().GetByID(extension_id);
    ASSERT_TRUE(extension_v2);
    // Enable the extension.
    extension_service()->GrantPermissionsAndEnableExtension(extension_v2);
    EXPECT_TRUE(catcher.GetNextResult());
  }
}

// This test is OK on Windows, but times out on other platforms.
// https://crbug.com/833854
#if BUILDFLAG(IS_WIN)
#define MAYBE_NewlyIntroducedListener NewlyIntroducedListener
#else
#define MAYBE_NewlyIntroducedListener DISABLED_NewlyIntroducedListener
#endif
// Tests that if an extension's updated version has a new lazy listener, it
// fires properly after the update.
IN_PROC_BROWSER_TEST_F(EventsApiTest, MAYBE_NewlyIntroducedListener) {
  std::vector<ExtensionCRXData> data;
  data.emplace_back("v1");
  data.emplace_back("v2");
  SetUpCRX("lazy_events/new_event_in_new_version", "pem.pem", &data);

  ExtensionId extension_id;
  {
    // Install version 1 of the extension.
    ResultCatcher catcher;
    const int expected_change = 1;
    const Extension* extension_v1 =
        InstallExtension(data[0].crx_path, expected_change);
    EXPECT_TRUE(extension_v1);
    extension_id = extension_v1->id();
    ASSERT_TRUE(extension_v1);
    EXPECT_TRUE(catcher.GetNextResult());
  }
  {
    // Update to version 2, that has tabs.onCreated event listener.
    ResultCatcher catcher;
    const int expected_change = 0;
    const Extension* extension_v2 =
        UpdateExtension(extension_id, data[1].crx_path, expected_change);
    ASSERT_TRUE(extension_v2);
    ui_test_utils::NavigateToURLWithDisposition(
        browser(), GURL(url::kAboutBlankURL),
        WindowOpenDisposition::NEW_BACKGROUND_TAB,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
    // Expect tabs.onCreated to fire.
    EXPECT_TRUE(catcher.GetNextResult());
  }
}

class ChromeUpdatesEventsApiTest : public EventsApiTest,
                                   public ProcessManagerObserver {
 public:
  ChromeUpdatesEventsApiTest() {
    // We set this in the constructor (rather than in a SetUp() method) because
    // it needs to be done before any of the extensions system is created.
    ChromeExtensionsBrowserClient::set_did_chrome_update_for_testing(true);
  }

  ChromeUpdatesEventsApiTest(const ChromeUpdatesEventsApiTest&) = delete;
  ChromeUpdatesEventsApiTest& operator=(const ChromeUpdatesEventsApiTest&) =
      delete;

  void SetUpOnMainThread() override {
    EventsApiTest::SetUpOnMainThread();
    ProcessManager* process_manager = ProcessManager::Get(profile());
    ProcessManager::Get(profile())->AddObserver(this);
    const ProcessManager::FrameSet& frames = process_manager->GetAllFrames();
    for (auto* frame : frames) {
      const Extension* extension =
          process_manager->GetExtensionForRenderFrameHost(frame);
      if (extension)
        observed_extension_names_.insert(extension->name());
    }
  }

  void TearDownOnMainThread() override {
    ProcessManager::Get(profile())->RemoveObserver(this);
    ChromeExtensionsBrowserClient::set_did_chrome_update_for_testing(false);
    EventsApiTest::TearDownOnMainThread();
  }

  void OnBackgroundHostCreated(ExtensionHost* host) override {
    // Use name since it's more deterministic than ID.
    observed_extension_names_.insert(host->extension()->name());
  }

  const std::set<std::string> observed_extension_names() const {
    return observed_extension_names_;
  }

 private:
  std::set<std::string> observed_extension_names_;
};

IN_PROC_BROWSER_TEST_F(ChromeUpdatesEventsApiTest, PRE_ChromeUpdates) {
  {
    ChromeTestExtensionLoader loader(profile());
    loader.set_pack_extension(true);
    ResultCatcher catcher;
    ASSERT_TRUE(loader.LoadExtension(
        test_data_dir_.AppendASCII("lazy_events/chrome_updates/listener")));
    EXPECT_TRUE(catcher.GetNextResult());
  }
  {
    ChromeTestExtensionLoader loader(profile());
    loader.set_pack_extension(true);
    ResultCatcher catcher;
    ASSERT_TRUE(loader.LoadExtension(
        test_data_dir_.AppendASCII("lazy_events/chrome_updates/non_listener")));
    EXPECT_TRUE(catcher.GetNextResult());
  }
}

// Test that we only dispatch the onInstalled event triggered by a chrome update
// to extensions that have a registered onInstalled listener.
IN_PROC_BROWSER_TEST_F(ChromeUpdatesEventsApiTest, ChromeUpdates) {
  ChromeExtensionTestNotificationObserver(browser())
      .WaitForExtensionViewsToLoad();

  content::RunAllPendingInMessageLoop();
  content::RunAllTasksUntilIdle();

  // "chrome updates listener" registerd a listener for the onInstalled event,
  // whereas "chrome updates non listener" did not. Only the
  // "chrome updates listener" extension should have been woken up for the
  // chrome update event.
  EXPECT_TRUE(observed_extension_names().count("chrome updates listener"));
  EXPECT_FALSE(observed_extension_names().count("chrome updates non listener"));
}

}  // namespace extensions
