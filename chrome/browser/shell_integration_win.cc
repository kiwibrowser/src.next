// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/shell_integration_win.h"

#include <shobjidl.h>
#include <windows.h>

#include <objbase.h>
#include <propkey.h>  // Needs to come after shobjidl.h.
#include <stddef.h>
#include <stdint.h>
#include <wrl/client.h>

#include <memory>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/callback.h"
#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/memory/weak_ptr.h"
#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"
#include "base/metrics/user_metrics.h"
#include "base/metrics/user_metrics_action.h"
#include "base/path_service.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "base/threading/platform_thread.h"
#include "base/threading/scoped_blocking_call.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/win/registry.h"
#include "base/win/scoped_propvariant.h"
#include "base/win/shlwapi.h"
#include "base/win/shortcut.h"
#include "base/win/windows_version.h"
#include "chrome/browser/policy/policy_path_parser.h"
#include "chrome/browser/shell_integration.h"
#include "chrome/browser/web_applications/os_integration/web_app_shortcut_win.h"
#include "chrome/browser/web_applications/web_app_helpers.h"
#include "chrome/browser/win/settings_app_monitor.h"
#include "chrome/browser/win/util_win_service.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_paths_internal.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/install_static/install_util.h"
#include "chrome/installer/util/install_util.h"
#include "chrome/installer/util/scoped_user_protocol_entry.h"
#include "chrome/installer/util/shell_util.h"
#include "chrome/services/util_win/public/mojom/util_win.mojom.h"
#include "components/variations/variations_associated_data.h"
#include "mojo/public/cpp/bindings/remote.h"

