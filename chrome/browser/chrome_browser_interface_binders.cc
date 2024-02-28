// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chrome_browser_interface_binders.h"

#include <utility>

#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/strings/stringprintf.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/accessibility/accessibility_labels_service.h"
#include "chrome/browser/accessibility/accessibility_labels_service_factory.h"
#include "chrome/browser/ash/drive/file_system_util.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/buildflags.h"
#include "chrome/browser/cart/commerce_hint_service.h"
#include "chrome/browser/companion/core/features.h"
#include "chrome/browser/dom_distiller/dom_distiller_service_factory.h"
#include "chrome/browser/history_clusters/history_clusters_service_factory.h"
#include "chrome/browser/media/media_engagement_score_details.mojom.h"
#include "chrome/browser/model_execution/model_manager_impl.h"
#include "chrome/browser/navigation_predictor/navigation_predictor.h"
#include "chrome/browser/optimization_guide/optimization_guide_internals_ui.h"
#include "chrome/browser/password_manager/chrome_password_manager_client.h"
#include "chrome/browser/predictors/lcp_critical_path_predictor/lcp_critical_path_predictor_host.h"
#include "chrome/browser/predictors/network_hints_handler_impl.h"
#include "chrome/browser/preloading/prefetch/no_state_prefetch/chrome_no_state_prefetch_contents_delegate.h"
#include "chrome/browser/preloading/prefetch/no_state_prefetch/chrome_no_state_prefetch_processor_impl_delegate.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/signin/identity_manager_factory.h"
#include "chrome/browser/ssl/security_state_tab_helper.h"
#include "chrome/browser/translate/translate_frame_binder.h"
#include "chrome/browser/ui/search_engines/search_engine_tab_helper.h"
#include "chrome/browser/ui/side_panel/companion/companion_utils.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/browser/ui/web_applications/draggable_region_host_impl.h"
#include "chrome/browser/ui/webui/browsing_topics/browsing_topics_internals_ui.h"
#include "chrome/browser/ui/webui/engagement/site_engagement_ui.h"
#include "chrome/browser/ui/webui/internals/internals_ui.h"
#include "chrome/browser/ui/webui/location_internals/location_internals.mojom.h"
#include "chrome/browser/ui/webui/location_internals/location_internals_ui.h"
#include "chrome/browser/ui/webui/media/media_engagement_ui.h"
#include "chrome/browser/ui/webui/omnibox/omnibox.mojom.h"
#include "chrome/browser/ui/webui/omnibox/omnibox_ui.h"
#include "chrome/browser/ui/webui/privacy_sandbox/privacy_sandbox_internals_ui.h"
#include "chrome/browser/ui/webui/segmentation_internals/segmentation_internals_ui.h"
#include "chrome/browser/ui/webui/suggest_internals/suggest_internals.mojom.h"
#include "chrome/browser/ui/webui/suggest_internals/suggest_internals_ui.h"
#include "chrome/browser/ui/webui/usb_internals/usb_internals.mojom.h"
#include "chrome/browser/ui/webui/usb_internals/usb_internals_ui.h"
#include "chrome/browser/web_applications/web_app_utils.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/services/speech/buildflags/buildflags.h"
#include "components/browsing_topics/mojom/browsing_topics_internals.mojom.h"
#include "components/commerce/content/browser/commerce_internals_ui.h"
#include "components/commerce/core/internals/mojom/commerce_internals.mojom.h"
#include "components/compose/buildflags.h"
#include "components/dom_distiller/content/browser/distillability_driver.h"
#include "components/dom_distiller/content/browser/distiller_javascript_service_impl.h"
#include "components/dom_distiller/content/common/mojom/distillability_service.mojom.h"
#include "components/dom_distiller/content/common/mojom/distiller_javascript_service.mojom.h"
#include "components/dom_distiller/core/dom_distiller_service.h"
#include "components/feed/buildflags.h"
#include "components/feed/feed_feature_list.h"
#include "components/history_clusters/core/features.h"
#include "components/history_clusters/core/history_clusters_service.h"
#include "components/history_clusters/history_clusters_internals/webui/history_clusters_internals_ui.h"
#include "components/live_caption/caption_util.h"
#include "components/live_caption/pref_names.h"
#include "components/no_state_prefetch/browser/no_state_prefetch_contents.h"
#include "components/no_state_prefetch/browser/no_state_prefetch_processor_impl.h"
#include "components/performance_manager/embedder/binders.h"
#include "components/performance_manager/public/features.h"
#include "components/performance_manager/public/performance_manager.h"
#include "components/prefs/pref_service.h"
#include "components/privacy_sandbox/privacy_sandbox_features.h"
#include "components/reading_list/features/reading_list_switches.h"
#include "components/safe_browsing/buildflags.h"
#include "components/search_engines/search_engine_choice_utils.h"
#include "components/security_state/content/content_utils.h"
#include "components/security_state/core/security_state.h"
#include "components/services/screen_ai/buildflags/buildflags.h"
#include "components/signin/public/identity_manager/identity_manager.h"
#include "components/site_engagement/core/mojom/site_engagement_details.mojom.h"
#include "components/translate/content/common/translate.mojom.h"
#include "components/user_notes/user_notes_features.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/web_ui_browser_interface_broker_registry.h"
#include "content/public/browser/web_ui_controller_interface_binder.h"
#include "content/public/common/content_features.h"
#include "content/public/common/url_constants.h"
#include "extensions/buildflags/buildflags.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"
#include "services/image_annotation/public/mojom/image_annotation.mojom.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/mojom/credentialmanagement/credential_manager.mojom.h"
#include "third_party/blink/public/mojom/lcp_critical_path_predictor/lcp_critical_path_predictor.mojom.h"
#include "third_party/blink/public/mojom/loader/navigation_predictor.mojom.h"
#include "third_party/blink/public/mojom/payments/payment_credential.mojom.h"
#include "third_party/blink/public/mojom/payments/payment_request.mojom.h"
#include "third_party/blink/public/mojom/prerender/prerender.mojom.h"
#include "third_party/blink/public/public_buildflags.h"
#include "ui/accessibility/accessibility_features.h"

#if BUILDFLAG(ENABLE_SCREEN_AI_SERVICE)
#include "chrome/browser/screen_ai/screen_ai_service_router.h"
#include "chrome/browser/screen_ai/screen_ai_service_router_factory.h"
#endif

#if BUILDFLAG(ENABLE_UNHANDLED_TAP)
#include "chrome/browser/android/contextualsearch/unhandled_tap_notifier_impl.h"
#include "chrome/browser/android/contextualsearch/unhandled_tap_web_contents_observer.h"
#include "third_party/blink/public/mojom/unhandled_tap_notifier/unhandled_tap_notifier.mojom.h"
#endif  // BUILDFLAG(ENABLE_UNHANDLED_TAP)

#if BUILDFLAG(FULL_SAFE_BROWSING)
#include "chrome/browser/ui/webui/reset_password/reset_password.mojom.h"
#include "chrome/browser/ui/webui/reset_password/reset_password_ui.h"
#endif  // BUILDFLAG(FULL_SAFE_BROWSING)

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS_ASH)
#include "chrome/browser/ui/webui/connectors_internals/connectors_internals.mojom.h"
#include "chrome/browser/ui/webui/connectors_internals/connectors_internals_ui.h"
#endif

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_FUCHSIA)
#include "chrome/browser/ui/webui/app_settings/web_app_settings_ui.h"
#include "ui/webui/resources/cr_components/app_management/app_management.mojom.h"
#endif

#if BUILDFLAG(IS_ANDROID)
#include "chrome/browser/android/dom_distiller/distiller_ui_handle_android.h"
#include "chrome/browser/offline_pages/android/offline_page_auto_fetcher.h"
#include "chrome/browser/ui/webui/feed_internals/feed_internals.mojom.h"
#include "chrome/browser/ui/webui/feed_internals/feed_internals_ui.h"
#include "chrome/common/offline_page_auto_fetcher.mojom.h"
#include "components/commerce/core/commerce_feature_list.h"
#include "services/service_manager/public/cpp/interface_provider.h"
#include "third_party/blink/public/mojom/digital_goods/digital_goods.mojom.h"
#include "third_party/blink/public/mojom/installedapp/installed_app_provider.mojom.h"
#else
#include "chrome/browser/badging/badge_manager.h"
#include "chrome/browser/cart/chrome_cart.mojom.h"
#include "chrome/browser/new_tab_page/modules/drive/drive.mojom.h"
#include "chrome/browser/new_tab_page/modules/feed/feed.mojom.h"
#include "chrome/browser/new_tab_page/modules/history_clusters/history_clusters.mojom.h"
#include "chrome/browser/new_tab_page/modules/photos/photos.mojom.h"
#include "chrome/browser/new_tab_page/modules/recipes/recipes.mojom.h"
#include "chrome/browser/new_tab_page/modules/v2/history_clusters/history_clusters_v2.mojom.h"
#include "chrome/browser/new_tab_page/modules/v2/tab_resumption/tab_resumption.mojom.h"
#include "chrome/browser/new_tab_page/new_tab_page_util.h"
#include "chrome/browser/payments/payment_request_factory.h"
#include "chrome/browser/ui/webui/access_code_cast/access_code_cast.mojom.h"
#include "chrome/browser/ui/webui/access_code_cast/access_code_cast_ui.h"
#include "chrome/browser/ui/webui/app_service_internals/app_service_internals.mojom.h"
#include "chrome/browser/ui/webui/app_service_internals/app_service_internals_ui.h"
#include "chrome/browser/ui/webui/downloads/downloads.mojom.h"
#include "chrome/browser/ui/webui/downloads/downloads_ui.h"
#include "chrome/browser/ui/webui/feed/feed.mojom.h"
#include "chrome/browser/ui/webui/feed/feed_ui.h"
#include "chrome/browser/ui/webui/on_device_internals/on_device_internals_ui.h"
#include "chrome/browser/ui/webui/web_app_internals/web_app_internals.mojom.h"
#include "chrome/browser/ui/webui/web_app_internals/web_app_internals_ui.h"
#include "components/omnibox/browser/omnibox.mojom.h"
#if !defined(OFFICIAL_BUILD)
#include "chrome/browser/ui/webui/new_tab_page/foo/foo.mojom.h"  // nogncheck crbug.com/1125897
#endif
#include "chrome/browser/ui/side_panel/customize_chrome/customize_chrome_utils.h"
#include "chrome/browser/ui/webui/commerce/shopping_insights_side_panel_ui.h"
#include "chrome/browser/ui/webui/hats/hats_ui.h"
#include "chrome/browser/ui/webui/history/history_ui.h"
#include "chrome/browser/ui/webui/internals/user_education/user_education_internals.mojom.h"
#include "chrome/browser/ui/webui/new_tab_page/new_tab_page.mojom.h"
#include "chrome/browser/ui/webui/new_tab_page/new_tab_page_ui.h"
#include "chrome/browser/ui/webui/new_tab_page_third_party/new_tab_page_third_party_ui.h"
#include "chrome/browser/ui/webui/omnibox_popup/omnibox_popup_ui.h"
#include "chrome/browser/ui/webui/password_manager/password_manager_ui.h"
#include "chrome/browser/ui/webui/search_engine_choice/search_engine_choice.mojom.h"  // nogncheck crbug.com/1125897
#include "chrome/browser/ui/webui/search_engine_choice/search_engine_choice_ui.h"
#include "chrome/browser/ui/webui/settings/settings_ui.h"
#include "chrome/browser/ui/webui/side_panel/bookmarks/bookmarks_side_panel_ui.h"
#include "chrome/browser/ui/webui/side_panel/companion/companion_side_panel_untrusted_ui.h"
#include "chrome/browser/ui/webui/side_panel/customize_chrome/customize_chrome.mojom.h"
#include "chrome/browser/ui/webui/side_panel/customize_chrome/customize_chrome_ui.h"
#include "chrome/browser/ui/webui/side_panel/customize_chrome/wallpaper_search/wallpaper_search.mojom.h"
#include "chrome/browser/ui/webui/side_panel/history_clusters/history_clusters_side_panel_ui.h"
#include "chrome/browser/ui/webui/side_panel/performance_controls/performance_side_panel_ui.h"
#include "chrome/browser/ui/webui/side_panel/read_anything/read_anything_untrusted_ui.h"
#include "chrome/browser/ui/webui/side_panel/reading_list/reading_list.mojom.h"
#include "chrome/browser/ui/webui/side_panel/reading_list/reading_list_ui.h"
#include "chrome/browser/ui/webui/side_panel/user_notes/user_notes.mojom.h"
#include "chrome/browser/ui/webui/side_panel/user_notes/user_notes_side_panel_ui.h"
#include "chrome/browser/ui/webui/tab_search/tab_search.mojom.h"
#include "chrome/browser/ui/webui/tab_search/tab_search_ui.h"
#include "chrome/browser/ui/webui/webui_gallery/webui_gallery_ui.h"
#include "chrome/browser/ui/webui/whats_new/whats_new_ui.h"
#include "chrome/common/webui_url_constants.h"
#include "components/commerce/core/mojom/shopping_list.mojom.h"  // nogncheck crbug.com/1125897
#include "components/optimization_guide/core/optimization_guide_features.h"
#include "components/page_image_service/mojom/page_image_service.mojom.h"
#include "components/search/ntp_features.h"
#include "ui/webui/resources/cr_components/color_change_listener/color_change_listener.mojom.h"
#include "ui/webui/resources/cr_components/customize_color_scheme_mode/customize_color_scheme_mode.mojom.h"
#include "ui/webui/resources/cr_components/customize_themes/customize_themes.mojom.h"
#include "ui/webui/resources/cr_components/help_bubble/help_bubble.mojom.h"
#include "ui/webui/resources/cr_components/history_clusters/history_clusters.mojom.h"
#include "ui/webui/resources/cr_components/most_visited/most_visited.mojom.h"
#include "ui/webui/resources/cr_components/theme_color_picker/theme_color_picker.mojom.h"
#include "ui/webui/resources/js/browser_command/browser_command.mojom.h"
#include "ui/webui/resources/js/metrics_reporter/metrics_reporter.mojom.h"
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS)
#include "chrome/browser/companion/visual_query/visual_query_suggestions_service_factory.h"
#include "chrome/browser/ui/web_applications/sub_apps_service_impl.h"
#include "chrome/browser/ui/webui/discards/discards.mojom.h"
#include "chrome/browser/ui/webui/discards/discards_ui.h"
#include "chrome/browser/ui/webui/discards/site_data.mojom.h"
#include "chrome/common/companion/visual_query.mojom.h"
#include "chrome/common/companion/visual_query/features.h"
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) ||
        // BUILDFLAG(IS_CHROMEOS)

