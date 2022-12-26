// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// History unit tests come in two flavors:
//
// 1. The more complicated style is that the unit test creates a full history
//    service. This spawns a background thread for the history backend, and
//    all communication is asynchronous. This is useful for testing more
//    complicated things or end-to-end behavior.
//
// 2. The simpler style is to create a history backend on this thread and
//    access it directly without a HistoryService object. This is much simpler
//    because communication is synchronous. Generally, sets should go through
//    the history backend (since there is a lot of logic) but gets can come
//    directly from the HistoryDatabase. This is because the backend generally
//    has no logic in the getter except threading stuff, which we don't want
//    to run.

#include "components/history/core/browser/history_service.h"

#include <stdint.h>

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <utility>

#include "base/bind.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/location.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/bind.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/task_environment.h"
#include "components/history/core/browser/history_backend.h"
#include "components/history/core/browser/history_database_params.h"
#include "components/history/core/browser/history_db_task.h"
#include "components/history/core/test/database_test_utils.h"
#include "components/history/core/test/test_history_database.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace history {

class HistoryServiceTest : public testing::Test {
 public:
  HistoryServiceTest() = default;
  ~HistoryServiceTest() override = default;

 protected:
  friend class BackendDelegate;

  // testing::Test
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    history_dir_ = temp_dir_.GetPath().AppendASCII("HistoryServiceTest");
    ASSERT_TRUE(base::CreateDirectory(history_dir_));
    history_service_ = std::make_unique<history::HistoryService>();
    if (!history_service_->Init(
            TestHistoryDatabaseParamsForPath(history_dir_))) {
      history_service_.reset();
      ADD_FAILURE();
    }
  }

  void TearDown() override {
    if (history_service_)
      CleanupHistoryService();

    // Make sure we don't have any event pending that could disrupt the next
    // test.
    base::RunLoop().RunUntilIdle();
  }

  void CleanupHistoryService() {
    DCHECK(history_service_);

    base::RunLoop run_loop;
    history_service_->ClearCachedDataForContextID(nullptr);
    history_service_->SetOnBackendDestroyTask(run_loop.QuitClosure());
    history_service_->Cleanup();
    history_service_.reset();

    // Wait for the backend class to terminate before deleting the files and
    // moving to the next test. Note: if this never terminates, somebody is
    // probably leaking a reference to the history backend, so it never calls
    // our destroy task.
    run_loop.Run();
  }

  // Fills the query_url_result_ structures with the information about the given
  // URL and whether the operation succeeded or not.
  bool QueryURL(const GURL& url) {
    base::RunLoop run_loop;
    history_service_->QueryURL(
        url, true,
        base::BindLambdaForTesting([&](history::QueryURLResult result) {
          query_url_result_ = std::move(result);
          run_loop.Quit();
        }),
        &tracker_);
    run_loop.Run();  // Will be exited in SaveURLAndQuit.
    return query_url_result_.success;
  }

  // Fills in saved_redirects_ with the redirect information for the given URL,
  // returning true on success. False means the URL was not found.
  void QueryRedirectsFrom(const GURL& url) {
    base::RunLoop run_loop;
    history_service_->QueryRedirectsFrom(
        url,
        base::BindOnce(&HistoryServiceTest::OnRedirectQueryComplete,
                       base::Unretained(this), run_loop.QuitClosure()),
        &tracker_);
    run_loop.Run();  // Will be exited in *QueryComplete.
  }

  // Callback for QueryRedirects.
  void OnRedirectQueryComplete(base::OnceClosure done,
                               history::RedirectList redirects) {
    saved_redirects_ = std::move(redirects);
    std::move(done).Run();
  }

  void QueryMostVisitedURLs() {
    const int kResultCount = 20;

    base::RunLoop run_loop;
    history_service_->QueryMostVisitedURLs(
        kResultCount, base::BindLambdaForTesting([&](MostVisitedURLList urls) {
          most_visited_urls_ = urls;
          run_loop.Quit();
        }),
        &tracker_);
    run_loop.Run();  // Will be exited in *QueryComplete.
  }

  base::ScopedTempDir temp_dir_;

  base::test::TaskEnvironment task_environment_;

  MostVisitedURLList most_visited_urls_;

  // When non-NULL, this will be deleted on tear down and we will block until
  // the backend thread has completed. This allows tests for the history
  // service to use this feature, but other tests to ignore this.
  std::unique_ptr<history::HistoryService> history_service_;

  // names of the database files
  base::FilePath history_dir_;

  // Set by the redirect callback when we get data. You should be sure to
  // clear this before issuing a redirect request.
  history::RedirectList saved_redirects_;

  // For history requests.
  base::CancelableTaskTracker tracker_;

  // For saving URL info after a call to QueryURL
  history::QueryURLResult query_url_result_;
};

