// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/signin/dice_web_signin_interceptor.h"

#include <memory>

#include "base/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/metrics/histogram_tester.h"
#include "build/buildflag.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/signin/chrome_signin_client_factory.h"
#include "chrome/browser/signin/chrome_signin_client_test_util.h"
#include "chrome/browser/signin/identity_test_environment_profile_adaptor.h"
#include "chrome/browser/signin/signin_features.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/testing_profile.h"
#include "chrome/test/base/testing_profile_manager.h"
#include "components/prefs/pref_service.h"
#include "components/signin/public/base/signin_pref_names.h"
#include "components/signin/public/identity_manager/account_info.h"
#include "components/signin/public/identity_manager/identity_test_environment.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "url/gurl.h"

namespace {

class MockDiceWebSigninInterceptorDelegate
    : public DiceWebSigninInterceptor::Delegate {
 public:
  MOCK_METHOD(std::unique_ptr<ScopedDiceWebSigninInterceptionBubbleHandle>,
              ShowSigninInterceptionBubble,
              (content::WebContents * web_contents,
               const BubbleParameters& bubble_parameters,
               base::OnceCallback<void(SigninInterceptionResult)> callback),
              (override));
  void ShowFirstRunExperienceInNewProfile(
      Browser* browser,
      const CoreAccountId& account_id,
      DiceWebSigninInterceptor::SigninInterceptionType interception_type)
      override {}
};

// Matches BubbleParameters fields excepting the color. This is useful in the
// test because the color is randomly generated.
testing::Matcher<const DiceWebSigninInterceptor::Delegate::BubbleParameters&>
MatchBubbleParameters(
    const DiceWebSigninInterceptor::Delegate::BubbleParameters& parameters) {
  return testing::AllOf(
      testing::Field("interception_type",
                     &DiceWebSigninInterceptor::Delegate::BubbleParameters::
                         interception_type,
                     parameters.interception_type),
      testing::Field("intercepted_account",
                     &DiceWebSigninInterceptor::Delegate::BubbleParameters::
                         intercepted_account,
                     parameters.intercepted_account),
      testing::Field("primary_account",
                     &DiceWebSigninInterceptor::Delegate::BubbleParameters::
                         primary_account,
                     parameters.primary_account),
      testing::Field("show_link_data_option",
                     &DiceWebSigninInterceptor::Delegate::BubbleParameters::
                         show_link_data_option,
                     parameters.show_link_data_option));
}

// If the account info is valid, does nothing. Otherwise fills the extended
// fields with default values.
void MakeValidAccountInfo(
    AccountInfo* info,
    const std::string& hosted_domain = kNoHostedDomainFound) {
  if (info->IsValid())
    return;
  info->full_name = "fullname";
  info->given_name = "givenname";
  info->hosted_domain = hosted_domain;
  info->locale = "en";
  info->picture_url = "https://example.com";
  DCHECK(info->IsValid());
}

}  // namespace

class DiceWebSigninInterceptorTest : public BrowserWithTestWindowTest {
 public:
  DiceWebSigninInterceptorTest() = default;
  ~DiceWebSigninInterceptorTest() override = default;

  DiceWebSigninInterceptor* interceptor() {
    return dice_web_signin_interceptor_.get();
  }

  MockDiceWebSigninInterceptorDelegate* mock_delegate() {
    return mock_delegate_;
  }

  content::WebContents* web_contents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  ProfileAttributesStorage* profile_attributes_storage() {
    return profile_manager()->profile_attributes_storage();
  }

  signin::IdentityTestEnvironment* identity_test_env() {
    return identity_test_env_profile_adaptor_->identity_test_env();
  }

  Profile* CreateTestingProfile(const std::string& name) {
    return profile_manager()->CreateTestingProfile(name);
  }

  // Helper function that calls MaybeInterceptWebSignin with parameters
  // compatible with interception.
  void MaybeIntercept(CoreAccountId account_id) {
    interceptor()->MaybeInterceptWebSignin(web_contents(), account_id,
                                           /*is_new_account=*/true,
                                           /*is_sync_signin=*/false);
  }

  // Calls MaybeInterceptWebSignin and verifies the heuristic outcome, the
  // histograms and whether the interception is in progress.
  // This function only works if the interception decision can be made
  // synchronously (GetHeuristicOutcome() returns a value).
  void TestSynchronousInterception(
      AccountInfo account_info,
      bool is_new_account,
      bool is_sync_signin,
      SigninInterceptionHeuristicOutcome expected_outcome) {
    ASSERT_EQ(interceptor()->GetHeuristicOutcome(is_new_account, is_sync_signin,
                                                 account_info.email,
                                                 /*entry=*/nullptr),
              expected_outcome);
    base::HistogramTester histogram_tester;
    interceptor()->MaybeInterceptWebSignin(web_contents(),
                                           account_info.account_id,
                                           is_new_account, is_sync_signin);
    testing::Mock::VerifyAndClearExpectations(mock_delegate());
    histogram_tester.ExpectUniqueSample("Signin.Intercept.HeuristicOutcome",
                                        expected_outcome, 1);
    EXPECT_EQ(interceptor()->is_interception_in_progress(),
              SigninInterceptionHeuristicOutcomeIsSuccess(expected_outcome));
  }

  // Calls MaybeInterceptWebSignin and verifies the heuristic outcome and the
  // histograms.
  // This function only works if the interception decision cannot be made
  // synchronously (GetHeuristicOutcome() returns no value).
  void TestAsynchronousInterception(
      AccountInfo account_info,
      bool is_new_account,
      bool is_sync_signin,
      SigninInterceptionHeuristicOutcome expected_outcome) {
    ASSERT_EQ(interceptor()->GetHeuristicOutcome(is_new_account, is_sync_signin,
                                                 account_info.email,
                                                 /*entry=*/nullptr),
              absl::nullopt);
    base::HistogramTester histogram_tester;
    interceptor()->MaybeInterceptWebSignin(web_contents(),
                                           account_info.account_id,
                                           is_new_account, is_sync_signin);
    testing::Mock::VerifyAndClearExpectations(mock_delegate());
    histogram_tester.ExpectUniqueSample("Signin.Intercept.HeuristicOutcome",
                                        expected_outcome, 1);
    EXPECT_EQ(interceptor()->is_interception_in_progress(),
              SigninInterceptionHeuristicOutcomeIsSuccess(expected_outcome));
  }

 protected:
  // testing::Test:
  void SetUp() override {
    BrowserWithTestWindowTest::SetUp();

    identity_test_env_profile_adaptor_ =
        std::make_unique<IdentityTestEnvironmentProfileAdaptor>(profile());
    identity_test_env_profile_adaptor_->identity_test_env()
        ->SetTestURLLoaderFactory(&test_url_loader_factory_);

    auto delegate = std::make_unique<
        testing::StrictMock<MockDiceWebSigninInterceptorDelegate>>();
    mock_delegate_ = delegate.get();
    dice_web_signin_interceptor_ = std::make_unique<DiceWebSigninInterceptor>(
        profile(), std::move(delegate));

    // Create the first tab so that web_contents() exists.
    AddTab(browser(), GURL("http://foo/1"));
  }