#if !BUILDFLAG(IS_CHROMEOS) && !BUILDFLAG(IS_ANDROID)
#include "chrome/browser/ui/webui/app_home/app_home.mojom.h"
#include "chrome/browser/ui/webui/app_home/app_home_ui.h"
#endif  // !BUILDFLAG(IS_CHROMEOS) && !BUILDFLAG(IS_ANDROID)

#if !BUILDFLAG(IS_CHROMEOS_ASH) && !BUILDFLAG(IS_ANDROID)
#include "chrome/browser/ui/webui/signin/profile_customization_ui.h"
#include "chrome/browser/ui/webui/signin/profile_picker_ui.h"
#include "ui/webui/resources/cr_components/customize_themes/customize_themes.mojom.h"
#endif  // !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_CHROMEOS_ASH)

#if BUILDFLAG(IS_CHROMEOS_ASH)
#include "ash/constants/ash_features.h"
#include "ash/webui/camera_app_ui/camera_app_helper.mojom.h"
#include "ash/webui/camera_app_ui/camera_app_ui.h"
#include "ash/webui/color_internals/color_internals_ui.h"
#include "ash/webui/color_internals/mojom/color_internals.mojom.h"
#include "ash/webui/common/mojom/accessibility_features.mojom.h"
#include "ash/webui/common/mojom/sea_pen.mojom.h"
#include "ash/webui/common/mojom/shortcut_input_provider.mojom.h"
#include "ash/webui/connectivity_diagnostics/connectivity_diagnostics_ui.h"
#include "ash/webui/demo_mode_app_ui/demo_mode_app_untrusted_ui.h"
#include "ash/webui/diagnostics_ui/diagnostics_ui.h"
#include "ash/webui/diagnostics_ui/mojom/input_data_provider.mojom.h"
#include "ash/webui/diagnostics_ui/mojom/network_health_provider.mojom.h"
#include "ash/webui/diagnostics_ui/mojom/system_data_provider.mojom.h"
#include "ash/webui/diagnostics_ui/mojom/system_routine_controller.mojom.h"
#include "ash/webui/eche_app_ui/eche_app_ui.h"
#include "ash/webui/eche_app_ui/mojom/eche_app.mojom.h"
#include "ash/webui/file_manager/file_manager_ui.h"
#include "ash/webui/file_manager/mojom/file_manager.mojom.h"
#include "ash/webui/files_internals/files_internals_ui.h"
#include "ash/webui/files_internals/mojom/files_internals.mojom.h"
#include "ash/webui/firmware_update_ui/firmware_update_app_ui.h"
#include "ash/webui/firmware_update_ui/mojom/firmware_update.mojom.h"
#include "ash/webui/help_app_ui/help_app_ui.h"
#include "ash/webui/help_app_ui/help_app_ui.mojom.h"
#include "ash/webui/help_app_ui/help_app_untrusted_ui.h"
#include "ash/webui/help_app_ui/search/search.mojom.h"
#include "ash/webui/media_app_ui/media_app_guest_ui.h"
#include "ash/webui/media_app_ui/media_app_ui.h"
#include "ash/webui/media_app_ui/media_app_ui.mojom.h"
#include "ash/webui/media_app_ui/media_app_ui_untrusted.mojom.h"
#include "ash/webui/multidevice_debug/proximity_auth_ui.h"
#include "ash/webui/os_feedback_ui/mojom/os_feedback_ui.mojom.h"
#include "ash/webui/os_feedback_ui/os_feedback_ui.h"
#include "ash/webui/os_feedback_ui/os_feedback_untrusted_ui.h"
#include "ash/webui/personalization_app/mojom/personalization_app.mojom.h"
#include "ash/webui/personalization_app/personalization_app_ui.h"
#include "ash/webui/personalization_app/search/search.mojom.h"
#include "ash/webui/print_management/print_management_ui.h"
#include "ash/webui/projector_app/mojom/untrusted_annotator.mojom.h"
#include "ash/webui/projector_app/mojom/untrusted_projector.mojom.h"
#include "ash/webui/projector_app/untrusted_projector_annotator_ui.h"
#include "ash/webui/projector_app/untrusted_projector_ui.h"
#include "ash/webui/scanning/mojom/scanning.mojom.h"
#include "ash/webui/scanning/scanning_ui.h"
#include "ash/webui/shimless_rma/shimless_rma.h"
#include "ash/webui/shortcut_customization_ui/backend/search/search.mojom.h"
#include "ash/webui/shortcut_customization_ui/mojom/shortcut_customization.mojom.h"
#include "ash/webui/shortcut_customization_ui/shortcut_customization_app_ui.h"
#include "ash/webui/vc_background_ui/vc_background_ui.h"
#include "chrome/browser/apps/digital_goods/digital_goods_factory_impl.h"
#include "chrome/browser/chromeos/upload_office_to_cloud/upload_office_to_cloud.h"
#include "chrome/browser/nearby_sharing/common/nearby_share_features.h"
#include "chrome/browser/speech/cros_speech_recognition_service_factory.h"
#include "chrome/browser/ui/webui/ash/add_supervision/add_supervision.mojom.h"
#include "chrome/browser/ui/webui/ash/add_supervision/add_supervision_ui.h"
#include "chrome/browser/ui/webui/ash/app_install/app_install.mojom.h"
#include "chrome/browser/ui/webui/ash/app_install/app_install_ui.h"
#include "chrome/browser/ui/webui/ash/audio/audio.mojom.h"
#include "chrome/browser/ui/webui/ash/audio/audio_ui.h"
#include "chrome/browser/ui/webui/ash/bluetooth_pairing_dialog.h"
#include "chrome/browser/ui/webui/ash/borealis_installer/borealis_installer.mojom.h"
#include "chrome/browser/ui/webui/ash/borealis_installer/borealis_installer_ui.h"
#include "chrome/browser/ui/webui/ash/cloud_upload/cloud_upload.mojom.h"
#include "chrome/browser/ui/webui/ash/cloud_upload/cloud_upload_dialog.h"
#include "chrome/browser/ui/webui/ash/cloud_upload/cloud_upload_ui.h"
#include "chrome/browser/ui/webui/ash/crostini_installer/crostini_installer.mojom.h"
#include "chrome/browser/ui/webui/ash/crostini_installer/crostini_installer_ui.h"
#include "chrome/browser/ui/webui/ash/crostini_upgrader/crostini_upgrader.mojom.h"
#include "chrome/browser/ui/webui/ash/crostini_upgrader/crostini_upgrader_ui.h"
#include "chrome/browser/ui/webui/ash/emoji/emoji_picker.mojom.h"
#include "chrome/browser/ui/webui/ash/emoji/emoji_ui.h"
#include "chrome/browser/ui/webui/ash/emoji/new_window_proxy.mojom.h"
#include "chrome/browser/ui/webui/ash/enterprise_reporting/enterprise_reporting.mojom.h"
#include "chrome/browser/ui/webui/ash/enterprise_reporting/enterprise_reporting_ui.h"
#include "chrome/browser/ui/webui/ash/internet_config_dialog.h"
#include "chrome/browser/ui/webui/ash/internet_detail_dialog.h"
#include "chrome/browser/ui/webui/ash/launcher_internals/launcher_internals.mojom.h"
#include "chrome/browser/ui/webui/ash/launcher_internals/launcher_internals_ui.h"
#include "chrome/browser/ui/webui/ash/lock_screen_reauth/lock_screen_network_ui.h"
#include "chrome/browser/ui/webui/ash/login/oobe_ui.h"
#include "chrome/browser/ui/webui/ash/mako/mako_ui.h"
#include "chrome/browser/ui/webui/ash/manage_mirrorsync/manage_mirrorsync.mojom.h"
#include "chrome/browser/ui/webui/ash/manage_mirrorsync/manage_mirrorsync_ui.h"
#include "chrome/browser/ui/webui/ash/multidevice_setup/multidevice_setup_dialog.h"
#include "chrome/browser/ui/webui/ash/network_ui.h"
#include "chrome/browser/ui/webui/ash/office_fallback/office_fallback.mojom.h"
#include "chrome/browser/ui/webui/ash/office_fallback/office_fallback_ui.h"
#include "chrome/browser/ui/webui/ash/parent_access/parent_access_ui.h"
#include "chrome/browser/ui/webui/ash/parent_access/parent_access_ui.mojom.h"
#include "chrome/browser/ui/webui/ash/remote_maintenance_curtain_ui.h"
#include "chrome/browser/ui/webui/ash/sensor_info/sensor.mojom.h"
#include "chrome/browser/ui/webui/ash/sensor_info/sensor_info_ui.h"
#include "chrome/browser/ui/webui/ash/set_time_ui.h"
#include "chrome/browser/ui/webui/ash/settings/os_settings_ui.h"
#include "chrome/browser/ui/webui/ash/settings/pages/apps/mojom/app_notification_handler.mojom.h"
#include "chrome/browser/ui/webui/ash/settings/pages/device/display_settings/display_settings_provider.mojom.h"
#include "chrome/browser/ui/webui/ash/settings/pages/device/input_device_settings/input_device_settings_provider.mojom.h"
#include "chrome/browser/ui/webui/ash/settings/pages/files/mojom/google_drive_handler.mojom.h"
#include "chrome/browser/ui/webui/ash/settings/pages/files/mojom/one_drive_handler.mojom.h"
#include "chrome/browser/ui/webui/ash/settings/pages/privacy/mojom/app_permission_handler.mojom.h"
#include "chrome/browser/ui/webui/ash/settings/search/mojom/search.mojom.h"
#include "chrome/browser/ui/webui/ash/settings/search/mojom/user_action_recorder.mojom.h"
#include "chrome/browser/ui/webui/ash/smb_shares/smb_credentials_dialog.h"
#include "chrome/browser/ui/webui/ash/smb_shares/smb_share_dialog.h"
#include "chrome/browser/ui/webui/ash/vm/vm.mojom.h"
#include "chrome/browser/ui/webui/ash/vm/vm_ui.h"
#include "chrome/browser/ui/webui/feedback/feedback_ui.h"
#include "chrome/browser/ui/webui/nearby_share/nearby_share.mojom.h"
#include "chrome/browser/ui/webui/nearby_share/nearby_share_dialog_ui.h"
#include "chromeos/ash/components/audio/public/mojom/cros_audio_config.mojom.h"
#include "chromeos/ash/components/local_search_service/public/mojom/index.mojom.h"
#include "chromeos/ash/services/auth_factor_config/public/mojom/auth_factor_config.mojom.h"
#include "chromeos/ash/services/bluetooth_config/public/mojom/cros_bluetooth_config.mojom.h"
#include "chromeos/ash/services/cellular_setup/public/mojom/cellular_setup.mojom.h"
#include "chromeos/ash/services/cellular_setup/public/mojom/esim_manager.mojom.h"
#include "chromeos/ash/services/connectivity/public/mojom/passpoint.mojom.h"
#include "chromeos/ash/services/hotspot_config/public/mojom/cros_hotspot_config.mojom.h"
#include "chromeos/ash/services/multidevice_setup/multidevice_setup_service.h"
#include "chromeos/ash/services/multidevice_setup/public/mojom/multidevice_setup.mojom.h"
#include "chromeos/ash/services/nearby/public/mojom/nearby_share_settings.mojom.h"  // nogncheck crbug.com/1125897
#include "chromeos/ash/services/orca/public/mojom/orca_service.mojom.h"
#include "chromeos/components/print_management/mojom/printing_manager.mojom.h"  // nogncheck
#include "chromeos/constants/chromeos_features.h"
#include "chromeos/services/network_config/public/mojom/cros_network_config.mojom.h"  // nogncheck
#include "chromeos/services/network_health/public/mojom/network_diagnostics.mojom.h"  // nogncheck
#include "chromeos/services/network_health/public/mojom/network_health.mojom.h"  // nogncheck
#include "media/capture/video/chromeos/mojom/camera_app.mojom.h"
#include "third_party/blink/public/mojom/digital_goods/digital_goods.mojom.h"
#include "ui/webui/resources/cr_components/app_management/app_management.mojom.h"
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

