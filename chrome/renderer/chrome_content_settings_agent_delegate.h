// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_RENDERER_CHROME_CONTENT_SETTINGS_AGENT_DELEGATE_H_
#define CHROME_RENDERER_CHROME_CONTENT_SETTINGS_AGENT_DELEGATE_H_

#include "base/gtest_prod_util.h"
#include "components/content_settings/renderer/content_settings_agent_impl.h"
#include "extensions/buildflags/buildflags.h"

#if BUILDFLAG(ENABLE_EXTENSIONS)
namespace extensions {
class Dispatcher;
class Extension;
}  // namespace extensions
#endif

class ChromeContentSettingsAgentDelegate
    : public content_settings::ContentSettingsAgentImpl::Delegate,
      public content::RenderFrameObserver,
      public content::RenderFrameObserverTracker<
          ChromeContentSettingsAgentDelegate> {
 public:
  explicit ChromeContentSettingsAgentDelegate(
      content::RenderFrame* render_frame);
  ~ChromeContentSettingsAgentDelegate() override;

#if BUILDFLAG(ENABLE_EXTENSIONS)
  // Sets the extension dispatcher. Call this right after constructing this
  // class. This should only be called once.
  void SetExtensionDispatcher(extensions::Dispatcher* extension_dispatcher);
#endif

  bool IsPluginTemporarilyAllowed(const std::string& identifier);
  void AllowPluginTemporarily(const std::string& identifier);

  // content_settings::ContentSettingsAgentImpl::Delegate:
  bool IsSchemeAllowlisted(const std::string& scheme) override;
  absl::optional<bool> AllowReadFromClipboard() override;
  absl::optional<bool> AllowWriteToClipboard() override;
  absl::optional<bool> AllowMutationEvents() override;

 private:
  FRIEND_TEST_ALL_PREFIXES(ChromeContentSettingsAgentDelegateBrowserTest,
                           PluginsTemporarilyAllowed);

  // RenderFrameObserver:
  void DidCommitProvisionalLoad(ui::PageTransition transition) override;
  void OnDestruct() override;

  // Whether the observed RenderFrame is for a platform app.
  bool IsPlatformApp();

  // Whether the observed RenderFrame is an allow-listed System Web App.
  bool IsAllowListedSystemWebApp();

#if BUILDFLAG(ENABLE_EXTENSIONS)
  // If |origin| corresponds to an installed extension, returns that extension.
  // Otherwise returns null.
  const extensions::Extension* GetExtension(
      const blink::WebSecurityOrigin& origin) const;

  // Owned by ChromeContentRendererClient and outlive us.
  extensions::Dispatcher* extension_dispatcher_ = nullptr;
#endif

  base::flat_set<std::string> temporarily_allowed_plugins_;

  content::RenderFrame* render_frame_ = nullptr;
};

#endif  // CHROME_RENDERER_CHROME_CONTENT_SETTINGS_AGENT_DELEGATE_H_