 private:
  void TearDown() override {
    dice_web_signin_interceptor_->Shutdown();
    identity_test_env_profile_adaptor_.reset();
    BrowserWithTestWindowTest::TearDown();
  }

  TestingProfile::TestingFactories GetTestingFactories() override {
    TestingProfile::TestingFactories factories =
        IdentityTestEnvironmentProfileAdaptor::
            GetIdentityTestEnvironmentFactories();
    factories.push_back(
        {ChromeSigninClientFactory::GetInstance(),
         base::BindRepeating(&BuildChromeSigninClientWithURLLoader,
                             &test_url_loader_factory_)});
    return factories;
  }

  network::TestURLLoaderFactory test_url_loader_factory_;
  std::unique_ptr<IdentityTestEnvironmentProfileAdaptor>
      identity_test_env_profile_adaptor_;
  std::unique_ptr<DiceWebSigninInterceptor> dice_web_signin_interceptor_;
  raw_ptr<MockDiceWebSigninInterceptorDelegate> mock_delegate_ = nullptr;
};

TEST_F(DiceWebSigninInterceptorTest, ShouldShowProfileSwitchBubble) {
  AccountInfo account_info =
      identity_test_env()->MakeAccountAvailable("bob@example.com");
  const std::string& email = account_info.email;
  EXPECT_FALSE(interceptor()->ShouldShowProfileSwitchBubble(
      email, profile_attributes_storage()));

  // Add another profile with no account.
  CreateTestingProfile("Profile 1");
  EXPECT_FALSE(interceptor()->ShouldShowProfileSwitchBubble(
      email, profile_attributes_storage()));

  // Add another profile with a different account.
  Profile* profile_2 = CreateTestingProfile("Profile 2");
  ProfileAttributesEntry* entry =
      profile_attributes_storage()->GetProfileAttributesWithPath(
          profile_2->GetPath());
  ASSERT_NE(entry, nullptr);
  std::string kOtherGaiaID = "SomeOtherGaiaID";
  ASSERT_NE(kOtherGaiaID, account_info.gaia);
  entry->SetAuthInfo(kOtherGaiaID, u"alice@gmail.com",
                     /*is_consented_primary_account=*/true);
  EXPECT_FALSE(interceptor()->ShouldShowProfileSwitchBubble(
      email, profile_attributes_storage()));

  // Change the account to match.
  entry->SetAuthInfo(account_info.gaia, base::UTF8ToUTF16(email),
                     /*is_consented_primary_account=*/false);
  const ProfileAttributesEntry* switch_to_entry =
      interceptor()->ShouldShowProfileSwitchBubble(
          email, profile_attributes_storage());
  EXPECT_EQ(entry, switch_to_entry);
}

TEST_F(DiceWebSigninInterceptorTest, NoBubbleWithSingleAccount) {
  AccountInfo account_info =
      identity_test_env()->MakeAccountAvailable("bob@example.com");
  MakeValidAccountInfo(&account_info, "example.com");
  identity_test_env()->UpdateAccountInfoForAccount(account_info);

  // Without UPA.
  EXPECT_FALSE(interceptor()->ShouldShowEnterpriseBubble(account_info));
  EXPECT_FALSE(interceptor()->ShouldShowMultiUserBubble(account_info));

  // With UPA.
  identity_test_env()->SetPrimaryAccount("bob@example.com",
                                         signin::ConsentLevel::kSignin);
  EXPECT_FALSE(interceptor()->ShouldShowEnterpriseBubble(account_info));
}

TEST_F(DiceWebSigninInterceptorTest, ShouldShowEnterpriseBubble) {
  // Setup 3 accounts in the profile:
  // - primary account
  // - other enterprise account that is not primary (should be ignored)
  // - intercepted account.
  AccountInfo primary_account_info =
      identity_test_env()->MakePrimaryAccountAvailable(
          "alice@example.com", signin::ConsentLevel::kSignin);
  AccountInfo other_account_info =
      identity_test_env()->MakeAccountAvailable("dummy@example.com");
  MakeValidAccountInfo(&other_account_info);
  other_account_info.hosted_domain = "example.com";
  identity_test_env()->UpdateAccountInfoForAccount(other_account_info);
  AccountInfo account_info =
      identity_test_env()->MakeAccountAvailable("bob@example.com");
  MakeValidAccountInfo(&account_info);
  identity_test_env()->UpdateAccountInfoForAccount(account_info);
  ASSERT_EQ(identity_test_env()->identity_manager()->GetPrimaryAccountId(
                signin::ConsentLevel::kSignin),
            primary_account_info.account_id);

  // The primary account does not have full account info (empty domain).
  ASSERT_TRUE(identity_test_env()
                  ->identity_manager()
                  ->FindExtendedAccountInfo(primary_account_info)
                  .hosted_domain.empty());
  EXPECT_FALSE(interceptor()->ShouldShowEnterpriseBubble(account_info));
  account_info.hosted_domain = "example.com";
  identity_test_env()->UpdateAccountInfoForAccount(account_info);
  EXPECT_TRUE(interceptor()->ShouldShowEnterpriseBubble(account_info));

  // The primary account has full info.
  MakeValidAccountInfo(&primary_account_info);
  identity_test_env()->UpdateAccountInfoForAccount(primary_account_info);
  // The intercepted account is enterprise.
  EXPECT_TRUE(interceptor()->ShouldShowEnterpriseBubble(account_info));
  // Two consummer accounts.
  account_info.hosted_domain = kNoHostedDomainFound;
  identity_test_env()->UpdateAccountInfoForAccount(account_info);
  EXPECT_FALSE(interceptor()->ShouldShowEnterpriseBubble(account_info));
  // The primary account is enterprise.
  primary_account_info.hosted_domain = "example.com";
  identity_test_env()->UpdateAccountInfoForAccount(primary_account_info);
  EXPECT_TRUE(interceptor()->ShouldShowEnterpriseBubble(account_info));
}

TEST_F(DiceWebSigninInterceptorTest, ShouldEnforceEnterpriseProfileSeparation) {
  profile()->GetPrefs()->SetBoolean(
      prefs::kManagedAccountsSigninRestrictionScopeMachine, true);
  profile()->GetPrefs()->SetString(prefs::kManagedAccountsSigninRestriction,
                                   "primary_account_strict");

  // Setup 3 accounts in the profile:
  // - primary account
  // - other enterprise account that is not primary (should be ignored)
  // - intercepted account.
  AccountInfo primary_account_info =
      identity_test_env()->MakePrimaryAccountAvailable(
          "alice@gmail.com", signin::ConsentLevel::kSignin);

  AccountInfo other_account_info =
      identity_test_env()->MakeAccountAvailable("dummy@example.com");
  MakeValidAccountInfo(&other_account_info);
  other_account_info.hosted_domain = "example.com";
  identity_test_env()->UpdateAccountInfoForAccount(other_account_info);
  AccountInfo account_info =
      identity_test_env()->MakeAccountAvailable("bob@example.com");
  MakeValidAccountInfo(&account_info);
  identity_test_env()->UpdateAccountInfoForAccount(account_info);
  ASSERT_EQ(identity_test_env()->identity_manager()->GetPrimaryAccountId(
                signin::ConsentLevel::kSignin),
            primary_account_info.account_id);
  interceptor()->new_account_interception_ = true;
  // Consumer account not intercepted.
  EXPECT_FALSE(
      interceptor()->ShouldEnforceEnterpriseProfileSeparation(account_info));
  account_info.hosted_domain = "example.com";
  identity_test_env()->UpdateAccountInfoForAccount(account_info);
  // Managed account intercepted.
  EXPECT_TRUE(
      interceptor()->ShouldEnforceEnterpriseProfileSeparation(account_info));
}

