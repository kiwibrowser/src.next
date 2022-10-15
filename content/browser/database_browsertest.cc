// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/public/test/test_utils.h"
#include "content/shell/browser/shell.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace content {

// TODO(crbug.com/1317431): WebSQL does not work on Fuchsia.
#if BUILDFLAG(IS_FUCHSIA)
#define MAYBE_DatabaseTest DISABLED_DatabaseTest
#else
#define MAYBE_DatabaseTest DatabaseTest
#endif
class MAYBE_DatabaseTest : public ContentBrowserTest {
 public:
  MAYBE_DatabaseTest() {}

  void RunScriptAndCheckResult(Shell* shell,
                               const std::string& script,
                               const std::string& result) {
    ASSERT_EQ(result, EvalJs(shell->web_contents(), script,
                             EXECUTE_SCRIPT_USE_MANUAL_REPLY));
  }

  void Navigate(Shell* shell) {
    EXPECT_TRUE(NavigateToURL(shell, GetTestUrl("", "simple_database.html")));
  }

  void CreateTable(Shell* shell) {
    RunScriptAndCheckResult(shell, "createTable()", "done");
  }

  void InsertRecord(Shell* shell, const std::string& data) {
    RunScriptAndCheckResult(shell, "insertRecord('" + data + "')", "done");
  }

  void UpdateRecord(Shell* shell, int index, const std::string& data) {
    RunScriptAndCheckResult(
        shell,
        "updateRecord(" + base::NumberToString(index) + ", '" + data + "')",
        "done");
  }

  void DeleteRecord(Shell* shell, int index) {
    RunScriptAndCheckResult(
        shell, "deleteRecord(" + base::NumberToString(index) + ")", "done");
  }

  void CompareRecords(Shell* shell, const std::string& expected) {
    RunScriptAndCheckResult(shell, "getRecords()", expected);
  }

  bool HasTable(Shell* shell) {
    std::string data =
        EvalJs(shell, "getRecords()", EXECUTE_SCRIPT_USE_MANUAL_REPLY)
            .ExtractString();
    return data != "getRecords error: [object SQLError]";
  }
};

// Insert records to the database.
IN_PROC_BROWSER_TEST_F(MAYBE_DatabaseTest, InsertRecord) {
  Navigate(shell());
  CreateTable(shell());
  InsertRecord(shell(), "text");
  CompareRecords(shell(), "text");
  InsertRecord(shell(), "text2");
  CompareRecords(shell(), "text, text2");
}

// Update records in the database.
IN_PROC_BROWSER_TEST_F(MAYBE_DatabaseTest, UpdateRecord) {
  Navigate(shell());
  CreateTable(shell());
  InsertRecord(shell(), "text");
  UpdateRecord(shell(), 0, "0");
  CompareRecords(shell(), "0");

  InsertRecord(shell(), "1");
  InsertRecord(shell(), "2");
  UpdateRecord(shell(), 1, "1000");
  CompareRecords(shell(), "0, 1000, 2");
}

// Delete records in the database.
IN_PROC_BROWSER_TEST_F(MAYBE_DatabaseTest, DeleteRecord) {
  Navigate(shell());
  CreateTable(shell());
  InsertRecord(shell(), "text");
  DeleteRecord(shell(), 0);
  CompareRecords(shell(), std::string());

  InsertRecord(shell(), "0");
  InsertRecord(shell(), "1");
  InsertRecord(shell(), "2");
  DeleteRecord(shell(), 1);
  CompareRecords(shell(), "0, 2");
}

// Attempts to delete a nonexistent row in the table.
IN_PROC_BROWSER_TEST_F(MAYBE_DatabaseTest, DeleteNonexistentRow) {
  Navigate(shell());
  CreateTable(shell());
  InsertRecord(shell(), "text");

  RunScriptAndCheckResult(
      shell(), "deleteRecord(1)", "could not find row with index: 1");

  CompareRecords(shell(), "text");
}

// Insert, update, and delete records in the database.
IN_PROC_BROWSER_TEST_F(MAYBE_DatabaseTest, DatabaseOperations) {
  Navigate(shell());
  CreateTable(shell());

  std::string expected;
  for (int i = 0; i < 10; ++i) {
    std::string item = base::NumberToString(i);
    InsertRecord(shell(), item);
    if (!expected.empty())
      expected += ", ";
    expected += item;
  }
  CompareRecords(shell(), expected);

  expected.clear();
  for (int i = 0; i < 10; ++i) {
    std::string item = base::NumberToString(i * i);
    UpdateRecord(shell(), i, item);
    if (!expected.empty())
      expected += ", ";
    expected += item;
  }
  CompareRecords(shell(), expected);

  for (int i = 0; i < 10; ++i)
    DeleteRecord(shell(), 0);

  CompareRecords(shell(), std::string());

  RunScriptAndCheckResult(
      shell(), "deleteRecord(1)", "could not find row with index: 1");

  CompareRecords(shell(), std::string());
}