#if BUILDFLAG(IS_CHROMEOS_LACROS)
#include "chrome/browser/apps/digital_goods/digital_goods_factory_stub.h"
#include "chrome/browser/apps/digital_goods/digital_goods_lacros.h"
#include "chrome/browser/chromeos/cros_apps/api/cros_apps_api_frame_context.h"
#include "chrome/browser/chromeos/cros_apps/api/cros_apps_api_registry.h"
#include "chrome/browser/lacros/cros_apps/api/diagnostics/cros_diagnostics_impl.h"
#include "chromeos/constants/chromeos_features.h"
#include "chromeos/lacros/lacros_service.h"
#include "third_party/blink/public/mojom/chromeos/diagnostics/cros_diagnostics.mojom.h"
#else
#include "chrome/browser/ui/webui/bluetooth_internals/bluetooth_internals.mojom.h"  // nogncheck
#include "chrome/browser/ui/webui/bluetooth_internals/bluetooth_internals_ui.h"  // nogncheck
#endif  // BUILDFLAG(IS_CHROMEOS_LACROS)

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_MAC) || \
    BUILDFLAG(IS_ANDROID)
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_MAC)
#include "chrome/browser/webshare/share_service_impl.h"
#endif
#include "third_party/blink/public/mojom/webshare/webshare.mojom.h"
#endif

#if BUILDFLAG(IS_CHROMEOS_ASH) && !defined(OFFICIAL_BUILD)
#include "ash/webui/sample_system_web_app_ui/mojom/sample_system_web_app_ui.mojom.h"
#include "ash/webui/sample_system_web_app_ui/sample_system_web_app_ui.h"
#include "ash/webui/sample_system_web_app_ui/sample_system_web_app_untrusted_ui.h"
#include "ash/webui/status_area_internals/mojom/status_area_internals.mojom.h"
#include "ash/webui/status_area_internals/status_area_internals_ui.h"
#endif

#if BUILDFLAG(ENABLE_SPEECH_SERVICE)
#include "chrome/browser/accessibility/live_caption/live_caption_speech_recognition_host.h"
#include "chrome/browser/accessibility/live_caption/live_caption_unavailability_notifier.h"
#include "chrome/browser/speech/speech_recognition_client_browser_interface.h"
#include "chrome/browser/speech/speech_recognition_client_browser_interface_factory.h"
#include "chrome/browser/speech/speech_recognition_service.h"
#include "media/mojo/mojom/renderer_extensions.mojom.h"
#include "media/mojo/mojom/speech_recognition.mojom.h"  // nogncheck
#if BUILDFLAG(IS_CHROMEOS_LACROS)
#include "chrome/browser/accessibility/live_caption/live_caption_surface.h"
#include "chromeos/crosapi/mojom/speech_recognition.mojom.h"
#endif  // BUILDFLAG(IS_CHROMEOS_LACROS)
#endif  // BUILDFLAG(ENABLE_SPEECH_SERVICE)

#if BUILDFLAG(IS_WIN)
#include "chrome/browser/media/media_foundation_service_monitor.h"
#include "media/mojo/mojom/media_foundation_preferences.mojom.h"
#include "media/mojo/services/media_foundation_preferences.h"
#endif  // BUILDFLAG(IS_WIN)

#if BUILDFLAG(ENABLE_BROWSER_SPEECH_SERVICE)
#include "chrome/browser/speech/speech_recognition_service_factory.h"
#include "media/mojo/mojom/speech_recognition_service.mojom.h"
#endif  // BUILDFLAG(ENABLE_BROWSER_SPEECH_SERVICE)

#if BUILDFLAG(ENABLE_EXTENSIONS)
#include "extensions/browser/api/mime_handler_private/mime_handler_private.h"
#include "extensions/browser/guest_view/mime_handler_view/mime_handler_view_guest.h"
#include "extensions/common/api/mime_handler.mojom.h"  // nogncheck
#endif

#if BUILDFLAG(ENABLE_WEBUI_TAB_STRIP)
#include "chrome/browser/ui/webui/tab_strip/tab_strip.mojom.h"
#include "chrome/browser/ui/webui/tab_strip/tab_strip_ui.h"
#endif

#if BUILDFLAG(ENABLE_COMPOSE)
#include "chrome/browser/compose/compose_enabling.h"
#include "chrome/browser/ui/webui/compose/compose_ui.h"
#include "chrome/common/compose/compose.mojom.h"
#include "components/compose/core/browser/compose_features.h"  // nogncheck crbug.com/1125897
#endif

#if BUILDFLAG(ENABLE_PRINT_PREVIEW)
#include "chrome/browser/printing/web_api/web_printing_service_binder.h"
#include "third_party/blink/public/mojom/printing/web_printing.mojom.h"
#endif

#if BUILDFLAG(IS_CHROMEOS)
#include "chrome/browser/ui/webui/dlp_internals/dlp_internals.mojom.h"
#include "chrome/browser/ui/webui/dlp_internals/dlp_internals_ui.h"
#endif