TEST_F(DiceWebSigninInterceptorTest,
       ShouldEnforceEnterpriseProfileSeparationWithoutUPA) {
  profile()->GetPrefs()->SetBoolean(
      prefs::kManagedAccountsSigninRestrictionScopeMachine, true);
  profile()->GetPrefs()->SetString(prefs::kManagedAccountsSigninRestriction,
                                   "primary_account_strict");
  AccountInfo account_info_1 =
      identity_test_env()->MakeAccountAvailable("bob@example.com");
  MakeValidAccountInfo(&account_info_1);
  account_info_1.hosted_domain = "example.com";
  identity_test_env()->UpdateAccountInfoForAccount(account_info_1);

  interceptor()->new_account_interception_ = true;
  // Primary account is not set.
  ASSERT_FALSE(identity_test_env()->identity_manager()->HasPrimaryAccount(
      signin::ConsentLevel::kSignin));
  EXPECT_TRUE(
      interceptor()->ShouldEnforceEnterpriseProfileSeparation(account_info_1));
}

TEST_F(DiceWebSigninInterceptorTest,
       ShouldEnforceEnterpriseProfileSeparationReauth) {
  profile()->GetPrefs()->SetString(prefs::kManagedAccountsSigninRestriction,
                                   "primary_account_strict");
  AccountInfo primary_account_info =
      identity_test_env()->MakePrimaryAccountAvailable(
          "alice@example.com", signin::ConsentLevel::kSignin);
  MakeValidAccountInfo(&primary_account_info);
  primary_account_info.hosted_domain = "example.com";
  identity_test_env()->UpdateAccountInfoForAccount(primary_account_info);

  // Primary account is set.
  ASSERT_TRUE(identity_test_env()->identity_manager()->HasPrimaryAccount(
      signin::ConsentLevel::kSignin));
  EXPECT_TRUE(interceptor()->ShouldEnforceEnterpriseProfileSeparation(
      primary_account_info));

  ProfileAttributesEntry* entry =
      profile_attributes_storage()->GetProfileAttributesWithPath(
          profile()->GetPath());
  entry->SetUserAcceptedAccountManagement(true);

  EXPECT_FALSE(interceptor()->ShouldEnforceEnterpriseProfileSeparation(
      primary_account_info));
}

class DiceWebSigninInterceptorManagedAccountTest
    : public DiceWebSigninInterceptorTest,
      public testing::WithParamInterface<bool> {
 protected:
  void SetUp() override {
    DiceWebSigninInterceptorTest::SetUp();
    profile()->GetPrefs()->SetBoolean(prefs::kSigninInterceptionEnabled,
                                      GetParam());
  }
};

TEST_P(DiceWebSigninInterceptorManagedAccountTest,
       NoForcedInterceptionShowsDialogIfFeatureEnabled) {
  base::test::ScopedFeatureList scoped_list;
  scoped_list.InitAndEnableFeature(
      kShowEnterpriseDialogForAllManagedAccountsSignin);
  // Reauth intercepted if enterprise confirmation not shown yet for forced
  // managed separation.
  AccountInfo account_info = identity_test_env()->MakePrimaryAccountAvailable(
      "alice@example.com", signin::ConsentLevel::kSignin);
  MakeValidAccountInfo(&account_info, "example.com");
  identity_test_env()->UpdateAccountInfoForAccount(account_info);
  interceptor()->SetAccountLevelSigninRestrictionFetchResultForTesting("");

  DiceWebSigninInterceptor::Delegate::BubbleParameters expected_parameters(
      DiceWebSigninInterceptor::SigninInterceptionType::
          kEnterpriseAcceptManagement,
      account_info, account_info, SkColor(), /*show_guest_option=*/false,
      /*show_link_data_option=*/true);
  EXPECT_CALL(*mock_delegate(),
              ShowSigninInterceptionBubble(
                  web_contents(), MatchBubbleParameters(expected_parameters),
                  testing::_));
  TestAsynchronousInterception(
      account_info, /*is_new_account=*/true, /*is_sync_signin=*/false,
      SigninInterceptionHeuristicOutcome::kInterceptEnterprise);
}

TEST_P(DiceWebSigninInterceptorManagedAccountTest,
       NoForcedInterceptionShowsNoBubble) {
  // Reauth intercepted if enterprise confirmation not shown yet for forced
  // managed separation.
  AccountInfo account_info = identity_test_env()->MakePrimaryAccountAvailable(
      "alice@example.com", signin::ConsentLevel::kSignin);
  MakeValidAccountInfo(&account_info, "example.com");
  identity_test_env()->UpdateAccountInfoForAccount(account_info);
  interceptor()->SetAccountLevelSigninRestrictionFetchResultForTesting("");

  bool signin_interception_enabled = GetParam();
  if (signin_interception_enabled) {
    TestAsynchronousInterception(
        account_info, /*is_new_account=*/true, /*is_sync_signin=*/false,
        SigninInterceptionHeuristicOutcome::kAbortAccountInfoNotCompatible);
  } else {
    TestAsynchronousInterception(
        account_info, /*is_new_account=*/true, /*is_sync_signin=*/false,
        SigninInterceptionHeuristicOutcome::kAbortInterceptionDisabled);
  }
}

TEST_P(DiceWebSigninInterceptorManagedAccountTest,
       EnforceManagedAccountAsPrimaryReauth) {
  profile()->GetPrefs()->SetBoolean(
      prefs::kManagedAccountsSigninRestrictionScopeMachine, true);
  profile()->GetPrefs()->SetString(prefs::kManagedAccountsSigninRestriction,
                                   "primary_account");

  // Reauth intercepted if enterprise confirmation not shown yet for forced
  // managed separation.
  AccountInfo account_info = identity_test_env()->MakePrimaryAccountAvailable(
      "alice@example.com", signin::ConsentLevel::kSignin);
  MakeValidAccountInfo(&account_info, "example.com");
  identity_test_env()->UpdateAccountInfoForAccount(account_info);
  profile()->GetPrefs()->SetString(prefs::kManagedAccountsSigninRestriction,
                                   "primary_account");

  // Check that interception works otherwise, as a sanity check.
  DiceWebSigninInterceptor::Delegate::BubbleParameters expected_parameters(
      DiceWebSigninInterceptor::SigninInterceptionType::kEnterpriseForced,
      account_info, account_info);
  EXPECT_CALL(*mock_delegate(),
              ShowSigninInterceptionBubble(
                  web_contents(), MatchBubbleParameters(expected_parameters),
                  testing::_));

  TestSynchronousInterception(
      account_info, /*is_new_account=*/false, /*is_sync_signin=*/false,
      SigninInterceptionHeuristicOutcome::kInterceptEnterpriseForced);
}

