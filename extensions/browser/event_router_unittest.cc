// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/browser/event_router.h"

#include <memory>
#include <string>
#include <utility>

#include "base/bind.h"
#include "base/compiler_specific.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/values.h"
#include "content/public/browser/browser_context.h"
#include "content/public/test/mock_render_process_host.h"
#include "extensions/browser/event_listener_map.h"
#include "extensions/browser/extensions_test.h"
#include "extensions/browser/test_event_router_observer.h"
#include "extensions/common/extension_api.h"
#include "extensions/common/extension_builder.h"
#include "extensions/common/extension_messages.h"
#include "extensions/common/features/feature_provider.h"
#include "extensions/common/features/simple_feature.h"
#include "testing/gtest/include/gtest/gtest.h"

using base::DictionaryValue;
using base::ListValue;
using base::Value;

namespace extensions {

namespace {

// A simple mock to keep track of listener additions and removals.
class MockEventRouterObserver : public EventRouter::Observer {
 public:
  MockEventRouterObserver()
      : listener_added_count_(0),
        listener_removed_count_(0) {}

  MockEventRouterObserver(const MockEventRouterObserver&) = delete;
  MockEventRouterObserver& operator=(const MockEventRouterObserver&) = delete;

  ~MockEventRouterObserver() override {}

  int listener_added_count() const { return listener_added_count_; }
  int listener_removed_count() const { return listener_removed_count_; }
  const std::string& last_event_name() const { return last_event_name_; }

  void Reset() {
    listener_added_count_ = 0;
    listener_removed_count_ = 0;
    last_event_name_.clear();
  }

  // EventRouter::Observer overrides:
  void OnListenerAdded(const EventListenerInfo& details) override {
    listener_added_count_++;
    last_event_name_ = details.event_name;
  }

  void OnListenerRemoved(const EventListenerInfo& details) override {
    listener_removed_count_++;
    last_event_name_ = details.event_name;
  }

 private:
  int listener_added_count_;
  int listener_removed_count_;
  std::string last_event_name_;
};

using EventListenerConstructor =
    base::RepeatingCallback<std::unique_ptr<EventListener>(
        const std::string& /* event_name */,
        content::RenderProcessHost* /* process */,
        std::unique_ptr<base::DictionaryValue> /* filter */)>;

std::unique_ptr<EventListener> CreateEventListenerForExtension(
    const std::string& extension_id,
    const std::string& event_name,
    content::RenderProcessHost* process,
    std::unique_ptr<base::DictionaryValue> filter) {
  return EventListener::ForExtension(event_name, extension_id, process,
                                     std::move(filter));
}

std::unique_ptr<EventListener> CreateEventListenerForURL(
    const GURL& listener_url,
    const std::string& event_name,
    content::RenderProcessHost* process,
    std::unique_ptr<base::DictionaryValue> filter) {
  return EventListener::ForURL(event_name, listener_url, process,
                               std::move(filter));
}

std::unique_ptr<EventListener> CreateEventListenerForExtensionServiceWorker(
    const std::string& extension_id,
    int64_t service_worker_version_id,
    int worker_thread_id,
    const std::string& event_name,
    content::RenderProcessHost* process,
    std::unique_ptr<base::DictionaryValue> filter) {
  return EventListener::ForExtensionServiceWorker(
      event_name, extension_id, process,
      Extension::GetBaseURLFromExtensionId(extension_id),
      service_worker_version_id, worker_thread_id, std::move(filter));
}

// Creates an extension.  If |component| is true, it is created as a component
// extension.  If |persistent| is true, it is created with a persistent
// background page; otherwise it is created with an event page.
scoped_refptr<const Extension> CreateExtension(bool component,
                                               bool persistent) {
  ExtensionBuilder builder;
  std::unique_ptr<base::DictionaryValue> manifest =
      std::make_unique<base::DictionaryValue>();
  manifest->SetStringKey("name", "foo");
  manifest->SetStringKey("version", "1.0.0");
  manifest->SetIntKey("manifest_version", 2);
  manifest->SetStringPath("background.page", "background.html");
  manifest->SetBoolPath("background.persistent", persistent);
  builder.SetManifest(std::move(manifest));
  if (component)
    builder.SetLocation(mojom::ManifestLocation::kComponent);

  return builder.Build();
}

scoped_refptr<const Extension> CreateServiceWorkerExtension() {
  ExtensionBuilder builder;
  auto manifest = std::make_unique<base::DictionaryValue>();
  manifest->SetStringKey("name", "foo");
  manifest->SetStringKey("version", "1.0.0");
  manifest->SetIntKey("manifest_version", 2);
  manifest->SetStringPath("background.service_worker", "worker.js");
  builder.SetManifest(std::move(manifest));
  return builder.Build();
}

std::unique_ptr<DictionaryValue> CreateHostSuffixFilter(
    const std::string& suffix) {
  auto filter_dict = std::make_unique<DictionaryValue>();
  filter_dict->SetKey("hostSuffix", Value(suffix));

  Value filter_list(Value::Type::LIST);
  filter_list.Append(std::move(*filter_dict));

  auto filter = std::make_unique<DictionaryValue>();
  filter->SetKey("url", std::move(filter_list));
  return filter;
}

}  // namespace

class EventRouterTest : public ExtensionsTest {
 public:
  EventRouterTest() = default;

