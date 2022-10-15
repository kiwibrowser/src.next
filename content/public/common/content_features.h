// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file defines all the public base::FeatureList features for the content
// module.

#ifndef CONTENT_PUBLIC_COMMON_CONTENT_FEATURES_H_
#define CONTENT_PUBLIC_COMMON_CONTENT_FEATURES_H_

#include "base/feature_list.h"
#include "base/metrics/field_trial_params.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "content/common/content_export.h"

namespace features {

// All features in alphabetical order. The features should be documented
// alongside the definition of their values in the .cc file.
CONTENT_EXPORT extern const base::Feature
    kAllowContentInitiatedDataUrlNavigations;
CONTENT_EXPORT extern const base::Feature kAndroidDownloadableFontsMatching;
#if BUILDFLAG(IS_WIN)
CONTENT_EXPORT extern const base::Feature kAudioProcessHighPriorityWin;
#endif
CONTENT_EXPORT extern const base::Feature kAudioServiceLaunchOnStartup;
CONTENT_EXPORT extern const base::Feature kAudioServiceOutOfProcess;
CONTENT_EXPORT extern const base::Feature kAudioServiceSandbox;
CONTENT_EXPORT extern const base::Feature
    kAvoidUnnecessaryBeforeUnloadCheckPostTask;
CONTENT_EXPORT extern const base::Feature
    kAvoidUnnecessaryBeforeUnloadCheckSync;
CONTENT_EXPORT extern const base::Feature kBackgroundFetch;
CONTENT_EXPORT extern const base::Feature kBackForwardCache;
CONTENT_EXPORT extern const base::Feature kBackForwardCacheMemoryControls;
CONTENT_EXPORT extern const base::Feature kBackForwardCacheMediaSessionService;
CONTENT_EXPORT extern const base::Feature kBlockInsecurePrivateNetworkRequests;
CONTENT_EXPORT extern const base::Feature
    kBlockInsecurePrivateNetworkRequestsFromPrivate;
CONTENT_EXPORT extern const base::Feature
    kBlockInsecurePrivateNetworkRequestsFromUnknown;
CONTENT_EXPORT extern const base::Feature
    kBlockInsecurePrivateNetworkRequestsDeprecationTrial;
CONTENT_EXPORT extern const base::Feature
    kBlockInsecurePrivateNetworkRequestsForNavigations;
CONTENT_EXPORT extern const base::Feature
    kBrokerFileOperationsOnDiskCacheInNetworkService;
CONTENT_EXPORT extern const base::Feature
    kBrowserVerifiedUserActivationKeyboard;
CONTENT_EXPORT extern const base::Feature kBrowserVerifiedUserActivationMouse;
CONTENT_EXPORT extern const base::Feature kCanvas2DImageChromium;
CONTENT_EXPORT extern const base::Feature
    kClearCrossSiteCrossBrowsingContextGroupWindowName;
CONTENT_EXPORT extern const base::Feature kCompositeBGColorAnimation;
CONTENT_EXPORT extern const base::Feature kCodeCacheDeletionWithoutFilter;
CONTENT_EXPORT extern const base::Feature kConsolidatedMovementXY;
CONTENT_EXPORT extern const base::Feature kCooperativeScheduling;
CONTENT_EXPORT extern const base::Feature kCrashReporting;
CONTENT_EXPORT extern const base::Feature kCriticalClientHint;
CONTENT_EXPORT extern const base::Feature
    kDebugHistoryInterventionNoUserActivation;
CONTENT_EXPORT extern const base::Feature kDesktopCaptureChangeSource;
#if BUILDFLAG(IS_CHROMEOS_LACROS)
CONTENT_EXPORT extern const base::Feature kDesktopCaptureLacrosV2;
#endif
CONTENT_EXPORT extern const base::Feature kDesktopPWAsTabStrip;
CONTENT_EXPORT extern const base::Feature kDevicePosture;
CONTENT_EXPORT extern const base::Feature kDigitalGoodsApi;
CONTENT_EXPORT extern const base::Feature kDocumentPolicy;
CONTENT_EXPORT extern const base::Feature kDocumentPolicyNegotiation;
CONTENT_EXPORT extern const base::Feature kEarlyEstablishGpuChannel;
CONTENT_EXPORT extern const base::Feature kEarlyHintsPreloadForNavigation;
CONTENT_EXPORT extern const base::Feature kEmbeddingRequiresOptIn;
CONTENT_EXPORT extern const base::Feature
    kEnableBackForwardCacheForScreenReader;
CONTENT_EXPORT extern const base::Feature kEnableCanvas2DLayers;
CONTENT_EXPORT extern const base::Feature
    kEnableMachineLearningModelLoaderWebPlatformApi;
CONTENT_EXPORT extern const base::Feature
    kEnableServiceWorkersForChromeUntrusted;
CONTENT_EXPORT extern const base::Feature kEnumerateDevicesHideDeviceIDs;
CONTENT_EXPORT extern const base::Feature kExperimentalAccessibilityLabels;
CONTENT_EXPORT extern const base::Feature
    kExperimentalContentSecurityPolicyFeatures;
CONTENT_EXPORT extern const base::Feature
    kExtraSafelistedRequestHeadersForOutOfBlinkCors;
CONTENT_EXPORT extern const base::Feature kFedCm;
CONTENT_EXPORT extern const char kFedCmAutoSigninFieldTrialParamName[];
CONTENT_EXPORT extern const char kFedCmIdpSignoutFieldTrialParamName[];
CONTENT_EXPORT extern const char kFedCmIframeSupportFieldTrialParamName[];
CONTENT_EXPORT extern const char kFedCmIdpSigninStatusFieldTrialParamName[];
CONTENT_EXPORT extern const base::Feature kFedCmManifestValidation;
CONTENT_EXPORT extern const base::Feature kFedCmMultipleIdentityProviders;
CONTENT_EXPORT extern const base::Feature kFirstPartySets;
CONTENT_EXPORT extern const base::FeatureParam<bool> kFirstPartySetsIsDogfooder;
CONTENT_EXPORT extern const base::FeatureParam<int>
    kFirstPartySetsMaxAssociatedSites;
CONTENT_EXPORT extern const base::Feature kFontManagerEarlyInit;
CONTENT_EXPORT extern const base::Feature kFontSrcLocalMatching;
#if !BUILDFLAG(IS_ANDROID)
CONTENT_EXPORT extern const base::Feature
    kForwardMemoryPressureEventsToGpuProcess;
#endif
CONTENT_EXPORT extern const base::Feature kFledgeLimitNumAuctions;
CONTENT_EXPORT extern const base::FeatureParam<int>
    kFledgeLimitNumAuctionsParam;
CONTENT_EXPORT extern const base::Feature kFractionalScrollOffsets;
CONTENT_EXPORT extern const base::Feature kGetDisplayMediaSet;
CONTENT_EXPORT extern const base::Feature
    kGetDisplayMediaSetAutoSelectAllScreens;
CONTENT_EXPORT extern const base::Feature kGreaseUACH;
CONTENT_EXPORT extern const base::Feature kIdentityInCanMakePaymentEventFeature;
CONTENT_EXPORT extern const base::Feature kIdleDetection;
CONTENT_EXPORT extern const base::Feature kInMemoryCodeCache;
CONTENT_EXPORT extern const base::Feature kIncludeIpcOverheadInNavigationStart;
CONTENT_EXPORT extern const base::Feature kInstalledApp;
CONTENT_EXPORT extern const base::Feature kInstalledAppProvider;
CONTENT_EXPORT extern const base::Feature kInstalledAppsInCbd;
CONTENT_EXPORT extern const base::Feature kIsolatedWebApps;
CONTENT_EXPORT extern const base::Feature kIsolateFencedFrames;
CONTENT_EXPORT extern const base::Feature kIsolateOrigins;
CONTENT_EXPORT extern const char kIsolateOriginsFieldTrialParamName[];
CONTENT_EXPORT extern const base::Feature kIsolateSandboxedIframes;
enum class IsolateSandboxedIframesGrouping {
  // In this grouping, all isolated sandboxed iframes whose URLs share the same
  // site in a given BrowsingInstance will share a process.
  kPerSite,
  // In this grouping, all isolated sandboxed iframes from a given
  // BrowsingInstance whose URLs share the same origin will be isolated in an
  // origin-keyed process.
  kPerOrigin,
  // Unlike the other two modes, which group sandboxed frames per-site or
  // per-origin, this one doesn't do any grouping at all and uses one process
  // per document.
  kPerDocument,
};
CONTENT_EXPORT extern const base::FeatureParam<IsolateSandboxedIframesGrouping>
    kIsolateSandboxedIframesGroupingParam;
CONTENT_EXPORT extern const base::Feature kLazyFrameLoading;
CONTENT_EXPORT extern const base::Feature kLazyFrameVisibleLoadTimeMetrics;
CONTENT_EXPORT extern const base::Feature kLazyImageLoading;
CONTENT_EXPORT extern const base::Feature kLazyImageVisibleLoadTimeMetrics;
CONTENT_EXPORT extern const base::Feature kLazyInitializeMediaControls;
CONTENT_EXPORT extern const base::Feature kLegacyWindowsDWriteFontFallback;
CONTENT_EXPORT extern const base::Feature kLogJsConsoleMessages;
CONTENT_EXPORT extern const base::Feature kLowerMemoryLimitForNonMainRenderers;
CONTENT_EXPORT extern const base::Feature kMBIMode;
enum class MBIMode {
  // In this mode, the AgentSchedulingGroup will use the process-wide legacy IPC
  // channel for communication with the renderer process and to associate its
  // interfaces with. AgentSchedulingGroup will effectively be a pass-through,
  // enabling legacy IPC and mojo behavior.
  kLegacy,