TEST_P(DiceWebSigninInterceptorManagedAccountTest,
       EnforceManagedAccountAsPrimaryManaged) {
  AccountInfo account_info =
      identity_test_env()->MakeAccountAvailable("alice@example.com");
  MakeValidAccountInfo(&account_info, "example.com");
  identity_test_env()->UpdateAccountInfoForAccount(account_info);

  profile()->GetPrefs()->SetString(prefs::kManagedAccountsSigninRestriction,
                                   "primary_account_strict");

  // Check that interception works otherwise, as a sanity check.
  DiceWebSigninInterceptor::Delegate::BubbleParameters expected_parameters(
      DiceWebSigninInterceptor::SigninInterceptionType::kEnterpriseForced,
      account_info, AccountInfo());
  EXPECT_CALL(*mock_delegate(),
              ShowSigninInterceptionBubble(
                  web_contents(), MatchBubbleParameters(expected_parameters),
                  testing::_));
  TestSynchronousInterception(
      account_info, /*is_new_account=*/true, /*is_sync_signin=*/false,
      SigninInterceptionHeuristicOutcome::kInterceptEnterpriseForced);
}

TEST_P(DiceWebSigninInterceptorManagedAccountTest,
       EnforceManagedAccountAsPrimaryManagedLinkData) {
  AccountInfo account_info =
      identity_test_env()->MakeAccountAvailable("alice@example.com");
  MakeValidAccountInfo(&account_info, "example.com");
  identity_test_env()->UpdateAccountInfoForAccount(account_info);

  interceptor()->SetAccountLevelSigninRestrictionFetchResultForTesting(
      "primary_account_keep_existing_data");

  // Check that interception works otherwise, as a sanity check.
  DiceWebSigninInterceptor::Delegate::BubbleParameters expected_parameters(
      DiceWebSigninInterceptor::SigninInterceptionType::kEnterpriseForced,
      account_info, AccountInfo(), SkColor(), /*show_guest_option=*/false,
      /*show_link_data_option=*/true);
  EXPECT_CALL(*mock_delegate(),
              ShowSigninInterceptionBubble(
                  web_contents(), MatchBubbleParameters(expected_parameters),
                  testing::_));
  TestAsynchronousInterception(
      account_info, /*is_new_account=*/true, /*is_sync_signin=*/false,
      SigninInterceptionHeuristicOutcome::kInterceptEnterpriseForced);
}

TEST_P(DiceWebSigninInterceptorManagedAccountTest,
       EnforceManagedAccountAsPrimaryManagedLinkDataSecondaryAccount) {
  AccountInfo primary_account_info =
      identity_test_env()->MakePrimaryAccountAvailable(
          "alice@example.com", signin::ConsentLevel::kSignin);
  MakeValidAccountInfo(&primary_account_info, "example.com");
  identity_test_env()->UpdateAccountInfoForAccount(primary_account_info);

  std::string email = "bob@example.com";
  AccountInfo account_info = identity_test_env()->MakeAccountAvailable(email);
  MakeValidAccountInfo(&account_info, "example.com");
  identity_test_env()->UpdateAccountInfoForAccount(account_info);

  profile()->GetPrefs()->SetBoolean(
      prefs::kManagedAccountsSigninRestrictionScopeMachine, true);
  profile()->GetPrefs()->SetString(prefs::kManagedAccountsSigninRestriction,
                                   "primary_account_keep_existing_data");

  // Check that interception works otherwise, as a sanity check.
  DiceWebSigninInterceptor::Delegate::BubbleParameters expected_parameters(
      DiceWebSigninInterceptor::SigninInterceptionType::kEnterpriseForced,
      account_info, primary_account_info, SkColor(),
      /*show_guest_option=*/false,
      /*show_link_data_option=*/false);
  EXPECT_CALL(*mock_delegate(),
              ShowSigninInterceptionBubble(
                  web_contents(), MatchBubbleParameters(expected_parameters),
                  testing::_));
  TestSynchronousInterception(
      account_info, /*is_new_account=*/true, /*is_sync_signin=*/false,
      SigninInterceptionHeuristicOutcome::kInterceptEnterpriseForced);
}

TEST_P(DiceWebSigninInterceptorManagedAccountTest,
       EnforceManagedAccountAsPrimaryManagedStrictLinkData) {
  AccountInfo account_info =
      identity_test_env()->MakeAccountAvailable("alice@example.com");
  MakeValidAccountInfo(&account_info, "example.com");
  identity_test_env()->UpdateAccountInfoForAccount(account_info);

  profile()->GetPrefs()->SetString(prefs::kManagedAccountsSigninRestriction,
                                   "primary_account_strict_keep_existing_data");

  // Check that interception works otherwise, as a sanity check.
  DiceWebSigninInterceptor::Delegate::BubbleParameters expected_parameters(
      DiceWebSigninInterceptor::SigninInterceptionType::kEnterpriseForced,
      account_info, AccountInfo(), SkColor(), /*show_guest_option=*/false,
      /*show_link_data_option=*/true);
  EXPECT_CALL(*mock_delegate(),
              ShowSigninInterceptionBubble(
                  web_contents(), MatchBubbleParameters(expected_parameters),
                  testing::_));
  TestSynchronousInterception(
      account_info, /*is_new_account=*/true, /*is_sync_signin=*/false,
      SigninInterceptionHeuristicOutcome::kInterceptEnterpriseForced);
}

TEST_P(DiceWebSigninInterceptorManagedAccountTest,
       EnforceManagedAccountAsPrimaryManagedStrictLinkDataSecondaryAccount) {
  AccountInfo primary_account_info =
      identity_test_env()->MakePrimaryAccountAvailable(
          "alice@example.com", signin::ConsentLevel::kSignin);
  MakeValidAccountInfo(&primary_account_info, "example.com");
  identity_test_env()->UpdateAccountInfoForAccount(primary_account_info);

  std::string email = "bob@example.com";
  AccountInfo account_info = identity_test_env()->MakeAccountAvailable(email);
  MakeValidAccountInfo(&account_info, "example.com");
  identity_test_env()->UpdateAccountInfoForAccount(account_info);

  profile()->GetPrefs()->SetString(prefs::kManagedAccountsSigninRestriction,
                                   "primary_account_strict_keep_existing_data");

  // Check that interception works otherwise, as a sanity check.
  DiceWebSigninInterceptor::Delegate::BubbleParameters expected_parameters(
      DiceWebSigninInterceptor::SigninInterceptionType::kEnterpriseForced,
      account_info, primary_account_info, SkColor(),
      /*show_guest_option=*/false,
      /*show_link_data_option=*/false);
  EXPECT_CALL(*mock_delegate(),
              ShowSigninInterceptionBubble(
                  web_contents(), MatchBubbleParameters(expected_parameters),
                  testing::_));
  TestSynchronousInterception(
      account_info, /*is_new_account=*/true, /*is_sync_signin=*/false,
      SigninInterceptionHeuristicOutcome::kInterceptEnterpriseForced);
}

