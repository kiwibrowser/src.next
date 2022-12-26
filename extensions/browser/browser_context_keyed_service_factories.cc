// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/browser/browser_context_keyed_service_factories.h"

#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "extensions/browser/api/alarms/alarm_manager.h"
#include "extensions/browser/api/api_resource_manager.h"
#include "extensions/browser/api/audio/audio_api.h"
#include "extensions/browser/api/bluetooth/bluetooth_api.h"
#include "extensions/browser/api/bluetooth/bluetooth_private_api.h"
#include "extensions/browser/api/bluetooth_socket/bluetooth_socket_event_dispatcher.h"
#include "extensions/browser/api/content_settings/content_settings_service.h"
#include "extensions/browser/api/declarative_net_request/rules_monitor_service.h"
#include "extensions/browser/api/feedback_private/feedback_private_api.h"
#include "extensions/browser/api/hid/hid_device_manager.h"
#include "extensions/browser/api/idle/idle_manager_factory.h"
#include "extensions/browser/api/management/management_api.h"
#include "extensions/browser/api/networking_private/networking_private_event_router_factory.h"
#include "extensions/browser/api/offscreen/offscreen_document_manager.h"
#include "extensions/browser/api/power/power_api.h"
#include "extensions/browser/api/runtime/runtime_api.h"
#include "extensions/browser/api/serial/serial_connection.h"
#include "extensions/browser/api/socket/socket.h"
#include "extensions/browser/api/socket/tcp_socket.h"
#include "extensions/browser/api/socket/udp_socket.h"
#include "extensions/browser/api/sockets_tcp/tcp_socket_event_dispatcher.h"
#include "extensions/browser/api/sockets_tcp_server/tcp_server_socket_event_dispatcher.h"
#include "extensions/browser/api/sockets_udp/udp_socket_event_dispatcher.h"
#include "extensions/browser/api/storage/storage_frontend.h"
#include "extensions/browser/api/system_info/system_info_api.h"
#include "extensions/browser/api/usb/usb_device_manager.h"
#include "extensions/browser/api/usb/usb_device_resource.h"
#include "extensions/browser/api/web_request/web_request_api.h"
#include "extensions/browser/app_window/app_window_geometry_cache.h"
#include "extensions/browser/app_window/app_window_registry.h"
#include "extensions/browser/event_router_factory.h"
#include "extensions/browser/extension_message_filter.h"
#include "extensions/browser/extension_prefs_factory.h"
#include "extensions/browser/extension_protocols.h"
#include "extensions/browser/extension_service_worker_message_filter.h"
#include "extensions/browser/process_manager_factory.h"
#include "extensions/browser/renderer_startup_helper.h"
#include "extensions/browser/updater/update_service_factory.h"

#if BUILDFLAG(IS_CHROMEOS)
#include "extensions/browser/api/clipboard/clipboard_api.h"
#endif

#if BUILDFLAG(IS_CHROMEOS_ASH)
#include "extensions/browser/api/virtual_keyboard_private/virtual_keyboard_private_api.h"
#include "extensions/browser/api/webcam_private/webcam_private_api.h"
#endif

namespace extensions {

void EnsureBrowserContextKeyedServiceFactoriesBuilt() {
  AlarmManager::GetFactoryInstance();
  ApiResourceManager<ResumableTCPServerSocket>::GetFactoryInstance();
  ApiResourceManager<ResumableTCPSocket>::GetFactoryInstance();
  ApiResourceManager<ResumableUDPSocket>::GetFactoryInstance();
  ApiResourceManager<SerialConnection>::GetFactoryInstance();
  ApiResourceManager<Socket>::GetFactoryInstance();
  ApiResourceManager<UsbDeviceResource>::GetFactoryInstance();
  AppWindowGeometryCache::Factory::GetInstance();
  AppWindowRegistry::Factory::GetInstance();
  AudioAPI::GetFactoryInstance();
  BluetoothAPI::GetFactoryInstance();
  BluetoothPrivateAPI::GetFactoryInstance();
#if BUILDFLAG(IS_CHROMEOS)
  ClipboardAPI::GetFactoryInstance();
#endif
  api::BluetoothSocketEventDispatcher::GetFactoryInstance();
  api::TCPServerSocketEventDispatcher::GetFactoryInstance();
  api::TCPSocketEventDispatcher::GetFactoryInstance();
  api::UDPSocketEventDispatcher::GetFactoryInstance();
  ContentSettingsService::GetFactoryInstance();
  declarative_net_request::RulesMonitorService::GetFactoryInstance();
  EnsureExtensionURLLoaderFactoryShutdownNotifierFactoryBuilt();
  EventRouterFactory::GetInstance();
  ExtensionFunction::EnsureShutdownNotifierFactoryBuilt();
  ExtensionMessageFilter::EnsureShutdownNotifierFactoryBuilt();
  ExtensionServiceWorkerMessageFilter::EnsureShutdownNotifierFactoryBuilt();
  ExtensionPrefsFactory::GetInstance();
  FeedbackPrivateAPI::GetFactoryInstance();
  HidDeviceManager::GetFactoryInstance();
  IdleManagerFactory::GetInstance();
  ManagementAPI::GetFactoryInstance();
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_WIN) || \
    BUILDFLAG(IS_MAC)
  NetworkingPrivateEventRouterFactory::GetInstance();
#endif
  OffscreenDocumentManager::GetFactory();
  PowerAPI::GetFactoryInstance();
  ProcessManagerFactory::GetInstance();
  RendererStartupHelperFactory::GetInstance();
  RuntimeAPI::GetFactoryInstance();
  StorageFrontend::GetFactoryInstance();
  SystemInfoAPI::GetFactoryInstance();
  UpdateServiceFactory::GetInstance();
  UsbDeviceManager::GetFactoryInstance();
#if BUILDFLAG(IS_CHROMEOS_ASH)
  VirtualKeyboardAPI::GetFactoryInstance();
  WebcamPrivateAPI::GetFactoryInstance();
#endif
  WebRequestAPI::GetFactoryInstance();
}

}  // namespace extensions
