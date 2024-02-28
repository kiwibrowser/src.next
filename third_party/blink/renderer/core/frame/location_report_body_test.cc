// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/frame/location_report_body.h"

#include <set>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace blink {
namespace {

class TestLocationReportBody : public LocationReportBody {
 public:
  explicit TestLocationReportBody(
      const String& source_file = g_empty_string,
      absl::optional<uint32_t> line_number = absl::nullopt,
      absl::optional<uint32_t> column_number = absl::nullopt)
      : LocationReportBody(source_file, line_number, column_number) {}
};

// Test whether LocationReportBody::MatchId() is a pure function, i.e. same
// input will give same return value.
TEST(LocationReportBodyMatchIdTest, SameInputGeneratesSameMatchId) {
  String url = "";
  absl::optional<uint32_t> line = absl::nullopt, column = absl::nullopt;
  EXPECT_EQ(TestLocationReportBody(url, line, column).MatchId(),
            TestLocationReportBody(url, line, column).MatchId());

  url = "https://example.com";
  line = absl::make_optional<uint32_t>(0);
  column = absl::make_optional<uint32_t>(0);
  EXPECT_EQ(TestLocationReportBody(url, line, column).MatchId(),
            TestLocationReportBody(url, line, column).MatchId());
}

bool AllDistinct(const std::vector<unsigned>& match_ids) {
  return match_ids.size() ==
         std::set<unsigned>(match_ids.begin(), match_ids.end()).size();
}

const struct {
  const char* url;
  const absl::optional<uint32_t> line_number;
  const absl::optional<uint32_t> column_number;
} kLocationReportBodyInputs[] = {
    {"url", absl::nullopt, absl::nullopt},
    {"url", 0, absl::nullopt},
    {"url", absl::nullopt, 0},
    {"url", 0, 0},
    {"url", 1, absl::nullopt},
    {"url", absl::nullopt, 1},
    {"url", 1, 1},
};

TEST(LocationReportBodyMatchIdTest, DifferentInputsGenerateDifferentMatchId) {
  std::vector<unsigned> match_ids;
  for (const auto& input : kLocationReportBodyInputs) {
    match_ids.push_back(TestLocationReportBody(input.url, input.line_number,
                                               input.column_number)
                            .MatchId());
  }
  EXPECT_TRUE(AllDistinct(match_ids));
}

TEST(LocationReportBodyMatchIdTest, MatchIdGeneratedShouldNotBeZero) {
  std::vector<unsigned> match_ids;
  for (const auto& input : kLocationReportBodyInputs) {
    EXPECT_NE(TestLocationReportBody(input.url, input.line_number,
                                     input.column_number)
                  .MatchId(),
              0u);
  }
}

// When URL is empty, LocationReportBody would call |CaptureSourceLocation()|
// to determine the location, and ignore |line_number| and |column_number|
// specified in constructor params.
TEST(LocationReportBodyMatchIdTest,
     EmptyURLGenerateSameMatchIdRegardlessOfOtherParams) {
  const unsigned empty_hash =
      TestLocationReportBody("", absl::nullopt, absl::nullopt).MatchId();
  for (const auto& input : kLocationReportBodyInputs) {
    EXPECT_EQ(TestLocationReportBody("", input.line_number, input.column_number)
                  .MatchId(),
              empty_hash);
  }
}

}  // namespace
}  // namespace blink