namespace chrome::internal {

using content::RegisterWebUIControllerInterfaceBinder;

#if BUILDFLAG(ENABLE_UNHANDLED_TAP)
void BindUnhandledTapWebContentsObserver(
    content::RenderFrameHost* const host,
    mojo::PendingReceiver<blink::mojom::UnhandledTapNotifier> receiver) {
  auto* web_contents = content::WebContents::FromRenderFrameHost(host);
  if (!web_contents) {
    return;
  }

  auto* unhandled_tap_notifier_observer =
      contextual_search::UnhandledTapWebContentsObserver::FromWebContents(
          web_contents);
  if (!unhandled_tap_notifier_observer) {
    return;
  }

  contextual_search::CreateUnhandledTapNotifierImpl(
      unhandled_tap_notifier_observer->unhandled_tap_callback(),
      std::move(receiver));
}
#endif  // BUILDFLAG(ENABLE_UNHANDLED_TAP)

// Forward image Annotator requests to the profile's AccessibilityLabelsService.
void BindImageAnnotator(
    content::RenderFrameHost* frame_host,
    mojo::PendingReceiver<image_annotation::mojom::Annotator> receiver) {
  AccessibilityLabelsServiceFactory::GetForProfile(
      Profile::FromBrowserContext(
          frame_host->GetProcess()->GetBrowserContext()))
      ->BindImageAnnotator(std::move(receiver));
}

void BindCommerceHintObserver(
    content::RenderFrameHost* const frame_host,
    mojo::PendingReceiver<cart::mojom::CommerceHintObserver> receiver) {
  // This is specifically restricting this to main frames, whether they are the
  // main frame of the tab or a <portal> element, while preventing this from
  // working in subframes and fenced frames.
  if (frame_host->GetParent() || frame_host->IsFencedFrameRoot()) {
    mojo::ReportBadMessage(
        "Unexpected the message from subframe or fenced frame.");
    return;
  }

// Check if features require CommerceHint are enabled.
#if !BUILDFLAG(IS_ANDROID)
  if (!IsCartModuleEnabled()) {
    return;
  }
#else
  if (!base::FeatureList::IsEnabled(commerce::kCommerceHintAndroid)) {
    return;
  }
#endif

// On Android, commerce hint observer is enabled for all users with the feature
// enabled since the observer is only used for collecting metrics for now, and
// we want to maximize the user population exposed; on Desktop, ChromeCart is
// not available for non-signin single-profile users and therefore neither does
// commerce hint observer.
#if !BUILDFLAG(IS_ANDROID)
  Profile* profile = Profile::FromBrowserContext(
      frame_host->GetProcess()->GetBrowserContext());
  auto* identity_manager = IdentityManagerFactory::GetForProfile(profile);
  ProfileManager* profile_manager = g_browser_process->profile_manager();
  if (!identity_manager || !profile_manager) {
    return;
  }
  if (!identity_manager->HasPrimaryAccount(signin::ConsentLevel::kSignin) &&
      profile_manager->GetNumberOfProfiles() <= 1) {
    return;
  }
#endif
  auto* web_contents = content::WebContents::FromRenderFrameHost(frame_host);
  if (!web_contents) {
    return;
  }
  content::BrowserContext* browser_context = web_contents->GetBrowserContext();
  if (!browser_context) {
    return;
  }
  if (browser_context->IsOffTheRecord()) {
    return;
  }

  cart::CommerceHintService::CreateForWebContents(web_contents);
  cart::CommerceHintService* service =
      cart::CommerceHintService::FromWebContents(web_contents);
  if (!service) {
    return;
  }
  service->BindCommerceHintObserver(frame_host, std::move(receiver));
}

void BindDistillabilityService(
    content::RenderFrameHost* const frame_host,
    mojo::PendingReceiver<dom_distiller::mojom::DistillabilityService>
        receiver) {
  auto* web_contents = content::WebContents::FromRenderFrameHost(frame_host);
  if (!web_contents) {
    return;
  }

  dom_distiller::DistillabilityDriver* driver =
      dom_distiller::DistillabilityDriver::FromWebContents(web_contents);
  if (!driver) {
    return;
  }
  driver->SetIsSecureCallback(
      base::BindRepeating([](content::WebContents* contents) {
        // SecurityStateTabHelper uses chrome-specific
        // GetVisibleSecurityState to determine if a page is SECURE.
        return SecurityStateTabHelper::FromWebContents(contents)
                   ->GetSecurityLevel() ==
               security_state::SecurityLevel::SECURE;
      }));
  driver->CreateDistillabilityService(std::move(receiver));
}

void BindDistillerJavaScriptService(
    content::RenderFrameHost* const frame_host,
    mojo::PendingReceiver<dom_distiller::mojom::DistillerJavaScriptService>
        receiver) {
  auto* web_contents = content::WebContents::FromRenderFrameHost(frame_host);
  if (!web_contents) {
    return;
  }

  dom_distiller::DomDistillerService* dom_distiller_service =
      dom_distiller::DomDistillerServiceFactory::GetForBrowserContext(
          web_contents->GetBrowserContext());
#if BUILDFLAG(IS_ANDROID)
  static_cast<dom_distiller::android::DistillerUIHandleAndroid*>(
      dom_distiller_service->GetDistillerUIHandle())
      ->set_render_frame_host(frame_host);
#endif
  CreateDistillerJavaScriptService(dom_distiller_service->GetWeakPtr(),
                                   std::move(receiver));
}

void BindPrerenderCanceler(
    content::RenderFrameHost* frame_host,
    mojo::PendingReceiver<prerender::mojom::PrerenderCanceler> receiver) {
  auto* web_contents = content::WebContents::FromRenderFrameHost(frame_host);
  if (!web_contents) {
    return;
  }

  auto* no_state_prefetch_contents =
      prerender::ChromeNoStatePrefetchContentsDelegate::FromWebContents(
          web_contents);
  if (!no_state_prefetch_contents) {
    return;
  }
  no_state_prefetch_contents->AddPrerenderCancelerReceiver(std::move(receiver));
}

void BindNoStatePrefetchProcessor(
    content::RenderFrameHost* frame_host,
    mojo::PendingReceiver<blink::mojom::NoStatePrefetchProcessor> receiver) {
  prerender::NoStatePrefetchProcessorImpl::Create(
      frame_host, std::move(receiver),
      std::make_unique<
          prerender::ChromeNoStatePrefetchProcessorImplDelegate>());
}

#if BUILDFLAG(IS_ANDROID)
template <typename Interface>
void ForwardToJavaWebContents(content::RenderFrameHost* frame_host,
                              mojo::PendingReceiver<Interface> receiver) {
  content::WebContents* contents =
      content::WebContents::FromRenderFrameHost(frame_host);
  if (contents) {
    contents->GetJavaInterfaces()->GetInterface(std::move(receiver));
  }
}

template <typename Interface>
void ForwardToJavaFrame(content::RenderFrameHost* render_frame_host,
                        mojo::PendingReceiver<Interface> receiver) {
  render_frame_host->GetJavaInterfaces()->GetInterface(std::move(receiver));
}
#endif

#if BUILDFLAG(ENABLE_EXTENSIONS)
void BindMimeHandlerService(
    content::RenderFrameHost* frame_host,
    mojo::PendingReceiver<extensions::mime_handler::MimeHandlerService>
        receiver) {
  auto* guest_view =
      extensions::MimeHandlerViewGuest::FromRenderFrameHost(frame_host);
  if (!guest_view) {
    return;
  }
  extensions::MimeHandlerServiceImpl::Create(guest_view->GetStreamWeakPtr(),
                                             std::move(receiver));
}

void BindBeforeUnloadControl(
    content::RenderFrameHost* frame_host,
    mojo::PendingReceiver<extensions::mime_handler::BeforeUnloadControl>
        receiver) {
  auto* guest_view =
      extensions::MimeHandlerViewGuest::FromRenderFrameHost(frame_host);
  if (!guest_view) {
    return;
  }
  guest_view->FuseBeforeUnloadControl(std::move(receiver));
}
#endif

void BindNetworkHintsHandler(
    content::RenderFrameHost* frame_host,
    mojo::PendingReceiver<network_hints::mojom::NetworkHintsHandler> receiver) {
  predictors::NetworkHintsHandlerImpl::Create(frame_host, std::move(receiver));
}

#if BUILDFLAG(ENABLE_SPEECH_SERVICE)
void BindSpeechRecognitionContextHandler(
    content::RenderFrameHost* frame_host,
    mojo::PendingReceiver<media::mojom::SpeechRecognitionContext> receiver) {
  if (!captions::IsLiveCaptionFeatureSupported()) {
    return;
  }

#if BUILDFLAG(IS_CHROMEOS_LACROS)
  // On LaCrOS, forward to Ash.
  auto* service = chromeos::LacrosService::Get();
  if (service && service->IsAvailable<crosapi::mojom::SpeechRecognition>()) {
    service->GetRemote<crosapi::mojom::SpeechRecognition>()
        ->BindSpeechRecognitionContext(std::move(receiver));
  }
#else
  // On other platforms (Ash, desktop), bind via the appropriate factory.
  Profile* profile = Profile::FromBrowserContext(
      frame_host->GetProcess()->GetBrowserContext());
#if BUILDFLAG(ENABLE_BROWSER_SPEECH_SERVICE)
  auto* factory = SpeechRecognitionServiceFactory::GetForProfile(profile);
#elif BUILDFLAG(IS_CHROMEOS_ASH)
  auto* factory = CrosSpeechRecognitionServiceFactory::GetForProfile(profile);
#else
#error "No speech recognition service factory on this platform."
#endif
  factory->BindSpeechRecognitionContext(std::move(receiver));
#endif  // BUILDFLAG(IS_CHROMEOS_LACROS)
}

void BindSpeechRecognitionClientBrowserInterfaceHandler(
    content::RenderFrameHost* frame_host,
    mojo::PendingReceiver<media::mojom::SpeechRecognitionClientBrowserInterface>
        receiver) {
  if (captions::IsLiveCaptionFeatureSupported()) {
#if BUILDFLAG(IS_CHROMEOS_LACROS)
    // On LaCrOS, forward to Ash.
    auto* service = chromeos::LacrosService::Get();
    if (service && service->IsAvailable<crosapi::mojom::SpeechRecognition>()) {
      service->GetRemote<crosapi::mojom::SpeechRecognition>()
          ->BindSpeechRecognitionClientBrowserInterface(std::move(receiver));
    }
#else
    // On other platforms (Ash, desktop), bind in this process.
    Profile* profile = Profile::FromBrowserContext(
        frame_host->GetProcess()->GetBrowserContext());
    SpeechRecognitionClientBrowserInterfaceFactory::GetForProfile(profile)
        ->BindReceiver(std::move(receiver));
#endif  // BUILDFLAG(IS_CHROMEOS_LACROS)
  }
}

void BindSpeechRecognitionRecognizerClientHandler(
    content::RenderFrameHost* frame_host,
    mojo::PendingReceiver<media::mojom::SpeechRecognitionRecognizerClient>
        client_receiver) {
#if BUILDFLAG(IS_CHROMEOS_LACROS)
  // On LaCrOS, forward to Ash.

  // Hold a client-browser interface just long enough to bootstrap a remote
  // recognizer client.
  mojo::Remote<media::mojom::SpeechRecognitionClientBrowserInterface>
      interface_remote;
  auto* service = chromeos::LacrosService::Get();
  if (!service || !service->IsAvailable<crosapi::mojom::SpeechRecognition>()) {
    return;
  }
  service->GetRemote<crosapi::mojom::SpeechRecognition>()
      ->BindSpeechRecognitionClientBrowserInterface(
          interface_remote.BindNewPipeAndPassReceiver());

  // Grab the per-web-contents logic on our end to drive the remote client.
  auto* surface = captions::LiveCaptionSurface::GetOrCreateForWebContents(
      content::WebContents::FromRenderFrameHost(frame_host));
  mojo::PendingRemote<media::mojom::SpeechRecognitionSurface> surface_remote;
  mojo::PendingReceiver<media::mojom::SpeechRecognitionSurfaceClient>
      surface_client_receiver;
  surface->BindToSurfaceClient(
      surface_remote.InitWithNewPipeAndPassReceiver(),
      surface_client_receiver.InitWithNewPipeAndPassRemote());

  // Populate static info to send to the client.
  auto metadata = media::mojom::SpeechRecognitionSurfaceMetadata::New();
  metadata->session_id = surface->session_id();

  // Bootstrap the recognizer client.
  interface_remote->BindRecognizerToRemoteClient(
      std::move(client_receiver), std::move(surface_client_receiver),
      std::move(surface_remote), std::move(metadata));
#else
  Profile* profile = Profile::FromBrowserContext(
      frame_host->GetProcess()->GetBrowserContext());
  PrefService* profile_prefs = profile->GetPrefs();
  if (profile_prefs->GetBoolean(prefs::kLiveCaptionEnabled) &&
      captions::IsLiveCaptionFeatureSupported()) {
    captions::LiveCaptionSpeechRecognitionHost::Create(
        frame_host, std::move(client_receiver));
  }
#endif
}

#if BUILDFLAG(IS_WIN)
void BindMediaFoundationRendererNotifierHandler(
    content::RenderFrameHost* frame_host,
    mojo::PendingReceiver<media::mojom::MediaFoundationRendererNotifier>
        receiver) {
  if (captions::IsLiveCaptionFeatureSupported()) {
    captions::LiveCaptionUnavailabilityNotifier::Create(frame_host,
                                                        std::move(receiver));
  }
}
#endif  // BUILDFLAG(IS_WIN)
#endif  // BUILDFLAG(ENABLE_SPEECH_SERVICE)

#if BUILDFLAG(IS_WIN)
void BindMediaFoundationPreferences(
    content::RenderFrameHost* frame_host,
    mojo::PendingReceiver<media::mojom::MediaFoundationPreferences> receiver) {
  MediaFoundationPreferencesImpl::Create(
      frame_host->GetSiteInstance()->GetSiteURL(),
      base::BindRepeating(&MediaFoundationServiceMonitor::
                              IsHardwareSecureDecryptionAllowedForSite),
      std::move(receiver));
}
#endif  // BUILDFLAG(IS_WIN)

#if BUILDFLAG(ENABLE_SCREEN_AI_SERVICE)
void BindScreenAIAnnotator(
    content::RenderFrameHost* frame_host,
    mojo::PendingReceiver<screen_ai::mojom::ScreenAIAnnotator> receiver) {
  content::BrowserContext* browser_context =
      frame_host->GetProcess()->GetBrowserContext();

  screen_ai::ScreenAIServiceRouterFactory::GetForBrowserContext(browser_context)
      ->BindScreenAIAnnotator(std::move(receiver));
}

void BindScreen2xMainContentExtractor(
    content::RenderFrameHost* frame_host,
    mojo::PendingReceiver<screen_ai::mojom::Screen2xMainContentExtractor>
        receiver) {
  screen_ai::ScreenAIServiceRouterFactory::GetForBrowserContext(
      frame_host->GetProcess()->GetBrowserContext())
      ->BindMainContentExtractor(std::move(receiver));
}
#endif

#if BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || \
    BUILDFLAG(IS_WIN)
void BindVisualSuggestionsModelProvider(
    content::RenderFrameHost* frame_host,
    mojo::PendingReceiver<
        companion::visual_query::mojom::VisualSuggestionsModelProvider>
        receiver) {
  companion::visual_query::VisualQuerySuggestionsServiceFactory::GetForProfile(
      Profile::FromBrowserContext(
          frame_host->GetProcess()->GetBrowserContext()))
      ->BindModelReceiver(std::move(receiver));
}
#endif

#if BUILDFLAG(IS_CHROMEOS_LACROS)
// A helper class to register ChromeOS Apps API binders. This includes the logic
// that checks that the feature is allowed on Profile before registering a
// binder, and wraps the binder with per-frame feature enablement checks before
// binding the Mojo pipe.
class CrosAppsApiFrameBinderMap {
  STACK_ALLOCATED();

