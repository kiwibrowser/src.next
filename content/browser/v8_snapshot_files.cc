// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/v8_snapshot_files.h"

#include "build/build_config.h"
#include "content/public/common/content_descriptor_keys.h"

namespace content {

std::map<std::string, base::FilePath> GetV8SnapshotFilesToPreload() {
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
#if defined(USE_V8_CONTEXT_SNAPSHOT)
  return {{kV8ContextSnapshotDataDescriptor,
           base::FilePath(FILE_PATH_LITERAL(V8_CONTEXT_SNAPSHOT_FILENAME))}};
#else
  return {{kV8SnapshotDataDescriptor,
           base::FilePath(FILE_PATH_LITERAL("snapshot_blob.bin"))}};
#endif
#elif BUILDFLAG(IS_ANDROID)
#if !defined(USE_V8_CONTEXT_SNAPSHOT)
  return {{kV8Snapshot64DataDescriptor,
           base::FilePath(FILE_PATH_LITERAL("assets/snapshot_blob_64.bin"))},
          {kV8Snapshot32DataDescriptor,
           base::FilePath(FILE_PATH_LITERAL("assets/snapshot_blob_32.bin"))}};
#elif defined(USE_V8_CONTEXT_SNAPSHOT)
  // For USE_V8_CONTEXT_SNAPSHOT, the renderer reads the files directly.
  return {};
#endif
#else
  return {};
#endif
}

}  // namespace content
