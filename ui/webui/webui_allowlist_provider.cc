// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/webui/webui_allowlist_provider.h"

#include "components/content_settings/core/common/content_settings_pattern.h"
#include "ui/webui/webui_allowlist.h"

WebUIAllowlistProvider::WebUIAllowlistProvider(
    scoped_refptr<WebUIAllowlist> allowlist)
    : allowlist_(std::move(allowlist)) {
  DCHECK(allowlist_);
  allowlist_->SetWebUIAllowlistProvider(this);
}

WebUIAllowlistProvider::~WebUIAllowlistProvider() = default;

std::unique_ptr<content_settings::RuleIterator>
WebUIAllowlistProvider::GetRuleIterator(ContentSettingsType content_type,
                                        bool incognito) const {
  return allowlist_->GetRuleIterator(content_type);
}

void WebUIAllowlistProvider::NotifyContentSettingChange(
    const ContentSettingsPattern& primary_pattern,
    const ContentSettingsPattern& secondary_pattern,
    ContentSettingsType content_type) {
  NotifyObservers(primary_pattern, secondary_pattern, content_type);
}

bool WebUIAllowlistProvider::SetWebsiteSetting(
    const ContentSettingsPattern& primary_pattern,
    const ContentSettingsPattern& secondary_pattern,
    ContentSettingsType content_type,
    base::Value&& value,
    const content_settings::ContentSettingConstraints& constraints) {
  // WebUIAllowlistProvider doesn't support settings Website settings.
  return false;
}

void WebUIAllowlistProvider::ClearAllContentSettingsRules(
    ContentSettingsType content_type) {
  // WebUIAllowlistProvider doesn't support changing content settings directly.
}

void WebUIAllowlistProvider::ShutdownOnUIThread() {
  DCHECK(CalledOnValidThread());

  RemoveAllObservers();
  allowlist_->ResetWebUIAllowlistProvider();
}
