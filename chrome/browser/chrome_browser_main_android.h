// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROME_BROWSER_MAIN_ANDROID_H_
#define CHROME_BROWSER_CHROME_BROWSER_MAIN_ANDROID_H_

#include <memory>

#include "chrome/browser/chrome_browser_main.h"

namespace android {
class ChromeBackupWatcher;
}

namespace crash_reporter {
class ChildExitObserver;
}

class ProfileManagerAndroid;

class ChromeBrowserMainPartsAndroid : public ChromeBrowserMainParts {
 public:
  ChromeBrowserMainPartsAndroid(bool is_integration_test,
                                StartupData* startup_data);

  ChromeBrowserMainPartsAndroid(const ChromeBrowserMainPartsAndroid&) = delete;
  ChromeBrowserMainPartsAndroid& operator=(
      const ChromeBrowserMainPartsAndroid&) = delete;

  ~ChromeBrowserMainPartsAndroid() override;

  // content::BrowserMainParts overrides.
  int PreCreateThreads() override;
  void PostProfileInit(Profile* profile, bool is_initial_profile) override;
  int PreEarlyInitialization() override;
  void PostEarlyInitialization() override;

  // ChromeBrowserMainParts overrides.
  void PostBrowserStart() override;
  void ShowMissingLocaleMessageBox() override;

 private:
  std::unique_ptr<crash_reporter::ChildExitObserver> child_exit_observer_;
  std::unique_ptr<android::ChromeBackupWatcher> backup_watcher_;
  std::unique_ptr<ProfileManagerAndroid> profile_manager_android_;
};

#endif  // CHROME_BROWSER_CHROME_BROWSER_MAIN_ANDROID_H_
