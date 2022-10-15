// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/path_service.h"

#include <unordered_map>

#include "base/check_op.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/synchronization/lock.h"
#include "build/build_config.h"

#if BUILDFLAG(IS_WIN)
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#endif

namespace base {

bool PathProvider(int key, FilePath* result);

#if BUILDFLAG(IS_WIN)
bool PathProviderWin(int key, FilePath* result);
#elif BUILDFLAG(IS_APPLE)
bool PathProviderMac(int key, FilePath* result);
#elif BUILDFLAG(IS_ANDROID)
bool PathProviderAndroid(int key, FilePath* result);
#elif BUILDFLAG(IS_FUCHSIA)
bool PathProviderFuchsia(int key, FilePath* result);
#elif BUILDFLAG(IS_POSIX)
// PathProviderPosix is the default path provider on POSIX OSes other than
// Mac and Android.
bool PathProviderPosix(int key, FilePath* result);
#endif

namespace {

typedef std::unordered_map<int, FilePath> PathMap;

// We keep a linked list of providers.  In a debug build we ensure that no two
// providers claim overlapping keys.
struct Provider {
  PathService::ProviderFunc func;
  struct Provider* next;
#ifndef NDEBUG
  int key_start;
  int key_end;
#endif
  bool is_static;
};

Provider base_provider = {PathProvider, nullptr,
#ifndef NDEBUG
                          PATH_START, PATH_END,
#endif
                          true};

#if BUILDFLAG(IS_WIN)
Provider base_provider_win = {
  PathProviderWin,
  &base_provider,
#ifndef NDEBUG
  PATH_WIN_START,
  PATH_WIN_END,
#endif
  true
};
#endif

#if BUILDFLAG(IS_APPLE)
Provider base_provider_mac = {
  PathProviderMac,
  &base_provider,
#ifndef NDEBUG
  PATH_MAC_START,
  PATH_MAC_END,
#endif
  true
};
#endif

#if BUILDFLAG(IS_ANDROID)
Provider base_provider_android = {
  PathProviderAndroid,
  &base_provider,
#ifndef NDEBUG
  PATH_ANDROID_START,
  PATH_ANDROID_END,
#endif
  true
};
#endif

#if BUILDFLAG(IS_FUCHSIA)
Provider base_provider_fuchsia = {PathProviderFuchsia, &base_provider,
#ifndef NDEBUG
                                  0, 0,
#endif
                                  true};
#endif

#if BUILDFLAG(IS_POSIX) && !BUILDFLAG(IS_APPLE) && !BUILDFLAG(IS_ANDROID)
Provider base_provider_posix = {
  PathProviderPosix,
  &base_provider,
#ifndef NDEBUG
  PATH_POSIX_START,
  PATH_POSIX_END,
#endif
  true
};
#endif


struct PathData {
  Lock lock;
  PathMap cache;        // Cache mappings from path key to path value.
  PathMap overrides;    // Track path overrides.
  raw_ptr<Provider> providers;  // Linked list of path service providers.
  bool cache_disabled;  // Don't use cache if true;