 public:
  CrosAppsApiFrameBinderMap(
      content::RenderFrameHost* rfh,
      mojo::BinderMapWithContext<content::RenderFrameHost*>& map)
      : api_registry_(CrosAppsApiRegistry::GetInstance(
            Profile::FromBrowserContext(rfh->GetBrowserContext()))),
        map_(map) {}
  ~CrosAppsApiFrameBinderMap() = default;

  // If `api_feature` is enabled (e.g. base::Feature is enabled), and it can be
  // enabled on the profile, registers a binder that performs context dependent
  // checks (e.g. whether the frame's last committed URL is in the allowlist)
  // before calling `binder_func`.
  template <typename Interface,
            auto binder_func,
            blink::mojom::RuntimeFeature api_feature>
  void MaybeAdd() {
    if (!api_registry_->CanEnableApi(api_feature)) {
      return;
    }

    map_->template Add<Interface>(
        base::BindRepeating([](content::RenderFrameHost* rfh,
                               mojo::PendingReceiver<Interface> receiver) {
          auto* profile = Profile::FromBrowserContext(rfh->GetBrowserContext());
          const auto& api_registry = CrosAppsApiRegistry::GetInstance(profile);

          if (!api_registry.IsApiEnabledForFrame(
                  api_feature, CrosAppsApiFrameContext(*rfh))) {
            mojo::ReportBadMessage(base::StringPrintf(
                "The requesting context isn't allowed to access interface %s "
                "because it isn't allowed to access the corresponding API: %s",
                Interface::Name_, base::ToString(api_feature).c_str()));
            return;
          }

          binder_func(rfh, std::move(receiver));
        }));
  }

 private:
  const raw_ref<const CrosAppsApiRegistry> api_registry_;
  raw_ref<mojo::BinderMapWithContext<content::RenderFrameHost*>> map_;
};
#endif

void PopulateChromeFrameBinders(
    mojo::BinderMapWithContext<content::RenderFrameHost*>* map,
    content::RenderFrameHost* render_frame_host) {
  map->Add<image_annotation::mojom::Annotator>(
      base::BindRepeating(&BindImageAnnotator));

  map->Add<cart::mojom::CommerceHintObserver>(
      base::BindRepeating(&BindCommerceHintObserver));

  map->Add<blink::mojom::AnchorElementMetricsHost>(
      base::BindRepeating(&NavigationPredictor::Create));

  map->Add<blink::mojom::LCPCriticalPathPredictorHost>(
      base::BindRepeating(&predictors::LCPCriticalPathPredictorHost::Create));

  map->Add<dom_distiller::mojom::DistillabilityService>(
      base::BindRepeating(&BindDistillabilityService));

  map->Add<dom_distiller::mojom::DistillerJavaScriptService>(
      base::BindRepeating(&BindDistillerJavaScriptService));

  map->Add<prerender::mojom::PrerenderCanceler>(
      base::BindRepeating(&BindPrerenderCanceler));

  map->Add<blink::mojom::NoStatePrefetchProcessor>(
      base::BindRepeating(&BindNoStatePrefetchProcessor));

  if (performance_manager::PerformanceManager::IsAvailable()) {
    map->Add<performance_manager::mojom::DocumentCoordinationUnit>(
        base::BindRepeating(
            &performance_manager::BindDocumentCoordinationUnit));
  }

  map->Add<translate::mojom::ContentTranslateDriver>(
      base::BindRepeating(&translate::BindContentTranslateDriver));

  map->Add<blink::mojom::CredentialManager>(
      base::BindRepeating(&ChromePasswordManagerClient::BindCredentialManager));

  map->Add<chrome::mojom::OpenSearchDescriptionDocumentHandler>(
      base::BindRepeating(
          &SearchEngineTabHelper::BindOpenSearchDescriptionDocumentHandler));

#if BUILDFLAG(IS_ANDROID)
  map->Add<blink::mojom::InstalledAppProvider>(base::BindRepeating(
      &ForwardToJavaFrame<blink::mojom::InstalledAppProvider>));
  map->Add<payments::mojom::DigitalGoodsFactory>(base::BindRepeating(
      &ForwardToJavaFrame<payments::mojom::DigitalGoodsFactory>));
#if defined(BROWSER_MEDIA_CONTROLS_MENU)
  map->Add<blink::mojom::MediaControlsMenuHost>(base::BindRepeating(
      &ForwardToJavaFrame<blink::mojom::MediaControlsMenuHost>));
#endif
  map->Add<chrome::mojom::OfflinePageAutoFetcher>(
      base::BindRepeating(&offline_pages::OfflinePageAutoFetcher::Create));
  if (base::FeatureList::IsEnabled(features::kWebPayments)) {
    map->Add<payments::mojom::PaymentRequest>(base::BindRepeating(
        &ForwardToJavaFrame<payments::mojom::PaymentRequest>));
  }
  map->Add<blink::mojom::ShareService>(base::BindRepeating(
      &ForwardToJavaWebContents<blink::mojom::ShareService>));

#if BUILDFLAG(ENABLE_UNHANDLED_TAP)
  map->Add<blink::mojom::UnhandledTapNotifier>(
      base::BindRepeating(&BindUnhandledTapWebContentsObserver));
#endif  // BUILDFLAG(ENABLE_UNHANDLED_TAP)

#else
  map->Add<blink::mojom::BadgeService>(
      base::BindRepeating(&badging::BadgeManager::BindFrameReceiverIfAllowed));
  if (base::FeatureList::IsEnabled(features::kWebPayments)) {
    map->Add<payments::mojom::PaymentRequest>(
        base::BindRepeating(&payments::CreatePaymentRequest));
  }
#endif

#if BUILDFLAG(IS_CHROMEOS_ASH)
  map->Add<payments::mojom::DigitalGoodsFactory>(base::BindRepeating(
      &apps::DigitalGoodsFactoryImpl::BindDigitalGoodsFactory));
#endif

#if BUILDFLAG(IS_CHROMEOS_LACROS)
  if (web_app::IsWebAppsCrosapiEnabled()) {
    map->Add<payments::mojom::DigitalGoodsFactory>(
        base::BindRepeating(&apps::DigitalGoodsFactoryLacros::Bind));
  } else {
    map->Add<payments::mojom::DigitalGoodsFactory>(
        base::BindRepeating(&apps::DigitalGoodsFactoryStub::Bind));
  }

  if (chromeos::features::IsBlinkExtensionEnabled()) {
    // Add frame binders for ChromeOS Apps APIs here using `binder_map_wrapper`.
    CrosAppsApiFrameBinderMap binder_map_wrapper(render_frame_host, *map);
    binder_map_wrapper
        .MaybeAdd<blink::mojom::CrosDiagnostics, &CrosDiagnosticsImpl::Create,
                  blink::mojom::RuntimeFeature::kBlinkExtensionDiagnostics>();
  }
#endif

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_MAC)
  if (base::FeatureList::IsEnabled(features::kWebShare)) {
    map->Add<blink::mojom::ShareService>(
        base::BindRepeating(&ShareServiceImpl::Create));
  }
#endif

#if BUILDFLAG(ENABLE_EXTENSIONS)
  map->Add<extensions::mime_handler::MimeHandlerService>(
      base::BindRepeating(&BindMimeHandlerService));
  map->Add<extensions::mime_handler::BeforeUnloadControl>(
      base::BindRepeating(&BindBeforeUnloadControl));
#endif

  map->Add<network_hints::mojom::NetworkHintsHandler>(
      base::BindRepeating(&BindNetworkHintsHandler));

#if BUILDFLAG(ENABLE_SPEECH_SERVICE)
  map->Add<media::mojom::SpeechRecognitionContext>(
      base::BindRepeating(&BindSpeechRecognitionContextHandler));
  map->Add<media::mojom::SpeechRecognitionClientBrowserInterface>(
      base::BindRepeating(&BindSpeechRecognitionClientBrowserInterfaceHandler));
  map->Add<media::mojom::SpeechRecognitionRecognizerClient>(
      base::BindRepeating(&BindSpeechRecognitionRecognizerClientHandler));
#if BUILDFLAG(IS_WIN)
  map->Add<media::mojom::MediaFoundationRendererNotifier>(
      base::BindRepeating(&BindMediaFoundationRendererNotifierHandler));
#endif
#endif  // BUILDFLAG(ENABLE_SPEECH_SERVICE)

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS)
  if (!render_frame_host->GetParent()) {
    map->Add<chrome::mojom::DraggableRegions>(
        base::BindRepeating(&DraggableRegionsHostImpl::CreateIfAllowed));
  }
#endif

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS)
  if (base::FeatureList::IsEnabled(blink::features::kDesktopPWAsSubApps) &&
      !render_frame_host->GetParentOrOuterDocument()) {
    // The service binder will reject non-primary main frames, but we still need
    // to register it for them because a non-primary main frame could become a
    // primary main frame at a later time (eg. a prerendered page).
    map->Add<blink::mojom::SubAppsService>(
        base::BindRepeating(&web_app::SubAppsServiceImpl::CreateIfAllowed));
  }

  if (companion::visual_query::features::
          IsVisualQuerySuggestionsAgentEnabled()) {
    map->Add<companion::visual_query::mojom::VisualSuggestionsModelProvider>(
        base::BindRepeating(&BindVisualSuggestionsModelProvider));
  }