TEST_P(DiceWebSigninInterceptorManagedAccountTest,
       EnforceManagedAccountAsPrimaryProfileSwitch) {
  AccountInfo account_info =
      identity_test_env()->MakeAccountAvailable("alice@example.com");
  MakeValidAccountInfo(&account_info, "example.com");
  identity_test_env()->UpdateAccountInfoForAccount(account_info);

  profile()->GetPrefs()->SetBoolean(
      prefs::kManagedAccountsSigninRestrictionScopeMachine, true);
  profile()->GetPrefs()->SetString(prefs::kManagedAccountsSigninRestriction,
                                   "primary_account_strict");

  // Setup for profile switch interception.
  Profile* profile_2 = CreateTestingProfile("Profile 2");
  ProfileAttributesEntry* entry =
      profile_attributes_storage()->GetProfileAttributesWithPath(
          profile_2->GetPath());
  ASSERT_NE(entry, nullptr);
  entry->SetAuthInfo(account_info.gaia, base::UTF8ToUTF16(account_info.email),
                     /*is_consented_primary_account=*/false);
  // Check that interception works otherwise, as a sanity check.
  DiceWebSigninInterceptor::Delegate::BubbleParameters expected_parameters(
      DiceWebSigninInterceptor::SigninInterceptionType::kProfileSwitchForced,
      account_info, AccountInfo());
  EXPECT_CALL(*mock_delegate(),
              ShowSigninInterceptionBubble(
                  web_contents(), MatchBubbleParameters(expected_parameters),
                  testing::_));
  TestSynchronousInterception(account_info, /*is_new_account=*/true,
                              /*is_sync_signin=*/false,
                              SigninInterceptionHeuristicOutcome::
                                  kInterceptEnterpriseForcedProfileSwitch);
}

INSTANTIATE_TEST_SUITE_P(All,
                         DiceWebSigninInterceptorManagedAccountTest,
                         ::testing::Bool());

TEST_F(DiceWebSigninInterceptorTest, ShouldShowEnterpriseBubbleWithoutUPA) {
  AccountInfo account_info_1 =
      identity_test_env()->MakeAccountAvailable("bob@example.com");
  MakeValidAccountInfo(&account_info_1);
  account_info_1.hosted_domain = "example.com";
  identity_test_env()->UpdateAccountInfoForAccount(account_info_1);
  AccountInfo account_info_2 =
      identity_test_env()->MakeAccountAvailable("alice@example.com");
  MakeValidAccountInfo(&account_info_2);
  account_info_2.hosted_domain = "example.com";
  identity_test_env()->UpdateAccountInfoForAccount(account_info_2);

  // Primary account is not set.
  ASSERT_FALSE(identity_test_env()->identity_manager()->HasPrimaryAccount(
      signin::ConsentLevel::kSignin));
  EXPECT_FALSE(interceptor()->ShouldShowEnterpriseBubble(account_info_1));
}

TEST_F(DiceWebSigninInterceptorTest, ShouldShowMultiUserBubble) {
  // Setup two accounts in the profile.
  AccountInfo account_info_1 =
      identity_test_env()->MakeAccountAvailable("bob@example.com");
  MakeValidAccountInfo(&account_info_1);
  account_info_1.given_name = "Bob";
  identity_test_env()->UpdateAccountInfoForAccount(account_info_1);
  AccountInfo account_info_2 =
      identity_test_env()->MakeAccountAvailable("alice@example.com");

  // The other account does not have full account info (empty name).
  ASSERT_TRUE(account_info_2.given_name.empty());
  EXPECT_TRUE(interceptor()->ShouldShowMultiUserBubble(account_info_1));

  // Accounts with different names.
  account_info_1.given_name = "Bob";
  identity_test_env()->UpdateAccountInfoForAccount(account_info_1);
  MakeValidAccountInfo(&account_info_2);
  account_info_2.given_name = "Alice";
  identity_test_env()->UpdateAccountInfoForAccount(account_info_2);
  EXPECT_TRUE(interceptor()->ShouldShowMultiUserBubble(account_info_1));

  // Accounts with same names.
  account_info_1.given_name = "Alice";
  identity_test_env()->UpdateAccountInfoForAccount(account_info_1);
  EXPECT_FALSE(interceptor()->ShouldShowMultiUserBubble(account_info_1));

  // Comparison is case insensitive.
  account_info_1.given_name = "alice";
  identity_test_env()->UpdateAccountInfoForAccount(account_info_1);
  EXPECT_FALSE(interceptor()->ShouldShowMultiUserBubble(account_info_1));
}

TEST_F(DiceWebSigninInterceptorTest, NoInterception) {
  // Setup for profile switch interception.
  std::string email = "bob@example.com";
  AccountInfo account_info = identity_test_env()->MakeAccountAvailable(email);
  MakeValidAccountInfo(&account_info, "example.com");
  identity_test_env()->UpdateAccountInfoForAccount(account_info);
  Profile* profile_2 = CreateTestingProfile("Profile 2");
  ProfileAttributesEntry* entry =
      profile_attributes_storage()->GetProfileAttributesWithPath(
          profile_2->GetPath());
  ASSERT_NE(entry, nullptr);
  entry->SetAuthInfo(account_info.gaia, base::UTF8ToUTF16(email),
                     /*is_consented_primary_account=*/false);

  // Check that Sync signin is not intercepted.
  TestSynchronousInterception(
      account_info, /*is_new_account=*/true, /*is_sync_signin=*/true,
      SigninInterceptionHeuristicOutcome::kAbortSyncSignin);

  // Check that reauth is not intercepted.
  TestSynchronousInterception(
      account_info, /*is_new_account=*/false, /*is_sync_signin=*/false,
      SigninInterceptionHeuristicOutcome::kAbortAccountNotNew);

  // Check that interception works otherwise, as a sanity check.
  DiceWebSigninInterceptor::Delegate::BubbleParameters expected_parameters(
      DiceWebSigninInterceptor::SigninInterceptionType::kProfileSwitch,
      account_info, AccountInfo());
  EXPECT_CALL(*mock_delegate(),
              ShowSigninInterceptionBubble(
                  web_contents(), MatchBubbleParameters(expected_parameters),
                  testing::_));
  TestSynchronousInterception(
      account_info, /*is_new_account=*/true, /*is_sync_signin=*/false,
      SigninInterceptionHeuristicOutcome::kInterceptProfileSwitch);
}

// Checks that the heuristic still works if the account was not added to Chrome
// yet.
TEST_F(DiceWebSigninInterceptorTest, HeuristicAccountNotAdded) {
  // Setup for profile switch interception.
  std::string email = "bob@example.com";
  AccountInfo account_info = identity_test_env()->MakeAccountAvailable(email);
  MakeValidAccountInfo(&account_info, "example.com");
  identity_test_env()->UpdateAccountInfoForAccount(account_info);
  Profile* profile_2 = CreateTestingProfile("Profile 2");
  ProfileAttributesEntry* entry =
      profile_attributes_storage()->GetProfileAttributesWithPath(
          profile_2->GetPath());
  ASSERT_NE(entry, nullptr);
  entry->SetAuthInfo("dummy_gaia_id", base::UTF8ToUTF16(email),
                     /*is_consented_primary_account=*/false);
  EXPECT_EQ(interceptor()->GetHeuristicOutcome(
                /*is_new_account=*/true, /*is_sync_signin=*/false, email,
                /*entry=*/nullptr),
            SigninInterceptionHeuristicOutcome::kInterceptProfileSwitch);
}

