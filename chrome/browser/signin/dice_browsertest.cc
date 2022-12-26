// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <memory>
#include <string>
#include <utility>

#include "base/auto_reset.h"
#include "base/base_switches.h"
#include "base/bind.h"
#include "base/callback.h"
#include "base/callback_helpers.h"
#include "base/check.h"
#include "base/command_line.h"
#include "base/location.h"
#include "base/run_loop.h"
#include "base/scoped_observation.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/test_mock_time_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "chrome/browser/apps/platform_apps/shortcut_manager.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/extensions/api/identity/web_auth_flow.h"
#include "chrome/browser/policy/cloud/user_policy_signin_service.h"
#include "chrome/browser/policy/cloud/user_policy_signin_service_internal.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/signin/account_consistency_mode_manager.h"
#include "chrome/browser/signin/account_reconcilor_factory.h"
#include "chrome/browser/signin/chrome_device_id_helper.h"
#include "chrome/browser/signin/chrome_signin_client.h"
#include "chrome/browser/signin/chrome_signin_client_factory.h"
#include "chrome/browser/signin/chrome_signin_helper.h"
#include "chrome/browser/signin/dice_response_handler.h"
#include "chrome/browser/signin/identity_manager_factory.h"
#include "chrome/browser/signin/signin_util.h"
#include "chrome/browser/sync/user_event_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/profile_chooser_constants.h"
#include "chrome/browser/ui/simple_message_box_internal.h"
#include "chrome/browser/ui/webui/signin/login_ui_service.h"
#include "chrome/browser/ui/webui/signin/login_ui_service_factory.h"
#include "chrome/browser/ui/webui/signin/login_ui_test_utils.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "components/search/ntp_features.h"
#include "components/signin/core/browser/account_reconcilor.h"
#include "components/signin/core/browser/dice_header_helper.h"
#include "components/signin/core/browser/signin_header_helper.h"
#include "components/signin/public/base/account_consistency_method.h"
#include "components/signin/public/base/signin_client.h"
#include "components/signin/public/base/signin_metrics.h"
#include "components/signin/public/base/signin_pref_names.h"
#include "components/signin/public/identity_manager/identity_manager.h"
#include "components/signin/public/identity_manager/identity_test_utils.h"
#include "components/signin/public/identity_manager/primary_account_mutator.h"
#include "components/sync/base/sync_prefs.h"
#include "components/sync_user_events/user_event_service.h"
#include "components/variations/variations_switches.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/load_notification_details.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_types.h"
#include "content/public/common/content_switches.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/test_navigation_observer.h"
#include "google_apis/gaia/gaia_switches.h"
#include "google_apis/gaia/gaia_urls.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "net/test/embedded_test_server/request_handler_util.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

using net::test_server::BasicHttpResponse;
using net::test_server::HttpRequest;
using net::test_server::HttpResponse;
using signin::AccountConsistencyMethod;

namespace {

constexpr int kAccountReconcilorDelayMs = 10;

enum SignoutType {
  kSignoutTypeFirst = 0,

  kAllAccounts = 0,       // Sign out from all accounts.
  kMainAccount = 1,       // Sign out from main account only.
  kSecondaryAccount = 2,  // Sign out from secondary account only.

  kSignoutTypeLast
};

const char kAuthorizationCode[] = "authorization_code";
const char kDiceResponseHeader[] = "X-Chrome-ID-Consistency-Response";
const char kChromeSyncEndpointURL[] = "/signin/chrome/sync";
const char kEnableSyncURL[] = "/enable_sync";
const char kGoogleSignoutResponseHeader[] = "Google-Accounts-SignOut";
const char kMainGmailEmail[] = "main_email@gmail.com";
const char kMainManagedEmail[] = "main_email@managed.com";
const char kNoDiceRequestHeader[] = "NoDiceHeader";
const char kOAuth2TokenExchangeURL[] = "/oauth2/v4/token";
const char kOAuth2TokenRevokeURL[] = "/o/oauth2/revoke";
const char kSecondaryEmail[] = "secondary_email@example.com";
const char kSigninURL[] = "/signin";
const char kSigninWithOutageInDiceURL[] = "/signin/outage";
const char kSignoutURL[] = "/signout";

// Test response that does not complete synchronously. It must be unblocked by
// calling the completion closure.
class BlockedHttpResponse : public net::test_server::BasicHttpResponse {
 public:
  explicit BlockedHttpResponse(
      base::OnceCallback<void(base::OnceClosure)> callback)
      : callback_(std::move(callback)) {}

  void SendResponse(
      base::WeakPtr<net::test_server::HttpResponseDelegate> delegate) override {
    // Called on the IO thread to unblock the response.
    base::OnceClosure unblock_io_thread =
        base::BindOnce(&BlockedHttpResponse::SendResponseInternal,
                       weak_factory_.GetWeakPtr(), delegate);
    // Unblock the response from any thread by posting a task to the IO thread.
    base::OnceClosure unblock_any_thread =
        base::BindOnce(base::IgnoreResult(&base::TaskRunner::PostTask),
                       base::ThreadTaskRunnerHandle::Get(), FROM_HERE,
                       std::move(unblock_io_thread));
    // Pass |unblock_any_thread| to the caller on the UI thread.
    content::GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(callback_), std::move(unblock_any_thread)));
  }

 private:
  void SendResponseInternal(
      base::WeakPtr<net::test_server::HttpResponseDelegate> delegate) {
    if (delegate)
      BasicHttpResponse::SendResponse(delegate);
  }
  base::OnceCallback<void(base::OnceClosure)> callback_;

  base::WeakPtrFactory<BlockedHttpResponse> weak_factory_{this};
};

}  // namespace