namespace shell_integration {

namespace {

const base::Feature kWin10UnattendedDefault{"Win10UnattendedDefault",
                                            base::FEATURE_DISABLED_BY_DEFAULT};

bool CanSetAsDefaultDirectly() {
  return base::win::GetVersion() >= base::win::Version::WIN10 &&
         base::FeatureList::IsEnabled(kWin10UnattendedDefault);
}

// Helper function for GetAppId to generates profile id
// from profile path. "profile_id" is composed of sanitized basenames of
// user data dir and profile dir joined by a ".".
std::wstring GetProfileIdFromPath(const base::FilePath& profile_path) {
  // Return empty string if profile_path is empty
  if (profile_path.empty())
    return std::wstring();

  base::FilePath default_user_data_dir;
  // Return empty string if profile_path is in default user data
  // dir and is the default profile.
  if (chrome::GetDefaultUserDataDirectory(&default_user_data_dir) &&
      profile_path.DirName() == default_user_data_dir &&
      profile_path.BaseName().value() ==
          base::ASCIIToWide(chrome::kInitialProfile)) {
    return std::wstring();
  }

  // Get joined basenames of user data dir and profile.
  std::wstring basenames = profile_path.DirName().BaseName().value() + L"." +
                           profile_path.BaseName().value();

  std::wstring profile_id;
  profile_id.reserve(basenames.size());

  // Generate profile_id from sanitized basenames.
  for (size_t i = 0; i < basenames.length(); ++i) {
    if (base::IsAsciiAlpha(basenames[i]) ||
        base::IsAsciiDigit(basenames[i]) ||
        basenames[i] == L'.')
      profile_id += basenames[i];
  }

  return profile_id;
}

std::wstring GetAppUserModelIdImpl(const std::wstring& prefix,
                                   const std::wstring& app_name,
                                   const base::FilePath& profile_path) {
  std::vector<std::wstring> components;
  if (!prefix.empty())
    components.push_back(prefix);
  components.push_back(app_name);
  const std::wstring profile_id(GetProfileIdFromPath(profile_path));
  if (!profile_id.empty())
    components.push_back(profile_id);
  return ShellUtil::BuildAppUserModelId(components);
}

// Gets expected app id for given Chrome (based on |command_line| and
// |is_per_user_install|).
std::wstring GetExpectedAppId(const base::CommandLine& command_line,
                              bool is_per_user_install) {
  base::FilePath user_data_dir;
  if (command_line.HasSwitch(switches::kUserDataDir))
    user_data_dir = command_line.GetSwitchValuePath(switches::kUserDataDir);
  // Adjust with any policy that overrides any other way to set the path.
  policy::path_parser::CheckUserDataDirPolicy(&user_data_dir);
  if (user_data_dir.empty())
    chrome::GetDefaultUserDataDirectory(&user_data_dir);
  DCHECK(!user_data_dir.empty());

  base::FilePath profile_subdir;
  if (command_line.HasSwitch(switches::kProfileDirectory)) {
    profile_subdir =
        command_line.GetSwitchValuePath(switches::kProfileDirectory);
  } else {
    profile_subdir = base::FilePath(base::ASCIIToWide(chrome::kInitialProfile));
  }
  DCHECK(!profile_subdir.empty());

  base::FilePath profile_path = user_data_dir.Append(profile_subdir);
  std::wstring prefix;
  std::wstring app_name;
  if (command_line.HasSwitch(switches::kApp)) {
    app_name = base::UTF8ToWide(web_app::GenerateApplicationNameFromURL(
        GURL(command_line.GetSwitchValueASCII(switches::kApp))));
    prefix = install_static::GetBaseAppId();
  } else if (command_line.HasSwitch(switches::kAppId)) {
    app_name = base::UTF8ToWide(web_app::GenerateApplicationNameFromAppId(
        command_line.GetSwitchValueASCII(switches::kAppId)));
    prefix = install_static::GetBaseAppId();
  } else {
    app_name = ShellUtil::GetBrowserModelId(is_per_user_install);
  }
  DCHECK(!app_name.empty());

  return GetAppUserModelIdImpl(prefix, app_name, profile_path);
}

// Windows treats a given scheme as an Internet scheme only if its registry
// entry has a "URL Protocol" key. Check this, otherwise we allow ProgIDs to be
// used as custom protocols which leads to security bugs.
bool IsValidCustomProtocol(const std::wstring& scheme) {
  if (scheme.empty())
    return false;
  base::win::RegKey cmd_key(HKEY_CLASSES_ROOT, scheme.c_str(), KEY_QUERY_VALUE);
  return cmd_key.Valid() && cmd_key.HasValue(L"URL Protocol");
}

// Windows 8 introduced a new protocol->executable binding system which cannot
// be retrieved in the HKCR registry subkey method implemented below. We call
// AssocQueryString with the new Win8-only flag ASSOCF_IS_PROTOCOL instead.
std::u16string GetAppForProtocolUsingAssocQuery(const GURL& url) {
  const std::wstring url_scheme = base::ASCIIToWide(url.scheme());
  if (!IsValidCustomProtocol(url_scheme))
    return std::u16string();

  // Query AssocQueryString for a human-readable description of the program
  // that will be invoked given the provided URL spec. This is used only to
  // populate the external protocol dialog box the user sees when invoking
  // an unknown external protocol.
  wchar_t out_buffer[1024];
  DWORD buffer_size = std::size(out_buffer);
  HRESULT hr =
      AssocQueryString(ASSOCF_IS_PROTOCOL, ASSOCSTR_FRIENDLYAPPNAME,
                       url_scheme.c_str(), NULL, out_buffer, &buffer_size);
  if (FAILED(hr)) {
    DLOG(WARNING) << "AssocQueryString failed!";
    return std::u16string();
  }
  return base::AsString16(std::wstring(out_buffer));
}

std::u16string GetAppForProtocolUsingRegistry(const GURL& url) {
  const std::wstring url_scheme = base::ASCIIToWide(url.scheme());
  if (!IsValidCustomProtocol(url_scheme))
    return std::u16string();

  // First, try and extract the application's display name.
  std::wstring command_to_launch;
  base::win::RegKey cmd_key_name(HKEY_CLASSES_ROOT, url_scheme.c_str(),
                                 KEY_READ);
  if (cmd_key_name.ReadValue(NULL, &command_to_launch) == ERROR_SUCCESS &&
      !command_to_launch.empty()) {
    return base::AsString16(command_to_launch);
  }

  // Otherwise, parse the command line in the registry, and return the basename
  // of the program path if it exists.
  const std::wstring cmd_key_path = url_scheme + L"\\shell\\open\\command";
  base::win::RegKey cmd_key_exe(HKEY_CLASSES_ROOT, cmd_key_path.c_str(),
                                KEY_READ);
  if (cmd_key_exe.ReadValue(NULL, &command_to_launch) == ERROR_SUCCESS) {
    base::CommandLine command_line(
        base::CommandLine::FromString(command_to_launch));
    return command_line.GetProgram().BaseName().AsUTF16Unsafe();
  }

  return std::u16string();
}

DefaultWebClientState GetDefaultWebClientStateFromShellUtilDefaultState(
    ShellUtil::DefaultState default_state) {
  switch (default_state) {
    case ShellUtil::UNKNOWN_DEFAULT:
      return DefaultWebClientState::UNKNOWN_DEFAULT;
    case ShellUtil::NOT_DEFAULT:
      return DefaultWebClientState::NOT_DEFAULT;
    case ShellUtil::IS_DEFAULT:
      return DefaultWebClientState::IS_DEFAULT;
    case ShellUtil::OTHER_MODE_IS_DEFAULT:
      return DefaultWebClientState::OTHER_MODE_IS_DEFAULT;
  }
  NOTREACHED();
  return DefaultWebClientState::UNKNOWN_DEFAULT;
}

// A recorder of user actions in the Windows Settings app.
class DefaultBrowserActionRecorder : public SettingsAppMonitor::Delegate {
 public:
  // Creates the recorder and the monitor that drives it. |continuation| will be
  // run once the monitor's initialization completes (regardless of success or
  // failure).
  explicit DefaultBrowserActionRecorder(base::OnceClosure continuation)
      : continuation_(std::move(continuation)), settings_app_monitor_(this) {}

  DefaultBrowserActionRecorder(const DefaultBrowserActionRecorder&) = delete;
  DefaultBrowserActionRecorder& operator=(const DefaultBrowserActionRecorder&) =
      delete;

 private:
  // win::SettingsAppMonitor::Delegate:
  void OnInitialized(HRESULT result) override {
    // UMA indicates that this succeeds > 99.98% of the time.
    if (SUCCEEDED(result)) {
      base::RecordAction(
          base::UserMetricsAction("SettingsAppMonitor.Initialized"));
    }
    std::move(continuation_).Run();
  }

  void OnAppFocused() override {
    base::RecordAction(
        base::UserMetricsAction("SettingsAppMonitor.AppFocused"));
  }

  void OnChooserInvoked() override {
    base::RecordAction(
        base::UserMetricsAction("SettingsAppMonitor.ChooserInvoked"));
  }

  void OnBrowserChosen(const std::wstring& browser_name) override {
    if (browser_name == InstallUtil::GetDisplayName()) {
      base::RecordAction(
          base::UserMetricsAction("SettingsAppMonitor.ChromeBrowserChosen"));
    } else {
      base::RecordAction(
          base::UserMetricsAction("SettingsAppMonitor.OtherBrowserChosen"));
    }
  }