// Checks that the heuristic defaults to gmail.com when no domain is specified.
TEST_F(DiceWebSigninInterceptorTest, HeuristicDefaultsToGmail) {
  // Setup for profile switch interception.
  std::string email = "bob@gmail.com";
  Profile* profile_2 = CreateTestingProfile("Profile 2");
  ProfileAttributesEntry* entry =
      profile_attributes_storage()->GetProfileAttributesWithPath(
          profile_2->GetPath());
  ASSERT_NE(entry, nullptr);
  entry->SetAuthInfo("dummy_gaia_id", base::UTF8ToUTF16(email),
                     /*is_consented_primary_account=*/false);
  // No domain defaults to gmail.com
  EXPECT_EQ(interceptor()->GetHeuristicOutcome(
                /*is_new_account=*/true, /*is_sync_signin=*/false, "bob",
                /*entry=*/nullptr),
            SigninInterceptionHeuristicOutcome::kInterceptProfileSwitch);
}

// Checks that no heuristic is returned if signin interception is disabled.
TEST_F(DiceWebSigninInterceptorTest, InterceptionDisabled) {
  // Setup for profile switch interception.
  std::string email = "bob@gmail.com";
  Profile* profile_2 = CreateTestingProfile("Profile 2");
  profile()->GetPrefs()->SetBoolean(prefs::kSigninInterceptionEnabled, false);
  ProfileAttributesEntry* entry =
      profile_attributes_storage()->GetProfileAttributesWithPath(
          profile_2->GetPath());
  ASSERT_NE(entry, nullptr);
  entry->SetAuthInfo("dummy_gaia_id", base::UTF8ToUTF16(email),
                     /*is_consented_primary_account=*/false);
  EXPECT_EQ(interceptor()->GetHeuristicOutcome(
                /*is_new_account=*/true, /*is_sync_signin=*/false, "bob",
                /*entry=*/nullptr),
            SigninInterceptionHeuristicOutcome::kAbortInterceptionDisabled);
  EXPECT_EQ(
      interceptor()->GetHeuristicOutcome(
          /*is_new_account=*/true, /*is_sync_signin=*/false, "bob@example.com",
          /*entry=*/nullptr),
      absl::nullopt);

  AccountInfo account_info =
      identity_test_env()->MakeAccountAvailable("bob@example.com");
  MakeValidAccountInfo(&account_info, "example.com");
  identity_test_env()->UpdateAccountInfoForAccount(account_info);
  EXPECT_EQ(
      interceptor()->GetHeuristicOutcome(
          /*is_new_account=*/true, /*is_sync_signin=*/false, "bob@example.com",
          /*entry=*/nullptr),
      SigninInterceptionHeuristicOutcome::kAbortInterceptionDisabled);
}

TEST_F(DiceWebSigninInterceptorTest, TabClosed) {
  base::HistogramTester histogram_tester;
  interceptor()->MaybeInterceptWebSignin(
      /*web_contents=*/nullptr, CoreAccountId(),
      /*is_new_account=*/true, /*is_sync_signin=*/false);
  histogram_tester.ExpectUniqueSample(
      "Signin.Intercept.HeuristicOutcome",
      SigninInterceptionHeuristicOutcome::kAbortTabClosed, 1);
}

TEST_F(DiceWebSigninInterceptorTest, InterceptionInProgress) {
  // Setup for profile switch interception.
  std::string email = "bob@example.com";
  AccountInfo account_info = identity_test_env()->MakeAccountAvailable(email);
  MakeValidAccountInfo(&account_info, "example.com");
  identity_test_env()->UpdateAccountInfoForAccount(account_info);
  Profile* profile_2 = CreateTestingProfile("Profile 2");
  ProfileAttributesEntry* entry =
      profile_attributes_storage()->GetProfileAttributesWithPath(
          profile_2->GetPath());
  ASSERT_NE(entry, nullptr);
  entry->SetAuthInfo(account_info.gaia, base::UTF8ToUTF16(email),
                     /*is_consented_primary_account=*/false);

  // Start an interception.
  DiceWebSigninInterceptor::Delegate::BubbleParameters expected_parameters(
      DiceWebSigninInterceptor::SigninInterceptionType::kProfileSwitch,
      account_info, AccountInfo());
  base::OnceCallback<void(SigninInterceptionResult)> delegate_callback;
  EXPECT_CALL(*mock_delegate(),
              ShowSigninInterceptionBubble(
                  web_contents(), MatchBubbleParameters(expected_parameters),
                  testing::_))
      .WillOnce(testing::WithArg<2>(testing::Invoke(
          [&delegate_callback](
              base::OnceCallback<void(SigninInterceptionResult)> callback) {
            delegate_callback = std::move(callback);
            return nullptr;
          })));
  MaybeIntercept(account_info.account_id);
  testing::Mock::VerifyAndClearExpectations(mock_delegate());
  EXPECT_TRUE(interceptor()->is_interception_in_progress());

  // Check that there is no interception while another one is in progress.
  base::HistogramTester histogram_tester;
  MaybeIntercept(account_info.account_id);
  testing::Mock::VerifyAndClearExpectations(mock_delegate());
  histogram_tester.ExpectUniqueSample(
      "Signin.Intercept.HeuristicOutcome",
      SigninInterceptionHeuristicOutcome::kAbortInterceptInProgress, 1);

  // Complete the interception that was in progress.
  std::move(delegate_callback).Run(SigninInterceptionResult::kDeclined);
  EXPECT_FALSE(interceptor()->is_interception_in_progress());

  // A new interception can now start.
  EXPECT_CALL(*mock_delegate(),
              ShowSigninInterceptionBubble(
                  web_contents(), MatchBubbleParameters(expected_parameters),
                  testing::_));
  MaybeIntercept(account_info.account_id);
}

