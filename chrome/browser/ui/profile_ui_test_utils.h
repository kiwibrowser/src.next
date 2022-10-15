// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_PROFILE_UI_TEST_UTILS_H_
#define CHROME_BROWSER_UI_PROFILE_UI_TEST_UTILS_H_

#include "base/files/file_path.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/ui/webui/signin/enterprise_profile_welcome_ui.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_CHROMEOS_LACROS)
#include "chrome/browser/ui/webui/signin/login_ui_service.h"
#endif

class EnterpriseProfileWelcomeHandler;

// This file contains helper functions for testing profile UIs, in particular,
// the profile picker.
namespace profiles::testing {

// Wait until the widget of the picker gets created and the initialization of
// the picker is thus finished (and notably `ProfilePicker::GetViewForTesting()`
// is not null).
void WaitForPickerWidgetCreated();

// Waits until the profile picker's current `WebContents` stops loading `url`.
// This also works if the profile picker's current `WebContents` changes
// throughout the waiting as it is
// technically observing all web contents.
void WaitForPickerLoadStop(const GURL& url);

// Waits until the picker gets closed.
void WaitForPickerClosed();

// Checks that the profile picker is currently displaying a welcome screen of
// type `expected_type` and returns the handler associated with it.
EnterpriseProfileWelcomeHandler* ExpectPickerWelcomeScreenType(
    EnterpriseProfileWelcomeUI::ScreenType expected_type);

// Checks that the profile picker is currently displaying a welcome screen of
// type `expected_type` and performs the user action represented by `choice` on
// that screen.
void ExpectPickerWelcomeScreenTypeAndProceed(
    EnterpriseProfileWelcomeUI::ScreenType expected_type,
    signin::SigninChoice choice);

#if BUILDFLAG(IS_CHROMEOS_LACROS)
void CompleteLacrosFirstRun(
    LoginUIService::SyncConfirmationUIClosedResult result);
#endif

}  // namespace profiles::testing

#endif  // CHROME_BROWSER_UI_PROFILE_UI_TEST_UTILS_H_
