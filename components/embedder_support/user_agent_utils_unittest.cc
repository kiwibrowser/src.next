// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/embedder_support/user_agent_utils.h"

#include "base/command_line.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/system/sys_info.h"
#include "base/test/gtest_util.h"
#include "base/test/scoped_command_line.h"
#include "base/test/scoped_feature_list.h"
#include "base/version.h"
#include "build/branding_buildflags.h"
#include "build/build_config.h"
#include "components/embedder_support/pref_names.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/testing_pref_service.h"
#include "components/version_info/version_info.h"
#include "content/public/common/content_features.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/user_agent.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/common/user_agent/user_agent_brand_version_type.h"
#include "third_party/blink/public/common/user_agent/user_agent_metadata.h"
#include "third_party/re2/src/re2/re2.h"

#if BUILDFLAG(IS_POSIX)
#include <sys/utsname.h>
#endif

#if BUILDFLAG(IS_WIN)
#include <windows.foundation.metadata.h>
#include <wrl.h>

#include "base/win/core_winrt_util.h"
#include "base/win/hstring_reference.h"
#include "base/win/scoped_hstring.h"
#include "base/win/scoped_winrt_initializer.h"
#include "base/win/windows_version.h"
#endif  // BUILDFLAG(IS_WIN)

namespace embedder_support {

namespace {

// A regular expression that matches Chrome/{major_version}.{minor_version} in
// the User-Agent string, where the first capture is the {major_version} and the
// second capture is the {minor_version}.
static constexpr char kChromeProductVersionRegex[] =
    "Chrome/([0-9]+).([0-9]+).([0-9]+).([0-9]+)";

#if BUILDFLAG(IS_ANDROID)
const char kAndroid[] =
    "Mozilla/5.0 (Linux; Android 10; K) AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/%s.0.0.0 "
    "%sSafari/537.36";
#else
const char kDesktop[] =
    "Mozilla/5.0 ("
#if BUILDFLAG(IS_CHROMEOS)
    "X11; CrOS x86_64 14541.0.0"
#elif BUILDFLAG(IS_FUCHSIA)
    "Fuchsia"
#elif BUILDFLAG(IS_LINUX)
    "X11; Linux x86_64"
#elif BUILDFLAG(IS_MAC)
    "Macintosh; Intel Mac OS X 10_15_7"
#elif BUILDFLAG(IS_WIN)
    "Windows NT 10.0; Win64; x64"
#else
#error Unsupported platform
#endif
    ") AppleWebKit/537.36 (KHTML, like Gecko) Chrome/%s.0.0.0 "
    "Safari/537.36";
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
std::string GetMachine() {
  struct utsname unixinfo;
  uname(&unixinfo);
  std::string machine = unixinfo.machine;
  if (strcmp(unixinfo.machine, "x86_64") == 0 &&
      sizeof(void*) == sizeof(int32_t)) {
    machine = "i686 (x86_64)";
  }
  return machine;
}
#endif

void CheckUserAgentStringOrdering(bool mobile_device) {
  std::vector<std::string> pieces;

  // Check if the pieces of the user agent string come in the correct order.
  std::string buffer = GetUserAgent();

  pieces = base::SplitStringUsingSubstr(
      buffer, "Mozilla/5.0 (", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  ASSERT_EQ(2u, pieces.size());
  buffer = pieces[1];
  EXPECT_EQ("", pieces[0]);

  pieces = base::SplitStringUsingSubstr(
      buffer, ") AppleWebKit/", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  ASSERT_EQ(2u, pieces.size());
  buffer = pieces[1];
  std::string os_str = pieces[0];

  pieces =
      base::SplitStringUsingSubstr(buffer, " (KHTML, like Gecko) ",
                                   base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  ASSERT_EQ(2u, pieces.size());
  buffer = pieces[1];
  std::string webkit_version_str = pieces[0];

  pieces = base::SplitStringUsingSubstr(
      buffer, " Safari/", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  ASSERT_EQ(2u, pieces.size());
  std::string product_str = pieces[0];
  std::string safari_version_str = pieces[1];

  EXPECT_FALSE(os_str.empty());

  pieces = base::SplitStringUsingSubstr(os_str, "; ", base::KEEP_WHITESPACE,
                                        base::SPLIT_WANT_ALL);
#if BUILDFLAG(IS_WIN)
  // Windows NT 10.0; Win64; x64
  // Windows NT 10.0; WOW64
  // Windows NT 10.0
  std::string os_and_version = pieces[0];
  for (unsigned int i = 1; i < pieces.size(); ++i) {
    bool equals = ((pieces[i] == "WOW64") || (pieces[i] == "Win64") ||
                   pieces[i] == "x64");
    ASSERT_TRUE(equals);
  }
  pieces = base::SplitStringUsingSubstr(pieces[0], " ", base::KEEP_WHITESPACE,
                                        base::SPLIT_WANT_ALL);
  ASSERT_EQ(3u, pieces.size());
  ASSERT_EQ("Windows", pieces[0]);
  ASSERT_EQ("NT", pieces[1]);
  double version;
  ASSERT_TRUE(base::StringToDouble(pieces[2], &version));
  ASSERT_LE(4.0, version);
  ASSERT_GT(11.0, version);
#elif BUILDFLAG(IS_MAC)
  // Macintosh; Intel Mac OS X 10_15_4
  ASSERT_EQ(2u, pieces.size());
  ASSERT_EQ("Macintosh", pieces[0]);
  pieces = base::SplitStringUsingSubstr(pieces[1], " ", base::KEEP_WHITESPACE,
                                        base::SPLIT_WANT_ALL);
  ASSERT_EQ(5u, pieces.size());
  ASSERT_EQ("Intel", pieces[0]);
  ASSERT_EQ("Mac", pieces[1]);
  ASSERT_EQ("OS", pieces[2]);
  ASSERT_EQ("X", pieces[3]);
  pieces = base::SplitStringUsingSubstr(pieces[4], "_", base::KEEP_WHITESPACE,
                                        base::SPLIT_WANT_ALL);
  {
    int major, minor, patch;
    base::SysInfo::OperatingSystemVersionNumbers(&major, &minor, &patch);
    // crbug.com/1175225
    if (major > 10)
      major = 10;
    ASSERT_EQ(base::StringPrintf("%d", major), pieces[0]);
  }
  int value;
  ASSERT_TRUE(base::StringToInt(pieces[1], &value));
  ASSERT_LE(0, value);
  ASSERT_TRUE(base::StringToInt(pieces[2], &value));
  ASSERT_LE(0, value);
#elif BUILDFLAG(IS_CHROMEOS)
  // X11; CrOS armv7l 4537.56.0
  ASSERT_EQ(2u, pieces.size());
  ASSERT_EQ("X11", pieces[0]);
  pieces = base::SplitStringUsingSubstr(pieces[1], " ", base::KEEP_WHITESPACE,
                                        base::SPLIT_WANT_ALL);
  ASSERT_EQ(3u, pieces.size());
  ASSERT_EQ("CrOS", pieces[0]);
  ASSERT_EQ(GetMachine(), pieces[1]);
  pieces = base::SplitStringUsingSubstr(pieces[2], ".", base::KEEP_WHITESPACE,
                                        base::SPLIT_WANT_ALL);
  for (unsigned int i = 1; i < pieces.size(); ++i) {
    int value;
    ASSERT_TRUE(base::StringToInt(pieces[i], &value));
  }
#elif BUILDFLAG(IS_LINUX)
  // X11; Linux x86_64
  ASSERT_EQ(2u, pieces.size());
  ASSERT_EQ("X11", pieces[0]);
  pieces = base::SplitStringUsingSubstr(pieces[1], " ", base::KEEP_WHITESPACE,
                                        base::SPLIT_WANT_ALL);
  ASSERT_EQ(2u, pieces.size());
  // This may not be Linux in all cases in the wild, but it is on the bots.
  ASSERT_EQ("Linux", pieces[0]);
  ASSERT_EQ(GetMachine(), pieces[1]);
#elif BUILDFLAG(IS_ANDROID)
  // Linux; Android 7.1.1; Pixel 2
  ASSERT_GE(3u, pieces.size());
  ASSERT_EQ("Linux", pieces[0]);
  std::string model;
  if (pieces.size() > 2)
    model = pieces[2];

  pieces = base::SplitStringUsingSubstr(pieces[1], " ", base::KEEP_WHITESPACE,
                                        base::SPLIT_WANT_ALL);
  ASSERT_EQ(2u, pieces.size());
  ASSERT_EQ("Android", pieces[0]);
  pieces = base::SplitStringUsingSubstr(pieces[1], ".", base::KEEP_WHITESPACE,
                                        base::SPLIT_WANT_ALL);
  for (unsigned int i = 1; i < pieces.size(); ++i) {
    int value;
    ASSERT_TRUE(base::StringToInt(pieces[i], &value));
  }

  if (!model.empty()) {
    if (base::SysInfo::GetAndroidBuildCodename() == "REL")
      ASSERT_EQ(base::SysInfo::HardwareModelName(), model);
    else
      ASSERT_EQ("", model);
  }
#elif BUILDFLAG(IS_FUCHSIA)
  // X11; Fuchsia
  ASSERT_EQ(1u, pieces.size());
  ASSERT_EQ("Fuchsia", pieces[0]);
#else
#error Unsupported platform
#endif

  // Check that the version numbers match.
  EXPECT_FALSE(webkit_version_str.empty());
  EXPECT_FALSE(safari_version_str.empty());
  EXPECT_EQ(webkit_version_str, safari_version_str);

  EXPECT_TRUE(
      base::StartsWith(product_str, "Chrome/", base::CompareCase::SENSITIVE));
  if (mobile_device) {
    // "Mobile" gets tacked on to the end for mobile devices, like phones.
    EXPECT_TRUE(
        base::EndsWith(product_str, " Mobile", base::CompareCase::SENSITIVE));
  }
}

#if BUILDFLAG(IS_WIN)
bool ResolveCoreWinRT() {
  return base::win::ResolveCoreWinRTDelayload() &&
         base::win::ScopedHString::ResolveCoreWinRTStringDelayload() &&
         base::win::HStringReference::ResolveCoreWinRTStringDelayload();
}

// On Windows, the client hint sec-ch-ua-platform-version should be
// the highest supported version of the UniversalApiContract.
void VerifyWinPlatformVersion(std::string version) {
  ASSERT_TRUE(ResolveCoreWinRT());
  base::win::ScopedWinrtInitializer scoped_winrt_initializer;
  ASSERT_TRUE(scoped_winrt_initializer.Succeeded());

  base::win::HStringReference api_info_class_name(
      RuntimeClass_Windows_Foundation_Metadata_ApiInformation);

  Microsoft::WRL::ComPtr<
      ABI::Windows::Foundation::Metadata::IApiInformationStatics>
      api;
  HRESULT result = base::win::RoGetActivationFactory(api_info_class_name.Get(),
                                                     IID_PPV_ARGS(&api));
  ASSERT_EQ(result, S_OK);

  base::win::HStringReference universal_contract_name(
      L"Windows.Foundation.UniversalApiContract");

  std::vector<std::string> version_parts = base::SplitString(
      version, ".", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  EXPECT_EQ(version_parts[2], "0");

  int major_version;
  base::StringToInt(version_parts[0], &major_version);

  // If this check fails, our highest known UniversalApiContract version
  // needs to be updated.
  EXPECT_LE(major_version,
            GetHighestKnownUniversalApiContractVersionForTesting());

  int minor_version;
  base::StringToInt(version_parts[1], &minor_version);

  boolean is_supported = false;
  // Verify that the major and minor versions are supported.
  result = api->IsApiContractPresentByMajor(universal_contract_name.Get(),
                                            major_version, &is_supported);
  EXPECT_EQ(result, S_OK);
  EXPECT_TRUE(is_supported)
      << " expected major version " << major_version << " to be supported.";
  result = api->IsApiContractPresentByMajorAndMinor(
      universal_contract_name.Get(), major_version, minor_version,
      &is_supported);
  EXPECT_EQ(result, S_OK);
  EXPECT_TRUE(is_supported)
      << " expected major version " << major_version << " and minor version "
      << minor_version << " to be supported.";

  // Verify that the next highest value is not supported.
  result = api->IsApiContractPresentByMajorAndMinor(
      universal_contract_name.Get(), major_version, minor_version + 1,
      &is_supported);
  EXPECT_EQ(result, S_OK);
  EXPECT_FALSE(is_supported) << " expected minor version " << minor_version + 1
                             << " to not be supported with a major version of "
                             << major_version << ".";
  result = api->IsApiContractPresentByMajor(universal_contract_name.Get(),
                                            major_version + 1, &is_supported);
  EXPECT_EQ(result, S_OK);
  EXPECT_FALSE(is_supported) << " expected major version " << major_version + 1
                             << " to not be supported.";
}

void VerifyLegacyWinPlatformVersion(const std::string& version) {
  switch (base::win::GetVersion()) {
    case base::win::Version::WIN7:
      EXPECT_EQ("0.1.0", version);
      break;
    case base::win::Version::WIN8:
      EXPECT_EQ("0.2.0", version);
      break;
    case base::win::Version::WIN8_1:
      EXPECT_EQ("0.3.0", version);
      break;
    default:
      EXPECT_EQ("0.0.0", version);
      break;
  }
}
#endif  // BUILDFLAG(IS_WIN)

bool ContainsBrandVersion(const blink::UserAgentBrandList& brand_list,
                          const blink::UserAgentBrandVersion brand_version) {
  for (const auto& brand_list_entry : brand_list) {
    if (brand_list_entry == brand_version)
      return true;
  }
  return false;
}

}  // namespace

class UserAgentUtilsTest : public testing::Test,
                           public testing::WithParamInterface<bool> {
 public:
  // The minor version in the reduced UA string is always "0.0.0".
  static constexpr char kReducedMinorVersion[] = "0.0.0";
  // The minor version in the ReduceUserAgentMinorVersion experiment is always
  // "0.X.0", where X is the frozen build version.
  const std::string kReduceUserAgentMinorVersion =
      "0." +
      std::string(blink::features::kUserAgentFrozenBuildVersion.Get().data()) +
      ".0";

  std::string MajorToMinorVersionNumber() {
    const base::Version version = version_info::GetVersion();
    std::string version_str;
    const auto& components = version.components();
    for (size_t i = 0; i < version.components().size(); ++i) {
      if (i > 0)
        version_str.append(".");
      if (i == 0)
        version_str.append("99");
      else if (i == 1)
        version_str.append(base::NumberToString(components[0]));
      else
        version_str.append(base::NumberToString(components[i]));
    }
    return version_str;
  }

  std::string GetUserAgentMinorVersion(const std::string& user_agent_value) {
    // A regular expression that matches Chrome/{major_version}.{minor_version}
    // in the User-Agent string, where the {minor_version} is captured.
    static constexpr char kChromeVersionRegex[] =
        "Chrome/[0-9]+\\.([0-9]+\\.[0-9]+\\.[0-9]+)";
    std::string minor_version;
    EXPECT_TRUE(re2::RE2::PartialMatch(user_agent_value, kChromeVersionRegex,
                                       &minor_version));
    return minor_version;
  }

  std::string GetUserAgentPlatformOsCpu(const std::string& user_agent_value) {
    // A regular expression that matches Mozilla/5.0 ({platform_oscpu})
    // in the User-Agent string.
    static constexpr char kChromePlatformOscpuRegex[] =
        "^Mozilla\\/5\\.0 \\((.+)\\) AppleWebKit\\/537\\.36";
    std::string platform_oscpu;
    EXPECT_TRUE(re2::RE2::PartialMatch(
        user_agent_value, kChromePlatformOscpuRegex, &platform_oscpu));
    return platform_oscpu;
  }

  void VerifyFullUserAgent() {
    EXPECT_EQ(
        GetUserAgent(ForceMajorVersionToMinorPosition::kForceEnabled),
        GetFullUserAgent(ForceMajorVersionToMinorPosition::kForceEnabled));
    EXPECT_EQ(
        GetUserAgent(ForceMajorVersionToMinorPosition::kForceDisabled),
        GetFullUserAgent(ForceMajorVersionToMinorPosition::kForceDisabled));
    EXPECT_EQ(GetUserAgent(), GetFullUserAgent());
    EXPECT_NE(GetUserAgentMinorVersion(GetFullUserAgent()),
              kReducedMinorVersion);
  }

  void VerifyGetUserAgentFunctions() {
    // 1) GetUserAgent should return user agent depends on
    // kReduceUserAgentMinorVersion feature.
    // 2) GetReducedUserAgent should return reduced user agent.
    // 3) GetFullUserAgent should return full user agent.
    if (base::FeatureList::IsEnabled(
            blink::features::kReduceUserAgentMinorVersion)) {
      EXPECT_EQ(GetUserAgentMinorVersion(GetUserAgent()), kReducedMinorVersion);
    } else {
      EXPECT_NE(GetUserAgentMinorVersion(GetUserAgent()), kReducedMinorVersion);
    }
    EXPECT_EQ(GetUserAgentMinorVersion(GetReducedUserAgent()),
              kReducedMinorVersion);
    EXPECT_NE(GetUserAgentMinorVersion(GetFullUserAgent()),
              kReducedMinorVersion);
  }

 private:
  base::test::ScopedFeatureList scoped_feature_list_;
};

TEST_F(UserAgentUtilsTest, UserAgentStringOrdering) {
#if BUILDFLAG(IS_ANDROID)
  const char* const kArguments[] = {"chrome"};
  base::test::ScopedCommandLine scoped_command_line;
  base::CommandLine* command_line = scoped_command_line.GetProcessCommandLine();
  command_line->InitFromArgv(1, kArguments);

  // Do it for regular devices.
  ASSERT_FALSE(command_line->HasSwitch(switches::kUseMobileUserAgent));
  CheckUserAgentStringOrdering(false);

  // Do it for mobile devices.
  command_line->AppendSwitch(switches::kUseMobileUserAgent);
  ASSERT_TRUE(command_line->HasSwitch(switches::kUseMobileUserAgent));
  CheckUserAgentStringOrdering(true);
#else
  CheckUserAgentStringOrdering(false);
#endif
}

TEST_F(UserAgentUtilsTest, UserAgentStringReduced) {
  base::test::ScopedFeatureList scoped_feature_list;
  scoped_feature_list.InitAndEnableFeature(blink::features::kReduceUserAgent);

#if BUILDFLAG(IS_ANDROID)
  // Verify the correct user agent is returned when the UseMobileUserAgent
  // command line flag is present.
  const char* const kArguments[] = {"chrome"};
  base::test::ScopedCommandLine scoped_command_line;
  base::CommandLine* command_line = scoped_command_line.GetProcessCommandLine();
  command_line->InitFromArgv(1, kArguments);
  const std::string major_version_number =
      version_info::GetMajorVersionNumber();
  const char* const major_version = major_version_number.c_str();

  // Verify the mobile user agent string is not returned when not using a mobile
  // user agent.
  ASSERT_FALSE(command_line->HasSwitch(switches::kUseMobileUserAgent));
  {
    std::string buffer = GetUserAgent();
    std::string device_compat = "";
    EXPECT_EQ(buffer, base::StringPrintf(kAndroid, major_version,
                                         device_compat.c_str()));
  }

  // Verify the mobile user agent string is returned when using a mobile user
  // agent.
  command_line->AppendSwitch(switches::kUseMobileUserAgent);
  ASSERT_TRUE(command_line->HasSwitch(switches::kUseMobileUserAgent));
  {
    std::string buffer = GetUserAgent();
    std::string device_compat = "Mobile ";
    EXPECT_EQ(buffer, base::StringPrintf(kAndroid, major_version,
                                         device_compat.c_str()));
  }

  // Verify that the reduced user agent string respects
  // --force-major-version-to-minor
  scoped_feature_list.Reset();
  scoped_feature_list.InitWithFeatures(
      {blink::features::kReduceUserAgent,
      blink::features::kForceMajorVersionInMinorPositionInUserAgent}, {});
  {
    std::string buffer = GetReducedUserAgent();
    std::string device_compat = "Mobile ";
    EXPECT_EQ(buffer,
              base::StringPrintf(kAndroid, "99", device_compat.c_str()));
  }

  // Ensure that the ForceMajorVersionToMinorPosition policy is applied even
  // when it contradicts Blink feature status values.
  scoped_feature_list.Reset();
  scoped_feature_list.InitWithFeatures(
      {blink::features::kReduceUserAgent,
       blink::features::kForceMajorVersionInMinorPositionInUserAgent},
      {});
  {
    std::string buffer =
        GetReducedUserAgent(ForceMajorVersionToMinorPosition::kForceDisabled);
    std::string device_compat = "Mobile ";
    EXPECT_EQ(buffer, base::StringPrintf(kAndroid, major_version,
                                         device_compat.c_str()));
  }
#else
  {
    std::string buffer = GetUserAgent();
    EXPECT_EQ(buffer,
              base::StringPrintf(
                  kDesktop, version_info::GetMajorVersionNumber().c_str()));
  }

  // Verify that the reduced user agent string respects
  // --force-major-version-to-minor
  scoped_feature_list.Reset();
  scoped_feature_list.InitWithFeatures(
      {blink::features::kReduceUserAgent,
      blink::features::kForceMajorVersionInMinorPositionInUserAgent}, {});
  {
    std::string buffer = GetReducedUserAgent();
    EXPECT_EQ(buffer, base::StringPrintf(kDesktop, "99"));
  }

  // Ensure that the ForceMajorVersionToMinorPosition policy is applied even
  // when it contradicts Blink feature status values.
  scoped_feature_list.Reset();
  scoped_feature_list.InitWithFeatures(
      {blink::features::kReduceUserAgent,
       blink::features::kForceMajorVersionInMinorPositionInUserAgent},
      {});
  {
    std::string buffer =
        GetReducedUserAgent(ForceMajorVersionToMinorPosition::kForceDisabled);
    EXPECT_EQ(buffer,
              base::StringPrintf(
                  kDesktop, version_info::GetMajorVersionNumber().c_str()));
  }
#endif

  EXPECT_EQ(GetUserAgent(), GetReducedUserAgent());
}

TEST_F(UserAgentUtilsTest, UserAgentStringFull) {
  base::test::ScopedFeatureList scoped_feature_list;

  // Verify that the full user agent string respects
  // --force-major-version-to-minor
  scoped_feature_list.Reset();
  scoped_feature_list.InitWithFeatures(
      {blink::features::kFullUserAgent,
       blink::features::kForceMajorVersionInMinorPositionInUserAgent},
      {});
  { VerifyFullUserAgent(); }

  // Verify that the full user agent string when both reduced and full UA
  // feature enabled respects
  // --force-major-version-to-minor
  scoped_feature_list.Reset();
  scoped_feature_list.InitWithFeatures(
      {blink::features::kFullUserAgent, blink::features::kReduceUserAgent,
       blink::features::kForceMajorVersionInMinorPositionInUserAgent},
      {});
  { VerifyFullUserAgent(); }

  // Verify that the full user agent string still returns the full user agent
  // even turns on kReduceUserAgentMinorVersion
  scoped_feature_list.Reset();
  scoped_feature_list.InitWithFeatures(
      {blink::features::kFullUserAgent,
       blink::features::kReduceUserAgentMinorVersion},
      {});
  { VerifyFullUserAgent(); }

  // Verify that three user agent functions return the correct user agent string
  // when kReduceUserAgentMinorVersion turns on.
  scoped_feature_list.Reset();
  scoped_feature_list.InitWithFeatures(
      {blink::features::kReduceUserAgentMinorVersion}, {});
  { VerifyGetUserAgentFunctions(); }

  // Verify that three user agent functions return the correct user agent string
  // when both kReduceUserAgentMinorVersion and kReduceUserAgentPlatformOsCpu
  // turn on.
  scoped_feature_list.Reset();
  scoped_feature_list.InitWithFeatures(
      {blink::features::kReduceUserAgentMinorVersion,
       blink::features::kReduceUserAgentPlatformOsCpu},
      {});
  { VerifyGetUserAgentFunctions(); }

  // Verify that three user agent functions return the correct user agent string
  // when kReduceUserAgentPlatformOsCpu turns on.
  scoped_feature_list.Reset();
  scoped_feature_list.InitWithFeatures(
      {blink::features::kReduceUserAgentPlatformOsCpu}, {});
  { VerifyGetUserAgentFunctions(); }

  // Verify that three user agent functions return the correct user agent
  // when kReduceUserAgentMinorVersion turns off.
  scoped_feature_list.Reset();
  scoped_feature_list.InitWithFeatures(
      {}, {blink::features::kReduceUserAgentMinorVersion});
  { VerifyGetUserAgentFunctions(); }

  // Verify that three user agent functions return the correct user agent
  // without explicit features turn on.
  scoped_feature_list.Reset();
  scoped_feature_list.InitWithFeatures({}, {});
  { VerifyGetUserAgentFunctions(); }
}

TEST_F(UserAgentUtilsTest, ReduceUserAgentPlatformOsCpu) {
  base::test::ScopedFeatureList scoped_feature_list;
  scoped_feature_list.InitWithFeatures(
      {blink::features::kReduceUserAgentMinorVersion,
       blink::features::kReduceUserAgentPlatformOsCpu},
      {});

#if BUILDFLAG(IS_ANDROID)
  // Verify the correct user agent is returned when the UseMobileUserAgent
  // command line flag is present.
  const char* const kArguments[] = {"chrome"};
  base::test::ScopedCommandLine scoped_command_line;
  base::CommandLine* command_line = scoped_command_line.GetProcessCommandLine();
  command_line->InitFromArgv(1, kArguments);

  // Verify the mobile platform and oscpu user agent string is not reduced when
  // not using a mobile user agent.
  ASSERT_FALSE(command_line->HasSwitch(switches::kUseMobileUserAgent));
  {
    EXPECT_NE(base::StringPrintf(
                  kAndroid, version_info::GetMajorVersionNumber().c_str(), ""),
              GetUserAgent());
    EXPECT_NE(content::GetUnifiedPlatformForTesting().c_str(),
              GetUserAgentPlatformOsCpu(GetUserAgent()));
  }

  // Verify the mobile platform and oscpu user agent string is not reduced when
  // using a mobile user agent.
  command_line->AppendSwitch(switches::kUseMobileUserAgent);
  ASSERT_TRUE(command_line->HasSwitch(switches::kUseMobileUserAgent));
  {
    EXPECT_NE(
        base::StringPrintf(
            kAndroid, version_info::GetMajorVersionNumber().c_str(), "Mobile "),
        GetUserAgent());
  }

#else
  {
    // Verify desktop unified platform user agent is returned.
    EXPECT_EQ(base::StringPrintf(kDesktop,
                                 version_info::GetMajorVersionNumber().c_str()),
              GetUserAgent());
  }

  // Verify disable reduce legacy windows platform
  scoped_feature_list.Reset();
  scoped_feature_list.InitWithFeaturesAndParameters(
      {{blink::features::kReduceUserAgentMinorVersion, {}},
       {blink::features::kReduceUserAgentPlatformOsCpu,
        {{"all_except_legacy_windows_platform", "true"},
         {"legacy_windows_platform", "false"}}}},
      {});
  {
#if BUILDFLAG(IS_WIN)
    if (base::win::GetVersion() < base::win::Version::WIN10) {
      EXPECT_NE(base::StringPrintf(
                    kDesktop, version_info::GetMajorVersionNumber().c_str()),
                GetUserAgent());
      EXPECT_NE("Windows NT 10.0; Win64; x64",
                GetUserAgentPlatformOsCpu(GetUserAgent()));
    } else {
      EXPECT_EQ(base::StringPrintf(
                    kDesktop, version_info::GetMajorVersionNumber().c_str()),
                GetUserAgent());
      EXPECT_EQ("Windows NT 10.0; Win64; x64",
                GetUserAgentPlatformOsCpu(GetUserAgent()));
    }
#else
    EXPECT_EQ(base::StringPrintf(kDesktop,
                                 version_info::GetMajorVersionNumber().c_str()),
              GetUserAgent());
#endif
  }

  // Verify that the reduced user agent string respects
  // --force-major-version-to-minor
  scoped_feature_list.Reset();
  scoped_feature_list.InitWithFeatures(
      {blink::features::kReduceUserAgentMinorVersion,
       blink::features::kReduceUserAgentPlatformOsCpu,
       blink::features::kForceMajorVersionInMinorPositionInUserAgent},
      {});
  {
    EXPECT_EQ(
        base::StringPrintf("Mozilla/5.0 (%s) AppleWebKit/537.36 (KHTML, "
                           "like Gecko) Chrome/%s.%s.0.0 Safari/537.36",
                           content::GetUnifiedPlatformForTesting().c_str(),
                           "99", version_info::GetMajorVersionNumber().c_str()),
        GetUserAgent());
  }

  // Ensure that the ForceMajorVersionToMinorPosition policy is applied even
  // when it contradicts Blink feature status values.
  scoped_feature_list.Reset();
  scoped_feature_list.InitWithFeatures(
      {blink::features::kReduceUserAgentMinorVersion,
       blink::features::kReduceUserAgentPlatformOsCpu,
       blink::features::kForceMajorVersionInMinorPositionInUserAgent},
      {});
  {
    EXPECT_EQ(base::StringPrintf(kDesktop,
                                 version_info::GetMajorVersionNumber().c_str()),
              GetUserAgent(ForceMajorVersionToMinorPosition::kForceDisabled));
  }
#endif

  scoped_feature_list.Reset();
  scoped_feature_list.InitWithFeatures(
      {blink::features::kReduceUserAgentMinorVersion,
       blink::features::kReduceUserAgentPlatformOsCpu},
      {});
// Verify only reduce platform and oscpu in desktop user agent string in
// phase 5.
#if BUILDFLAG(IS_ANDROID)
  EXPECT_NE(GetUserAgent(), GetReducedUserAgent());
  EXPECT_NE(content::GetUnifiedPlatformForTesting().c_str(),
            GetUserAgentPlatformOsCpu(GetUserAgent()));
#else
  EXPECT_EQ(GetUserAgent(), GetReducedUserAgent());
#endif
}

TEST_F(UserAgentUtilsTest, UserAgentMetadata) {
  auto metadata = GetUserAgentMetadata();

  const std::string major_version = version_info::GetMajorVersionNumber();
  const std::string full_version = version_info::GetVersionNumber();
  const std::string major_to_minor_full_version = MajorToMinorVersionNumber();

  // According to spec, Sec-CH-UA should contain what project the browser is
  // based on (i.e. Chromium in this case) as well as the actual product.
  // In CHROMIUM_BRANDING builds this will check chromium twice. That should be
  // ok though.

  const blink::UserAgentBrandVersion chromium_brand_version = {"Chromium",
                                                               major_version};
  const blink::UserAgentBrandVersion major_to_minor_chromium_brand_version = {
      "Chromium", "99"};
  const blink::UserAgentBrandVersion product_brand_version = {
      version_info::GetProductName(), major_version};
  const blink::UserAgentBrandVersion major_to_minor_product_brand_version = {
      version_info::GetProductName(), "99"};

  EXPECT_TRUE(ContainsBrandVersion(metadata.brand_version_list,
                                   chromium_brand_version));
  EXPECT_TRUE(
      ContainsBrandVersion(metadata.brand_version_list, product_brand_version));

  // verify full version list
  const blink::UserAgentBrandVersion chromium_brand_full_version = {
      "Chromium", full_version};
  const blink::UserAgentBrandVersion
      major_to_minor_chromium_brand_full_version = {
          "Chromium", major_to_minor_full_version};
  const blink::UserAgentBrandVersion product_brand_full_version = {
      version_info::GetProductName(), full_version};
  const blink::UserAgentBrandVersion major_to_minor_product_brand_full_version =
      {version_info::GetProductName(), major_to_minor_full_version};

  EXPECT_TRUE(ContainsBrandVersion(metadata.brand_full_version_list,
                                   chromium_brand_full_version));
  EXPECT_TRUE(ContainsBrandVersion(metadata.brand_full_version_list,
                                   product_brand_full_version));
  EXPECT_EQ(metadata.full_version, full_version);

  // Ensure ForceMajorVersionToMinorPosition is respected,
  TestingPrefServiceSimple pref_service;

  // ForceMajorVersionToMinorPosition: kForceDisabled
  pref_service.registry()->RegisterIntegerPref(
      kForceMajorVersionToMinorPosition,
      static_cast<int>(ForceMajorVersionToMinorPosition::kForceDisabled));
  metadata = GetUserAgentMetadata(&pref_service);
  EXPECT_EQ(metadata.full_version, full_version);
  EXPECT_TRUE(ContainsBrandVersion(metadata.brand_version_list,
                                   chromium_brand_version));
  EXPECT_TRUE(
      ContainsBrandVersion(metadata.brand_version_list, product_brand_version));
  EXPECT_TRUE(ContainsBrandVersion(metadata.brand_full_version_list,
                                   chromium_brand_full_version));
  EXPECT_TRUE(ContainsBrandVersion(metadata.brand_full_version_list,
                                   product_brand_full_version));
  EXPECT_FALSE(ContainsBrandVersion(metadata.brand_version_list,
                                    major_to_minor_chromium_brand_version));
  EXPECT_FALSE(ContainsBrandVersion(metadata.brand_version_list,
                                    major_to_minor_product_brand_version));
  EXPECT_FALSE(
      ContainsBrandVersion(metadata.brand_full_version_list,
                           major_to_minor_chromium_brand_full_version));
  EXPECT_FALSE(ContainsBrandVersion(metadata.brand_full_version_list,
                                    major_to_minor_product_brand_full_version));

  // ForceMajorVersionToMinorPosition: kForceEnabled
  pref_service.SetInteger(
      kForceMajorVersionToMinorPosition,
      static_cast<int>(ForceMajorVersionToMinorPosition::kForceEnabled));
  metadata = GetUserAgentMetadata(&pref_service);
  EXPECT_EQ(metadata.full_version, major_to_minor_full_version);
  EXPECT_TRUE(ContainsBrandVersion(metadata.brand_version_list,
                                   major_to_minor_chromium_brand_version));
  EXPECT_TRUE(ContainsBrandVersion(metadata.brand_version_list,
                                   major_to_minor_product_brand_version));
  EXPECT_TRUE(ContainsBrandVersion(metadata.brand_full_version_list,
                                   major_to_minor_chromium_brand_full_version));
  EXPECT_TRUE(ContainsBrandVersion(metadata.brand_full_version_list,
                                   major_to_minor_product_brand_full_version));
  EXPECT_FALSE(ContainsBrandVersion(metadata.brand_version_list,
                                    chromium_brand_version));
  EXPECT_FALSE(
      ContainsBrandVersion(metadata.brand_version_list, product_brand_version));
  EXPECT_FALSE(ContainsBrandVersion(metadata.brand_full_version_list,
                                    chromium_brand_full_version));
  EXPECT_FALSE(ContainsBrandVersion(metadata.brand_full_version_list,
                                    product_brand_full_version));

#if BUILDFLAG(IS_WIN)
  if (base::win::GetVersion() < base::win::Version::WIN10) {
    VerifyLegacyWinPlatformVersion(metadata.platform_version);
  } else {
    VerifyWinPlatformVersion(metadata.platform_version);
  }
#else
  int32_t major, minor, bugfix = 0;
  base::SysInfo::OperatingSystemVersionNumbers(&major, &minor, &bugfix);
  EXPECT_EQ(metadata.platform_version,
            base::StringPrintf("%d.%d.%d", major, minor, bugfix));
#endif
  // This makes sure no extra information is added to the platform version.
  EXPECT_EQ(metadata.platform_version.find(";"), std::string::npos);
  // If you're here because your change to GetOSType broke this test, it likely
  // means that GetPlatformForUAMetadata needs a new special case to prevent
  // breaking client hints. Check with the code owners for further guidance.
#if BUILDFLAG(IS_WIN)
  EXPECT_EQ(metadata.platform, "Windows");
#elif BUILDFLAG(IS_IOS)
  EXPECT_EQ(metadata.platform, "iOS");
#elif BUILDFLAG(IS_MAC)
  EXPECT_EQ(metadata.platform, "macOS");
#elif BUILDFLAG(IS_CHROMEOS)
# if BUILDFLAG(GOOGLE_CHROME_BRANDING)
  EXPECT_EQ(metadata.platform, "Chrome OS");
# else
  EXPECT_EQ(metadata.platform, "Chromium OS");
# endif
#elif BUILDFLAG(IS_ANDROID)
  EXPECT_EQ(metadata.platform, "Android");
#elif BUILDFLAG(IS_LINUX)
  EXPECT_EQ(metadata.platform, "Linux");
#elif BUILDFLAG(IS_FREEBSD)
  EXPECT_EQ(metadata.platform, "FreeBSD");
#elif BUILDFLAG(IS_OPENBSD)
  EXPECT_EQ(metadata.platform, "OpenBSD");
#elif BUILDFLAG(IS_SOLARIS)
  EXPECT_EQ(metadata.platform, "Solaris");
#elif BUILDFLAG(IS_FUCHSIA)
  EXPECT_EQ(metadata.platform, "Fuchsia");
#else
  EXPECT_EQ(metadata.platform, "Unknown");
#endif
  EXPECT_EQ(metadata.architecture, content::GetCpuArchitecture());
  EXPECT_EQ(metadata.model, content::BuildModelInfo());
  EXPECT_EQ(metadata.bitness, content::GetCpuBitness());
  EXPECT_EQ(metadata.wow64, content::IsWoW64());
}

TEST_F(UserAgentUtilsTest, GenerateBrandVersionListUnbranded) {
  blink::UserAgentMetadata metadata;
  metadata.brand_version_list = GenerateBrandVersionList(
      84, absl::nullopt, "84", absl::nullopt, absl::nullopt, true,
      blink::UserAgentBrandVersionType::kMajorVersion);
  metadata.brand_full_version_list = GenerateBrandVersionList(
      84, absl::nullopt, "84.0.0.0", absl::nullopt, absl::nullopt, true,
      blink::UserAgentBrandVersionType::kFullVersion);
  // 1. verify major version
  std::string brand_list = metadata.SerializeBrandMajorVersionList();
  EXPECT_EQ(R"("Not;A=Brand";v="8", "Chromium";v="84")", brand_list);
  // 2. verify full version
  std::string brand_list_w_fv = metadata.SerializeBrandFullVersionList();
  EXPECT_EQ(R"("Not;A=Brand";v="8.0.0.0", "Chromium";v="84.0.0.0")",
            brand_list_w_fv);
}

TEST_F(UserAgentUtilsTest, GenerateBrandVersionListUnbrandedVerifySeedChanges) {
  blink::UserAgentMetadata metadata;

  metadata.brand_version_list = GenerateBrandVersionList(
      84, absl::nullopt, "84", absl::nullopt, absl::nullopt, true,
      blink::UserAgentBrandVersionType::kMajorVersion);
  // Capture the serialized brand lists with version 84 as the seed.
  std::string brand_list = metadata.SerializeBrandMajorVersionList();
  std::string brand_list_w_fv = metadata.SerializeBrandFullVersionList();

  metadata.brand_version_list = GenerateBrandVersionList(
      85, absl::nullopt, "85", absl::nullopt, absl::nullopt, true,
      blink::UserAgentBrandVersionType::kMajorVersion);
  metadata.brand_full_version_list = GenerateBrandVersionList(
      85, absl::nullopt, "85.0.0.0", absl::nullopt, absl::nullopt, true,
      blink::UserAgentBrandVersionType::kFullVersion);

  // Make sure the lists are different for different seeds (84 vs 85).
  // 1. verify major version
  std::string brand_list_diff = metadata.SerializeBrandMajorVersionList();
  EXPECT_EQ(R"("Chromium";v="85", "Not=A?Brand";v="99")", brand_list_diff);
  EXPECT_NE(brand_list, brand_list_diff);
  // 2.verify full version
  std::string brand_list_diff_w_fv = metadata.SerializeBrandFullVersionList();
  EXPECT_EQ(R"("Chromium";v="85.0.0.0", "Not=A?Brand";v="99.0.0.0")",
            brand_list_diff_w_fv);
  EXPECT_NE(brand_list_w_fv, brand_list_diff_w_fv);
}

TEST_F(UserAgentUtilsTest, GenerateBrandVersionListWithGreaseBrandOverride) {
  blink::UserAgentMetadata metadata;
  // The GREASE generation algorithm should respond to experiment overrides.
  metadata.brand_version_list = GenerateBrandVersionList(
      84, absl::nullopt, "84", "Clean GREASE", absl::nullopt, true,
      blink::UserAgentBrandVersionType::kMajorVersion);
  metadata.brand_full_version_list = GenerateBrandVersionList(
      84, absl::nullopt, "84.0.0.0", "Clean GREASE", absl::nullopt, true,
      blink::UserAgentBrandVersionType::kFullVersion);
  // 1. verify major version
  std::string brand_list_grease_override =
      metadata.SerializeBrandMajorVersionList();
  EXPECT_EQ(R"("Clean GREASE";v="8", "Chromium";v="84")",
            brand_list_grease_override);
  // 2. verify full version
  std::string brand_list_grease_override_fv =
      metadata.SerializeBrandFullVersionList();
  EXPECT_EQ(R"("Clean GREASE";v="8.0.0.0", "Chromium";v="84.0.0.0")",
            brand_list_grease_override_fv);
}

TEST_F(UserAgentUtilsTest,
       GenerateBrandVersionListWithGreaseBrandAndVersionOverride) {
  blink::UserAgentMetadata metadata;

  metadata.brand_version_list = GenerateBrandVersionList(
      84, absl::nullopt, "84", "Clean GREASE", "1024", true,
      blink::UserAgentBrandVersionType::kMajorVersion);
  metadata.brand_full_version_list = GenerateBrandVersionList(
      84, absl::nullopt, "84.0.0.0", "Clean GREASE", "1024", true,
      blink::UserAgentBrandVersionType::kFullVersion);
  // 1. verify major version
  std::string brand_list_and_version_grease_override =
      metadata.SerializeBrandMajorVersionList();
  EXPECT_EQ(R"("Clean GREASE";v="1024", "Chromium";v="84")",
            brand_list_and_version_grease_override);
  // 2. verify full version
  std::string brand_list_and_version_grease_override_fv =
      metadata.SerializeBrandFullVersionList();
  EXPECT_EQ(R"("Clean GREASE";v="1024.0.0.0", "Chromium";v="84.0.0.0")",
            brand_list_and_version_grease_override_fv);
}

TEST_F(UserAgentUtilsTest, GenerateBrandVersionListWithGreaseVersionOverride) {
  blink::UserAgentMetadata metadata;

  metadata.brand_version_list = GenerateBrandVersionList(
      84, absl::nullopt, "84", absl::nullopt, "1024", true,
      blink::UserAgentBrandVersionType::kMajorVersion);
  metadata.brand_full_version_list = GenerateBrandVersionList(
      84, absl::nullopt, "84.0.0.0", absl::nullopt, "1024", true,
      blink::UserAgentBrandVersionType::kFullVersion);
  // 1. verify major version
  std::string brand_version_grease_override =
      metadata.SerializeBrandMajorVersionList();
  EXPECT_EQ(R"("Not;A=Brand";v="1024", "Chromium";v="84")",
            brand_version_grease_override);
  // 2. verify full version
  std::string brand_version_grease_override_fv =
      metadata.SerializeBrandFullVersionList();
  EXPECT_EQ(R"("Not;A=Brand";v="1024.0.0.0", "Chromium";v="84.0.0.0")",
            brand_version_grease_override_fv);
}

TEST_F(UserAgentUtilsTest, GenerateBrandVersionListWithBrand) {
  blink::UserAgentMetadata metadata;
  metadata.brand_version_list = GenerateBrandVersionList(
      84, "Totally A Brand", "84", absl::nullopt, absl::nullopt, true,
      blink::UserAgentBrandVersionType::kMajorVersion);
  metadata.brand_full_version_list = GenerateBrandVersionList(
      84, "Totally A Brand", "84.0.0.0", absl::nullopt, absl::nullopt, true,
      blink::UserAgentBrandVersionType::kFullVersion);
  // 1. verify major version
  std::string brand_list_w_brand = metadata.SerializeBrandMajorVersionList();
  EXPECT_EQ(
      R"("Not;A=Brand";v="8", "Chromium";v="84", "Totally A Brand";v="84")",
      brand_list_w_brand);
  // 2. verify full version
  std::string brand_list_w_brand_fv = metadata.SerializeBrandFullVersionList();
  EXPECT_EQ(base::StrCat({"\"Not;A=Brand\";v=\"8.0.0.0\", ",
                          "\"Chromium\";v=\"84.0.0.0\", ",
                          "\"Totally A Brand\";v=\"84.0.0.0\""}),
            brand_list_w_brand_fv);
}

TEST_F(UserAgentUtilsTest, GenerateBrandVersionListInvalidSeed) {
  // Should DCHECK on negative numbers
  EXPECT_DCHECK_DEATH(GenerateBrandVersionList(
      -1, absl::nullopt, "99", absl::nullopt, absl::nullopt, true,
      blink::UserAgentBrandVersionType::kMajorVersion));
  EXPECT_DCHECK_DEATH(GenerateBrandVersionList(
      -1, absl::nullopt, "99.0.0.0", absl::nullopt, absl::nullopt, true,
      blink::UserAgentBrandVersionType::kFullVersion));
}

TEST_F(UserAgentUtilsTest, GetGreasedUserAgentBrandVersionOldAlgorithm) {
  base::test::ScopedFeatureList scoped_feature_list;
  // Test to ensure the old algorithm is respected when opted into.
  scoped_feature_list.InitAndEnableFeatureWithParameters(
      features::kGreaseUACH, {{"updated_algorithm", "false"}});

  std::vector<int> permuted_order{0, 1, 2};
  blink::UserAgentBrandVersion greased_bv = GetGreasedUserAgentBrandVersion(
      permuted_order, 84, absl::nullopt, absl::nullopt, true,
      blink::UserAgentBrandVersionType::kMajorVersion);
  EXPECT_EQ(greased_bv.brand, " Not A;Brand");
  EXPECT_EQ(greased_bv.version, "99");

  greased_bv = GetGreasedUserAgentBrandVersion(
      permuted_order, 84, absl::nullopt, absl::nullopt, true,
      blink::UserAgentBrandVersionType::kFullVersion);
  EXPECT_EQ(greased_bv.brand, " Not A;Brand");
  EXPECT_EQ(greased_bv.version, "99.0.0.0");
}

TEST_F(UserAgentUtilsTest,
       GetGreasedUserAgentBrandVersionOldAlgorithmIgnoresBrandOverrides) {
  base::test::ScopedFeatureList scoped_feature_list;
  // Test to ensure the old algorithm is respected when the flag is not set.
  scoped_feature_list.InitAndEnableFeatureWithParameters(
      features::kGreaseUACH, {{"updated_algorithm", "false"}});
  // With the new algorithm disabled, we want to avoid experiment params
  // ("WhatIsGrease", 1024) from taking an effect.
  std::vector<int> permuted_order{0, 1, 2};
  blink::UserAgentBrandVersion greased_bv = GetGreasedUserAgentBrandVersion(
      permuted_order, 84, "WhatIsGrease", absl::nullopt, true,
      blink::UserAgentBrandVersionType::kMajorVersion);
  EXPECT_EQ(greased_bv.brand, " Not A;Brand");
  EXPECT_EQ(greased_bv.version, "99");

  greased_bv = GetGreasedUserAgentBrandVersion(
      permuted_order, 84, "WhatIsGrease", absl::nullopt, true,
      blink::UserAgentBrandVersionType::kFullVersion);
  EXPECT_EQ(greased_bv.brand, " Not A;Brand");
  EXPECT_EQ(greased_bv.version, "99.0.0.0");
}

TEST_F(UserAgentUtilsTest,
       GetGreasedUserAgentBrandVersionOldAlgorithmIgnoresVersionOverrides) {
  base::test::ScopedFeatureList scoped_feature_list;
  // Test to ensure the old algorithm is respected when the flag is not set.
  scoped_feature_list.InitAndEnableFeatureWithParameters(
      features::kGreaseUACH, {{"updated_algorithm", "false"}});
  // With the new algorithm disabled, we want to avoid experiment params
  // ("WhatIsGrease", 1024) from taking an effect.
  std::vector<int> permuted_order{0, 1, 2};
  blink::UserAgentBrandVersion greased_bv = GetGreasedUserAgentBrandVersion(
      permuted_order, 84, absl::nullopt, "1024", true,
      blink::UserAgentBrandVersionType::kMajorVersion);
  EXPECT_EQ(greased_bv.brand, " Not A;Brand");
  EXPECT_EQ(greased_bv.version, "99");

  greased_bv = GetGreasedUserAgentBrandVersion(
      permuted_order, 84, absl::nullopt, "1024", true,
      blink::UserAgentBrandVersionType::kFullVersion);
  EXPECT_EQ(greased_bv.brand, " Not A;Brand");
  EXPECT_EQ(greased_bv.version, "99.0.0.0");
}

TEST_F(
    UserAgentUtilsTest,
    GetGreasedUserAgentBrandVersionOldAlgorithmIgnoresBrandAndVersionOverrides) {
  base::test::ScopedFeatureList scoped_feature_list;
  // Test to ensure the old algorithm is respected when the flag is not set.
  scoped_feature_list.InitAndEnableFeatureWithParameters(
      features::kGreaseUACH, {{"updated_algorithm", "false"}});
  // With the new algorithm disabled, we want to avoid experiment params
  // ("WhatIsGrease", 1024) from taking an effect.
  std::vector<int> permuted_order{0, 1, 2};
  blink::UserAgentBrandVersion greased_bv = GetGreasedUserAgentBrandVersion(
      permuted_order, 84, "WhatIsGrease", "1024", true,
      blink::UserAgentBrandVersionType::kMajorVersion);
  EXPECT_EQ(greased_bv.brand, " Not A;Brand");
  EXPECT_EQ(greased_bv.version, "99");

  greased_bv = GetGreasedUserAgentBrandVersion(
      permuted_order, 84, "WhatIsGrease", "1024", true,
      blink::UserAgentBrandVersionType::kFullVersion);
  EXPECT_EQ(greased_bv.brand, " Not A;Brand");
  EXPECT_EQ(greased_bv.version, "99.0.0.0");
}

// Tests to ensure the new algorithm works and is still overridable.
TEST_F(UserAgentUtilsTest, GetGreasedUserAgentBrandVersionNewAlgorithm) {
  std::vector<int> permuted_order{0, 1, 2};
  blink::UserAgentBrandVersion greased_bv = GetGreasedUserAgentBrandVersion(
      permuted_order, 84, absl::nullopt, absl::nullopt, true,
      blink::UserAgentBrandVersionType::kMajorVersion);
  EXPECT_EQ(greased_bv.brand, "Not;A=Brand");
  EXPECT_EQ(greased_bv.version, "8");

  greased_bv = GetGreasedUserAgentBrandVersion(
      permuted_order, 84, absl::nullopt, absl::nullopt, true,
      blink::UserAgentBrandVersionType::kFullVersion);
  EXPECT_EQ(greased_bv.brand, "Not;A=Brand");
  EXPECT_EQ(greased_bv.version, "8.0.0.0");
}

TEST_F(UserAgentUtilsTest,
       GetGreasedUserAgentBrandVersionNewAlgorithmBrandOverride) {
  std::vector<int> permuted_order{0, 1, 2};
  blink::UserAgentBrandVersion greased_bv = GetGreasedUserAgentBrandVersion(
      permuted_order, 84, "WhatIsGrease", absl::nullopt, true,
      blink::UserAgentBrandVersionType::kMajorVersion);
  EXPECT_EQ(greased_bv.brand, "WhatIsGrease");
  EXPECT_EQ(greased_bv.version, "8");

  greased_bv = GetGreasedUserAgentBrandVersion(
      permuted_order, 84, "WhatIsGrease", absl::nullopt, true,
      blink::UserAgentBrandVersionType::kFullVersion);
  EXPECT_EQ(greased_bv.brand, "WhatIsGrease");
  EXPECT_EQ(greased_bv.version, "8.0.0.0");
}

TEST_F(UserAgentUtilsTest,
       GetGreasedUserAgentBrandVersionNewAlgorithmVersionOverride) {
  std::vector<int> permuted_order{0, 1, 2};
  blink::UserAgentBrandVersion greased_bv = GetGreasedUserAgentBrandVersion(
      permuted_order, 84, absl::nullopt, "1024", true,
      blink::UserAgentBrandVersionType::kMajorVersion);
  EXPECT_EQ(greased_bv.brand, "Not;A=Brand");
  EXPECT_EQ(greased_bv.version, "1024");

  greased_bv = GetGreasedUserAgentBrandVersion(
      permuted_order, 84, absl::nullopt, "1024", true,
      blink::UserAgentBrandVersionType::kFullVersion);
  EXPECT_EQ(greased_bv.brand, "Not;A=Brand");
  EXPECT_EQ(greased_bv.version, "1024.0.0.0");
}

TEST_F(UserAgentUtilsTest,
       GetGreasedUserAgentBrandVersionNewAlgorithmBrandAndVersionOverride) {
  std::vector<int> permuted_order{0, 1, 2};
  blink::UserAgentBrandVersion greased_bv = GetGreasedUserAgentBrandVersion(
      permuted_order, 84, "WhatIsGrease", "1024", true,
      blink::UserAgentBrandVersionType::kMajorVersion);
  EXPECT_EQ(greased_bv.brand, "WhatIsGrease");
  EXPECT_EQ(greased_bv.version, "1024");

  greased_bv = GetGreasedUserAgentBrandVersion(
      permuted_order, 84, "WhatIsGrease", "1024", true,
      blink::UserAgentBrandVersionType::kFullVersion);
  EXPECT_EQ(greased_bv.brand, "WhatIsGrease");
  EXPECT_EQ(greased_bv.version, "1024.0.0.0");
}

TEST_F(UserAgentUtilsTest, GetGreasedUserAgentBrandVersionFullVersions) {
  std::vector<int> permuted_order = {2, 1, 0};
  blink::UserAgentBrandVersion greased_bv = GetGreasedUserAgentBrandVersion(
      permuted_order, 86, absl::nullopt, absl::nullopt, true,
      blink::UserAgentBrandVersionType::kMajorVersion);
  EXPECT_EQ(greased_bv.brand, "Not?A_Brand");
  EXPECT_EQ(greased_bv.version, "24");

  greased_bv = GetGreasedUserAgentBrandVersion(
      permuted_order, 86, absl::nullopt, absl::nullopt, true,
      blink::UserAgentBrandVersionType::kFullVersion);
  EXPECT_EQ(greased_bv.brand, "Not?A_Brand");
  EXPECT_EQ(greased_bv.version, "24.0.0.0");

  // Test the greasy input with full version
  greased_bv = GetGreasedUserAgentBrandVersion(
      permuted_order, 84, absl::nullopt, "1024.0.0.0", true,
      blink::UserAgentBrandVersionType::kMajorVersion);
  EXPECT_EQ(greased_bv.brand, "Not;A=Brand");
  EXPECT_EQ(greased_bv.version, "1024");

  greased_bv = GetGreasedUserAgentBrandVersion(
      permuted_order, 84, absl::nullopt, "1024.0.0.0", true,
      blink::UserAgentBrandVersionType::kFullVersion);
  EXPECT_EQ(greased_bv.brand, "Not;A=Brand");
  EXPECT_EQ(greased_bv.version, "1024.0.0.0");
}

TEST_F(UserAgentUtilsTest, GetGreasedUserAgentBrandVersionEnterpriseOverride) {
  // Ensure the enterprise override bool can force the old GREASE algorithm to
  // be used.
  std::vector<int> permuted_order = {0, 1, 2};
  blink::UserAgentBrandVersion greased_bv = GetGreasedUserAgentBrandVersion(
      permuted_order, 84, absl::nullopt, absl::nullopt, false,
      blink::UserAgentBrandVersionType::kMajorVersion);
  EXPECT_EQ(greased_bv.brand, " Not A;Brand");
  EXPECT_EQ(greased_bv.version, "99");

  greased_bv = GetGreasedUserAgentBrandVersion(
      permuted_order, 84, absl::nullopt, absl::nullopt, false,
      blink::UserAgentBrandVersionType::kFullVersion);
  EXPECT_EQ(greased_bv.brand, " Not A;Brand");
  EXPECT_EQ(greased_bv.version, "99.0.0.0");
}

TEST_F(
    UserAgentUtilsTest,
    GetGreasedUserAgentBrandVersionEnterpriseOverrideSupersedesOtherOverrides) {
  // Ensure the enterprise override bool can force the old GREASE algorithm to
  // be used and supersedes passed-in brand/version overrides.
  std::vector<int> permuted_order = {0, 1, 2};
  blink::UserAgentBrandVersion greased_bv = GetGreasedUserAgentBrandVersion(
      permuted_order, 84, "helloWorld", "100000", false,
      blink::UserAgentBrandVersionType::kMajorVersion);
  EXPECT_EQ(greased_bv.brand, " Not A;Brand");
  EXPECT_EQ(greased_bv.version, "99");

  greased_bv = GetGreasedUserAgentBrandVersion(
      permuted_order, 84, "helloWorld", "100000", false,
      blink::UserAgentBrandVersionType::kFullVersion);
  EXPECT_EQ(greased_bv.brand, " Not A;Brand");
  EXPECT_EQ(greased_bv.version, "99.0.0.0");
}

TEST_F(UserAgentUtilsTest, GetGreasedUserAgentBrandVersionNoLeadingWhitespace) {
  std::vector<int> permuted_order = {0, 1, 2};
  blink::UserAgentBrandVersion greased_bv;
  // Go up to 110 based on the 11 total chars * 10 possible first chars.
  for (int i = 0; i < 110; i++) {
    // Regardless of the major version seed, the spec calls for no leading
    // whitespace in the brand.
    greased_bv = GetGreasedUserAgentBrandVersion(
        permuted_order, i, absl::nullopt, absl::nullopt, true,
        blink::UserAgentBrandVersionType::kMajorVersion);
    EXPECT_NE(greased_bv.brand[0], ' ');

    greased_bv = GetGreasedUserAgentBrandVersion(
        permuted_order, i, absl::nullopt, absl::nullopt, true,
        blink::UserAgentBrandVersionType::kFullVersion);
    EXPECT_NE(greased_bv.brand[0], ' ');
  }
}

TEST_F(UserAgentUtilsTest, GetProductAndVersion) {
  std::string product;
  std::string major_version;
  std::string minor_version;
  std::string build_version;
  std::string patch_version;

  // (1) Features: UserAgentReduction and MajorVersionInMinor disabled.
  base::test::ScopedFeatureList scoped_feature_list;
  scoped_feature_list.InitWithFeatures(
      /*enabled_features=*/{}, /*disabled_features=*/{
          blink::features::kForceMajorVersionInMinorPositionInUserAgent,
          blink::features::kReduceUserAgentMinorVersion});

  // (1a) Policies: UserAgentReduction and MajorVersionInMinor default.
  product =
      GetProductAndVersion(ForceMajorVersionToMinorPosition::kDefault,
                           UserAgentReductionEnterprisePolicyState::kDefault);
  EXPECT_TRUE(re2::RE2::FullMatch(product, kChromeProductVersionRegex,
                                  &major_version, &minor_version,
                                  &build_version));
  EXPECT_EQ(major_version, version_info::GetMajorVersionNumber());
  EXPECT_EQ(minor_version, "0");
  EXPECT_NE(build_version, "0");
  // Patch version cannot be tested as it would be set in a release branch.

  // (1b) Policies: UserAgentReduction and MajorVersionInMinor force enabled.
  product = GetProductAndVersion(
      ForceMajorVersionToMinorPosition::kForceEnabled,
      UserAgentReductionEnterprisePolicyState::kForceEnabled);
  EXPECT_TRUE(re2::RE2::FullMatch(product, kChromeProductVersionRegex,
                                  &major_version, &minor_version,
                                  &build_version, &patch_version));
  EXPECT_EQ(major_version, "99");
  EXPECT_EQ(minor_version, version_info::GetMajorVersionNumber());
  EXPECT_EQ(build_version, "0");
  EXPECT_EQ(patch_version, "0");

  // (1c) Policies:: UserAgentReduction and MajorVersionInMinor force disabled.
  product = GetProductAndVersion(
      ForceMajorVersionToMinorPosition::kForceDisabled,
      UserAgentReductionEnterprisePolicyState::kForceDisabled);
  EXPECT_TRUE(re2::RE2::FullMatch(product, kChromeProductVersionRegex,
                                  &major_version, &minor_version,
                                  &build_version));
  EXPECT_EQ(major_version, version_info::GetMajorVersionNumber());
  EXPECT_EQ(minor_version, "0");
  EXPECT_NE(build_version, "0");
  // Patch version cannot be tested as it would be set in a release branch.

  // (2) Features: UserAgentReduction enabled with version and
  // MajorVersionInMinor disabled.
  scoped_feature_list.Reset();
  scoped_feature_list.InitWithFeaturesAndParameters(
      /*enabled_features=*/{{blink::features::kReduceUserAgentMinorVersion,
                             {{{"build_version", "5555"}}}}},
      /*disabled_features=*/{
          blink::features::kForceMajorVersionInMinorPositionInUserAgent});

  // (2a) Policies: UserAgentReduction and MajorVersionInMinor default.
  product =
      GetProductAndVersion(ForceMajorVersionToMinorPosition::kDefault,
                           UserAgentReductionEnterprisePolicyState::kDefault);
  EXPECT_TRUE(re2::RE2::FullMatch(product, kChromeProductVersionRegex,
                                  &major_version, &minor_version,
                                  &build_version, &patch_version));
  EXPECT_EQ(major_version, version_info::GetMajorVersionNumber());
  EXPECT_EQ(minor_version, "0");
  EXPECT_EQ(build_version, "5555");
  EXPECT_EQ(patch_version, "0");

  // (2b) Policies: UserAgentReduction and MajorVersionInMinor force enabled.
  product = GetProductAndVersion(
      ForceMajorVersionToMinorPosition::kForceEnabled,
      UserAgentReductionEnterprisePolicyState::kForceEnabled);
  EXPECT_TRUE(re2::RE2::FullMatch(product, kChromeProductVersionRegex,
                                  &major_version, &minor_version,
                                  &build_version, &patch_version));
  EXPECT_EQ(major_version, "99");
  EXPECT_EQ(minor_version, version_info::GetMajorVersionNumber());
  EXPECT_EQ(build_version, "0");
  EXPECT_EQ(patch_version, "0");

  // (2c) Policies:: UserAgentReduction and MajorVersionInMinor force disabled.
  product = GetProductAndVersion(
      ForceMajorVersionToMinorPosition::kForceDisabled,
      UserAgentReductionEnterprisePolicyState::kForceDisabled);
  EXPECT_TRUE(re2::RE2::FullMatch(product, kChromeProductVersionRegex,
                                  &major_version, &minor_version,
                                  &build_version));
  EXPECT_EQ(major_version, version_info::GetMajorVersionNumber());
  EXPECT_EQ(minor_version, "0");
  EXPECT_NE(build_version, "5555");
  // Patch version cannot be tested as it would be set in a release branch.

  // (3) Features: UserAgentReduction disabled and MajorVersionInMinor enabled.
  scoped_feature_list.Reset();
  scoped_feature_list.InitWithFeatures(
      /*enabled_features=*/{blink::features::
                                kForceMajorVersionInMinorPositionInUserAgent},
      /*disabled_features=*/{blink::features::kReduceUserAgentMinorVersion});

  // (3a) Policies: UserAgentReduction and MajorVersionInMinor default.
  product =
      GetProductAndVersion(ForceMajorVersionToMinorPosition::kDefault,
                           UserAgentReductionEnterprisePolicyState::kDefault);
  EXPECT_TRUE(re2::RE2::FullMatch(product, kChromeProductVersionRegex,
                                  &major_version, &minor_version,
                                  &build_version));
  EXPECT_EQ(major_version, "99");
  EXPECT_EQ(minor_version, version_info::GetMajorVersionNumber());
  EXPECT_NE(build_version, "0");
  // Patch version cannot be tested as it would be set in a release branch.

  // (3b) Policies: UserAgentReduction and MajorVersionInMinor force enabled.
  product = GetProductAndVersion(
      ForceMajorVersionToMinorPosition::kForceEnabled,
      UserAgentReductionEnterprisePolicyState::kForceEnabled);
  EXPECT_TRUE(re2::RE2::FullMatch(product, kChromeProductVersionRegex,
                                  &major_version, &minor_version,
                                  &build_version, &patch_version));
  EXPECT_EQ(major_version, "99");
  EXPECT_EQ(minor_version, version_info::GetMajorVersionNumber());
  EXPECT_EQ(build_version, "0");
  EXPECT_EQ(patch_version, "0");

  // (3c) Policies:: UserAgentReduction and MajorVersionInMinor force disabled.
  product = GetProductAndVersion(
      ForceMajorVersionToMinorPosition::kForceDisabled,
      UserAgentReductionEnterprisePolicyState::kForceDisabled);
  EXPECT_TRUE(re2::RE2::FullMatch(product, kChromeProductVersionRegex,
                                  &major_version, &minor_version,
                                  &build_version));
  EXPECT_EQ(major_version, version_info::GetMajorVersionNumber());
  EXPECT_EQ(minor_version, "0");
  EXPECT_NE(build_version, "0");
  // Patch version cannot be tested as it would be set in a release branch.

  // (4) Features: UserAgentReduction enabled and MajorVersionInMinor disabled.
  scoped_feature_list.Reset();
  scoped_feature_list.InitWithFeatures(
      /*enabled_features=*/{blink::features::kReduceUserAgentMinorVersion},
      /*disabled_features=*/{
          blink::features::kForceMajorVersionInMinorPositionInUserAgent});

  // (4a) Policies: UserAgentReduction and MajorVersionInMinor default.
  product =
      GetProductAndVersion(ForceMajorVersionToMinorPosition::kDefault,
                           UserAgentReductionEnterprisePolicyState::kDefault);
  EXPECT_TRUE(re2::RE2::FullMatch(product, kChromeProductVersionRegex,
                                  &major_version, &minor_version,
                                  &build_version, &patch_version));
  EXPECT_EQ(major_version, version_info::GetMajorVersionNumber());
  EXPECT_EQ(minor_version, "0");
  EXPECT_EQ(build_version, "0");
  EXPECT_EQ(patch_version, "0");

  // (4b) Policies: UserAgentReduction and MajorVersionInMinor force enabled.
  product = GetProductAndVersion(
      ForceMajorVersionToMinorPosition::kForceEnabled,
      UserAgentReductionEnterprisePolicyState::kForceEnabled);
  EXPECT_TRUE(re2::RE2::FullMatch(product, kChromeProductVersionRegex,
                                  &major_version, &minor_version,
                                  &build_version, &patch_version));
  EXPECT_EQ(major_version, "99");
  EXPECT_EQ(minor_version, version_info::GetMajorVersionNumber());
  EXPECT_EQ(build_version, "0");
  EXPECT_EQ(patch_version, "0");

  // (4c) Policies:: UserAgentReduction and MajorVersionInMinor force disabled.
  product = GetProductAndVersion(
      ForceMajorVersionToMinorPosition::kForceDisabled,
      UserAgentReductionEnterprisePolicyState::kForceDisabled);
  EXPECT_TRUE(re2::RE2::FullMatch(product, kChromeProductVersionRegex,
                                  &major_version, &minor_version,
                                  &build_version));
  EXPECT_EQ(major_version, version_info::GetMajorVersionNumber());
  EXPECT_EQ(minor_version, "0");
  EXPECT_NE(build_version, "0");
  // Patch version cannot be tested as it would be set in a release branch.

  // (5) Features: UserAgentReduction and MajorVersionInMinor enabled.
  scoped_feature_list.Reset();
  scoped_feature_list.InitWithFeatures(
      /*enabled_features=*/{blink::features::kReduceUserAgentMinorVersion,
                            blink::features::
                                kForceMajorVersionInMinorPositionInUserAgent},
      /*disabled_features=*/{});

  // (5a) Policies: UserAgentReduction and MajorVersionInMinor default.
  product =
      GetProductAndVersion(ForceMajorVersionToMinorPosition::kDefault,
                           UserAgentReductionEnterprisePolicyState::kDefault);
  EXPECT_TRUE(re2::RE2::FullMatch(product, kChromeProductVersionRegex,
                                  &major_version, &minor_version,
                                  &build_version, &patch_version));
  EXPECT_EQ(major_version, "99");
  EXPECT_EQ(minor_version, version_info::GetMajorVersionNumber());
  EXPECT_EQ(build_version, "0");
  EXPECT_EQ(patch_version, "0");

  // (5b) Policies: UserAgentReduction and MajorVersionInMinor force enabled.
  product = GetProductAndVersion(
      ForceMajorVersionToMinorPosition::kForceEnabled,
      UserAgentReductionEnterprisePolicyState::kForceEnabled);
  EXPECT_TRUE(re2::RE2::FullMatch(product, kChromeProductVersionRegex,
                                  &major_version, &minor_version,
                                  &build_version, &patch_version));
  EXPECT_EQ(major_version, "99");
  EXPECT_EQ(minor_version, version_info::GetMajorVersionNumber());
  EXPECT_EQ(build_version, "0");
  EXPECT_EQ(patch_version, "0");

  // (5c) Policies:: UserAgentReduction and MajorVersionInMinor force disabled.
  product = GetProductAndVersion(
      ForceMajorVersionToMinorPosition::kForceDisabled,
      UserAgentReductionEnterprisePolicyState::kForceDisabled);
  EXPECT_TRUE(re2::RE2::FullMatch(product, kChromeProductVersionRegex,
                                  &major_version, &minor_version,
                                  &build_version));
  EXPECT_EQ(major_version, version_info::GetMajorVersionNumber());
  EXPECT_EQ(minor_version, "0");
  EXPECT_NE(build_version, "0");
  // Patch version cannot be tested as it would be set in a release branch.
}

TEST_F(UserAgentUtilsTest, GetUserAgent) {
  const std::string ua = GetUserAgent();
  std::string major_version;
  std::string minor_version;
  EXPECT_TRUE(re2::RE2::PartialMatch(ua, kChromeProductVersionRegex,
                                     &major_version, &minor_version));
  EXPECT_EQ(major_version, version_info::GetMajorVersionNumber());
  // Minor version should contain the actual minor version number.
  EXPECT_EQ(minor_version, "0");
}

class UserAgentUtilsMinorVersionTest
    : public testing::Test,
      public testing::WithParamInterface<bool> {
 public:
  void SetUp() override {
    if (ForceMajorInMinorVersion())
      scoped_feature_list_.InitAndEnableFeature(
          blink::features::kForceMajorVersionInMinorPositionInUserAgent);
  }

  bool ForceMajorInMinorVersion() { return GetParam(); }

 private:
  base::test::ScopedFeatureList scoped_feature_list_;
};

}  // namespace embedder_support
