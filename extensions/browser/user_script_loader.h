// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_BROWSER_USER_SCRIPT_LOADER_H_
#define EXTENSIONS_BROWSER_USER_SCRIPT_LOADER_H_

#include <list>
#include <map>
#include <memory>
#include <set>

#include "base/callback_forward.h"
#include "base/compiler_specific.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/read_only_shared_memory_region.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "content/public/browser/render_process_host_creation_observer.h"
#include "extensions/common/mojom/host_id.mojom.h"
#include "extensions/common/user_script.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace base {
class ReadOnlySharedMemoryRegion;
}

namespace content {
class BrowserContext;
class RenderProcessHost;
}

namespace extensions {

// Manages one "logical unit" of user scripts in shared memory by constructing a
// new shared memory region when the set of scripts changes. Also notifies
// renderers of new shared memory region when new renderers appear, or when
// script reloading completes. Script loading lives on the UI thread. Instances
// of this class are embedded within classes with names ending in
// UserScriptManager. These "manager" classes implement the strategy for which
// scripts to load/unload on this logical unit of scripts.
class UserScriptLoader : public content::RenderProcessHostCreationObserver {
 public:
  using LoadScriptsCallback =
      base::OnceCallback<void(std::unique_ptr<UserScriptList>,
                              base::ReadOnlySharedMemoryRegion shared_memory)>;

  using ScriptsLoadedCallback =
      base::OnceCallback<void(UserScriptLoader* loader,
                              const absl::optional<std::string>& error)>;

  class Observer {
   public:
    virtual void OnScriptsLoaded(UserScriptLoader* loader,
                                 content::BrowserContext* browser_context) = 0;
    virtual void OnUserScriptLoaderDestroyed(UserScriptLoader* loader) = 0;
  };

  // Parses the includes out of |script| and returns them in |includes|.
  static bool ParseMetadataHeader(const base::StringPiece& script_text,
                                  UserScript* script);

  UserScriptLoader(content::BrowserContext* browser_context,
                   const mojom::HostID& host_id);

  UserScriptLoader(const UserScriptLoader&) = delete;
  UserScriptLoader& operator=(const UserScriptLoader&) = delete;

  ~UserScriptLoader() override;

  // Add |scripts| to the set of scripts managed by this loader. If provided,
  // |callback| is called when |scripts| have been loaded.
  void AddScripts(std::unique_ptr<UserScriptList> scripts,
                  ScriptsLoadedCallback callback);

  // Add |scripts| to the set of scripts managed by this loader.
  // The fetch of the content of the script starts URL request
  // to the associated render specified by
  // |render_process_id, render_frame_id|.
  // TODO(hanxi): The renderer information doesn't really belong in this base
  // class, but it's not an easy fix.
  virtual void AddScripts(std::unique_ptr<UserScriptList> scripts,
                          int render_process_id,
                          int render_frame_id,
                          ScriptsLoadedCallback callback);

  // Removes scripts with ids specified in |scripts| from the set of scripts
  // managed by this loader and calls |callback| once these scripts have been
  // removed, if specified.
  // TODO(lazyboy): Likely we can make |scripts| a std::vector, but
  // WebViewContentScriptManager makes this non-trivial.
  void RemoveScripts(const std::set<std::string>& script_ids,
                     ScriptsLoadedCallback callback);

  // Returns true if the scripts for this loader's HostID have been loaded.
  bool HasLoadedScripts() const;

  // Returns true if we have any scripts ready.
  bool initial_load_complete() const { return shared_memory_.IsValid(); }

  const mojom::HostID& host_id() const { return host_id_; }

  // Pickle user scripts and return pointer to the shared memory.
  static base::ReadOnlySharedMemoryRegion Serialize(
      const extensions::UserScriptList& scripts);

  // Adds or removes observers.
  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // Manually attempts a load for this loader, and optionally adds a callback to
  // |queued_load_callbacks_|, to be called when the next load has completed.
  // Only used for tests which manually trigger loads.
  void StartLoadForTesting(ScriptsLoadedCallback callback);

 protected:
  // Allows the derived classes to have different ways to load user scripts.
  // This may not be synchronous with the calls to Add/Remove/Clear scripts.
  virtual void LoadScripts(std::unique_ptr<UserScriptList> user_scripts,
                           const std::set<std::string>& added_script_ids,
                           LoadScriptsCallback callback) = 0;

  // Sets the flag if the initial set of hosts has finished loading; if it's
  // set to be true, calls AttempLoad() to bootstrap.
  void SetReady(bool ready);

  content::BrowserContext* browser_context() const { return browser_context_; }

 private:
  // content::RenderProcessHostCreationObserver:
  void OnRenderProcessHostCreated(
      content::RenderProcessHost* process_host) override;

  // Returns whether or not it is possible that calls to AddScripts(),
  // RemoveScripts(), and/or ClearScripts() have caused any real change in the
  // set of scripts to be loaded.
  bool ScriptsMayHaveChanged() const;

  // Attempts to initiate a load. |callback| is added to
  // |queued_load_callbacks_|, to be called when the next load completes. If no
  // scripts will be changed then |callback| will be called immediately.
  void AttemptLoad(ScriptsLoadedCallback callback);

  // Initiates procedure to start loading scripts on the file thread.
  void StartLoad();

  // Called once we have finished loading the scripts on the file thread.
  void OnScriptsLoaded(std::unique_ptr<UserScriptList> user_scripts,
                       base::ReadOnlySharedMemoryRegion shared_memory);

  // Sends the renderer process a new set of user scripts for this
  // UserScriptLoader's host.
  void SendUpdate(content::RenderProcessHost* process,
                  const base::ReadOnlySharedMemoryRegion& shared_memory);

  bool is_loading() const {
    // |loaded_scripts_| is reset when loading.
    return loaded_scripts_.get() == nullptr;
  }

  // Contains the scripts that were found the last time scripts were updated.
  base::ReadOnlySharedMemoryRegion shared_memory_;

  // List of scripts that are currently loaded. This is null when a load is in
  // progress.
  std::unique_ptr<UserScriptList> loaded_scripts_;

  // The mutually-exclusive information about sets of scripts that were added or
  // removed since the last script load. These maps are keyed by script ids.
  // Note that we only need a script's id for removal.
  std::map<std::string, std::unique_ptr<UserScript>> added_scripts_map_;
  std::set<std::string> removed_script_ids_;

  // If the initial set of hosts has finished loading.
  bool ready_;

  // If list of user scripts is modified while we're loading it, we note
  // that we're currently mid-load and then start over again once the load
  // finishes.  This boolean tracks whether another load is pending.
  bool queued_load_;

  // The browser_context for which the scripts managed here are installed.
  raw_ptr<content::BrowserContext> browser_context_;

  // ID of the host that owns these scripts, if any. This is only set to a
  // non-empty value for declarative user script shared memory regions.
  mojom::HostID host_id_;

  // The associated observers.
  base::ObserverList<Observer>::Unchecked observers_;

  // A list of callbacks associated with script updates that are queued for the
  // next script load (if one is already in progress). These callbacks are moved
  // to |loading_callbacks_| once a new script load starts.
  std::list<ScriptsLoadedCallback> queued_load_callbacks_;

  // A list of callbacks associated with script updates that will be applied in
  // the current script load. These callbacks are called once scripts have
  // finished loading and IPC messages to renderers have been sent.
  std::list<ScriptsLoadedCallback> loading_callbacks_;

  base::WeakPtrFactory<UserScriptLoader> weak_factory_{this};
};

}  // namespace extensions

#endif  // EXTENSIONS_BROWSER_USER_SCRIPT_LOADER_H_
