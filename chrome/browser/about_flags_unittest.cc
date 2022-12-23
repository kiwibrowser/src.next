// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/about_flags.h"

#include <stddef.h>

#include <map>
#include <set>
#include <string>
#include <utility>

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/test/metrics/histogram_enum_reader.h"
#include "build/build_config.h"
#include "chrome/common/chrome_version.h"
#include "components/flags_ui/feature_entry.h"
#include "components/flags_ui/feature_entry_macros.h"
#include "components/flags_ui/flags_test_helpers.h"
#include "components/flags_ui/flags_ui_metrics.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace about_flags {

namespace {

using Sample = base::HistogramBase::Sample;
using SwitchToIdMap = std::map<std::string, Sample>;

// Get all associated switches corresponding to defined about_flags.cc entries.
std::set<std::string> GetAllPublicSwitchesAndFeaturesForTesting() {
  std::set<std::string> result;

  for (const auto& entry : testing::GetFeatureEntries()) {
    // Skip over flags that are part of the flags system itself - they don't
    // have any of the usual metadata or histogram entries for flags, since they
    // are synthesized during the build process.
    // TODO(https://crbug.com/1068258): Remove the need for this by generating
    // histogram entries automatically.
    if (entry.supported_platforms & flags_ui::kFlagInfrastructure)
      continue;

    switch (entry.type) {
      case flags_ui::FeatureEntry::SINGLE_VALUE:
      case flags_ui::FeatureEntry::SINGLE_DISABLE_VALUE:
        result.insert(entry.switches.command_line_switch);
        break;
      case flags_ui::FeatureEntry::ORIGIN_LIST_VALUE:
        // Do nothing, origin list values are not added as feature flags.
        break;
      case flags_ui::FeatureEntry::MULTI_VALUE:
        for (int j = 0; j < entry.NumOptions(); ++j) {
          result.insert(entry.ChoiceForOption(j).command_line_switch);
        }
        break;
      case flags_ui::FeatureEntry::ENABLE_DISABLE_VALUE:
        result.insert(entry.switches.command_line_switch);
        result.insert(entry.switches.disable_command_line_switch);
        break;
      case flags_ui::FeatureEntry::FEATURE_VALUE:
      case flags_ui::FeatureEntry::FEATURE_WITH_PARAMS_VALUE:
        result.insert(std::string(entry.feature.feature->name) + ":enabled");
        result.insert(std::string(entry.feature.feature->name) + ":disabled");
        break;
#if BUILDFLAG(IS_CHROMEOS_ASH)
      case flags_ui::FeatureEntry::PLATFORM_FEATURE_NAME_VALUE:
      case flags_ui::FeatureEntry::PLATFORM_FEATURE_NAME_WITH_PARAMS_VALUE:
        std::string name(entry.platform_feature_name.name);
        result.insert(name + ":enabled");
        result.insert(name + ":disabled");
        break;
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)
    }
  }
  return result;
}

// Returns all variation ids defined in flags entries.
std::vector<std::string> GetAllVariationIds() {
  std::vector<std::string> variation_ids;
  for (const auto& entry : testing::GetFeatureEntries()) {
    // Only FEATURE_WITH_PARAMS_VALUE or PLATFORM_FEATURE_NAME_WITH_PARAMS_VALUE
    // entries can have a variation id.
    if (entry.type != flags_ui::FeatureEntry::FEATURE_WITH_PARAMS_VALUE
#if BUILDFLAG(IS_CHROMEOS_ASH)
        && entry.type !=
               flags_ui::FeatureEntry::PLATFORM_FEATURE_NAME_WITH_PARAMS_VALUE
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)
    ) {
      continue;
    }

    for (const auto& variation : entry.GetVariations()) {
      if (variation.variation_id)
        variation_ids.push_back(variation.variation_id);
    }
  }
  return variation_ids;
}

// Returns the parsed |variation_id|. If it is malformed, returns absl::nullopt.
absl::optional<int> ParseVariationId(const std::string& variation_id) {
  // Remove the "t" prefix if it is there.
  std::string trimmed_id =
      base::StartsWith(variation_id, "t", base::CompareCase::SENSITIVE)
          ? variation_id.substr(1)
          : variation_id;
  int id;
  if (base::StringToInt(trimmed_id, &id) && id >= 0)
    return id;
  return absl::nullopt;
}

}  // namespace