// Simple test that removes a bookmark. This test exercises the code paths in
// History that block till BookmarkModel is loaded.
TEST_F(HistoryServiceTest, RemoveNotification) {
  ASSERT_TRUE(history_service_.get());

  // Add a URL.
  GURL url("http://www.google.com");

  history_service_->AddPage(url, base::Time::Now(), nullptr, 1, GURL(),
                            RedirectList(), ui::PAGE_TRANSITION_TYPED,
                            SOURCE_BROWSED, false, false);

  // This won't actually delete the URL, rather it'll empty out the visits.
  // This triggers blocking on the BookmarkModel.
  history_service_->DeleteURLs({url});
}

TEST_F(HistoryServiceTest, AddPage) {
  ASSERT_TRUE(history_service_.get());
  // Add the page once from a child frame.
  const GURL test_url("http://www.google.com/");
  history_service_->AddPage(test_url, base::Time::Now(), nullptr, 0, GURL(),
                            history::RedirectList(),
                            ui::PAGE_TRANSITION_MANUAL_SUBFRAME,
                            history::SOURCE_BROWSED, false, false);
  EXPECT_TRUE(QueryURL(test_url));
  EXPECT_EQ(1, query_url_result_.row.visit_count());
  EXPECT_EQ(0, query_url_result_.row.typed_count());
  EXPECT_TRUE(
      query_url_result_.row.hidden());  // Hidden because of child frame.

  // Add the page once from the main frame (should unhide it).
  history_service_->AddPage(test_url, base::Time::Now(), nullptr, 0, GURL(),
                            history::RedirectList(), ui::PAGE_TRANSITION_LINK,
                            history::SOURCE_BROWSED, false, false);
  EXPECT_TRUE(QueryURL(test_url));
  EXPECT_EQ(2, query_url_result_.row.visit_count());  // Added twice.
  EXPECT_EQ(0, query_url_result_.row.typed_count());  // Never typed.
  EXPECT_FALSE(
      query_url_result_.row.hidden());  // Because loaded in main frame.
}

TEST_F(HistoryServiceTest, AddRedirect) {
  ASSERT_TRUE(history_service_.get());
  history::RedirectList first_redirects = {GURL("http://first.page.com/"),
                                           GURL("http://second.page.com/")};

  // Add the sequence of pages as a server with no referrer. Note that we need
  // to have a non-NULL page ID scope.
  history_service_->AddPage(first_redirects.back(), base::Time::Now(),
                            reinterpret_cast<ContextID>(1), 0, GURL(),
                            first_redirects, ui::PAGE_TRANSITION_LINK,
                            history::SOURCE_BROWSED, true, false);

  // The first page should be added once with a link visit type (because we set
  // LINK when we added the original URL, and a referrer of nowhere (0).
  EXPECT_TRUE(QueryURL(first_redirects[0]));
  EXPECT_EQ(1, query_url_result_.row.visit_count());
  ASSERT_EQ(1U, query_url_result_.visits.size());
  int64_t first_visit = query_url_result_.visits[0].visit_id;
  EXPECT_TRUE(ui::PageTransitionTypeIncludingQualifiersIs(
      query_url_result_.visits[0].transition,
      ui::PageTransitionFromInt(ui::PAGE_TRANSITION_LINK |
                                ui::PAGE_TRANSITION_CHAIN_START)));
  EXPECT_EQ(0, query_url_result_.visits[0].referring_visit);  // No referrer.

  // The second page should be a server redirect type with a referrer of the
  // first page.
  EXPECT_TRUE(QueryURL(first_redirects[1]));
  EXPECT_EQ(1, query_url_result_.row.visit_count());
  ASSERT_EQ(1U, query_url_result_.visits.size());
  int64_t second_visit = query_url_result_.visits[0].visit_id;
  EXPECT_TRUE(ui::PageTransitionTypeIncludingQualifiersIs(
      query_url_result_.visits[0].transition,
      ui::PageTransitionFromInt(ui::PAGE_TRANSITION_SERVER_REDIRECT |
                                ui::PAGE_TRANSITION_CHAIN_END)));
  EXPECT_EQ(first_visit, query_url_result_.visits[0].referring_visit);

  // Check that the redirect finding function successfully reports it.
  saved_redirects_.clear();
  QueryRedirectsFrom(first_redirects[0]);
  ASSERT_EQ(1U, saved_redirects_.size());
  EXPECT_EQ(first_redirects[1], saved_redirects_[0]);

  // Now add a client redirect from that second visit to a third, client
  // redirects are tracked by the RenderView prior to updating history,
  // so we pass in a CLIENT_REDIRECT qualifier to mock that behavior.
  history::RedirectList second_redirects = {first_redirects[1],
                                            GURL("http://last.page.com/")};
  history_service_->AddPage(
      second_redirects[1], base::Time::Now(), reinterpret_cast<ContextID>(1), 1,
      second_redirects[0], second_redirects,
      ui::PageTransitionFromInt(ui::PAGE_TRANSITION_LINK |
                                ui::PAGE_TRANSITION_CLIENT_REDIRECT),
      history::SOURCE_BROWSED, true, false);

  // The last page (source of the client redirect) should NOT have an
  // additional visit added, because it was a client redirect (normally it
  // would). We should only have 1 left over from the first sequence.
  EXPECT_TRUE(QueryURL(second_redirects[0]));
  EXPECT_EQ(1, query_url_result_.row.visit_count());

  // The final page should be set as a client redirect from the previous visit.
  EXPECT_TRUE(QueryURL(second_redirects[1]));
  EXPECT_EQ(1, query_url_result_.row.visit_count());
  ASSERT_EQ(1U, query_url_result_.visits.size());
  EXPECT_TRUE(ui::PageTransitionTypeIncludingQualifiersIs(
      query_url_result_.visits[0].transition,
      ui::PageTransitionFromInt(ui::PAGE_TRANSITION_CLIENT_REDIRECT |
                                ui::PAGE_TRANSITION_CHAIN_END)));
  EXPECT_EQ(second_visit, query_url_result_.visits[0].referring_visit);
}