  void OnPromoFocused() override {
    base::RecordAction(
        base::UserMetricsAction("SettingsAppMonitor.PromoFocused"));
  }

  void OnPromoChoiceMade(bool accept_promo) override {
    if (accept_promo) {
      base::RecordAction(
          base::UserMetricsAction("SettingsAppMonitor.CheckItOut"));
    } else {
      base::RecordAction(
          base::UserMetricsAction("SettingsAppMonitor.SwitchAnyway"));
    }
  }

  // A closure to be run once initialization completes.
  base::OnceClosure continuation_;

  // Monitors user interaction with the Windows Settings app for the sake of
  // reporting user actions.
  SettingsAppMonitor settings_app_monitor_;
};

// A function bound up in a callback with a DefaultBrowserActionRecorder and
// a closure to keep the former alive until the time comes to run the latter.
void OnSettingsAppFinished(
    std::unique_ptr<DefaultBrowserActionRecorder> recorder,
    base::OnceClosure on_finished_callback) {
  recorder.reset();
  std::move(on_finished_callback).Run();
}

// There is no way to make sure the user is done with the system settings, but a
// signal that the interaction is finished is needed for UMA. A timer of 2
// minutes is used as a substitute. The registry keys for the protocol
// association with an app are also monitored to signal the end of the
// interaction early when it is clear that the user made a choice (e.g. http
// and https for default browser).
//
// This helper class manages both the timer and the registry watchers and makes
// sure the callback for the end of the settings interaction is only run once.
// This class also manages its own lifetime.
class OpenSystemSettingsHelper {
 public:
  OpenSystemSettingsHelper(const OpenSystemSettingsHelper&) = delete;
  OpenSystemSettingsHelper& operator=(const OpenSystemSettingsHelper&) = delete;

  // Begin the monitoring and will call |on_finished_callback| when done.
  // Takes in a null-terminated array of |protocols| whose registry keys must be
  // watched. The array must contain at least one element.
  static void Begin(const wchar_t* const protocols[],
                    base::OnceClosure on_finished_callback) {
    delete instance_;
    instance_ = new OpenSystemSettingsHelper(protocols,
                                             std::move(on_finished_callback));
  }

 private:
  // The reason the settings interaction concluded. Do not modify the ordering
  // because it is used for UMA.
  enum ConcludeReason { REGISTRY_WATCHER, TIMEOUT, NUM_CONCLUDE_REASON_TYPES };

  OpenSystemSettingsHelper(const wchar_t* const protocols[],
                           base::OnceClosure on_finished_callback)
      : scoped_user_protocol_entry_(protocols[0]),
        on_finished_callback_(std::move(on_finished_callback)) {
    static const wchar_t kUrlAssociationFormat[] =
        L"SOFTWARE\\Microsoft\\Windows\\Shell\\Associations\\UrlAssociations\\"
        L"%ls\\UserChoice";

    // Remember the start time.
    start_time_ = base::TimeTicks::Now();

    for (const wchar_t* const* scan = &protocols[0]; *scan != nullptr; ++scan) {
      AddRegistryKeyWatcher(
          base::StringPrintf(kUrlAssociationFormat, *scan).c_str());
    }
    // Only the watchers that were succesfully initialized are counted.
    registry_watcher_count_ = registry_key_watchers_.size();

    timer_.Start(FROM_HERE, base::Minutes(2),
                 base::BindOnce(&OpenSystemSettingsHelper::ConcludeInteraction,
                                weak_ptr_factory_.GetWeakPtr(),
                                ConcludeReason::TIMEOUT));
  }

  ~OpenSystemSettingsHelper() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  }

  // Called when a change is detected on one of the registry keys being watched.
  // Note: All types of modification to the registry key will trigger this
  //       function even if the value change is the only one that matters. This
  //       is good enough for now.
  void OnRegistryKeyChanged() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    // Make sure all the registry watchers have fired.
    if (--registry_watcher_count_ == 0) {
      base::UmaHistogramMediumTimes(
          "DefaultBrowser.SettingsInteraction.RegistryWatcherDuration",
          base::TimeTicks::Now() - start_time_);

      ConcludeInteraction(ConcludeReason::REGISTRY_WATCHER);
    }
  }

  // Ends the monitoring with the system settings. Will call
  // |on_finished_callback_| and then dispose of this class instance to make
  // sure the callback won't get called subsequently.
  void ConcludeInteraction(ConcludeReason conclude_reason) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

