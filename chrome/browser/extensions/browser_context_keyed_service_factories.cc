// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/browser_context_keyed_service_factories.h"

#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/extensions/activity_log/activity_log.h"
#include "chrome/browser/extensions/api/activity_log_private/activity_log_private_api.h"
#include "chrome/browser/extensions/api/autofill_private/autofill_private_event_router_factory.h"
#include "chrome/browser/extensions/api/bookmark_manager_private/bookmark_manager_private_api.h"
#include "chrome/browser/extensions/api/bookmarks/bookmarks_api.h"
#include "chrome/browser/extensions/api/braille_display_private/braille_display_private_api.h"
#include "chrome/browser/extensions/api/commands/command_service.h"
#include "chrome/browser/extensions/api/cookies/cookies_api.h"
#include "chrome/browser/extensions/api/developer_private/developer_private_api.h"
#include "chrome/browser/extensions/api/extension_action/extension_action_api.h"
#include "chrome/browser/extensions/api/font_settings/font_settings_api.h"
#include "chrome/browser/extensions/api/history/history_api.h"
#include "chrome/browser/extensions/api/identity/identity_api.h"
#include "chrome/browser/extensions/api/language_settings_private/language_settings_private_delegate_factory.h"
#include "chrome/browser/extensions/api/networking_private/networking_private_ui_delegate_factory_impl.h"
#include "chrome/browser/extensions/api/omnibox/omnibox_api.h"
#include "chrome/browser/extensions/api/passwords_private/passwords_private_event_router_factory.h"
#include "chrome/browser/extensions/api/preference/preference_api.h"
#include "chrome/browser/extensions/api/processes/processes_api.h"
#include "chrome/browser/extensions/api/safe_browsing_private/safe_browsing_private_event_router_factory.h"
#include "chrome/browser/extensions/api/sessions/sessions_api.h"
#include "chrome/browser/extensions/api/settings_overrides/settings_overrides_api.h"
#include "chrome/browser/extensions/api/settings_private/settings_private_event_router_factory.h"
#include "chrome/browser/extensions/api/side_panel/side_panel_service.h"
#include "chrome/browser/extensions/api/tab_capture/tab_capture_registry.h"
#include "chrome/browser/extensions/api/tab_groups/tab_groups_event_router_factory.h"
#include "chrome/browser/extensions/api/tabs/tabs_windows_api.h"
#include "chrome/browser/extensions/api/web_authentication_proxy/web_authentication_proxy_api.h"
#include "chrome/browser/extensions/api/web_navigation/web_navigation_api.h"
#include "chrome/browser/extensions/api/webrtc_audio_private/webrtc_audio_private_api.h"
#include "chrome/browser/extensions/chrome_extension_cookies_factory.h"
#include "chrome/browser/extensions/extension_garbage_collector_factory.h"
#include "chrome/browser/extensions/extension_gcm_app_handler.h"
#include "chrome/browser/extensions/extension_management.h"
#include "chrome/browser/extensions/extension_system_factory.h"
#include "chrome/browser/extensions/extension_web_ui_override_registrar.h"
#include "chrome/browser/extensions/install_tracker_factory.h"
#include "chrome/browser/extensions/install_verifier_factory.h"
#include "chrome/browser/extensions/menu_manager_factory.h"
#include "chrome/browser/extensions/plugin_manager.h"
#include "chrome/browser/extensions/warning_badge_service_factory.h"
#include "chrome/browser/speech/extension_api/tts_extension_api.h"
#include "chrome/common/buildflags.h"
#include "extensions/browser/api/bluetooth_low_energy/bluetooth_low_energy_api.h"
#include "extensions/browser/api/networking_private/networking_private_delegate_factory.h"
#include "ppapi/buildflags/buildflags.h"

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
#include "chrome/browser/extensions/api/system_indicator/system_indicator_manager_factory.h"
#endif

#if BUILDFLAG(IS_CHROMEOS_ASH)
#include "chrome/browser/chromeos/extensions/login_screen/login/external_logout_done/external_logout_done_event_handler_factory.h"
#include "chrome/browser/extensions/api/input_ime/input_ime_api.h"
#include "chrome/browser/extensions/api/platform_keys/verify_trust_api.h"
#include "chrome/browser/extensions/api/terminal/terminal_private_api.h"
#endif

