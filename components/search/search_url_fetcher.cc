// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/search/search_url_fetcher.h"

#include "base/bind.h"
#include "components/version_info/version_info_values.h"
#include "base/json/json_string_value_serializer.h"
#include "net/base/url_util.h"
#include "net/base/load_flags.h"
#include "base/android/sys_utils.h"
#include "components/search_engines/template_url_service.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/search_engines/search_engines_pref_names.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "net/base/network_change_notifier.h"
#include "url/gurl.h"
#include "services/network/public/cpp/resource_request.h"

#include "components/search_engines/template_url_service.h"
#include "components/search_engines/template_url_data_util.h"

#include "services/network/public/mojom/url_response_head.mojom.h"

const char SearchURLFetcher::kSearchDomainCheckURL[] =
    "https://settings.kiwibrowser.com/search/getrecommendedsearch?format=domain&serie=next&type=chrome&version=" PRODUCT_VERSION "&release_name=" RELEASE_NAME "&release_version=" RELEASE_VERSION;

SearchURLFetcherFactory::SearchURLFetcherFactory(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory, PrefService* prefs, TemplateURLService* template_url_service)
    : url_loader_factory_(url_loader_factory),
      prefs_(prefs),
      template_url_service_(template_url_service) {}

SearchURLFetcherFactory::~SearchURLFetcherFactory() {}

SearchURLFetcher* SearchURLFetcherFactory::CreateSearchURLFetcher()
    const {
  return new SearchURLFetcher(url_loader_factory_, prefs_, template_url_service_);
}

SearchURLFetcher::SearchURLFetcher(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory, PrefService* prefs, TemplateURLService* template_url_service)
    : url_loader_factory_(url_loader_factory),
      prefs_(prefs),
      template_url_service_(template_url_service) {
  LOG(INFO) << "[Kiwi] List of search engines is initializing";
  net::NetworkChangeNotifier::AddNetworkChangeObserver(this);
}

SearchURLFetcher::~SearchURLFetcher() {}

void SearchURLFetcher::FetchURL() {
  // Don't allow a fetch if one is pending.
  if (already_loaded_)
    return;
  DCHECK(!url_loader_);
  url_loader_ = CreateURLFetcher();
  url_loader_->DownloadToStringOfUnboundedSizeUntilCrashAndDie(
      url_loader_factory_.get(),
      base::BindOnce(&SearchURLFetcher::OnURLLoadComplete,
                     base::Unretained(this)));
}

std::unique_ptr<network::SimpleURLLoader> SearchURLFetcher::CreateURLFetcher() {
  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("search", R"(
        semantics {
          sender: "Search URL"
          description:
            "Chromium provides Mobile-friendly view on Android phones when the "
          trigger:
            "When the user enters Mobile-friendly view on Android phones, or "
          data:
            "URLs of the required website resources to fetch."
          destination: WEBSITE
        }
        policy {
          cookies_allowed: YES
          cookies_store: "user"
          setting: "Users can enable or disable Mobile-friendly view by "
          "toggling chrome://flags#reader-mode-heuristics in Chromium on "
          "Android."
          policy_exception_justification:
            "Not implemented, considered not useful as no content is being "
            "uploaded; this request merely downloads the resources on the web."
        })");
  auto resource_request = std::make_unique<network::ResourceRequest>();
  resource_request->url = GURL(SearchURLFetcher::kSearchDomainCheckURL);
  resource_request->method = "GET";

  long firstInstallDate = base::android::SysUtils::FirstInstallDateFromJni();
  resource_request->url = net::AppendOrReplaceQueryParameter(resource_request->url, "install_date", base::NumberToString(firstInstallDate));
  int searchVersion = prefs_->GetInteger(prefs::kSearchProviderOverridesVersion);
  resource_request->url = net::AppendOrReplaceQueryParameter(resource_request->url, "settings_version", std::to_string(searchVersion));
  std::string referrerString = base::android::SysUtils::ReferrerStringFromJni();
  resource_request->url = net::AppendOrReplaceQueryParameter(resource_request->url, "ref", referrerString);

  LOG(INFO) << "[Kiwi] List of search engines is requesting";

  resource_request->load_flags =
      (net::LOAD_DISABLE_CACHE | net::LOAD_DO_NOT_SAVE_COOKIES);

  auto url_loader = network::SimpleURLLoader::Create(
      std::move(resource_request), traffic_annotation);
  static const int kMaxRetries = 5;
  url_loader->SetRetryOptions(kMaxRetries,
                              network::SimpleURLLoader::RETRY_ON_5XX | network::SimpleURLLoader::RetryMode::RETRY_ON_NETWORK_CHANGE);

  return url_loader;
}

