// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/download/download_status_updater.h"

#include <dlfcn.h>

#include <memory>
#include <string>

#include "base/compiler_specific.h"
#include "base/environment.h"
#include "base/nix/xdg_util.h"
#include "chrome/common/channel_info.h"
#include "ui/base/glib/glib_integers.h"

// Unity data typedefs.
typedef struct _UnityInspector UnityInspector;
typedef UnityInspector* (*unity_inspector_get_default_func)(void);
typedef gboolean (*unity_inspector_get_unity_running_func)(
    UnityInspector* self);

typedef struct _UnityLauncherEntry UnityLauncherEntry;
typedef UnityLauncherEntry* (*unity_launcher_entry_get_for_desktop_id_func)(
    const gchar* desktop_id);
typedef void (*unity_launcher_entry_set_count_func)(UnityLauncherEntry* self,
                                                    gint64 value);
typedef void (*unity_launcher_entry_set_count_visible_func)(
    UnityLauncherEntry* self,
    gboolean value);
typedef void (*unity_launcher_entry_set_progress_func)(UnityLauncherEntry* self,
                                                       gdouble value);
typedef void (*unity_launcher_entry_set_progress_visible_func)(
    UnityLauncherEntry* self,
    gboolean value);

namespace {

bool attempted_load = false;

// Unity has a singleton object that we can ask whether the unity is running.
UnityInspector* inspector = nullptr;

// A link to the desktop entry in the panel.
UnityLauncherEntry* chrome_entry = nullptr;

// Retrieved functions from libunity.
unity_inspector_get_unity_running_func get_unity_running = nullptr;
unity_launcher_entry_set_count_func entry_set_count = nullptr;
unity_launcher_entry_set_count_visible_func entry_set_count_visible = nullptr;
unity_launcher_entry_set_progress_func entry_set_progress = nullptr;
unity_launcher_entry_set_progress_visible_func entry_set_progress_visible =
    nullptr;

NO_SANITIZE("cfi-icall")
void EnsureLibUnityLoaded() {
  using base::nix::GetDesktopEnvironment;

  if (attempted_load)
    return;
  attempted_load = true;

  std::unique_ptr<base::Environment> env(base::Environment::Create());
  base::nix::DesktopEnvironment desktop_env = GetDesktopEnvironment(env.get());

  // The "icon-tasks" KDE task manager also honors Unity Launcher API.
  if (desktop_env != base::nix::DESKTOP_ENVIRONMENT_UNITY &&
      desktop_env != base::nix::DESKTOP_ENVIRONMENT_KDE4 &&
      desktop_env != base::nix::DESKTOP_ENVIRONMENT_KDE5)
    return;

  // Ubuntu still hasn't given us a nice libunity.so symlink.
  void* unity_lib = dlopen("libunity.so.4", RTLD_LAZY);
  if (!unity_lib)
    unity_lib = dlopen("libunity.so.6", RTLD_LAZY);
  if (!unity_lib)
    unity_lib = dlopen("libunity.so.9", RTLD_LAZY);
  if (!unity_lib)
    return;

  unity_inspector_get_default_func inspector_get_default =
      reinterpret_cast<unity_inspector_get_default_func>(
          dlsym(unity_lib, "unity_inspector_get_default"));
  if (inspector_get_default) {
    inspector = inspector_get_default();

    get_unity_running =
        reinterpret_cast<unity_inspector_get_unity_running_func>(
            dlsym(unity_lib, "unity_inspector_get_unity_running"));
  }

  unity_launcher_entry_get_for_desktop_id_func entry_get_for_desktop_id =
      reinterpret_cast<unity_launcher_entry_get_for_desktop_id_func>(
          dlsym(unity_lib, "unity_launcher_entry_get_for_desktop_id"));
  if (entry_get_for_desktop_id) {
    std::string desktop_id = chrome::GetDesktopName(env.get());
    chrome_entry = entry_get_for_desktop_id(desktop_id.c_str());

    entry_set_count = reinterpret_cast<unity_launcher_entry_set_count_func>(
        dlsym(unity_lib, "unity_launcher_entry_set_count"));

    entry_set_count_visible =
        reinterpret_cast<unity_launcher_entry_set_count_visible_func>(
            dlsym(unity_lib, "unity_launcher_entry_set_count_visible"));

    entry_set_progress =
        reinterpret_cast<unity_launcher_entry_set_progress_func>(
            dlsym(unity_lib, "unity_launcher_entry_set_progress"));

    entry_set_progress_visible =
        reinterpret_cast<unity_launcher_entry_set_progress_visible_func>(
            dlsym(unity_lib, "unity_launcher_entry_set_progress_visible"));
  }
}

NO_SANITIZE("cfi-icall")
bool IsRunning() {
  return inspector && get_unity_running && get_unity_running(inspector);
}

NO_SANITIZE("cfi-icall")
void SetDownloadCount(int count) {
  if (chrome_entry && entry_set_count && entry_set_count_visible) {
    entry_set_count(chrome_entry, count);
    entry_set_count_visible(chrome_entry, count != 0);
  }
}

NO_SANITIZE("cfi-icall")
void SetProgressFraction(float percentage) {
  if (chrome_entry && entry_set_progress && entry_set_progress_visible) {
    entry_set_progress(chrome_entry, percentage);
    entry_set_progress_visible(chrome_entry,
                               percentage > 0.0 && percentage < 1.0);
  }
}

}  // namespace

void DownloadStatusUpdater::UpdateAppIconDownloadProgress(
    download::DownloadItem* download) {
  // Only implemented on Unity for now.
  EnsureLibUnityLoaded();
  if (!IsRunning())
    return;
  float progress = 0;
  int download_count = 0;
  GetProgress(&progress, &download_count);
  SetDownloadCount(download_count);
  SetProgressFraction(progress);
}
