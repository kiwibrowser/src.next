// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/location.h"

#include "base/debug/debugging_buildflags.h"
#include "base/trace_event/base_tracing.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

#if BUILDFLAG(ENABLE_BASE_TRACING)
#include "third_party/perfetto/include/perfetto/test/traced_value_test_support.h"  // no-presubmit-check
#endif  // BUILDFLAG(ENABLE_BASE_TRACING)

namespace base {

namespace {

// This is a typical use: taking Location::Current as a default parameter.
// So even though this looks contrived, it confirms that such usage works as
// expected.
Location WhereAmI(const Location& location = Location::Current()) {
  return location;
}

}  // namespace

TEST(LocationTest, CurrentYieldsCorrectValue) {
  [[maybe_unused]] int previous_line = __LINE__;
  Location here = WhereAmI();
  EXPECT_NE(here.program_counter(), WhereAmI().program_counter());
#if SUPPORTS_LOCATION_BUILTINS
  EXPECT_THAT(here.file_name(), ::testing::EndsWith("location_unittest.cc"));
#if BUILDFLAG(ENABLE_LOCATION_SOURCE)
  EXPECT_EQ(here.line_number(), previous_line + 1);
  EXPECT_STREQ("TestBody", here.function_name());
#endif
#elif defined(OFFICIAL_BUILD)
#error Location builtins must be supported in official builds.
#elif BUILDFLAG(FROM_HERE_USES_LOCATION_BUILTINS)
#error FROM_HERE requires location builtins to be supported.
#endif
}

#if BUILDFLAG(ENABLE_BASE_TRACING)
TEST(LocationTest, TracingSupport) {
  EXPECT_EQ(perfetto::TracedValueToString(
                Location("func", "file", 42, WhereAmI().program_counter())),
            "{function_name:func,file_name:file,line_number:42}");
}
#endif  // BUILDFLAG(ENABLE_BASE_TRACING)

}  // namespace base