    base::UmaHistogramEnumeration(
        "DefaultBrowser.SettingsInteraction.ConcludeReason", conclude_reason,
        NUM_CONCLUDE_REASON_TYPES);
    std::move(on_finished_callback_).Run();
    delete instance_;
    instance_ = nullptr;
  }

  // Helper function to create a registry watcher for a given |key_path|. Do
  // nothing on initialization failure.
  void AddRegistryKeyWatcher(const wchar_t* key_path) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

    auto reg_key = std::make_unique<base::win::RegKey>(HKEY_CURRENT_USER,
                                                       key_path, KEY_NOTIFY);

    if (reg_key->Valid() && reg_key->StartWatching(base::BindOnce(
                                &OpenSystemSettingsHelper::OnRegistryKeyChanged,
                                weak_ptr_factory_.GetWeakPtr()))) {
      registry_key_watchers_.push_back(std::move(reg_key));
    }
  }

  // Used to make sure only one instance is alive at the same time.
  static OpenSystemSettingsHelper* instance_;

  // This is needed to make sure that Windows displays an entry for the protocol
  // inside the "Choose default apps by protocol" settings page.
  ScopedUserProtocolEntry scoped_user_protocol_entry_;

  // The function to call when the interaction with the system settings is
  // finished.
  base::OnceClosure on_finished_callback_;

  // The number of time the registry key watchers must fire.
  int registry_watcher_count_ = 0;

  // There can be multiple registry key watchers as some settings modify
  // multiple protocol associations. e.g. Changing the default browser modifies
  // the http and https associations.
  std::vector<std::unique_ptr<base::win::RegKey>> registry_key_watchers_;

  base::OneShotTimer timer_;

  // Records the time it takes for the final registry watcher to get signaled.
  base::TimeTicks start_time_;

  SEQUENCE_CHECKER(sequence_checker_);

  // Weak ptrs are used to bind this class to the callbacks of the timer and the
  // registry watcher. This makes it possible to self-delete after one of the
  // callbacks is executed to cancel the remaining ones.
  base::WeakPtrFactory<OpenSystemSettingsHelper> weak_ptr_factory_{this};
};

OpenSystemSettingsHelper* OpenSystemSettingsHelper::instance_ = nullptr;

// Helper class to determine if Chrome is pinned to the taskbar. Hides the
// complexity of managing the lifetime of the connection to the ChromeWinUtil
// service.
class IsPinnedToTaskbarHelper {
 public:
  using ResultCallback = win::IsPinnedToTaskbarCallback;
  using ErrorCallback = win::ConnectionErrorCallback;

  IsPinnedToTaskbarHelper(const IsPinnedToTaskbarHelper&) = delete;
  IsPinnedToTaskbarHelper& operator=(const IsPinnedToTaskbarHelper&) = delete;

  static void GetState(ErrorCallback error_callback,
                       ResultCallback result_callback);

 private:
  IsPinnedToTaskbarHelper(ErrorCallback error_callback,
                          ResultCallback result_callback);

  void OnConnectionError();
  void OnIsPinnedToTaskbarResult(bool succeeded, bool is_pinned_to_taskbar);

  mojo::Remote<chrome::mojom::UtilWin> remote_util_win_;

  ErrorCallback error_callback_;
  ResultCallback result_callback_;

  SEQUENCE_CHECKER(sequence_checker_);
};

// static
void IsPinnedToTaskbarHelper::GetState(ErrorCallback error_callback,
                                       ResultCallback result_callback) {
  // Self-deleting when the ShellHandler completes.
  new IsPinnedToTaskbarHelper(std::move(error_callback),
                              std::move(result_callback));
}

IsPinnedToTaskbarHelper::IsPinnedToTaskbarHelper(ErrorCallback error_callback,
                                                 ResultCallback result_callback)
    : remote_util_win_(LaunchUtilWinServiceInstance()),
      error_callback_(std::move(error_callback)),
      result_callback_(std::move(result_callback)) {
  DCHECK(error_callback_);
  DCHECK(result_callback_);

  // |remote_util_win_| owns the callbacks and is guaranteed to be destroyed
  // before |this|, therefore making base::Unretained() safe to use.
  remote_util_win_.set_disconnect_handler(base::BindOnce(
      &IsPinnedToTaskbarHelper::OnConnectionError, base::Unretained(this)));
  remote_util_win_->IsPinnedToTaskbar(
      base::BindOnce(&IsPinnedToTaskbarHelper::OnIsPinnedToTaskbarResult,
                     base::Unretained(this)));
}

void IsPinnedToTaskbarHelper::OnConnectionError() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::move(error_callback_).Run();
  delete this;
}

void IsPinnedToTaskbarHelper::OnIsPinnedToTaskbarResult(
    bool succeeded,
    bool is_pinned_to_taskbar) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::move(result_callback_).Run(succeeded, is_pinned_to_taskbar);
  delete this;
}

// Helper class to unpin shortcuts from the taskbar. Hides the complexity of
//  managing the lifetime of the connection to the Windows utility service.
class UnpinShortcutsHelper {
 public:
  UnpinShortcutsHelper(const UnpinShortcutsHelper&) = delete;
  UnpinShortcutsHelper& operator=(const UnpinShortcutsHelper&) = delete;

  static void DoUnpin(const std::vector<base::FilePath>& shortcuts,
                      base::OnceClosure completion_callback);

 private:
  static void RecordUnpinShortcutProcessError(bool error);

  UnpinShortcutsHelper(const std::vector<base::FilePath>& shortcuts,
                       base::OnceClosure completion_callback);

  void OnConnectionError();
  void OnUnpinShortcutResult();

  mojo::Remote<chrome::mojom::UtilWin> remote_util_win_;

  base::OnceClosure completion_callback_;

  SEQUENCE_CHECKER(sequence_checker_);
};

// static
void UnpinShortcutsHelper::RecordUnpinShortcutProcessError(bool error) {
  base::UmaHistogramBoolean("Windows.UnpinShortcut.ProcessError", error);
}

// static
void UnpinShortcutsHelper::DoUnpin(const std::vector<base::FilePath>& shortcuts,
                                   base::OnceClosure completion_callback) {
  // Self-deleting when the ShellHandler completes.
  new UnpinShortcutsHelper(shortcuts, std::move(completion_callback));
}

