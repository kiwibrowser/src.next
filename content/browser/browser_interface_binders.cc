// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/browser_interface_binders.h"

#include "base/callback.h"
#include "base/callback_helpers.h"
#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/no_destructor.h"
#include "base/task/thread_pool.h"
#include "build/branding_buildflags.h"
#include "build/build_config.h"
#include "cc/base/switches.h"
#include "content/browser/attribution_reporting/attribution_internals.mojom.h"
#include "content/browser/attribution_reporting/attribution_internals_ui.h"
#include "content/browser/background_fetch/background_fetch_service_impl.h"
#include "content/browser/bad_message.h"
#include "content/browser/browser_context_impl.h"
#include "content/browser/browser_main_loop.h"
#include "content/browser/browsing_topics/browsing_topics_document_host.h"
#include "content/browser/compute_pressure/pressure_service_impl.h"
#include "content/browser/contacts/contacts_manager_impl.h"
#include "content/browser/content_index/content_index_service_impl.h"
#include "content/browser/cookie_store/cookie_store_manager.h"
#include "content/browser/eye_dropper_chooser_impl.h"
#include "content/browser/handwriting/handwriting_recognition_service_factory.h"
#include "content/browser/image_capture/image_capture_impl.h"
#include "content/browser/interest_group/ad_auction_service_impl.h"
#include "content/browser/keyboard_lock/keyboard_lock_service_impl.h"
#include "content/browser/loader/content_security_notifier.h"
#include "content/browser/media/media_web_contents_observer.h"
#include "content/browser/media/midi_host.h"
#include "content/browser/media/session/media_session_service_impl.h"
#include "content/browser/ml/ml_service_factory.h"
#include "content/browser/net/reporting_service_proxy.h"
#include "content/browser/picture_in_picture/picture_in_picture_service_impl.h"
#include "content/browser/preloading/prerender/prerender_internals.mojom.h"
#include "content/browser/preloading/prerender/prerender_internals_ui.h"
#include "content/browser/preloading/speculation_rules/speculation_host_impl.h"
#include "content/browser/process_internals/process_internals.mojom.h"
#include "content/browser/process_internals/process_internals_ui.h"
#include "content/browser/quota/quota_context.h"
#include "content/browser/quota/quota_internals_ui.h"
#include "content/browser/renderer_host/clipboard_host_impl.h"
#include "content/browser/renderer_host/file_utilities_host_impl.h"
#include "content/browser/renderer_host/media/media_devices_dispatcher_host.h"
#include "content/browser/renderer_host/media/media_stream_dispatcher_host.h"
#include "content/browser/renderer_host/media/video_capture_host.h"
#include "content/browser/renderer_host/render_frame_host_impl.h"
#include "content/browser/renderer_host/render_process_host_impl.h"
#include "content/browser/service_worker/service_worker_host.h"
#include "content/browser/speech/speech_recognition_dispatcher_host.h"
#include "content/browser/wake_lock/wake_lock_service_impl.h"
#include "content/browser/web_contents/file_chooser_impl.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/browser/webid/flags.h"
#include "content/browser/worker_host/dedicated_worker_host.h"
#include "content/browser/worker_host/shared_worker_connector_impl.h"
#include "content/browser/worker_host/shared_worker_host.h"
#include "content/browser/xr/service/vr_service_impl.h"
#include "content/common/input/input_injector.mojom.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/device_service.h"
#include "content/public/browser/service_worker_context.h"
#include "content/public/browser/service_worker_version_base_info.h"
#include "content/public/browser/shared_worker_instance.h"
#include "content/public/browser/site_isolation_policy.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/common/content_client.h"
#include "content/public/common/content_features.h"
#include "content/public/common/url_constants.h"
#include "device/gamepad/gamepad_haptics_manager.h"
#include "device/gamepad/gamepad_monitor.h"
#include "device/gamepad/public/mojom/gamepad.mojom.h"
#include "device/vr/buildflags/buildflags.h"
#include "device/vr/public/mojom/vr_service.mojom.h"
#include "media/capture/mojom/image_capture.mojom.h"
#include "media/capture/mojom/video_capture.mojom.h"
#include "media/mojo/mojom/interface_factory.mojom.h"
#include "media/mojo/mojom/media_metrics_provider.mojom.h"
#include "media/mojo/mojom/media_player.mojom.h"
#include "media/mojo/mojom/remoting.mojom.h"
#include "media/mojo/mojom/video_decode_perf_history.mojom.h"
#include "media/mojo/mojom/webrtc_video_perf.mojom.h"
#include "media/mojo/services/webrtc_video_perf_recorder.h"
#include "services/device/public/mojom/battery_monitor.mojom.h"
#include "services/device/public/mojom/sensor_provider.mojom.h"
#include "services/device/public/mojom/vibration_manager.mojom.h"
#include "services/metrics/public/mojom/ukm_interface.mojom.h"
#include "services/metrics/ukm_recorder_interface.h"
#include "services/network/public/mojom/p2p.mojom.h"
#include "services/network/public/mojom/restricted_cookie_manager.mojom.h"
#include "services/shape_detection/public/mojom/barcodedetection_provider.mojom.h"
#include "services/shape_detection/public/mojom/facedetection_provider.mojom.h"
#include "services/shape_detection/public/mojom/shape_detection_service.mojom.h"
#include "services/shape_detection/public/mojom/textdetection.mojom.h"
#include "storage/browser/quota/quota_internals.mojom.h"
#include "storage/browser/quota/quota_manager.h"
#include "storage/browser/quota/quota_manager_proxy.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/common/storage_key/storage_key.h"
#include "third_party/blink/public/mojom/background_fetch/background_fetch.mojom.h"
#include "third_party/blink/public/mojom/background_sync/background_sync.mojom.h"
#include "third_party/blink/public/mojom/bluetooth/web_bluetooth.mojom.h"
#include "third_party/blink/public/mojom/buckets/bucket_manager_host.mojom.h"
#include "third_party/blink/public/mojom/cache_storage/cache_storage.mojom.h"
#include "third_party/blink/public/mojom/choosers/color_chooser.mojom.h"
#include "third_party/blink/public/mojom/compute_pressure/pressure_service.mojom.h"
#include "third_party/blink/public/mojom/contacts/contacts_manager.mojom.h"
#include "third_party/blink/public/mojom/content_index/content_index.mojom.h"
#include "third_party/blink/public/mojom/cookie_store/cookie_store.mojom.h"
#include "third_party/blink/public/mojom/credentialmanagement/credential_manager.mojom.h"
#include "third_party/blink/public/mojom/device/device.mojom.h"
#include "third_party/blink/public/mojom/feature_observer/feature_observer.mojom.h"
#include "third_party/blink/public/mojom/file/file_utilities.mojom.h"
#include "third_party/blink/public/mojom/file_system_access/file_system_access_manager.mojom.h"
#include "third_party/blink/public/mojom/filesystem/file_system.mojom.h"
#include "third_party/blink/public/mojom/font_access/font_access.mojom.h"
#include "third_party/blink/public/mojom/frame/pending_beacon.mojom.h"
#include "third_party/blink/public/mojom/geolocation/geolocation_service.mojom.h"
#include "third_party/blink/public/mojom/idle/idle_manager.mojom.h"
#include "third_party/blink/public/mojom/indexeddb/indexeddb.mojom.h"
#include "third_party/blink/public/mojom/input/input_host.mojom.h"
#include "third_party/blink/public/mojom/keyboard_lock/keyboard_lock.mojom.h"
#include "third_party/blink/public/mojom/loader/anchor_element_interaction_host.mojom.h"
#include "third_party/blink/public/mojom/loader/code_cache.mojom.h"
#include "third_party/blink/public/mojom/loader/content_security_notifier.mojom.h"
#include "third_party/blink/public/mojom/loader/navigation_predictor.mojom.h"
#include "third_party/blink/public/mojom/locks/lock_manager.mojom.h"
#include "third_party/blink/public/mojom/media/renderer_audio_input_stream_factory.mojom.h"
#include "third_party/blink/public/mojom/media/renderer_audio_output_stream_factory.mojom.h"
#include "third_party/blink/public/mojom/mediasession/media_session.mojom.h"
#include "third_party/blink/public/mojom/mediastream/media_devices.mojom.h"
#include "third_party/blink/public/mojom/mediastream/media_stream.mojom.h"
#include "third_party/blink/public/mojom/native_io/native_io.mojom.h"
#include "third_party/blink/public/mojom/notifications/notification_service.mojom.h"
#include "third_party/blink/public/mojom/payments/payment_app.mojom.h"
#include "third_party/blink/public/mojom/payments/payment_credential.mojom.h"
#include "third_party/blink/public/mojom/peerconnection/peer_connection_tracker.mojom.h"
#include "third_party/blink/public/mojom/permissions/permission.mojom.h"
#include "third_party/blink/public/mojom/picture_in_picture/picture_in_picture.mojom.h"
#include "third_party/blink/public/mojom/prerender/prerender.mojom.h"
#include "third_party/blink/public/mojom/presentation/presentation.mojom.h"
#include "third_party/blink/public/mojom/push_messaging/push_messaging.mojom.h"
#include "third_party/blink/public/mojom/quota/quota_manager_host.mojom.h"
#include "third_party/blink/public/mojom/sms/webotp_service.mojom.h"
#include "third_party/blink/public/mojom/speculation_rules/speculation_rules.mojom.h"
#include "third_party/blink/public/mojom/speech/speech_recognizer.mojom.h"
#include "third_party/blink/public/mojom/speech/speech_synthesis.mojom.h"
#include "third_party/blink/public/mojom/usb/web_usb_service.mojom.h"
#include "third_party/blink/public/mojom/wake_lock/wake_lock.mojom.h"
#include "third_party/blink/public/mojom/webaudio/audio_context_manager.mojom.h"
#include "third_party/blink/public/mojom/webauthn/authenticator.mojom.h"
#include "third_party/blink/public/mojom/webauthn/virtual_authenticator.mojom.h"
#include "third_party/blink/public/mojom/webid/federated_auth_request.mojom.h"
#include "third_party/blink/public/mojom/websockets/websocket_connector.mojom.h"
#include "third_party/blink/public/mojom/webtransport/web_transport_connector.mojom.h"
#include "third_party/blink/public/mojom/worker/dedicated_worker_host_factory.mojom.h"
#include "third_party/blink/public/mojom/worker/shared_worker_connector.mojom.h"
#include "third_party/blink/public/public_buildflags.h"
#include "url/origin.h"