namespace FakeGaia {

// Handler for the signin page on the embedded test server.
// The response has the content of the Dice request header in its body, and has
// the Dice response header.
// Handles both the "Chrome Sync" endpoint and the old endpoint.
std::unique_ptr<HttpResponse> HandleSigninURL(
    const std::string& main_email,
    const base::RepeatingCallback<void(const std::string&)>& callback,
    const HttpRequest& request) {
  if (!net::test_server::ShouldHandle(request, kSigninURL) &&
      !net::test_server::ShouldHandle(request, kChromeSyncEndpointURL) &&
      !net::test_server::ShouldHandle(request, kSigninWithOutageInDiceURL))
    return nullptr;

  // Extract Dice request header.
  std::string header_value = kNoDiceRequestHeader;
  auto it = request.headers.find(signin::kDiceRequestHeader);
  if (it != request.headers.end())
    header_value = it->second;

  content::GetUIThreadTaskRunner({})->PostTask(
      FROM_HERE, base::BindOnce(callback, header_value));

  // Add the SIGNIN dice header.
  std::unique_ptr<BasicHttpResponse> http_response(new BasicHttpResponse);
  if (header_value != kNoDiceRequestHeader) {
    if (net::test_server::ShouldHandle(request, kSigninWithOutageInDiceURL)) {
      http_response->AddCustomHeader(
          kDiceResponseHeader,
          base::StringPrintf("action=SIGNIN,authuser=1,id=%s,email=%s,"
                             "no_authorization_code=true",
                             signin::GetTestGaiaIdForEmail(main_email).c_str(),
                             main_email.c_str()));
    } else {
      http_response->AddCustomHeader(
          kDiceResponseHeader,
          base::StringPrintf(
              "action=SIGNIN,authuser=1,id=%s,email=%s,authorization_code=%s",
              signin::GetTestGaiaIdForEmail(main_email).c_str(),
              main_email.c_str(), kAuthorizationCode));
    }
  }

  // When hitting the Chrome Sync endpoint, redirect to kEnableSyncURL, which
  // adds the ENABLE_SYNC dice header.
  if (net::test_server::ShouldHandle(request, kChromeSyncEndpointURL)) {
    http_response->set_code(net::HTTP_FOUND);  // 302 redirect.
    http_response->AddCustomHeader("location", kEnableSyncURL);
  }

  http_response->AddCustomHeader("Cache-Control", "no-store");
  return std::move(http_response);
}

// Handler for the Gaia endpoint adding the ENABLE_SYNC dice header.
std::unique_ptr<HttpResponse> HandleEnableSyncURL(
    const std::string& main_email,
    const base::RepeatingCallback<void(base::OnceClosure)>& callback,
    const HttpRequest& request) {
  if (!net::test_server::ShouldHandle(request, kEnableSyncURL))
    return nullptr;

  std::unique_ptr<BlockedHttpResponse> http_response =
      std::make_unique<BlockedHttpResponse>(callback);
  http_response->AddCustomHeader(
      kDiceResponseHeader,
      base::StringPrintf("action=ENABLE_SYNC,authuser=1,id=%s,email=%s",
                         signin::GetTestGaiaIdForEmail(main_email).c_str(),
                         main_email.c_str()));
  http_response->AddCustomHeader("Cache-Control", "no-store");
  return std::move(http_response);
}

// Handler for the signout page on the embedded test server.
// Responds with a Google-Accounts-SignOut header for the main account, the
// secondary account, or both (depending on the SignoutType, which is encoded in
// the query string).
std::unique_ptr<HttpResponse> HandleSignoutURL(const std::string& main_email,
                                               const HttpRequest& request) {
  if (!net::test_server::ShouldHandle(request, kSignoutURL))
    return nullptr;

  // Build signout header.
  int query_value;
  EXPECT_TRUE(base::StringToInt(request.GetURL().query(), &query_value));
  SignoutType signout_type = static_cast<SignoutType>(query_value);
  EXPECT_GE(signout_type, kSignoutTypeFirst);
  EXPECT_LT(signout_type, kSignoutTypeLast);
  std::string signout_header_value;
  if (signout_type == kAllAccounts || signout_type == kMainAccount) {
    std::string main_gaia_id = signin::GetTestGaiaIdForEmail(main_email);
    signout_header_value =
        base::StringPrintf("email=\"%s\", obfuscatedid=\"%s\", sessionindex=1",
                           main_email.c_str(), main_gaia_id.c_str());
  }
  if (signout_type == kAllAccounts || signout_type == kSecondaryAccount) {
    if (!signout_header_value.empty())
      signout_header_value += ", ";
    std::string secondary_gaia_id =
        signin::GetTestGaiaIdForEmail(kSecondaryEmail);
    signout_header_value +=
        base::StringPrintf("email=\"%s\", obfuscatedid=\"%s\", sessionindex=2",
                           kSecondaryEmail, secondary_gaia_id.c_str());
  }

  std::unique_ptr<BasicHttpResponse> http_response(new BasicHttpResponse);
  http_response->AddCustomHeader(kGoogleSignoutResponseHeader,
                                 signout_header_value);
  http_response->AddCustomHeader("Cache-Control", "no-store");
  return std::move(http_response);
}

// Handler for OAuth2 token exchange.
// Checks that the request is well formatted and returns a refresh token in a
// JSON dictionary.
std::unique_ptr<HttpResponse> HandleOAuth2TokenExchangeURL(
    const base::RepeatingCallback<void(base::OnceClosure)>& callback,
    const HttpRequest& request) {
  if (!net::test_server::ShouldHandle(request, kOAuth2TokenExchangeURL))
    return nullptr;

  // Check that the authorization code is somewhere in the request body.
  if (!request.has_content)
    return nullptr;
  if (request.content.find(kAuthorizationCode) == std::string::npos)
    return nullptr;

  std::unique_ptr<BlockedHttpResponse> http_response =
      std::make_unique<BlockedHttpResponse>(callback);

  std::string content =
      "{"
      "  \"access_token\":\"access_token\","
      "  \"refresh_token\":\"new_refresh_token\","
      "  \"expires_in\":9999"
      "}";

  http_response->set_content(content);
  http_response->set_content_type("text/plain");
  http_response->AddCustomHeader("Cache-Control", "no-store");
  return std::move(http_response);
}

// Handler for OAuth2 token revocation.
std::unique_ptr<HttpResponse> HandleOAuth2TokenRevokeURL(
    const base::RepeatingClosure& callback,
    const HttpRequest& request) {
  if (!net::test_server::ShouldHandle(request, kOAuth2TokenRevokeURL))
    return nullptr;

  content::GetUIThreadTaskRunner({})->PostTask(FROM_HERE, callback);

  std::unique_ptr<BasicHttpResponse> http_response(new BasicHttpResponse);
  http_response->AddCustomHeader("Cache-Control", "no-store");
  return std::move(http_response);
}

// Handler for ServiceLogin on the embedded test server.
// Calls the callback with the dice request header, or kNoDiceRequestHeader if
// there is no Dice header.
std::unique_ptr<HttpResponse> HandleChromeSigninEmbeddedURL(
    const base::RepeatingCallback<void(const std::string&)>& callback,
    const HttpRequest& request) {
  if (!net::test_server::ShouldHandle(request,
                                      "/embedded/setup/chrome/usermenu"))
    return nullptr;

  std::string dice_request_header(kNoDiceRequestHeader);
  auto it = request.headers.find(signin::kDiceRequestHeader);
  if (it != request.headers.end())
    dice_request_header = it->second;
  content::GetUIThreadTaskRunner({})->PostTask(
      FROM_HERE, base::BindOnce(callback, dice_request_header));

  std::unique_ptr<BasicHttpResponse> http_response(new BasicHttpResponse);
  http_response->AddCustomHeader("Cache-Control", "no-store");
  return std::move(http_response);
}

}  // namespace FakeGaia

