// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SEARCH_CORE_DISTILLER_URL_FETCHER_H_
#define COMPONENTS_SEARCH_CORE_DISTILLER_URL_FETCHER_H_

#include <string>

#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "components/search_engines/template_url_service.h"
#include "base/callback.h"
#include "net/base/network_change_notifier.h"

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}  // namespace network

class SearchURLFetcher;

// Class for creating a SearchURLFetcher.
class SearchURLFetcherFactory {
 public:
  explicit SearchURLFetcherFactory(
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory, PrefService* prefs, TemplateURLService* template_url_service);
  virtual ~SearchURLFetcherFactory();
  virtual SearchURLFetcher* CreateSearchURLFetcher() const;

 private:
  PrefService* prefs_;
  TemplateURLService* template_url_service_;
  friend class TestSearchURLFetcherFactory;
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
};

// This class loads a URL, and notifies the caller when the operation
// completes or fails. If the request fails, an empty string will be returned.
class SearchURLFetcher
    : public net::NetworkChangeNotifier::NetworkChangeObserver
{
 public:
  explicit SearchURLFetcher(
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory, PrefService* prefs, TemplateURLService* template_url_service);
  virtual ~SearchURLFetcher();

  virtual void FetchURL();

  SearchURLFetcher(const SearchURLFetcher&) = delete;
  SearchURLFetcher& operator=(const SearchURLFetcher&) = delete;

 protected:
  virtual std::unique_ptr<network::SimpleURLLoader> CreateURLFetcher();

 private:
  PrefService* prefs_;
  TemplateURLService* template_url_service_;
  int search_version_;
  bool already_loaded_;    // True if we've already loaded a URL once this run;
                           // we won't load again until after a restart.
  int search_version() const { return search_version_; }

  void OnURLLoadComplete(std::unique_ptr<std::string> response_body);
  void OnNetworkChanged(net::NetworkChangeNotifier::ConnectionType type);

  static const char kSearchDomainCheckURL[];
  std::unique_ptr<network::SimpleURLLoader> url_loader_;
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
};

#endif  // COMPONENTS_SEARCH_CORE_DISTILLER_URL_FETCHER_H_