#endif

#if BUILDFLAG(ENABLE_SCREEN_AI_SERVICE)
  if (features::IsPdfOcrEnabled()) {
    map->Add<screen_ai::mojom::ScreenAIAnnotator>(
        base::BindRepeating(&BindScreenAIAnnotator));
  }

  if (features::IsReadAnythingWithScreen2xEnabled()) {
    map->Add<screen_ai::mojom::Screen2xMainContentExtractor>(
        base::BindRepeating(&BindScreen2xMainContentExtractor));
  }
#endif

#if BUILDFLAG(IS_WIN)
  map->Add<media::mojom::MediaFoundationPreferences>(
      base::BindRepeating(&BindMediaFoundationPreferences));
#endif

#if BUILDFLAG(ENABLE_PRINT_PREVIEW)
  map->Add<blink::mojom::WebPrintingService>(
      base::BindRepeating(&printing::CreateWebPrintingServiceForFrame));
#endif

  if (base::FeatureList::IsEnabled(blink::features::kEnableModelExecutionAPI)) {
    map->Add<blink::mojom::ModelManager>(
        base::BindRepeating(&ModelManagerImpl::Create));
  }
}

void PopulateChromeWebUIFrameBinders(
    mojo::BinderMapWithContext<content::RenderFrameHost*>* map,
    content::RenderFrameHost* render_frame_host) {
#if !BUILDFLAG(IS_CHROMEOS_LACROS)
  RegisterWebUIControllerInterfaceBinder<::mojom::BluetoothInternalsHandler,
                                         BluetoothInternalsUI>(map);
#endif

  RegisterWebUIControllerInterfaceBinder<
      media::mojom::MediaEngagementScoreDetailsProvider, MediaEngagementUI>(
      map);

  RegisterWebUIControllerInterfaceBinder<browsing_topics::mojom::PageHandler,
                                         BrowsingTopicsInternalsUI>(map);

  RegisterWebUIControllerInterfaceBinder<::mojom::OmniboxPageHandler,
                                         OmniboxUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      site_engagement::mojom::SiteEngagementDetailsProvider, SiteEngagementUI>(
      map);

  RegisterWebUIControllerInterfaceBinder<::mojom::UsbInternalsPageHandler,
                                         UsbInternalsUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      history_clusters_internals::mojom::PageHandlerFactory,
      HistoryClustersInternalsUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      optimization_guide_internals::mojom::PageHandlerFactory,
      OptimizationGuideInternalsUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      segmentation_internals::mojom::PageHandlerFactory,
      SegmentationInternalsUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      commerce::mojom::CommerceInternalsHandlerFactory,
      commerce::CommerceInternalsUI>(map);

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS_ASH)
  RegisterWebUIControllerInterfaceBinder<
      connectors_internals::mojom::PageHandler,
      enterprise_connectors::ConnectorsInternalsUI>(map);
#endif

#if BUILDFLAG(IS_CHROMEOS)
  RegisterWebUIControllerInterfaceBinder<dlp_internals::mojom::PageHandler,
                                         policy::DlpInternalsUI>(map);
#endif

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_FUCHSIA)
  RegisterWebUIControllerInterfaceBinder<
      app_management::mojom::PageHandlerFactory, WebAppSettingsUI>(map);
#endif

#if !BUILDFLAG(IS_ANDROID)
  if (search_engines::IsChoiceScreenFlagEnabled(
          search_engines::ChoicePromo::kAny)) {
    RegisterWebUIControllerInterfaceBinder<
        search_engine_choice::mojom::PageHandlerFactory, SearchEngineChoiceUI>(
        map);
  }

  RegisterWebUIControllerInterfaceBinder<downloads::mojom::PageHandlerFactory,
                                         DownloadsUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      new_tab_page_third_party::mojom::PageHandlerFactory,
      NewTabPageThirdPartyUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      color_change_listener::mojom::PageHandler,
#if BUILDFLAG(ENABLE_WEBUI_TAB_STRIP)
      TabStripUI,
#endif
#if BUILDFLAG(IS_CHROMEOS_ASH)
      ash::OobeUI, ash::personalization_app::PersonalizationAppUI,
      ash::vc_background_ui::VcBackgroundUI, ash::settings::OSSettingsUI,
      ash::DiagnosticsDialogUI, ash::FirmwareUpdateAppUI, ash::ScanningUI,
      ash::OSFeedbackUI, ash::ShortcutCustomizationAppUI,
      ash::printing::printing_manager::PrintManagementUI,
      ash::InternetConfigDialogUI, ash::InternetDetailDialogUI, ash::SetTimeUI,
      ash::BluetoothPairingDialogUI, nearby_share::NearbyShareDialogUI,
      ash::cloud_upload::CloudUploadUI, ash::office_fallback::OfficeFallbackUI,
      ash::multidevice_setup::MultiDeviceSetupDialogUI, ash::ParentAccessUI,
      ash::EmojiUI, ash::RemoteMaintenanceCurtainUI,
#endif
#if BUILDFLAG(ENABLE_COMPOSE)
      ComposeUI,
#endif
      NewTabPageUI, OmniboxPopupUI, BookmarksSidePanelUI, CustomizeChromeUI,
      InternalsUI, ReadingListUI, TabSearchUI, WebuiGalleryUI,
      HistoryClustersSidePanelUI, PerformanceSidePanelUI,
      ShoppingInsightsSidePanelUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      new_tab_page::mojom::PageHandlerFactory, NewTabPageUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      most_visited::mojom::MostVisitedPageHandlerFactory, NewTabPageUI,
      NewTabPageThirdPartyUI>(map);

  auto* history_clusters_service =
      HistoryClustersServiceFactory::GetForBrowserContext(
          render_frame_host->GetProcess()->GetBrowserContext());
  if (history_clusters_service &&
      history_clusters_service->is_journeys_feature_flag_enabled()) {
    if (base::FeatureList::IsEnabled(history_clusters::kSidePanelJourneys)) {
      RegisterWebUIControllerInterfaceBinder<
          history_clusters::mojom::PageHandler, HistoryUI,
          HistoryClustersSidePanelUI>(map);
    } else {
      RegisterWebUIControllerInterfaceBinder<
          history_clusters::mojom::PageHandler, HistoryUI>(map);
    }
  }

  RegisterWebUIControllerInterfaceBinder<
      page_image_service::mojom::PageImageServiceHandler, HistoryUI,
      HistoryClustersSidePanelUI, NewTabPageUI, BookmarksSidePanelUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      browser_command::mojom::CommandHandlerFactory, NewTabPageUI, WhatsNewUI>(
      map);

  RegisterWebUIControllerInterfaceBinder<omnibox::mojom::PageHandler,
                                         NewTabPageUI, OmniboxPopupUI>(map);

  RegisterWebUIControllerInterfaceBinder<suggest_internals::mojom::PageHandler,
                                         SuggestInternalsUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      customize_color_scheme_mode::mojom::
          CustomizeColorSchemeModeHandlerFactory,
      CustomizeChromeUI, settings::SettingsUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      theme_color_picker::mojom::ThemeColorPickerHandlerFactory,
      CustomizeChromeUI
#if !BUILDFLAG(IS_CHROMEOS_ASH)
      ,
      ProfileCustomizationUI, settings::SettingsUI
#endif  // !BUILDFLAG(IS_CHROMEOS_ASH)
      >(map);

  RegisterWebUIControllerInterfaceBinder<
      customize_themes::mojom::CustomizeThemesHandlerFactory, NewTabPageUI
#if !BUILDFLAG(IS_CHROMEOS_ASH)
      ,
      ProfileCustomizationUI, settings::SettingsUI
#endif  // !BUILDFLAG(IS_CHROMEOS_ASH)
      >(map);

  RegisterWebUIControllerInterfaceBinder<
      help_bubble::mojom::HelpBubbleHandlerFactory, InternalsUI,
      settings::SettingsUI, ReadingListUI, NewTabPageUI, CustomizeChromeUI,
      PasswordManagerUI>(map);

#if !defined(OFFICIAL_BUILD)
  RegisterWebUIControllerInterfaceBinder<foo::mojom::FooHandler, NewTabPageUI>(
      map);
#endif  // !defined(OFFICIAL_BUILD)

  if (IsCartModuleEnabled() && customize_chrome::IsSidePanelEnabled()) {
    RegisterWebUIControllerInterfaceBinder<chrome_cart::mojom::CartHandler,
                                           NewTabPageUI, CustomizeChromeUI>(
        map);
  } else if (IsCartModuleEnabled()) {
    RegisterWebUIControllerInterfaceBinder<chrome_cart::mojom::CartHandler,
                                           NewTabPageUI>(map);
  }

  if (IsDriveModuleEnabled()) {
    RegisterWebUIControllerInterfaceBinder<drive::mojom::DriveHandler,
                                           NewTabPageUI>(map);
  }

  if (base::FeatureList::IsEnabled(ntp_features::kNtpPhotosModule)) {
    RegisterWebUIControllerInterfaceBinder<photos::mojom::PhotosHandler,
                                           NewTabPageUI>(map);
  }

  if (IsRecipeTasksModuleEnabled()) {
    RegisterWebUIControllerInterfaceBinder<recipes::mojom::RecipesHandler,
                                           NewTabPageUI>(map);
  }

  if (base::FeatureList::IsEnabled(ntp_features::kNtpFeedModule)) {
    RegisterWebUIControllerInterfaceBinder<ntp::feed::mojom::FeedHandler,
                                           NewTabPageUI>(map);
  }

  if (base::FeatureList::IsEnabled(ntp_features::kNtpHistoryClustersModule) ||
      base::FeatureList::IsEnabled(
          ntp_features::kNtpHistoryClustersModuleLoad)) {
    if (base::FeatureList::IsEnabled(ntp_features::kNtpModulesRedesigned)) {
      RegisterWebUIControllerInterfaceBinder<
          ntp::history_clusters_v2::mojom::PageHandler, NewTabPageUI>(map);
    } else {
      RegisterWebUIControllerInterfaceBinder<
          ntp::history_clusters::mojom::PageHandler, NewTabPageUI>(map);
    }
  }

  if (base::FeatureList::IsEnabled(ntp_features::kNtpTabResumptionModule)) {
    RegisterWebUIControllerInterfaceBinder<
        ntp::tab_resumption::mojom::PageHandler, NewTabPageUI>(map);
  }

  RegisterWebUIControllerInterfaceBinder<
      reading_list::mojom::PageHandlerFactory, ReadingListUI>(map);
  RegisterWebUIControllerInterfaceBinder<
      side_panel::mojom::BookmarksPageHandlerFactory, BookmarksSidePanelUI>(
      map);

  RegisterWebUIControllerInterfaceBinder<
      shopping_list::mojom::ShoppingListHandlerFactory, BookmarksSidePanelUI,
      ShoppingInsightsSidePanelUI>(map);

  if (base::FeatureList::IsEnabled(
          performance_manager::features::kPerformanceControlsSidePanel)) {
    RegisterWebUIControllerInterfaceBinder<
        side_panel::mojom::PerformancePageHandlerFactory,
        PerformanceSidePanelUI>(map);
  }

  if (customize_chrome::IsSidePanelEnabled()) {
    RegisterWebUIControllerInterfaceBinder<
        side_panel::mojom::CustomizeChromePageHandlerFactory,
        CustomizeChromeUI>(map);

    if (base::FeatureList::IsEnabled(
            ntp_features::kCustomizeChromeWallpaperSearch) &&
        base::FeatureList::IsEnabled(
            optimization_guide::features::kOptimizationGuideModelExecution)) {
      RegisterWebUIControllerInterfaceBinder<
          side_panel::customize_chrome::mojom::WallpaperSearchHandlerFactory,
          CustomizeChromeUI>(map);
    }
  }

  if (user_notes::IsUserNotesEnabled()) {
    RegisterWebUIControllerInterfaceBinder<
        side_panel::mojom::UserNotesPageHandlerFactory, UserNotesSidePanelUI>(
        map);
  }

  if (features::IsReadAnythingEnabled()) {
    RegisterWebUIControllerInterfaceBinder<
        read_anything::mojom::UntrustedPageHandlerFactory,
        ReadAnythingUntrustedUI>(map);
  }

  RegisterWebUIControllerInterfaceBinder<tab_search::mojom::PageHandlerFactory,
                                         TabSearchUI>(map);
  if (base::FeatureList::IsEnabled(features::kTabSearchUseMetricsReporter)) {
    RegisterWebUIControllerInterfaceBinder<
        metrics_reporter::mojom::PageMetricsHost, TabSearchUI, NewTabPageUI,
        OmniboxPopupUI>(map);
  } else {
    RegisterWebUIControllerInterfaceBinder<
        metrics_reporter::mojom::PageMetricsHost, NewTabPageUI, OmniboxPopupUI>(
        map);
  }

  RegisterWebUIControllerInterfaceBinder<
      ::mojom::user_education_internals::UserEducationInternalsPageHandler,
      InternalsUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      ::mojom::app_service_internals::AppServiceInternalsPageHandler,
      AppServiceInternalsUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      access_code_cast::mojom::PageHandlerFactory,
      media_router::AccessCodeCastUI>(map);
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(ENABLE_WEBUI_TAB_STRIP)
  RegisterWebUIControllerInterfaceBinder<tab_strip::mojom::PageHandlerFactory,
                                         TabStripUI>(map);
