// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_MANAGED_UI_H_
#define CHROME_BROWSER_UI_MANAGED_UI_H_

#include <string>

#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

class Profile;

namespace chrome {

// Returns true if a 'Managed by your organization' message should appear in
// Chrome's App Menu, and on the following chrome:// pages:
// - chrome://bookmarks
// - chrome://downloads
// - chrome://extensions
// - chrome://history
// - chrome://settings
//
// N.B.: This is independent of Chrome OS's system tray message for enterprise
// users.
bool ShouldDisplayManagedUi(Profile* profile);

#if !BUILDFLAG(IS_ANDROID)
// The label for the App Menu item for Managed UI.
std::u16string GetManagedUiMenuItemLabel(Profile* profile);

// The label for the WebUI footnote for Managed UI indicating that the browser
// is managed. These strings contain HTML for an <a> element.
std::u16string GetManagedUiWebUILabel(Profile* profile);
#endif  // !BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_CHROMEOS_ASH)
// The label for the WebUI footnote for Managed UI indicating that the device
// is mananged. These strings contain HTML for an <a> element.
std::u16string GetDeviceManagedUiWebUILabel();
#endif

// Returns nullopt if the device is not managed, the UTF8-encoded string
// representation of the manager identity if available and an empty string if
// the device is managed but the manager is not known.
absl::optional<std::string> GetDeviceManagerIdentity();

#if BUILDFLAG(IS_CHROMEOS_LACROS)
// Returns the UTF8-encoded string representation of the the entity that manages
// the current session or nullopt if unmanaged. Returns the same result as
// `GetAccountManagerIdentity(primary_profile)` where `primary_profile` is the
// initial profile in the session. This concept only makes sense on lacros where
//  - session manager can be different from account manager for a profile in
//    this session, and also
//  - session manager can be different from device manager.
absl::optional<std::string> GetSessionManagerIdentity();
#endif

// Returns the UTF8-encoded string representation of the the entity that manages
// `profile` or nullopt if unmanaged. For standard dasher domains, this will be
// a domain name (ie foo.com). For FlexOrgs, this will be the email address of
// the admin of the FlexOrg (ie user@foo.com). If DMServer does not provide this
// information, this function defaults to the domain of the account.
// TODO(crbug.com/1081272): Refactor localization hints for all strings that
// depend on this function.
absl::optional<std::string> GetAccountManagerIdentity(Profile* profile);

}  // namespace chrome

#endif  // CHROME_BROWSER_UI_MANAGED_UI_H_