class DiceBrowserTest : public InProcessBrowserTest,
                        public AccountReconcilor::Observer,
                        public signin::IdentityManager::Observer {
 public:
  DiceBrowserTest(const DiceBrowserTest&) = delete;
  DiceBrowserTest& operator=(const DiceBrowserTest&) = delete;

 protected:
  ~DiceBrowserTest() override {}

  explicit DiceBrowserTest(const std::string& main_email = kMainGmailEmail)
      : main_email_(main_email),
        https_server_(net::EmbeddedTestServer::TYPE_HTTPS),
        enable_sync_requested_(false),
        token_requested_(false),
        refresh_token_available_(false),
        token_revoked_notification_count_(0),
        token_revoked_count_(0),
        reconcilor_blocked_count_(0),
        reconcilor_unblocked_count_(0),
        reconcilor_started_count_(0) {
    feature_list_.InitAndEnableFeature(kSupportOAuthOutageInDice);
    https_server_.RegisterDefaultHandler(base::BindRepeating(
        &FakeGaia::HandleSigninURL, main_email_,
        base::BindRepeating(&DiceBrowserTest::OnSigninRequest,
                            base::Unretained(this))));
    https_server_.RegisterDefaultHandler(base::BindRepeating(
        &FakeGaia::HandleEnableSyncURL, main_email_,
        base::BindRepeating(&DiceBrowserTest::OnEnableSyncRequest,
                            base::Unretained(this))));
    https_server_.RegisterDefaultHandler(
        base::BindRepeating(&FakeGaia::HandleSignoutURL, main_email_));
    https_server_.RegisterDefaultHandler(base::BindRepeating(
        &FakeGaia::HandleOAuth2TokenExchangeURL,
        base::BindRepeating(&DiceBrowserTest::OnTokenExchangeRequest,
                            base::Unretained(this))));
    https_server_.RegisterDefaultHandler(base::BindRepeating(
        &FakeGaia::HandleOAuth2TokenRevokeURL,
        base::BindRepeating(&DiceBrowserTest::OnTokenRevocationRequest,
                            base::Unretained(this))));
    https_server_.RegisterDefaultHandler(base::BindRepeating(
        &FakeGaia::HandleChromeSigninEmbeddedURL,
        base::BindRepeating(&DiceBrowserTest::OnChromeSigninEmbeddedRequest,
                            base::Unretained(this))));
    signin::SetDiceAccountReconcilorBlockDelayForTesting(
        kAccountReconcilorDelayMs);
  }

  // Navigates to the given path on the test server.
  void NavigateToURL(const std::string& path) {
    ASSERT_TRUE(
        ui_test_utils::NavigateToURL(browser(), https_server_.GetURL(path)));
  }

  // Returns the identity manager.
  signin::IdentityManager* GetIdentityManager() {
    return IdentityManagerFactory::GetForProfile(browser()->profile());
  }

  // Returns the account ID associated with |main_email_| and its associated
  // gaia ID.
  CoreAccountId GetMainAccountID() {
    return GetIdentityManager()->PickAccountIdForAccount(
        signin::GetTestGaiaIdForEmail(main_email_), main_email_);
  }

  // Returns the account ID associated with kSecondaryEmail and its associated
  // gaia ID.
  CoreAccountId GetSecondaryAccountID() {
    return GetIdentityManager()->PickAccountIdForAccount(
        signin::GetTestGaiaIdForEmail(kSecondaryEmail), kSecondaryEmail);
  }

  std::string GetDeviceId() {
    return GetSigninScopedDeviceIdForProfile(browser()->profile());
  }

  // Signin with a main account and add token for a secondary account.
  void SetupSignedInAccounts(
      signin::ConsentLevel primary_account_consent_level) {
    // Signin main account.
    AccountInfo primary_account_info = signin::MakePrimaryAccountAvailable(
        GetIdentityManager(), main_email_, primary_account_consent_level);
    ASSERT_TRUE(
        GetIdentityManager()->HasAccountWithRefreshToken(GetMainAccountID()));
    ASSERT_FALSE(
        GetIdentityManager()->HasAccountWithRefreshTokenInPersistentErrorState(
            GetMainAccountID()));
    ASSERT_EQ(GetMainAccountID(), GetIdentityManager()->GetPrimaryAccountId(
                                      primary_account_consent_level));

    // Add a token for a secondary account.
    AccountInfo secondary_account_info =
        signin::MakeAccountAvailable(GetIdentityManager(), kSecondaryEmail);
    ASSERT_TRUE(GetIdentityManager()->HasAccountWithRefreshToken(
        secondary_account_info.account_id));
    ASSERT_FALSE(
        GetIdentityManager()->HasAccountWithRefreshTokenInPersistentErrorState(
            secondary_account_info.account_id));
  }

  // Navigate to a Gaia URL setting the Google-Accounts-SignOut header.
  void SignOutWithDice(SignoutType signout_type) {
    NavigateToURL(base::StringPrintf("%s?%i", kSignoutURL, signout_type));
    EXPECT_EQ(1, reconcilor_blocked_count_);
    WaitForReconcilorUnblockedCount(1);

    base::RunLoop().RunUntilIdle();
  }

  // InProcessBrowserTest:
  void SetUp() override {
    ASSERT_TRUE(https_server_.InitializeAndListen());
    InProcessBrowserTest::SetUp();
  }

  void SetUpCommandLine(base::CommandLine* command_line) override {
    const GURL& base_url = https_server_.base_url();
    command_line->AppendSwitchASCII(switches::kGaiaUrl, base_url.spec());
    command_line->AppendSwitchASCII(switches::kGoogleApisUrl, base_url.spec());
    command_line->AppendSwitchASCII(switches::kLsoUrl, base_url.spec());
  }

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    https_server_.StartAcceptingConnections();

    identity_manager_observation_.Observe(GetIdentityManager());
    // Wait for the token service to be ready.
    if (!GetIdentityManager()->AreRefreshTokensLoaded()) {
      WaitForClosure(&tokens_loaded_quit_closure_);
    }
    ASSERT_TRUE(GetIdentityManager()->AreRefreshTokensLoaded());

    AccountReconcilor* reconcilor =
        AccountReconcilorFactory::GetForProfile(browser()->profile());

    // Reconcilor starts as soon as the token service finishes loading its
    // credentials. Abort the reconcilor here to make sure tests start in a
    // stable state.
    reconcilor->AbortReconcile();
    reconcilor->SetState(
        signin_metrics::AccountReconcilorState::ACCOUNT_RECONCILOR_OK);
    account_reconcilor_observation_.Observe(reconcilor);
  }

  void TearDownOnMainThread() override {
    identity_manager_observation_.Reset();
    account_reconcilor_observation_.Reset();
  }

  // Calls |closure| if it is not null and resets it after.
  void RunClosureIfValid(base::OnceClosure closure) {
    if (closure)
      std::move(closure).Run();
  }

  // Creates and runs a RunLoop until |closure| is called.
  void WaitForClosure(base::OnceClosure* closure) {
    base::RunLoop run_loop;
    *closure = run_loop.QuitClosure();
    run_loop.Run();
  }

  // FakeGaia callbacks:
  void OnSigninRequest(const std::string& dice_request_header) {
    EXPECT_EQ(dice_request_header != kNoDiceRequestHeader,
              IsReconcilorBlocked());
    dice_request_header_ = dice_request_header;
    RunClosureIfValid(std::move(signin_requested_quit_closure_));
  }

  void OnChromeSigninEmbeddedRequest(const std::string& dice_request_header) {
    dice_request_header_ = dice_request_header;
    RunClosureIfValid(std::move(chrome_signin_embedded_quit_closure_));
  }

  void OnEnableSyncRequest(base::OnceClosure unblock_response_closure) {
    EXPECT_TRUE(IsReconcilorBlocked());
    enable_sync_requested_ = true;
    RunClosureIfValid(std::move(enable_sync_requested_quit_closure_));
    unblock_enable_sync_response_closure_ = std::move(unblock_response_closure);
  }

  void OnTokenExchangeRequest(base::OnceClosure unblock_response_closure) {
    // The token must be exchanged only once.
    EXPECT_FALSE(token_requested_);
    EXPECT_TRUE(IsReconcilorBlocked());
    token_requested_ = true;
    RunClosureIfValid(std::move(token_requested_quit_closure_));
    unblock_token_exchange_response_closure_ =
        std::move(unblock_response_closure);
  }

  void OnTokenRevocationRequest() {
    ++token_revoked_count_;
    RunClosureIfValid(std::move(token_revoked_quit_closure_));
  }

  // AccountReconcilor::Observer:
  void OnBlockReconcile() override { ++reconcilor_blocked_count_; }
  void OnUnblockReconcile() override {
    ++reconcilor_unblocked_count_;
    RunClosureIfValid(std::move(unblock_count_quit_closure_));
  }
  void OnStateChanged(signin_metrics::AccountReconcilorState state) override {
    if (state ==
        signin_metrics::AccountReconcilorState::ACCOUNT_RECONCILOR_RUNNING) {
      ++reconcilor_started_count_;
    }
  }

  // signin::IdentityManager::Observer
  void OnPrimaryAccountChanged(
      const signin::PrimaryAccountChangeEvent& event) override {
    if (event.GetEventTypeFor(signin::ConsentLevel::kSync) ==
        signin::PrimaryAccountChangeEvent::Type::kSet) {
      RunClosureIfValid(std::move(on_primary_account_set_quit_closure_));
    }
  }

  void OnRefreshTokenUpdatedForAccount(
      const CoreAccountInfo& account_info) override {
    if (account_info.account_id == GetMainAccountID()) {
      refresh_token_available_ = true;
      RunClosureIfValid(std::move(refresh_token_available_quit_closure_));
    }
  }

  void OnRefreshTokenRemovedForAccount(
      const CoreAccountId& account_id) override {
    ++token_revoked_notification_count_;
  }

  void OnRefreshTokensLoaded() override {
    RunClosureIfValid(std::move(tokens_loaded_quit_closure_));
  }

  // Returns true if the account reconcilor is currently blocked.
  bool IsReconcilorBlocked() {
    EXPECT_GE(reconcilor_blocked_count_, reconcilor_unblocked_count_);
    EXPECT_LE(reconcilor_blocked_count_, reconcilor_unblocked_count_ + 1);
    return (reconcilor_unblocked_count_ + 1) == reconcilor_blocked_count_;
  }

  // Waits until |reconcilor_unblocked_count_| reaches |count|.
  void WaitForReconcilorUnblockedCount(int count) {
    if (reconcilor_unblocked_count_ == count)
      return;

    ASSERT_EQ(count - 1, reconcilor_unblocked_count_);
    // Wait for the timeout after the request is complete.
    WaitForClosure(&unblock_count_quit_closure_);
    EXPECT_EQ(count, reconcilor_unblocked_count_);
  }

  // Waits until the user consented for sync.
  void WaitForSigninSucceeded() {
    if (GetIdentityManager()
            ->GetPrimaryAccountId(signin::ConsentLevel::kSync)
            .empty()) {
      WaitForClosure(&on_primary_account_set_quit_closure_);
    }
  }

  // Waits for the ENABLE_SYNC request to hit the server, and unblocks the
  // response. If this is not called, ENABLE_SYNC will not be sent by the
  // server.
  // Note: this does not wait for the response to reach Chrome.
  void SendEnableSyncResponse() {
    if (!enable_sync_requested_)
      WaitForClosure(&enable_sync_requested_quit_closure_);
    DCHECK(unblock_enable_sync_response_closure_);
    std::move(unblock_enable_sync_response_closure_).Run();
  }

  // Waits until the token request is sent to the server, the response is
  // received and the refresh token is available. If this is not called, the
  // refresh token will not be sent by the server.
  void SendRefreshTokenResponse() {
    // Wait for the request hitting the server.
    if (!token_requested_)
      WaitForClosure(&token_requested_quit_closure_);
    EXPECT_TRUE(token_requested_);
    // Unblock the server response.
    DCHECK(unblock_token_exchange_response_closure_);
    std::move(unblock_token_exchange_response_closure_).Run();
    // Wait for the response coming back.
    if (!refresh_token_available_)
      WaitForClosure(&refresh_token_available_quit_closure_);
    EXPECT_TRUE(refresh_token_available_);
  }

  void WaitForTokenRevokedCount(int count) {
    EXPECT_LE(token_revoked_count_, count);
    while (token_revoked_count_ < count)
      WaitForClosure(&token_revoked_quit_closure_);
    EXPECT_EQ(count, token_revoked_count_);
  }

  DiceResponseHandler* GetDiceResponseHandler() {
    return DiceResponseHandler::GetForProfile(browser()->profile());
  }

  void CloseBrowser() {
    identity_manager_observation_.Reset();
    account_reconcilor_observation_.Reset();
    CloseBrowserSynchronously(browser());
  }

  const std::string main_email_;
  net::EmbeddedTestServer https_server_;
  bool enable_sync_requested_;
  bool token_requested_;
  bool refresh_token_available_;
  int token_revoked_notification_count_;
  int token_revoked_count_;
  int reconcilor_blocked_count_;
  int reconcilor_unblocked_count_;
  int reconcilor_started_count_;
  std::string dice_request_header_;
  base::test::ScopedFeatureList feature_list_;

  base::ScopedObservation<signin::IdentityManager,
                          signin::IdentityManager::Observer>
      identity_manager_observation_{this};
  base::ScopedObservation<AccountReconcilor, AccountReconcilor::Observer>
      account_reconcilor_observation_{this};

  // Unblocks the server responses.
  base::OnceClosure unblock_token_exchange_response_closure_;
  base::OnceClosure unblock_enable_sync_response_closure_;

  // Used for waiting asynchronous events.
  base::OnceClosure enable_sync_requested_quit_closure_;
  base::OnceClosure token_requested_quit_closure_;
  base::OnceClosure token_revoked_quit_closure_;
  base::OnceClosure refresh_token_available_quit_closure_;
  base::OnceClosure chrome_signin_embedded_quit_closure_;
  base::OnceClosure unblock_count_quit_closure_;
  base::OnceClosure tokens_loaded_quit_closure_;
  base::OnceClosure on_primary_account_set_quit_closure_;
  base::OnceClosure signin_requested_quit_closure_;
};