#if BUILDFLAG(IS_ANDROID)
#include "content/browser/android/date_time_chooser_android.h"
#include "content/browser/android/text_suggestion_host_android.h"
#include "content/browser/renderer_host/render_widget_host_view_android.h"
#include "services/device/public/mojom/nfc.mojom.h"
#include "third_party/blink/public/mojom/hid/hid.mojom.h"
#include "third_party/blink/public/mojom/unhandled_tap_notifier/unhandled_tap_notifier.mojom.h"
#else  // BUILDFLAG(IS_ANDROID)
#include "content/browser/direct_sockets/direct_sockets_service_impl.h"
#include "media/mojo/mojom/renderer_extensions.mojom.h"
#include "media/mojo/mojom/speech_recognition.mojom.h"  // nogncheck
#include "third_party/blink/public/mojom/hid/hid.mojom.h"
#include "third_party/blink/public/mojom/installedapp/installed_app_provider.mojom.h"
#include "third_party/blink/public/mojom/serial/serial.mojom.h"
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(ENABLE_MEDIA_REMOTING)
#include "media/mojo/mojom/remoting.mojom-forward.h"
#endif

#if BUILDFLAG(GOOGLE_CHROME_BRANDING) && BUILDFLAG(IS_CHROMEOS)
#include "content/public/browser/service_process_host.h"
#else
#include "content/browser/gpu/gpu_process_host.h"
#endif

#if BUILDFLAG(IS_MAC)
#include "content/browser/renderer_host/text_input_host_impl.h"
#include "third_party/blink/public/mojom/input/text_input_host.mojom.h"
#endif

#if BUILDFLAG(IS_CHROMEOS)
#include "content/browser/lock_screen/lock_screen_service_impl.h"
#include "third_party/blink/public/mojom/lock_screen/lock_screen.mojom.h"
#endif

#if BUILDFLAG(IS_FUCHSIA)
#include "content/browser/renderer_host/media/media_resource_provider_fuchsia.h"
#include "media/fuchsia/mojom/fuchsia_media_resource_provider.mojom.h"
#endif

namespace blink {
class StorageKey;
}  // namespace blink