TEST_F(DiceWebSigninInterceptorTest, DeclineCreationRepeatedly) {
  base::HistogramTester histogram_tester;
  AccountInfo primary_account_info =
      identity_test_env()->MakePrimaryAccountAvailable(
          "bob@example.com", signin::ConsentLevel::kSignin);
  AccountInfo account_info =
      identity_test_env()->MakeAccountAvailable("alice@example.com");
  MakeValidAccountInfo(&account_info, "example.com");
  identity_test_env()->UpdateAccountInfoForAccount(account_info);

  const int kMaxProfileCreationDeclinedCount = 2;
  // Decline the interception kMaxProfileCreationDeclinedCount times.
  DiceWebSigninInterceptor::Delegate::BubbleParameters expected_parameters(
      DiceWebSigninInterceptor::SigninInterceptionType::kEnterprise,
      account_info, primary_account_info);
  for (int i = 0; i < kMaxProfileCreationDeclinedCount; ++i) {
    EXPECT_CALL(*mock_delegate(),
                ShowSigninInterceptionBubble(
                    web_contents(), MatchBubbleParameters(expected_parameters),
                    testing::_))
        .WillOnce(testing::WithArg<2>(testing::Invoke(
            [](base::OnceCallback<void(SigninInterceptionResult)> callback) {
              std::move(callback).Run(SigninInterceptionResult::kDeclined);
              return nullptr;
            })));
    MaybeIntercept(account_info.account_id);
    EXPECT_EQ(interceptor()->is_interception_in_progress(), false);
    histogram_tester.ExpectUniqueSample(
        "Signin.Intercept.HeuristicOutcome",
        SigninInterceptionHeuristicOutcome::kInterceptEnterprise, i + 1);
  }

  // Next time the interception is not shown again.
  MaybeIntercept(account_info.account_id);
  EXPECT_EQ(interceptor()->is_interception_in_progress(), false);
  histogram_tester.ExpectBucketCount(
      "Signin.Intercept.HeuristicOutcome",
      SigninInterceptionHeuristicOutcome::kAbortUserDeclinedProfileForAccount,
      1);

  // Another account can still be intercepted.
  account_info.email = "oscar@example.com";
  identity_test_env()->UpdateAccountInfoForAccount(account_info);
  expected_parameters.intercepted_account = account_info;
  EXPECT_CALL(*mock_delegate(),
              ShowSigninInterceptionBubble(
                  web_contents(), MatchBubbleParameters(expected_parameters),
                  testing::_));
  MaybeIntercept(account_info.account_id);
  histogram_tester.ExpectBucketCount(
      "Signin.Intercept.HeuristicOutcome",
      SigninInterceptionHeuristicOutcome::kInterceptEnterprise,
      kMaxProfileCreationDeclinedCount + 1);
  EXPECT_EQ(interceptor()->is_interception_in_progress(), true);
}

// Regression test for https://crbug.com/1309647
TEST_F(DiceWebSigninInterceptorTest,
       DeclineCreationRepeatedlyWithPolicyFetcher) {
  base::HistogramTester histogram_tester;
  AccountInfo primary_account_info =
      identity_test_env()->MakePrimaryAccountAvailable(
          "bob@example.com", signin::ConsentLevel::kSignin);
  AccountInfo account_info =
      identity_test_env()->MakeAccountAvailable("alice@example.com");
  MakeValidAccountInfo(&account_info, "example.com");
  identity_test_env()->UpdateAccountInfoForAccount(account_info);

  interceptor()->SetAccountLevelSigninRestrictionFetchResultForTesting("");

  const int kMaxProfileCreationDeclinedCount = 2;
  // Decline the interception kMaxProfileCreationDeclinedCount times.
  DiceWebSigninInterceptor::Delegate::BubbleParameters expected_parameters(
      DiceWebSigninInterceptor::SigninInterceptionType::kEnterprise,
      account_info, primary_account_info);
  for (int i = 0; i < kMaxProfileCreationDeclinedCount; ++i) {
    EXPECT_CALL(*mock_delegate(),
                ShowSigninInterceptionBubble(
                    web_contents(), MatchBubbleParameters(expected_parameters),
                    testing::_))
        .WillOnce(testing::WithArg<2>(testing::Invoke(
            [](base::OnceCallback<void(SigninInterceptionResult)> callback) {
              std::move(callback).Run(SigninInterceptionResult::kDeclined);
              return nullptr;
            })));
    MaybeIntercept(account_info.account_id);
    EXPECT_EQ(interceptor()->is_interception_in_progress(), false);
    histogram_tester.ExpectUniqueSample(
        "Signin.Intercept.HeuristicOutcome",
        SigninInterceptionHeuristicOutcome::kInterceptEnterprise, i + 1);
  }

  // Next time the interception is not shown again.
  MaybeIntercept(account_info.account_id);
  EXPECT_EQ(interceptor()->is_interception_in_progress(), false);
  histogram_tester.ExpectBucketCount(
      "Signin.Intercept.HeuristicOutcome",
      SigninInterceptionHeuristicOutcome::kAbortUserDeclinedProfileForAccount,
      1);

  // Another account can still be intercepted.
  account_info.email = "oscar@example.com";
  identity_test_env()->UpdateAccountInfoForAccount(account_info);
  expected_parameters.intercepted_account = account_info;
  EXPECT_CALL(*mock_delegate(),
              ShowSigninInterceptionBubble(
                  web_contents(), MatchBubbleParameters(expected_parameters),
                  testing::_));
  MaybeIntercept(account_info.account_id);
  histogram_tester.ExpectBucketCount(
      "Signin.Intercept.HeuristicOutcome",
      SigninInterceptionHeuristicOutcome::kInterceptEnterprise,
      kMaxProfileCreationDeclinedCount + 1);
  EXPECT_EQ(interceptor()->is_interception_in_progress(), true);
}

TEST_F(DiceWebSigninInterceptorTest, DeclineSwitchRepeatedly_NoLimit) {
  base::HistogramTester histogram_tester;
  // Setup for profile switch interception.
  std::string email = "bob@example.com";
  AccountInfo account_info = identity_test_env()->MakeAccountAvailable(email);
  MakeValidAccountInfo(&account_info, "example.com");
  identity_test_env()->UpdateAccountInfoForAccount(account_info);
  Profile* profile_2 = CreateTestingProfile("Profile 2");
  ProfileAttributesEntry* entry =
      profile_attributes_storage()->GetProfileAttributesWithPath(
          profile_2->GetPath());
  ASSERT_NE(entry, nullptr);
  entry->SetAuthInfo(account_info.gaia, base::UTF8ToUTF16(email),
                     /*is_consented_primary_account=*/false);

  // Test that the profile switch can be declined multiple times.
  DiceWebSigninInterceptor::Delegate::BubbleParameters expected_parameters(
      DiceWebSigninInterceptor::SigninInterceptionType::kProfileSwitch,
      account_info, AccountInfo());
  for (int i = 0; i < 10; ++i) {
    EXPECT_CALL(*mock_delegate(),
                ShowSigninInterceptionBubble(
                    web_contents(), MatchBubbleParameters(expected_parameters),
                    testing::_))
        .WillOnce(testing::WithArg<2>(testing::Invoke(
            [](base::OnceCallback<void(SigninInterceptionResult)> callback) {
              std::move(callback).Run(SigninInterceptionResult::kDeclined);
              return nullptr;
            })));
    MaybeIntercept(account_info.account_id);
    EXPECT_EQ(interceptor()->is_interception_in_progress(), false);
    histogram_tester.ExpectUniqueSample(
        "Signin.Intercept.HeuristicOutcome",
        SigninInterceptionHeuristicOutcome::kInterceptProfileSwitch, i + 1);
  }
}

TEST_F(DiceWebSigninInterceptorTest, PersistentHash) {
  // The hash is persistent (the value should never change).
  EXPECT_EQ("email_174",
            interceptor()->GetPersistentEmailHash("alice@example.com"));
  // Different email get another hash.
  EXPECT_NE(interceptor()->GetPersistentEmailHash("bob@gmail.com"),
            interceptor()->GetPersistentEmailHash("alice@example.com"));
  // Equivalent emails get the same hash.
  EXPECT_EQ(interceptor()->GetPersistentEmailHash("bob"),
            interceptor()->GetPersistentEmailHash("bob@gmail.com"));
  EXPECT_EQ(interceptor()->GetPersistentEmailHash("bo.b@gmail.com"),
            interceptor()->GetPersistentEmailHash("bob@gmail.com"));
  // Dots are removed only for gmail accounts.
  EXPECT_NE(interceptor()->GetPersistentEmailHash("alice@example.com"),
            interceptor()->GetPersistentEmailHash("al.ice@example.com"));
}

