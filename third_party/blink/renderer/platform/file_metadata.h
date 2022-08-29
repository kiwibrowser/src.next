/*
 * Copyright (C) 2010 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef THIRD_PARTY_BLINK_RENDERER_PLATFORM_FILE_METADATA_H_
#define THIRD_PARTY_BLINK_RENDERER_PLATFORM_FILE_METADATA_H_

#include "base/files/file.h"
#include "base/time/time.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/renderer/platform/platform_export.h"
#include "third_party/blink/renderer/platform/weborigin/kurl.h"
#include "third_party/blink/renderer/platform/wtf/allocator/allocator.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"

namespace blink {

class MojoBindingContext;

class FileMetadata {
  DISALLOW_NEW();

 public:
  FileMetadata() = default;

  PLATFORM_EXPORT static FileMetadata From(const base::File::Info& file_info);

  // The last modification time of the file.
  absl::optional<base::Time> modification_time = absl::nullopt;

  // The length of the file in bytes.
  // The value -1 means that the length is not set.
  int64_t length = -1;

  enum Type { kTypeUnknown = 0, kTypeFile, kTypeDirectory };

  Type type = kTypeUnknown;
  String platform_path;
};

PLATFORM_EXPORT bool GetFileSize(const String&,
                                 const MojoBindingContext&,
                                 int64_t& result);
PLATFORM_EXPORT bool GetFileMetadata(const String&,
                                     const MojoBindingContext&,
                                     FileMetadata& result);
PLATFORM_EXPORT KURL FilePathToURL(const String&);

inline absl::optional<base::Time> NullableTimeToOptionalTime(base::Time time) {
  if (time.is_null())
    return absl::nullopt;
  return time;
}

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_PLATFORM_FILE_METADATA_H_