TEST_F(HistoryServiceTest, MakeIntranetURLsTyped) {
  ASSERT_TRUE(history_service_.get());

  // Add a non-typed visit to an intranet URL on an unvisited host.  This should
  // get promoted to a typed visit.
  const GURL test_url("http://intranet_host/path");
  history_service_->AddPage(test_url, base::Time::Now(), nullptr, 0, GURL(),
                            history::RedirectList(), ui::PAGE_TRANSITION_LINK,
                            history::SOURCE_BROWSED, false, false);
  EXPECT_TRUE(QueryURL(test_url));
  EXPECT_EQ(1, query_url_result_.row.visit_count());
  EXPECT_EQ(1, query_url_result_.row.typed_count());
  ASSERT_EQ(1U, query_url_result_.visits.size());
  EXPECT_TRUE(ui::PageTransitionCoreTypeIs(
      query_url_result_.visits[0].transition, ui::PAGE_TRANSITION_TYPED));

  // Add more visits on the same host.  None of these should be promoted since
  // there is already a typed visit.

  // Different path.
  const GURL test_url2("http://intranet_host/different_path");
  history_service_->AddPage(test_url2, base::Time::Now(), nullptr, 0, GURL(),
                            history::RedirectList(), ui::PAGE_TRANSITION_LINK,
                            history::SOURCE_BROWSED, false, false);
  EXPECT_TRUE(QueryURL(test_url2));
  EXPECT_EQ(1, query_url_result_.row.visit_count());
  EXPECT_EQ(0, query_url_result_.row.typed_count());
  ASSERT_EQ(1U, query_url_result_.visits.size());
  EXPECT_TRUE(ui::PageTransitionCoreTypeIs(
      query_url_result_.visits[0].transition, ui::PAGE_TRANSITION_LINK));

  // No path.
  const GURL test_url3("http://intranet_host/");
  history_service_->AddPage(test_url3, base::Time::Now(), nullptr, 0, GURL(),
                            history::RedirectList(), ui::PAGE_TRANSITION_LINK,
                            history::SOURCE_BROWSED, false, false);
  EXPECT_TRUE(QueryURL(test_url3));
  EXPECT_EQ(1, query_url_result_.row.visit_count());
  EXPECT_EQ(0, query_url_result_.row.typed_count());
  ASSERT_EQ(1U, query_url_result_.visits.size());
  EXPECT_TRUE(ui::PageTransitionCoreTypeIs(
      query_url_result_.visits[0].transition, ui::PAGE_TRANSITION_LINK));

  // Different scheme.
  const GURL test_url4("https://intranet_host/");
  history_service_->AddPage(test_url4, base::Time::Now(), nullptr, 0, GURL(),
                            history::RedirectList(), ui::PAGE_TRANSITION_LINK,
                            history::SOURCE_BROWSED, false, false);
  EXPECT_TRUE(QueryURL(test_url4));
  EXPECT_EQ(1, query_url_result_.row.visit_count());
  EXPECT_EQ(0, query_url_result_.row.typed_count());
  ASSERT_EQ(1U, query_url_result_.visits.size());
  EXPECT_TRUE(ui::PageTransitionCoreTypeIs(
      query_url_result_.visits[0].transition, ui::PAGE_TRANSITION_LINK));

  // Different transition.
  const GURL test_url5("http://intranet_host/another_path");
  history_service_->AddPage(
      test_url5, base::Time::Now(), nullptr, 0, GURL(), history::RedirectList(),
      ui::PAGE_TRANSITION_AUTO_BOOKMARK, history::SOURCE_BROWSED, false, false);
  EXPECT_TRUE(QueryURL(test_url5));
  EXPECT_EQ(1, query_url_result_.row.visit_count());
  EXPECT_EQ(0, query_url_result_.row.typed_count());
  ASSERT_EQ(1U, query_url_result_.visits.size());
  EXPECT_TRUE(
      ui::PageTransitionCoreTypeIs(query_url_result_.visits[0].transition,
                                   ui::PAGE_TRANSITION_AUTO_BOOKMARK));

  // Original URL.
  history_service_->AddPage(test_url, base::Time::Now(), nullptr, 0, GURL(),
                            history::RedirectList(), ui::PAGE_TRANSITION_LINK,
                            history::SOURCE_BROWSED, false, false);
  EXPECT_TRUE(QueryURL(test_url));
  EXPECT_EQ(2, query_url_result_.row.visit_count());
  EXPECT_EQ(1, query_url_result_.row.typed_count());
  ASSERT_EQ(2U, query_url_result_.visits.size());
  EXPECT_TRUE(ui::PageTransitionCoreTypeIs(
      query_url_result_.visits[1].transition, ui::PAGE_TRANSITION_LINK));

  // A redirect chain with an intranet URL at the head should be promoted.
  history::RedirectList redirects1 = {GURL("http://intranet1/path"),
                                      GURL("http://second1.com/"),
                                      GURL("http://third1.com/")};
  history_service_->AddPage(redirects1.back(), base::Time::Now(), nullptr, 0,
                            GURL(), redirects1, ui::PAGE_TRANSITION_LINK,
                            history::SOURCE_BROWSED, false, false);
  EXPECT_TRUE(QueryURL(redirects1.front()));
  EXPECT_EQ(1, query_url_result_.row.visit_count());
  EXPECT_EQ(1, query_url_result_.row.typed_count());
  ASSERT_EQ(1U, query_url_result_.visits.size());
  EXPECT_TRUE(ui::PageTransitionCoreTypeIs(
      query_url_result_.visits[0].transition, ui::PAGE_TRANSITION_TYPED));

  // As should one with an intranet URL at the tail.
  history::RedirectList redirects2 = {GURL("http://first2.com/"),
                                      GURL("http://second2.com/"),
                                      GURL("http://intranet2/path")};
  history_service_->AddPage(redirects2.back(), base::Time::Now(), nullptr, 0,
                            GURL(), redirects2, ui::PAGE_TRANSITION_LINK,
                            history::SOURCE_BROWSED, false, false);
  EXPECT_TRUE(QueryURL(redirects2.back()));
  EXPECT_EQ(1, query_url_result_.row.visit_count());
  EXPECT_EQ(0, query_url_result_.row.typed_count());
  ASSERT_EQ(1U, query_url_result_.visits.size());
  EXPECT_TRUE(ui::PageTransitionCoreTypeIs(
      query_url_result_.visits[0].transition, ui::PAGE_TRANSITION_TYPED));

  // But not one with an intranet URL in the middle.
  history::RedirectList redirects3 = {GURL("http://first3.com/"),
                                      GURL("http://intranet3/path"),
                                      GURL("http://third3.com/")};
  history_service_->AddPage(redirects3.back(), base::Time::Now(), nullptr, 0,
                            GURL(), redirects3, ui::PAGE_TRANSITION_LINK,
                            history::SOURCE_BROWSED, false, false);
  EXPECT_TRUE(QueryURL(redirects3[1]));
  EXPECT_EQ(1, query_url_result_.row.visit_count());
  EXPECT_EQ(0, query_url_result_.row.typed_count());
  ASSERT_EQ(1U, query_url_result_.visits.size());
  EXPECT_TRUE(ui::PageTransitionCoreTypeIs(
      query_url_result_.visits[0].transition, ui::PAGE_TRANSITION_LINK));
}

