// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/embedder_support/content_settings_utils.h"

#include "components/content_settings/browser/page_specific_content_settings.h"
#include "components/content_settings/core/browser/cookie_settings.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_utils.h"
#include "content/public/browser/browser_thread.h"
#include "net/cookies/site_for_cookies.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace embedder_support {

using StorageType =
    content_settings::mojom::ContentSettingsManager::StorageType;
using QueryReason = content_settings::CookieSettings::QueryReason;

namespace {
bool AllowWorkerStorageAccess(
    StorageType storage_type,
    const GURL& url,
    const std::vector<content::GlobalRenderFrameHostId>& render_frames,
    const content_settings::CookieSettings* cookie_settings) {
  bool allow = cookie_settings->IsFullCookieAccessAllowed(
      url, net::SiteForCookies::FromUrl(url), url::Origin::Create(url),
      QueryReason::kSiteStorage);

  for (const auto& it : render_frames) {
    content_settings::PageSpecificContentSettings::StorageAccessed(
        storage_type, it.child_id, it.frame_routing_id, url, !allow);
  }

  return allow;
}
}  // namespace

content::AllowServiceWorkerResult AllowServiceWorker(
    const GURL& scope,
    const net::SiteForCookies& site_for_cookies,
    const absl::optional<url::Origin>& top_frame_origin,
    const content_settings::CookieSettings* cookie_settings,
    const HostContentSettingsMap* settings_map) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  // TODO(crbug.com/1336617): Remove this check once we figure out what is
  // wrong.
  DCHECK(settings_map);
  GURL first_party_url = top_frame_origin ? top_frame_origin->GetURL() : GURL();
  // Check if JavaScript is allowed.
  content_settings::SettingInfo info;
  const base::Value value = settings_map->GetWebsiteSetting(
      first_party_url, first_party_url, ContentSettingsType::JAVASCRIPT, &info);
  ContentSetting setting = content_settings::ValueToContentSetting(value);
  bool allow_javascript = setting == CONTENT_SETTING_ALLOW;

  // Check if cookies are allowed.
  bool allow_cookies = cookie_settings->IsFullCookieAccessAllowed(
      scope, site_for_cookies, top_frame_origin, QueryReason::kSiteStorage);

  return content::AllowServiceWorkerResult::FromPolicy(!allow_javascript,
                                                       !allow_cookies);
}

bool AllowSharedWorker(
    const GURL& worker_url,
    const net::SiteForCookies& site_for_cookies,
    const absl::optional<url::Origin>& top_frame_origin,
    const std::string& name,
    const blink::StorageKey& storage_key,
    int render_process_id,
    int render_frame_id,
    const content_settings::CookieSettings* cookie_settings) {
  bool allow = cookie_settings->IsFullCookieAccessAllowed(
      worker_url, site_for_cookies, top_frame_origin,
      QueryReason::kSiteStorage);

  content_settings::PageSpecificContentSettings::SharedWorkerAccessed(
      render_process_id, render_frame_id, worker_url, name, storage_key,
      !allow);
  return allow;
}

bool AllowWorkerFileSystem(
    const GURL& url,
    const std::vector<content::GlobalRenderFrameHostId>& render_frames,
    const content_settings::CookieSettings* cookie_settings) {
  return AllowWorkerStorageAccess(StorageType::FILE_SYSTEM, url, render_frames,
                                  cookie_settings);
}

bool AllowWorkerIndexedDB(
    const GURL& url,
    const std::vector<content::GlobalRenderFrameHostId>& render_frames,
    const content_settings::CookieSettings* cookie_settings) {
  return AllowWorkerStorageAccess(StorageType::INDEXED_DB, url, render_frames,
                                  cookie_settings);
}

bool AllowWorkerCacheStorage(
    const GURL& url,
    const std::vector<content::GlobalRenderFrameHostId>& render_frames,
    const content_settings::CookieSettings* cookie_settings) {
  return AllowWorkerStorageAccess(StorageType::CACHE, url, render_frames,
                                  cookie_settings);
}

bool AllowWorkerWebLocks(
    const GURL& url,
    const content_settings::CookieSettings* cookie_settings) {
  return AllowWorkerStorageAccess(StorageType::WEB_LOCKS, url, {},
                                  cookie_settings);
}

}  // namespace embedder_support