namespace content {
namespace internal {

namespace {

shape_detection::mojom::ShapeDetectionService* GetShapeDetectionService() {
  static base::NoDestructor<
      mojo::Remote<shape_detection::mojom::ShapeDetectionService>>
      remote;
  if (!*remote) {
#if BUILDFLAG(GOOGLE_CHROME_BRANDING) && BUILDFLAG(IS_CHROMEOS)
    ServiceProcessHost::Launch<shape_detection::mojom::ShapeDetectionService>(
        remote->BindNewPipeAndPassReceiver(),
        ServiceProcessHost::Options()
            .WithDisplayName("Shape Detection Service")
            .Pass());
#else
    auto* gpu = GpuProcessHost::Get();
    if (gpu)
      gpu->RunService(remote->BindNewPipeAndPassReceiver());
#endif
    remote->reset_on_disconnect();
  }

  return remote->get();
}

void BindBarcodeDetectionProvider(
    mojo::PendingReceiver<shape_detection::mojom::BarcodeDetectionProvider>
        receiver) {
  GetShapeDetectionService()->BindBarcodeDetectionProvider(std::move(receiver));
}

void BindFaceDetectionProvider(
    mojo::PendingReceiver<shape_detection::mojom::FaceDetectionProvider>
        receiver) {
  GetShapeDetectionService()->BindFaceDetectionProvider(std::move(receiver));
}

void BindTextDetection(
    mojo::PendingReceiver<shape_detection::mojom::TextDetection> receiver) {
  GetShapeDetectionService()->BindTextDetection(std::move(receiver));
}

#if BUILDFLAG(IS_MAC)
void BindTextInputHost(
    mojo::PendingReceiver<blink::mojom::TextInputHost> receiver) {
  GetIOThreadTaskRunner({})->PostTask(
      FROM_HERE,
      base::BindOnce(&TextInputHostImpl::Create, std::move(receiver)));
}
#endif

void BindUkmRecorderInterface(
    mojo::PendingReceiver<ukm::mojom::UkmRecorderInterface> receiver) {
  metrics::UkmRecorderInterface::Create(ukm::UkmRecorder::Get(),
                                        std::move(receiver));
}

void BindColorChooserFactoryForFrame(
    RenderFrameHost* host,
    mojo::PendingReceiver<blink::mojom::ColorChooserFactory> receiver) {
  auto* web_contents =
      static_cast<WebContentsImpl*>(WebContents::FromRenderFrameHost(host));
  web_contents->OnColorChooserFactoryReceiver(std::move(receiver));
}

void BindAttributionInternalsHandler(
    RenderFrameHost* host,
    mojo::PendingReceiver<attribution_internals::mojom::Handler> receiver) {
  WebUI* web_ui = host->GetWebUI();

  // Performs a safe downcast to the concrete AttributionInternalsUI subclass.
  AttributionInternalsUI* attribution_internals_ui =
      web_ui ? web_ui->GetController()->GetAs<AttributionInternalsUI>()
             : nullptr;

  // This is expected to be called only for outermost main frames and for the
  // right WebUI pages matching the same WebUI associated to the
  // RenderFrameHost.
  if (host->GetParentOrOuterDocument() || !attribution_internals_ui) {
    ReceivedBadMessage(
        host->GetProcess(),
        bad_message::BadMessageReason::RFH_INVALID_WEB_UI_CONTROLLER);
    return;
  }

  DCHECK_EQ(host->GetLastCommittedURL().host_piece(),
            kChromeUIAttributionInternalsHost);
  DCHECK(host->GetLastCommittedURL().SchemeIs(kChromeUIScheme));

  attribution_internals_ui->BindInterface(std::move(receiver));
}

void BindQuotaInternalsHandler(
    RenderFrameHost* host,
    mojo::PendingReceiver<storage::mojom::QuotaInternalsHandler> receiver) {
  WebUI* web_ui = host->GetWebUI();

  // Performs a safe downcast to the concrete QuotaInternalsUI
  // subclass.
  QuotaInternalsUI* quota_internals_ui =
      web_ui ? web_ui->GetController()->GetAs<QuotaInternalsUI>() : nullptr;

  // This is expected to be called only for main frames and for the right WebUI
  // pages matching the same WebUI associated to the RenderFrameHost.
  if (host->GetParent() || !quota_internals_ui) {
    ReceivedBadMessage(
        host->GetProcess(),
        bad_message::BadMessageReason::RFH_INVALID_WEB_UI_CONTROLLER);
    return;
  }

  DCHECK_EQ(host->GetLastCommittedURL().host_piece(),
            kChromeUIQuotaInternalsHost);
  DCHECK(host->GetLastCommittedURL().SchemeIs(kChromeUIScheme));

  static_cast<StoragePartitionImpl*>(host->GetStoragePartition())
      ->GetQuotaManager()
      ->proxy()
      ->BindInternalsHandler(std::move(receiver));
}

void BindPrerenderInternalsHandler(
    RenderFrameHost* host,
    mojo::PendingReceiver<mojom::PrerenderInternalsHandler> receiver) {
  WebUI* web_ui = host->GetWebUI();

  PrerenderInternalsUI* prerender_internals_ui =
      web_ui ? web_ui->GetController()->GetAs<PrerenderInternalsUI>() : nullptr;

  // This is expected to be called only for outermost main frames and for the
  // right WebUI pages matching the same WebUI associated to the
  // RenderFrameHost.
  if (host->GetParentOrOuterDocument() || !prerender_internals_ui) {
    ReceivedBadMessage(
        host->GetProcess(),
        bad_message::BadMessageReason::RFH_INVALID_WEB_UI_CONTROLLER);
    return;
  }

  DCHECK_EQ(host->GetLastCommittedURL().host_piece(),
            kChromeUIPrerenderInternalsHost);
  DCHECK(host->GetLastCommittedURL().SchemeIs(kChromeUIScheme));

  prerender_internals_ui->BindPrerenderInternalsHandler(std::move(receiver));
}

void BindProcessInternalsHandler(
    RenderFrameHost* host,
    mojo::PendingReceiver<::mojom::ProcessInternalsHandler> receiver) {
  WebUI* web_ui = host->GetWebUI();

  // Performs a safe downcast to the concrete ProcessInternalsUI subclass.
  ProcessInternalsUI* process_internals_ui =
      web_ui ? web_ui->GetController()->GetAs<ProcessInternalsUI>() : nullptr;

  // This is expected to be called only for outermost main frames and for the
  // right WebUI pages matching the same WebUI associated to the
  // RenderFrameHost.
  if (host->GetParentOrOuterDocument() || !process_internals_ui) {
    ReceivedBadMessage(
        host->GetProcess(),
        bad_message::BadMessageReason::RFH_INVALID_WEB_UI_CONTROLLER);
    return;
  }

  DCHECK_EQ(host->GetLastCommittedURL().host_piece(),
            kChromeUIProcessInternalsHost);
  DCHECK(host->GetLastCommittedURL().SchemeIs(kChromeUIScheme));

  process_internals_ui->BindProcessInternalsHandler(std::move(receiver), host);
}

void BindQuotaManagerHost(
    RenderFrameHostImpl* host,
    mojo::PendingReceiver<blink::mojom::QuotaManagerHost> receiver) {
  host->GetStoragePartition()->GetQuotaContext()->BindQuotaManagerHost(
      host->GetProcess()->GetID(), host->GetRoutingID(), host->storage_key(),
      std::move(receiver));
}

void BindNativeIOHost(
    RenderFrameHost* host,
    mojo::PendingReceiver<blink::mojom::NativeIOHost> receiver) {
  static_cast<RenderProcessHostImpl*>(host->GetProcess())
      ->BindNativeIOHost(static_cast<RenderFrameHostImpl*>(host)->storage_key(),
                         std::move(receiver));
}

void BindSharedWorkerConnector(
    RenderFrameHostImpl* host,
    mojo::PendingReceiver<blink::mojom::SharedWorkerConnector> receiver) {
  SharedWorkerConnectorImpl::Create(host->GetGlobalId(), std::move(receiver));
}

#if BUILDFLAG(IS_ANDROID)
void BindDateTimeChooserForFrame(
    RenderFrameHost* host,
    mojo::PendingReceiver<blink::mojom::DateTimeChooser> receiver) {
  auto* date_time_chooser = DateTimeChooserAndroid::FromWebContents(
      WebContents::FromRenderFrameHost(host));
  date_time_chooser->OnDateTimeChooserReceiver(std::move(receiver));
}

void BindTextSuggestionHostForFrame(
    RenderFrameHost* host,
    mojo::PendingReceiver<blink::mojom::TextSuggestionHost> receiver) {
  auto* view = static_cast<RenderWidgetHostViewAndroid*>(host->GetView());
  if (!view || !view->text_suggestion_host())
    return;

  view->text_suggestion_host()->BindTextSuggestionHost(std::move(receiver));
}
#endif

// Get the service worker's worker process ID and post a task to bind the
// receiver on a USER_VISIBLE task runner.
// This is necessary because:
// - Binding the host itself and checking the ID on the task's thread may cause
//   a UAF if the host has been deleted in the meantime.
// - The process ID is not yet populated at the time `PopulateInterfaceBinders`
//   is called.
void BindFileUtilitiesHost(
    ServiceWorkerHost* host,
    mojo::PendingReceiver<blink::mojom::FileUtilitiesHost> receiver) {
  auto task_runner = base::ThreadPool::CreateSequencedTaskRunner(
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE});
  task_runner->PostTask(
      FROM_HERE,
      base::BindOnce(&FileUtilitiesHostImpl::Create, host->worker_process_id(),
                     std::move(receiver)));
}

template <typename WorkerHost, typename Interface>
base::RepeatingCallback<void(mojo::PendingReceiver<Interface>)>
BindWorkerReceiver(
    void (RenderProcessHostImpl::*method)(mojo::PendingReceiver<Interface>),
    WorkerHost* host) {
  return base::BindRepeating(
      [](WorkerHost* host,
         void (RenderProcessHostImpl::*method)(
             mojo::PendingReceiver<Interface>),
         mojo::PendingReceiver<Interface> receiver) {
        auto* process_host =
            static_cast<RenderProcessHostImpl*>(host->GetProcessHost());
        if (process_host)
          (process_host->*method)(std::move(receiver));
      },
      base::Unretained(host), method);
}

template <typename WorkerHost, typename Interface>
base::RepeatingCallback<void(const url::Origin&,
                             mojo::PendingReceiver<Interface>)>
BindWorkerReceiverForOrigin(
    void (RenderProcessHostImpl::*method)(const url::Origin&,
                                          mojo::PendingReceiver<Interface>),
    WorkerHost* host) {
  return base::BindRepeating(
      [](WorkerHost* host,
         void (RenderProcessHostImpl::*method)(
             const url::Origin&, mojo::PendingReceiver<Interface>),
         const url::Origin& origin, mojo::PendingReceiver<Interface> receiver) {
        auto* process_host =
            static_cast<RenderProcessHostImpl*>(host->GetProcessHost());
        if (process_host)
          (process_host->*method)(origin, std::move(receiver));
      },
      base::Unretained(host), method);
}

template <typename WorkerHost, typename Interface>
base::RepeatingCallback<void(const url::Origin&,
                             mojo::PendingReceiver<Interface>)>
BindWorkerReceiverForOriginAndFrameId(
    void (RenderProcessHostImpl::*method)(int,
                                          const url::Origin&,
                                          mojo::PendingReceiver<Interface>),
    WorkerHost* host) {
  return base::BindRepeating(
      [](WorkerHost* host,
         void (RenderProcessHostImpl::*method)(
             int, const url::Origin&, mojo::PendingReceiver<Interface>),
         const url::Origin& origin, mojo::PendingReceiver<Interface> receiver) {
        auto* process_host =
            static_cast<RenderProcessHostImpl*>(host->GetProcessHost());
        if (process_host)
          (process_host->*method)(MSG_ROUTING_NONE, origin,
                                  std::move(receiver));
      },
      base::Unretained(host), method);
}

template <typename WorkerHost, typename Interface>
base::RepeatingCallback<void(mojo::PendingReceiver<Interface>)>
BindWorkerReceiverForStorageKey(
    void (RenderProcessHostImpl::*method)(const blink::StorageKey&,
                                          mojo::PendingReceiver<Interface>),
    WorkerHost* host) {
  return base::BindRepeating(
      [](WorkerHost* host,
         void (RenderProcessHostImpl::*method)(
             const blink::StorageKey&, mojo::PendingReceiver<Interface>),
         mojo::PendingReceiver<Interface> receiver) {
        auto* process_host =
            static_cast<RenderProcessHostImpl*>(host->GetProcessHost());
        if (process_host)
          (process_host->*method)(host->GetStorageKey(), std::move(receiver));
      },
      base::Unretained(host), method);
}

template <typename Interface>
base::RepeatingCallback<void(mojo::PendingReceiver<Interface>)>
BindServiceWorkerReceiver(
    void (RenderProcessHostImpl::*method)(mojo::PendingReceiver<Interface>),
    ServiceWorkerHost* host) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  return base::BindRepeating(
      [](ServiceWorkerHost* host,
         void (RenderProcessHostImpl::*method)(
             mojo::PendingReceiver<Interface>),
         mojo::PendingReceiver<Interface> receiver) {
        DCHECK_CURRENTLY_ON(BrowserThread::UI);
        auto* process_host = static_cast<RenderProcessHostImpl*>(
            RenderProcessHost::FromID(host->worker_process_id()));
        if (!process_host)
          return;
        (process_host->*method)(std::move(receiver));
      },
      base::Unretained(host), method);
}

template <typename Interface>
base::RepeatingCallback<void(const ServiceWorkerVersionBaseInfo&,
                             mojo::PendingReceiver<Interface>)>
BindServiceWorkerReceiverForOrigin(
    void (RenderProcessHostImpl::*method)(const url::Origin&,
                                          mojo::PendingReceiver<Interface>),
    ServiceWorkerHost* host) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  return base::BindRepeating(
      [](ServiceWorkerHost* host,
         void (RenderProcessHostImpl::*method)(
             const url::Origin&, mojo::PendingReceiver<Interface>),
         const ServiceWorkerVersionBaseInfo& info,
         mojo::PendingReceiver<Interface> receiver) {
        DCHECK_CURRENTLY_ON(BrowserThread::UI);
        auto origin = info.storage_key.origin();
        auto* process_host = static_cast<RenderProcessHostImpl*>(
            RenderProcessHost::FromID(host->worker_process_id()));
        if (!process_host)
          return;
        (process_host->*method)(origin, std::move(receiver));
      },
      base::Unretained(host), method);
}

template <typename Interface>
base::RepeatingCallback<void(const ServiceWorkerVersionBaseInfo&,
                             mojo::PendingReceiver<Interface>)>
BindServiceWorkerReceiverForOriginAndFrameId(
    void (RenderProcessHostImpl::*method)(int,
                                          const url::Origin&,
                                          mojo::PendingReceiver<Interface>),
    ServiceWorkerHost* host) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  return base::BindRepeating(
      [](ServiceWorkerHost* host,
         void (RenderProcessHostImpl::*method)(
             int, const url::Origin&, mojo::PendingReceiver<Interface>),
         const ServiceWorkerVersionBaseInfo& info,
         mojo::PendingReceiver<Interface> receiver) {
        DCHECK_CURRENTLY_ON(BrowserThread::UI);
        auto origin = info.storage_key.origin();
        auto* process_host = static_cast<RenderProcessHostImpl*>(
            RenderProcessHost::FromID(host->worker_process_id()));
        if (!process_host)
          return;
        (process_host->*method)(MSG_ROUTING_NONE, origin, std::move(receiver));
      },
      base::Unretained(host), method);
}

template <typename Interface>
base::RepeatingCallback<void(const ServiceWorkerVersionBaseInfo&,
                             mojo::PendingReceiver<Interface>)>