// Interception other than the profile switch require at least 2 accounts.
TEST_F(DiceWebSigninInterceptorTest, NoInterceptionWithOneAccount) {
  base::HistogramTester histogram_tester;
  AccountInfo account_info =
      identity_test_env()->MakeAccountAvailable("bob@gmail.com");
  // Interception aborts even if the account info is not available.
  ASSERT_FALSE(identity_test_env()
                   ->identity_manager()
                   ->FindExtendedAccountInfoByAccountId(account_info.account_id)
                   .IsValid());
  TestSynchronousInterception(
      account_info, /*is_new_account=*/true, /*is_sync_signin=*/false,
      SigninInterceptionHeuristicOutcome::kAbortSingleAccount);
}

// When profile creation is disallowed, profile switch interception is still
// enabled, but others are disabled.
TEST_F(DiceWebSigninInterceptorTest, ProfileCreationDisallowed) {
  base::HistogramTester histogram_tester;
  g_browser_process->local_state()->SetBoolean(prefs::kBrowserAddPersonEnabled,
                                               false);
  // Setup for profile switch interception.
  std::string email = "bob@example.com";
  AccountInfo account_info = identity_test_env()->MakeAccountAvailable(email);
  MakeValidAccountInfo(&account_info, "example.com");
  identity_test_env()->UpdateAccountInfoForAccount(account_info);
  AccountInfo other_account_info =
      identity_test_env()->MakeAccountAvailable("alice@example.com");
  MakeValidAccountInfo(&other_account_info, "example.com");
  identity_test_env()->UpdateAccountInfoForAccount(other_account_info);
  Profile* profile_2 = CreateTestingProfile("Profile 2");
  ProfileAttributesEntry* entry =
      profile_attributes_storage()->GetProfileAttributesWithPath(
          profile_2->GetPath());
  ASSERT_NE(entry, nullptr);
  entry->SetAuthInfo(account_info.gaia, base::UTF8ToUTF16(email),
                     /*is_consented_primary_account=*/false);

  // Interception that would offer creating a new profile does not work.
  TestSynchronousInterception(
      other_account_info, /*is_new_account=*/true, /*is_sync_signin=*/false,
      SigninInterceptionHeuristicOutcome::kAbortProfileCreationDisallowed);

  // Profile switch interception still works.
  DiceWebSigninInterceptor::Delegate::BubbleParameters expected_parameters(
      DiceWebSigninInterceptor::SigninInterceptionType::kProfileSwitch,
      account_info, AccountInfo());
  EXPECT_CALL(*mock_delegate(),
              ShowSigninInterceptionBubble(
                  web_contents(), MatchBubbleParameters(expected_parameters),
                  testing::_));
  MaybeIntercept(account_info.account_id);
}

TEST_F(DiceWebSigninInterceptorTest, WaitForAccountInfoAvailable) {
  base::HistogramTester histogram_tester;
  AccountInfo primary_account_info =
      identity_test_env()->MakePrimaryAccountAvailable(
          "bob@example.com", signin::ConsentLevel::kSignin);
  AccountInfo account_info =
      identity_test_env()->MakeAccountAvailable("alice@example.com");
  EXPECT_FALSE(interceptor()
                   ->GetHeuristicOutcome(/*is_new_account=*/true,
                                         /*is_sync_signin=*/false,
                                         account_info.email,
                                         /*entry=*/nullptr)
                   .has_value());
  MaybeIntercept(account_info.account_id);
  // Delegate was not called yet.
  testing::Mock::VerifyAndClearExpectations(mock_delegate());

  // Account info becomes available, interception happens.
  DiceWebSigninInterceptor::Delegate::BubbleParameters expected_parameters(
      DiceWebSigninInterceptor::SigninInterceptionType::kEnterprise,
      account_info, primary_account_info);
  EXPECT_CALL(*mock_delegate(),
              ShowSigninInterceptionBubble(
                  web_contents(), MatchBubbleParameters(expected_parameters),
                  testing::_));
  MakeValidAccountInfo(&account_info, "example.com");
  identity_test_env()->UpdateAccountInfoForAccount(account_info);
  histogram_tester.ExpectTotalCount("Signin.Intercept.AccountInfoFetchDuration",
                                    1);
}

TEST_F(DiceWebSigninInterceptorTest, AccountInfoAlreadyAvailable) {
  base::HistogramTester histogram_tester;
  AccountInfo primary_account_info =
      identity_test_env()->MakePrimaryAccountAvailable(
          "bob@example.com", signin::ConsentLevel::kSignin);
  AccountInfo account_info =
      identity_test_env()->MakeAccountAvailable("alice@example.com");
  MakeValidAccountInfo(&account_info, "example.com");
  identity_test_env()->UpdateAccountInfoForAccount(account_info);

  // Account info is already available, interception happens immediately.
  DiceWebSigninInterceptor::Delegate::BubbleParameters expected_parameters(
      DiceWebSigninInterceptor::SigninInterceptionType::kEnterprise,
      account_info, primary_account_info);
  EXPECT_CALL(*mock_delegate(),
              ShowSigninInterceptionBubble(
                  web_contents(), MatchBubbleParameters(expected_parameters),
                  testing::_));
  MaybeIntercept(account_info.account_id);
  histogram_tester.ExpectTotalCount("Signin.Intercept.AccountInfoFetchDuration",
                                    1);
  histogram_tester.ExpectUniqueSample(
      "Signin.Intercept.HeuristicOutcome",
      SigninInterceptionHeuristicOutcome::kInterceptEnterprise, 1);
}

TEST_F(DiceWebSigninInterceptorTest, MultiUserInterception) {
  base::HistogramTester histogram_tester;
  AccountInfo primary_account_info =
      identity_test_env()->MakePrimaryAccountAvailable(
          "bob@example.com", signin::ConsentLevel::kSignin);
  AccountInfo account_info =
      identity_test_env()->MakeAccountAvailable("alice@example.com");
  MakeValidAccountInfo(&account_info);
  identity_test_env()->UpdateAccountInfoForAccount(account_info);

  // Account info is already available, interception happens immediately.
  DiceWebSigninInterceptor::Delegate::BubbleParameters expected_parameters(
      DiceWebSigninInterceptor::SigninInterceptionType::kMultiUser,
      account_info, primary_account_info);
  EXPECT_CALL(*mock_delegate(),
              ShowSigninInterceptionBubble(
                  web_contents(), MatchBubbleParameters(expected_parameters),
                  testing::_));
  MaybeIntercept(account_info.account_id);
  histogram_tester.ExpectUniqueSample(
      "Signin.Intercept.HeuristicOutcome",
      SigninInterceptionHeuristicOutcome::kInterceptMultiUser, 1);
}