// Create records in the database and verify they persist after reload.
IN_PROC_BROWSER_TEST_F(MAYBE_DatabaseTest, ReloadPage) {
  Navigate(shell());
  CreateTable(shell());
  InsertRecord(shell(), "text");

  LoadStopObserver load_stop_observer(shell()->web_contents());
  shell()->Reload();
  load_stop_observer.Wait();

  CompareRecords(shell(), "text");
}

// Attempt to read a database created in a regular browser from an off the
// record browser.
IN_PROC_BROWSER_TEST_F(MAYBE_DatabaseTest,
                       OffTheRecordCannotReadRegularDatabase) {
  Navigate(shell());
  CreateTable(shell());
  InsertRecord(shell(), "text");

  Shell* otr = CreateOffTheRecordBrowser();
  Navigate(otr);
  ASSERT_FALSE(HasTable(otr));

  CreateTable(otr);
  CompareRecords(otr, std::string());
}

// Attempt to read a database created in an off the record browser from a
// regular browser.
IN_PROC_BROWSER_TEST_F(MAYBE_DatabaseTest,
                       RegularCannotReadOffTheRecordDatabase) {
  Shell* otr = CreateOffTheRecordBrowser();
  Navigate(otr);
  CreateTable(otr);
  InsertRecord(otr, "text");

  Navigate(shell());
  ASSERT_FALSE(HasTable(shell()));
  CreateTable(shell());
  CompareRecords(shell(), std::string());
}

// Verify DB changes within first window are present in the second window.
IN_PROC_BROWSER_TEST_F(MAYBE_DatabaseTest, ModificationPersistInSecondTab) {
  Navigate(shell());
  CreateTable(shell());
  InsertRecord(shell(), "text");

  Shell* shell2 = CreateBrowser();
  Navigate(shell2);
  UpdateRecord(shell2, 0, "0");

  CompareRecords(shell(), "0");
  CompareRecords(shell2, "0");
}

// Verify database modifications persist after restarting browser.
IN_PROC_BROWSER_TEST_F(MAYBE_DatabaseTest, PRE_DatabasePersistsAfterRelaunch) {
  Navigate(shell());
  CreateTable(shell());
  InsertRecord(shell(), "text");
}

IN_PROC_BROWSER_TEST_F(MAYBE_DatabaseTest, DatabasePersistsAfterRelaunch) {
  Navigate(shell());
  CompareRecords(shell(), "text");
}

// Verify OTR database is removed after OTR window closes.
IN_PROC_BROWSER_TEST_F(MAYBE_DatabaseTest,
                       PRE_OffTheRecordDatabaseNotPersistent) {
  Shell* otr = CreateOffTheRecordBrowser();
  Navigate(otr);
  CreateTable(otr);
  InsertRecord(otr, "text");
}

IN_PROC_BROWSER_TEST_F(MAYBE_DatabaseTest, OffTheRecordDatabaseNotPersistent) {
  Shell* otr = CreateOffTheRecordBrowser();
  Navigate(otr);
  ASSERT_FALSE(HasTable(otr));
}

// Verify database modifications persist after crashing window.
IN_PROC_BROWSER_TEST_F(MAYBE_DatabaseTest,
                       ModificationsPersistAfterRendererCrash) {
  Navigate(shell());
  CreateTable(shell());
  InsertRecord(shell(), "1");

  CrashTab(shell()->web_contents());
  Navigate(shell());
  CompareRecords(shell(), "1");
}

// Test to check if database modifications are persistent across windows in
// off the record window.
IN_PROC_BROWSER_TEST_F(MAYBE_DatabaseTest,
                       OffTheRecordDBPersistentAcrossWindows) {
  Shell* otr1 = CreateOffTheRecordBrowser();
  Navigate(otr1);
  CreateTable(otr1);
  InsertRecord(otr1, "text");

  Shell* otr2 = CreateOffTheRecordBrowser();
  Navigate(otr2);
  CompareRecords(otr2, "text");
}

}  // namespace content