TEST_F(HistoryServiceTest, Typed) {
  const ContextID context_id = reinterpret_cast<ContextID>(1);

  ASSERT_TRUE(history_service_.get());

  // Add the page once as typed.
  const GURL test_url("http://www.google.com/");
  history_service_->AddPage(test_url, base::Time::Now(), context_id, 0, GURL(),
                            history::RedirectList(), ui::PAGE_TRANSITION_TYPED,
                            history::SOURCE_BROWSED, false, false);
  EXPECT_TRUE(QueryURL(test_url));

  // We should have the same typed & visit count.
  EXPECT_EQ(1, query_url_result_.row.visit_count());
  EXPECT_EQ(1, query_url_result_.row.typed_count());

  // Add the page again not typed.
  history_service_->AddPage(test_url, base::Time::Now(), context_id, 0, GURL(),
                            history::RedirectList(), ui::PAGE_TRANSITION_LINK,
                            history::SOURCE_BROWSED, false, false);
  EXPECT_TRUE(QueryURL(test_url));

  // The second time should not have updated the typed count.
  EXPECT_EQ(2, query_url_result_.row.visit_count());
  EXPECT_EQ(1, query_url_result_.row.typed_count());

  // Add the page again as a generated URL.
  history_service_->AddPage(test_url, base::Time::Now(), context_id, 0, GURL(),
                            history::RedirectList(),
                            ui::PAGE_TRANSITION_GENERATED,
                            history::SOURCE_BROWSED, false, false);
  EXPECT_TRUE(QueryURL(test_url));

  // This should have worked like a link click.
  EXPECT_EQ(3, query_url_result_.row.visit_count());
  EXPECT_EQ(1, query_url_result_.row.typed_count());

  // Add the page again as a reload.
  history_service_->AddPage(test_url, base::Time::Now(), context_id, 0, GURL(),
                            history::RedirectList(), ui::PAGE_TRANSITION_RELOAD,
                            history::SOURCE_BROWSED, false, false);
  EXPECT_TRUE(QueryURL(test_url));

  // This should not have incremented any visit counts.
  EXPECT_EQ(3, query_url_result_.row.visit_count());
  EXPECT_EQ(1, query_url_result_.row.typed_count());
}