BindServiceWorkerReceiverForStorageKey(
    void (RenderProcessHostImpl::*method)(const blink::StorageKey&,
                                          mojo::PendingReceiver<Interface>),
    ServiceWorkerHost* host) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  return base::BindRepeating(
      [](ServiceWorkerHost* host,
         void (RenderProcessHostImpl::*method)(
             const blink::StorageKey&, mojo::PendingReceiver<Interface>),
         const ServiceWorkerVersionBaseInfo& info,
         mojo::PendingReceiver<Interface> receiver) {
        DCHECK_CURRENTLY_ON(BrowserThread::UI);
        auto* process_host = static_cast<RenderProcessHostImpl*>(
            RenderProcessHost::FromID(host->worker_process_id()));
        if (!process_host)
          return;
        (process_host->*method)(info.storage_key, std::move(receiver));
      },
      base::Unretained(host), method);
}

template <typename Interface>
void EmptyBinderForFrame(RenderFrameHost* host,
                         mojo::PendingReceiver<Interface> receiver) {
  DVLOG(1) << "Empty binder for interface " << Interface::Name_
           << " for the frame/document scope";
}

BatteryMonitorBinder& GetBatteryMonitorBinderOverride() {
  static base::NoDestructor<BatteryMonitorBinder> binder;
  return *binder;
}

void BindBatteryMonitor(
    RenderFrameHostImpl* host,
    mojo::PendingReceiver<device::mojom::BatteryMonitor> receiver) {
  const auto& binder = GetBatteryMonitorBinderOverride();
  // TODO(crbug.com/1007264, crbug.com/1290231): remove fenced frame specific
  // code when permission policy implements the battery status API support.
  if (host->IsNestedWithinFencedFrame()) {
    bad_message::ReceivedBadMessage(
        host->GetProcess(), bad_message::BadMessageReason::
                                BIBI_BIND_BATTERY_MONITOR_FOR_FENCED_FRAME);
    return;
  }
  if (binder)
    binder.Run(std::move(receiver));
  else
    GetDeviceService().BindBatteryMonitor(std::move(receiver));
}

DevicePostureProviderBinder& GetDevicePostureProviderBinderOverride() {
  static base::NoDestructor<DevicePostureProviderBinder> binder;
  return *binder;
}

void BindDevicePostureProvider(
    mojo::PendingReceiver<device::mojom::DevicePostureProvider> receiver) {
  const auto& binder = GetDevicePostureProviderBinderOverride();
  if (binder)
    binder.Run(std::move(receiver));
#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_WIN)
  else if (base::FeatureList::IsEnabled(features::kDevicePosture))
    GetDeviceService().BindDevicePostureProvider(std::move(receiver));
#endif
}

VibrationManagerBinder& GetVibrationManagerBinderOverride() {
  static base::NoDestructor<VibrationManagerBinder> binder;
  return *binder;
}

void BindVibrationManager(
    mojo::PendingReceiver<device::mojom::VibrationManager> receiver) {
  const auto& binder = GetVibrationManagerBinderOverride();
  if (binder)
    binder.Run(std::move(receiver));
  else
    GetDeviceService().BindVibrationManager(std::move(receiver));
}

void BindMediaPlayerObserverClientHandler(
    RenderFrameHost* frame_host,
    mojo::PendingReceiver<media::mojom::MediaPlayerObserverClient> receiver) {
  WebContentsImpl* web_contents = static_cast<WebContentsImpl*>(
      WebContents::FromRenderFrameHost(frame_host));
  web_contents->media_web_contents_observer()->BindMediaPlayerObserverClient(
      std::move(receiver));
}

void BindSocketManager(
    RenderFrameHostImpl* frame,
    mojo::PendingReceiver<network::mojom::P2PSocketManager> receiver) {
  static_cast<RenderProcessHostImpl*>(frame->GetProcess())
      ->BindP2PSocketManager(frame->GetNetworkIsolationKey(),
                             std::move(receiver), frame->GetGlobalId());
}

void BindGamepadMonitor(
    RenderFrameHostImpl* frame,
    mojo::PendingReceiver<device::mojom::GamepadMonitor> receiver) {
  // TODO(https://crbug.com/1011006): Remove fenced frame specific code when
  // permission policy implements the Gamepad API support.
  if (frame->IsNestedWithinFencedFrame()) {
    bad_message::ReceivedBadMessage(
        frame->GetProcess(), bad_message::BadMessageReason::
                                 BIBI_BIND_GAMEPAD_MONITOR_FOR_FENCED_FRAME);
    return;
  }
  device::GamepadMonitor::Create(std::move(receiver));
}

void BindGamepadHapticsManager(
    RenderFrameHostImpl* frame,
    mojo::PendingReceiver<device::mojom::GamepadHapticsManager> receiver) {
  // TODO(https://crbug.com/1011006): Remove fenced frame specific code when
  // permission policy implements the Gamepad API support.
  if (frame->IsNestedWithinFencedFrame()) {
    bad_message::ReceivedBadMessage(
        frame->GetProcess(),
        bad_message::BadMessageReason::
            BIBI_BIND_GAMEPAD_HAPTICS_MANAGER_FOR_FENCED_FRAME);
    return;
  }
  device::GamepadHapticsManager::Create(std::move(receiver));
}

}  // namespace