// Checks that signin on Gaia triggers the fetch for a refresh token.
IN_PROC_BROWSER_TEST_F(DiceBrowserTest, Signin) {
  EXPECT_EQ(0, reconcilor_started_count_);

  // Navigate to Gaia and sign in.
  NavigateToURL(kSigninURL);

  // Check that the Dice request header was sent.
  std::string client_id = GaiaUrls::GetInstance()->oauth2_chrome_client_id();
  EXPECT_EQ(base::StringPrintf("version=%s,client_id=%s,device_id=%s,"
                               "signin_mode=all_accounts,"
                               "signout_mode=show_confirmation",
                               signin::kDiceProtocolVersion, client_id.c_str(),
                               GetDeviceId().c_str()),
            dice_request_header_);

  // Check that the token was requested and added to the token service.
  SendRefreshTokenResponse();
  EXPECT_TRUE(
      GetIdentityManager()->HasAccountWithRefreshToken(GetMainAccountID()));
  // Sync should not be enabled.
  EXPECT_TRUE(GetIdentityManager()
                  ->GetPrimaryAccountId(signin::ConsentLevel::kSync)
                  .empty());

  EXPECT_EQ(1, reconcilor_blocked_count_);
  WaitForReconcilorUnblockedCount(1);
  EXPECT_EQ(1, reconcilor_started_count_);
}