UnpinShortcutsHelper::UnpinShortcutsHelper(
    const std::vector<base::FilePath>& shortcuts,
    base::OnceClosure completion_callback)
    : remote_util_win_(LaunchUtilWinServiceInstance()),
      completion_callback_(std::move(completion_callback)) {
  DCHECK(completion_callback_);

  // |remote_util_win_| owns the callbacks and is guaranteed to be destroyed
  // before |this|, therefore making base::Unretained() safe to use.
  remote_util_win_.set_disconnect_handler(base::BindOnce(
      &UnpinShortcutsHelper::OnConnectionError, base::Unretained(this)));
  remote_util_win_->UnpinShortcuts(
      shortcuts, base::BindOnce(&UnpinShortcutsHelper::OnUnpinShortcutResult,
                                base::Unretained(this)));
}

void UnpinShortcutsHelper::OnConnectionError() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  RecordUnpinShortcutProcessError(true);
  std::move(completion_callback_).Run();
  delete this;
}

void UnpinShortcutsHelper::OnUnpinShortcutResult() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  RecordUnpinShortcutProcessError(false);
  std::move(completion_callback_).Run();
  delete this;
}

// Helper class to create or update desktop shortcuts Hides the complexity of
// managing the lifetime of the connection to the Windows utility service.
class CreateOrUpdateShortcutsHelper {
 public:
  CreateOrUpdateShortcutsHelper(const CreateOrUpdateShortcutsHelper&) = delete;
  CreateOrUpdateShortcutsHelper& operator=(
      const CreateOrUpdateShortcutsHelper&) = delete;

  static void DoCreateOrUpdateShortcuts(
      const std::vector<base::FilePath>& shortcuts,
      const std::vector<base::win::ShortcutProperties>& properties,
      base::win::ShortcutOperation operation,
      win::CreateOrUpdateShortcutsResultCallback);

 private:
  // Possible results of DoCreateOrUpdateShortcuts().
  // These values are persisted to logs. Entries should not be renumbered and
  // numeric values should never be reused. These correspond to
  // CreateOrUpdateShortcutsResult in enums.xml.
  enum class CreateOrUpdateShortcutsResult {
    kSuccess = 0,
    kErrorProcessDisconnected = 1,
    kErrorShortcutOperationFailed = 2,
    kMaxValue = kErrorShortcutOperationFailed
  };

  static void RecordCreateOrUpdateShortcutsResult(
      CreateOrUpdateShortcutsResult result);

  CreateOrUpdateShortcutsHelper(
      const std::vector<base::FilePath>& shortcuts,
      const std::vector<base::win::ShortcutProperties>& properties,
      base::win::ShortcutOperation operation,
      win::CreateOrUpdateShortcutsResultCallback completion_callback);

  void OnConnectionError();
  void OnCreateOrUpdateShortcutResult(bool succeeded);

  mojo::Remote<chrome::mojom::UtilWin> remote_util_win_;

  win::CreateOrUpdateShortcutsResultCallback completion_callback_;

  SEQUENCE_CHECKER(sequence_checker_);
};

// static
void CreateOrUpdateShortcutsHelper::RecordCreateOrUpdateShortcutsResult(
    CreateOrUpdateShortcutsResult result) {
  base::UmaHistogramEnumeration("Windows.CreateOrUpdateShortcuts.Result",
                                result);
}

// static
void CreateOrUpdateShortcutsHelper::DoCreateOrUpdateShortcuts(
    const std::vector<base::FilePath>& shortcuts,
    const std::vector<base::win::ShortcutProperties>& properties,
    base::win::ShortcutOperation operation,
    win::CreateOrUpdateShortcutsResultCallback completion_callback) {
  // Self-deleting when the ShellHandler completes.
  new CreateOrUpdateShortcutsHelper(shortcuts, properties, operation,
                                    std::move(completion_callback));
}

CreateOrUpdateShortcutsHelper::CreateOrUpdateShortcutsHelper(
    const std::vector<base::FilePath>& shortcuts,
    const std::vector<base::win::ShortcutProperties>& properties,
    base::win::ShortcutOperation operation,
    win::CreateOrUpdateShortcutsResultCallback completion_callback)
    : remote_util_win_(LaunchUtilWinServiceInstance()),
      completion_callback_(std::move(completion_callback)) {
  DCHECK(completion_callback_);

  // |remote_util_win_| owns the callbacks and is guaranteed to be destroyed
  // before |this|, therefore making base::Unretained() safe to use.
  remote_util_win_.set_disconnect_handler(
      base::BindOnce(&CreateOrUpdateShortcutsHelper::OnConnectionError,
                     base::Unretained(this)));
  remote_util_win_->CreateOrUpdateShortcuts(
      shortcuts, properties, operation,
      base::BindOnce(
          &CreateOrUpdateShortcutsHelper::OnCreateOrUpdateShortcutResult,
          base::Unretained(this)));
}

void CreateOrUpdateShortcutsHelper::OnConnectionError() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  RecordCreateOrUpdateShortcutsResult(
      CreateOrUpdateShortcutsResult::kErrorProcessDisconnected);
  std::move(completion_callback_).Run(false);
  delete this;
}

void CreateOrUpdateShortcutsHelper::OnCreateOrUpdateShortcutResult(
    bool succeeded) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  RecordCreateOrUpdateShortcutsResult(
      succeeded ? CreateOrUpdateShortcutsResult::kSuccess
                : CreateOrUpdateShortcutsResult::kErrorShortcutOperationFailed);
  std::move(completion_callback_).Run(succeeded);
  delete this;
}

void MigrateChromeAndChromeProxyShortcuts(
    const base::FilePath& chrome_exe,
    const base::FilePath& chrome_proxy_path,
    const base::FilePath& shortcut_path) {
  win::MigrateShortcutsInPathInternal(chrome_exe, shortcut_path);

  // Migrate any pinned PWA shortcuts in taskbar directory.
  win::MigrateShortcutsInPathInternal(chrome_proxy_path, shortcut_path);
}