// Documents/frames
void PopulateFrameBinders(RenderFrameHostImpl* host, mojo::BinderMap* map) {
  map->Add<blink::mojom::AudioContextManager>(base::BindRepeating(
      &RenderFrameHostImpl::GetAudioContextManager, base::Unretained(host)));

  map->Add<device::mojom::BatteryMonitor>(
      base::BindRepeating(&BindBatteryMonitor, base::Unretained(host)));

  map->Add<blink::mojom::CacheStorage>(base::BindRepeating(
      &RenderFrameHostImpl::BindCacheStorage, base::Unretained(host)));

  map->Add<blink::mojom::CodeCacheHost>(base::BindRepeating(
      &RenderFrameHostImpl::CreateCodeCacheHost, base::Unretained(host)));

  if (base::FeatureList::IsEnabled(blink::features::kComputePressure)) {
    map->Add<blink::mojom::PressureService>(base::BindRepeating(
        &PressureServiceImpl::Create, base::Unretained(host)));
  }

  map->Add<blink::mojom::ContactsManager>(
      base::BindRepeating(ContactsManagerImpl::Create, base::Unretained(host)));

  map->Add<blink::mojom::ContentSecurityNotifier>(base::BindRepeating(
      [](RenderFrameHostImpl* host,
         mojo::PendingReceiver<blink::mojom::ContentSecurityNotifier>
             receiver) {
        mojo::MakeSelfOwnedReceiver(
            std::make_unique<ContentSecurityNotifier>(host->GetGlobalId()),
            std::move(receiver));
      },
      base::Unretained(host)));

  map->Add<blink::mojom::DedicatedWorkerHostFactory>(base::BindRepeating(
      &RenderFrameHostImpl::CreateDedicatedWorkerHostFactory,
      base::Unretained(host)));

  map->Add<blink::mojom::FeatureObserver>(base::BindRepeating(
      &RenderFrameHostImpl::GetFeatureObserver, base::Unretained(host)));

  map->Add<blink::mojom::FileSystemAccessManager>(
      base::BindRepeating(&RenderFrameHostImpl::GetFileSystemAccessManager,
                          base::Unretained(host)));

  map->Add<blink::mojom::FileSystemManager>(base::BindRepeating(
      &RenderFrameHostImpl::GetFileSystemManager, base::Unretained(host)));

  if (base::FeatureList::IsEnabled(blink::features::kFontAccess)) {
    map->Add<blink::mojom::FontAccessManager>(base::BindRepeating(
        &RenderFrameHostImpl::GetFontAccessManager, base::Unretained(host)));
  }

  map->Add<device::mojom::GamepadHapticsManager>(
      base::BindRepeating(&BindGamepadHapticsManager, base::Unretained(host)));

  map->Add<blink::mojom::GeolocationService>(base::BindRepeating(
      &RenderFrameHostImpl::GetGeolocationService, base::Unretained(host)));

  map->Add<blink::mojom::IdleManager>(base::BindRepeating(
      &RenderFrameHostImpl::BindIdleManager, base::Unretained(host)));

#if BUILDFLAG(ENABLE_MDNS)
  map->Add<network::mojom::MdnsResponder>(base::BindRepeating(
      &RenderFrameHostImpl::CreateMdnsResponder, base::Unretained(host)));
#endif  // BUILDFLAG(ENABLE_MDNS)

  // BrowserMainLoop::GetInstance() may be null on unit tests.
  if (BrowserMainLoop::GetInstance()) {
    map->Add<midi::mojom::MidiSessionProvider>(
        base::BindRepeating(&MidiHost::BindReceiver,
                            host->GetProcess()->GetID(),
                            BrowserMainLoop::GetInstance()->midi_service()),
        GetIOThreadTaskRunner({}));
  }

  map->Add<media::mojom::MediaPlayerObserverClient>(base::BindRepeating(
      &BindMediaPlayerObserverClientHandler, base::Unretained(host)));

  map->Add<blink::mojom::NotificationService>(base::BindRepeating(
      &RenderFrameHostImpl::CreateNotificationService, base::Unretained(host)));

  map->Add<network::mojom::P2PSocketManager>(
      base::BindRepeating(&BindSocketManager, base::Unretained(host)));

  map->Add<blink::mojom::PeerConnectionTrackerHost>(
      base::BindRepeating(&RenderFrameHostImpl::BindPeerConnectionTrackerHost,
                          base::Unretained(host)));

  map->Add<blink::mojom::PermissionService>(base::BindRepeating(
      &RenderFrameHostImpl::CreatePermissionService, base::Unretained(host)));

  map->Add<blink::mojom::PresentationService>(base::BindRepeating(
      &RenderFrameHostImpl::GetPresentationService, base::Unretained(host)));

  map->Add<blink::mojom::QuotaManagerHost>(
      base::BindRepeating(&BindQuotaManagerHost, base::Unretained(host)));

  map->Add<blink::mojom::ReportingServiceProxy>(base::BindRepeating(
      &CreateReportingServiceProxyForFrame, base::Unretained(host)));

  map->Add<blink::mojom::SharedWorkerConnector>(
      base::BindRepeating(&BindSharedWorkerConnector, base::Unretained(host)));

  map->Add<blink::mojom::SpeechRecognizer>(
      base::BindRepeating(&SpeechRecognitionDispatcherHost::Create,
                          host->GetProcess()->GetID(), host->GetRoutingID()),
      GetIOThreadTaskRunner({}));

  map->Add<blink::mojom::SpeechSynthesis>(base::BindRepeating(
      &RenderFrameHostImpl::GetSpeechSynthesis, base::Unretained(host)));

#if !BUILDFLAG(IS_ANDROID)
  map->Add<blink::mojom::DeviceAPIService>(base::BindRepeating(
      &RenderFrameHostImpl::GetDeviceInfoService, base::Unretained(host)));
  map->Add<blink::mojom::ManagedConfigurationService>(
      base::BindRepeating(&RenderFrameHostImpl::GetManagedConfigurationService,
                          base::Unretained(host)));
#endif  // !BUILDFLAG(IS_ANDROID)

  if (base::FeatureList::IsEnabled(features::kWebOTP)) {
    map->Add<blink::mojom::WebOTPService>(
        base::BindRepeating(&RenderFrameHostImpl::BindWebOTPServiceReceiver,
                            base::Unretained(host)));
  }

  map->Add<blink::mojom::FederatedAuthRequest>(base::BindRepeating(
      &RenderFrameHostImpl::BindFederatedAuthRequestReceiver,
      base::Unretained(host)));

  map->Add<blink::mojom::WebUsbService>(base::BindRepeating(
      &RenderFrameHostImpl::CreateWebUsbService, base::Unretained(host)));

  map->Add<blink::mojom::WebSocketConnector>(base::BindRepeating(
      &RenderFrameHostImpl::CreateWebSocketConnector, base::Unretained(host)));

  map->Add<blink::mojom::LockManager>(base::BindRepeating(
      &RenderFrameHostImpl::CreateLockManager, base::Unretained(host)));

  map->Add<blink::mojom::NativeIOHost>(
      base::BindRepeating(&BindNativeIOHost, base::Unretained(host)));

  map->Add<blink::mojom::IDBFactory>(base::BindRepeating(
      &RenderFrameHostImpl::CreateIDBFactory, base::Unretained(host)));

  map->Add<blink::mojom::BucketManagerHost>(base::BindRepeating(
      &RenderFrameHostImpl::CreateBucketManagerHost, base::Unretained(host)));

  map->Add<blink::mojom::FileChooser>(
      base::BindRepeating(&FileChooserImpl::Create, base::Unretained(host)));

  map->Add<blink::mojom::FileUtilitiesHost>(
      base::BindRepeating(FileUtilitiesHostImpl::Create,
                          host->GetProcess()->GetID()),
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE}));

  map->Add<device::mojom::GamepadMonitor>(
      base::BindRepeating(&BindGamepadMonitor, base::Unretained(host)));

  map->Add<device::mojom::SensorProvider>(base::BindRepeating(
      &RenderFrameHostImpl::GetSensorProvider, base::Unretained(host)));

  map->Add<device::mojom::VibrationManager>(
      base::BindRepeating(&BindVibrationManager));

  map->Add<payments::mojom::PaymentManager>(base::BindRepeating(
      &RenderFrameHostImpl::CreatePaymentManager, base::Unretained(host)));

  map->Add<handwriting::mojom::HandwritingRecognitionService>(
      base::BindRepeating(&CreateHandwritingRecognitionService));

  if (base::FeatureList::IsEnabled(
          features::kEnableMachineLearningModelLoaderWebPlatformApi)) {
    map->Add<ml::model_loader::mojom::MLService>(
        base::BindRepeating(&CreateMLService));
  }

  if (base::FeatureList::IsEnabled(blink::features::kPendingBeaconAPI)) {
    map->Add<blink::mojom::PendingBeaconHost>(base::BindRepeating(
        &RenderFrameHostImpl::GetPendingBeaconHost, base::Unretained(host)));
  }

  map->Add<blink::mojom::WebBluetoothService>(base::BindRepeating(
      &RenderFrameHostImpl::CreateWebBluetoothService, base::Unretained(host)));

  map->Add<blink::mojom::PushMessaging>(base::BindRepeating(
      &RenderFrameHostImpl::GetPushMessaging, base::Unretained(host)));

  map->Add<blink::mojom::WebTransportConnector>(
      base::BindRepeating(&RenderFrameHostImpl::CreateWebTransportConnector,
                          base::Unretained(host)));

  map->Add<blink::mojom::Authenticator>(
      base::BindRepeating(&RenderFrameHostImpl::GetWebAuthenticationService,
                          base::Unretained(host)));

  map->Add<blink::test::mojom::VirtualAuthenticatorManager>(
      base::BindRepeating(&RenderFrameHostImpl::GetVirtualAuthenticatorManager,
                          base::Unretained(host)));

  map->Add<device::mojom::DevicePostureProvider>(
      base::BindRepeating(&BindDevicePostureProvider));

  // BrowserMainLoop::GetInstance() may be null on unit tests.
  if (BrowserMainLoop::GetInstance()) {
    // BrowserMainLoop, which owns MediaStreamManager, is alive for the lifetime
    // of Mojo communication (see BrowserMainLoop::ShutdownThreadsAndCleanUp(),
    // which shuts down Mojo). Hence, passing that MediaStreamManager instance
    // as a raw pointer here is safe.
    MediaStreamManager* media_stream_manager =
        BrowserMainLoop::GetInstance()->media_stream_manager();

    map->Add<blink::mojom::MediaDevicesDispatcherHost>(
        base::BindRepeating(&MediaDevicesDispatcherHost::Create,
                            host->GetProcess()->GetID(), host->GetRoutingID(),
                            base::Unretained(media_stream_manager)),
        GetIOThreadTaskRunner({}));

    map->Add<blink::mojom::MediaStreamDispatcherHost>(
        base::BindRepeating(&MediaStreamDispatcherHost::Create,
                            host->GetProcess()->GetID(), host->GetRoutingID(),
                            base::Unretained(media_stream_manager)),
        GetIOThreadTaskRunner({}));

    map->Add<media::mojom::VideoCaptureHost>(
        base::BindRepeating(&VideoCaptureHost::Create,
                            host->GetProcess()->GetID(),
                            base::Unretained(media_stream_manager)),
        GetIOThreadTaskRunner({}));
  }

  map->Add<blink::mojom::RendererAudioInputStreamFactory>(
      base::BindRepeating(&RenderFrameHostImpl::CreateAudioInputStreamFactory,
                          base::Unretained(host)));

  map->Add<blink::mojom::RendererAudioOutputStreamFactory>(
      base::BindRepeating(&RenderFrameHostImpl::CreateAudioOutputStreamFactory,
                          base::Unretained(host)));

  map->Add<media::mojom::ImageCapture>(
      base::BindRepeating(&ImageCaptureImpl::Create, base::Unretained(host)));

  map->Add<media::mojom::InterfaceFactory>(base::BindRepeating(
      &RenderFrameHostImpl::BindMediaInterfaceFactoryReceiver,
      base::Unretained(host)));

  map->Add<media::mojom::MediaMetricsProvider>(base::BindRepeating(
      &RenderFrameHostImpl::BindMediaMetricsProviderReceiver,
      base::Unretained(host)));

  map->Add<media::mojom::WebrtcVideoPerfRecorder>(base::BindRepeating(
      [](RenderFrameHostImpl* host,
         mojo::PendingReceiver<media::mojom::WebrtcVideoPerfRecorder>
             receiver) {
        DCHECK_CURRENTLY_ON(BrowserThread::UI);
        media::WebrtcVideoPerfRecorder::Create(
            BrowserContextImpl::From(host->GetBrowserContext())
                ->GetWebrtcVideoPerfHistory(),
            std::move(receiver));
      },
      base::Unretained(host)));

  map->Add<media::mojom::WebrtcVideoPerfHistory>(base::BindRepeating(
      [](RenderFrameHostImpl* host,
         mojo::PendingReceiver<media::mojom::WebrtcVideoPerfHistory> receiver) {
        DCHECK_CURRENTLY_ON(BrowserThread::UI);
        BrowserContextImpl::From(host->GetBrowserContext())
            ->GetWebrtcVideoPerfHistory()
            ->BindReceiver(std::move(receiver));
      },
      base::Unretained(host)));

#if BUILDFLAG(ENABLE_MEDIA_REMOTING)
  map->Add<media::mojom::RemoterFactory>(
      base::BindRepeating(&RenderFrameHostImpl::BindMediaRemoterFactoryReceiver,
                          base::Unretained(host)));