TEST_F(HistoryServiceTest, SetTitle) {
  ASSERT_TRUE(history_service_.get());

  // Add a URL.
  const GURL existing_url("http://www.google.com/");
  history_service_->AddPage(
      existing_url, base::Time::Now(), history::SOURCE_BROWSED);

  // Set some title.
  const std::u16string existing_title = u"Google";
  history_service_->SetPageTitle(existing_url, existing_title);

  // Make sure the title got set.
  EXPECT_TRUE(QueryURL(existing_url));
  EXPECT_EQ(existing_title, query_url_result_.row.title());

  // set a title on a nonexistent page
  const GURL nonexistent_url("http://news.google.com/");
  const std::u16string nonexistent_title = u"Google News";
  history_service_->SetPageTitle(nonexistent_url, nonexistent_title);

  // Make sure nothing got written.
  EXPECT_FALSE(QueryURL(nonexistent_url));
  EXPECT_EQ(std::u16string(), query_url_result_.row.title());

  // TODO(brettw) this should also test redirects, which get the title of the
  // destination page.
}

TEST_F(HistoryServiceTest, MostVisitedURLs) {
  ASSERT_TRUE(history_service_.get());

  const GURL url0("http://www.google.com/url0/");
  const GURL url1("http://www.google.com/url1/");
  const GURL url2("http://www.google.com/url2/");
  const GURL url3("http://www.google.com/url3/");
  const GURL url4("http://www.google.com/url4/");

  const ContextID context_id = reinterpret_cast<ContextID>(1);

  // Add two pages.
  history_service_->AddPage(url0, base::Time::Now(), context_id, 0, GURL(),
                            history::RedirectList(), ui::PAGE_TRANSITION_TYPED,
                            history::SOURCE_BROWSED, false, false);
  history_service_->AddPage(url1, base::Time::Now(), context_id, 0, GURL(),
                            history::RedirectList(), ui::PAGE_TRANSITION_TYPED,
                            history::SOURCE_BROWSED, false, false);

  QueryMostVisitedURLs();

  EXPECT_EQ(2U, most_visited_urls_.size());
  EXPECT_EQ(url0, most_visited_urls_[0].url);
  EXPECT_EQ(url1, most_visited_urls_[1].url);

  // Add another page.
  history_service_->AddPage(url2, base::Time::Now(), context_id, 0, GURL(),
                            history::RedirectList(), ui::PAGE_TRANSITION_TYPED,
                            history::SOURCE_BROWSED, false, false);

  QueryMostVisitedURLs();

  EXPECT_EQ(3U, most_visited_urls_.size());
  EXPECT_EQ(url0, most_visited_urls_[0].url);
  EXPECT_EQ(url1, most_visited_urls_[1].url);
  EXPECT_EQ(url2, most_visited_urls_[2].url);

  // Revisit url2, making it the top URL.
  history_service_->AddPage(url2, base::Time::Now(), context_id, 0, GURL(),
                            history::RedirectList(), ui::PAGE_TRANSITION_TYPED,
                            history::SOURCE_BROWSED, false, false);

  QueryMostVisitedURLs();

  EXPECT_EQ(3U, most_visited_urls_.size());
  EXPECT_EQ(url2, most_visited_urls_[0].url);
  EXPECT_EQ(url0, most_visited_urls_[1].url);
  EXPECT_EQ(url1, most_visited_urls_[2].url);

  // Revisit url1, making it the top URL.
  history_service_->AddPage(url1, base::Time::Now(), context_id, 0, GURL(),
                            history::RedirectList(), ui::PAGE_TRANSITION_TYPED,
                            history::SOURCE_BROWSED, false, false);

  QueryMostVisitedURLs();

  EXPECT_EQ(3U, most_visited_urls_.size());
  EXPECT_EQ(url1, most_visited_urls_[0].url);
  EXPECT_EQ(url2, most_visited_urls_[1].url);
  EXPECT_EQ(url0, most_visited_urls_[2].url);

  // Visit url4 using redirects.
  history::RedirectList redirects = {url3, url4};
  history_service_->AddPage(url4, base::Time::Now(), context_id, 0, GURL(),
                            redirects, ui::PAGE_TRANSITION_TYPED,
                            history::SOURCE_BROWSED, false, false);

  QueryMostVisitedURLs();

  EXPECT_EQ(4U, most_visited_urls_.size());
  EXPECT_EQ(url1, most_visited_urls_[0].url);
  EXPECT_EQ(url2, most_visited_urls_[1].url);
  EXPECT_EQ(url0, most_visited_urls_[2].url);
  EXPECT_EQ(url3, most_visited_urls_[3].url);
}