#if BUILDFLAG(ENABLE_SERVICE_DISCOVERY)
#include "chrome/browser/extensions/api/mdns/mdns_api.h"
#endif

namespace chrome_extensions {

void EnsureBrowserContextKeyedServiceFactoriesBuilt() {
  extensions::ActivityLog::GetFactoryInstance();
  extensions::ActivityLogAPI::GetFactoryInstance();
  extensions::AutofillPrivateEventRouterFactory::GetInstance();
  extensions::BluetoothLowEnergyAPI::GetFactoryInstance();
  extensions::BookmarksAPI::GetFactoryInstance();
  extensions::BookmarkManagerPrivateAPI::GetFactoryInstance();
  extensions::BrailleDisplayPrivateAPI::GetFactoryInstance();
  extensions::CommandService::GetFactoryInstance();
  extensions::CookiesAPI::GetFactoryInstance();
  extensions::ChromeExtensionCookiesFactory::GetInstance();
  extensions::DeveloperPrivateAPI::GetFactoryInstance();
  extensions::ExtensionActionAPI::GetFactoryInstance();
  extensions::ExtensionGarbageCollectorFactory::GetInstance();
  extensions::ExtensionGCMAppHandler::GetFactoryInstance();
  extensions::ExtensionManagementFactory::GetInstance();
  extensions::ExtensionSystemFactory::GetInstance();
  extensions::ExtensionWebUIOverrideRegistrar::GetFactoryInstance();
#if BUILDFLAG(IS_CHROMEOS_ASH)
  extensions::ExternalLogoutDoneEventHandlerFactory::GetInstance();
#endif
  extensions::FontSettingsAPI::GetFactoryInstance();
  extensions::HistoryAPI::GetFactoryInstance();
  extensions::IdentityAPI::GetFactoryInstance();
  extensions::InstallTrackerFactory::GetInstance();
  extensions::InstallVerifierFactory::GetInstance();
#if BUILDFLAG(IS_CHROMEOS_ASH)
  extensions::InputImeAPI::GetFactoryInstance();
#endif
  extensions::LanguageSettingsPrivateDelegateFactory::GetInstance();
#if BUILDFLAG(ENABLE_SERVICE_DISCOVERY)
  extensions::MDnsAPI::GetFactoryInstance();
#endif
  extensions::MenuManagerFactory::GetInstance();
#if BUILDFLAG(IS_CHROMEOS_ASH) || BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC)
  auto networking_private_ui_delegate_factory =
      std::make_unique<extensions::NetworkingPrivateUIDelegateFactoryImpl>();
  extensions::NetworkingPrivateDelegateFactory::GetInstance()
      ->SetUIDelegateFactory(std::move(networking_private_ui_delegate_factory));
#endif
  extensions::OmniboxAPI::GetFactoryInstance();
  extensions::PasswordsPrivateEventRouterFactory::GetInstance();
#if BUILDFLAG(ENABLE_PLUGINS)
  extensions::PluginManager::GetFactoryInstance();
#endif
  extensions::PreferenceAPI::GetFactoryInstance();
  extensions::ProcessesAPI::GetFactoryInstance();
  extensions::SafeBrowsingPrivateEventRouterFactory::GetInstance();
  extensions::SessionsAPI::GetFactoryInstance();
  extensions::SettingsPrivateEventRouterFactory::GetInstance();
  extensions::SettingsOverridesAPI::GetFactoryInstance();
  extensions::SidePanelService::GetFactoryInstance();
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
  extensions::SystemIndicatorManagerFactory::GetInstance();
#endif
  extensions::TabGroupsEventRouterFactory::GetInstance();
  extensions::TabCaptureRegistry::GetFactoryInstance();
  extensions::TabsWindowsAPI::GetFactoryInstance();
#if BUILDFLAG(IS_CHROMEOS_ASH)
  extensions::TerminalPrivateAPI::GetFactoryInstance();
#endif
  extensions::TtsAPI::GetFactoryInstance();
#if BUILDFLAG(IS_CHROMEOS_ASH)
  extensions::VerifyTrustAPI::GetFactoryInstance();
#endif

  extensions::WarningBadgeServiceFactory::GetInstance();
  extensions::WebAuthenticationProxyAPI::GetFactoryInstance();
  extensions::WebNavigationAPI::GetFactoryInstance();
  extensions::WebrtcAudioPrivateEventService::GetFactoryInstance();
}

}  // namespace chrome_extensions