// Makes sure there are no separators in any of the entry names.
TEST(AboutFlagsTest, NoSeparators) {
  for (const auto& entry : testing::GetFeatureEntries()) {
    const std::string name(entry.internal_name);
    EXPECT_EQ(std::string::npos, name.find(flags_ui::testing::kMultiSeparator))
        << name;
  }
}

// Makes sure that every flag has an owner and an expiry entry in
// flag-metadata.json.
TEST(AboutFlagsTest, EveryFlagHasMetadata) {
  flags_ui::testing::EnsureEveryFlagHasMetadata(testing::GetFeatureEntries());
}

// Ensures that all flags marked as never expiring in flag-metadata.json is
// listed in flag-never-expire-list.json.
TEST(AboutFlagsTest, OnlyPermittedFlagsNeverExpire) {
  flags_ui::testing::EnsureOnlyPermittedFlagsNeverExpire();
}

// Ensures that every flag has an owner.
TEST(AboutFlagsTest, EveryFlagHasNonEmptyOwners) {
  flags_ui::testing::EnsureEveryFlagHasNonEmptyOwners();
}

// Ensures that owners conform to rules in flag-metadata.json.
TEST(AboutFlagsTest, OwnersLookValid) {
  flags_ui::testing::EnsureOwnersLookValid();
}

// For some bizarre reason, far too many people see a file filled with
// alphabetically-ordered items and think "hey, let me drop this new item into a
// random location!" Prohibit such behavior in the flags files.
TEST(AboutFlagsTest, FlagsListedInAlphabeticalOrder) {
  flags_ui::testing::EnsureFlagsAreListedInAlphabeticalOrder();
}

TEST(AboutFlagsTest, EveryFlagIsValid) {
  for (const auto& entry : testing::GetFeatureEntries()) {
    EXPECT_TRUE(entry.IsValid()) << entry.internal_name;
  }
}

TEST(AboutFlagsTest, RecentUnexpireFlagsArePresent) {
  flags_ui::testing::EnsureRecentUnexpireFlagsArePresent(
      testing::GetFeatureEntries(), CHROME_VERSION_MAJOR);
}

// Ensures that all variation IDs specified are well-formed and unique.
TEST(AboutFlagsTest, VariationIdsAreValid) {
  std::set<int> variation_ids;

  for (const std::string& variation_id : GetAllVariationIds()) {
    absl::optional<int> id = ParseVariationId(variation_id);
    EXPECT_TRUE(id)
        << "Variation ID \"" << variation_id
        << "\" is malformed. It must be a nonnegative integer and can "
           "optionally start with a \"t\".";

    if (id.has_value()) {
      EXPECT_TRUE(variation_ids.insert(id.value()).second)
          << "Variation ID " << variation_id << " is duplicated.";
    }
  }
}

// Test that ScopedFeatureEntries restores existing feature entries on
// destruction.
TEST(AboutFlagsTest, ScopedFeatureEntriesRestoresFeatureEntries) {
  const base::span<const flags_ui::FeatureEntry> old_entries =
      testing::GetFeatureEntries();
  EXPECT_GT(old_entries.size(), 0U);
  const char* first_feature_name = old_entries[0].internal_name;
  {
    const base::Feature kTestFeature1{"FeatureName1",
                                      base::FEATURE_ENABLED_BY_DEFAULT};
    testing::ScopedFeatureEntries feature_entries(
        {{"feature-1", "", "", flags_ui::FlagsState::GetCurrentPlatform(),
          FEATURE_VALUE_TYPE(kTestFeature1)}});
    EXPECT_EQ(testing::GetFeatureEntries().size(), 1U);
  }

  const base::span<const flags_ui::FeatureEntry> new_entries =
      testing::GetFeatureEntries();
  EXPECT_EQ(old_entries.size(), new_entries.size());
  EXPECT_TRUE(about_flags::GetCurrentFlagsState()->FindFeatureEntryByName(
      first_feature_name));
}