namespace {

// A HistoryDBTask implementation. Each time RunOnDBThread is invoked
// invoke_count is increment. When invoked kWantInvokeCount times, true is
// returned from RunOnDBThread which should stop RunOnDBThread from being
// invoked again. When DoneRunOnMainThread is invoked, done_invoked is set to
// true.
class HistoryDBTaskImpl : public HistoryDBTask {
 public:
  static const int kWantInvokeCount;

  HistoryDBTaskImpl(int* invoke_count, bool* done_invoked)
      : invoke_count_(invoke_count), done_invoked_(done_invoked) {}

  HistoryDBTaskImpl(const HistoryDBTaskImpl&) = delete;
  HistoryDBTaskImpl& operator=(const HistoryDBTaskImpl&) = delete;

  bool RunOnDBThread(HistoryBackend* backend, HistoryDatabase* db) override {
    return (++*invoke_count_ == kWantInvokeCount);
  }

  void DoneRunOnMainThread() override {
    *done_invoked_ = true;
    base::RunLoop::QuitCurrentWhenIdleDeprecated();
  }

  raw_ptr<int> invoke_count_;
  raw_ptr<bool> done_invoked_;

 private:
  ~HistoryDBTaskImpl() override = default;
};

// static
const int HistoryDBTaskImpl::kWantInvokeCount = 2;

}  // namespace

TEST_F(HistoryServiceTest, HistoryDBTask) {
  ASSERT_TRUE(history_service_.get());
  base::CancelableTaskTracker task_tracker;
  int invoke_count = 0;
  bool done_invoked = false;
  history_service_->ScheduleDBTask(
      FROM_HERE,
      std::unique_ptr<history::HistoryDBTask>(
          new HistoryDBTaskImpl(&invoke_count, &done_invoked)),
      &task_tracker);
  // Run the message loop. When HistoryDBTaskImpl::DoneRunOnMainThread runs,
  // it will stop the message loop. If the test hangs here, it means
  // DoneRunOnMainThread isn't being invoked correctly.
  base::RunLoop().Run();
  CleanupHistoryService();
  // WARNING: history has now been deleted.
  history_service_.reset();
  ASSERT_EQ(HistoryDBTaskImpl::kWantInvokeCount, invoke_count);
  ASSERT_TRUE(done_invoked);
}

TEST_F(HistoryServiceTest, HistoryDBTaskCanceled) {
  ASSERT_TRUE(history_service_.get());
  base::CancelableTaskTracker task_tracker;
  int invoke_count = 0;
  bool done_invoked = false;
  history_service_->ScheduleDBTask(
      FROM_HERE,
      std::unique_ptr<history::HistoryDBTask>(
          new HistoryDBTaskImpl(&invoke_count, &done_invoked)),
      &task_tracker);
  task_tracker.TryCancelAll();
  CleanupHistoryService();
  // WARNING: history has now been deleted.
  history_service_.reset();
  ASSERT_FALSE(done_invoked);
}

// Helper to add a page at specified point of time.
void AddPageAtTime(HistoryService* history,
                   const std::string& url_spec,
                   base::Time time_in_the_past) {
  const GURL url(url_spec);
  history->AddPage(url, time_in_the_past, nullptr, 0, GURL(),
                   history::RedirectList(), ui::PAGE_TRANSITION_LINK,
                   history::SOURCE_BROWSED, false, false);
}

void AddPageInThePast(HistoryService* history,
                      const std::string& url_spec,
                      int days_back) {
  base::Time time_in_the_past = base::Time::Now() - base::Days(days_back);
  AddPageAtTime(history, url_spec, time_in_the_past);
}

// Helper to add a page with specified days back in the past.
base::Time GetTimeInThePast(base::Time base_time,
                            int days_back,
                            int hours_since_midnight,
                            int minutes = 0,
                            int seconds = 0) {
  base::Time past_midnight = MidnightNDaysLater(base_time, -days_back);

  return past_midnight + base::Hours(hours_since_midnight) +
         base::Minutes(minutes) + base::Seconds(seconds);
}

// Helper to contain a callback and run loop logic.
int GetMonthlyHostCountHelper(HistoryService* history,
                              base::CancelableTaskTracker* tracker) {
  base::RunLoop run_loop;
  int count = 0;
  history->CountUniqueHostsVisitedLastMonth(
      base::BindLambdaForTesting([&](HistoryCountResult result) {
        count = result.count;
        run_loop.Quit();
      }),
      tracker);
  run_loop.Run();
  return count;
}

