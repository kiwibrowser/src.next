// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/platform/disk_data_allocator.h"

#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/rand_util.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/renderer/platform/disk_data_allocator_test_utils.h"
#include "third_party/blink/renderer/platform/disk_data_metadata.h"

using ThreadPoolExecutionMode =
    base::test::TaskEnvironment::ThreadPoolExecutionMode;

namespace blink {

class DiskDataAllocatorTest : public ::testing::Test {
 public:
  explicit DiskDataAllocatorTest(
      ThreadPoolExecutionMode thread_pool_execution_mode =
          ThreadPoolExecutionMode::DEFAULT)
      : task_environment_(base::test::TaskEnvironment::TimeSource::MOCK_TIME,
                          thread_pool_execution_mode) {}

  static std::vector<std::unique_ptr<DiskDataMetadata>>
  Allocate(InMemoryDataAllocator* allocator, size_t size, size_t count) {
    std::string random_data = base::RandBytesAsString(size);

    std::vector<std::unique_ptr<DiskDataMetadata>> all_metadata;
    for (size_t i = 0; i < count; i++) {
      auto metadata = allocator->Write(random_data.c_str(), random_data.size());
      EXPECT_EQ(metadata->start_offset(), static_cast<int64_t>(i * size));
      all_metadata.push_back(std::move(metadata));
    }
    return all_metadata;
  }

 protected:
  void SetUp() override {
    // On some platforms, initialization takes time, though it happens when
    // base::ThreadTicks is used. To prevent flakiness depending on test
    // execution ordering, force initialization.
    if (base::ThreadTicks::IsSupported())
      base::ThreadTicks::WaitUntilInitialized();
  }