#endif

#if BUILDFLAG(IS_CHROMEOS_ASH)
  RegisterWebUIControllerInterfaceBinder<
      ash::file_manager::mojom::PageHandlerFactory,
      ash::file_manager::FileManagerUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      add_supervision::mojom::AddSupervisionHandler, ash::AddSupervisionUI>(
      map);

  RegisterWebUIControllerInterfaceBinder<
      app_management::mojom::PageHandlerFactory, ash::settings::OSSettingsUI>(
      map);

  RegisterWebUIControllerInterfaceBinder<
      ash::settings::mojom::UserActionRecorder, ash::settings::OSSettingsUI>(
      map);

  RegisterWebUIControllerInterfaceBinder<ash::settings::mojom::SearchHandler,
                                         ash::settings::OSSettingsUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      ash::personalization_app::mojom::SearchHandler,
      ash::settings::OSSettingsUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      ash::settings::app_notification::mojom::AppNotificationsHandler,
      ash::settings::OSSettingsUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      ash::settings::app_permission::mojom::AppPermissionsHandler,
      ash::settings::OSSettingsUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      ash::settings::mojom::InputDeviceSettingsProvider,
      ash::settings::OSSettingsUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      ash::settings::mojom::DisplaySettingsProvider,
      ash::settings::OSSettingsUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      ash::common::mojom::ShortcutInputProvider, ash::settings::OSSettingsUI,
      ash::ShortcutCustomizationAppUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      ash::cellular_setup::mojom::CellularSetup, ash::settings::OSSettingsUI>(
      map);

  RegisterWebUIControllerInterfaceBinder<ash::auth::mojom::AuthFactorConfig,
                                         ash::settings::OSSettingsUI,
                                         ash::OobeUI>(map);

  RegisterWebUIControllerInterfaceBinder<ash::auth::mojom::RecoveryFactorEditor,
                                         ash::settings::OSSettingsUI>(map);

  RegisterWebUIControllerInterfaceBinder<ash::auth::mojom::PinFactorEditor,
                                         ash::settings::OSSettingsUI,
                                         ash::OobeUI>(map);

  RegisterWebUIControllerInterfaceBinder<ash::auth::mojom::PasswordFactorEditor,
                                         ash::settings::OSSettingsUI,
                                         ash::OobeUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      ash::cellular_setup::mojom::ESimManager, ash::settings::OSSettingsUI,
      ash::NetworkUI, ash::OobeUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      ash::borealis_installer::mojom::PageHandlerFactory,
      ash::BorealisInstallerUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      ash::crostini_installer::mojom::PageHandlerFactory,
      ash::CrostiniInstallerUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      ash::crostini_upgrader::mojom::PageHandlerFactory,
      ash::CrostiniUpgraderUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      ash::multidevice_setup::mojom::MultiDeviceSetup, ash::OobeUI,
      ash::multidevice::ProximityAuthUI,
      ash::multidevice_setup::MultiDeviceSetupDialogUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      parent_access_ui::mojom::ParentAccessUiHandler, ash::ParentAccessUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      ash::multidevice_setup::mojom::PrivilegedHostDeviceSetter, ash::OobeUI>(
      map);

  RegisterWebUIControllerInterfaceBinder<
      chromeos::network_config::mojom::CrosNetworkConfig,
      ash::InternetConfigDialogUI, ash::InternetDetailDialogUI, ash::NetworkUI,
      ash::OobeUI, ash::settings::OSSettingsUI, ash::LockScreenNetworkUI,
      ash::ShimlessRMADialogUI>(map);

  if (ash::features::IsPasspointSettingsEnabled()) {
    RegisterWebUIControllerInterfaceBinder<
        chromeos::connectivity::mojom::PasspointService,
        ash::InternetDetailDialogUI, ash::NetworkUI,
        ash::settings::OSSettingsUI>(map);
  }

  RegisterWebUIControllerInterfaceBinder<
      chromeos::printing::printing_manager::mojom::PrintingMetadataProvider,
      ash::printing::printing_manager::PrintManagementUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      chromeos::printing::printing_manager::mojom::PrintManagementHandler,
      ash::printing::printing_manager::PrintManagementUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      ash::help_app::mojom::PageHandlerFactory, ash::HelpAppUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      ash::local_search_service::mojom::Index, ash::HelpAppUI>(map);

  RegisterWebUIControllerInterfaceBinder<ash::help_app::mojom::SearchHandler,
                                         ash::HelpAppUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      ash::eche_app::mojom::SignalingMessageExchanger,
      ash::eche_app::EcheAppUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      ash::eche_app::mojom::SystemInfoProvider, ash::eche_app::EcheAppUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      ash::eche_app::mojom::AccessibilityProvider, ash::eche_app::EcheAppUI>(
      map);

  RegisterWebUIControllerInterfaceBinder<ash::eche_app::mojom::UidGenerator,
                                         ash::eche_app::EcheAppUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      ash::eche_app::mojom::NotificationGenerator, ash::eche_app::EcheAppUI>(
      map);

  RegisterWebUIControllerInterfaceBinder<
      ash::eche_app::mojom::DisplayStreamHandler, ash::eche_app::EcheAppUI>(
      map);

  RegisterWebUIControllerInterfaceBinder<
      ash::eche_app::mojom::StreamOrientationObserver,
      ash::eche_app::EcheAppUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      ash::eche_app::mojom::ConnectionStatusObserver, ash::eche_app::EcheAppUI>(
      map);

  RegisterWebUIControllerInterfaceBinder<
      ash::eche_app::mojom::KeyboardLayoutHandler, ash::eche_app::EcheAppUI>(
      map);

  RegisterWebUIControllerInterfaceBinder<
      ash::media_app_ui::mojom::PageHandlerFactory, ash::MediaAppUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      chromeos::network_health::mojom::NetworkHealthService, ash::NetworkUI,
      ash::ConnectivityDiagnosticsUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      chromeos::network_diagnostics::mojom::NetworkDiagnosticsRoutines,
      ash::NetworkUI, ash::ConnectivityDiagnosticsUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      ash::diagnostics::mojom::InputDataProvider, ash::DiagnosticsDialogUI>(
      map);

  RegisterWebUIControllerInterfaceBinder<
      ash::diagnostics::mojom::NetworkHealthProvider, ash::DiagnosticsDialogUI>(
      map);

  RegisterWebUIControllerInterfaceBinder<
      ash::diagnostics::mojom::SystemDataProvider, ash::DiagnosticsDialogUI>(
      map);

  RegisterWebUIControllerInterfaceBinder<
      ash::diagnostics::mojom::SystemRoutineController,
      ash::DiagnosticsDialogUI>(map);

  RegisterWebUIControllerInterfaceBinder<ash::vm::mojom::VmDiagnosticsProvider,
                                         ash::VmUI>(map);

  RegisterWebUIControllerInterfaceBinder<ash::scanning::mojom::ScanService,
                                         ash::ScanningUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      ash::common::mojom::AccessibilityFeatures, ash::ScanningUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      ash::os_feedback_ui::mojom::HelpContentProvider, ash::OSFeedbackUI>(map);
  RegisterWebUIControllerInterfaceBinder<
      ash::os_feedback_ui::mojom::FeedbackServiceProvider, ash::OSFeedbackUI>(
      map);

  RegisterWebUIControllerInterfaceBinder<
      ash::shimless_rma::mojom::ShimlessRmaService, ash::ShimlessRMADialogUI>(
      map);

  if (base::FeatureList::IsEnabled(features::kShortcutCustomizationApp)) {
    RegisterWebUIControllerInterfaceBinder<
        ash::shortcut_customization::mojom::AcceleratorConfigurationProvider,
        ash::ShortcutCustomizationAppUI>(map);

    RegisterWebUIControllerInterfaceBinder<
        ash::shortcut_customization::mojom::SearchHandler,
        ash::ShortcutCustomizationAppUI>(map);
  }

  RegisterWebUIControllerInterfaceBinder<
      emoji_picker::mojom::PageHandlerFactory, ash::EmojiUI>(map);

  RegisterWebUIControllerInterfaceBinder<sensor::mojom::PageHandlerFactory,
                                         ash::SensorInfoUI>(map);
  RegisterWebUIControllerInterfaceBinder<
      enterprise_reporting::mojom::PageHandlerFactory,
      ash::reporting::EnterpriseReportingUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      ash::personalization_app::mojom::WallpaperProvider,
      ash::personalization_app::PersonalizationAppUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      ash::personalization_app::mojom::AmbientProvider,
      ash::personalization_app::PersonalizationAppUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      ash::personalization_app::mojom::ThemeProvider,
      ash::personalization_app::PersonalizationAppUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      ash::personalization_app::mojom::UserProvider,
      ash::personalization_app::PersonalizationAppUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      ash::personalization_app::mojom::KeyboardBacklightProvider,
      ash::personalization_app::PersonalizationAppUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      ash::personalization_app::mojom::SeaPenProvider,
      ash::personalization_app::PersonalizationAppUI,
      ash::vc_background_ui::VcBackgroundUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      launcher_internals::mojom::PageHandlerFactory, ash::LauncherInternalsUI>(
      map);

  RegisterWebUIControllerInterfaceBinder<
      ash::bluetooth_config::mojom::CrosBluetoothConfig,
      ash::BluetoothPairingDialogUI, ash::settings::OSSettingsUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      ash::audio_config::mojom::CrosAudioConfig, ash::settings::OSSettingsUI>(
      map);

  if (ash::features::IsHotspotEnabled()) {
    RegisterWebUIControllerInterfaceBinder<
        ash::hotspot_config::mojom::CrosHotspotConfig,
        ash::settings::OSSettingsUI>(map);
  }

  RegisterWebUIControllerInterfaceBinder<audio::mojom::PageHandlerFactory,
                                         ash::AudioUI>(map);

  RegisterWebUIControllerInterfaceBinder<
      ash::firmware_update::mojom::UpdateProvider, ash::FirmwareUpdateAppUI>(
      map);

  if (ash::features::IsDriveFsMirroringEnabled()) {
    RegisterWebUIControllerInterfaceBinder<
        ash::manage_mirrorsync::mojom::PageHandlerFactory,
        ash::ManageMirrorSyncUI>(map);
  }

  Profile* profile =
      Profile::FromBrowserContext(render_frame_host->GetBrowserContext());
  if (chromeos::IsEligibleAndEnabledUploadOfficeToCloud(profile)) {
    RegisterWebUIControllerInterfaceBinder<
        ash::cloud_upload::mojom::PageHandlerFactory,
        ash::cloud_upload::CloudUploadUI>(map);
  }

  if (chromeos::IsEligibleAndEnabledUploadOfficeToCloud(profile)) {
    RegisterWebUIControllerInterfaceBinder<
        ash::office_fallback::mojom::PageHandlerFactory,
        ash::office_fallback::OfficeFallbackUI>(map);
    RegisterWebUIControllerInterfaceBinder<
        ash::settings::one_drive::mojom::PageHandlerFactory,
        ash::settings::OSSettingsUI>(map);
  }

  RegisterWebUIControllerInterfaceBinder<
      ash::settings::google_drive::mojom::PageHandlerFactory,
      ash::settings::OSSettingsUI>(map);

  if (base::FeatureList::IsEnabled(
          chromeos::features::kCrosWebAppInstallDialog) ||
      base::FeatureList::IsEnabled(
          chromeos::features::kCrosOmniboxInstallDialog)) {
    RegisterWebUIControllerInterfaceBinder<
        ash::app_install::mojom::PageHandlerFactory,
        ash::app_install::AppInstallDialogUI>(map);
  }

  RegisterWebUIControllerInterfaceBinder<
      new_window_proxy::mojom::NewWindowProxy, ash::EmojiUI>(map);
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS)
  RegisterWebUIControllerInterfaceBinder<discards::mojom::DetailsProvider,
                                         DiscardsUI>(map);

  RegisterWebUIControllerInterfaceBinder<discards::mojom::GraphDump,
                                         DiscardsUI>(map);

  RegisterWebUIControllerInterfaceBinder<discards::mojom::SiteDataProvider,
                                         DiscardsUI>(map);