  EventRouterTest(const EventRouterTest&) = delete;
  EventRouterTest& operator=(const EventRouterTest&) = delete;

 protected:
  // Tests adding and removing observers from EventRouter.
  void RunEventRouterObserverTest(const EventListenerConstructor& constructor);

  // Tests that the correct counts are recorded for the Extensions.Events
  // histograms.
  void ExpectHistogramCounts(int dispatch_count,
                             int component_count,
                             int persistent_count,
                             int suspended_count,
                             int running_count,
                             int service_worker_count) {
    histogram_tester_.ExpectBucketCount("Extensions.Events.Dispatch",
                                        events::HistogramValue::FOR_TEST,
                                        dispatch_count);
    histogram_tester_.ExpectBucketCount("Extensions.Events.DispatchToComponent",
                                        events::HistogramValue::FOR_TEST,
                                        component_count);
    histogram_tester_.ExpectBucketCount(
        "Extensions.Events.DispatchWithPersistentBackgroundPage",
        events::HistogramValue::FOR_TEST, persistent_count);
    histogram_tester_.ExpectBucketCount(
        "Extensions.Events.DispatchWithSuspendedEventPage",
        events::HistogramValue::FOR_TEST, suspended_count);
    histogram_tester_.ExpectBucketCount(
        "Extensions.Events.DispatchWithRunningEventPage",
        events::HistogramValue::FOR_TEST, running_count);
    histogram_tester_.ExpectBucketCount(
        "Extensions.Events.DispatchWithServiceWorkerBackground",
        events::HistogramValue::FOR_TEST, service_worker_count);
  }