#endif

  map->Add<blink::mojom::OneShotBackgroundSyncService>(base::BindRepeating(
      [](RenderFrameHostImpl* host,
         mojo::PendingReceiver<blink::mojom::OneShotBackgroundSyncService>
             receiver) {
        host->GetProcess()->CreateOneShotSyncService(
            host->storage_key().origin(), std::move(receiver));
      },
      base::Unretained(host)));

  map->Add<blink::mojom::PeriodicBackgroundSyncService>(base::BindRepeating(
      [](RenderFrameHostImpl* host,
         mojo::PendingReceiver<blink::mojom::PeriodicBackgroundSyncService>
             receiver) {
        host->GetProcess()->CreatePeriodicSyncService(
            host->storage_key().origin(), std::move(receiver));
      },
      base::Unretained(host)));

  map->Add<media::mojom::VideoDecodePerfHistory>(
      base::BindRepeating(&RenderProcessHost::BindVideoDecodePerfHistory,
                          base::Unretained(host->GetProcess())));

  map->Add<network::mojom::RestrictedCookieManager>(
      base::BindRepeating(&RenderFrameHostImpl::BindRestrictedCookieManager,
                          base::Unretained(host)));

  map->Add<network::mojom::TrustTokenQueryAnswerer>(
      base::BindRepeating(&RenderFrameHostImpl::BindTrustTokenQueryAnswerer,
                          base::Unretained(host)));

  map->Add<shape_detection::mojom::BarcodeDetectionProvider>(
      base::BindRepeating(&BindBarcodeDetectionProvider));

  map->Add<shape_detection::mojom::FaceDetectionProvider>(
      base::BindRepeating(&BindFaceDetectionProvider));

  map->Add<shape_detection::mojom::TextDetection>(
      base::BindRepeating(&BindTextDetection));

  auto* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(cc::switches::kEnableGpuBenchmarking)) {
    map->Add<mojom::InputInjector>(
        base::BindRepeating(&RenderFrameHostImpl::BindInputInjectorReceiver,
                            base::Unretained(host)));
  }

#if BUILDFLAG(IS_ANDROID)
  if (base::FeatureList::IsEnabled(features::kWebNfc)) {
    map->Add<device::mojom::NFC>(base::BindRepeating(
        &RenderFrameHostImpl::BindNFCReceiver, base::Unretained(host)));
  }
#else
  map->Add<blink::mojom::HidService>(base::BindRepeating(
      &RenderFrameHostImpl::GetHidService, base::Unretained(host)));

  map->Add<blink::mojom::InstalledAppProvider>(
      base::BindRepeating(&RenderFrameHostImpl::CreateInstalledAppProvider,
                          base::Unretained(host)));

  map->Add<blink::mojom::SerialService>(base::BindRepeating(
      &RenderFrameHostImpl::BindSerialService, base::Unretained(host)));
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_MAC)
  map->Add<blink::mojom::TextInputHost>(
      base::BindRepeating(&BindTextInputHost));
#endif

  map->Add<blink::mojom::RenderAccessibilityHost>(
      base::BindRepeating(&RenderFrameHostImpl::BindRenderAccessibilityHost,
                          base::Unretained(host)));
}

void PopulateBinderMapWithContext(
    RenderFrameHostImpl* host,
    mojo::BinderMapWithContext<RenderFrameHost*>* map) {
  // Register empty binders for interfaces not bound by content but requested
  // by blink.
  // This avoids renderer kills when no binder is found in the absence of the
  // production embedder (such as in tests).
  map->Add<blink::mojom::NoStatePrefetchProcessor>(base::BindRepeating(
      &EmptyBinderForFrame<blink::mojom::NoStatePrefetchProcessor>));
  map->Add<payments::mojom::PaymentCredential>(base::BindRepeating(
      &EmptyBinderForFrame<payments::mojom::PaymentCredential>));
  map->Add<payments::mojom::PaymentRequest>(base::BindRepeating(
      &EmptyBinderForFrame<payments::mojom::PaymentRequest>));
  map->Add<blink::mojom::AnchorElementMetricsHost>(base::BindRepeating(
      &EmptyBinderForFrame<blink::mojom::AnchorElementMetricsHost>));
  if (base::FeatureList::IsEnabled(
          blink::features::kAnchorElementInteraction)) {
    map->Add<blink::mojom::AnchorElementInteractionHost>(base::BindRepeating(
        &EmptyBinderForFrame<blink::mojom::AnchorElementInteractionHost>));
  }
  map->Add<blink::mojom::CredentialManager>(base::BindRepeating(
      &EmptyBinderForFrame<blink::mojom::CredentialManager>));
  if (base::FeatureList::IsEnabled(blink::features::kBrowsingTopics)) {
    map->Add<blink::mojom::BrowsingTopicsDocumentService>(
        base::BindRepeating(&BrowsingTopicsDocumentHost::CreateMojoService));
  }
#if !BUILDFLAG(IS_ANDROID)
  if (SiteIsolationPolicy::IsApplicationIsolationLevelEnabled()) {
    map->Add<blink::mojom::DirectSocketsService>(
        base::BindRepeating(&DirectSocketsServiceImpl::CreateForFrame));
  }
  map->Add<media::mojom::SpeechRecognitionContext>(base::BindRepeating(
      &EmptyBinderForFrame<media::mojom::SpeechRecognitionContext>));
  map->Add<media::mojom::SpeechRecognitionClientBrowserInterface>(
      base::BindRepeating(
          &EmptyBinderForFrame<
              media::mojom::SpeechRecognitionClientBrowserInterface>));
  map->Add<media::mojom::MediaFoundationRendererNotifier>(base::BindRepeating(
      &EmptyBinderForFrame<media::mojom::MediaFoundationRendererNotifier>));
  map->Add<media::mojom::MediaPlayerObserverClient>(base::BindRepeating(
      &EmptyBinderForFrame<media::mojom::MediaPlayerObserverClient>));
#endif
#if BUILDFLAG(ENABLE_UNHANDLED_TAP)
  map->Add<blink::mojom::UnhandledTapNotifier>(base::BindRepeating(
      &EmptyBinderForFrame<blink::mojom::UnhandledTapNotifier>));
#endif

  map->Add<blink::mojom::BackgroundFetchService>(
      base::BindRepeating(&BackgroundFetchServiceImpl::CreateForFrame));
  map->Add<blink::mojom::ColorChooserFactory>(
      base::BindRepeating(&BindColorChooserFactoryForFrame));
  map->Add<blink::mojom::EyeDropperChooser>(
      base::BindRepeating(&EyeDropperChooserImpl::Create));
  map->Add<blink::mojom::CookieStore>(
      base::BindRepeating(&CookieStoreManager::BindReceiverForFrame));
  map->Add<blink::mojom::ContentIndexService>(
      base::BindRepeating(&ContentIndexServiceImpl::CreateForFrame));
  map->Add<blink::mojom::KeyboardLockService>(
      base::BindRepeating(&KeyboardLockServiceImpl::CreateMojoService));
  if (base::FeatureList::IsEnabled(blink::features::kInterestGroupStorage)) {
    map->Add<blink::mojom::AdAuctionService>(
        base::BindRepeating(&AdAuctionServiceImpl::CreateMojoService));
  }
  map->Add<blink::mojom::MediaSessionService>(
      base::BindRepeating(&MediaSessionServiceImpl::Create));
  map->Add<blink::mojom::PictureInPictureService>(
      base::BindRepeating(&PictureInPictureServiceImpl::Create));
  map->Add<blink::mojom::WakeLockService>(
      base::BindRepeating(&WakeLockServiceImpl::Create));
#if BUILDFLAG(ENABLE_VR)
  map->Add<device::mojom::VRService>(
      base::BindRepeating(&VRServiceImpl::Create));
#else
  map->Add<device::mojom::VRService>(
      base::BindRepeating(&EmptyBinderForFrame<device::mojom::VRService>));
#endif
  map->Add<attribution_internals::mojom::Handler>(
      base::BindRepeating(&BindAttributionInternalsHandler));
  map->Add<mojom::PrerenderInternalsHandler>(
      base::BindRepeating(&BindPrerenderInternalsHandler));
  map->Add<::mojom::ProcessInternalsHandler>(
      base::BindRepeating(&BindProcessInternalsHandler));
  map->Add<storage::mojom::QuotaInternalsHandler>(
      base::BindRepeating(&BindQuotaInternalsHandler));
#if BUILDFLAG(IS_ANDROID)
  map->Add<blink::mojom::DateTimeChooser>(
      base::BindRepeating(&BindDateTimeChooserForFrame));
  map->Add<blink::mojom::TextSuggestionHost>(
      base::BindRepeating(&BindTextSuggestionHostForFrame));
#else
  map->Add<blink::mojom::TextSuggestionHost>(base::BindRepeating(
      &EmptyBinderForFrame<blink::mojom::TextSuggestionHost>));
#endif  // BUILDFLAG(IS_ANDROID)

  map->Add<blink::mojom::ClipboardHost>(
      base::BindRepeating(&ClipboardHostImpl::Create));
  map->Add<blink::mojom::SpeculationHost>(
      base::BindRepeating(&SpeculationHostImpl::Bind));
  GetContentClient()->browser()->RegisterBrowserInterfaceBindersForFrame(host,
                                                                         map);

#if BUILDFLAG(IS_CHROMEOS)
  if (base::FeatureList::IsEnabled(features::kWebLockScreenApi)) {
    map->Add<blink::mojom::LockScreenService>(
        base::BindRepeating(&LockScreenServiceImpl::Create));
  }
#endif

#if BUILDFLAG(IS_FUCHSIA)
  map->Add<media::mojom::FuchsiaMediaResourceProvider>(
      base::BindRepeating(&MediaResourceProviderFuchsia::Bind));
#endif
}

void PopulateBinderMap(RenderFrameHostImpl* host, mojo::BinderMap* map) {
  PopulateFrameBinders(host, map);
}

RenderFrameHost* GetContextForHost(RenderFrameHostImpl* host) {
  return host;
}