#endif

#if BUILDFLAG(ENABLE_FEED_V2) && BUILDFLAG(IS_ANDROID)
  RegisterWebUIControllerInterfaceBinder<feed_internals::mojom::PageHandler,
                                         FeedInternalsUI>(map);
#endif

#if BUILDFLAG(FULL_SAFE_BROWSING)
  RegisterWebUIControllerInterfaceBinder<::mojom::ResetPasswordHandler,
                                         ResetPasswordUI>(map);
#endif

#if BUILDFLAG(IS_CHROMEOS_ASH)
  // Because Nearby Share is only currently supported for the primary profile,
  // we should only register binders in that scenario. However, we don't want to
  // plumb the profile through to this function, so we 1) ensure that
  // NearbyShareDialogUI will not be created for non-primary profiles, and 2)
  // rely on the BindInterface implementation of OSSettingsUI to ensure that no
  // Nearby Share receivers are bound.
  if (base::FeatureList::IsEnabled(features::kNearbySharing)) {
    RegisterWebUIControllerInterfaceBinder<
        nearby_share::mojom::NearbyShareSettings, ash::settings::OSSettingsUI,
        nearby_share::NearbyShareDialogUI>(map);
    RegisterWebUIControllerInterfaceBinder<nearby_share::mojom::ContactManager,
                                           ash::settings::OSSettingsUI,
                                           nearby_share::NearbyShareDialogUI>(
        map);
    RegisterWebUIControllerInterfaceBinder<
        nearby_share::mojom::DiscoveryManager,
        nearby_share::NearbyShareDialogUI>(map);
    RegisterWebUIControllerInterfaceBinder<nearby_share::mojom::ReceiveManager,
                                           ash::settings::OSSettingsUI>(map);
  }
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

#if !BUILDFLAG(IS_CHROMEOS) && !BUILDFLAG(IS_ANDROID)
  RegisterWebUIControllerInterfaceBinder<::app_home::mojom::PageHandlerFactory,
                                         webapps::AppHomeUI>(map);
#endif  // !BUILDFLAG(IS_CHROMEOS) && !BUILDFLAG(IS_ANDROID)

#if !BUILDFLAG(IS_ANDROID)
  RegisterWebUIControllerInterfaceBinder<::mojom::WebAppInternalsHandler,
                                         WebAppInternalsUI>(map);
#endif

  RegisterWebUIControllerInterfaceBinder<::mojom::LocationInternalsHandler,
                                         LocationInternalsUI>(map);

#if !BUILDFLAG(IS_ANDROID)
  if (base::FeatureList::IsEnabled(
          optimization_guide::features::kOptimizationGuideOnDeviceModel)) {
    RegisterWebUIControllerInterfaceBinder<::mojom::OnDeviceInternalsPage,
                                           OnDeviceInternalsUI>(map);
  }
#endif

#if BUILDFLAG(ENABLE_COMPOSE)
  if (ComposeEnabling::IsEnabledForProfile(Profile::FromBrowserContext(
          render_frame_host->GetBrowserContext()))) {
    RegisterWebUIControllerInterfaceBinder<
        compose::mojom::ComposeSessionPageHandlerFactory, ComposeUI>(map);
  }
#endif  // BUILDFLAG(ENABLE_COMPOSE)

  if (base::FeatureList::IsEnabled(
          privacy_sandbox::kPrivacySandboxInternalsDevUI)) {
    RegisterWebUIControllerInterfaceBinder<
        privacy_sandbox_internals::mojom::PageHandler,
        privacy_sandbox_internals::PrivacySandboxInternalsUI>(map);
  }
}

void PopulateChromeWebUIFrameInterfaceBrokers(
    content::WebUIBrowserInterfaceBrokerRegistry& registry) {
  // This function is broken up into sections based on WebUI types.

  // --- Section 1: chrome:// WebUIs:

#if BUILDFLAG(IS_CHROMEOS_ASH) && !defined(OFFICIAL_BUILD)
  registry.ForWebUI<ash::SampleSystemWebAppUI>()
      .Add<ash::mojom::sample_swa::PageHandlerFactory>()
      .Add<color_change_listener::mojom::PageHandler>();

  registry.ForWebUI<ash::StatusAreaInternalsUI>()
      .Add<ash::mojom::status_area_internals::PageHandler>();
#endif  // BUILDFLAG(IS_CHROMEOS_ASH) && !defined(OFFICIAL_BUILD)

#if BUILDFLAG(IS_CHROMEOS_ASH)
  registry.ForWebUI<ash::CameraAppUI>()
      .Add<color_change_listener::mojom::PageHandler>()
      .Add<cros::mojom::CameraAppDeviceProvider>()
      .Add<ash::camera_app::mojom::CameraAppHelper>();
  registry.ForWebUI<ash::ColorInternalsUI>()
      .Add<color_change_listener::mojom::PageHandler>()
      .Add<ash::color_internals::mojom::WallpaperColorsHandler>();
  registry.ForWebUI<ash::FilesInternalsUI>()
      .Add<ash::mojom::files_internals::PageHandler>();
  registry.ForWebUI<ash::file_manager::FileManagerUI>()
      .Add<color_change_listener::mojom::PageHandler>();
  registry.ForWebUI<ash::smb_dialog::SmbShareDialogUI>()
      .Add<color_change_listener::mojom::PageHandler>();
  registry.ForWebUI<ash::smb_dialog::SmbCredentialsDialogUI>()
      .Add<color_change_listener::mojom::PageHandler>();
  registry.ForWebUI<FeedbackUI>()
      .Add<color_change_listener::mojom::PageHandler>();
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

  // --- Section 2: chrome-untrusted:// WebUIs:

#if BUILDFLAG(IS_CHROMEOS_ASH)
  if (chromeos::features::IsOrcaEnabled()) {
    registry.ForWebUI<ash::MakoUntrustedUI>()
        .Add<ash::orca::mojom::EditorClient>();
  }

  registry.ForWebUI<ash::DemoModeAppUntrustedUI>()
      .Add<ash::mojom::demo_mode::UntrustedPageHandlerFactory>();

  registry.ForWebUI<ash::UntrustedProjectorAnnotatorUI>()
      .Add<ash::annotator::mojom::UntrustedAnnotatorPageHandlerFactory>();

  registry.ForWebUI<ash::UntrustedProjectorUI>()
      .Add<ash::projector::mojom::UntrustedProjectorPageHandlerFactory>();

  registry.ForWebUI<ash::feedback::OsFeedbackUntrustedUI>()
      .Add<color_change_listener::mojom::PageHandler>();

  registry.ForWebUI<ash::MediaAppGuestUI>()
      .Add<color_change_listener::mojom::PageHandler>()
      .Add<ash::media_app_ui::mojom::UntrustedPageHandlerFactory>();

  registry.ForWebUI<ash::HelpAppUntrustedUI>()
      .Add<color_change_listener::mojom::PageHandler>();
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

#if BUILDFLAG(IS_CHROMEOS_ASH) && !defined(OFFICIAL_BUILD)
  registry.ForWebUI<ash::SampleSystemWebAppUntrustedUI>()
      .Add<ash::mojom::sample_swa::UntrustedPageInterfacesFactory>();
#endif  // BUILDFLAG(IS_CHROMEOS_ASH) && !defined(OFFICIAL_BUILD)

#if !BUILDFLAG(IS_ANDROID) && BUILDFLAG(ENABLE_FEED_V2)
  registry.ForWebUI<feed::FeedUI>()
      .Add<feed::mojom::FeedSidePanelHandlerFactory>();
#endif  // !BUILDFLAG(IS_ANDROID)
#if !BUILDFLAG(IS_ANDROID)
  if (companion::IsCompanionFeatureEnabled()) {
    registry.ForWebUI<CompanionSidePanelUntrustedUI>()
        .Add<side_panel::mojom::CompanionPageHandlerFactory>();
  }
  if (features::IsReadAnythingEnabled() &&
      features::IsReadAnythingWebUIToolbarEnabled()) {
    registry.ForWebUI<ReadAnythingUntrustedUI>()
        .Add<color_change_listener::mojom::PageHandler>();
  }
  if (base::FeatureList::IsEnabled(features::kHaTSWebUI)) {
    registry.ForWebUI<HatsUI>().Add<hats::mojom::PageHandlerFactory>();
  }
#endif  // !BUILDFLAG(IS_ANDROID)
}

}  // namespace chrome::internal