// Checks that the account reconcilor is blocked when where was OAuth
// outage in Dice, and unblocked after the timeout.
IN_PROC_BROWSER_TEST_F(DiceBrowserTest, SupportOAuthOutageInDice) {
  DiceResponseHandler* dice_response_handler = GetDiceResponseHandler();
  scoped_refptr<base::TestMockTimeTaskRunner> task_runner =
      new base::TestMockTimeTaskRunner();
  dice_response_handler->SetTaskRunner(task_runner);
  NavigateToURL(kSigninWithOutageInDiceURL);
  // Check that the Dice request header was sent.
  std::string client_id = GaiaUrls::GetInstance()->oauth2_chrome_client_id();
  EXPECT_EQ(base::StringPrintf("version=%s,client_id=%s,device_id=%s,"
                               "signin_mode=all_accounts,"
                               "signout_mode=show_confirmation",
                               signin::kDiceProtocolVersion, client_id.c_str(),
                               GetDeviceId().c_str()),
            dice_request_header_);
  // Check that the reconcilor was blocked and not unblocked before timeout.
  EXPECT_EQ(1, reconcilor_blocked_count_);
  EXPECT_EQ(0, reconcilor_unblocked_count_);
  task_runner->FastForwardBy(
      base::Hours(kLockAccountReconcilorTimeoutHours / 2));
  EXPECT_EQ(0, reconcilor_unblocked_count_);
  task_runner->FastForwardBy(
      base::Hours((kLockAccountReconcilorTimeoutHours + 1) / 2));
  // Wait until reconcilor is unblocked.
  WaitForReconcilorUnblockedCount(1);
}

// Checks that re-auth on Gaia triggers the fetch for a refresh token.
IN_PROC_BROWSER_TEST_F(DiceBrowserTest, Reauth) {
  EXPECT_EQ(0, reconcilor_started_count_);

  // Start from a signed-in state.
  SetupSignedInAccounts(signin::ConsentLevel::kSync);
  EXPECT_EQ(1, reconcilor_started_count_);

  // Navigate to Gaia and sign in again with the main account.
  NavigateToURL(kSigninURL);

  // Check that the Dice request header was sent.
  std::string client_id = GaiaUrls::GetInstance()->oauth2_chrome_client_id();
  EXPECT_EQ(base::StringPrintf("version=%s,client_id=%s,device_id=%s,"
                               "signin_mode=all_accounts,"
                               "signout_mode=show_confirmation",
                               signin::kDiceProtocolVersion, client_id.c_str(),
                               GetDeviceId().c_str()),
            dice_request_header_);

  // Check that the token was requested and added to the token service.
  SendRefreshTokenResponse();
  EXPECT_EQ(GetMainAccountID(), GetIdentityManager()->GetPrimaryAccountId(
                                    signin::ConsentLevel::kSync));

  // Old token must not be revoked (see http://crbug.com/865189).
  EXPECT_EQ(0, token_revoked_notification_count_);

  EXPECT_EQ(1, reconcilor_blocked_count_);
  WaitForReconcilorUnblockedCount(1);
  EXPECT_EQ(2, reconcilor_started_count_);
}

// Checks that the Dice signout flow works and deletes all tokens.
IN_PROC_BROWSER_TEST_F(DiceBrowserTest, SignoutMainAccount) {
  // Start from a signed-in state.
  SetupSignedInAccounts(signin::ConsentLevel::kSync);

  // Signout from main account.
  SignOutWithDice(kMainAccount);

  // Check that the user is in error state.
  EXPECT_EQ(GetMainAccountID(), GetIdentityManager()->GetPrimaryAccountId(
                                    signin::ConsentLevel::kSync));
  EXPECT_TRUE(
      GetIdentityManager()->HasAccountWithRefreshToken(GetMainAccountID()));
  EXPECT_TRUE(
      GetIdentityManager()->HasAccountWithRefreshTokenInPersistentErrorState(
          GetMainAccountID()));
  EXPECT_TRUE(GetIdentityManager()->HasAccountWithRefreshToken(
      GetSecondaryAccountID()));

  // Token for main account is revoked on server but not notified in the client.
  EXPECT_EQ(0, token_revoked_notification_count_);
  WaitForTokenRevokedCount(1);

  EXPECT_EQ(1, reconcilor_blocked_count_);
  WaitForReconcilorUnblockedCount(1);
}

// Checks that signing out from a secondary account does not delete the main
// token.
IN_PROC_BROWSER_TEST_F(DiceBrowserTest, SignoutSecondaryAccount) {
  // Start from a signed-in state.
  SetupSignedInAccounts(signin::ConsentLevel::kSync);

  // Signout from secondary account.
  SignOutWithDice(kSecondaryAccount);

  // Check that the user is still signed in from main account, but secondary
  // token is deleted.
  EXPECT_EQ(GetMainAccountID(), GetIdentityManager()->GetPrimaryAccountId(
                                    signin::ConsentLevel::kSync));
  EXPECT_TRUE(
      GetIdentityManager()->HasAccountWithRefreshToken(GetMainAccountID()));
  EXPECT_FALSE(GetIdentityManager()->HasAccountWithRefreshToken(
      GetSecondaryAccountID()));
  EXPECT_EQ(1, token_revoked_notification_count_);
  WaitForTokenRevokedCount(1);
  EXPECT_EQ(1, reconcilor_blocked_count_);
  WaitForReconcilorUnblockedCount(1);
}

// Checks that the Dice signout flow works and deletes all tokens.
IN_PROC_BROWSER_TEST_F(DiceBrowserTest, SignoutAllAccounts) {
  // Start from a signed-in state.
  SetupSignedInAccounts(signin::ConsentLevel::kSync);

  // Signout from all accounts.
  SignOutWithDice(kAllAccounts);

  // Check that the user is in error state.
  EXPECT_EQ(GetMainAccountID(), GetIdentityManager()->GetPrimaryAccountId(
                                    signin::ConsentLevel::kSync));
  EXPECT_TRUE(
      GetIdentityManager()->HasAccountWithRefreshToken(GetMainAccountID()));
  EXPECT_TRUE(
      GetIdentityManager()->HasAccountWithRefreshTokenInPersistentErrorState(
          GetMainAccountID()));
  EXPECT_FALSE(GetIdentityManager()->HasAccountWithRefreshToken(
      GetSecondaryAccountID()));

  // Token for main account is revoked on server but not notified in the client.
  EXPECT_EQ(1, token_revoked_notification_count_);
  WaitForTokenRevokedCount(2);

  EXPECT_EQ(1, reconcilor_blocked_count_);
  WaitForReconcilorUnblockedCount(1);
}