class AboutFlagsHistogramTest : public ::testing::Test {
 protected:
  // This is a helper function to check that all IDs in enum LoginCustomFlags in
  // histograms.xml are unique.
  void SetSwitchToHistogramIdMapping(const std::string& switch_name,
                                     const Sample switch_histogram_id,
                                     std::map<std::string, Sample>* out_map) {
    const std::pair<std::map<std::string, Sample>::iterator, bool> status =
        out_map->insert(std::make_pair(switch_name, switch_histogram_id));
    if (!status.second) {
      EXPECT_TRUE(status.first->second == switch_histogram_id)
          << "Duplicate switch '" << switch_name
          << "' found in enum 'LoginCustomFlags' in "
             "tools/metrics/histograms/enums.xml.";
    }
  }

  // This method generates a hint for the user for what string should be added
  // to the enum LoginCustomFlags to make in consistent.
  std::string GetHistogramEnumEntryText(const std::string& switch_name,
                                        Sample value) {
    return base::StringPrintf(
        "<int value=\"%d\" label=\"%s\"/>", value, switch_name.c_str());
  }
};

TEST_F(AboutFlagsHistogramTest, CheckHistograms) {
  absl::optional<base::HistogramEnumEntryMap> login_custom_flags =
      base::ReadEnumFromEnumsXml("LoginCustomFlags");
  ASSERT_TRUE(login_custom_flags)
      << "Error reading enum 'LoginCustomFlags' from "
         "tools/metrics/histograms/enums.xml.";

  // Build reverse map {switch_name => id} from login_custom_flags.
  SwitchToIdMap metadata_switches_ids;

  EXPECT_TRUE(
      login_custom_flags->count(flags_ui::testing::kBadSwitchFormatHistogramId))
      << "Entry for UMA ID of incorrect command-line flag is not found in "
         "tools/metrics/histograms/enums.xml enum LoginCustomFlags. "
         "Consider adding entry:\n"
      << "  " << GetHistogramEnumEntryText("BAD_FLAG_FORMAT", 0);
  // Check that all LoginCustomFlags entries have correct values.
  for (const auto& entry : *login_custom_flags) {
    if (entry.first == flags_ui::testing::kBadSwitchFormatHistogramId) {
      // Add error value with empty name.
      SetSwitchToHistogramIdMapping(std::string(), entry.first,
                                    &metadata_switches_ids);
      continue;
    }
    const Sample uma_id = flags_ui::GetSwitchUMAId(entry.second);
    EXPECT_EQ(uma_id, entry.first)
        << "tools/metrics/histograms/enums.xml enum LoginCustomFlags "
           "entry '"
        << entry.second << "' has incorrect value=" << entry.first << ", but "
        << uma_id << " is expected. Consider changing entry to:\n"
        << "  " << GetHistogramEnumEntryText(entry.second, uma_id);
    SetSwitchToHistogramIdMapping(entry.second, entry.first,
                                  &metadata_switches_ids);
  }

  // Check that all flags in about_flags.cc have entries in login_custom_flags.
  std::set<std::string> all_flags = GetAllPublicSwitchesAndFeaturesForTesting();
  for (const std::string& flag : all_flags) {
    // Skip empty placeholders.
    if (flag.empty())
      continue;
    const Sample uma_id = flags_ui::GetSwitchUMAId(flag);
    EXPECT_NE(flags_ui::testing::kBadSwitchFormatHistogramId, uma_id)
        << "Command-line switch '" << flag
        << "' from about_flags.cc has UMA ID equal to reserved value "
           "kBadSwitchFormatHistogramId="
        << flags_ui::testing::kBadSwitchFormatHistogramId
        << ". Please modify switch name.";
    auto enum_entry = metadata_switches_ids.lower_bound(flag);

    // Ignore case here when switch ID is incorrect - it has already been
    // reported in the previous loop.
    EXPECT_TRUE(enum_entry != metadata_switches_ids.end() &&
                enum_entry->first == flag)
        << "tools/metrics/histograms/enums.xml enum LoginCustomFlags doesn't "
           "contain switch '"
        << flag << "' (value=" << uma_id
        << " expected). Consider adding entry:\n"
        << "  " << GetHistogramEnumEntryText(flag, uma_id);
  }
}

}  // namespace about_flags