 private:
  base::HistogramTester histogram_tester_;
};

class EventRouterFilterTest : public ExtensionsTest,
                              public testing::WithParamInterface<bool> {
 public:
  EventRouterFilterTest() = default;

  EventRouterFilterTest(const EventRouterFilterTest&) = delete;
  EventRouterFilterTest& operator=(const EventRouterFilterTest&) = delete;

  void SetUp() override {
    ExtensionsTest::SetUp();
    render_process_host_ =
        std::make_unique<content::MockRenderProcessHost>(browser_context());
    ASSERT_TRUE(event_router());  // constructs EventRouter
  }

  void TearDown() override {
    render_process_host_.reset();
    ExtensionsTest::TearDown();
  }

  content::RenderProcessHost* render_process_host() const {
    return render_process_host_.get();
  }

  EventRouter* event_router() { return EventRouter::Get(browser_context()); }

  const DictionaryValue* GetFilteredEvents(const std::string& extension_id) {
    return event_router()->GetFilteredEvents(
        extension_id, is_for_service_worker()
                          ? EventRouter::RegisteredEventType::kServiceWorker
                          : EventRouter::RegisteredEventType::kLazy);
  }

  bool ContainsFilter(const std::string& extension_id,
                      const std::string& event_name,
                      const DictionaryValue& to_check) {
    const Value* filter_list = GetFilterList(extension_id, event_name);
    if (!filter_list) {
      ADD_FAILURE();
      return false;
    }

    for (const base::Value& filter : filter_list->GetList()) {
      if (!filter.is_dict()) {
        ADD_FAILURE();
        return false;
      }
      if (filter == to_check)
        return true;
    }
    return false;
  }

  bool is_for_service_worker() const { return GetParam(); }

 private:
  const Value* GetFilterList(const std::string& extension_id,
                             const std::string& event_name) {
    const base::DictionaryValue* filtered_events =
        GetFilteredEvents(extension_id);
    const auto iter = filtered_events->GetDict().begin();
    if (iter->first != event_name)
      return nullptr;

    return iter->second.is_list() ? &iter->second : nullptr;
  }

  std::unique_ptr<content::RenderProcessHost> render_process_host_;
};

TEST_F(EventRouterTest, GetBaseEventName) {
  // Normal event names are passed through unchanged.
  EXPECT_EQ("foo.onBar", EventRouter::GetBaseEventName("foo.onBar"));

  // Sub-events are converted to the part before the slash.
  EXPECT_EQ("foo.onBar", EventRouter::GetBaseEventName("foo.onBar/123"));
}

// Tests adding and removing observers from EventRouter.
void EventRouterTest::RunEventRouterObserverTest(
    const EventListenerConstructor& constructor) {
  EventRouter router(nullptr, nullptr);
  std::unique_ptr<EventListener> listener = constructor.Run(
      "event_name", nullptr, std::make_unique<base::DictionaryValue>());

  // Add/remove works without any observers.
  router.OnListenerAdded(listener.get());
  router.OnListenerRemoved(listener.get());

  // Register observers that both match and don't match the event above.
  MockEventRouterObserver matching_observer;
  router.RegisterObserver(&matching_observer, "event_name");
  MockEventRouterObserver non_matching_observer;
  router.RegisterObserver(&non_matching_observer, "other");

  // Adding a listener notifies the appropriate observers.
  router.OnListenerAdded(listener.get());
  EXPECT_EQ(1, matching_observer.listener_added_count());
  EXPECT_EQ(0, non_matching_observer.listener_added_count());

  // Removing a listener notifies the appropriate observers.
  router.OnListenerRemoved(listener.get());
  EXPECT_EQ(1, matching_observer.listener_removed_count());
  EXPECT_EQ(0, non_matching_observer.listener_removed_count());

  // Adding the listener again notifies again.
  router.OnListenerAdded(listener.get());
  EXPECT_EQ(2, matching_observer.listener_added_count());
  EXPECT_EQ(0, non_matching_observer.listener_added_count());

  // Removing the listener again notifies again.
  router.OnListenerRemoved(listener.get());
  EXPECT_EQ(2, matching_observer.listener_removed_count());
  EXPECT_EQ(0, non_matching_observer.listener_removed_count());

  // Adding a listener with a sub-event notifies the main observer with
  // proper details.
  matching_observer.Reset();
  std::unique_ptr<EventListener> sub_event_listener = constructor.Run(
      "event_name/1", nullptr, std::make_unique<base::DictionaryValue>());
  router.OnListenerAdded(sub_event_listener.get());
  EXPECT_EQ(1, matching_observer.listener_added_count());
  EXPECT_EQ(0, matching_observer.listener_removed_count());
  EXPECT_EQ("event_name/1", matching_observer.last_event_name());

  // Ditto for removing the listener.
  matching_observer.Reset();
  router.OnListenerRemoved(sub_event_listener.get());
  EXPECT_EQ(0, matching_observer.listener_added_count());
  EXPECT_EQ(1, matching_observer.listener_removed_count());
  EXPECT_EQ("event_name/1", matching_observer.last_event_name());
}

TEST_F(EventRouterTest, EventRouterObserverForExtensions) {
  RunEventRouterObserverTest(
      base::BindRepeating(&CreateEventListenerForExtension, "extension_id"));
}

TEST_F(EventRouterTest, EventRouterObserverForURLs) {
  RunEventRouterObserverTest(base::BindRepeating(
      &CreateEventListenerForURL, GURL("http://google.com/path")));
}

TEST_F(EventRouterTest, EventRouterObserverForServiceWorkers) {
  RunEventRouterObserverTest(base::BindRepeating(
      &CreateEventListenerForExtensionServiceWorker, "extension_id",
      // Dummy version_id and thread_id.
      99, 199));
}

TEST_F(EventRouterTest, MultipleEventRouterObserver) {
  EventRouter router(nullptr, nullptr);
  std::unique_ptr<EventListener> listener =
      EventListener::ForURL("event_name", GURL("http://google.com/path"),
                            nullptr, std::make_unique<base::DictionaryValue>());

  // Add/remove works without any observers.
  router.OnListenerAdded(listener.get());
  router.OnListenerRemoved(listener.get());

  // Register two observers for same event name.
  MockEventRouterObserver matching_observer1;
  router.RegisterObserver(&matching_observer1, "event_name");
  MockEventRouterObserver matching_observer2;
  router.RegisterObserver(&matching_observer2, "event_name");

  // Adding a listener notifies the appropriate observers.
  router.OnListenerAdded(listener.get());
  EXPECT_EQ(1, matching_observer1.listener_added_count());
  EXPECT_EQ(1, matching_observer2.listener_added_count());

  // Removing a listener notifies the appropriate observers.
  router.OnListenerRemoved(listener.get());
  EXPECT_EQ(1, matching_observer1.listener_removed_count());
  EXPECT_EQ(1, matching_observer2.listener_removed_count());

  // Unregister the observer so that the current observer no longer receives
  // monitoring, but the other observer still continues to receive monitoring.
  router.UnregisterObserver(&matching_observer1);

  router.OnListenerAdded(listener.get());
  EXPECT_EQ(1, matching_observer1.listener_added_count());
  EXPECT_EQ(2, matching_observer2.listener_added_count());
}

TEST_F(EventRouterTest, TestReportEvent) {
  EventRouter router(browser_context(), nullptr);
  scoped_refptr<const Extension> normal = ExtensionBuilder("Test").Build();
  router.ReportEvent(events::HistogramValue::FOR_TEST, normal.get(),
                     false /** did_enqueue */);
  ExpectHistogramCounts(1 /** Dispatch */, 0 /** DispatchToComponent */,
                        0 /** DispatchWithPersistentBackgroundPage */,
                        0 /** DispatchWithSuspendedEventPage */,
                        0 /** DispatchWithRunningEventPage */,
                        0 /** DispatchWithServiceWorkerBackground */);

  scoped_refptr<const Extension> component =
      CreateExtension(true /** component */, true /** persistent */);
  router.ReportEvent(events::HistogramValue::FOR_TEST, component.get(),
                     false /** did_enqueue */);
  ExpectHistogramCounts(2, 1, 1, 0, 0, 0);

  scoped_refptr<const Extension> persistent = CreateExtension(false, true);
  router.ReportEvent(events::HistogramValue::FOR_TEST, persistent.get(),
                     false /** did_enqueue */);
  ExpectHistogramCounts(3, 1, 2, 0, 0, 0);

  scoped_refptr<const Extension> event = CreateExtension(false, false);
  router.ReportEvent(events::HistogramValue::FOR_TEST, event.get(),
                     false /** did_enqueue */);
  ExpectHistogramCounts(4, 1, 2, 0, 1, 0);
  router.ReportEvent(events::HistogramValue::FOR_TEST, event.get(),
                     true /** did_enqueue */);
  ExpectHistogramCounts(5, 1, 2, 1, 1, 0);

  scoped_refptr<const Extension> component_event = CreateExtension(true, false);
  router.ReportEvent(events::HistogramValue::FOR_TEST, component_event.get(),
                     false /** did_enqueue */);
  ExpectHistogramCounts(6, 2, 2, 1, 2, 0);
  router.ReportEvent(events::HistogramValue::FOR_TEST, component_event.get(),
                     true /** did_enqueue */);
  ExpectHistogramCounts(7, 3, 2, 2, 2, 0);

  scoped_refptr<const Extension> service_worker_extension =
      CreateServiceWorkerExtension();
  router.ReportEvent(events::HistogramValue::FOR_TEST,
                     service_worker_extension.get(), true /** did_enqueue */);
  ExpectHistogramCounts(8, 3, 2, 2, 2, 1);
}

// Tests adding and removing events with filters.
TEST_P(EventRouterFilterTest, Basic) {
  // For the purpose of this test, "." is important in |event_name| as it
  // exercises the code path that uses |event_name| as a key in DictionaryValue.
  const std::string kEventName = "webNavigation.onBeforeNavigate";

  const std::string kExtensionId = "mbflcebpggnecokmikipoihdbecnjfoj";
  auto param = mojom::EventListenerParam::NewExtensionId(kExtensionId);
  const std::string kHostSuffixes[] = {"foo.com", "bar.com", "baz.com"};

  absl::optional<ServiceWorkerIdentifier> worker_identifier = absl::nullopt;
  if (is_for_service_worker()) {
    ServiceWorkerIdentifier identifier;
    identifier.scope = Extension::GetBaseURLFromExtensionId(kExtensionId);
    identifier.version_id = 99;  // Dummy version_id.
    identifier.thread_id = 199;  // Dummy thread_id.
    worker_identifier =
        absl::make_optional<ServiceWorkerIdentifier>(std::move(identifier));
  }
  std::vector<std::unique_ptr<DictionaryValue>> filters;
  for (size_t i = 0; i < std::size(kHostSuffixes); ++i) {
    std::unique_ptr<base::DictionaryValue> filter =
        CreateHostSuffixFilter(kHostSuffixes[i]);
    event_router()->AddFilteredEventListener(kEventName, render_process_host(),
                                             param.Clone(), worker_identifier,
                                             *filter, true);
    filters.push_back(std::move(filter));
  }

  const base::DictionaryValue* filtered_events =
      GetFilteredEvents(kExtensionId);
  ASSERT_TRUE(filtered_events);
  ASSERT_EQ(1u, filtered_events->DictSize());

  const auto iter = filtered_events->GetDict().begin();
  ASSERT_EQ(kEventName, iter->first);
  ASSERT_TRUE(iter->second.is_list());
  ASSERT_EQ(3u, iter->second.GetList().size());

  ASSERT_TRUE(ContainsFilter(kExtensionId, kEventName, *filters[0]));
  ASSERT_TRUE(ContainsFilter(kExtensionId, kEventName, *filters[1]));
  ASSERT_TRUE(ContainsFilter(kExtensionId, kEventName, *filters[2]));

  // Remove the second filter.
  event_router()->RemoveFilteredEventListener(kEventName, render_process_host(),
                                              param.Clone(), worker_identifier,
                                              *filters[1], true);
  ASSERT_TRUE(ContainsFilter(kExtensionId, kEventName, *filters[0]));
  ASSERT_FALSE(ContainsFilter(kExtensionId, kEventName, *filters[1]));
  ASSERT_TRUE(ContainsFilter(kExtensionId, kEventName, *filters[2]));

  // Remove the first filter.
  event_router()->RemoveFilteredEventListener(kEventName, render_process_host(),
                                              param.Clone(), worker_identifier,
                                              *filters[0], true);
  ASSERT_FALSE(ContainsFilter(kExtensionId, kEventName, *filters[0]));
  ASSERT_FALSE(ContainsFilter(kExtensionId, kEventName, *filters[1]));
  ASSERT_TRUE(ContainsFilter(kExtensionId, kEventName, *filters[2]));

  // Remove the third filter.
  event_router()->RemoveFilteredEventListener(kEventName, render_process_host(),
                                              param.Clone(), worker_identifier,
                                              *filters[2], true);
  ASSERT_FALSE(ContainsFilter(kExtensionId, kEventName, *filters[0]));
  ASSERT_FALSE(ContainsFilter(kExtensionId, kEventName, *filters[1]));
  ASSERT_FALSE(ContainsFilter(kExtensionId, kEventName, *filters[2]));
}

TEST_P(EventRouterFilterTest, URLBasedFilteredEventListener) {
  const std::string kEventName = "windows.onRemoved";
  const GURL kUrl("chrome-untrusted://terminal");
  absl::optional<ServiceWorkerIdentifier> worker_identifier = absl::nullopt;
  auto filter = std::make_unique<DictionaryValue>();
  bool lazy = false;
  EXPECT_FALSE(event_router()->HasEventListener(kEventName));
  event_router()->AddFilteredEventListener(
      kEventName, render_process_host(),
      mojom::EventListenerParam::NewListenerUrl(kUrl), worker_identifier,
      *filter, lazy);
  EXPECT_TRUE(event_router()->HasEventListener(kEventName));
  event_router()->RemoveFilteredEventListener(
      kEventName, render_process_host(),
      mojom::EventListenerParam::NewListenerUrl(kUrl), worker_identifier,
      *filter, lazy);
  EXPECT_FALSE(event_router()->HasEventListener(kEventName));
}

INSTANTIATE_TEST_SUITE_P(Lazy, EventRouterFilterTest, testing::Values(false));
INSTANTIATE_TEST_SUITE_P(ServiceWorker,
                         EventRouterFilterTest,
                         testing::Values(true));

class EventRouterDispatchTest : public ExtensionsTest {
 public:
  EventRouterDispatchTest() = default;
  EventRouterDispatchTest(const EventRouterDispatchTest&) = delete;
  EventRouterDispatchTest& operator=(const EventRouterDispatchTest&) = delete;

