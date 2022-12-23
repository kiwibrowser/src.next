// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/breadcrumb_manager_browser_agent.h"

#include "base/feature_list.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/breadcrumbs/breadcrumb_manager_keyed_service_factory.h"
#include "chrome/browser/metrics/chrome_metrics_service_accessor.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "components/breadcrumbs/core/breadcrumb_manager_keyed_service.h"
#include "components/breadcrumbs/core/features.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/web_contents_tester.h"
#include "testing/gtest/include/gtest/gtest.h"

// Test fixture for testing BreadcrumbManagerBrowserAgent class.
class BreadcrumbManagerBrowserAgentTest : public BrowserWithTestWindowTest {
 protected:
  BreadcrumbManagerBrowserAgentTest() {
    scoped_feature_list_.InitWithFeatures({breadcrumbs::kLogBreadcrumbs}, {});
  }

  void SetUp() override {
    BrowserWithTestWindowTest::SetUp();
    breadcrumb_service_ =
        static_cast<breadcrumbs::BreadcrumbManagerKeyedService*>(
            BreadcrumbManagerKeyedServiceFactory::GetForBrowserContext(
                profile()));
  }

  void InsertTab(Browser* browser) {
    std::unique_ptr<content::WebContents> contents =
        content::WebContentsTester::CreateTestWebContents(profile(), nullptr);
    browser->tab_strip_model()->AppendWebContents(std::move(contents),
                                                  /*foreground=*/true);
  }

  std::list<std::string> GetEvents() const {
    return breadcrumb_service_->GetEvents(/*event_count_limit=*/0);
  }

 private:
  base::test::ScopedFeatureList scoped_feature_list_;
  raw_ptr<breadcrumbs::BreadcrumbManagerKeyedService> breadcrumb_service_ =
      nullptr;
};

// Tests that an event logged by the BrowserAgent is returned with events for
// the associated tab.
TEST_F(BreadcrumbManagerBrowserAgentTest, LogEvent) {
  ASSERT_EQ(0u, GetEvents().size());
  InsertTab(browser());
  EXPECT_EQ(1u, GetEvents().size());
}

// Tests that events logged through BrowserAgents associated with different
// Browser instances are returned with events for the associated tab and are
// uniquely identifiable.
TEST_F(BreadcrumbManagerBrowserAgentTest, MultipleBrowsers) {
  ASSERT_EQ(0u, GetEvents().size());

  // Insert tab into `browser`.
  InsertTab(browser());

  // Create and set up second Browser.
  Browser::CreateParams create_params(profile(), false);
  std::unique_ptr<BrowserWindow> test_window = CreateBrowserWindow();
  create_params.window = test_window.get();
  std::unique_ptr<Browser> browser2(Browser::Create(create_params));
  EXPECT_TRUE(browser2);

  // Insert tab into `browser2`.
  InsertTab(browser2.get());

  const std::list<std::string> events = GetEvents();
  EXPECT_EQ(2u, events.size());
  const std::string event1 = events.front();
  const std::string event2 = events.back();

  // Separately compare the start and end of the event strings to ensure
  // uniqueness at both the Browser and tab layer.
  const size_t event1_split_pos = event1.find("Insert");
  const size_t event2_split_pos = event2.find("Insert");

  // The start of the string must be unique to differentiate the associated
  // Browser object by including the BreadcrumbManagerBrowserAgent's
  // `unique_id_`.
  // (The timestamp will match due to TimeSource::MOCK_TIME in the `task_env_`.)
  const std::string event1_browser_part = event1.substr(event1_split_pos);
  const std::string event2_browser_part = event2.substr(event2_split_pos);
  EXPECT_NE(event1_browser_part, event2_browser_part);

  // The end of the string must be unique because the tabs are different
  // and that needs to be represented in the event string.
  const std::string event1_tab_part =
      event1.substr(event1_split_pos, event1.length() - event1_split_pos);
  const std::string event2_tab_part =
      event2.substr(event2_split_pos, event2.length() - event2_split_pos);
  EXPECT_NE(event1_tab_part, event2_tab_part);

  // Empty `browser2`'s tab strip so that it can be destroyed.
  browser2->tab_strip_model()->CloseAllTabs();
}

// Tests batch closing.
TEST_F(BreadcrumbManagerBrowserAgentTest, BatchOperations) {
  InsertTab(browser());
  InsertTab(browser());

  // Close multiple tabs.
  browser()->tab_strip_model()->CloseAllTabs();
  const std::list<std::string> events = GetEvents();
  ASSERT_EQ(3u, events.size());
  EXPECT_NE(std::string::npos, events.back().find("Closed 2 tabs"))
      << events.back();
}