  base::test::TaskEnvironment task_environment_;
};

TEST_F(DiskDataAllocatorTest, ReadWrite) {
  InMemoryDataAllocator allocator;

  constexpr size_t kSize = 1000;
  std::string random_data = base::RandBytesAsString(kSize);
  auto metadata = allocator.Write(random_data.c_str(), random_data.size());
  EXPECT_TRUE(metadata);
  EXPECT_EQ(kSize, metadata->size());

  auto read_data = std::vector<char>(kSize);
  allocator.Read(*metadata, &read_data[0]);

  EXPECT_EQ(0, memcmp(&read_data[0], random_data.c_str(), kSize));
}

TEST_F(DiskDataAllocatorTest, ReadWriteDiscardMultiple) {
  InMemoryDataAllocator allocator;

  std::vector<std::pair<std::unique_ptr<DiskDataMetadata>, std::string>>
      data_written;

  for (int i = 0; i < 10; i++) {
    int size = base::RandInt(100, 1000);
    auto data = base::RandBytesAsString(size);
    auto metadata = allocator.Write(&data[0], size);
    ASSERT_TRUE(metadata);
    data_written.emplace_back(std::move(metadata), data);
  }

  base::RandomShuffle(data_written.begin(), data_written.end());

  for (const auto& p : data_written) {
    size_t size = p.first->size();
    auto read_data = std::vector<char>(size);
    allocator.Read(*p.first, &read_data[0]);

    EXPECT_EQ(0, memcmp(&read_data[0], &p.second[0], size));
  }

  base::RandomShuffle(data_written.begin(), data_written.end());

  for (auto& p : data_written) {
    auto metadata = std::move(p.first);
    allocator.Discard(std::move(metadata));
  }
}

TEST_F(DiskDataAllocatorTest, WriteEventuallyFail) {
  InMemoryDataAllocator allocator;

  constexpr size_t kSize = 1 << 18;
  std::string random_data = base::RandBytesAsString(kSize);

  static_assert(4 * kSize == InMemoryDataAllocator::kMaxSize, "");
  for (int i = 0; i < 4; i++) {
    auto metadata = allocator.Write(random_data.c_str(), random_data.size());
    EXPECT_TRUE(metadata);
  }
  auto metadata = allocator.Write(random_data.c_str(), random_data.size());
  EXPECT_FALSE(metadata);
  EXPECT_FALSE(allocator.may_write());
}

TEST_F(DiskDataAllocatorTest, CanReuseFreedChunk) {
  InMemoryDataAllocator allocator;

  constexpr size_t kSize = 1 << 10;
  std::vector<std::unique_ptr<DiskDataMetadata>> all_metadata;

  for (int i = 0; i < 10; i++) {
    std::string random_data = base::RandBytesAsString(kSize);
    auto metadata = allocator.Write(random_data.c_str(), random_data.size());
    ASSERT_TRUE(metadata);
    all_metadata.push_back(std::move(metadata));
  }

  auto metadata = std::move(all_metadata[4]);
  ASSERT_TRUE(metadata);
  int64_t start_offset = metadata->start_offset();
  allocator.Discard(std::move(metadata));

  std::string random_data = base::RandBytesAsString(kSize);
  auto new_metadata = allocator.Write(random_data.c_str(), random_data.size());
  ASSERT_TRUE(new_metadata);
  EXPECT_EQ(new_metadata->start_offset(), start_offset);
}

TEST_F(DiskDataAllocatorTest, ExactThenWorstFit) {
  InMemoryDataAllocator allocator;

  int count = 10;
  size_t size_increment = 1000;
  std::vector<std::unique_ptr<DiskDataMetadata>> all_metadata;

  size_t size = 10000;
  // Allocate a bunch of random-sized
  for (int i = 0; i < count; i++) {
    std::string random_data = base::RandBytesAsString(size);
    auto metadata = allocator.Write(random_data.c_str(), random_data.size());
    ASSERT_TRUE(metadata);
    all_metadata.push_back(std::move(metadata));
    size += size_increment;
  }

  auto& hole_metadata = all_metadata[4];
  size_t hole_size = hole_metadata->size();
  int64_t hole_offset = hole_metadata->start_offset();
  allocator.Discard(std::move(hole_metadata));

  auto& larger_hole_metadata = all_metadata[9];
  int64_t larger_hole_offset = larger_hole_metadata->start_offset();
  allocator.Discard(std::move(larger_hole_metadata));

  std::string random_data = base::RandBytesAsString(hole_size);
  auto metadata = allocator.Write(random_data.c_str(), random_data.size());
  // Exact fit.
  EXPECT_EQ(metadata->start_offset(), hole_offset);
  allocator.Discard(std::move(metadata));

  // -1 to check that this is not best fit.
  random_data = base::RandBytesAsString(hole_size - 1);
  metadata = allocator.Write(random_data.c_str(), random_data.size());
  EXPECT_EQ(metadata->start_offset(), larger_hole_offset);
}

TEST_F(DiskDataAllocatorTest, FreeChunksMerging) {
  constexpr size_t kSize = 100;

  auto allocator = std::make_unique<InMemoryDataAllocator>();
  auto chunks = Allocate(allocator.get(), kSize, 4);
  EXPECT_EQ(static_cast<int64_t>(4 * kSize), allocator->disk_footprint());
  EXPECT_EQ(0u, allocator->free_chunks_size());

  // Layout is (indices in |chunks|):
  // | 0 | 1 | 2 | 3 |
  // Discarding a higher index after a lower one triggers merging on the left.

  // Merge left.
  allocator->Discard(std::move(chunks[0]));
  EXPECT_EQ(1u, allocator->FreeChunks().size());
  allocator->Discard(std::move(chunks[1]));
  EXPECT_EQ(1u, allocator->FreeChunks().size());
  EXPECT_EQ(2 * kSize, allocator->FreeChunks().begin()->second);
  allocator->Discard(std::move(chunks[2]));
  EXPECT_EQ(1u, allocator->FreeChunks().size());
  EXPECT_EQ(3 * kSize, allocator->FreeChunks().begin()->second);
  EXPECT_EQ(3 * kSize, allocator->free_chunks_size());
  allocator->Discard(std::move(chunks[3]));
  EXPECT_EQ(1u, allocator->FreeChunks().size());
  EXPECT_EQ(4 * kSize, allocator->FreeChunks().begin()->second);
  EXPECT_EQ(static_cast<int64_t>(4 * kSize), allocator->disk_footprint());

  allocator = std::make_unique<InMemoryDataAllocator>();
  chunks = Allocate(allocator.get(), kSize, 4);

  // Merge right.
  allocator->Discard(std::move(chunks[3]));
  EXPECT_EQ(1u, allocator->FreeChunks().size());
  allocator->Discard(std::move(chunks[2]));
  EXPECT_EQ(1u, allocator->FreeChunks().size());
  EXPECT_EQ(2 * kSize, allocator->FreeChunks().begin()->second);
  allocator->Discard(std::move(chunks[0]));
  EXPECT_EQ(2u, allocator->FreeChunks().size());
  EXPECT_EQ(3 * kSize, allocator->free_chunks_size());
  // Multiple merges: left, then right.
  allocator->Discard(std::move(chunks[1]));
  EXPECT_EQ(1u, allocator->FreeChunks().size());

  allocator = std::make_unique<InMemoryDataAllocator>();
  chunks = Allocate(allocator.get(), kSize, 4);

  // Left then right merging.
  allocator->Discard(std::move(chunks[0]));
  allocator->Discard(std::move(chunks[2]));
  EXPECT_EQ(2u, allocator->FreeChunks().size());
  allocator->Discard(std::move(chunks[1]));
  EXPECT_EQ(1u, allocator->FreeChunks().size());
}

TEST_F(DiskDataAllocatorTest, ProvideInvalidFile) {
  DiskDataAllocator allocator;
  EXPECT_FALSE(allocator.may_write());
  allocator.ProvideTemporaryFile(base::File());
  EXPECT_FALSE(allocator.may_write());
}

TEST_F(DiskDataAllocatorTest, ProvideValidFile) {
  base::FilePath path;
  if (!base::CreateTemporaryFile(&path))
    GTEST_SKIP() << "Cannot create temporary file.";

  int flags = base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_READ |
              base::File::FLAG_WRITE | base::File::FLAG_DELETE_ON_CLOSE;
  auto file = base::File(base::FilePath(path), flags);
  if (!file.IsValid())
    GTEST_SKIP() << "Cannot create temporary file.";

  DiskDataAllocator allocator;
  EXPECT_FALSE(allocator.may_write());
  allocator.ProvideTemporaryFile(std::move(file));
  EXPECT_TRUE(allocator.may_write());

  // Test read/write with a real file.
  constexpr size_t kSize = 1000;
  std::string random_data = base::RandBytesAsString(kSize);
  auto metadata = allocator.Write(random_data.c_str(), random_data.size());
  if (!metadata)
    GTEST_SKIP() << "Disk full?";

  EXPECT_EQ(kSize, metadata->size());

  auto read_data = std::vector<char>(kSize);
  allocator.Read(*metadata, &read_data[0]);

  EXPECT_EQ(0, memcmp(&read_data[0], random_data.c_str(), kSize));
}

}  // namespace blink