// Checks that the Dice signout flow works and deletes all tokens.
IN_PROC_BROWSER_TEST_F(DiceBrowserTest, RevokeSyncAccountInAuthErrorState) {
  // Start from a signed-in state.
  SetupSignedInAccounts(signin::ConsentLevel::kSync);

  // Signout from main account.
  SignOutWithDice(kMainAccount);

  // Check that the user is in error state.
  ASSERT_TRUE(
      GetIdentityManager()->HasPrimaryAccount(signin::ConsentLevel::kSync));
  ASSERT_TRUE(
      GetIdentityManager()->HasAccountWithRefreshToken(GetMainAccountID()));
  ASSERT_TRUE(
      GetIdentityManager()->HasAccountWithRefreshTokenInPersistentErrorState(
          GetMainAccountID()));

  // Revoking the sync consent should clear the primary account as it is in
  // an permanent auth error state.
  RevokeSyncConsent(GetIdentityManager());

  // Updating the primary is done asynchronously. Wait for the update to happen.
  WaitForPrimaryAccount(GetIdentityManager(), signin::ConsentLevel::kSignin,
                        CoreAccountId());
  EXPECT_FALSE(
      GetIdentityManager()->HasPrimaryAccount(signin::ConsentLevel::kSignin));
}

// Checks that Dice request header is not set from request from WebUI.
// See https://crbug.com/428396
#if BUILDFLAG(IS_WIN)
#define MAYBE_NoDiceFromWebUI DISABLED_NoDiceFromWebUI
#else
#define MAYBE_NoDiceFromWebUI NoDiceFromWebUI
#endif
IN_PROC_BROWSER_TEST_F(DiceBrowserTest, MAYBE_NoDiceFromWebUI) {
  // Navigate to Gaia and from the native tab, which uses an extension.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), GURL("chrome:chrome-signin?reason=5")));

  // Check that the request had no Dice request header.
  if (dice_request_header_.empty())
    WaitForClosure(&chrome_signin_embedded_quit_closure_);
  EXPECT_EQ(kNoDiceRequestHeader, dice_request_header_);
  EXPECT_EQ(0, reconcilor_blocked_count_);
  WaitForReconcilorUnblockedCount(0);
}

IN_PROC_BROWSER_TEST_F(DiceBrowserTest,
                       NoDiceExtensionConsent_LaunchWebAuthFlow) {
  auto web_auth_flow = std::make_unique<extensions::WebAuthFlow>(
      nullptr, browser()->profile(), https_server_.GetURL(kSigninURL),
      extensions::WebAuthFlow::INTERACTIVE,
      extensions::WebAuthFlow::LAUNCH_WEB_AUTH_FLOW);
  web_auth_flow->Start();

  if (dice_request_header_.empty())
    WaitForClosure(&signin_requested_quit_closure_);

  EXPECT_EQ(kNoDiceRequestHeader, dice_request_header_);
  EXPECT_EQ(0, reconcilor_blocked_count_);
  WaitForReconcilorUnblockedCount(0);

  // Delete the web auth flow (uses DeleteSoon).
  web_auth_flow.release()->DetachDelegateAndDelete();
  base::RunLoop().RunUntilIdle();
}

IN_PROC_BROWSER_TEST_F(DiceBrowserTest, DiceExtensionConsent_GetAuthToken) {
  // Signin from extension consent flow.
  class DummyDelegate : public extensions::WebAuthFlow::Delegate {
   public:
    void OnAuthFlowFailure(extensions::WebAuthFlow::Failure failure) override {}
    ~DummyDelegate() override = default;
  };

  DummyDelegate delegate;
  auto web_auth_flow = std::make_unique<extensions::WebAuthFlow>(
      &delegate, browser()->profile(), https_server_.GetURL(kSigninURL),
      extensions::WebAuthFlow::INTERACTIVE,
      extensions::WebAuthFlow::GET_AUTH_TOKEN);
  web_auth_flow->Start();

  // Check that the token was requested and added to the token service.
  SendRefreshTokenResponse();
  EXPECT_TRUE(
      GetIdentityManager()->HasAccountWithRefreshToken(GetMainAccountID()));

  // Check that the Dice request header was sent.
  std::string client_id = GaiaUrls::GetInstance()->oauth2_chrome_client_id();
  EXPECT_EQ(base::StringPrintf("version=%s,client_id=%s,device_id=%s,"
                               "signin_mode=all_accounts,"
                               "signout_mode=show_confirmation",
                               signin::kDiceProtocolVersion, client_id.c_str(),
                               GetDeviceId().c_str()),
            dice_request_header_);

  // Sync should not be enabled.
  EXPECT_TRUE(GetIdentityManager()
                  ->GetPrimaryAccountId(signin::ConsentLevel::kSync)
                  .empty());

  EXPECT_EQ(1, reconcilor_blocked_count_);
  WaitForReconcilorUnblockedCount(1);
  EXPECT_EQ(1, reconcilor_started_count_);

  // Delete the web auth flow (uses DeleteSoon).
  web_auth_flow.release()->DetachDelegateAndDelete();
  base::RunLoop().RunUntilIdle();
}

// Tests that Sync is enabled if the ENABLE_SYNC response is received after the
// refresh token.
IN_PROC_BROWSER_TEST_F(DiceBrowserTest, EnableSyncAfterToken) {
  EXPECT_EQ(0, reconcilor_started_count_);

  // Signin using the Chrome Sync endpoint.
  browser()->signin_view_controller()->ShowSignin(
      profiles::BUBBLE_VIEW_MODE_GAIA_SIGNIN,
      signin_metrics::AccessPoint::ACCESS_POINT_SETTINGS);

  // Receive token.
  EXPECT_FALSE(
      GetIdentityManager()->HasAccountWithRefreshToken(GetMainAccountID()));
  SendRefreshTokenResponse();
  EXPECT_TRUE(
      GetIdentityManager()->HasAccountWithRefreshToken(GetMainAccountID()));

  // Receive ENABLE_SYNC.
  SendEnableSyncResponse();

  // Check that the Dice request header was sent, with signout confirmation.
  std::string client_id = GaiaUrls::GetInstance()->oauth2_chrome_client_id();
  EXPECT_EQ(base::StringPrintf("version=%s,client_id=%s,device_id=%s,"
                               "signin_mode=all_accounts,"
                               "signout_mode=show_confirmation",
                               signin::kDiceProtocolVersion, client_id.c_str(),
                               GetDeviceId().c_str()),
            dice_request_header_);

  content::WindowedNotificationObserver ntp_url_observer(
      content::NOTIFICATION_LOAD_STOP,
      base::BindRepeating([](const content::NotificationSource&,
                             const content::NotificationDetails& details) {
        auto url =
            content::Details<content::LoadNotificationDetails>(details)->url;
        // Some test flags (e.g. ForceWebRequestProxyForTest) can change whether
        // the reported NTP URL is chrome://newtab or chrome://new-tab-page.
        return url == GURL(chrome::kChromeUINewTabPageURL) ||
               url == GURL(chrome::kChromeUINewTabURL);
      }));

  WaitForSigninSucceeded();
  EXPECT_EQ(GetMainAccountID(), GetIdentityManager()->GetPrimaryAccountId(
                                    signin::ConsentLevel::kSync));

  EXPECT_EQ(1, reconcilor_blocked_count_);
  WaitForReconcilorUnblockedCount(1);
  EXPECT_EQ(1, reconcilor_started_count_);

  // Check that the tab was navigated to the NTP.
  ntp_url_observer.Wait();

  // Dismiss the Sync confirmation UI.
  EXPECT_TRUE(login_ui_test_utils::ConfirmSyncConfirmationDialog(browser()));
}