  void SetUp() override {
    ExtensionsTest::SetUp();
    render_process_host_ =
        std::make_unique<content::MockRenderProcessHost>(browser_context());
    ASSERT_TRUE(event_router());  // constructs EventRouter
  }

  void TearDown() override {
    render_process_host_.reset();
    ExtensionsTest::TearDown();
  }

  content::RenderProcessHost* process() const {
    return render_process_host_.get();
  }
  EventRouter* event_router() { return EventRouter::Get(browser_context()); }

 private:
  std::unique_ptr<content::RenderProcessHost> render_process_host_;
};

TEST_F(EventRouterDispatchTest, TestDispatch) {
  std::string ext1 = "ext1";
  std::string ext2 = "ext2";
  GURL webui1("chrome-untrusted://one");
  GURL webui2("chrome-untrusted://two");
  std::string event_name = "testapi.onEvent";
  FeatureProvider provider;
  auto feature = std::make_unique<SimpleFeature>();
  feature->set_name("test feature");
  feature->set_matches({webui1.spec().c_str(), webui2.spec().c_str()});
  provider.AddFeature(event_name, std::move(feature));
  ExtensionAPI::GetSharedInstance()->RegisterDependencyProvider("api",
                                                                &provider);
  TestEventRouterObserver observer(event_router());
  auto add_extension = [&](const std::string& id) {
    scoped_refptr<const Extension> extension =
        ExtensionBuilder()
            .SetID(id)
            .SetManifest(DictionaryBuilder()
                             .Set("name", "Test app")
                             .Set("version", "1.0")
                             .Set("manifest_version", 2)
                             .Build())
            .Build();
    ExtensionRegistry::Get(browser_context())->AddEnabled(extension);
  };
  add_extension(ext1);
  add_extension(ext2);
  auto event = [](std::string name) {
    return std::make_unique<extensions::Event>(extensions::events::FOR_TEST,
                                               name, base::Value::List());
  };

  // Register both extensions and both URLs for event.
  event_router()->AddEventListener(event_name, process(), ext1);
  event_router()->AddEventListener(event_name, process(), ext2);
  event_router()->AddEventListenerForURL(event_name, process(), webui1);
  event_router()->AddEventListenerForURL(event_name, process(), webui2);

  // Should only dispatch to the single specified extension or url.
  event_router()->DispatchEventToExtension(ext1, event(event_name));
  EXPECT_EQ(1u, observer.dispatched_events().size());
  observer.ClearEvents();
  event_router()->DispatchEventToExtension(ext2, event(event_name));
  EXPECT_EQ(1u, observer.dispatched_events().size());
  observer.ClearEvents();
  event_router()->DispatchEventToURL(webui1, event(event_name));
  EXPECT_EQ(1u, observer.dispatched_events().size());
  observer.ClearEvents();
  event_router()->DispatchEventToURL(webui2, event(event_name));
  EXPECT_EQ(1u, observer.dispatched_events().size());
  observer.ClearEvents();

  // No listeners registered for 'api.other' event.
  event_router()->DispatchEventToExtension(ext1, event("api.other"));
  EXPECT_EQ(0u, observer.dispatched_events().size());
  event_router()->DispatchEventToURL(webui1, event("api.other"));
  EXPECT_EQ(0u, observer.dispatched_events().size());
}

}  // namespace extensions