// Dedicated workers
const url::Origin& GetContextForHost(DedicatedWorkerHost* host) {
  return host->GetStorageKey().origin();
}

void PopulateDedicatedWorkerBinders(DedicatedWorkerHost* host,
                                    mojo::BinderMap* map) {
  // Do nothing for interfaces that the renderer might request, but doesn't
  // always expect to be bound.
  map->Add<blink::mojom::FeatureObserver>(base::DoNothing());

  // static binders
  map->Add<shape_detection::mojom::BarcodeDetectionProvider>(
      base::BindRepeating(&BindBarcodeDetectionProvider));
  map->Add<shape_detection::mojom::FaceDetectionProvider>(
      base::BindRepeating(&BindFaceDetectionProvider));
  map->Add<shape_detection::mojom::TextDetection>(
      base::BindRepeating(&BindTextDetection));
  map->Add<ukm::mojom::UkmRecorderInterface>(
      base::BindRepeating(&BindUkmRecorderInterface));

  // worker host binders
  // base::Unretained(host) is safe because the map is owned by
  // |DedicatedWorkerHost::broker_|.
  map->Add<blink::mojom::IdleManager>(base::BindRepeating(
      &DedicatedWorkerHost::CreateIdleManager, base::Unretained(host)));
  map->Add<blink::mojom::DedicatedWorkerHostFactory>(
      base::BindRepeating(&DedicatedWorkerHost::CreateNestedDedicatedWorker,
                          base::Unretained(host)));

  map->Add<blink::mojom::FileUtilitiesHost>(
      base::BindRepeating(FileUtilitiesHostImpl::Create,
                          host->GetProcessHost()->GetID()),
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE}));

  map->Add<blink::mojom::WebUsbService>(base::BindRepeating(
      &DedicatedWorkerHost::CreateWebUsbService, base::Unretained(host)));
  map->Add<blink::mojom::WebSocketConnector>(base::BindRepeating(
      &DedicatedWorkerHost::CreateWebSocketConnector, base::Unretained(host)));
  map->Add<blink::mojom::WebTransportConnector>(
      base::BindRepeating(&DedicatedWorkerHost::CreateWebTransportConnector,
                          base::Unretained(host)));
  map->Add<blink::mojom::WakeLockService>(base::BindRepeating(
      &DedicatedWorkerHost::CreateWakeLockService, base::Unretained(host)));
  map->Add<blink::mojom::ContentSecurityNotifier>(
      base::BindRepeating(&DedicatedWorkerHost::CreateContentSecurityNotifier,
                          base::Unretained(host)));
  map->Add<blink::mojom::CacheStorage>(base::BindRepeating(
      &DedicatedWorkerHost::BindCacheStorage, base::Unretained(host)));
  map->Add<blink::mojom::CodeCacheHost>(base::BindRepeating(
      &DedicatedWorkerHost::CreateCodeCacheHost, base::Unretained(host)));
  map->Add<blink::mojom::BroadcastChannelProvider>(
      base::BindRepeating(&DedicatedWorkerHost::CreateBroadcastChannelProvider,
                          base::Unretained(host)));
  map->Add<blink::mojom::ReportingServiceProxy>(base::BindRepeating(
      &CreateReportingServiceProxyForDedicatedWorker, base::Unretained(host)));
#if !BUILDFLAG(IS_ANDROID)
  map->Add<blink::mojom::SerialService>(base::BindRepeating(
      &DedicatedWorkerHost::BindSerialService, base::Unretained(host)));
#endif  // !BUILDFLAG(IS_ANDROID)

  // RenderProcessHost binders
  map->Add<media::mojom::VideoDecodePerfHistory>(BindWorkerReceiver(
      &RenderProcessHostImpl::BindVideoDecodePerfHistory, host));
  map->Add<media::mojom::WebrtcVideoPerfHistory>(BindWorkerReceiver(
      &RenderProcessHostImpl::BindWebrtcVideoPerfHistory, host));

  // RenderProcessHost binders taking a StorageKey
  map->Add<blink::mojom::FileSystemAccessManager>(
      BindWorkerReceiverForStorageKey(
          &RenderProcessHostImpl::BindFileSystemAccessManager, host));
  map->Add<blink::mojom::FileSystemManager>(BindWorkerReceiverForStorageKey(
      &RenderProcessHostImpl::BindFileSystemManager, host));
  map->Add<blink::mojom::IDBFactory>(BindWorkerReceiverForStorageKey(
      &RenderProcessHostImpl::BindIndexedDB, host));
  map->Add<blink::mojom::NativeIOHost>(BindWorkerReceiverForStorageKey(
      &RenderProcessHostImpl::BindNativeIOHost, host));
  map->Add<blink::mojom::LockManager>(BindWorkerReceiverForStorageKey(
      &RenderProcessHostImpl::CreateLockManager, host));
  map->Add<blink::mojom::QuotaManagerHost>(BindWorkerReceiverForStorageKey(
      &RenderProcessHostImpl::BindQuotaManagerHost, host));
}

void PopulateBinderMapWithContext(
    DedicatedWorkerHost* host,
    mojo::BinderMapWithContext<const url::Origin&>* map) {
  // RenderProcessHost binders taking an origin
  map->Add<payments::mojom::PaymentManager>(BindWorkerReceiverForOrigin(
      &RenderProcessHostImpl::CreatePaymentManagerForOrigin, host));
  map->Add<blink::mojom::PermissionService>(BindWorkerReceiverForOrigin(
      &RenderProcessHostImpl::CreatePermissionService, host));
  map->Add<blink::mojom::BucketManagerHost>(BindWorkerReceiverForOrigin(
      &RenderProcessHostImpl::BindBucketManagerHostForWorker, host));

  // RenderProcessHost binders taking a frame id and an origin
  map->Add<blink::mojom::NotificationService>(
      BindWorkerReceiverForOriginAndFrameId(
          &RenderProcessHostImpl::CreateNotificationService, host));
}

void PopulateBinderMap(DedicatedWorkerHost* host, mojo::BinderMap* map) {
  PopulateDedicatedWorkerBinders(host, map);
}

// Shared workers
url::Origin GetContextForHost(SharedWorkerHost* host) {
  return url::Origin::Create(host->instance().url());
}

void PopulateSharedWorkerBinders(SharedWorkerHost* host, mojo::BinderMap* map) {
  // Do nothing for interfaces that the renderer might request, but doesn't
  // always expect to be bound.
  map->Add<blink::mojom::FeatureObserver>(base::DoNothing());
  // Ignore the pending receiver because it's not clear how to handle
  // notifications about content security (e.g., mixed contents and certificate
  // errors) on shared workers. Generally these notifications are routed to the
  // ancestor frame's WebContents like dedicated workers, but shared workers
  // don't have the ancestor frame.
  map->Add<blink::mojom::ContentSecurityNotifier>(base::DoNothing());

  // static binders
  map->Add<shape_detection::mojom::BarcodeDetectionProvider>(
      base::BindRepeating(&BindBarcodeDetectionProvider));
  map->Add<shape_detection::mojom::FaceDetectionProvider>(
      base::BindRepeating(&BindFaceDetectionProvider));
  map->Add<shape_detection::mojom::TextDetection>(
      base::BindRepeating(&BindTextDetection));
  map->Add<ukm::mojom::UkmRecorderInterface>(
      base::BindRepeating(&BindUkmRecorderInterface));

  // worker host binders
  // base::Unretained(host) is safe because the map is owned by
  // |SharedWorkerHost::broker_|.
  map->Add<blink::mojom::FileUtilitiesHost>(
      base::BindRepeating(FileUtilitiesHostImpl::Create,
                          host->GetProcessHost()->GetID()),
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE}));

  map->Add<blink::mojom::WebTransportConnector>(base::BindRepeating(
      &SharedWorkerHost::CreateWebTransportConnector, base::Unretained(host)));
  map->Add<blink::mojom::CacheStorage>(base::BindRepeating(
      &SharedWorkerHost::BindCacheStorage, base::Unretained(host)));
  map->Add<blink::mojom::CodeCacheHost>(base::BindRepeating(
      &SharedWorkerHost::CreateCodeCacheHost, base::Unretained(host)));
  map->Add<blink::mojom::BroadcastChannelProvider>(
      base::BindRepeating(&SharedWorkerHost::CreateBroadcastChannelProvider,
                          base::Unretained(host)));
  map->Add<blink::mojom::ReportingServiceProxy>(base::BindRepeating(
      &CreateReportingServiceProxyForSharedWorker, base::Unretained(host)));

  // RenderProcessHost binders
  map->Add<media::mojom::VideoDecodePerfHistory>(BindWorkerReceiver(
      &RenderProcessHostImpl::BindVideoDecodePerfHistory, host));
  map->Add<media::mojom::WebrtcVideoPerfHistory>(BindWorkerReceiver(
      &RenderProcessHostImpl::BindWebrtcVideoPerfHistory, host));

  // RenderProcessHost binders taking a StorageKey
  map->Add<blink::mojom::FileSystemAccessManager>(
      BindWorkerReceiverForStorageKey(
          &RenderProcessHostImpl::BindFileSystemAccessManager, host));
  map->Add<blink::mojom::FileSystemManager>(BindWorkerReceiverForStorageKey(
      &RenderProcessHostImpl::BindFileSystemManager, host));
  map->Add<blink::mojom::IDBFactory>(BindWorkerReceiverForStorageKey(
      &RenderProcessHostImpl::BindIndexedDB, host));
  map->Add<blink::mojom::NativeIOHost>(BindWorkerReceiverForStorageKey(
      &RenderProcessHostImpl::BindNativeIOHost, host));
  map->Add<blink::mojom::WebSocketConnector>(BindWorkerReceiverForStorageKey(
      &RenderProcessHostImpl::CreateWebSocketConnector, host));
  map->Add<blink::mojom::LockManager>(BindWorkerReceiverForStorageKey(
      &RenderProcessHostImpl::CreateLockManager, host));
  map->Add<blink::mojom::QuotaManagerHost>(BindWorkerReceiverForStorageKey(
      &RenderProcessHostImpl::BindQuotaManagerHost, host));
}