DomainDiversityResults GetDomainDiversityHelper(
    HistoryService* history,
    base::Time begin_time,
    base::Time end_time,
    DomainMetricBitmaskType metric_type_bitmask,
    base::CancelableTaskTracker* tracker) {
  base::RunLoop run_loop;
  base::TimeDelta dst_rounding_offset = base::Hours(4);

  // Compute the number of days to report metrics for.
  int number_of_days = 0;
  if (begin_time < end_time) {
    number_of_days = (end_time.LocalMidnight() - begin_time.LocalMidnight() +
                      dst_rounding_offset)
                         .InDaysFloored();
  }

  DomainDiversityResults results;
  history->GetDomainDiversity(
      end_time, number_of_days, metric_type_bitmask,
      base::BindLambdaForTesting([&](DomainDiversityResults result) {
        results = result;
        run_loop.Quit();
      }),
      tracker);
  run_loop.Run();
  return results;
}

// Test one domain visit metric. A negative value indicates that an invalid
// metric is expected.
void TestDomainMetric(const absl::optional<DomainMetricCountType>& metric,
                      int expected) {
  if (expected >= 0) {
    ASSERT_TRUE(metric.has_value());
    EXPECT_EQ(expected, metric.value().count);
  } else {
    EXPECT_FALSE(metric.has_value());
  }
}

// Test a set of 1-day, 7-day and 28-day domain visit metrics.
void TestDomainMetricSet(const DomainMetricSet& metric_set,
                         int expected_one_day_metric,
                         int expected_seven_day_metric,
                         int expected_twenty_eight_day_metric) {
  TestDomainMetric(metric_set.one_day_metric, expected_one_day_metric);
  TestDomainMetric(metric_set.seven_day_metric, expected_seven_day_metric);
  TestDomainMetric(metric_set.twenty_eight_day_metric,
                   expected_twenty_eight_day_metric);
}

// Counts hosts visited in the last month.
TEST_F(HistoryServiceTest, CountMonthlyVisitedHosts) {
  base::HistogramTester histogram_tester;
  HistoryService* history = history_service_.get();
  ASSERT_TRUE(history);

  AddPageInThePast(history, "http://www.google.com/", 0);
  EXPECT_EQ(1, GetMonthlyHostCountHelper(history, &tracker_));

  AddPageInThePast(history, "http://www.google.com/foo", 1);
  AddPageInThePast(history, "https://www.google.com/foo", 5);
  AddPageInThePast(history, "https://www.gmail.com/foo", 10);
  // Expect 2 because only host part of URL counts.
  EXPECT_EQ(2, GetMonthlyHostCountHelper(history, &tracker_));

  AddPageInThePast(history, "https://www.gmail.com/foo", 31);
  // Count should not change since URL added is older than a month.
  EXPECT_EQ(2, GetMonthlyHostCountHelper(history, &tracker_));

  AddPageInThePast(history, "https://www.yahoo.com/foo", 29);
  EXPECT_EQ(3, GetMonthlyHostCountHelper(history, &tracker_));

  // The time required to compute host count is reported on each computation.
  histogram_tester.ExpectTotalCount("History.DatabaseMonthlyHostCountTime", 4);
}

TEST_F(HistoryServiceTest, GetDomainDiversityShortBasetimeRange) {
  HistoryService* history = history_service_.get();
  ASSERT_TRUE(history);

  base::Time query_time = base::Time::Now();

  // Make sure `query_time` is at least some time past the midnight so that
  // some domain visits can be inserted between `query_time` and midnight
  // for testing.
  query_time =
      std::max(query_time.LocalMidnight() + base::Minutes(10), query_time);

  AddPageAtTime(history, "http://www.google.com/",
                GetTimeInThePast(query_time, /*days_back=*/2,
                                 /*hours_since_midnight=*/12));
  AddPageAtTime(history, "http://www.gmail.com/",
                GetTimeInThePast(query_time, 2, 13));
  AddPageAtTime(history, "http://www.gmail.com/foo",
                GetTimeInThePast(query_time, 2, 14));
  AddPageAtTime(history, "http://images.google.com/foo",
                GetTimeInThePast(query_time, 1, 7));

  // Domains visited on the query day will not be included in the result.
  AddPageAtTime(history, "http://www.youtube.com/", query_time.LocalMidnight());
  AddPageAtTime(history, "http://www.chromium.com/",
                query_time.LocalMidnight() + base::Minutes(5));
  AddPageAtTime(history, "http://www.youtube.com/", query_time);

  // IP addresses, empty strings, non-TLD's should not be counted
  // as domains.
  AddPageAtTime(history, "127.0.0.1", GetTimeInThePast(query_time, 1, 8));
  AddPageAtTime(history, "", GetTimeInThePast(query_time, 1, 13));
  AddPageAtTime(history, "http://localhost/",
                GetTimeInThePast(query_time, 1, 8));
  AddPageAtTime(history, "http://ak/", GetTimeInThePast(query_time, 1, 14));

  // Should return empty result if `begin_time` == `end_time`.
  DomainDiversityResults res = GetDomainDiversityHelper(
      history, query_time, query_time,
      history::kEnableLast1DayMetric | history::kEnableLast7DayMetric |
          history::kEnableLast28DayMetric,
      &tracker_);
  EXPECT_EQ(0u, res.size());

  // Metrics will be computed for each of the 4 continuous midnights.
  res = GetDomainDiversityHelper(
      history, GetTimeInThePast(query_time, 4, 0), query_time,
      history::kEnableLast1DayMetric | history::kEnableLast7DayMetric |
          history::kEnableLast28DayMetric,
      &tracker_);

  ASSERT_EQ(4u, res.size());

  TestDomainMetricSet(res[0], 1, 2, 2);
  TestDomainMetricSet(res[1], 2, 2, 2);
  TestDomainMetricSet(res[2], 0, 0, 0);
  TestDomainMetricSet(res[3], 0, 0, 0);
}

