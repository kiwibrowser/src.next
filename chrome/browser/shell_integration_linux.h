// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SHELL_INTEGRATION_LINUX_H_
#define CHROME_BROWSER_SHELL_INTEGRATION_LINUX_H_

#include <string>

#include "base/files/file_path.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/web_applications/web_app_id.h"
#include "chrome/common/buildflags.h"
#include "components/services/app_service/public/cpp/file_handler.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_CHROMEOS_ASH)
#error shell_integration_linux is for desktop linux only.
#endif

namespace base {
class CommandLine;
class Environment;
}

namespace web_app {
struct DesktopActionInfo;
}

namespace shell_integration_linux {

// Get the path to write user-specific application data files to, as specified
// in the XDG Base Directory Specification:
// http://standards.freedesktop.org/basedir-spec/latest/
base::FilePath GetDataWriteLocation(base::Environment* env);

// Get the list of paths to search for application data files, in order of
// preference, as specified in the XDG Base Directory Specification:
// http://standards.freedesktop.org/basedir-spec/latest/
// Called on the FILE thread.
std::vector<base::FilePath> GetDataSearchLocations(base::Environment* env);

// Gets the name for use as the res_name of the window's WM_CLASS property.
std::string GetProgramClassName();

// Gets the name for use as the res_class of the window's WM_CLASS property.
std::string GetProgramClassClass();

// Returns name of the browser icon (without a path or file extension).
std::string GetIconName();

// Returns the contents of an existing .desktop file installed in the system.
// Searches the "applications" subdirectory of each XDG data directory for a
// file named |desktop_filename|. If the file is found, populates |output| with
// its contents and returns true. Else, returns false.
bool GetExistingShortcutContents(base::Environment* env,
                                 const base::FilePath& desktop_filename,
                                 std::string* output);

// Returns filename for .desktop file based on |url|, sanitized for security.
base::FilePath GetWebShortcutFilename(const GURL& url);

// Returns a list of filenames for all existing .desktop files corresponding to
// on |profile_path| in a given |directory|.
std::vector<base::FilePath> GetExistingProfileShortcutFilenames(
    const base::FilePath& profile_path,
    const base::FilePath& directory);

// Returns contents for .desktop file based on |url| and |title|. If
// |no_display| is true, the shortcut will not be visible to the user in menus.
std::string GetDesktopFileContents(
    const base::FilePath& chrome_exe_path,
    const std::string& app_name,
    const GURL& url,
    const std::string& extension_id,
    const std::u16string& title,
    const std::string& icon_name,
    const base::FilePath& profile_path,
    const std::string& categories,
    const std::string& mime_type,
    bool no_display,
    const std::string& run_on_os_login_mode,
    std::set<web_app::DesktopActionInfo> action_info);

// Returns contents for .desktop file that executes command_line. This is a more
// general form of GetDesktopFileContents. If |no_display| is true, the shortcut
// will not be visible to the user in menus.
std::string GetDesktopFileContentsForCommand(
    const base::CommandLine& command_line,
    const std::string& app_name,
    const GURL& url,
    const std::u16string& title,
    const std::string& icon_name,
    const std::string& categories,
    const std::string& mime_type,
    bool no_display,
    std::set<web_app::DesktopActionInfo> action_info);

// Returns contents for .directory file named |title| with icon |icon_name|. If
// |icon_name| is empty, will use the Chrome icon.
std::string GetDirectoryFileContents(const std::u16string& title,
                                     const std::string& icon_name);

// Returns the filename for a .xml file, corresponding to a given |app_id|,
// which is passed to `xdg-mime` to register one or more custom MIME types in
// Linux.
base::FilePath GetMimeTypesRegistrationFilename(
    const base::FilePath& profile_path,
    const web_app::AppId& app_id);

// Returns the contents of a .xml file as specified by |file_handlers|, which is
// passed to `xdg-mime` to register one or more custom MIME types in Linux.
std::string GetMimeTypesRegistrationFileContents(
    const apps::FileHandlers& file_handlers);

// Windows that correspond to web apps need to have a deterministic (and
// different) WMClass than normal chrome windows so the window manager groups
// them as a separate application.
std::string GetWMClassFromAppName(std::string app_name);

// Wayland version of GetWMClassFromAppName explained above.
// The XDG application ID must match the name of the desktop entry file, where
// the latter looks like 'chrome-<web app id>-<profile name>.desktop'.
std::string GetXdgAppIdForWebApp(std::string app_name,
                                 const base::FilePath& profile_path);

// Helper to launch xdg scripts. We don't want them to ask any questions on the
// terminal etc. The function returns true if the utility launches and exits
// cleanly, in which case |exit_code| returns the utility's exit code.
// thread_restrictions.h assumes it to be in shell_integration namespace.
bool LaunchXdgUtility(const std::vector<std::string>& argv, int* exit_code);

namespace internal {

// Exposed for testing.  Clients should use the corresponding functions in
// shell_integration_linux instead.
std::string GetProgramClassName(const base::CommandLine& command_line,
                                const std::string& desktop_file_name);
std::string GetProgramClassClass(const base::CommandLine& command_line,
                                 const std::string& desktop_file_name);

// Get the value of NoDisplay from the [Desktop Entry] section of a .desktop
// file, given in |shortcut_contents|. If the key is not found, returns false.
bool GetNoDisplayFromDesktopFile(const std::string& shortcut_contents);

// Gets the path to the Chrome executable or wrapper script.
// Returns an empty path if the executable path could not be found, which should
// never happen.
base::FilePath GetChromeExePath();

}  // namespace internal

}  // namespace shell_integration_linux

#endif  // CHROME_BROWSER_SHELL_INTEGRATION_LINUX_H_