void SearchURLFetcher::OnURLLoadComplete(
    std::unique_ptr<std::string> response_body) {
  int version_code = -1;
  int enable_server_suggestions = -1;

  LOG(INFO) << "[Kiwi] We received response from SearchURLFetcher - A";

  if (response_body)
    LOG(INFO) << "[Kiwi] List of search engines returned with body";
  else
    LOG(INFO) << "[Kiwi] List of search engines returned without body";
  if (!response_body) {
    LOG(INFO) << "[Kiwi] We received response from SearchURLFetcher - Empty response";
    url_loader_.reset();
    already_loaded_ = false;
    return;
  }
  std::string body;
  if (url_loader_->ResponseInfo() && url_loader_->ResponseInfo()->headers && url_loader_->ResponseInfo()->headers->HasHeader("se-version-code")) {
    version_code = url_loader_->ResponseInfo()->headers->GetInt64HeaderValue("se-version-code");
  } else {
    // Delete the loader.
    url_loader_.reset();
    already_loaded_ = false;
    return;
  }
  body = std::move(*response_body);
  LOG(INFO) << "[Kiwi] version_code: [" << version_code << "], response_body: [" << body.length() << "]";
  LOG(INFO) << "[Kiwi] List of search engines returned with body:" << body;
  if (!base::StartsWith(body, "{",
                        base::CompareCase::INSENSITIVE_ASCII)) {
    LOG(INFO) << "[Kiwi] Received invalid search-engines info with [" << body.length() << "]";
    url_loader_.reset();
    already_loaded_ = false;
    return;
  }

  if (version_code != -1 && search_version_ != version_code && version_code > 0 && body.length() > 10) {
    search_version_ = version_code;
    LOG(INFO) << "[Kiwi] Received search-engines version: [" << version_code << "] settings from server-side: " << body.length() << " chars";

    std::unique_ptr<base::DictionaryValue> master_dictionary_;

    JSONStringValueDeserializer json(body);
    std::string error;
    std::unique_ptr<base::Value> root(json.Deserialize(NULL, &error));
    if (!root.get()) {
      LOG(ERROR) << "[Kiwi] Failed to parse brandcode prefs file: " << error;
      url_loader_.reset();
      already_loaded_ = false;
      return;
    }
    if (!root->is_dict()) {
      LOG(ERROR) << "[Kiwi] Failed to parse brandcode prefs file: "
                 << "Root item must be a dictionary.";
      url_loader_.reset();
      already_loaded_ = false;
      return;
    }

    const TemplateURL* default_search = template_url_service_->GetDefaultSearchProvider();
    int current_default_search_prepopulated_id = 1;
    std::u16string current_default_search_prepopulated_keyword = u"kiwi";
    if (default_search)
      current_default_search_prepopulated_id = default_search->prepopulate_id();
    if (default_search)
      current_default_search_prepopulated_keyword = default_search->keyword();

    LOG(INFO) << "[Kiwi] search_url_fetcher - Trying to find template for search engine keyword: " << current_default_search_prepopulated_keyword;
    TemplateURL *t = template_url_service_->FindPrepopulatedTemplateURLByKeyword(current_default_search_prepopulated_keyword);
    if (!t) {
      LOG(INFO) << "[Kiwi] search_url_fetcher - Trying to find template for search engine : " << current_default_search_prepopulated_id;
      t = template_url_service_->FindPrepopulatedTemplateURL(current_default_search_prepopulated_id);
    }
    if (!t) {
      LOG(INFO) << "[Kiwi] search_url_fetcher - Template not found, trying to find template for search engine ID 1";
      t = template_url_service_->FindPrepopulatedTemplateURL(1);
    }
    if (!t) {
      LOG(INFO) << "[Kiwi] search_url_fetcher - Template not found, trying to find template for search engine keyword kiwi";
      t = template_url_service_->FindPrepopulatedTemplateURLByKeyword(u"kiwi");
    }
    if (!t) {
      LOG(ERROR) << "[Kiwi] search_url_fetcher - Error, cannot find default template";
      return ;
    }
    const TemplateURLData *new_dse = &(t->data());
    if (!new_dse) {
      LOG(ERROR) << "[Kiwi] search_url_fetcher - Error, cannot find new dse";
      return ;
    }
    std::unique_ptr<base::DictionaryValue> saved_dse = TemplateURLDataToDictionary(*new_dse);

    master_dictionary_.reset(
        static_cast<base::DictionaryValue*>(root.release()));

    const base::ListValue* value = NULL;
    if (master_dictionary_ &&
        master_dictionary_->GetList(prefs::kSearchProviderOverrides, &value) &&
        value && value->GetSize() >= 2) {
      LOG(INFO) << "[Kiwi] Search engine list contains " << value->GetSize() << " elements";

      prefs_->ClearPref(prefs::kSearchProviderOverrides);
      prefs_->SetInteger(prefs::kSearchProviderOverridesVersion,
                                     -1);
      prefs_->SetInteger(prefs::kLastKnownSearchVersion,
                                     -1);
      ListPrefUpdate update(prefs_, prefs::kSearchProviderOverrides);
      base::ListValue* list = update.Get();
      bool found_existing_search_engine = false;
      bool success = false;
      size_t num_engines = value->GetSize();
      for (size_t i = 0; i != num_engines; ++i) {
        const base::DictionaryValue* engine;
        if (value->GetDictionary(i, &engine)) {
          success = true;
          std::u16string name;
          engine->GetString("name", &name);
          std::u16string keyword;
          engine->GetString("keyword", &keyword);
          LOG(INFO) << "[Kiwi] Adding to the list one search engine: " << engine << " is " << name << " (keyword: " << keyword << ")";
          if (keyword == new_dse->keyword())
            found_existing_search_engine = true;
          list->Append(engine->CreateDeepCopy());
        }
      }

      if (found_existing_search_engine || new_dse->id == 1 || new_dse->prepopulate_id == 1) {
        LOG(INFO) << "[Kiwi] Search engine " << new_dse->keyword() << " was already present";
      } else {
        LOG(INFO) << "[Kiwi] Search engine " << new_dse->keyword() << " was not already present";
        list->Append(saved_dse->CreateDeepCopy());
      }

      if (success) {
        LOG(INFO) << "[Kiwi] Search engines processing is a success";
        prefs_->SetInteger(prefs::kSearchProviderOverridesVersion,
                                       version_code);
        prefs_->SetInteger(prefs::kLastKnownSearchVersion,
                                       version_code);
        template_url_service_->SearchEnginesChanged();
      } else {
        LOG(ERROR) << "[Kiwi] Failure, no search engine found";
      }
      url_loader_.reset();
      already_loaded_ = false;
      return ;
    }
    LOG(ERROR) << "[Kiwi] Failed to parse search-engines JSON";
  } else {
    LOG(INFO) << "[Kiwi] Received search-engines [" << version_code << "] settings from server-side: " << body.length() << " chars but we already have it";
  }
  url_loader_.reset();
  already_loaded_ = false;
}

void SearchURLFetcher::OnNetworkChanged(net::NetworkChangeNotifier::ConnectionType type) {
  // Ignore destructive signals.
  LOG(INFO) << "[Kiwi] SearchURLFetcher::OnNetworkChanged";
  if (type == net::NetworkChangeNotifier::CONNECTION_NONE)
    return;
  already_loaded_ = false;
  url_loader_.reset();
  FetchURL();
}