TEST_F(HistoryServiceTest, GetDomainDiversityLongBasetimeRange) {
  HistoryService* history = history_service_.get();
  ASSERT_TRUE(history);

  base::Time query_time = base::Time::Now();

  AddPageAtTime(history, "http://www.google.com/",
                GetTimeInThePast(query_time, /*days_back=*/90,
                                 /*hours_since_midnight=*/6));
  AddPageAtTime(history, "http://maps.google.com/",
                GetTimeInThePast(query_time, 34, 6));
  AddPageAtTime(history, "http://www.google.com/",
                GetTimeInThePast(query_time, 31, 4));
  AddPageAtTime(history, "https://www.google.co.uk/",
                GetTimeInThePast(query_time, 14, 5));
  AddPageAtTime(history, "http://www.gmail.com/",
                GetTimeInThePast(query_time, 10, 13));
  AddPageAtTime(history, "http://www.chromium.org/foo",
                GetTimeInThePast(query_time, 7, 14));
  AddPageAtTime(history, "https://www.youtube.com/",
                GetTimeInThePast(query_time, 2, 12));
  AddPageAtTime(history, "https://www.youtube.com/foo",
                GetTimeInThePast(query_time, 2, 12));
  AddPageAtTime(history, "https://www.chromium.org/",
                GetTimeInThePast(query_time, 1, 13));
  AddPageAtTime(history, "https://www.google.com/",
                GetTimeInThePast(query_time, 1, 13));

  DomainDiversityResults res = GetDomainDiversityHelper(
      history, GetTimeInThePast(query_time, 10, 12), query_time,
      history::kEnableLast1DayMetric | history::kEnableLast7DayMetric |
          history::kEnableLast28DayMetric,
      &tracker_);
  // Only up to seven days will be considered.
  ASSERT_EQ(7u, res.size());

  TestDomainMetricSet(res[0], 2, 3, 5);
  TestDomainMetricSet(res[1], 1, 2, 4);
  TestDomainMetricSet(res[2], 0, 1, 3);
  TestDomainMetricSet(res[3], 0, 2, 4);
  TestDomainMetricSet(res[4], 0, 2, 4);
  TestDomainMetricSet(res[5], 0, 2, 4);
  TestDomainMetricSet(res[6], 1, 2, 4);
}

TEST_F(HistoryServiceTest, GetDomainDiversityBitmaskTest) {
  HistoryService* history = history_service_.get();
  ASSERT_TRUE(history);

  base::Time query_time = base::Time::Now();

  AddPageAtTime(history, "http://www.google.com/",
                GetTimeInThePast(query_time, /*days_back=*/28,
                                 /*hours_since_midnight=*/6));
  AddPageAtTime(history, "http://www.youtube.com/",
                GetTimeInThePast(query_time, 7, 6));
  AddPageAtTime(history, "http://www.chromium.com/",
                GetTimeInThePast(query_time, 1, 4));

  DomainDiversityResults res = GetDomainDiversityHelper(
      history, GetTimeInThePast(query_time, 7, 12), query_time,
      history::kEnableLast1DayMetric | history::kEnableLast7DayMetric,
      &tracker_);
  ASSERT_EQ(7u, res.size());

  TestDomainMetricSet(res[0], 1, 2, -1);
  TestDomainMetricSet(res[1], 0, 1, -1);
  TestDomainMetricSet(res[2], 0, 1, -1);
  TestDomainMetricSet(res[3], 0, 1, -1);
  TestDomainMetricSet(res[4], 0, 1, -1);
  TestDomainMetricSet(res[5], 0, 1, -1);
  TestDomainMetricSet(res[6], 1, 1, -1);

  res = GetDomainDiversityHelper(
      history, GetTimeInThePast(query_time, 6, 12), query_time,
      history::kEnableLast28DayMetric | history::kEnableLast7DayMetric,
      &tracker_);

  ASSERT_EQ(6u, res.size());
  TestDomainMetricSet(res[0], -1, 2, 3);
  TestDomainMetricSet(res[1], -1, 1, 2);
  TestDomainMetricSet(res[2], -1, 1, 2);
  TestDomainMetricSet(res[3], -1, 1, 2);
  TestDomainMetricSet(res[4], -1, 1, 2);
  TestDomainMetricSet(res[5], -1, 1, 2);
}
}  // namespace history
