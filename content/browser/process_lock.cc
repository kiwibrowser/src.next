// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/process_lock.h"

#include "base/strings/stringprintf.h"
#include "content/public/browser/browser_thread.h"

namespace content {

// static
ProcessLock ProcessLock::CreateAllowAnySite(
    const StoragePartitionConfig& storage_partition_config,
    const WebExposedIsolationInfo& web_exposed_isolation_info) {
  return ProcessLock(SiteInfo(
      GURL(), GURL(), false, false /* is_sandboxed */,
      UrlInfo::kInvalidUniqueSandboxId, storage_partition_config,
      web_exposed_isolation_info, /* is_guest */ false,
      /* does_site_request_dedicated_process_for_coop */ false,
      /* is_jit_disabled */ false, /* is_pdf */ false, /* is_fenced */ false));
}

// static
ProcessLock ProcessLock::Create(const IsolationContext& isolation_context,
                                const UrlInfo& url_info) {
  DCHECK(url_info.storage_partition_config.has_value());
  if (BrowserThread::CurrentlyOn(BrowserThread::UI))
    return ProcessLock(SiteInfo::Create(isolation_context, url_info));

  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  // On the IO thread we need to use a special SiteInfo creation method because
  // we cannot properly compute some SiteInfo fields on that thread.
  // ProcessLocks must always match no matter which thread they were created on,
  // but the SiteInfo objects used to create them may not always match.
  return ProcessLock(SiteInfo::CreateOnIOThread(isolation_context, url_info));
}

// static
ProcessLock ProcessLock::FromSiteInfo(const SiteInfo& site_info) {
  return ProcessLock(site_info);
}

ProcessLock::ProcessLock(const SiteInfo& site_info) : site_info_(site_info) {}

ProcessLock::ProcessLock() = default;

ProcessLock::ProcessLock(const ProcessLock&) = default;

ProcessLock& ProcessLock::operator=(const ProcessLock&) = default;

ProcessLock::~ProcessLock() = default;

StoragePartitionConfig ProcessLock::GetStoragePartitionConfig() const {
  DCHECK(site_info_.has_value());
  return site_info_->storage_partition_config();
}

WebExposedIsolationInfo ProcessLock::GetWebExposedIsolationInfo() const {
  return site_info_.has_value() ? site_info_->web_exposed_isolation_info()
                                : WebExposedIsolationInfo::CreateNonIsolated();
}

bool ProcessLock::IsASiteOrOrigin() const {
  const GURL lock_url = ProcessLock::lock_url();
  return lock_url.has_scheme() && lock_url.has_host() && lock_url.is_valid();
}

bool ProcessLock::HasOpaqueOrigin() const {
  DCHECK(is_locked_to_site());
  return url::Origin::Create(lock_url()).opaque();
}

bool ProcessLock::MatchesOrigin(const url::Origin& origin) const {
  url::Origin process_lock_origin = url::Origin::Create(lock_url());
  return origin == process_lock_origin;
}

bool ProcessLock::IsCompatibleWithWebExposedIsolation(
    const SiteInfo& site_info) const {
  return site_info_.has_value() && site_info_->web_exposed_isolation_info() ==
                                       site_info.web_exposed_isolation_info();
}

bool ProcessLock::operator==(const ProcessLock& rhs) const {
  if (site_info_.has_value() != rhs.site_info_.has_value())
    return false;

  if (!site_info_.has_value())  // Neither has a value, so they're equal.
    return true;

  // At this point, both `this` and `rhs` are known to have valid SiteInfos.
  // Here we proceed with a comparison almost identical to
  // SiteInfo::MakeSecurityPrincipalKey(), except that `site_url_` is excluded.
  return site_info_->ProcessLockCompareTo(rhs.site_info_.value()) == 0;
}

bool ProcessLock::operator!=(const ProcessLock& rhs) const {
  return !(*this == rhs);
}

bool ProcessLock::operator<(const ProcessLock& rhs) const {
  if (!site_info_.has_value() && !rhs.site_info_.has_value())
    return false;
  if (!site_info_.has_value())  // Here rhs.site_info_.has_value() is true.
    return true;
  if (!rhs.site_info_.has_value())  // Here site_info_.has_value() is true.
    return false;

  // At this point, both `this` and `rhs` are known to have valid SiteInfos.
  // Here we proceed with a comparison almost identical to
  // SiteInfo::MakeSecurityPrincipalKey(), except that `site_url_` is excluded.
  return site_info_->ProcessLockCompareTo(rhs.site_info_.value()) < 0;
}

std::string ProcessLock::ToString() const {
  std::string ret = "{ ";

  if (site_info_.has_value()) {
    ret += lock_url().possibly_invalid_spec();

    if (is_origin_keyed_process())
      ret += " origin-keyed";

    if (is_sandboxed()) {
      ret += " sandboxed";
      if (site_info_->unique_sandbox_id() != UrlInfo::kInvalidUniqueSandboxId)
        ret += base::StringPrintf(" (id=%d)", site_info_->unique_sandbox_id());
    }

    if (is_pdf())
      ret += " pdf";

    if (is_guest())
      ret += " guest";

    if (is_fenced())
      ret += " fenced";

    if (GetWebExposedIsolationInfo().is_isolated()) {
      ret += " cross-origin-isolated";
      if (GetWebExposedIsolationInfo().is_isolated_application())
        ret += "-application";
      ret += " coi-origin='" +
             GetWebExposedIsolationInfo().origin().GetDebugString() + "'";
    }

    if (!GetStoragePartitionConfig().is_default()) {
      ret += ", partition=" + GetStoragePartitionConfig().partition_domain() +
             "." + GetStoragePartitionConfig().partition_name();
      if (GetStoragePartitionConfig().in_memory())
        ret += ", in-memory";
    }
  } else {
    ret += " no-site-info";
  }
  ret += " }";

  return ret;
}

std::ostream& operator<<(std::ostream& out, const ProcessLock& process_lock) {
  return out << process_lock.ToString();
}

}  // namespace content