std::wstring GetHttpProtocolUserChoiceProgId() {
  std::wstring prog_id;
  base::win::RegKey key(HKEY_CURRENT_USER, ShellUtil::kRegVistaUrlPrefs,
                        KEY_QUERY_VALUE);
  if (key.Valid())
    key.ReadValue(L"ProgId", &prog_id);
  return prog_id;
}

}  // namespace

bool SetAsDefaultBrowser() {
  base::ScopedBlockingCall scoped_blocking_call(FROM_HERE,
                                                base::BlockingType::MAY_BLOCK);

  base::FilePath chrome_exe;
  if (!base::PathService::Get(base::FILE_EXE, &chrome_exe)) {
    LOG(ERROR) << "Error getting app exe path";
    return false;
  }

  // From UI currently we only allow setting default browser for current user.
  if (!(CanSetAsDefaultDirectly()
            ? ShellUtil::MakeChromeDefaultDirectly(
                  ShellUtil::CURRENT_USER, chrome_exe,
                  true /* elevate_if_not_admin */)
            : ShellUtil::MakeChromeDefault(ShellUtil::CURRENT_USER, chrome_exe,
                                           true /* elevate_if_not_admin */))) {
    LOG(ERROR) << "Chrome could not be set as default browser.";
    return false;
  }

  VLOG(1) << "Chrome registered as default browser.";
  return true;
}

bool SetAsDefaultProtocolClient(const std::string& protocol) {
  base::ScopedBlockingCall scoped_blocking_call(FROM_HERE,
                                                base::BlockingType::MAY_BLOCK);

  if (protocol.empty())
    return false;

  base::FilePath chrome_exe;
  if (!base::PathService::Get(base::FILE_EXE, &chrome_exe)) {
    LOG(ERROR) << "Error getting app exe path";
    return false;
  }

  std::wstring wprotocol(base::UTF8ToWide(protocol));
  if (!ShellUtil::MakeChromeDefaultProtocolClient(chrome_exe, wprotocol)) {
    LOG(ERROR) << "Chrome could not be set as default handler for "
               << protocol << ".";
    return false;
  }

  VLOG(1) << "Chrome registered as default handler for " << protocol << ".";
  return true;
}

DefaultWebClientSetPermission GetDefaultWebClientSetPermission() {
  if (!install_static::SupportsSetAsDefaultBrowser())
    return SET_DEFAULT_NOT_ALLOWED;
  if (ShellUtil::CanMakeChromeDefaultUnattended())
    return SET_DEFAULT_UNATTENDED;
  if (CanSetAsDefaultDirectly())
    return SET_DEFAULT_UNATTENDED;
  // Setting the default web client generally requires user interaction in
  // Windows 8+ with permitted exceptions above.
  return SET_DEFAULT_INTERACTIVE;
}

bool IsElevationNeededForSettingDefaultProtocolClient() {
  return base::win::GetVersion() < base::win::Version::WIN8;
}

std::u16string GetApplicationNameForProtocol(const GURL& url) {
  // Windows 8 or above has a new protocol association query.
  if (base::win::GetVersion() >= base::win::Version::WIN8) {
    std::u16string application_name = GetAppForProtocolUsingAssocQuery(url);
    if (!application_name.empty())
      return application_name;
  }

  return GetAppForProtocolUsingRegistry(url);
}

DefaultWebClientState GetDefaultBrowser() {
  return GetDefaultWebClientStateFromShellUtilDefaultState(
      ShellUtil::GetChromeDefaultState());
}

// This method checks if Firefox is default browser by checking for the default
// HTTP protocol handler. Returns false in case of error or if Firefox is not
// the user's default http protocol client.
bool IsFirefoxDefaultBrowser() {
  return base::StartsWith(GetHttpProtocolUserChoiceProgId(), L"FirefoxURL",
                          base::CompareCase::SENSITIVE);
}

std::string GetFirefoxProgIdSuffix() {
  const std::wstring app_cmd = GetHttpProtocolUserChoiceProgId();
  static constexpr base::WStringPiece kFirefoxProgIdPrefix(L"FirefoxURL-");
  if (base::StartsWith(app_cmd, kFirefoxProgIdPrefix,
                       base::CompareCase::SENSITIVE)) {
    // Returns the id that appears after the prefix "FirefoxURL-".
    return std::string(app_cmd.begin() + kFirefoxProgIdPrefix.size(),
                       app_cmd.end());
  }
  return std::string();
}

bool IsIEDefaultBrowser() {
  return GetHttpProtocolUserChoiceProgId() == L"IE.HTTP";
}

DefaultWebClientState IsDefaultProtocolClient(const std::string& protocol) {
  return GetDefaultWebClientStateFromShellUtilDefaultState(
      ShellUtil::GetChromeDefaultProtocolClientState(
          base::UTF8ToWide(protocol)));
}