  PathData() : cache_disabled(false) {
#if BUILDFLAG(IS_WIN)
    providers = &base_provider_win;
#elif BUILDFLAG(IS_APPLE)
    providers = &base_provider_mac;
#elif BUILDFLAG(IS_ANDROID)
    providers = &base_provider_android;
#elif BUILDFLAG(IS_FUCHSIA)
    providers = &base_provider_fuchsia;
#elif BUILDFLAG(IS_POSIX)
    providers = &base_provider_posix;
#endif
  }
};

static PathData* GetPathData() {
  static auto* path_data = new PathData();
  return path_data;
}

// Tries to find |key| in the cache.
bool LockedGetFromCache(int key, const PathData* path_data, FilePath* result)
    EXCLUSIVE_LOCKS_REQUIRED(path_data->lock) {
  if (path_data->cache_disabled)
    return false;
  // check for a cached version
  auto it = path_data->cache.find(key);
  if (it != path_data->cache.end()) {
    *result = it->second;
    return true;
  }
  return false;
}

// Tries to find |key| in the overrides map.
bool LockedGetFromOverrides(int key, PathData* path_data, FilePath* result)
    EXCLUSIVE_LOCKS_REQUIRED(path_data->lock) {
  // check for an overridden version.
  PathMap::const_iterator it = path_data->overrides.find(key);
  if (it != path_data->overrides.end()) {
    if (!path_data->cache_disabled)
      path_data->cache[key] = it->second;
    *result = it->second;
    return true;
  }
  return false;
}

}  // namespace

// TODO(brettw): this function does not handle long paths (filename > MAX_PATH)
// characters). This isn't supported very well by Windows right now, so it is
// moot, but we should keep this in mind for the future.
// static
bool PathService::Get(int key, FilePath* result) {
  PathData* path_data = GetPathData();
  DCHECK(path_data);
  DCHECK(result);
  DCHECK_GT(key, PATH_START);

  // Special case the current directory because it can never be cached.
  if (key == DIR_CURRENT)
    return GetCurrentDirectory(result);

  Provider* provider = nullptr;
  {
    AutoLock scoped_lock(path_data->lock);
    if (LockedGetFromCache(key, path_data, result))
      return true;

    if (LockedGetFromOverrides(key, path_data, result))
      return true;

    // Get the beginning of the list while it is still locked.
    provider = path_data->providers;
  }

  FilePath path;

  // Iterating does not need the lock because only the list head might be
  // modified on another thread.
  while (provider) {
    if (provider->func(key, &path))
      break;
    DCHECK(path.empty()) << "provider should not have modified path";
    provider = provider->next;
  }

  if (path.empty())
    return false;

  if (path.ReferencesParent()) {
    // Make sure path service never returns a path with ".." in it.
    path = MakeAbsoluteFilePath(path);
    if (path.empty())
      return false;
  }
  *result = path;

  AutoLock scoped_lock(path_data->lock);
  if (!path_data->cache_disabled)
    path_data->cache[key] = path;

  return true;
}

FilePath PathService::CheckedGet(int key) {
  FilePath path;
  LOG_IF(FATAL, !Get(key, &path)) << "Failed to get the path for " << key;
  return path;
}

// static
bool PathService::Override(int key, const FilePath& path) {
  // Just call the full function with true for the value of |create|, and
  // assume that |path| may not be absolute yet.
  return OverrideAndCreateIfNeeded(key, path, false, true);
}

// static
bool PathService::OverrideAndCreateIfNeeded(int key,
                                            const FilePath& path,
                                            bool is_absolute,
                                            bool create) {
  PathData* path_data = GetPathData();
  DCHECK(path_data);
  DCHECK_GT(key, PATH_START) << "invalid path key";

  FilePath file_path = path;

  // For some locations this will fail if called from inside the sandbox there-
  // fore we protect this call with a flag.
  if (create) {
    // Make sure the directory exists. We need to do this before we translate
    // this to the absolute path because on POSIX, MakeAbsoluteFilePath fails
    // if called on a non-existent path.
    if (!PathExists(file_path) && !CreateDirectory(file_path))
      return false;
  }

  // We need to have an absolute path.
  if (!is_absolute) {
    file_path = MakeAbsoluteFilePath(file_path);
    if (file_path.empty())
      return false;
  }
  DCHECK(file_path.IsAbsolute());

  AutoLock scoped_lock(path_data->lock);

  // Clear the cache now. Some of its entries could have depended
  // on the value we are overriding, and are now out of sync with reality.
  path_data->cache.clear();

  path_data->overrides[key] = file_path;

  return true;
}

// static
bool PathService::RemoveOverrideForTests(int key) {
  PathData* path_data = GetPathData();
  DCHECK(path_data);

  AutoLock scoped_lock(path_data->lock);

  if (path_data->overrides.find(key) == path_data->overrides.end())
    return false;

  // Clear the cache now. Some of its entries could have depended on the value
  // we are going to remove, and are now out of sync.
  path_data->cache.clear();

  path_data->overrides.erase(key);

  return true;
}

// static
bool PathService::IsOverriddenForTests(int key) {
  PathData* path_data = GetPathData();
  DCHECK(path_data);

  AutoLock scoped_lock(path_data->lock);

  return path_data->overrides.find(key) != path_data->overrides.end();
}

// static
void PathService::RegisterProvider(ProviderFunc func, int key_start,
                                   int key_end) {
  PathData* path_data = GetPathData();
  DCHECK(path_data);
  DCHECK_GT(key_end, key_start);

  Provider* p;

  p = new Provider;
  p->is_static = false;
  p->func = func;
#ifndef NDEBUG
  p->key_start = key_start;
  p->key_end = key_end;
#endif

  AutoLock scoped_lock(path_data->lock);

#ifndef NDEBUG
  Provider *iter = path_data->providers;
  while (iter) {
    DCHECK(key_start >= iter->key_end || key_end <= iter->key_start) <<
      "path provider collision";
    iter = iter->next;
  }
#endif

  p->next = path_data->providers;
  path_data->providers = p;
}

// static
void PathService::DisableCache() {
  PathData* path_data = GetPathData();
  DCHECK(path_data);

  AutoLock scoped_lock(path_data->lock);
  path_data->cache.clear();
  path_data->cache_disabled = true;
}

}  // namespace base