// Tests that Sync is enabled if the ENABLE_SYNC response is received before the
// refresh token.

// https://crbug.com/1082858
#if (BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)) && !defined(NDEBUG)
#define MAYBE_EnableSyncBeforeToken DISABLED_EnableSyncBeforeToken
#else
#define MAYBE_EnableSyncBeforeToken EnableSyncBeforeToken
#endif
IN_PROC_BROWSER_TEST_F(DiceBrowserTest, MAYBE_EnableSyncBeforeToken) {
  EXPECT_EQ(0, reconcilor_started_count_);

  ui_test_utils::UrlLoadObserver enable_sync_url_observer(
      https_server_.GetURL(kEnableSyncURL),
      content::NotificationService::AllSources());

  // Signin using the Chrome Sync endpoint.
  browser()->signin_view_controller()->ShowSignin(
      profiles::BUBBLE_VIEW_MODE_GAIA_SIGNIN,
      signin_metrics::AccessPoint::ACCESS_POINT_SETTINGS);

  // Receive ENABLE_SYNC.
  SendEnableSyncResponse();
  // Wait for the page to be fully loaded.
  enable_sync_url_observer.Wait();

  // Receive token.
  EXPECT_FALSE(
      GetIdentityManager()->HasAccountWithRefreshToken(GetMainAccountID()));
  SendRefreshTokenResponse();
  EXPECT_TRUE(
      GetIdentityManager()->HasAccountWithRefreshToken(GetMainAccountID()));

  // Check that the Dice request header was sent, with signout confirmation.
  std::string client_id = GaiaUrls::GetInstance()->oauth2_chrome_client_id();
  EXPECT_EQ(base::StringPrintf("version=%s,client_id=%s,device_id=%s,"
                               "signin_mode=all_accounts,"
                               "signout_mode=show_confirmation",
                               signin::kDiceProtocolVersion, client_id.c_str(),
                               GetDeviceId().c_str()),
            dice_request_header_);

  ui_test_utils::UrlLoadObserver ntp_url_observer(
      GURL(chrome::kChromeUINewTabURL),
      content::NotificationService::AllSources());

  WaitForSigninSucceeded();
  EXPECT_EQ(GetMainAccountID(), GetIdentityManager()->GetPrimaryAccountId(
                                    signin::ConsentLevel::kSync));

  EXPECT_EQ(1, reconcilor_blocked_count_);
  WaitForReconcilorUnblockedCount(1);
  EXPECT_EQ(1, reconcilor_started_count_);

  // Check that the tab was navigated to the NTP.
  ntp_url_observer.Wait();

  // Dismiss the Sync confirmation UI.
  EXPECT_TRUE(login_ui_test_utils::ConfirmSyncConfirmationDialog(browser()));
}

// Verifies that Chrome doesn't crash on browser window close when the sync
// confirmation dialog is waiting for its size.
// Regression test for https://crbug.com/1304055.
IN_PROC_BROWSER_TEST_F(DiceBrowserTest,
                       CloseBrowserWhileInitializingSyncConfirmation) {
  // Signin using the Chrome Sync endpoint.
  browser()->signin_view_controller()->ShowSignin(
      profiles::BUBBLE_VIEW_MODE_GAIA_SIGNIN,
      signin_metrics::AccessPoint::ACCESS_POINT_SETTINGS);

  content::TestNavigationObserver sync_confirmation_url_observer(
      GURL("chrome://sync-confirmation"));
  sync_confirmation_url_observer.StartWatchingNewWebContents();

  // Receive token.
  SendRefreshTokenResponse();
  // Receive ENABLE_SYNC.
  SendEnableSyncResponse();

  WaitForSigninSucceeded();
  EXPECT_EQ(GetMainAccountID(), GetIdentityManager()->GetPrimaryAccountId(
                                    signin::ConsentLevel::kSync));

  // Wait until the sync confirmation webUI is created but not fully loaded
  // yet. The native dialog is not displayed yet since it waits until the webUI
  // passes the dialog height back to native.
  sync_confirmation_url_observer.WaitForNavigationFinished();

  // This should not crash.
  CloseBrowser();
}

// Tests that turning off Dice via preferences works when singed out.
IN_PROC_BROWSER_TEST_F(DiceBrowserTest, PRE_TurnOffDice_SignedOut) {
  ASSERT_FALSE(
      GetIdentityManager()->HasPrimaryAccount(signin::ConsentLevel::kSignin));
  ASSERT_TRUE(AccountConsistencyModeManager::IsDiceEnabledForProfile(
      browser()->profile()));

  // Turn off Dice for this profile.
  browser()->profile()->GetPrefs()->SetBoolean(
      prefs::kSigninAllowedOnNextStartup, false);
}

IN_PROC_BROWSER_TEST_F(DiceBrowserTest, TurnOffDice_SignedOut) {
  // Check that Dice is disabled.
  EXPECT_FALSE(
      browser()->profile()->GetPrefs()->GetBoolean(prefs::kSigninAllowed));
  EXPECT_FALSE(browser()->profile()->GetPrefs()->GetBoolean(
      prefs::kSigninAllowedOnNextStartup));
  EXPECT_FALSE(AccountConsistencyModeManager::IsDiceEnabledForProfile(
      browser()->profile()));

  EXPECT_FALSE(
      GetIdentityManager()->HasPrimaryAccount(signin::ConsentLevel::kSignin));

  // Navigate to Gaia and sign in.
  NavigateToURL(kSigninURL);
  // Check that the Dice request header was not sent.
  EXPECT_EQ(kNoDiceRequestHeader, dice_request_header_);
  EXPECT_EQ(0, reconcilor_blocked_count_);
  WaitForReconcilorUnblockedCount(0);
}

// Tests that turning off Dice via preferences works when signed in without sync
// consent.
//
// Regression test for crbug/1254325
IN_PROC_BROWSER_TEST_F(DiceBrowserTest, PRE_TurnOffDice_NotOptedIntoSync) {
  SetupSignedInAccounts(signin::ConsentLevel::kSignin);

  ASSERT_TRUE(
      GetIdentityManager()->HasPrimaryAccount(signin::ConsentLevel::kSignin));
  ASSERT_FALSE(
      GetIdentityManager()->HasPrimaryAccount(signin::ConsentLevel::kSync));
  ASSERT_TRUE(AccountConsistencyModeManager::IsDiceEnabledForProfile(
      browser()->profile()));

  // Turn off Dice for this profile.
  browser()->profile()->GetPrefs()->SetBoolean(
      prefs::kSigninAllowedOnNextStartup, false);
}

