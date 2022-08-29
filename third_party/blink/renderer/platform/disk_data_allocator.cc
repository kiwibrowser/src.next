// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/platform/disk_data_allocator.h"

#include <algorithm>
#include <utility>

#include "base/logging.h"
#include "base/synchronization/lock.h"
#include "base/threading/thread_restrictions.h"
#include "third_party/blink/renderer/platform/disk_data_metadata.h"
#include "third_party/blink/renderer/platform/wtf/std_lib_extras.h"
#include "third_party/blink/renderer/platform/wtf/wtf.h"

namespace blink {

DiskDataAllocator::DiskDataAllocator()
    : free_chunks_size_(0), file_tail_(0), may_write_(false) {}

DiskDataAllocator::~DiskDataAllocator() = default;

bool DiskDataAllocator::may_write() {
  base::AutoLock locker(lock_);
  return may_write_;
}

void DiskDataAllocator::set_may_write_for_testing(bool may_write) {
  base::AutoLock locker(lock_);
  may_write_ = may_write;
}

DiskDataMetadata DiskDataAllocator::FindChunk(size_t size) {
  // Try to reuse some space. Policy:
  // 1. Exact fit
  // 2. Worst fit
  DiskDataMetadata chosen_chunk{-1, 0};

  size_t worst_fit_size = 0;
  for (const auto& chunk : free_chunks_) {
    size_t chunk_size = chunk.second;
    if (size == chunk_size) {
      chosen_chunk = {chunk.first, chunk.second};
      break;
    } else if (chunk_size > size && chunk_size > worst_fit_size) {
      chosen_chunk = {chunk.first, chunk.second};
      worst_fit_size = chunk.second;
    }
  }

  if (chosen_chunk.start_offset() != -1) {
    free_chunks_size_ -= size;
    free_chunks_.erase(chosen_chunk.start_offset());
    if (chosen_chunk.size() > size) {
      std::pair<int64_t, size_t> remainder_chunk = {
          chosen_chunk.start_offset() + size, chosen_chunk.size() - size};
      auto result = free_chunks_.insert(remainder_chunk);
      DCHECK(result.second);
      chosen_chunk.size_ = size;
    }
  } else {
    chosen_chunk = {file_tail_, size};
    file_tail_ += size;
  }

  return chosen_chunk;
}

void DiskDataAllocator::ReleaseChunk(const DiskDataMetadata& metadata) {
  DiskDataMetadata chunk = metadata;
  DCHECK(free_chunks_.find(chunk.start_offset()) == free_chunks_.end());

  auto lower_bound = free_chunks_.lower_bound(chunk.start_offset());
  DCHECK(free_chunks_.upper_bound(chunk.start_offset()) ==
         free_chunks_.lower_bound(chunk.start_offset()));
  if (lower_bound != free_chunks_.begin()) {
    // There is a chunk left.
    auto left = --lower_bound;
    // Can merge with the left chunk.
    int64_t left_chunk_end = left->first + left->second;
    DCHECK_LE(left_chunk_end, chunk.start_offset());
    if (left_chunk_end == chunk.start_offset()) {
      chunk = {left->first, left->second + chunk.size()};
      free_chunks_size_ -= left->second;
      free_chunks_.erase(left);
    }
  }

  auto right = free_chunks_.upper_bound(chunk.start_offset());
  if (right != free_chunks_.end()) {
    DCHECK_NE(right->first, chunk.start_offset());
    int64_t chunk_end = chunk.start_offset() + chunk.size();
    DCHECK_LE(chunk_end, right->first);
    if (right->first == chunk_end) {
      chunk = {chunk.start_offset(), chunk.size() + right->second};
      free_chunks_size_ -= right->second;
      free_chunks_.erase(right);
    }
  }

  auto result = free_chunks_.insert({chunk.start_offset(), chunk.size()});
  DCHECK(result.second);
  free_chunks_size_ += chunk.size();
}

std::unique_ptr<DiskDataMetadata> DiskDataAllocator::Write(const void* data,
                                                           size_t size) {
  DiskDataMetadata chosen_chunk = {0, 0};

  {
    base::AutoLock locker(lock_);
    if (!may_write_)
      return nullptr;

    chosen_chunk = FindChunk(size);
  }  // Don't hold the lock during the actual Write().

  int size_int = static_cast<int>(size);
  const char* data_char = reinterpret_cast<const char*>(data);
  int written = DoWrite(chosen_chunk.start_offset(), data_char, size_int);

  base::AutoLock locker(lock_);
  if (size_int != written) {
    // Assume that the error is not transient. This can happen if the disk is
    // full for instance, in which case it is likely better not to try writing
    // later.
    may_write_ = false;
    return nullptr;
  }

#if DCHECK_IS_ON()
  allocated_chunks_.insert({chosen_chunk.start_offset(), chosen_chunk.size()});
#endif

  return std::unique_ptr<DiskDataMetadata>(
      new DiskDataMetadata(chosen_chunk.start_offset(), chosen_chunk.size()));
}

void DiskDataAllocator::Read(const DiskDataMetadata& metadata, void* data) {
  // Doesn't need locking as files support concurrent access, and we don't
  // update metadata.
  char* data_char = reinterpret_cast<char*>(data);
  DoRead(metadata.start_offset(), data_char,
         base::checked_cast<int>(metadata.size()));

#if DCHECK_IS_ON()
  {
    base::AutoLock locker(lock_);
    auto it = allocated_chunks_.find(metadata.start_offset());
    DCHECK(it != allocated_chunks_.end());
    DCHECK_EQ(metadata.size(), it->second);
  }
#endif
}

void DiskDataAllocator::Discard(std::unique_ptr<DiskDataMetadata> metadata) {
  base::AutoLock locker(lock_);
  DCHECK(may_write_ || file_.IsValid());

#if DCHECK_IS_ON()
  auto it = allocated_chunks_.find(metadata->start_offset());
  DCHECK(it != allocated_chunks_.end());
  DCHECK_EQ(metadata->size(), it->second);
  allocated_chunks_.erase(it);
#endif

  ReleaseChunk(*metadata);
}

int DiskDataAllocator::DoWrite(int64_t offset, const char* data, int size) {
  int rv = file_.Write(offset, data, size);

  // No PCHECK(), since a file writing error is recoverable.
  if (rv != size) {
    LOG(ERROR) << "DISK: Cannot write to disk. written = " << rv << " "
               << base::File::ErrorToString(base::File::GetLastFileError());
  }
  return rv;
}

void DiskDataAllocator::DoRead(int64_t offset, char* data, int size) {
  // This happens on the main thread, which is typically not allowed. This is
  // fine as this is expected to happen rarely, and only be slow with memory
  // pressure, in which case writing to/reading from disk is better than
  // swapping out random parts of the memory. See crbug.com/1029320 for details.
  base::ScopedAllowBlocking allow_blocking;
  int rv = file_.Read(offset, data, size);
  // Can only crash, since we cannot continue without the data.
  PCHECK(rv == size) << "Likely file corruption.";
}

void DiskDataAllocator::ProvideTemporaryFile(base::File file) {
  base::AutoLock locker(lock_);
  DCHECK(IsMainThread());
  DCHECK(!file_.IsValid());
  DCHECK(!may_write_);

  file_ = std::move(file);
  may_write_ = file_.IsValid();
}

// static
DiskDataAllocator& DiskDataAllocator::Instance() {
  DEFINE_THREAD_SAFE_STATIC_LOCAL(DiskDataAllocator, instance, ());
  return instance;
}

// static
void DiskDataAllocator::Bind(
    mojo::PendingReceiver<mojom::blink::DiskAllocator> receiver) {
  DCHECK(!Instance().receiver_.is_bound());
  Instance().receiver_.Bind(std::move(receiver));
}

}  // namespace blink