namespace win {

bool SetAsDefaultBrowserUsingIntentPicker() {
  base::ScopedBlockingCall scoped_blocking_call(FROM_HERE,
                                                base::BlockingType::MAY_BLOCK);

  base::FilePath chrome_exe;
  if (!base::PathService::Get(base::FILE_EXE, &chrome_exe)) {
    NOTREACHED() << "Error getting app exe path";
    return false;
  }

  if (!ShellUtil::ShowMakeChromeDefaultSystemUI(chrome_exe)) {
    LOG(ERROR) << "Failed to launch the set-default-browser Windows UI.";
    return false;
  }

  VLOG(1) << "Set-default-browser Windows UI completed.";
  return true;
}

void SetAsDefaultBrowserUsingSystemSettings(
    base::OnceClosure on_finished_callback) {
  base::FilePath chrome_exe;
  if (!base::PathService::Get(base::FILE_EXE, &chrome_exe)) {
    NOTREACHED() << "Error getting app exe path";
    std::move(on_finished_callback).Run();
    return;
  }

  // Create an action recorder that will open the settings app once it has
  // initialized.
  std::unique_ptr<DefaultBrowserActionRecorder> recorder =
          std::make_unique<DefaultBrowserActionRecorder>(base::BindOnce(
          base::IgnoreResult(&ShellUtil::ShowMakeChromeDefaultSystemUI),
          chrome_exe));

  // The helper manages its own lifetime. Bind the action recorder
  // into the finished callback to keep it alive throughout the
  // interaction.
  static const wchar_t* const kProtocols[] = {L"http", L"https", nullptr};
  OpenSystemSettingsHelper::Begin(
      kProtocols, base::BindOnce(&OnSettingsAppFinished, std::move(recorder),
                                 std::move(on_finished_callback)));
}

bool SetAsDefaultProtocolClientUsingIntentPicker(const std::string& protocol) {
  base::ScopedBlockingCall scoped_blocking_call(FROM_HERE,
                                                base::BlockingType::MAY_BLOCK);

  base::FilePath chrome_exe;
  if (!base::PathService::Get(base::FILE_EXE, &chrome_exe)) {
    NOTREACHED() << "Error getting app exe path";
    return false;
  }

  std::wstring wprotocol(base::UTF8ToWide(protocol));
  if (!ShellUtil::ShowMakeChromeDefaultProtocolClientSystemUI(chrome_exe,
                                                              wprotocol)) {
    LOG(ERROR) << "Failed to launch the set-default-client Windows UI.";
    return false;
  }

  VLOG(1) << "Set-default-client Windows UI completed.";
  return true;
}

void SetAsDefaultProtocolClientUsingSystemSettings(
    const std::string& protocol,
    base::OnceClosure on_finished_callback) {
  base::FilePath chrome_exe;
  if (!base::PathService::Get(base::FILE_EXE, &chrome_exe)) {
    NOTREACHED() << "Error getting app exe path";
    std::move(on_finished_callback).Run();
    return;
  }

  // The helper manages its own lifetime.
  std::wstring wprotocol(base::UTF8ToWide(protocol));
  const wchar_t* const kProtocols[] = {wprotocol.c_str(), nullptr};
  OpenSystemSettingsHelper::Begin(kProtocols, std::move(on_finished_callback));

  ShellUtil::ShowMakeChromeDefaultProtocolClientSystemUI(chrome_exe, wprotocol);
}

std::wstring GetAppUserModelIdForApp(const std::wstring& app_name,
                                     const base::FilePath& profile_path) {
  return GetAppUserModelIdImpl(install_static::GetBaseAppId(), app_name,
                               profile_path);
}

std::wstring GetAppUserModelIdForBrowser(const base::FilePath& profile_path) {
  return GetAppUserModelIdImpl(
      std::wstring(),
      ShellUtil::GetBrowserModelId(InstallUtil::IsPerUserInstall()),
      profile_path);
}

void UnpinShortcuts(const std::vector<base::FilePath>& shortcuts,
                    base::OnceClosure completion_callback) {
  UnpinShortcutsHelper::DoUnpin(shortcuts, std::move(completion_callback));
}

void CreateOrUpdateShortcuts(
    const std::vector<base::FilePath>& shortcuts,
    const std::vector<base::win::ShortcutProperties>& properties,
    base::win::ShortcutOperation operation,
    win::CreateOrUpdateShortcutsResultCallback callback) {
  CreateOrUpdateShortcutsHelper::DoCreateOrUpdateShortcuts(
      shortcuts, properties, operation, std::move(callback));
}

void MigrateTaskbarPins(base::OnceClosure completion_callback) {
  // This needs to happen (e.g. so that the appid is fixed and the
  // run-time Chrome icon is merged with the taskbar shortcut), but it is not an
  // urgent task.
  // MigrateTaskbarPinsCallback just calls MigrateShortcutsInPathInternal
  // several times with different parameters.  Each call may or may not load
  // DLL's. Since the callback may take the loader lock several times, and this
  // is the bulk of the callback's work, run the whole thing on a foreground
  // thread.
  //
  // BEST_EFFORT means it will be scheduled after higher-priority tasks, but
  // MUST_USE_FOREGROUND means that when it is scheduled it will run in the
  // foregound.
  base::ThreadPool::CreateCOMSTATaskRunner(
      {base::MayBlock(), base::TaskPriority::BEST_EFFORT,
       base::ThreadPolicy::MUST_USE_FOREGROUND})
      ->PostTaskAndReply(
          FROM_HERE, base::BindOnce([]() {
            base::FilePath taskbar_path;
            base::FilePath implicit_apps_path;
            base::PathService::Get(base::DIR_TASKBAR_PINS, &taskbar_path);
            base::PathService::Get(base::DIR_IMPLICIT_APP_SHORTCUTS,
                                   &implicit_apps_path);
            MigrateTaskbarPinsCallback(taskbar_path, implicit_apps_path);
          }),
          std::move(completion_callback));
}

void MigrateTaskbarPinsCallback(const base::FilePath& taskbar_path,
                                const base::FilePath& implicit_apps_path) {
  // Get full path of chrome.
  base::FilePath chrome_exe;
  if (!base::PathService::Get(base::FILE_EXE, &chrome_exe))
    return;
  base::FilePath chrome_proxy_path(web_app::GetChromeProxyPath());

  if (!taskbar_path.empty()) {
    MigrateChromeAndChromeProxyShortcuts(chrome_exe, chrome_proxy_path,
                                         taskbar_path);
  }
  if (implicit_apps_path.empty())
    return;
  base::FileEnumerator directory_enum(implicit_apps_path, /*recursive=*/false,
                                      base::FileEnumerator::DIRECTORIES);
  for (base::FilePath implicit_app_sub_directory = directory_enum.Next();
       !implicit_app_sub_directory.empty();
       implicit_app_sub_directory = directory_enum.Next()) {
    MigrateChromeAndChromeProxyShortcuts(chrome_exe, chrome_proxy_path,
                                         implicit_app_sub_directory);
  }
}

void GetIsPinnedToTaskbarState(ConnectionErrorCallback on_error_callback,
                               IsPinnedToTaskbarCallback result_callback) {
  IsPinnedToTaskbarHelper::GetState(std::move(on_error_callback),
                                    std::move(result_callback));
}

int MigrateShortcutsInPathInternal(const base::FilePath& chrome_exe,
                                   const base::FilePath& path) {
  // This function may load DLL's so ensure it is running in a foreground
  // thread.
  DCHECK_GT(base::PlatformThread::GetCurrentThreadType(),
            base::ThreadType::kBackground);

  // Enumerate all pinned shortcuts in the given path directly.
  base::FileEnumerator shortcuts_enum(
      path, false,  // not recursive
      base::FileEnumerator::FILES, FILE_PATH_LITERAL("*.lnk"));

  bool is_per_user_install = InstallUtil::IsPerUserInstall();

  int shortcuts_migrated = 0;
  base::FilePath target_path;
  std::wstring arguments;
  base::win::ScopedPropVariant propvariant;
  for (base::FilePath shortcut = shortcuts_enum.Next(); !shortcut.empty();
       shortcut = shortcuts_enum.Next()) {
    // TODO(gab): Use ProgramCompare instead of comparing FilePaths below once
    // it is fixed to work with FilePaths with spaces.
    if (!base::win::ResolveShortcut(shortcut, &target_path, &arguments) ||
        !base::FilePath::CompareEqualIgnoreCase(chrome_exe.value(),
                                                target_path.value())) {
      continue;
    }
    base::CommandLine command_line(
        base::CommandLine::FromString(base::StringPrintf(
            L"\"%ls\" %ls", target_path.value().c_str(), arguments.c_str())));

    // Get the expected AppId for this Chrome shortcut.
    std::wstring expected_app_id(
        GetExpectedAppId(command_line, is_per_user_install));
    if (expected_app_id.empty())
      continue;

    // Load the shortcut.
    Microsoft::WRL::ComPtr<IShellLink> shell_link;
    Microsoft::WRL::ComPtr<IPersistFile> persist_file;
    if (FAILED(::CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&shell_link))) ||
        FAILED(shell_link.As(&persist_file)) ||
        FAILED(persist_file->Load(shortcut.value().c_str(), STGM_READ))) {
      DLOG(WARNING) << "Failed loading shortcut at " << shortcut.value();
      continue;
    }