  // In this mode, each AgentSchedulingGroup will have its own legacy IPC
  // channel for communication with the renderer process and to associate its
  // interfaces with. Communication over that channel will not be ordered with
  // respect to the process-global legacy IPC channel. There will only be a
  // single AgentSchedulingGroup per RenderProcessHost.
  kEnabledPerRenderProcessHost,

  // This is just like the above state, however there will be a single
  // AgentSchedulingGroup per SiteInstance, and therefore potentially multiple
  // AgentSchedulingGroups per RenderProcessHost. Ordering between the
  // AgentSchedulingGroups in the same render process is not preserved.
  kEnabledPerSiteInstance,
};
CONTENT_EXPORT extern const base::FeatureParam<MBIMode> kMBIModeParam;
CONTENT_EXPORT extern const base::Feature kMediaDevicesSystemMonitorCache;
CONTENT_EXPORT extern const base::Feature kMediaStreamTrackTransfer;
CONTENT_EXPORT extern const base::Feature kMojoDedicatedThread;
CONTENT_EXPORT extern const base::Feature kMojoVideoCapture;
CONTENT_EXPORT extern const base::Feature kMojoVideoCaptureSecondary;
CONTENT_EXPORT extern const base::Feature kMouseSubframeNoImplicitCapture;
CONTENT_EXPORT extern const base::Feature kNavigationNetworkResponseQueue;
CONTENT_EXPORT extern const base::Feature kNavigationRequestPreconnect;
CONTENT_EXPORT extern const base::Feature kNavigationThreadingOptimizations;
CONTENT_EXPORT extern const base::Feature kNetworkQualityEstimatorWebHoldback;
CONTENT_EXPORT extern const base::Feature kNetworkServiceInProcess;
CONTENT_EXPORT extern const base::Feature kNeverSlowMode;
CONTENT_EXPORT extern const base::Feature kNotificationContentImage;
CONTENT_EXPORT extern const base::Feature kNotificationTriggers;
CONTENT_EXPORT extern const base::Feature kOriginIsolationHeader;
CONTENT_EXPORT extern const base::Feature kOverscrollHistoryNavigation;
CONTENT_EXPORT extern const base::Feature kWebPaymentAPICSP;
CONTENT_EXPORT extern const base::Feature kPeriodicBackgroundSync;
CONTENT_EXPORT extern const base::Feature kFeaturePolicyHeader;
CONTENT_EXPORT extern const base::Feature kPepper3DImageChromium;
CONTENT_EXPORT extern const base::Feature kPepperCrossOriginRedirectRestriction;
CONTENT_EXPORT extern const base::Feature kDocumentPictureInPictureAPI;
CONTENT_EXPORT extern const base::Feature kHighPriorityBeforeUnload;
CONTENT_EXPORT extern const base::Feature kPreloadCookies;
CONTENT_EXPORT extern const base::Feature kPrerender2Holdback;
CONTENT_EXPORT extern const base::Feature kPrivacySandboxAdsAPIsOverride;
CONTENT_EXPORT extern const base::Feature kPrivateNetworkAccessForWorkers;
CONTENT_EXPORT extern const base::Feature
    kPrivateNetworkAccessRespectPreflightResults;
CONTENT_EXPORT extern const base::Feature kPrivateNetworkAccessSendPreflights;
CONTENT_EXPORT extern const base::Feature kPrivateNetworkAccessPermissionPrompt;
CONTENT_EXPORT extern const base::Feature kProactivelySwapBrowsingInstance;
CONTENT_EXPORT extern const base::Feature
    kProcessSharingWithDefaultSiteInstances;
CONTENT_EXPORT extern const base::Feature
    kProcessSharingWithStrictSiteInstances;
CONTENT_EXPORT extern const base::Feature kPushSubscriptionChangeEvent;
CONTENT_EXPORT extern const base::Feature kReloadHiddenTabsWithCrashedSubframes;
CONTENT_EXPORT extern const base::Feature
    kRenderAccessibilityHostDeserializationOffMainThread;
CONTENT_EXPORT extern const base::Feature kRenderDocument;
CONTENT_EXPORT extern const base::Feature
    kRunVideoCaptureServiceInBrowserProcess;
CONTENT_EXPORT extern const base::Feature kSavePageAsWebBundle;
CONTENT_EXPORT extern const base::Feature kSecurePaymentConfirmation;
CONTENT_EXPORT extern const base::Feature kSecurePaymentConfirmationDebug;
CONTENT_EXPORT extern const base::Feature
    kSendBeaconThrowForBlobWithNonSimpleType;
CONTENT_EXPORT extern const base::Feature kServiceWorkerPaymentApps;
CONTENT_EXPORT extern const base::Feature
    kServiceWorkerTerminationOnNoControllee;
CONTENT_EXPORT extern const base::Feature kSharedArrayBuffer;
CONTENT_EXPORT extern const base::Feature kSharedArrayBufferOnDesktop;
CONTENT_EXPORT extern const base::Feature
    kSignedExchangeReportingForDistributors;
CONTENT_EXPORT extern const base::Feature kSignedHTTPExchange;
CONTENT_EXPORT extern const base::Feature
    kSiteIsolationForCrossOriginOpenerPolicy;
CONTENT_EXPORT extern const base::FeatureParam<bool>
    kSiteIsolationForCrossOriginOpenerPolicyShouldPersistParam;
CONTENT_EXPORT extern const base::FeatureParam<int>
    kSiteIsolationForCrossOriginOpenerPolicyMaxSitesParam;
CONTENT_EXPORT extern const base::FeatureParam<base::TimeDelta>
    kSiteIsolationForCrossOriginOpenerPolicyExpirationTimeoutParam;
CONTENT_EXPORT extern const base::Feature kSiteIsolationForGuests;
CONTENT_EXPORT extern const base::Feature kDisableProcessReuse;
CONTENT_EXPORT extern const base::Feature
    kSkipEarlyCommitPendingForCrashedFrame;
CONTENT_EXPORT extern const base::Feature
    kServiceWorkerSkipIgnorableFetchHandler;
CONTENT_EXPORT extern const base::FeatureParam<bool> kSkipEmptyFetchHandler;
CONTENT_EXPORT extern const base::Feature kUserMediaCaptureOnFocus;
CONTENT_EXPORT extern const base::Feature kWebLockScreenApi;
CONTENT_EXPORT extern const base::Feature kWebOTP;
CONTENT_EXPORT extern const base::Feature kWebOTPAssertionFeaturePolicy;
CONTENT_EXPORT extern const base::Feature kSpareRendererForSitePerProcess;
CONTENT_EXPORT extern const base::Feature kStopVideoCaptureOnScreenLock;
CONTENT_EXPORT extern const base::Feature kStrictOriginIsolation;
CONTENT_EXPORT extern const base::Feature kSubframeShutdownDelay;
enum class SubframeShutdownDelayType {
  // A flat 2s shutdown delay.
  kConstant,
  // A flat 8s shutdown delay.
  kConstantLong,
  // A variable delay from 0s to 8s based on the median interval between
  // subframe shutdown and process reuse over the past 5 subframe navigations.
  // A subframe that could not be reused is counted as 0s.
  kHistoryBased,
  // A variable delay from 0s to 8s based on the 75th-percentile interval
  // between subframe shutdown and process reuse over the past 5 subframe
  // navigations. A subframe that could not be reused is counted as 0s.
  kHistoryBasedLong,
  // A 2s base delay at 8 GB available memory or lower. Above 8 GB available
  // memory, scales up linearly to a maximum 8s delay at 16 GB or more.
  kMemoryBased
};
CONTENT_EXPORT extern const base::FeatureParam<SubframeShutdownDelayType>
    kSubframeShutdownDelayTypeParam;
CONTENT_EXPORT extern const base::Feature kSubresourceWebBundles;
CONTENT_EXPORT extern const base::Feature
    kSuppressDifferentOriginSubframeJSDialogs;
CONTENT_EXPORT extern const base::Feature kSyntheticPointerActions;
CONTENT_EXPORT extern const base::Feature kThreadingOptimizationsOnIO;
CONTENT_EXPORT extern const base::Feature kTouchDragAndContextMenu;
CONTENT_EXPORT extern const base::Feature kTouchpadAsyncPinchEvents;
CONTENT_EXPORT extern const base::Feature kTouchpadOverscrollHistoryNavigation;
CONTENT_EXPORT extern const base::Feature kTreatBootstrapAsDefault;
CONTENT_EXPORT extern const base::Feature kUnrestrictedSharedArrayBuffer;
CONTENT_EXPORT extern const base::Feature kUserActivationSameOriginVisibility;
CONTENT_EXPORT extern const base::Feature kVerifyDidCommitParams;
CONTENT_EXPORT extern const base::Feature kVideoPlaybackQuality;
CONTENT_EXPORT extern const base::Feature kV8VmFuture;
CONTENT_EXPORT extern const base::Feature kJavaScriptExperimentalSharedMemory;
CONTENT_EXPORT extern const base::Feature kWebAppWindowControlsOverlay;
CONTENT_EXPORT extern const base::Feature kWebAssemblyBaseline;
CONTENT_EXPORT extern const base::Feature kWebAssemblyCodeProtection;
#if (BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)) && defined(ARCH_CPU_X86_64)
CONTENT_EXPORT extern const base::Feature kWebAssemblyCodeProtectionPku;
#endif  // (BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)) &&
        // defined(ARCH_CPU_X86_64)
CONTENT_EXPORT extern const base::Feature kWebAssemblyDynamicTiering;
#if defined(ARCH_CPU_X86_64)
CONTENT_EXPORT extern const base::Feature
    kEnableExperimentalWebAssemblyStackSwitching;
#endif  // defined(ARCH_CPU_X86_64)
CONTENT_EXPORT extern const base::Feature kWebAssemblyLazyCompilation;
CONTENT_EXPORT extern const base::Feature kWebAssemblySimd;
CONTENT_EXPORT extern const base::Feature kWebAssemblyTiering;
CONTENT_EXPORT extern const base::Feature kWebAssemblyTrapHandler;
CONTENT_EXPORT extern const base::Feature kWebAuthConditionalUI;
CONTENT_EXPORT extern const base::Feature kWebBluetooth;
CONTENT_EXPORT extern const base::Feature kWebBluetoothNewPermissionsBackend;
CONTENT_EXPORT extern const base::Feature kWebBundles;
CONTENT_EXPORT extern const base::Feature kWebBundlesFromNetwork;
CONTENT_EXPORT extern const base::Feature kWebGLImageChromium;
CONTENT_EXPORT extern const base::Feature kWebMidi;
CONTENT_EXPORT extern const base::Feature kWebOtpBackendAuto;
CONTENT_EXPORT extern const base::Feature kWebPayments;
CONTENT_EXPORT extern const base::Feature kWebRtcUseGpuMemoryBufferVideoFrames;
CONTENT_EXPORT extern const base::Feature kWebUICodeCache;
CONTENT_EXPORT extern const base::Feature kWebUIReportOnlyTrustedTypes;
CONTENT_EXPORT extern const base::Feature kWebUsb;
CONTENT_EXPORT extern const base::Feature kWebXr;
CONTENT_EXPORT extern const base::Feature kWebXrArModule;

#if BUILDFLAG(IS_ANDROID)
CONTENT_EXPORT extern const base::Feature kAccessibilityAsyncTreeConstruction;
CONTENT_EXPORT extern const base::Feature kAccessibilityPageZoom;
CONTENT_EXPORT extern const base::Feature
    kBackgroundMediaRendererHasModerateBinding;
CONTENT_EXPORT extern const base::Feature kBindingManagerConnectionLimit;
CONTENT_EXPORT extern const base::Feature
    kBindingManagerUseNotPerceptibleBinding;
CONTENT_EXPORT extern const base::Feature kReduceGpuPriorityOnBackground;
CONTENT_EXPORT extern const base::Feature kOnDemandAccessibilityEvents;
CONTENT_EXPORT extern const base::Feature kRequestDesktopSiteAdditions;
CONTENT_EXPORT extern const base::Feature kRequestDesktopSiteExceptions;
CONTENT_EXPORT extern const base::Feature kUserMediaScreenCapturing;
CONTENT_EXPORT extern const base::Feature kWarmUpNetworkProcess;
CONTENT_EXPORT extern const base::Feature kWebNfc;
CONTENT_EXPORT extern const base::Feature kWebViewThrottleBackgroundBeginFrame;

extern const char kDragAndDropMovementThresholdDipParam[];

CONTENT_EXPORT extern const base::Feature kOptimizeEarlyNavigation;
CONTENT_EXPORT extern const base::FeatureParam<base::TimeDelta>
    kCompositorLockTimeout;

#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_MAC)
CONTENT_EXPORT extern const base::Feature kDeviceMonitorMac;
CONTENT_EXPORT extern const base::Feature kIOSurfaceCapturer;
CONTENT_EXPORT extern const base::Feature kMacSyscallSandbox;
CONTENT_EXPORT extern const base::Feature kMacWebContentsOcclusion;
CONTENT_EXPORT extern const base::Feature kRetryGetVideoCaptureDeviceInfos;
#endif  // BUILDFLAG(IS_MAC)

#if defined(WEBRTC_USE_PIPEWIRE)
CONTENT_EXPORT extern const base::Feature kWebRtcPipeWireCapturer;
#endif  // defined(WEBRTC_USE_PIPEWIRE)

// DON'T ADD RANDOM STUFF HERE. Put it in the main section above in
// alphabetical order, or in one of the ifdefs (also in order in each section).

CONTENT_EXPORT bool IsVideoCaptureServiceEnabledForOutOfProcess();
CONTENT_EXPORT bool IsVideoCaptureServiceEnabledForBrowserProcess();

}  // namespace features

#endif  // CONTENT_PUBLIC_COMMON_CONTENT_FEATURES_H_