void PopulateBinderMapWithContext(
    SharedWorkerHost* host,
    mojo::BinderMapWithContext<const url::Origin&>* map) {
  // RenderProcessHost binders taking an origin
  map->Add<payments::mojom::PaymentManager>(BindWorkerReceiverForOrigin(
      &RenderProcessHostImpl::CreatePaymentManagerForOrigin, host));
  map->Add<blink::mojom::PermissionService>(BindWorkerReceiverForOrigin(
      &RenderProcessHostImpl::CreatePermissionService, host));
  map->Add<blink::mojom::BucketManagerHost>(BindWorkerReceiverForOrigin(
      &RenderProcessHostImpl::BindBucketManagerHostForWorker, host));

  // RenderProcessHost binders taking a frame id and an origin
  map->Add<blink::mojom::NotificationService>(
      BindWorkerReceiverForOriginAndFrameId(
          &RenderProcessHostImpl::CreateNotificationService, host));
}

void PopulateBinderMap(SharedWorkerHost* host, mojo::BinderMap* map) {
  PopulateSharedWorkerBinders(host, map);
}

// Service workers
ServiceWorkerVersionInfo GetContextForHost(ServiceWorkerHost* host) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  return host->version()->GetInfo();
}

void PopulateServiceWorkerBinders(ServiceWorkerHost* host,
                                  mojo::BinderMap* map) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  // Do nothing for interfaces that the renderer might request, but doesn't
  // always expect to be bound.
  map->Add<blink::mojom::FeatureObserver>(base::DoNothing());
  // Ignore the pending receiver because it's not clear how to handle
  // notifications about content security (e.g., mixed contents and certificate
  // errors) on service workers. Generally these notifications are routed to the
  // ancestor frame's WebContents like dedicated workers, but service workers
  // don't have the ancestor frame.
  map->Add<blink::mojom::ContentSecurityNotifier>(base::DoNothing());

  // static binders
  map->Add<blink::mojom::FileUtilitiesHost>(
      base::BindRepeating(&BindFileUtilitiesHost, host));
  map->Add<shape_detection::mojom::BarcodeDetectionProvider>(
      base::BindRepeating(&BindBarcodeDetectionProvider));
  map->Add<shape_detection::mojom::FaceDetectionProvider>(
      base::BindRepeating(&BindFaceDetectionProvider));
  map->Add<shape_detection::mojom::TextDetection>(
      base::BindRepeating(&BindTextDetection));
  map->Add<ukm::mojom::UkmRecorderInterface>(
      base::BindRepeating(&BindUkmRecorderInterface));

  // worker host binders
  map->Add<blink::mojom::WebTransportConnector>(base::BindRepeating(
      &ServiceWorkerHost::CreateWebTransportConnector, base::Unretained(host)));
  map->Add<blink::mojom::CacheStorage>(base::BindRepeating(
      &ServiceWorkerHost::BindCacheStorage, base::Unretained(host)));
  map->Add<blink::mojom::CodeCacheHost>(base::BindRepeating(
      &ServiceWorkerHost::CreateCodeCacheHost, base::Unretained(host)));
  map->Add<blink::mojom::BroadcastChannelProvider>(
      base::BindRepeating(&ServiceWorkerHost::CreateBroadcastChannelProvider,
                          base::Unretained(host)));
  map->Add<blink::mojom::ReportingServiceProxy>(base::BindRepeating(
      &CreateReportingServiceProxyForServiceWorker, base::Unretained(host)));
#if !BUILDFLAG(IS_ANDROID)
  map->Add<blink::mojom::HidService>(base::BindRepeating(
      &ServiceWorkerHost::BindHidService, base::Unretained(host)));
#endif

  // RenderProcessHost binders
  map->Add<media::mojom::VideoDecodePerfHistory>(BindServiceWorkerReceiver(
      &RenderProcessHostImpl::BindVideoDecodePerfHistory, host));
  map->Add<media::mojom::WebrtcVideoPerfHistory>(BindServiceWorkerReceiver(
      &RenderProcessHostImpl::BindWebrtcVideoPerfHistory, host));
  map->Add<blink::mojom::PushMessaging>(BindServiceWorkerReceiver(
      &RenderProcessHostImpl::BindPushMessaging, host));
}

void PopulateBinderMapWithContext(
    ServiceWorkerHost* host,
    mojo::BinderMapWithContext<const ServiceWorkerVersionBaseInfo&>* map) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  // static binders
  // Use a task runner if ServiceWorkerHost lives on the IO thread, as
  // CreateForWorker() needs to be called on the UI thread.
  map->Add<blink::mojom::BackgroundFetchService>(
      base::BindRepeating(&BackgroundFetchServiceImpl::CreateForWorker,
                          host->GetNetworkIsolationKey()));
  map->Add<blink::mojom::ContentIndexService>(
      base::BindRepeating(&ContentIndexServiceImpl::CreateForWorker));
  map->Add<blink::mojom::CookieStore>(
      base::BindRepeating(&CookieStoreManager::BindReceiverForWorker));

  // RenderProcessHost binders taking an origin
  map->Add<payments::mojom::PaymentManager>(BindServiceWorkerReceiverForOrigin(
      &RenderProcessHostImpl::CreatePaymentManagerForOrigin, host));
  map->Add<blink::mojom::PermissionService>(BindServiceWorkerReceiverForOrigin(
      &RenderProcessHostImpl::CreatePermissionService, host));
  map->Add<network::mojom::RestrictedCookieManager>(
      BindServiceWorkerReceiverForStorageKey(
          &RenderProcessHostImpl::BindRestrictedCookieManagerForServiceWorker,
          host));
  map->Add<blink::mojom::BucketManagerHost>(BindServiceWorkerReceiverForOrigin(
      &RenderProcessHostImpl::BindBucketManagerHostForWorker, host));
  map->Add<blink::mojom::OneShotBackgroundSyncService>(
      BindServiceWorkerReceiverForOrigin(
          &RenderProcessHostImpl::CreateOneShotSyncService, host));
  map->Add<blink::mojom::PeriodicBackgroundSyncService>(
      BindServiceWorkerReceiverForOrigin(
          &RenderProcessHostImpl::CreatePeriodicSyncService, host));

  // RenderProcessHost binders taking a storage key
  map->Add<blink::mojom::NativeIOHost>(BindServiceWorkerReceiverForStorageKey(
      &RenderProcessHostImpl::BindNativeIOHost, host));
  map->Add<blink::mojom::IDBFactory>(BindServiceWorkerReceiverForStorageKey(
      &RenderProcessHostImpl::BindIndexedDB, host));
  map->Add<blink::mojom::FileSystemAccessManager>(
      BindServiceWorkerReceiverForStorageKey(
          &RenderProcessHostImpl::BindFileSystemAccessManager, host));
  map->Add<blink::mojom::WebSocketConnector>(
      BindServiceWorkerReceiverForStorageKey(
          &RenderProcessHostImpl::CreateWebSocketConnector, host));
  map->Add<blink::mojom::LockManager>(BindServiceWorkerReceiverForStorageKey(
      &RenderProcessHostImpl::CreateLockManager, host));
  map->Add<blink::mojom::QuotaManagerHost>(
      BindServiceWorkerReceiverForStorageKey(
          &RenderProcessHostImpl::BindQuotaManagerHost, host));

  // RenderProcessHost binders taking a frame id and an origin
  map->Add<blink::mojom::NotificationService>(
      BindServiceWorkerReceiverForOriginAndFrameId(
          &RenderProcessHostImpl::CreateNotificationService, host));

  // This is called when `host` is constructed. ServiceWorkerVersion, which
  // constructs `host`, checks that context() is not null and also uses
  // BrowserContext right after constructing `host`, so this is safe.
  BrowserContext* browser_context =
      host->version()->context()->wrapper()->browser_context();

  // Give the embedder a chance to register binders.
  GetContentClient()
      ->browser()
      ->RegisterBrowserInterfaceBindersForServiceWorker(browser_context, map);
}

void PopulateBinderMap(ServiceWorkerHost* host, mojo::BinderMap* map) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  PopulateServiceWorkerBinders(host, map);
}

// AgentSchedulingGroup
void PopulateBinderMapWithContext(
    AgentSchedulingGroupHost* host,
    mojo::BinderMapWithContext<AgentSchedulingGroupHost*>* map) {}
void PopulateBinderMap(AgentSchedulingGroupHost* host, mojo::BinderMap* map) {}
AgentSchedulingGroupHost* GetContextForHost(AgentSchedulingGroupHost* host) {
  return host;
}

}  // namespace internal

void OverrideDevicePostureProviderBinderForTesting(
    DevicePostureProviderBinder binder) {
  internal::GetDevicePostureProviderBinderOverride() = std::move(binder);
}

void OverrideBatteryMonitorBinderForTesting(BatteryMonitorBinder binder) {
  internal::GetBatteryMonitorBinderOverride() = std::move(binder);
}

void OverrideVibrationManagerBinderForTesting(VibrationManagerBinder binder) {
  internal::GetVibrationManagerBinderOverride() = std::move(binder);
}

}  // namespace content