    // Any properties that need to be updated on the shortcut will be stored in
    // |updated_properties|.
    base::win::ShortcutProperties updated_properties;

    // Validate the existing app id for the shortcut.
    Microsoft::WRL::ComPtr<IPropertyStore> property_store;
    propvariant.Reset();
    if (FAILED(shell_link.As(&property_store)) ||
        property_store->GetValue(PKEY_AppUserModel_ID, propvariant.Receive()) !=
            S_OK) {
      // When in doubt, prefer not updating the shortcut.
      NOTREACHED();
      continue;
    } else {
      switch (propvariant.get().vt) {
        case VT_EMPTY:
          // If there is no app_id set, set our app_id if one is expected.
          if (!expected_app_id.empty())
            updated_properties.set_app_id(expected_app_id);
          break;
        case VT_LPWSTR:
          if (expected_app_id != std::wstring(propvariant.get().pwszVal))
            updated_properties.set_app_id(expected_app_id);
          break;
        default:
          NOTREACHED();
          continue;
      }
    }

    // Clear dual_mode property from any shortcuts that previously had it (it
    // was only ever installed on shortcuts with the
    // |default_chromium_model_id|).
    std::wstring default_chromium_model_id(
        ShellUtil::GetBrowserModelId(is_per_user_install));
    if (expected_app_id == default_chromium_model_id) {
      propvariant.Reset();
      if (property_store->GetValue(PKEY_AppUserModel_IsDualMode,
                                   propvariant.Receive()) != S_OK) {
        // When in doubt, prefer to not update the shortcut.
        NOTREACHED();
        continue;
      }
      if (propvariant.get().vt == VT_BOOL &&
                 !!propvariant.get().boolVal) {
        updated_properties.set_dual_mode(false);
      }
    }

    persist_file.Reset();
    shell_link.Reset();

    // Update the shortcut if some of its properties need to be updated.
    if (updated_properties.options &&
        base::win::CreateOrUpdateShortcutLink(
            shortcut, updated_properties,
            base::win::ShortcutOperation::kUpdateExisting)) {
      ++shortcuts_migrated;
    }
  }
  return shortcuts_migrated;
}

}  // namespace win

}  // namespace shell_integration