IN_PROC_BROWSER_TEST_F(DiceBrowserTest, TurnOffDice_NotOptedIntoSync) {
  // Check that Dice is disabled.
  EXPECT_FALSE(
      browser()->profile()->GetPrefs()->GetBoolean(prefs::kSigninAllowed));
  EXPECT_FALSE(browser()->profile()->GetPrefs()->GetBoolean(
      prefs::kSigninAllowedOnNextStartup));
  EXPECT_FALSE(AccountConsistencyModeManager::IsDiceEnabledForProfile(
      browser()->profile()));

  EXPECT_FALSE(
      GetIdentityManager()->HasPrimaryAccount(signin::ConsentLevel::kSignin));
  EXPECT_FALSE(
      GetIdentityManager()->HasAccountWithRefreshToken(GetMainAccountID()));
  EXPECT_TRUE(GetIdentityManager()->GetAccountsWithRefreshTokens().empty());

  // Navigate to Gaia and sign in.
  NavigateToURL(kSigninURL);
  // Check that the Dice request header was not sent.
  EXPECT_EQ(kNoDiceRequestHeader, dice_request_header_);
  EXPECT_EQ(0, reconcilor_blocked_count_);
  WaitForReconcilorUnblockedCount(0);
}

// Tests that turning off Dice via preferences works when signed in with sync
// consent
IN_PROC_BROWSER_TEST_F(DiceBrowserTest, PRE_TurnOffDice_OptedIntoSync) {
  // Sign the profile in and turn sync on.
  SetupSignedInAccounts(signin::ConsentLevel::kSync);
  syncer::SyncPrefs(browser()->profile()->GetPrefs()).SetFirstSetupComplete();

  ASSERT_TRUE(
      GetIdentityManager()->HasPrimaryAccount(signin::ConsentLevel::kSync));
  ASSERT_TRUE(AccountConsistencyModeManager::IsDiceEnabledForProfile(
      browser()->profile()));

  // Turn off Dice for this profile.
  browser()->profile()->GetPrefs()->SetBoolean(
      prefs::kSigninAllowedOnNextStartup, false);
}

IN_PROC_BROWSER_TEST_F(DiceBrowserTest, TurnOffDice_OptedIntoSync) {
  EXPECT_FALSE(
      browser()->profile()->GetPrefs()->GetBoolean(prefs::kSigninAllowed));
  EXPECT_FALSE(browser()->profile()->GetPrefs()->GetBoolean(
      prefs::kSigninAllowedOnNextStartup));
  EXPECT_FALSE(AccountConsistencyModeManager::IsDiceEnabledForProfile(
      browser()->profile()));

  EXPECT_FALSE(
      GetIdentityManager()->HasPrimaryAccount(signin::ConsentLevel::kSync));
  EXPECT_FALSE(
      GetIdentityManager()->HasPrimaryAccount(signin::ConsentLevel::kSignin));
  EXPECT_FALSE(
      GetIdentityManager()->HasAccountWithRefreshToken(GetMainAccountID()));
  EXPECT_TRUE(GetIdentityManager()->GetAccountsWithRefreshTokens().empty());

  // Navigate to Gaia and sign in.
  NavigateToURL(kSigninURL);
  // Check that the Dice request header was not sent.
  EXPECT_EQ(kNoDiceRequestHeader, dice_request_header_);
  EXPECT_EQ(0, reconcilor_blocked_count_);
  WaitForReconcilorUnblockedCount(0);
}

// Checks that Dice is disabled in incognito mode.
IN_PROC_BROWSER_TEST_F(DiceBrowserTest, Incognito) {
  Browser* incognito_browser = Browser::Create(Browser::CreateParams(
      browser()->profile()->GetPrimaryOTRProfile(/*create_if_needed=*/true),
      true));

  // Check that Dice is disabled.
  EXPECT_FALSE(AccountConsistencyModeManager::IsDiceEnabledForProfile(
      incognito_browser->profile()));
}

// This test is not specifically related to DICE, but it extends
// |DiceBrowserTest| for convenience.
class DiceManageAccountBrowserTest : public DiceBrowserTest {
 public:
  DiceManageAccountBrowserTest()
      : DiceBrowserTest(kMainManagedEmail),
        // Skip showing the error message box to avoid freezing the main thread.
        skip_message_box_auto_reset_(
            &chrome::internal::g_should_skip_message_box_for_test,
            true),
        // Force the policy component to prohibit clearing the primary account
        // even when the policy core component is not initialized.
        prohibit_sigout_auto_reset_(
            &policy::internal::g_force_prohibit_signout_for_tests,
            true) {}

  void SetUp() override {
#if BUILDFLAG(IS_WIN)
    // Shortcut deletion delays tests shutdown on Win-7 and results in time out.
    // See crbug.com/1073451.
    AppShortcutManager::SuppressShortcutsForTesting();
#endif
    DiceBrowserTest::SetUp();
  }

 protected:
  base::AutoReset<bool> skip_message_box_auto_reset_;
  base::AutoReset<bool> prohibit_sigout_auto_reset_;
  unsigned int number_of_profiles_added_ = 0;
};

// Tests that prohiting sign-in on startup for a managed profile clears the
// profile directory on next start-up.
IN_PROC_BROWSER_TEST_F(DiceManageAccountBrowserTest,
                       PRE_ClearManagedProfileOnStartup) {
  // Ensure that there are not deleted profiles before running this test.
  PrefService* local_state = g_browser_process->local_state();
  DCHECK(local_state);
  const base::Value::List& deleted_profiles =
      local_state->GetValueList(prefs::kProfilesDeleted);
  ASSERT_TRUE(deleted_profiles.empty());

  // Sign the profile in.
  SetupSignedInAccounts(signin::ConsentLevel::kSync);

  // Prohibit sign-in on next start-up.
  browser()->profile()->GetPrefs()->SetBoolean(
      prefs::kSigninAllowedOnNextStartup, false);
}

IN_PROC_BROWSER_TEST_F(DiceManageAccountBrowserTest,
                       ClearManagedProfileOnStartup) {
  // Initial profile should have been deleted as sign-in and sign out were no
  // longer allowed.
  PrefService* local_state = g_browser_process->local_state();
  DCHECK(local_state);
  const base::Value::List& deleted_profiles =
      local_state->GetValueList(prefs::kProfilesDeleted);
  EXPECT_EQ(1U, deleted_profiles.size());

  content::RunAllTasksUntilIdle();

  // Verify that there is an active profile.
  Profile* initial_profile = browser()->profile();
  EXPECT_EQ(1U, g_browser_process->profile_manager()->GetNumberOfProfiles());
  EXPECT_EQ(g_browser_process->profile_manager()->GetLastUsedProfile(),
            initial_profile);
}
