// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_FLAG_DESCRIPTIONS_H_
#define CHROME_BROWSER_FLAG_DESCRIPTIONS_H_

// Includes needed for macros allowing conditional compilation of some strings.
#include "base/debug/debugging_buildflags.h"
#include "build/branding_buildflags.h"
#include "build/build_config.h"
#include "build/buildflag.h"
#include "build/chromeos_buildflags.h"
#include "chrome/common/buildflags.h"
#include "components/compose/buildflags.h"
#include "components/nacl/common/buildflags.h"
#include "components/paint_preview/buildflags/buildflags.h"
#include "components/signin/public/base/signin_buildflags.h"
#include "components/supervised_user/core/common/buildflags.h"
#include "device/vr/buildflags/buildflags.h"
#include "extensions/buildflags/buildflags.h"
#include "media/media_buildflags.h"
#include "pdf/buildflags.h"
#include "printing/buildflags/buildflags.h"
#include "third_party/blink/public/common/buildflags.h"

// This file declares strings used in chrome://flags. These messages are not
// translated, because instead of end-users they target Chromium developers and
// testers. See https://crbug.com/587272 and https://crbug.com/703134 for more
// details.
//
// Comments are not necessary. The contents of the strings (which appear in the
// UI) should be good enough documentation for what flags do and when they
// apply. If they aren't, fix them.
//
// Sort flags in each section alphabetically by the k...Name constant. Follow
// that by the k...Description constant and any special values associated with
// that.
//
// Put #ifdefed flags in the appropriate section toward the bottom, don't
// intersperse the file with ifdefs.

namespace flag_descriptions {

// Cross-platform -------------------------------------------------------------

extern const char kAccelerated2dCanvasName[];
extern const char kAccelerated2dCanvasDescription[];

extern const char kAcceleratedVideoDecodeName[];
extern const char kAcceleratedVideoDecodeDescription[];

extern const char kAcceleratedVideoEncodeName[];
extern const char kAcceleratedVideoEncodeDescription[];

extern const char kAdvancedPeripheralsSupportTabStripName[];
extern const char kAdvancedPeripheralsSupportTabStripDescription[];

#if BUILDFLAG(ENABLE_PDF)
extern const char kAccessiblePDFFormName[];
extern const char kAccessiblePDFFormDescription[];

extern const char kPdfOopifName[];
extern const char kPdfOopifDescription[];

extern const char kPdfPortfolioName[];
extern const char kPdfPortfolioDescription[];

extern const char kPdfUseSkiaRendererName[];
extern const char kPdfUseSkiaRendererDescription[];
#endif

extern const char kAlignWakeUpsName[];
extern const char kAlignWakeUpsDescription[];

extern const char kAllowInsecureLocalhostName[];
extern const char kAllowInsecureLocalhostDescription[];

extern const char kAndroidAppIntegrationName[];
extern const char kAndroidAppIntegrationDescription[];

extern const char kAndroidExtendedKeyboardShortcutsName[];
extern const char kAndroidExtendedKeyboardShortcutsDescription[];

extern const char kAnimatedImageResumeName[];
extern const char kAnimatedImageResumeDescription[];

extern const char kAriaElementReflectionName[];
extern const char kAriaElementReflectionDescription[];

extern const char kAttributionReportingDebugModeName[];
extern const char kAttributionReportingDebugModeDescription[];

extern const char kAppDeduplicationServiceFondueName[];
extern const char kAppDeduplicationServiceFondueDescription[];

extern const char kAuxiliarySearchDonationName[];
extern const char kAuxiliarySearchDonationDescription[];

extern const char kBackgroundResourceFetchName[];
extern const char kBackgroundResourceFetchDescription[];

extern const char kCOLRV1FontsDescription[];

extern const char kCdmStorageDatabaseName[];
extern const char kCdmStorageDatabaseDescription[];

extern const char kCdmStorageDatabaseMigrationName[];
extern const char kCdmStorageDatabaseMigrationDescription[];

extern const char kClickToCallName[];
extern const char kClickToCallDescription[];

extern const char kClipboardUnsanitizedContentName[];
extern const char kClipboardUnsanitizedContentDescription[];

extern const char kClipboardWellFormedHtmlSanitizationWriteName[];
extern const char kClipboardWellFormedHtmlSanitizationWriteDescription[];

extern const char kClipboardMaximumAgeName[];
extern const char kClipboardMaximumAgeDescription[];

extern const char kComputePressureRateObfuscationMitigationName[];
extern const char kComputePressureRateObfuscationMitigationDescription[];

extern const char kComputePressureBreakCalibrationMitigationName[];
extern const char kComputePressureBreakCalibrationMitigationDescription[];

extern const char kContentLanguagesInLanguagePickerName[];
extern const char kContentLanguagesInLanguagePickerDescription[];

extern const char kContentSettingsPartitioningName[];
extern const char kContentSettingsPartitioningDescription[];

#if BUILDFLAG(IS_CHROMEOS_ASH)
extern const char kCoralFeatureKeyName[];
extern const char kCoralFeatureKeyDescription[];
#endif  // IS_CHROMEOS_ASH

#if BUILDFLAG(IS_CHROMEOS)
extern const char kCrOSLegacyMediaFormatsName[];
extern const char kCrOSLegacyMediaFormatsDescription[];
#endif

extern const char kCustomizeChromeColorExtractionName[];
extern const char kCustomizeChromeColorExtractionDescription[];

extern const char kCustomizeChromeSidePanelId[];
extern const char kCustomizeChromeSidePanelName[];
extern const char KCustomizeChromeSidePanelDescription[];

extern const char kCustomizeChromeSidePanelExtensionsCardName[];
extern const char kCustomizeChromeSidePanelExtensionsCardDescription[];

extern const char kCustomizeChromeWallpaperSearchName[];
extern const char kCustomizeChromeWallpaperSearchDescription[];

extern const char kCustomizeChromeWallpaperSearchInspirationCardName[];
extern const char kCustomizeChromeWallpaperSearchInspirationCardDescription[];

extern const char kWallpaperSearchSettingsVisibilityName[];
extern const char kWallpaperSearchSettingsVisibilityDescription[];

extern const char kDeprecateUnloadName[];
extern const char kDeprecateUnloadDescription[];

extern const char kDIPSName[];
extern const char kDIPSDescription[];

extern const char kDocumentPictureInPictureApiName[];
extern const char kDocumentPictureInPictureApiDescription[];

extern const char kDownloadWarningImprovementsName[];
extern const char kDownloadWarningImprovementsDescription[];

extern const char kEnableBenchmarkingName[];
extern const char kEnableBenchmarkingDescription[];
extern const char kEnableBenchmarkingChoiceDisabled[];
extern const char kEnableBenchmarkingChoiceDefaultFeatureStates[];
extern const char kEnableBenchmarkingChoiceMatchFieldTrialTestingConfig[];

#if BUILDFLAG(USE_FONTATIONS_BACKEND)
extern const char kFontationsFontBackendName[];
extern const char kFontationsFontBackendDescription[];
#endif

extern const char kForceStartupSigninPromoName[];
extern const char kForceStartupSigninPromoDescription[];

extern const char kGainmapHdrImagesName[];
extern const char kGainmapHdrImagesDescription[];

extern const char kAvifGainmapHdrImagesName[];
extern const char kAvifGainmapHdrImagesDescription[];

extern const char kTangibleSyncName[];
extern const char kTangibleSyncDescription[];

extern const char kMainThreadCompositingPriorityName[];
extern const char kMainThreadCompositingPriorityDescription[];

extern const char kMediaSessionEnterPictureInPictureName[];
extern const char kMediaSessionEnterPictureInPictureDescription[];

extern const char kPasspointARCSupportName[];
extern const char kPasspointARCSupportDescription[];

extern const char kPasspointSettingsName[];
extern const char kPasspointSettingsDescription[];

extern const char kPrivacyIndicatorsName[];
extern const char kPrivacyIndicatorsDescription[];

extern const char kEnableBluetoothSerialPortProfileInSerialApiName[];
extern const char kEnableBluetoothSerialPortProfileInSerialApiDescription[];

extern const char kEnableDrDcName[];
extern const char kEnableDrDcDescription[];

extern const char kForceGpuMainThreadToNormalPriorityDrDcName[];
extern const char kForceGpuMainThreadToNormalPriorityDrDcDescription[];

extern const char kUseClientGmbInterfaceName[];
extern const char kUseClientGmbInterfaceDescription[];

#if BUILDFLAG(ENABLE_SUPERVISED_USERS)

extern const char kFilterWebsitesForSupervisedUsersOnDesktopName[];
extern const char kFilterWebsitesForSupervisedUsersOnDesktopDescription[];

extern const char kEnableExtensionsPermissionsForSupervisedUsersOnDesktopName[];
extern const char
    kEnableExtensionsPermissionsForSupervisedUsersOnDesktopDescription[];

extern const char kSupervisedPrefsControlledBySupervisedStoreName[];
extern const char kSupervisedPrefsControlledBySupervisedStoreDescription[];

extern const char kEnableManagedByParentUiName[];
extern const char kEnableManagedByParentUiDescription[];

extern const char kClearingCookiesKeepsSupervisedUsersSignedInName[];
extern const char kClearingCookiesKeepsSupervisedUsersSignedInDescription[];
#endif  // ENABLE_SUPERVISED_USERS

extern const char kUseAndroidStagingSmdsName[];
extern const char kUseAndroidStagingSmdsDescription[];

extern const char kUseSharedImagesForPepperVideoName[];
extern const char kUseSharedImagesForPepperVideoDescription[];

extern const char kUseStorkSmdsServerAddressName[];
extern const char kUseStorkSmdsServerAddressDescription[];

extern const char kUseWallpaperStagingUrlName[];
extern const char kUseWallpaperStagingUrlDescription[];

extern const char kUseMessagesStagingUrlName[];
extern const char kUseMessagesStagingUrlDescription[];

extern const char kUseCustomMessagesDomainName[];
extern const char kUseCustomMessagesDomainDescription[];

extern const char kEnableFileBackedBlobFactoryName[];
extern const char kEnableFileBackedBlobFactoryDescription[];

extern const char kEnableAutoDisableAccessibilityName[];
extern const char kEnableAutoDisableAccessibilityDescription[];

extern const char kEnableAutoDisableAccessibilityV2Name[];
extern const char kEnableAutoDisableAccessibilityV2Description[];

extern const char kAutofillEnableAndroidNKeyForFidoAuthenticationName[];
extern const char kAutofillEnableAndroidNKeyForFidoAuthenticationDescription[];

extern const char kAutofillEnableCvcStorageAndFillingName[];
extern const char kAutofillEnableCvcStorageAndFillingDescription[];

extern const char kAutofillEnableFIDOProgressDialogName[];
extern const char kAutofillEnableFIDOProgressDialogDescription[];

extern const char kAutofillEnableFpanRiskBasedAuthenticationName[];
extern const char kAutofillEnableFpanRiskBasedAuthenticationDescription[];

extern const char kAutofillEnableManualFallbackForVirtualCardsName[];
extern const char kAutofillEnableManualFallbackForVirtualCardsDescription[];

extern const char kAutofillEnableMerchantDomainInUnmaskCardRequestName[];
extern const char kAutofillEnableMerchantDomainInUnmaskCardRequestDescription[];

extern const char kAutofillEnableUserAvatarInSaveCardFooterName[];
extern const char kAutofillEnableUserAvatarInSaveCardFooterDescription[];

extern const char kAutofillEnableCardArtImageName[];
extern const char kAutofillEnableCardArtImageDescription[];

extern const char kAutofillEnableCardArtServerSideStretchingName[];
extern const char kAutofillEnableCardArtServerSideStretchingDescription[];

extern const char kAutofillEnableCardBenefitsName[];
extern const char kAutofillEnableCardBenefitsDescription[];

extern const char kAutofillEnableCardProductNameName[];
extern const char kAutofillEnableCardProductNameDescription[];

extern const char kAutofillEnableEmailOtpForVcnYellowPathName[];
extern const char kAutofillEnableEmailOtpForVcnYellowPathDescription[];

extern const char kAutofillEnableIbanAndroidBottomSheetName[];
extern const char kAutofillEnableIbanAndroidBottomSheetDescription[];

extern const char kAutofillEnableNewCardArtAndNetworkImagesName[];
extern const char kAutofillEnableNewCardArtAndNetworkImagesDescription[];

extern const char kAutofillEnableOffersInClankKeyboardAccessoryName[];
extern const char kAutofillEnableOffersInClankKeyboardAccessoryDescription[];

extern const char kAutofillEnablePaymentsAndroidBottomSheetName[];
extern const char kAutofillEnablePaymentsAndroidBottomSheetDescription[];

extern const char kAutofillEnablePaymentsMandatoryReauthName[];
extern const char kAutofillEnablePaymentsMandatoryReauthDescription[];

extern const char kAutofillEnableRankingFormulaAddressProfilesName[];
extern const char kAutofillEnableRankingFormulaAddressProfilesDescription[];

extern const char kAutofillEnableRankingFormulaCreditCardsName[];
extern const char kAutofillEnableRankingFormulaCreditCardsDescription[];

extern const char kAutofillEnableRemadeDownstreamMetricsName[];
extern const char kAutofillEnableRemadeDownstreamMetricsDescription[];

extern const char kAutofillEnableSaveCardLoadingAndConfirmationName[];
extern const char kAutofillEnableSaveCardLoadingAndConfirmationDescription[];

extern const char kAutofillEnableServerIbanName[];
extern const char kAutofillEnableServerIbanDescription[];

extern const char kAutofillEnableStickyManualFallbackForCardsName[];
extern const char kAutofillEnableStickyManualFallbackForCardsDescription[];

extern const char kAutofillEnableUpdateVirtualCardEnrollmentName[];
extern const char kAutofillEnableUpdateVirtualCardEnrollmentDescription[];

extern const char kAutofillEnableVcn3dsAuthenticationName[];
extern const char kAutofillEnableVcn3dsAuthenticationDescription[];

extern const char kAutofillEnableNewSaveCardBubbleUiName[];
extern const char kAutofillEnableNewSaveCardBubbleUiDescription[];

extern const char kAutofillEnableVcnEnrollLoadingAndConfirmationName[];
extern const char kAutofillEnableVcnEnrollLoadingAndConfirmationDescription[];

extern const char kAutofillEnableVirtualCardMetadataName[];
extern const char kAutofillEnableVirtualCardMetadataDescription[];

extern const char kAutofillHighlightOnlyChangedValuesInPreviewModeName[];
extern const char kAutofillHighlightOnlyChangedValuesInPreviewModeDescription[];

extern const char kAutofillParseVcnCardOnFileStandaloneCvcFieldsName[];
extern const char kAutofillParseVcnCardOnFileStandaloneCvcFieldsDescription[];

extern const char kAutofillShowAutocompleteDeleteButtonName[];
extern const char kAutofillShowAutocompleteDeleteButtonDescription[];

extern const char kAutofillSuggestServerCardInsteadOfLocalCardName[];
extern const char kAutofillSuggestServerCardInsteadOfLocalCardDescription[];

extern const char kAutofillUpdateChromeSettingsLinkToGPayWebName[];
extern const char kAutofillUpdateChromeSettingsLinkToGPayWebDescription[];

extern const char kAutofillMoreProminentPopupName[];
extern const char kAutofillMoreProminentPopupDescription[];

extern const char kAutofillVirtualCardsOnTouchToFillAndroidName[];
extern const char kAutofillVirtualCardsOnTouchToFillAndroidDescription[];

extern const char kAutofillVirtualViewStructureAndroidName[];
extern const char kAutofillVirtualViewStructureAndroidDescription[];

extern const char kBackForwardCacheName[];
extern const char kBackForwardCacheDescription[];

extern const char kBackForwardTransitionsName[];
extern const char kBackForwardTransitionsDescription[];

extern const char kBackgroundListeningName[];
extern const char kBackgroundListeningDescription[];

extern const char kBiometricReauthForPasswordFillingName[];
extern const char kBiometricReauthForPasswordFillingDescription[];

extern const char kFailFastQuietChipName[];
extern const char kFailFastQuietChipDescription[];

extern const char kBorealisBigGlName[];
extern const char kBorealisBigGlDescription[];

extern const char kBorealisDGPUName[];
extern const char kBorealisDGPUDescription[];

extern const char kBorealisEnableUnsupportedHardwareName[];
extern const char kBorealisEnableUnsupportedHardwareDescription[];

extern const char kBorealisForceBetaClientName[];
extern const char kBorealisForceBetaClientDescription[];

extern const char kBorealisForceDoubleScaleName[];
extern const char kBorealisForceDoubleScaleDescription[];

extern const char kBorealisLinuxModeName[];
extern const char kBorealisLinuxModeDescription[];

extern const char kBorealisPermittedName[];
extern const char kBorealisPermittedDescription[];

extern const char kBorealisProvisionName[];
extern const char kBorealisProvisionDescription[];

extern const char kBorealisScaleClientByDPIName[];
extern const char kBorealisScaleClientByDPIDescription[];

extern const char kBorealisWebUIInstallerName[];
extern const char kBorealisWebUIInstallerDescription[];

extern const char kBorealisZinkGlDriverName[];
extern const char kBorealisZinkGlDriverDescription[];

extern const char kBypassAppBannerEngagementChecksName[];
extern const char kBypassAppBannerEngagementChecksDescription[];

extern const char kSeparateWebAppShortcutBadgeIconName[];
extern const char kSeparateWebAppShortcutBadgeIconDescription[];

extern const char kServiceWorkerBypassFetchHandlerName[];
extern const char kServiceWorkerBypassFetchHandlerDescription[];

extern const char kServiceWorkerBypassFetchHandlerForMainResourceName[];
extern const char kServiceWorkerBypassFetchHandlerForMainResourceDescription[];

extern const char kServiceWorkerStaticRouterName[];
extern const char kServiceWorkerStaticRouterDescription[];

#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_CHROMEOS) && !BUILDFLAG(IS_FUCHSIA)
extern const char kCameraMicEffectsName[];
extern const char kCameraMicEffectsDescription[];

extern const char kCameraMicPreviewName[];
extern const char kCameraMicPreviewDescription[];
#endif

extern const char kCanvasOopRasterizationName[];
extern const char kCanvasOopRasterizationDescription[];

extern const char kCapturedSurfaceControlName[];
extern const char kCapturedSurfaceControlDescription[];

#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_CHROMEOS)
extern const char kCcoTest1Name[];
extern const char kCcoTest1Description[];
#endif

extern const char kChromeLabsName[];
extern const char kChromeLabsDescription[];

extern const char kChromeRefresh2023Id[];
extern const char kChromeRefresh2023Name[];
extern const char kChromeRefresh2023Description[];

extern const char kChromeWebuiRefresh2023Id[];
extern const char kChromeWebuiRefresh2023Name[];
extern const char kChromeWebuiRefresh2023Description[];

extern const char kChromeRefresh2023NTBName[];
extern const char kChromeRefresh2023NTBDescription[];

extern const char kChromeRefresh2023TopChromeFontName[];
extern const char kChromeRefresh2023TopChromeFontDescription[];

extern const char kCommerceHintAndroidName[];
extern const char kCommerceHintAndroidDescription[];

extern const char kClearCrossSiteCrossBrowsingContextGroupWindowNameName[];
extern const char
    kClearCrossSiteCrossBrowsingContextGroupWindowNameDescription[];

extern const char kCreateShortcutIgnoresManifestName[];
extern const char kCreateShortcutIgnoresManifestDescription[];

extern const char kDisableProcessReuse[];
extern const char kDisableProcessReuseDescription[];

extern const char kDiscountConsentV2Name[];
extern const char kDiscountConsentV2Description[];

extern const char kDisruptiveNotificationPermissionRevocationName[];
extern const char kDisruptiveNotificationPermissionRevocationDescription[];

extern const char kDoubleBufferCompositingName[];
extern const char kDoubleBufferCompositingDescription[];

extern const char kCodeBasedRBDName[];
extern const char kCodeBasedRBDDescription[];

extern const char kCompressionDictionaryTransportName[];
extern const char kCompressionDictionaryTransportDescription[];

extern const char kCompressionDictionaryTransportBackendName[];
extern const char kCompressionDictionaryTransportBackendDescription[];

extern const char kCompressionDictionaryTransportOverHttp1Name[];
extern const char kCompressionDictionaryTransportOverHttp1Description[];

extern const char kCompressionDictionaryTransportRequireKnownRootCertName[];
extern const char
    kCompressionDictionaryTransportRequireKnownRootCertDescription[];

extern const char kUseDMSAAForTilesName[];
extern const char kUseDMSAAForTilesDescription[];

extern const char kUseDMSAAForTilesAndroidGLName[];
extern const char kUseDMSAAForTilesAndroidGLDescription[];

extern const char kUseDnsHttpsSvcbAlpnName[];
extern const char kUseDnsHttpsSvcbAlpnDescription[];

extern const char kEditContextName[];
extern const char kEditContextDescription[];

extern const char kIsolatedSandboxedIframesName[];
extern const char kIsolatedSandboxedIframesDescription[];

extern const char kExperimentalAccessibilityLanguageDetectionName[];
extern const char kExperimentalAccessibilityLanguageDetectionDescription[];

extern const char kExperimentalAccessibilityLanguageDetectionDynamicName[];
extern const char
    kExperimentalAccessibilityLanguageDetectionDynamicDescription[];

extern const char kForceColorProfileSRGB[];
extern const char kForceColorProfileP3[];
extern const char kForceColorProfileRec2020[];
extern const char kForceColorProfileColorSpin[];
extern const char kForceColorProfileSCRGBLinear[];
extern const char kForceColorProfileHDR10[];

extern const char kForceColorProfileName[];
extern const char kForceColorProfileDescription[];

extern const char kDynamicColorGamutName[];
extern const char kDynamicColorGamutDescription[];

extern const char kCooperativeSchedulingName[];
extern const char kCooperativeSchedulingDescription[];

extern const char kDarkenWebsitesCheckboxInThemesSettingName[];
extern const char kDarkenWebsitesCheckboxInThemesSettingDescription[];

extern const char kDebugPackedAppName[];
extern const char kDebugPackedAppDescription[];

extern const char kDebugShortcutsName[];
extern const char kDebugShortcutsDescription[];

extern const char kDisallowDocWrittenScriptsUiName[];
extern const char kDisallowDocWrittenScriptsUiDescription[];

#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_CHROMEOS_ASH)
extern const char kDisallowManagedProfileSignoutName[];
extern const char kDisallowManagedProfileSignoutDescription[];
#endif  // !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_CHROMEOS_ASH)

extern const char kViewTransitionOnNavigationName[];
extern const char kViewTransitionOnNavigationDescription[];

extern const char kEnableAutofillAddressSavePromptName[];
extern const char kEnableAutofillAddressSavePromptDescription[];

extern const char kEnableExperimentalCookieFeaturesName[];
extern const char kEnableExperimentalCookieFeaturesDescription[];

extern const char kEnableRawDrawName[];
extern const char kEnableRawDrawDescription[];

extern const char kEnableDelegatedCompositingName[];
extern const char kEnableDelegatedCompositingDescription[];

extern const char kEnableRemovingAllThirdPartyCookiesName[];
extern const char kEnableRemovingAllThirdPartyCookiesDescription[];

extern const char kDesktopPWAsAdditionalWindowingControlsName[];
extern const char kDesktopPWAsAdditionalWindowingControlsDescription[];

extern const char kDesktopPWAsElidedExtensionsMenuName[];
extern const char kDesktopPWAsElidedExtensionsMenuDescription[];

extern const char kDesktopPWAsLaunchHandlerName[];
extern const char kDesktopPWAsLaunchHandlerDescription[];

extern const char kDesktopPWAsTabStripName[];
extern const char kDesktopPWAsTabStripDescription[];

extern const char kDesktopPWAsTabStripSettingsName[];
extern const char kDesktopPWAsTabStripSettingsDescription[];

extern const char kDesktopPWAsTabStripCustomizationsName[];
extern const char kDesktopPWAsTabStripCustomizationsDescription[];

extern const char kDesktopPWAsSubAppsName[];
extern const char kDesktopPWAsSubAppsDescription[];

extern const char kDesktopPWAsScopeExtensionsName[];
extern const char kDesktopPWAsScopeExtensionsDescription[];

extern const char kDesktopPWAsBorderlessName[];
extern const char kDesktopPWAsBorderlessDescription[];

extern const char kDeviceForceScheduledRebootName[];
extern const char kDeviceForceScheduledRebootDescription[];

extern const char kDevicePostureName[];
extern const char kDevicePostureDescription[];

extern const char kEnableTLS13EarlyDataName[];
extern const char kEnableTLS13EarlyDataDescription[];

extern const char kEnableTLS13KyberName[];
extern const char kEnableTLS13KyberDescription[];

extern const char kAccessibilityAcceleratorNotificationsTimeoutName[];
extern const char kAccessibilityAcceleratorNotificationsTimeoutDescription[];

extern const char kAccessibilityServiceName[];
extern const char kAccessibilityServiceDescription[];

extern const char kExperimentalAccessibilityColorEnhancementSettingsName[];
extern const char
    kExperimentalAccessibilityColorEnhancementSettingsDescription[];

extern const char kAccessibilityChromeVoxPageMigrationName[];
extern const char kAccessibilityChromeVoxPageMigrationDescription[];

extern const char kAccessibilityDictationKeyboardImprovementsName[];
extern const char kAccessibilityDictationKeyboardImprovementsDescription[];

extern const char kAccessibilityExtraLargeCursorName[];
extern const char kAccessibilityExtraLargeCursorDescription[];

extern const char kAccessibilityFaceGazeName[];
extern const char kAccessibilityFaceGazeDescription[];

extern const char kAccessibilityMagnifierFollowsStsName[];
extern const char kAccessibilityMagnifierFollowsStsDescription[];

extern const char kAccessibilitySelectToSpeakPrefsMigrationName[];
extern const char kAccessibilitySelectToSpeakPrefsMigrationDescription[];

extern const char kAccessibilitySelectToSpeakHoverTextImprovementsName[];
extern const char kAccessibilitySelectToSpeakHoverTextImprovementsDescription[];

extern const char kMacCoreLocationBackendName[];
extern const char kMacCoreLocationBackendDescription[];

extern const char kNewMacNotificationAPIName[];
extern const char kNewMacNotificationAPIDescription[];

extern const char kWinrtGeolocationImplementationName[];
extern const char kWinrtGeolocationImplementationDescription[];

extern const char kEnableGenericSensorExtraClassesName[];
extern const char kEnableGenericSensorExtraClassesDescription[];

extern const char kEnableGpuServiceLoggingName[];
extern const char kEnableGpuServiceLoggingDescription[];

extern const char kEnableNetworkLoggingToFileName[];
extern const char kEnableNetworkLoggingToFileDescription[];

extern const char kEnableWindowsGamingInputDataFetcherName[];
extern const char kEnableWindowsGamingInputDataFetcherDescription[];

extern const char kBlockInsecurePrivateNetworkRequestsName[];
extern const char kBlockInsecurePrivateNetworkRequestsDescription[];

extern const char kPipDoubleTapToResizeName[];
extern const char kPipDoubleTapToResizeDescription[];

extern const char kPipPinchToResizeName[];
extern const char kPipPinchToResizeDescription[];

extern const char kPipTiltName[];
extern const char kPipTiltDescription[];

extern const char kPipTuckName[];
extern const char kPipTuckDescription[];

extern const char kPrivateNetworkAccessSendPreflightsName[];
extern const char kPrivateNetworkAccessSendPreflightsDescription[];

extern const char kPrivateNetworkAccessRespectPreflightResultsName[];
extern const char kPrivateNetworkAccessRespectPreflightResultsDescription[];

extern const char kPrivateNetworkAccessPreflightShortTimeoutName[];
extern const char kPrivateNetworkAccessPreflightShortTimeoutDescription[];

extern const char kPrivateNetworkAccessPermissionPromptName[];
extern const char kPrivateNetworkAccessPermissionPromptDescription[];

extern const char kDeprecateAltClickName[];
extern const char kDeprecateAltClickDescription[];

extern const char kDeprecateOldKeyboardShortcutsAcceleratorName[];
extern const char kDeprecateOldKeyboardShortcutsAcceleratorDescription[];

extern const char kMemlogName[];
extern const char kMemlogDescription[];
extern const char kMemlogModeMinimal[];
extern const char kMemlogModeAll[];
extern const char kMemlogModeAllRenderers[];
extern const char kMemlogModeBrowser[];
extern const char kMemlogModeGpu[];
extern const char kMemlogModeRendererSampling[];

extern const char kMemlogSamplingRateName[];
extern const char kMemlogSamplingRateDescription[];
extern const char kMemlogSamplingRate10KB[];
extern const char kMemlogSamplingRate50KB[];
extern const char kMemlogSamplingRate100KB[];
extern const char kMemlogSamplingRate500KB[];
extern const char kMemlogSamplingRate1MB[];
extern const char kMemlogSamplingRate5MB[];

extern const char kMemlogStackModeName[];
extern const char kMemlogStackModeDescription[];
extern const char kMemlogStackModeNative[];
extern const char kMemlogStackModeNativeWithThreadNames[];

extern const char kEarlyDocumentSwapForBackForwardTransitionsName[];
extern const char kEarlyDocumentSwapForBackForwardTransitionsDescription[];

extern const char kEnableFriendlierSafeBrowsingSettingsEnhancedProtectionName[];
extern const char
    kEnableFriendlierSafeBrowsingSettingsEnhancedProtectionDescription[];

extern const char kEnableFriendlierSafeBrowsingSettingsStandardProtectionName[];
extern const char
    kEnableFriendlierSafeBrowsingSettingsStandardProtectionDescription[];

extern const char kEnableRedInterstitialFaceliftName[];
extern const char kEnableRedInterstitialFaceliftDescription[];

extern const char kEnableSearchEngineChoiceName[];
extern const char kEnableSearchEngineChoiceDescription[];

extern const char kEnableSuspiciousSiteDetectionRTLookupsName[];
extern const char kEnableSuspiciousSiteDetectionRTLookupsDescription[];

extern const char kEnableTailoredSecurityUpdatedMessagesName[];
extern const char kEnableTailoredSecurityUpdatedMessagesDescription[];

extern const char kEnableFencedFramesName[];
extern const char kEnableFencedFramesDescription[];

extern const char kEnableFencedFramesCrossOriginAutomaticBeaconsName[];
extern const char kEnableFencedFramesCrossOriginAutomaticBeaconsDescription[];

extern const char kEnableFencedFramesDeveloperModeName[];
extern const char kEnableFencedFramesDeveloperModeDescription[];

extern const char kEnableFencedFramesM120FeaturesName[];
extern const char kEnableFencedFramesM120FeaturesDescription[];

extern const char kEnableFencedFramesReportingAttestationsChangeName[];
extern const char kEnableFencedFramesReportingAttestationsChangeDescription[];

extern const char kEnableGamepadButtonAxisEventsName[];
extern const char kEnableGamepadButtonAxisEventsDescription[];

extern const char kEnableGamepadMultitouchName[];
extern const char kEnableGamepadMultitouchDescription[];

extern const char kEnableGamepadTriggerRumbleName[];
extern const char kEnableGamepadTriggerRumbleDescription[];

extern const char kEnableIsolatedWebAppsName[];
extern const char kEnableIsolatedWebAppsDescription[];

#if BUILDFLAG(IS_CHROMEOS)
extern const char kEnableIsolatedWebAppAutomaticUpdatesName[];
extern const char kEnableIsolatedWebAppAutomaticUpdatesDescription[];
#endif

extern const char kEnableIsolatedWebAppDevModeName[];
extern const char kEnableIsolatedWebAppDevModeDescription[];

extern const char kEnableLensStandaloneFlagId[];
extern const char kEnableLensStandaloneName[];
extern const char kEnableLensStandaloneDescription[];

extern const char kEnableManagedConfigurationWebApiName[];
extern const char kEnableManagedConfigurationWebApiDescription[];

extern const char kDownloadNotificationServiceUnifiedAPIName[];
extern const char kDownloadNotificationServiceUnifiedAPIDescription[];

extern const char kDownloadsMigrateToJobsAPIName[];
extern const char kDownloadsMigrateToJobsAPIDescription[];

extern const char kEnablePerfettoSystemTracingName[];
extern const char kEnablePerfettoSystemTracingDescription[];

extern const char kEnablePeripheralCustomizationName[];
extern const char kEnablePeripheralCustomizationDescription[];

extern const char kEnablePeripheralNotificationName[];
extern const char kEnablePeripheralNotificationDescription[];

extern const char kEnablePeripheralsLoggingName[];
extern const char kEnablePeripheralsLoggingDescription[];

extern const char kEnablePixelCanvasRecordingName[];
extern const char kEnablePixelCanvasRecordingDescription[];

extern const char kEnableProcessPerSiteUpToMainFrameThresholdName[];
extern const char kEnableProcessPerSiteUpToMainFrameThresholdDescription[];

extern const char kEnableShortcutCustomizationAppName[];
extern const char kEnableShortcutCustomizationAppDescription[];

extern const char kEnableShortcutCustomizationName[];
extern const char kEnableShortcutCustomizationDescription[];

extern const char kEnableSearchCustomizableShortcutsInLauncherName[];
extern const char kEnableSearchCustomizableShortcutsInLauncherDescription[];

extern const char kEnableShortcutCustomizationLoggingName[];
extern const char kEnableShortcutCustomizationLoggingDescription[];

extern const char kEnableInputDeviceSettingsLoggingName[];
extern const char kEnableInputDeviceSettingsLoggingDescription[];

extern const char kEnableInputDeviceSettingsSplitName[];
extern const char kEnableInputDeviceSettingsSplitDescription[];

extern const char kExperimentalRgbKeyboardPatternsName[];
extern const char kExperimentalRgbKeyboardPatternsDescription[];

extern const char kRetailCouponsName[];
extern const char kRetailCouponsDescription[];

extern const char kDropInputEventsBeforeFirstPaintName[];
extern const char kDropInputEventsBeforeFirstPaintDescription[];

extern const char kBoundaryEventDispatchTracksNodeRemovalName[];
extern const char kBoundaryEventDispatchTracksNodeRemovalDescription[];

extern const char kEnableCssSelectorFragmentAnchorName[];
extern const char kEnableCssSelectorFragmentAnchorDescription[];

extern const char kEnablePreferencesAccountStorageName[];
extern const char kEnablePreferencesAccountStorageDescription[];

extern const char kEnableResamplingScrollEventsExperimentalPredictionName[];
extern const char
    kEnableResamplingScrollEventsExperimentalPredictionDescription[];

extern const char kEnableSystemEntropyOnPerformanceNavigationTimingName[];
extern const char
    kEnableSystemEntropyOnPerformanceNavigationTimingDescription[];

extern const char kEnableWebAuthenticationChromeOSAuthenticatorName[];
extern const char kEnableWebAuthenticationChromeOSAuthenticatorDescription[];

extern const char kEnableZeroCopyTabCaptureName[];
extern const char kEnableZeroCopyTabCaptureDescription[];

extern const char kExperimentalWebAssemblyFeaturesName[];
extern const char kExperimentalWebAssemblyFeaturesDescription[];

extern const char kExperimentalWebAssemblyJSPIName[];
extern const char kExperimentalWebAssemblyJSPIDescription[];

extern const char kEnablePolicyTestPageName[];
extern const char kEnablePolicyTestPageDescription[];

extern const char kEnableUnrestrictedUsbName[];
extern const char kEnableUnrestrictedUsbDescription[];

extern const char kEnableWasmBaselineName[];
extern const char kEnableWasmBaselineDescription[];

extern const char kEnableWasmGarbageCollectionName[];
extern const char kEnableWasmGarbageCollectionDescription[];

extern const char kEnableWasmLazyCompilationName[];
extern const char kEnableWasmLazyCompilationDescription[];

extern const char kEnableWasmRelaxedSimdName[];
extern const char kEnableWasmRelaxedSimdDescription[];

extern const char kEnableWasmStringrefName[];
extern const char kEnableWasmStringrefDescription[];

extern const char kEnableWasmTieringName[];
extern const char kEnableWasmTieringDescription[];

extern const char kEvDetailsInPageInfoName[];
extern const char kEvDetailsInPageInfoDescription[];

extern const char kExperimentalWebPlatformFeaturesName[];
extern const char kExperimentalWebPlatformFeaturesDescription[];

extern const char kSafeBrowsingAsyncRealTimeCheckName[];
extern const char kSafeBrowsingAsyncRealTimeCheckDescription[];

extern const char kSafeBrowsingHashPrefixRealTimeLookupsName[];
extern const char kSafeBrowsingHashPrefixRealTimeLookupsDescription[];

#if BUILDFLAG(ENABLE_EXTENSIONS)
extern const char kExtensionsMenuAccessControlName[];
extern const char kExtensionsMenuAccessControlDescription[];
extern const char kIPHExtensionsMenuFeatureName[];
extern const char kIPHExtensionsMenuFeatureDescription[];
extern const char kIPHExtensionsRequestAccessButtonFeatureName[];
extern const char kIPHExtensionsRequestAccessButtonFeatureDescription[];

extern const char kWebAuthFlowInBrowserTabName[];
extern const char kWebAuthFlowInBrowserTabDescription[];

extern const char kCWSInfoFastCheckName[];
extern const char kCWSInfoFastCheckDescription[];

extern const char kSafetyCheckExtensionsName[];
extern const char kSafetyCheckExtensionsDescription[];

#if BUILDFLAG(IS_CHROMEOS)
extern const char kExtensionWebFileHandlersName[];
extern const char kExtensionWebFileHandlersDescription[];
#endif  // IS_CHROMEOS
#endif  // ENABLE_EXTENSIONS

extern const char kExtensionsOnChromeUrlsName[];
extern const char kExtensionsOnChromeUrlsDescription[];

extern const char kFractionalScrollOffsetsName[];
extern const char kFractionalScrollOffsetsDescription[];

extern const char kFedCmAuthzName[];
extern const char kFedCmAuthzDescription[];

extern const char kFedCmAutoSelectedFlagName[];
extern const char kFedCmAutoSelectedFlagDescription[];

extern const char kFedCmButtonModeName[];
extern const char kFedCmButtonModeDescription[];

extern const char kFedCmDisconnectName[];
extern const char kFedCmDisconnectDescription[];

extern const char kFedCmDomainHintName[];
extern const char kFedCmDomainHintDescription[];

extern const char kFedCmErrorName[];
extern const char kFedCmErrorDescription[];

extern const char kFedCmExemptIdpWithThirdPartyCookiesName[];
extern const char kFedCmExemptIdpWithThirdPartyCookiesDescription[];

extern const char kFedCmIdPRegistrationName[];
extern const char kFedCmIdPRegistrationDescription[];

extern const char kFedCmMetricsEndpointName[];
extern const char kFedCmMetricsEndpointDescription[];

extern const char kFedCmMultiIdpName[];
extern const char kFedCmMultiIdpDescription[];

extern const char kFedCmSelectiveDisclosureName[];
extern const char kFedCmSelectiveDisclosureDescription[];

extern const char kFedCmSkipWellKnownForSameSiteName[];
extern const char kFedCmSkipWellKnownForSameSiteDescription[];

extern const char kFedCmIdpSigninStatusName[];
extern const char kFedCmIdpSigninStatusDescription[];

extern const char kFedCmAddAccountName[];
extern const char kFedCmAddAccountDescription[];

extern const char kFedCmWithoutThirdPartyCookiesName[];
extern const char kFedCmWithoutThirdPartyCookiesDescription[];

extern const char kFedCmWithoutWellKnownEnforcementName[];
extern const char kFedCmWithoutWellKnownEnforcementDescription[];

extern const char kWebIdentityDigitalCredentialsName[];
extern const char kWebIdentityDigitalCredentialsDescription[];

extern const char kFileHandlingIconsName[];
extern const char kFileHandlingIconsDescription[];

extern const char kFileSystemAccessLockingSchemeName[];
extern const char kFileSystemAccessLockingSchemeDescription[];

extern const char kFileSystemAccessPersistentPermissionName[];
extern const char kFileSystemAccessPersistentPermissionDescription[];

extern const char kFileSystemObserverName[];
extern const char kFileSystemObserverDescription[];

#if BUILDFLAG(IS_ANDROID)
extern const char kFillingAcrossAffiliatedWebsitesName[];
extern const char kFillingAcrossAffiliatedWebsitesDescription[];
#endif

extern const char kDrawImmediatelyWhenInteractiveName[];
extern const char kDrawImmediatelyWhenInteractiveDescription[];

extern const char kFillingAcrossGroupedSitesName[];
extern const char kFillingAcrossGroupedSitesDescription[];

extern const char kFluentOverlayScrollbarsName[];
extern const char kFluentOverlayScrollbarsDescription[];

extern const char kFluentScrollbarsName[];
extern const char kFluentScrollbarsDescription[];

extern const char kMutationEventsName[];
extern const char kMutationEventsDescription[];

extern const char kFillOnAccountSelectName[];
extern const char kFillOnAccountSelectDescription[];

extern const char kForceTextDirectionName[];
extern const char kForceTextDirectionDescription[];
extern const char kForceDirectionLtr[];
extern const char kForceDirectionRtl[];

extern const char kForceUiDirectionName[];
extern const char kForceUiDirectionDescription[];

extern const char kForgotPasswordFormSupportName[];
extern const char kForgotPasswordFormSupportDescription[];

extern const char kFullscreenPopupWindowsName[];
extern const char kFullscreenPopupWindowsDescription[];

extern const char kGalleryAppPdfEditNotificationName[];
extern const char kGalleryAppPdfEditNotificationDescription[];

extern const char kMediaRemotingWithoutFullscreenName[];
extern const char kMediaRemotingWithoutFullscreenDescription[];

extern const char kRemotePlaybackBackendName[];
extern const char kRemotePlaybackBackendDescription[];

#if BUILDFLAG(IS_CHROMEOS)
extern const char kGlobalMediaControlsCrOSUpdatedUIName[];
extern const char kGlobalMediaControlsCrOSUpdatedUIDescription[];
#endif

extern const char kGoogleOneOfferFilesBannerName[];
extern const char kGoogleOneOfferFilesBannerDescription[];

extern const char kObservableAPIName[];
extern const char kObservableAPIDescription[];

extern const char kCastStreamingAv1Name[];
extern const char kCastStreamingAv1Description[];

extern const char kCastStreamingHardwareH264Name[];
extern const char kCastStreamingHardwareH264Description[];

extern const char kCastStreamingHardwareVp8Name[];
extern const char kCastStreamingHardwareVp8Description[];

extern const char kCastStreamingPerformanceOverlayName[];
extern const char kCastStreamingPerformanceOverlayDescription[];

extern const char kCastStreamingVp9Name[];
extern const char kCastStreamingVp9Description[];

extern const char kCastEnableStreamingWithHiDPIName[];
extern const char kCastEnableStreamingWithHiDPIDescription[];

extern const char kContextualPageActionsName[];
extern const char kContextualPageActionsDescription[];

extern const char kContextualPageActionsPriceTrackingName[];
extern const char kContextualPageActionsPriceTrackingDescription[];

extern const char kContextualPageActionsReaderModeName[];
extern const char kContextualPageActionsReaderModeDescription[];

extern const char kContextualPageActionsShareModelName[];
extern const char kContextualPageActionsShareModelDescription[];

extern const char kEnableOsIntegrationSubManagersName[];
extern const char kEnableOsIntegrationSubManagersDescription[];

extern const char kGpuRasterizationName[];
extern const char kGpuRasterizationDescription[];

extern const char kHandwritingLegacyRecognitionName[];
extern const char kHandwritingLegacyRecognitionDescription[];

extern const char kHandwritingLibraryDlcName[];
extern const char kHandwritingLibraryDlcDescription[];

extern const char kHardwareMediaKeyHandling[];
extern const char kHardwareMediaKeyHandlingDescription[];

extern const char kHeavyAdPrivacyMitigationsName[];
extern const char kHeavyAdPrivacyMitigationsDescription[];

extern const char kHeavyAdInterventionName[];
extern const char kHeavyAdInterventionDescription[];

extern const char kTabAudioMutingName[];
extern const char kTabAudioMutingDescription[];

extern const char kCrasSplitAlsaUsbInternalName[];
extern const char kCrasSplitAlsaUsbInternalDescription[];

extern const char kPwaRestoreUiName[];
extern const char kPwaRestoreUiDescription[];

extern const char kPwaUniversalInstallUiName[];
extern const char kPwaUniversalInstallUiDescription[];

extern const char kRestoreTabsOnFREName[];
extern const char kRestoreTabsOnFREDescription[];

extern const char kRestoreSyncedPlaceholderTabsName[];
extern const char kRestoreSyncedPlaceholderTabsDescription[];

extern const char kStartSurfaceReturnTimeName[];
extern const char kStartSurfaceReturnTimeDescription[];

extern const char kStorageBucketsName[];
extern const char kStorageBucketsDescription[];

extern const char kHideIncognitoMediaMetadataName[];
extern const char kHideIncognitoMediaMetadataDescription[];

extern const char kHttpsFirstModeIncognitoName[];
extern const char kHttpsFirstModeIncognitoDescription[];

extern const char kHttpsFirstModeV2Name[];
extern const char kHttpsFirstModeV2Description[];

extern const char kHttpsFirstModeV2ForEngagedSitesName[];
extern const char kHttpsFirstModeV2ForEngagedSitesDescription[];

extern const char kHttpsFirstModeForTypicallySecureUsersName[];
extern const char kHttpsFirstModeForTypicallySecureUsersDescription[];

extern const char kHttpsUpgradesName[];
extern const char kHttpsUpgradesDescription[];

extern const char kIgnoreGpuBlocklistName[];
extern const char kIgnoreGpuBlocklistDescription[];

extern const char kIgnoreSyncEncryptionKeysLongMissingName[];
extern const char kIgnoreSyncEncryptionKeysLongMissingDescription[];

extern const char kImprovedKeyboardShortcutsName[];
extern const char kImprovedKeyboardShortcutsDescription[];

extern const char kIncognitoReauthenticationForAndroidName[];
extern const char kIncognitoReauthenticationForAndroidDescription[];

extern const char kIncognitoDownloadsWarningName[];
extern const char kIncognitoDownloadsWarningDescription[];

extern const char kIncognitoNtpRevampName[];
extern const char kIncognitoNtpRevampDescription[];

extern const char kIncognitoScreenshotName[];
extern const char kIncognitoScreenshotDescription[];

extern const char kInProductHelpDemoModeChoiceName[];
extern const char kInProductHelpDemoModeChoiceDescription[];

extern const char kInProductHelpSnoozeName[];
extern const char kInProductHelpSnoozeDescription[];

extern const char kInProductHelpUseClientConfigName[];
extern const char kInProductHelpUseClientConfigDescription[];

#if !BUILDFLAG(IS_ANDROID)
extern const char kUserEducationExperienceVersion2Name[];
extern const char kUserEducationExperienceVersion2Description[];
#endif

extern const char kInsecureDownloadWarningsName[];
extern const char kInsecureDownloadWarningsDescription[];

extern const char kInstallIsolatedWebAppFromUrl[];
extern const char kInstallIsolatedWebAppFromUrlDescription[];

extern const char kInstantHotspotRebrandName[];
extern const char kInstantHotspotRebrandDescription[];

extern const char kIndexedDBCompressValuesWithSnappy[];
extern const char kIndexedDBCompressValuesWithSnappyDescription[];

extern const char kIndexedDBDefaultDurabilityRelaxed[];
extern const char kIndexedDBDefaultDurabilityRelaxedDescription[];

extern const char kIpProtectionProxyOptOutName[];
extern const char kIpProtectionProxyOptOutDescription[];
extern const char kIpProtectionProxyOptOutChoiceDefault[];
extern const char kIpProtectionProxyOptOutChoiceOptOut[];

extern const char kJapaneseOSSettingsName[];
extern const char kJapaneseOSSettingsDescription[];

extern const char kJavascriptHarmonyName[];
extern const char kJavascriptHarmonyDescription[];

extern const char kJavascriptHarmonyShippingName[];
extern const char kJavascriptHarmonyShippingDescription[];

extern const char kJavascriptExperimentalSharedMemoryName[];
extern const char kJavascriptExperimentalSharedMemoryDescription[];

extern const char kJourneysName[];
extern const char kJourneysDescription[];

extern const char kRenameJourneysName[];
extern const char kRenameJourneysDescription[];

extern const char kJourneysLabelsName[];
extern const char kJourneysLabelsDescription[];

extern const char kJourneysShowAllClustersName[];
extern const char kJourneysShowAllClustersDescription[];

extern const char kJourneysIncludeSyncedVisitsName[];
extern const char kJourneysIncludeSyncedVisitsDescription[];

extern const char kJourneysZeroStateFilteringName[];
extern const char kJourneysZeroStateFilteringDescription[];

extern const char kExtractRelatedSearchesFromPrefetchedZPSResponseName[];
extern const char kExtractRelatedSearchesFromPrefetchedZPSResponseDescription[];

extern const char kLargeFaviconFromGoogleName[];
extern const char kLargeFaviconFromGoogleDescription[];

extern const char kLegacyTechReportEnableCookieIssueReportsName[];
extern const char kLegacyTechReportEnableCookieIssueReportsDescription[];

extern const char kLegacyTechReportTopLevelUrlName[];
extern const char kLegacyTechReportTopLevelUrlDescription[];

extern const char kLensRegionSearchStaticPageName[];
extern const char kLensRegionSearchStaticPageDescription[];

extern const char kLensImageFormatOptimizationsName[];
extern const char kLensImageFormatOptimizationsDescription[];

extern const char kLensImageTranslateName[];
extern const char kLensImageTranslateDescription[];

extern const char kCscCompanionEnablePageContentName[];
extern const char kCscCompanionEnablePageContentDescription[];

extern const char kCscForceCompanionPinnedStateName[];
extern const char kCscForceCompanionPinnedStateDescription[];

extern const char kCscSidePanelCompanionName[];
extern const char kCscSidePanelCompanionDescription[];

extern const char kCscVisualQuerySuggestionsName[];
extern const char kCscVisualQuerySuggestionsDescription[];

extern const char kLensOnQuickActionSearchWidgetName[];
extern const char kLensOnQuickActionSearchWidgetDescription[];

extern const char kLocationBarModelOptimizationsName[];
extern const char kLocationBarModelOptimizationsDescription[];

extern const char kLogJsConsoleMessagesName[];
extern const char kLogJsConsoleMessagesDescription[];

extern const char kUndoAutofillName[];
extern const char kUndoAutofillDescription[];

extern const char kMediaRouterCastAllowAllIPsName[];
extern const char kMediaRouterCastAllowAllIPsDescription[];

extern const char kMojoLinuxChannelSharedMemName[];
extern const char kMojoLinuxChannelSharedMemDescription[];

extern const char kUsernameFirstFlowFallbackCrowdsourcingName[];
extern const char kUsernameFirstFlowFallbackCrowdsourcingDescription[];

extern const char kUsernameFirstFlowStoreSeveralValuesName[];
extern const char kUsernameFirstFlowStoreSeveralValuesDescription[];

extern const char kUsernameFirstFlowWithIntermediateValuesName[];
extern const char kUsernameFirstFlowWithIntermediateValuesDescription[];

extern const char kUsernameFirstFlowWithIntermediateValuesPredictionsName[];
extern const char
    kUsernameFirstFlowWithIntermediateValuesPredictionsDescription[];

extern const char kUsernameFirstFlowWithIntermediateValuesVotingName[];
extern const char kUsernameFirstFlowWithIntermediateValuesVotingDescription[];

extern const char kCanvas2DLayersName[];
extern const char kCanvas2DLayersDescription[];

extern const char kEnableMachineLearningModelLoaderWebPlatformApiName[];
extern const char kEnableMachineLearningModelLoaderWebPlatformApiDescription[];

extern const char kWebMachineLearningNeuralNetworkName[];
extern const char kWebMachineLearningNeuralNetworkDescription[];

extern const char kNotificationInteractionHistoryName[];
extern const char kNotificationInteractionHistoryDescription[];

extern const char kNotificationSchedulerName[];
extern const char kNotificationSchedulerDescription[];

extern const char kNotificationSchedulerDebugOptionName[];
extern const char kNotificationSchedulerDebugOptionDescription[];
extern const char kNotificationSchedulerImmediateBackgroundTaskDescription[];

extern const char kNotificationsSystemFlagName[];
extern const char kNotificationsSystemFlagDescription[];

extern const char kOrganicRepeatableQueriesName[];
extern const char kOrganicRepeatableQueriesDescription[];

extern const char kOriginAgentClusterDefaultName[];
extern const char kOriginAgentClusterDefaultDescription[];

extern const char kOriginKeyedProcessesByDefaultName[];
extern const char kOriginKeyedProcessesByDefaultDescription[];

extern const char kOmitCorsClientCertName[];
extern const char kOmitCorsClientCertDescription[];

extern const char kOmniboxActionsInSuggestName[];
extern const char kOmniboxActionsInSuggestDescription[];

extern const char kOmniboxAdaptiveSuggestionsCountName[];
extern const char kOmniboxAdaptiveSuggestionsCountDescription[];

extern const char kOmniboxCalcProviderName[];
extern const char kOmniboxCalcProviderDescription[];

extern const char kOmniboxCompanyEntityIconAdjustmentName[];
extern const char kOmniboxCompanyEntityIconAdjustmentDescription[];

extern const char kOmniboxConsumesImeInsetsName[];
extern const char kOmniboxConsumesImeInsetsDescription[];

extern const char kOmniboxCR23ActionChipsName[];
extern const char kOmniboxCR23ActionChipsDescription[];

extern const char kOmniboxCR23ActionChipsIconsName[];
extern const char kOmniboxCR23ActionChipsIconsDescription[];

extern const char kOmniboxCR23ExpandedStateColorsName[];
extern const char kOmniboxCR23ExpandedStateColorsDescription[];

extern const char kOmniboxCR23ExpandedStateHeightName[];
extern const char kOmniboxCR23ExpandedStateHeightDescription[];

extern const char kOmniboxCR23ExpandedStateLayoutName[];
extern const char kOmniboxCR23ExpandedStateLayoutDescription[];

extern const char kOmniboxCR23ExpandedStateShapeName[];
extern const char kOmniboxCR23ExpandedStateShapeDescription[];

extern const char kOmniboxCR23ExpandedStateSuggestIconsName[];
extern const char kOmniboxCR23ExpandedStateSuggestIconsDescription[];

extern const char kOmniboxCR23SteadyStateIconsName[];
extern const char kOmniboxCR23SteadyStateIconsDescription[];

extern const char kOmniboxCR23SuggestionHoverFillShapeName[];
extern const char kOmniboxCR23SuggestionHoverFillShapeDescription[];

extern const char kOmniboxActionsUISimplificationName[];
extern const char kOmniboxActionsUISimplificationDescription[];

extern const char kOmniboxKeywordModeRefreshName[];
extern const char kOmniboxKeywordModeRefreshDescription[];

extern const char kOmniboxDomainSuggestionsName[];
extern const char kOmniboxDomainSuggestionsDescription[];

extern const char kOmniboxForceAllowedToBeDefaultName[];
extern const char kOmniboxForceAllowedToBeDefaultDescription[];

extern const char kOmniboxGM3SteadyStateBackgroundColorName[];
extern const char kOmniboxGM3SteadyStateBackgroundColorDescription[];

extern const char kOmniboxGM3SteadyStateHeightName[];
extern const char kOmniboxGM3SteadyStateHeightDescription[];

extern const char kOmniboxGM3SteadyStateTextStyleName[];
extern const char kOmniboxGM3SteadyStateTextStyleDescription[];

extern const char kOmniboxGM3SteadyStateTextColorName[];
extern const char kOmniboxGM3SteadyStateTextColorDescription[];

extern const char kOmniboxGroupingFrameworkNonZPSName[];
extern const char kOmniboxGroupingFrameworkZPSName[];
extern const char kOmniboxGroupingFrameworkDescription[];

extern const char kOmniboxMlLogUrlScoringSignalsName[];
extern const char kOmniboxMlLogUrlScoringSignalsDescription[];

extern const char kOmniboxMlUrlScoringName[];
extern const char kOmniboxMlUrlScoringDescription[];

extern const char kOmniboxMlUrlScoringModelName[];
extern const char kOmniboxMlUrlScoringModelDescription[];

extern const char kOmniboxMlUrlSearchBlendingName[];
extern const char kOmniboxMlUrlSearchBlendingDescription[];

extern const char kOmniboxModernizeVisualUpdateName[];
extern const char kOmniboxModernizeVisualUpdateDescription[];

extern const char kOmniboxMatchToolbarAndStatusBarColorName[];
extern const char kOmniboxMatchToolbarAndStatusBarColorDescription[];

extern const char kOmniboxMostVisitedTilesHorizontalRenderGroupName[];
extern const char kOmniboxMostVisitedTilesHorizontalRenderGroupDescription[];

extern const char kOmniboxMostVisitedTilesTitleWrapAroundName[];
extern const char kOmniboxMostVisitedTilesTitleWrapAroundDescription[];

extern const char kOmniboxOnDeviceHeadSuggestionsName[];
extern const char kOmniboxOnDeviceHeadSuggestionsDescription[];

extern const char kOmniboxOnDeviceHeadSuggestionsIncognitoName[];
extern const char kOmniboxOnDeviceHeadSuggestionsIncognitoDescription[];

extern const char kOmniboxOnDeviceTailSuggestionsName[];
extern const char kOmniboxOnDeviceTailSuggestionsDescription[];

extern const char kOmniboxPrefBasedDataCollectionConsentHelperName[];
extern const char kOmniboxPrefBasedDataCollectionConsentHelperDescription[];

extern const char kOmniboxQueryTilesInZPSOnNTPName[];
extern const char kOmniboxQueryTilesInZPSOnNTPDesc[];

extern const char kOmniboxSuppressClipboardSuggestionAfterFirstUsedName[];
extern const char
    kOmniboxSuppressClipboardSuggestionAfterFirstUsedDescription[];

extern const char kOmniboxRichAutocompletionPromisingName[];
extern const char kOmniboxRichAutocompletionPromisingDescription[];

extern const char kOmniboxLocalHistoryZeroSuggestBeyondNTPName[];
extern const char kOmniboxLocalHistoryZeroSuggestBeyondNTPDescription[];

extern const char kOmniboxOnClobberFocusTypeOnContentName[];
extern const char kOmniboxOnClobberFocusTypeOnContentDescription[];

extern const char kOmniboxShortcutBoostName[];
extern const char kOmniboxShortcutBoostDescription[];

extern const char kOmniboxShortcutExpandingName[];
extern const char kOmniboxShortcutExpandingDescription[];

extern const char kOmniboxSimplifiedUiUniformRowHeightName[];
extern const char kOmniboxSimplifiedUiUniformRowHeightDescription[];

extern const char kOmniboxSimplifiedUiSquareSuggestIconName[];
extern const char kOmniboxSimplifiedUiSquareSuggestIconDescription[];

extern const char kOmniboxZeroSuggestPrefetchingName[];
extern const char kOmniboxZeroSuggestPrefetchingDescription[];

extern const char kOmniboxZeroSuggestPrefetchingOnSRPName[];
extern const char kOmniboxZeroSuggestPrefetchingOnSRPDescription[];

extern const char kOmniboxZeroSuggestPrefetchingOnWebName[];
extern const char kOmniboxZeroSuggestPrefetchingOnWebDescription[];

extern const char kOmniboxZeroSuggestInMemoryCachingName[];
extern const char kOmniboxZeroSuggestInMemoryCachingDescription[];

extern const char kOmniboxMaxZeroSuggestMatchesName[];
extern const char kOmniboxMaxZeroSuggestMatchesDescription[];

extern const char kOmniboxUIMaxAutocompleteMatchesName[];
extern const char kOmniboxUIMaxAutocompleteMatchesDescription[];

#if BUILDFLAG(IS_ANDROID)
extern const char kOmnibox2023RefreshConnectionSecurityIndicatorsName[];
extern const char kOmnibox2023RefreshConnectionSecurityIndicatorsDescription[];
#endif  // BUILDFLAG(IS_ANDROID)

extern const char kWebUIOmniboxPopupName[];
extern const char kWebUIOmniboxPopupDescription[];

extern const char kOmniboxMaxURLMatchesName[];
extern const char kOmniboxMaxURLMatchesDescription[];

extern const char kOmniboxDynamicMaxAutocompleteName[];
extern const char kOmniboxDynamicMaxAutocompleteDescription[];

extern const char kOneTimePermissionName[];
extern const char kOneTimePermissionDescription[];

extern const char kOptimizationGuideDebugLogsName[];
extern const char kOptimizationGuideDebugLogsDescription[];

extern const char kOptimizationGuideModelExecutionName[];
extern const char kOptimizationGuideModelExecutionDescription[];

extern const char kOptimizationGuideOnDeviceModelName[];
extern const char kOptimizationGuideOnDeviceModelDescription[];

extern const char kOptimizationGuidePersonalizedFetchingName[];
extern const char kOptimizationGuidePersonalizedFetchingDescription[];

extern const char kOptimizationGuidePushNotificationName[];
extern const char kOptimizationGuidePushNotificationDescription[];

extern const char kOrcaKeyName[];
extern const char kOrcaKeyDescription[];

extern const char kOsFeedbackDialogName[];
extern const char kOsFeedbackDialogDescription[];

extern const char kOsFeedbackJellyName[];
extern const char kOsFeedbackJellyDescription[];

extern const char kOsSettingsAppNotificationsPageName[];
extern const char kOsSettingsAppNotificationsPageDescription[];

extern const char kOsSettingsDeprecateSyncMetricsToggleName[];
extern const char kOsSettingsDeprecateSyncMetricsToggleDescription[];

extern const char kOverviewButtonName[];
extern const char kOverviewButtonDescription[];

extern const char kOverviewDeskNavigationName[];
extern const char kOverviewDeskNavigationDescription[];

extern const char kOverlayScrollbarsName[];
extern const char kOverlayScrollbarsDescription[];

extern const char kOverlayStrategiesName[];
extern const char kOverlayStrategiesDescription[];
extern const char kOverlayStrategiesDefault[];
extern const char kOverlayStrategiesNone[];
extern const char kOverlayStrategiesUnoccludedFullscreen[];
extern const char kOverlayStrategiesUnoccluded[];
extern const char kOverlayStrategiesOccludedAndUnoccluded[];

extern const char kOverscrollHistoryNavigationName[];
extern const char kOverscrollHistoryNavigationDescription[];
extern const char kOverscrollHistoryNavigationSettingName[];
extern const char kOverscrollHistoryNavigationSettingDescription[];

extern const char kPageContentAnnotationsName[];
extern const char kPageContentAnnotationsDescription[];

extern const char kPageContentAnnotationsPersistSalientImageMetadataName[];
extern const char
    kPageContentAnnotationsPersistSalientImageMetadataDescription[];

extern const char kPageContentAnnotationsRemotePageMetadataName[];
extern const char kPageContentAnnotationsRemotePageMetadataDescription[];

extern const char kPageImageServiceOptimizationGuideSalientImagesName[];
extern const char kPageImageServiceOptimizationGuideSalientImagesDescription[];

extern const char kPageImageServiceSuggestPoweredImagesName[];
extern const char kPageImageServiceSuggestPoweredImagesDescription[];

extern const char kPageInfoAboutThisPagePersistentEntryName[];
extern const char kPageInfoAboutThisPagePersistentEntryDescription[];

extern const char kPageInfoCookiesSubpageName[];
extern const char kPageInfoCookiesSubpageDescription[];

extern const char kPageInfoHideSiteSettingsName[];
extern const char kPageInfoHideSiteSettingsDescription[];

extern const char kPageInfoHistoryDesktopName[];
extern const char kPageInfoHistoryDesktopDescription[];

extern const char kPageVisibilityPageContentAnnotationsName[];
extern const char kPageVisibilityPageContentAnnotationsDescription[];

extern const char kParallelDownloadingName[];
extern const char kParallelDownloadingDescription[];

extern const char kPasswordGenerationExperimentName[];
extern const char kPasswordGenerationExperimentDescription[];

extern const char kPasswordGenerationStrongLabelExperimentName[];
extern const char kPasswordGenerationStrongLabelExperimentDescription[];

extern const char kPasswordManagerShadowDomSupportName[];
extern const char kPasswordManagerShadowDomSupportDescription[];

extern const char kPasswordParsingOnSaveUsesPredictionsName[];
extern const char kPasswordParsingOnSaveUsesPredictionsDescription[];

extern const char kPdfOcrName[];
extern const char kPdfOcrDescription[];

extern const char kPdfXfaFormsName[];
extern const char kPdfXfaFormsDescription[];

extern const char kAutoWebContentsDarkModeName[];
extern const char kAutoWebContentsDarkModeDescription[];

extern const char kForcedColorsName[];
extern const char kForcedColorsDescription[];

extern const char kWindowsScrollingPersonalityName[];
extern const char kWindowsScrollingPersonalityDescription[];

extern const char kImprovedSemanticsActivityIndicatorsName[];
extern const char kImprovedSemanticsActivityIndicatorsDescription[];

extern const char kLeftHandSideActivityIndicatorsName[];
extern const char kLeftHandSideActivityIndicatorsDescription[];

extern const char kPermissionPredictionsName[];
extern const char kPermissionPredictionsDescription[];

extern const char kPermissionQuietChipName[];
extern const char kPermissionQuietChipDescription[];

extern const char kPermissionStorageAccessAPIName[];
extern const char kPermissionStorageAccessAPIDescription[];

extern const char kShowRelatedWebsiteSetsPermissionGrantsName[];
extern const char kShowRelatedWebsiteSetsPermissionGrantsDescription[];

extern const char kPowerBookmarkBackendName[];
extern const char kPowerBookmarkBackendDescription[];

extern const char kSpeculationRulesPrerenderingTargetHintName[];
extern const char kSpeculationRulesPrerenderingTargetHintDescription[];

extern const char kSupportSearchSuggestionForPrerender2Name[];
extern const char kSupportSearchSuggestionForPrerender2Description[];

extern const char kEnableOmniboxSearchPrefetchName[];
extern const char kEnableOmniboxSearchPrefetchDescription[];

extern const char kEnableOmniboxClientSearchPrefetchName[];
extern const char kEnableOmniboxClientSearchPrefetchDescription[];

extern const char kPreloadingOnPerformancePageName[];
extern const char kPreloadingOnPerformancePageDescription[];

extern const char kCbdTimeframeRequiredName[];
extern const char kCbdTimeframeRequiredDescription[];

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS_ASH)
extern const char kPolicyIndicationForManagedDefaultSearchName[];
extern const char kPolicyIndicationForManagedDefaultSearchDescription[];
#endif

extern const char kPriceChangeModuleName[];
extern const char kPriceChangeModuleDescription[];

extern const char kPrivacyGuide3Name[];
extern const char kPrivacyGuide3Description[];

extern const char kPrivacyGuidePreloadName[];
extern const char kPrivacyGuidePreloadDescription[];

extern const char kPrivacyGuideAndroid3Name[];
extern const char kPrivacyGuideAndroid3Description[];

extern const char kPrivacyGuidePreloadAndroidName[];
extern const char kPrivacyGuidePreloadAndroidDescription[];

extern const char kPrivacySandboxAdsAPIsOverrideName[];
extern const char kPrivacySandboxAdsAPIsOverrideDescription[];

extern const char kPrivacySandboxAdsNoticeCCTName[];
extern const char kPrivacySandboxAdsNoticeCCTDescription[];

extern const char kPrivacySandboxEnrollmentOverridesName[];
extern const char kPrivacySandboxEnrollmentOverridesDescription[];

extern const char kPrivacySandboxInternalsName[];
extern const char kPrivacySandboxInternalsDescription[];

extern const char kPrivacySandboxProactiveTopicsBlockingName[];
extern const char kPrivacySandboxProactiveTopicsBlockingDescription[];

extern const char kPrivateAggregationDeveloperModeName[];
extern const char kPrivateAggregationDeveloperModeDescription[];

extern const char kProtectedAudiencesConsentedDebugTokenName[];
extern const char kProtectedAudiencesConsentedDebugTokenDescription[];

extern const char kPullToRefreshName[];
extern const char kPullToRefreshDescription[];
extern const char kPullToRefreshEnabledTouchscreen[];

extern const char kPWAsDefaultOfflinePageName[];
extern const char kPWAsDefaultOfflinePageDescription[];

extern const char kPwaUpdateDialogForAppIconName[];
extern const char kPwaUpdateDialogForAppIconDescription[];

extern const char kRenderDocumentName[];
extern const char kRenderDocumentDescription[];

extern const char kQuicName[];
extern const char kQuicDescription[];

extern const char kQuickAppAccessTestUIName[];
extern const char kQuickAppAccessTestUIDescription[];

extern const char kQuickDimName[];
extern const char kQuickDimDescription[];

extern const char kQuickIntensiveWakeUpThrottlingAfterLoadingName[];
extern const char kQuickIntensiveWakeUpThrottlingAfterLoadingDescription[];

extern const char kQuickDeleteForAndroidName[];
extern const char kQuickDeleteForAndroidDescription[];

extern const char kSettingsAppNotificationSettingsName[];
extern const char kSettingsAppNotificationSettingsDescription[];

extern const char kReadLaterFlagId[];
extern const char kReadLaterName[];
extern const char kReadLaterDescription[];

extern const char kRecordWebAppDebugInfoName[];
extern const char kRecordWebAppDebugInfoDescription[];

extern const char kRestrictGamepadAccessName[];
extern const char kRestrictGamepadAccessDescription[];

extern const char kRoundedDisplay[];
extern const char kRoundedDisplayDescription[];

extern const char kRoundedWindows[];
extern const char kRoundedWindowsDescription[];

extern const char kMBIModeName[];
extern const char kMBIModeDescription[];

extern const char kSafetyCheckNotificationPermissionsName[];
extern const char kSafetyCheckNotificationPermissionsDescription[];

extern const char kSafetyCheckUnusedSitePermissionsName[];
extern const char kSafetyCheckUnusedSitePermissionsDescription[];

extern const char kSafetyHubName[];
extern const char kSafetyHubDescription[];

extern const char kSameAppWindowCycleName[];
extern const char kSameAppWindowCycleDescription[];

extern const char kTestThirdPartyCookiePhaseoutName[];
extern const char kTestThirdPartyCookiePhaseoutDescription[];

extern const char kThirdPartyStoragePartitioningName[];
extern const char kThirdPartyStoragePartitioningDescription[];

extern const char kScreenSaverDurationName[];
extern const char kScreenSaverDurationDescription[];

extern const char kScrollableTabStripFlagId[];
extern const char kScrollableTabStripName[];
extern const char kScrollableTabStripDescription[];

extern const char kTabScrollingButtonPositionFlagId[];
extern const char kTabScrollingButtonPositionName[];
extern const char kTabScrollingButtonPositionDescription[];

extern const char kScrollableTabStripWithDraggingFlagId[];
extern const char kScrollableTabStripWithDraggingName[];
extern const char kScrollableTabStripWithDraggingDescription[];

extern const char kScrollableTabStripOverflowFlagId[];
extern const char kScrollableTabStripOverflowName[];
extern const char kScrollableTabStripOverflowDescription[];

extern const char kSplitTabStripName[];
extern const char kSplitTabStripDescription[];

extern const char kDynamicSearchUpdateAnimationName[];
extern const char kDynamicSearchUpdateAnimationDescription[];

extern const char kSecurePaymentConfirmationDebugName[];
extern const char kSecurePaymentConfirmationDebugDescription[];

extern const char kSecurePaymentConfirmationExtensionsName[];
extern const char kSecurePaymentConfirmationExtensionsDescription[];

#if BUILDFLAG(IS_ANDROID)
extern const char kSeedAccountsRevampName[];
extern const char kSeedAccountsRevampDescription[];
#endif  // BUILDFLAG(IS_ANDROID)

extern const char kSidePanelJourneysFlagId[];
extern const char kSidePanelJourneysName[];
extern const char kSidePanelJourneysDescription[];

extern const char kSidePanelJourneysQuerylessFlagId[];
extern const char kSidePanelJourneysQuerylessName[];
extern const char kSidePanelJourneysQuerylessDescription[];

extern const char kSidePanelPinningFlagId[];
extern const char kSidePanelPinningName[];
extern const char kSidePanelPinningDescription[];

extern const char kSharedZstdName[];
extern const char kSharedZstdDescription[];

extern const char kSharingDesktopScreenshotsName[];
extern const char kSharingDesktopScreenshotsDescription[];

extern const char kShelfStackedHotseatName[];
extern const char kShelfStackedHotseatDescription[];

extern const char kShowAutofillSignaturesName[];
extern const char kShowAutofillSignaturesDescription[];

extern const char kShowAutofillTypePredictionsName[];
extern const char kShowAutofillTypePredictionsDescription[];

extern const char kShowPerformanceMetricsHudName[];
extern const char kShowPerformanceMetricsHudDescription[];

extern const char kShowOverdrawFeedbackName[];
extern const char kShowOverdrawFeedbackDescription[];

extern const char kSiteInstanceGroupsForDataUrlsName[];
extern const char kSiteInstanceGroupsForDataUrlsDescription[];

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS_ASH)
extern const char kSiteSearchSettingsPolicyName[];
extern const char kSiteSearchSettingsPolicyDescription[];
#endif

extern const char kSkiaRendererName[];
extern const char kSkiaRendererDescription[];

extern const char kIsolateOriginsName[];
extern const char kIsolateOriginsDescription[];

extern const char kIsolationByDefaultName[];
extern const char kIsolationByDefaultDescription[];

extern const char kSiteIsolationOptOutName[];
extern const char kSiteIsolationOptOutDescription[];
extern const char kSiteIsolationOptOutChoiceDefault[];
extern const char kSiteIsolationOptOutChoiceOptOut[];

extern const char kSkiaGraphiteName[];
extern const char kSkiaGraphiteDescription[];

extern const char kSmoothScrollingName[];
extern const char kSmoothScrollingDescription[];

extern const char kSplitCacheByNetworkIsolationKeyName[];
extern const char kSplitCacheByNetworkIsolationKeyDescription[];

extern const char kStrictOriginIsolationName[];
extern const char kStrictOriginIsolationDescription[];

extern const char kStylusBatteryStatusName[];
extern const char kStylusBatteryStatusDescription[];

extern const char kSupportTool[];
extern const char kSupportToolDescription[];

extern const char kSupportToolScreenshot[];
extern const char kSupportToolScreenshotDescription[];

extern const char kSuppressTextMessagesName[];
extern const char kSuppressTextMessagesDescription[];

extern const char kSuppressToolbarCapturesName[];
extern const char kSuppressToolbarCapturesDescription[];

extern const char kSystemKeyboardLockName[];
extern const char kSystemKeyboardLockDescription[];

extern const char kSystemSoundsName[];
extern const char kSystemSoundsDescription[];

extern const char kAutofillContentEditablesId[];
extern const char kAutofillContentEditablesName[];
extern const char kAutofillContentEditablesDescription[];

extern const char kSyncAutofillWalletCredentialDataName[];
extern const char kSyncAutofillWalletCredentialDataDescription[];

extern const char kSyncAutofillWalletUsageDataName[];
extern const char kSyncAutofillWalletUsageDataDescription[];

extern const char kSyncSandboxName[];
extern const char kSyncSandboxDescription[];

extern const char kSyncSessionOnVisibilityChangedName[];
extern const char kSyncSessionOnVisibilityChangedDescription[];

extern const char kSyncShowIdentityErrorsForSignedInUsersName[];
extern const char kSyncShowIdentityErrorsForSignedInUsersDescription[];

extern const char kSyncTrustedVaultPassphrasePromoName[];
extern const char kSyncTrustedVaultPassphrasePromoDescription[];

extern const char kSystemProxyForSystemServicesName[];
extern const char kSystemProxyForSystemServicesDescription[];

extern const char kTabDragDropName[];
extern const char kTabDragDropDescription[];

extern const char kTabAndLinkDragDropName[];
extern const char kTabAndLinkDragDropDescription[];

extern const char kTabEngagementReportingName[];
extern const char kTabEngagementReportingDescription[];

extern const char kCommerceDeveloperName[];
extern const char kCommerceDeveloperDescription[];

extern const char kTabGroupsSaveId[];
extern const char kTabGroupsSaveName[];
extern const char kTabGroupsSaveDescription[];

extern const char kTabHoverCardImagesName[];
extern const char kTabHoverCardImagesDescription[];

extern const char kTabSearchFuzzySearchName[];
extern const char kTabSearchFuzzySearchDescription[];

extern const char kTearOffWebAppAppTabOpensWebAppWindowName[];
extern const char kTearOffWebAppAppTabOpensWebAppWindowDescription[];

extern const char kTabletToolbarReorderingAndroidName[];
extern const char kTabletToolbarReorderingAndroidDescription[];

extern const char kTabResumptionModuleAndroidName[];
extern const char kTabResumptionModuleAndroidDescription[];

extern const char kTabStripStartupRefactoringName[];
extern const char kTabStripStartupRefactoringDescription[];

extern const char kDelayTempStripRemovalName[];
extern const char kDelayTempStripRemovalDescription[];

extern const char kTextBasedAudioDescriptionName[];
extern const char kTextBasedAudioDescriptionDescription[];

extern const char kTextInShelfName[];
extern const char kTextInShelfDescription[];

extern const char kTintCompositedContentName[];
extern const char kTintCompositedContentDescription[];

extern const char kTopChromeTouchUiName[];
extern const char kTopChromeTouchUiDescription[];

extern const char
    kThrottleDisplayNoneAndVisibilityHiddenCrossOriginIframesName[];
extern const char
    kThrottleDisplayNoneAndVisibilityHiddenCrossOriginIframesDescription[];

extern const char kNewBaseUrlInheritanceBehaviorName[];
extern const char kNewBaseUrlInheritanceBehaviorDescription[];

extern const char kTouchDragDropName[];
extern const char kTouchDragDropDescription[];

extern const char kTouchSelectionStrategyName[];
extern const char kTouchSelectionStrategyDescription[];
extern const char kTouchSelectionStrategyCharacter[];
extern const char kTouchSelectionStrategyDirection[];

extern const char kTouchTextEditingRedesignName[];
extern const char kTouchTextEditingRedesignDescription[];

extern const char kTranslateForceTriggerOnEnglishName[];
extern const char kTranslateForceTriggerOnEnglishDescription[];

extern const char kTreatInsecureOriginAsSecureName[];
extern const char kTreatInsecureOriginAsSecureDescription[];

extern const char kTailoredSecurityRetryForSyncUsersName[];
extern const char kTailoredSecurityRetryForSyncUsersDescription[];

extern const char kTPCPhaseOutFacilitatedTestingName[];
extern const char kTPCPhaseOutFacilitatedTestingDescription[];

extern const char kTpcdHeuristicsGrantsName[];
extern const char kTpcdHeuristicsGrantsDescription[];

extern const char kTpcdMetadataGrantsName[];
extern const char kTpcdMetadataGrantsDescription[];

extern const char kTpcdTrialSettingsName[];
extern const char kTpcdTrialSettingsDescription[];

extern const char kTopLevelTpcdTrialSettingsName[];
extern const char kTopLevelTpcdTrialSettingsDescription[];

extern const char kIpProtectionV1Name[];
extern const char kIpProtectionV1Description[];

extern const char kTrackingProtection3pcdName[];
extern const char kTrackingProtection3pcdDescription[];

extern const char kTrackingProtectionOnboardingRollbackName[];
extern const char kTrackingProtectionOnboardingRollbackDescription[];

extern const char kTrustedVaultFrequentDegradedRecoverabilityPollingName[];
extern const char
    kTrustedVaultFrequentDegradedRecoverabilityPollingDescription[];

extern const char kUnifiedPasswordManagerAndroidReenrollmentName[];
extern const char kUnifiedPasswordManagerAndroidReenrollmentDescription[];

extern const char kUnsafeWebGPUName[];
extern const char kUnsafeWebGPUDescription[];

extern const char kUiPartialSwapName[];
extern const char kUiPartialSwapDescription[];

extern const char kUIEnableSharedImageCacheForGpuName[];
extern const char kUIEnableSharedImageCacheForGpuDescription[];

extern const char kUseNAT64ForIPv4LiteralName[];
extern const char kUseNAT64ForIPv4LiteralDescription[];

extern const char kUserNotesSidePanelName[];
extern const char kUserNotesSidePanelDescription[];

extern const char kUseSearchClickForRightClickName[];
extern const char kUseSearchClickForRightClickDescription[];

extern const char kVerticalAutomotiveBackButtonToolbarName[];
extern const char kVerticalAutomotiveBackButtonToolbarDescription[];

extern const char kVideoConferenceName[];
extern const char kVideoConferenceDescription[];

extern const char kVcBackgroundReplaceName[];
extern const char kVcBackgroundReplaceDescription[];

extern const char kVcSegmentationModelName[];
extern const char kVcSegmentationModelDescription[];

extern const char kVcLightIntensityName[];
extern const char kVcLightIntensityDescription[];

extern const char kVcWebApiName[];
extern const char kVcWebApiDescription[];

extern const char kViewportSegmentsName[];
extern const char kViewportSegmentsDescription[];

extern const char kV8VmFutureName[];
extern const char kV8VmFutureDescription[];

extern const char kGlobalVaapiLockName[];
extern const char kGlobalVaapiLockDescription[];

extern const char kWalletServiceUseSandboxName[];
extern const char kWalletServiceUseSandboxDescription[];

extern const char kTaskManagerEndProcessDisabledForExtensionName[];
extern const char kTaskManagerEndProcessDisabledForExtensionDescription[];

extern const char kWallpaperFastRefreshName[];
extern const char kWallpaperFastRefreshDescription[];

extern const char kWallpaperGooglePhotosSharedAlbumsName[];
extern const char kWallpaperGooglePhotosSharedAlbumsDescription[];

extern const char kWallpaperPerDeskName[];
extern const char kWallpaperPerDeskDescription[];

extern const char kWebAuthnScreenReaderModeName[];
extern const char kWebAuthnScreenReaderModeDescription[];

extern const char kWebBluetoothName[];
extern const char kWebBluetoothDescription[];

extern const char kWebBluetoothNewPermissionsBackendName[];
extern const char kWebBluetoothNewPermissionsBackendDescription[];

extern const char kWebContentsCaptureHiDpiName[];
extern const char kWebContentsCaptureHiDpiDescription[];

extern const char kWebMidiName[];
extern const char kWebMidiDescription[];

extern const char kWebOtpBackendName[];
extern const char kWebOtpBackendDescription[];
extern const char kWebOtpBackendSmsVerification[];
extern const char kWebOtpBackendUserConsent[];
extern const char kWebOtpBackendAuto[];

extern const char kWebglDeveloperExtensionsName[];
extern const char kWebglDeveloperExtensionsDescription[];

extern const char kWebglDraftExtensionsName[];
extern const char kWebglDraftExtensionsDescription[];

extern const char kWebGpuDeveloperFeaturesName[];
extern const char kWebGpuDeveloperFeaturesDescription[];

extern const char kWebPaymentsExperimentalFeaturesName[];
extern const char kWebPaymentsExperimentalFeaturesDescription[];

extern const char kAddIdentityInCanMakePaymentEventName[];
extern const char kAddIdentityInCanMakePaymentEventDescription[];

extern const char kAppStoreBillingDebugName[];
extern const char kAppStoreBillingDebugDescription[];

extern const char kWebrtcHideLocalIpsWithMdnsName[];
extern const char kWebrtcHideLocalIpsWithMdnsDecription[];

extern const char kWebRtcAllowInputVolumeAdjustmentName[];
extern const char kWebRtcAllowInputVolumeAdjustmentDescription[];

extern const char kWebRtcApmDownmixCaptureAudioMethodName[];
extern const char kWebRtcApmDownmixCaptureAudioMethodDescription[];

extern const char kWebrtcHwDecodingName[];
extern const char kWebrtcHwDecodingDescription[];

extern const char kWebrtcHwEncodingName[];
extern const char kWebrtcHwEncodingDescription[];

extern const char kWebRtcRemoteEventLogName[];
extern const char kWebRtcRemoteEventLogDescription[];

extern const char kWebrtcUseMinMaxVEADimensionsName[];
extern const char kWebrtcUseMinMaxVEADimensionsDescription[];

extern const char kWebTransportDeveloperModeName[];
extern const char kWebTransportDeveloperModeDescription[];

extern const char kWebUsbDeviceDetectionName[];
extern const char kWebUsbDeviceDetectionDescription[];

extern const char kWebXrForceRuntimeName[];
extern const char kWebXrForceRuntimeDescription[];

extern const char kWebXrRuntimeChoiceNone[];
extern const char kWebXrRuntimeChoiceCardboard[];
extern const char kWebXrRuntimeChoiceOpenXR[];

extern const char kWebXrHandInputName[];
extern const char kWebXrHandInputDescription[];

extern const char kWebXrIncubationsName[];
extern const char kWebXrIncubationsDescription[];

extern const char kZeroCopyName[];
extern const char kZeroCopyDescription[];

extern const char kZstdContentEncodingName[];
extern const char kZstdContentEncodingDescription[];

extern const char kEnableVulkanName[];
extern const char kEnableVulkanDescription[];

extern const char kDefaultAngleVulkanName[];
extern const char kDefaultAngleVulkanDescription[];

extern const char kVulkanFromAngleName[];
extern const char kVulkanFromAngleDescription[];

extern const char kResponsiveToolbarName[];
extern const char kResponsiveToolbarDescription[];

extern const char kSharedHighlightingManagerName[];
extern const char kSharedHighlightingManagerDescription[];

extern const char kDraw1PredictedPoint12Ms[];
extern const char kDraw2PredictedPoints6Ms[];
extern const char kDraw1PredictedPoint6Ms[];
extern const char kDraw2PredictedPoints3Ms[];
extern const char kDrawPredictedPointsDescription[];
extern const char kDrawPredictedPointsName[];

extern const char kSanitizerApiName[];
extern const char kSanitizerApiDescription[];

extern const char kUsePassthroughCommandDecoderName[];
extern const char kUsePassthroughCommandDecoderDescription[];

extern const char kUseMultiPlaneFormatForHardwareVideoName[];
extern const char kUseMultiPlaneFormatForHardwareVideoDescription[];

extern const char kUseMultiPlaneFormatForSoftwareVideoName[];
extern const char kUseMultiPlaneFormatForSoftwareVideoDescription[];

extern const char kReduceAcceptLanguageName[];
extern const char kReduceAcceptLanguageDescription[];

extern const char kReduceTransferSizeUpdatedIPCName[];
extern const char kReduceTransferSizeUpdatedIPCDescription[];

extern const char kResetShortcutCustomizationsName[];
extern const char kResetShortcutCustomizationsDescription[];

extern const char kSkipServiceWorkerFetchHandlerName[];
extern const char kSkipServiceWorkerFetchHandlerDescription[];

extern const char kWebSQLAccessName[];
extern const char kWebSQLAccessDescription[];

extern const char kUseGpuSchedulerDfsName[];
extern const char kUseGpuSchedulerDfsDescription[];

extern const char kUseIDNA2008NonTransitionalName[];
extern const char kUseIDNA2008NonTransitionalDescription[];

extern const char kEnablePasswordSharingName[];
extern const char kEnablePasswordSharingDescription[];

// Android --------------------------------------------------------------------

#if BUILDFLAG(IS_ANDROID)

extern const char kAccessibilityIncludeLongClickActionName[];
extern const char kAccessibilityIncludeLongClickActionDescription[];
extern const char kAccessibilityPageZoomName[];
extern const char kAccessibilityPageZoomDescription[];
extern const char kAccessibilityPageZoomEnhancementsName[];
extern const char kAccessibilityPageZoomEnhancementsDescription[];
extern const char kAccessibilityPerformanceFilteringName[];
extern const char kAccessibilityPerformanceFilteringDescription[];
extern const char kAccessibilitySnapshotStressTestsName[];
extern const char kAccessibilitySnapshotStressTestsDescription[];

extern const char kAdaptiveButtonInTopToolbarName[];
extern const char kAdaptiveButtonInTopToolbarDescription[];
extern const char kAdaptiveButtonInTopToolbarTranslateName[];
extern const char kAdaptiveButtonInTopToolbarTranslateDescription[];
extern const char kAdaptiveButtonInTopToolbarAddToBookmarksName[];
extern const char kAdaptiveButtonInTopToolbarAddToBookmarksDescription[];
extern const char kAdaptiveButtonInTopToolbarCustomizationName[];
extern const char kAdaptiveButtonInTopToolbarCustomizationDescription[];

extern const char kAddToHomescreenIPHName[];
extern const char kAddToHomescreenIPHDescription[];

extern const char kAImageReaderName[];
extern const char kAImageReaderDescription[];

extern const char kAndroidSurfaceControlName[];
extern const char kAndroidSurfaceControlDescription[];

extern const char kAnimatedImageDragShadowName[];
extern const char kAnimatedImageDragShadowDescription[];
extern const char kAndroidImprovedBookmarksName[];
extern const char kAndroidImprovedBookmarksDescription[];

extern const char kAndroidElegantTextHeightName[];
extern const char kAndroidElegantTextHeightDescription[];

extern const char kAndroidHatsRefactorName[];
extern const char kAndroidHatsRefactorDescription[];

extern const char kAndroidHubName[];
extern const char kAndroidHubDescription[];

extern const char kAppSpecificHistoryName[];
extern const char kAppSpecificHistoryDescription[];

extern const char kArchiveTabServiceName[];
extern const char kArchiveTabServiceDescription[];

extern const char kBackGestureActivityTabProviderName[];
extern const char kBackGestureActivityTabProviderDescription[];

extern const char kBackGestureRefactorAndroidName[];
extern const char kBackGestureRefactorAndroidDescription[];

extern const char kBoardingPassDetectorName[];
extern const char kBoardingPassDetectorDescription[];

extern const char kCCTEmbedderSpecialBehaviorTriggerName[];
extern const char kCCTEmbedderSpecialBehaviorTriggerDescription[];

extern const char kCCTIncognitoAvailableToThirdPartyName[];
extern const char kCCTIncognitoAvailableToThirdPartyDescription[];

extern const char kCCTMinimizedName[];
extern const char kCCTMinimizedDescription[];

extern const char kCCTPageInsightsHubName[];
extern const char kCCTPageInsightsHubDescription[];

extern const char kCCTPageInsightsHubBetterScrollName[];
extern const char kCCTPageInsightsHubBetterScrollDescription[];

extern const char kCCTResizableForThirdPartiesName[];
extern const char kCCTResizableForThirdPartiesDescription[];
extern const char kCCTResizableSideSheetName[];
extern const char kCCTResizableSideSheetDescription[];
extern const char kCCTResizableSideSheetForThirdPartiesName[];
extern const char kCCTResizableSideSheetForThirdPartiesDescription[];

extern const char kAccountReauthenticationRecentTimeWindowName[];
extern const char kAccountReauthenticationRecentTimeWindowDescription[];
extern const char kAccountReauthenticationRecentTimeWindowDefault[];
extern const char kAccountReauthenticationRecentTimeWindow0mins[];
extern const char kAccountReauthenticationRecentTimeWindow1mins[];
extern const char kAccountReauthenticationRecentTimeWindow5mins[];

extern const char kChimeAlwaysShowNotificationDescription[];
extern const char kChimeAlwaysShowNotificationName[];

extern const char kChimeAndroidSdkDescription[];
extern const char kChimeAndroidSdkName[];

extern const char kTabStateFlatBufferName[];
extern const char kTabStateFlatBufferDescription[];

extern const char kContextMenuPopupForAllScreenSizesName[];
extern const char kContextMenuPopupForAllScreenSizesDescription[];

extern const char kContextualSearchSuppressShortViewName[];
extern const char kContextualSearchSuppressShortViewDescription[];

extern const char kConvertTrackpadEventsToMouseName[];
extern const char kConvertTrackpadEventsToMouseDescription[];

extern const char kDefaultViewportIsDeviceWidthName[];
extern const char kDefaultViewportIsDeviceWidthDescription[];

extern const char kDeferTabSwitcherLayoutCreationName[];
extern const char kDeferTabSwitcherLayoutCreationDescription[];

extern const char kDeprecatedExternalPickerFunctionName[];
extern const char kDeprecatedExternalPickerFunctionDescription[];

extern const char kDragDropIntoOmniboxName[];
extern const char kDragDropIntoOmniboxDescription[];

extern const char kDrawCutoutEdgeToEdgeName[];
extern const char kDrawCutoutEdgeToEdgeDescription[];

extern const char kDrawEdgeToEdgeName[];
extern const char kDrawEdgeToEdgeDescription[];

extern const char kDrawNativeEdgeToEdgeName[];
extern const char kDrawNativeEdgeToEdgeDescription[];

extern const char kDrawWebEdgeToEdgeName[];
extern const char kDrawWebEdgeToEdgeDescription[];

extern const char kDynamicTopChromeName[];
extern const char kDynamicTopChromeDescription[];

extern const char kEnableAndroidGamepadVibrationName[];
extern const char kEnableAndroidGamepadVibrationDescription[];

extern const char kEnableCommandLineOnNonRootedName[];
extern const char kEnableCommandLineOnNoRootedDescription[];

extern const char kEnableEncryptedAV1Name[];
extern const char kEnableEncryptedAV1Description[];

extern const char kEnablePixDetectionName[];
extern const char kEnablePixDetectionDescription[];

extern const char kExternalNavigationDebugLogsName[];
extern const char kExternalNavigationDebugLogsDescription[];

extern const char kFeatureNotificationGuideName[];
extern const char kFeatureNotificationGuideDescription[];

extern const char kFeatureNotificationGuideSkipCheckForLowEngagedUsersName[];
extern const char
    kFeatureNotificationGuideSkipCheckForLowEngagedUsersDescription[];

extern const char kFeedFollowUiUpdateName[];
extern const char kFeedFollowUiUpdateDescription[];

extern const char kFeedLoadingPlaceholderName[];
extern const char kFeedLoadingPlaceholderDescription[];

extern const char kFeedSignedOutViewDemotionName[];
extern const char kFeedSignedOutViewDemotionDescription[];

extern const char kFeedSportsCardName[];
extern const char kFeedSportsCardDescription[];

extern const char kFeedStampName[];
extern const char kFeedStampDescription[];

extern const char kFeedCloseRefreshName[];
extern const char kFeedCloseRefreshDescription[];

extern const char kFeedDynamicColorsName[];
extern const char kFeedDynamicColorsDescription[];

extern const char kFeedDiscoFeedEndpointName[];
extern const char kFeedDiscoFeedEndpointDescription[];

extern const char kFullscreenInsetsApiMigrationName[];
extern const char kFullscreenInsetsApiMigrationDescription[];

extern const char kRefreshFeedOnRestartName[];
extern const char kRefreshFeedOnRestartDescription[];

extern const char kGridTabSwitcherAndroidAnimationsName[];
extern const char kGridTabSwitcherAndroidAnimationsDescription[];

extern const char kHideTabOnTabSwitcherName[];
extern const char kHideTabOnTabSwitcherDescription[];

extern const char kInfoCardAcknowledgementTrackingName[];
extern const char kInfoCardAcknowledgementTrackingDescription[];

extern const char kInstantStartName[];
extern const char kInstantStartDescription[];

extern const char kInterestFeedV2Name[];
extern const char kInterestFeedV2Description[];

extern const char kInterestFeedV2HeartsName[];
extern const char kInterestFeedV2HeartsDescription[];

extern const char kMagicStackAndroidName[];
extern const char kMagicStackAndroidDescription[];

extern const char kMediaPickerAdoptionStudyName[];
extern const char kMediaPickerAdoptionStudyDescription[];

extern const char kMessagesForAndroidAdsBlockedName[];
extern const char kMessagesForAndroidAdsBlockedDescription[];

extern const char kMessagesForAndroidOfferNotificationName[];
extern const char kMessagesForAndroidOfferNotificationDescription[];

extern const char kMessagesForAndroidPermissionUpdateName[];
extern const char kMessagesForAndroidPermissionUpdateDescription[];

extern const char kMessagesForAndroidPopupBlockedName[];
extern const char kMessagesForAndroidPopupBlockedDescription[];

extern const char kMessagesForAndroidSaveCardName[];
extern const char kMessagesForAndroidSaveCardDescription[];

extern const char kMessagesForAndroidStackingAnimationName[];
extern const char kMessagesForAndroidStackingAnimationDescription[];

extern const char kMobilePWAInstallPromptMlName[];
extern const char kMobilePWAInstallPromptMlDescription[];

extern const char kMouseAndTrackpadDropdownMenuName[];
extern const char kMouseAndTrackpadDropdownMenuDescription[];

extern const char kNewTabSearchEngineUrlAndroidName[];
extern const char kNewTabSearchEngineUrlAndroidDescription[];

extern const char kNotificationPermissionRationaleName[];
extern const char kNotificationPermissionRationaleDescription[];

extern const char kNotificationPermissionRationaleBottomSheetName[];
extern const char kNotificationPermissionRationaleBottomSheetDescription[];

extern const char kOfflineAutoFetchName[];
extern const char kOfflineAutoFetchDescription[];

extern const char kOfflineContentOnNetErrorName[];
extern const char kOfflineContentOnNetErrorDescription[];

extern const char kOmahaMinSdkVersionAndroidName[];
extern const char kOmahaMinSdkVersionAndroidDescription[];
extern const char kOmahaMinSdkVersionAndroidMinSdk1Description[];
extern const char kOmahaMinSdkVersionAndroidMinSdk1000Description[];

extern const char kOpenDownloadDialog[];
extern const char kOpenDownloadDialogDescription[];

extern const char kPassExplicitSyncPassphraseToGmsCoreName[];
extern const char kPassExplicitSyncPassphraseToGmsCoreDescription[];

extern const char kPasswordGenerationBottomSheetName[];
extern const char kPasswordGenerationBottomSheetDescription[];

extern const char kPasswordSuggestionBottomSheetV2Name[];
extern const char kPasswordSuggestionBottomSheetV2Description[];

extern const char kQueryTilesName[];
extern const char kQueryTilesDescription[];
extern const char kQueryTilesNTPName[];
extern const char kQueryTilesNTPDescription[];
extern const char kQueryTilesOnStartName[];
extern const char kQueryTilesOnStartDescription[];
extern const char kQueryTilesSingleTierName[];
extern const char kQueryTilesSingleTierDescription[];
extern const char kQueryTilesEnableQueryEditingName[];
extern const char kQueryTilesEnableQueryEditingDescription[];
extern const char kQueryTilesEnableTrendingName[];
extern const char kQueryTilesEnableTrendingDescription[];
extern const char kQueryTilesDisableCountryOverrideName[];
extern const char kQueryTilesDisableCountryOverrideDescription[];
extern const char kQueryTilesCountryCode[];
extern const char kQueryTilesCountryCodeDescription[];
extern const char kQueryTilesCountryCodeUS[];
extern const char kQueryTilesCountryCodeIndia[];
extern const char kQueryTilesCountryCodeBrazil[];
extern const char kQueryTilesCountryCodeNigeria[];
extern const char kQueryTilesCountryCodeIndonesia[];
extern const char kQueryTilesInstantFetchName[];
extern const char kQueryTilesInstantFetchDescription[];
extern const char kQueryTilesRankTilesName[];
extern const char kQueryTilesRankTilesDescription[];
extern const char kQueryTilesSegmentationName[];
extern const char kQueryTilesSegmentationDescription[];
extern const char kQueryTilesSwapTrendingName[];
extern const char kQueryTilesSwapTrendingDescription[];

extern const char kReadAloudName[];
extern const char kReadAloudDescription[];
extern const char kReadAloudInCCTName[];
extern const char kReadAloudInCCTDescription[];

extern const char kReaderModeHeuristicsName[];
extern const char kReaderModeHeuristicsDescription[];
extern const char kReaderModeHeuristicsMarkup[];
extern const char kReaderModeHeuristicsAdaboost[];
extern const char kReaderModeHeuristicsAllArticles[];
extern const char kReaderModeHeuristicsAlwaysOff[];
extern const char kReaderModeHeuristicsAlwaysOn[];

extern const char kReaderModeInCCTName[];
extern const char kReaderModeInCCTDescription[];

extern const char kRecoverFromNeverSaveAndroidName[];
extern const char kRecoverFromNeverSaveAndroidDescription[];

extern const char kReengagementNotificationName[];
extern const char kReengagementNotificationDescription[];

extern const char kRelatedSearchesAllLanguageName[];
extern const char kRelatedSearchesAllLanguageDescription[];

extern const char kRemoveUPMUnenrollmentName[];
extern const char kRemoveUPMUnenrollmentDescription[];

extern const char kRequestDesktopSiteDefaultsName[];
extern const char kRequestDesktopSiteDefaultsDescription[];

extern const char kRequestDesktopSiteDefaultsDowngradeName[];
extern const char kRequestDesktopSiteDefaultsDowngradeDescription[];

extern const char kRequestDesktopSiteDefaultsLoggingName[];
extern const char kRequestDesktopSiteDefaultsLoggingDescription[];

extern const char kRequestDesktopSiteWindowSettingName[];
extern const char kRequestDesktopSiteWindowSettingDescription[];

extern const char kRequestDesktopSiteZoomName[];
extern const char kRequestDesktopSiteZoomDescription[];

extern const char kRevokeNotificationsPermissionIfDisabledOnAppLevelName[];
extern const char
    kRevokeNotificationsPermissionIfDisabledOnAppLevelDescription[];

extern const char kSafeBrowsingNewGmsApiForBrowseUrlDatabaseCheckName[];
extern const char kSafeBrowsingNewGmsApiForBrowseUrlDatabaseCheckDescription[];

extern const char kScreenshotsForAndroidV2Name[];
extern const char kScreenshotsForAndroidV2Description[];

extern const char kSecurePaymentConfirmationAndroidName[];
extern const char kSecurePaymentConfirmationAndroidDescription[];

extern const char kShowNtpAtStartupAndroidName[];
extern const char kShowNtpAtStartupAndroidDescription[];

extern const char kShowScrollableMVTOnNTPAndroidName[];
extern const char kShowScrollableMVTOnNTPAndroidDescription[];

extern const char kSendTabToSelfV2Name[];
extern const char kSendTabToSelfV2Description[];

extern const char kSetMarketUrlForTestingName[];
extern const char kSetMarketUrlForTestingDescription[];

extern const char kShareSheetMigrationAndroidName[];
extern const char kShareSheetMigrationAndroidDescription[];

extern const char kSiteIsolationForPasswordSitesName[];
extern const char kSiteIsolationForPasswordSitesDescription[];

extern const char kSmartZoomName[];
extern const char kSmartZoomDescription[];

extern const char kSmartSuggestionForLargeDownloadsName[];
extern const char kSmartSuggestionForLargeDownloadsDescription[];

extern const char kStartSurfaceAndroidName[];
extern const char kStartSurfaceAndroidDescription[];

extern const char kFeedPositionAndroidName[];
extern const char kFeedPositionAndroidDescription[];

extern const char kSearchResumptionModuleAndroidName[];
extern const char kSearchResumptionModuleAndroidDescription[];

extern const char kStartSurfaceOnTabletName[];
extern const char kStartSurfaceOnTabletDescription[];

extern const char kStartSurfaceRefactorName[];
extern const char kStartSurfaceRefactorDescription[];

extern const char kStartSurfaceWithAccessibilityName[];
extern const char kStartSurfaceWithAccessibilityDescription[];

extern const char kStrictSiteIsolationName[];
extern const char kStrictSiteIsolationDescription[];

extern const char kSurfaceControlMagnifierName[];
extern const char kSurfaceControlMagnifierDescription[];

extern const char kSurfacePolishName[];
extern const char kSurfacePolishDescription[];

extern const char kTabGroupParityAndroidName[];
extern const char kTabGroupParityAndroidDescription[];

extern const char kTabGroupsForTabletsName[];
extern const char kTabGroupsForTabletsDescription[];

extern const char kThumbnailPlaceholderName[];
extern const char kThumbnailPlaceholderDescription[];

extern const char kTouchDragAndContextMenuName[];
extern const char kTouchDragAndContextMenuDescription[];

extern const char kTranslateMessageUIName[];
extern const char kTranslateMessageUIDescription[];

extern const char kUnifiedPasswordManagerLocalPasswordsAndroidNoMigrationName[];
extern const char
    kUnifiedPasswordManagerLocalPasswordsAndroidNoMigrationDescription[];

extern const char kUpdateMenuBadgeName[];
extern const char kUpdateMenuBadgeDescription[];

extern const char kUpdateMenuItemCustomSummaryDescription[];
extern const char kUpdateMenuItemCustomSummaryName[];

extern const char kUpdateMenuTypeName[];
extern const char kUpdateMenuTypeDescription[];
extern const char kUpdateMenuTypeNone[];
extern const char kUpdateMenuTypeUpdateAvailable[];
extern const char kUpdateMenuTypeUnsupportedOSVersion[];

extern const char kUseGMSCoreForBrandingInfoName[];
extern const char kUseGMSCoreForBrandingInfoDescription[];

extern const char kVideoTutorialsName[];
extern const char kVideoTutorialsDescription[];

extern const char kWebAuthnAndroidCredManName[];
extern const char kWebAuthnAndroidCredManDescription[];

extern const char kWebFeedName[];
extern const char kWebFeedDescription[];

extern const char kWebFeedAwarenessName[];
extern const char kWebFeedAwarenessDescription[];

extern const char kWebFeedOnboardingName[];
extern const char kWebFeedOnboardingDescription[];

extern const char kWebFeedSortName[];
extern const char kWebFeedSortDescription[];

extern const char kWebXrSharedBuffersName[];
extern const char kWebXrSharedBuffersDescription[];

extern const char kXsurfaceMetricsReportingName[];
extern const char kXsurfaceMetricsReportingDescription[];

#if BUILDFLAG(ENABLE_VR) && BUILDFLAG(ENABLE_OPENXR)
extern const char kOpenXRExtendedFeaturesName[];
extern const char kOpenXRExtendedFeaturesDescription[];

extern const char kOpenXRName[];
extern const char kOpenXRDescription[];
#endif

// Non-Android ----------------------------------------------------------------

#else  // !BUILDFLAG(IS_ANDROID)

extern const char kAllowAllSitesToInitiateMirroringName[];
extern const char kAllowAllSitesToInitiateMirroringDescription[];

extern const char kButterOnDesktopFollowupName[];
extern const char kButterOnDesktopFollowupDescription[];

extern const char kDialMediaRouteProviderName[];
extern const char kDialMediaRouteProviderDescription[];

extern const char kCastMirroringTargetPlayoutDelayName[];
extern const char kCastMirroringTargetPlayoutDelayDescription[];
extern const char kCastMirroringTargetPlayoutDelayDefault[];
extern const char kCastMirroringTargetPlayoutDelay100ms[];
extern const char kCastMirroringTargetPlayoutDelay150ms[];
extern const char kCastMirroringTargetPlayoutDelay200ms[];
extern const char kCastMirroringTargetPlayoutDelay250ms[];
extern const char kCastMirroringTargetPlayoutDelay300ms[];
extern const char kCastMirroringTargetPlayoutDelay350ms[];

extern const char kEnablePolicyTestPageName[];
extern const char kEnablePolicyTestPageDescription[];

extern const char kEnableAccessibilityLiveCaptionName[];
extern const char kEnableAccessibilityLiveCaptionDescription[];

extern const char kEnableMantaServiceName[];
extern const char kEnableMantaServiceDescription[];

extern const char kReadAnythingName[];
extern const char kReadAnythingDescription[];

extern const char kReadAnythingWithScreen2xName[];
extern const char kReadAnythingWithScreen2xDescription[];

extern const char kReadAnythingWithAlgorithmName[];
extern const char kReadAnythingWithAlgorithmDescription[];

extern const char kReadAnythingWebUIToolbarName[];
extern const char kReadAnythingWebUIToolbarDescription[];

extern const char kReadAnythingReadAloudName[];
extern const char kReadAnythingReadAloudDescription[];

extern const char kReadAnythingOmniboxIconName[];
extern const char kReadAnythingOmniboxIconDescription[];

extern const char kReadAnythingLocalSidePanelName[];
extern const char kReadAnythingLocalSidePanelDescription[];

extern const char kGlobalMediaControlsCastStartStopName[];
extern const char kGlobalMediaControlsCastStartStopDescription[];

extern const char kHeuristicMemorySaverName[];
extern const char kHeuristicMemorySaverDescription[];

extern const char kLinkPreviewName[];
extern const char kLinkPreviewDescription[];

extern const char kMemorySaverMultistateModeAvailableName[];
extern const char kMemorySaverMultistateModeAvailableDescription[];

extern const char kMemorySaverDiscardedTabTreatmentName[];
extern const char kMemorySaverDiscardedTabTreatmentDescription[];

extern const char kMemorySaverMemoryUsageInHovercardsName[];
extern const char kMemorySaverMemoryUsageInHovercardsDescription[];

extern const char kMemorySaverDiscardExceptionsImprovementsName[];
extern const char kMemorySaverDiscardExceptionsImprovementsDescription[];

extern const char kMemorySaverSavingsReportingImprovementsName[];
extern const char kMemorySaverSavingsReportingImprovementsDescription[];

extern const char kModalMemorySaverName[];
extern const char kModalMemorySaverDescription[];

extern const char kMuteNotificationSnoozeActionName[];
extern const char kMuteNotificationSnoozeActionDescription[];

extern const char kNtpAlphaBackgroundCollectionsName[];
extern const char kNtpAlphaBackgroundCollectionsDescription[];

extern const char kNtpBackgroundImageErrorDetectionName[];
extern const char kNtpBackgroundImageErrorDetectionDescription[];

extern const char kNtpCacheOneGoogleBarName[];
extern const char kNtpCacheOneGoogleBarDescription[];

extern const char kNtpChromeCartModuleName[];
extern const char kNtpChromeCartModuleDescription[];

extern const char kNtpDriveModuleName[];
extern const char kNtpDriveModuleDescription[];

extern const char kNtpDriveModuleSegmentationName[];
extern const char kNtpDriveModuleSegmentationDescription[];

extern const char kNtpDriveModuleShowSixFilesName[];
extern const char kNtpDriveModuleShowSixFilesDescription[];

#if !defined(OFFICIAL_BUILD)
extern const char kNtpDummyModulesName[];
extern const char kNtpDummyModulesDescription[];
#endif

extern const char kNtpHistoryClustersModuleName[];
extern const char kNtpHistoryClustersModuleDescription[];

extern const char kNtpHistoryClustersModuleSuggestionChipHeaderName[];
extern const char kNtpHistoryClustersModuleSuggestionChipHeaderDescription[];

extern const char kNtpHistoryClustersModuleUseModelRankingName[];
extern const char kNtpHistoryClustersModuleUseModelRankingDescription[];

extern const char kNtpHistoryClustersModuleTextOnlyName[];
extern const char kNtpHistoryClustersModuleTextOnlyDescription[];

extern const char kNtpChromeCartInHistoryClustersModuleName[];
extern const char kNtpChromeCartInHistoryClustersModuleDescription[];

extern const char kNtpChromeCartHistoryClusterCoexistName[];
extern const char kNtpChromeCartHistoryClusterCoexistDescription[];

extern const char kNtpTabResumptionModuleName[];
extern const char kNtpTabResumptionModuleDescription[];

extern const char kNtpMiddleSlotPromoDismissalName[];
extern const char kNtpMiddleSlotPromoDismissalDescription[];

extern const char kNtpModulesDragAndDropName[];
extern const char kNtpModulesDragAndDropDescription[];

extern const char kNtpModulesFirstRunExperienceName[];
extern const char kNtpModulesFirstRunExperienceDescription[];

extern const char kNtpModulesHeaderIconName[];
extern const char kNtpModulesHeaderIconDescription[];

extern const char kNtpModulesRedesignedName[];
extern const char kNtpModulesRedesignedDescription[];

extern const char kNtpPhotosModuleOptInTitleName[];
extern const char kNtpPhotosModuleOptInTitleDescription[];

extern const char kNtpPhotosModuleOptInArtWorkName[];
extern const char kNtpPhotosModuleOptInArtWorkDescription[];

extern const char kNtpPhotosModuleName[];
extern const char kNtpPhotosModuleDescription[];

extern const char kNtpPhotosModuleSoftOptOutName[];
extern const char kNtpPhotosModuleSoftOptOutDescription[];

extern const char kNtpRealboxIsTallName[];
extern const char kNtpRealboxIsTallDescription[];

extern const char kNtpRealboxCr23AllName[];
extern const char kNtpRealboxCr23AllDescription[];

extern const char kNtpRealboxCr23ConsistentRowHeightName[];
extern const char kNtpRealboxCr23ConsistentRowHeightDescription[];

extern const char kNtpRealboxCr23ExpandedStateIconsName[];
extern const char kNtpRealboxCr23ExpandedStateIconsDescription[];

extern const char kNtpRealboxCr23ExpandedStateLayoutName[];
extern const char kNtpRealboxCr23ExpandedStateLayoutDescription[];

extern const char kNtpRealboxCr23HoverFillShapeName[];
extern const char kNtpRealboxCr23HoverFillShapeDescription[];

extern const char kNtpRealboxCr23ThemingName[];
extern const char kNtpRealboxCr23ThemingDescription[];

extern const char kNtpRealboxMatchSearchboxThemeName[];
extern const char kNtpRealboxMatchSearchboxThemeDescription[];

extern const char kNtpRealboxPedalsName[];
extern const char kNtpRealboxPedalsDescription[];

extern const char kNtpRealboxWidthBehaviorName[];
extern const char kNtpRealboxWidthBehaviorDescription[];

extern const char kNtpRealboxUseGoogleGIconName[];
extern const char kNtpRealboxUseGoogleGIconDescription[];

extern const char kNtpRecipeTasksModuleName[];
extern const char kNtpRecipeTasksModuleDescription[];

extern const char kNtpReducedLogoSpaceName[];
extern const char kNtpReducedLogoSpaceDescription[];

extern const char kNtpSafeBrowsingModuleName[];
extern const char kNtpSafeBrowsingModuleDescription[];

extern const char kNtpSingleRowShortcutsName[];
extern const char kNtpSingleRowShortcutsDescription[];

extern const char kNtpWideModulesName[];
extern const char kNtpWideModulesDescription[];

extern const char kEnableReaderModeName[];
extern const char kEnableReaderModeDescription[];

extern const char kHappinessTrackingSurveysForDesktopDemoName[];
extern const char kHappinessTrackingSurveysForDesktopDemoDescription[];

extern const char kHatsWebUIName[];
extern const char kHatsWebUIDescription[];

extern const char kLayoutExtractionName[];
extern const char kLayoutExtractionDescription[];

extern const char kOmniboxDriveSuggestionsName[];
extern const char kOmniboxDriveSuggestionsDescription[];

extern const char kOmniboxDriveSuggestionsNoSettingName[];
extern const char kOmniboxDriveSuggestionsNoSettingDescription[];

extern const char kOmniboxDriveSuggestionsNoSyncRequirementName[];
extern const char kOmniboxDriveSuggestionsNoSyncRequirementDescription[];

extern const char kProbabilisticMemorySaverName[];
extern const char kProbabilisticMemorySaverDescription[];

extern const char kSCTAuditingName[];
extern const char kSCTAuditingDescription[];

extern const char kSmartCardWebApiName[];
extern const char kSmartCardWebApiDescription[];

extern const char kSyncWebauthnCredentialsName[];
extern const char kSyncWebauthnCredentialsDescription[];

#if BUILDFLAG(GOOGLE_CHROME_BRANDING)
extern const char kSettingsEnableGetTheMostOutOfChromeName[];
extern const char kSettingsEnableGetTheMostOutOfChromeDescription[];
#endif

extern const char kWebAppDedupeInstallUrlsName[];
extern const char kWebAppDedupeInstallUrlsDescription[];

extern const char kWebAppManifestImmediateUpdatingName[];
extern const char kWebAppManifestImmediateUpdatingDescription[];

extern const char kWebAppSeparateUserDisplayModeForCrosName[];
extern const char kWebAppSeparateUserDisplayModeForCrosDescription[];

extern const char kWebAppSyncOnlySeparateUserDisplayModeForCrosName[];
extern const char kWebAppSyncOnlySeparateUserDisplayModeForCrosDescription[];

extern const char kWebAppSyncGeneratedIconBackgroundFixName[];
extern const char kWebAppSyncGeneratedIconBackgroundFixDescription[];

extern const char kWebAppSyncGeneratedIconRetroactiveFixName[];
extern const char kWebAppSyncGeneratedIconRetroactiveFixDescription[];

extern const char kWebAppSyncGeneratedIconUpdateFixName[];
extern const char kWebAppSyncGeneratedIconUpdateFixDescription[];

extern const char kUserDisplayModeSyncBrowserMitigationName[];
extern const char kUserDisplayModeSyncBrowserMitigationDescription[];

extern const char kUserDisplayModeSyncStandaloneMitigationName[];
extern const char kUserDisplayModeSyncStandaloneMitigationDescription[];

extern const char kWebAppSystemMediaControlsWinName[];
extern const char kWebAppSystemMediaControlsWinDescription[];

extern const char kWebAuthenticationNewPasskeyUIName[];
extern const char kWebAuthenticationNewPasskeyUIDescription[];

extern const char kWebAuthenticationPermitEnterpriseAttestationName[];
extern const char kWebAuthenticationPermitEnterpriseAttestationDescription[];

extern const char kDevToolsTabTargetLiteralName[];
extern const char kDevToolsTabTargetLiteralDescription[];

extern const char kNewConfirmationBubbleForGeneratedPasswordsName[];
extern const char kNewConfirmationBubbleForGeneratedPasswordsDescription[];

#endif  // BUILDFLAG(IS_ANDROID)

// Windows --------------------------------------------------------------------

#if BUILDFLAG(IS_WIN)

extern const char kCalculateNativeWinOcclusionName[];
extern const char kCalculateNativeWinOcclusionDescription[];

extern const char kEnableMediaFoundationVideoCaptureName[];
extern const char kEnableMediaFoundationVideoCaptureDescription[];

extern const char kHardwareSecureDecryptionName[];
extern const char kHardwareSecureDecryptionDescription[];

extern const char kHardwareSecureDecryptionExperimentName[];
extern const char kHardwareSecureDecryptionExperimentDescription[];

extern const char kHardwareSecureDecryptionFallbackName[];
extern const char kHardwareSecureDecryptionFallbackDescription[];

extern const char kMediaFoundationClearName[];
extern const char kMediaFoundationClearDescription[];

extern const char kMediaFoundationClearStrategyName[];
extern const char kMediaFoundationClearStrategyDescription[];

extern const char kRawAudioCaptureName[];
extern const char kRawAudioCaptureDescription[];

extern const char kRunVideoCaptureServiceInBrowserProcessName[];
extern const char kRunVideoCaptureServiceInBrowserProcessDescription[];

extern const char kUseAngleDescriptionWindows[];

extern const char kUseAngleD3D11[];
extern const char kUseAngleD3D9[];
extern const char kUseAngleD3D11on12[];

extern const char kUseWaitableSwapChainName[];
extern const char kUseWaitableSwapChainDescription[];

extern const char kUseWinrtMidiApiName[];
extern const char kUseWinrtMidiApiDescription[];

extern const char kWebRtcAllowWgcScreenCapturerName[];
extern const char kWebRtcAllowWgcScreenCapturerDescription[];

extern const char kWebRtcAllowWgcWindowCapturerName[];
extern const char kWebRtcAllowWgcWindowCapturerDescription[];

extern const char kWindows11MicaTitlebarName[];
extern const char kWindows11MicaTitlebarDescription[];

#if BUILDFLAG(ENABLE_EXTENSIONS)
extern const char kLaunchWindowsNativeHostsDirectlyName[];
extern const char kLaunchWindowsNativeHostsDirectlyDescription[];
#endif  // ENABLE_EXTENSIONS

#if BUILDFLAG(ENABLE_PRINTING)
extern const char kPrintWithPostScriptType42FontsName[];
extern const char kPrintWithPostScriptType42FontsDescription[];

extern const char kPrintWithReducedRasterizationName[];
extern const char kPrintWithReducedRasterizationDescription[];

extern const char kReadPrinterCapabilitiesWithXpsName[];
extern const char kReadPrinterCapabilitiesWithXpsDescription[];

extern const char kUseXpsForPrintingName[];
extern const char kUseXpsForPrintingDescription[];

extern const char kUseXpsForPrintingFromPdfName[];
extern const char kUseXpsForPrintingFromPdfDescription[];
#endif  // BUILDFLAG(ENABLE_PRINTING)

#endif  // BUILDFLAG(IS_WIN)

// Mac ------------------------------------------------------------------------

#if BUILDFLAG(IS_MAC)

#if BUILDFLAG(ENABLE_PRINTING)
extern const char kCupsIppPrintingBackendName[];
extern const char kCupsIppPrintingBackendDescription[];
#endif  // BUILDFLAG(ENABLE_PRINTING)

extern const char kImmersiveFullscreenName[];
extern const char kImmersiveFullscreenDescription[];

extern const char kMacImeLiveConversionFixName[];
extern const char kMacImeLiveConversionFixDescription[];

extern const char kMacLoopbackAudioForCastName[];
extern const char kMacLoopbackAudioForCastDescription[];

extern const char kMacLoopbackAudioForScreenShareName[];
extern const char kMacLoopbackAudioForScreenShareDescription[];

extern const char kMacPWAsNotificationAttributionName[];
extern const char kMacPWAsNotificationAttributionDescription[];

extern const char kMacSyscallSandboxName[];
extern const char kMacSyscallSandboxDescription[];

extern const char kRetryGetVideoCaptureDeviceInfosName[];
extern const char kRetryGetVideoCaptureDeviceInfosDescription[];

extern const char kScreenTimeName[];
extern const char kScreenTimeDescription[];

extern const char kUseAngleDescriptionMac[];
extern const char kUseAngleMetal[];

extern const char kSwapBackquoteKeysInISOKeyboardName[];
extern const char kSwapBackquoteKeysInISOKeyboardDescription[];

extern const char kSystemColorChooserName[];
extern const char kSystemColorChooserDescription[];

extern const char kVideoToolboxAv1DecodingName[];
extern const char kVideoToolboxAv1DecodingDescription[];

#endif  // BUILDFLAG(IS_MAC)

// Windows and Mac ------------------------------------------------------------

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC)

extern const char kUseAngleName[];

extern const char kUseAngleDefault[];
extern const char kUseAngleGL[];

#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC)

// ChromeOS -------------------------------------------------------------------

#if BUILDFLAG(IS_CHROMEOS_ASH)

extern const char kAcceleratedMjpegDecodeName[];
extern const char kAcceleratedMjpegDecodeDescription[];

extern const char kAdaptiveChargingName[];
extern const char kAdaptiveChargingDescription[];

extern const char kAdaptiveChargingForTestingName[];
extern const char kAdaptiveChargingForTestingDescription[];

extern const char kAdvancedDocumentScanApiName[];
extern const char kAdvancedDocumentScanApiDescription[];

extern const char kAllowCrossDeviceFeatureSuiteName[];
extern const char kAllowCrossDeviceFeatureSuiteDescription[];

extern const char kLinkCrossDeviceInternalsName[];
extern const char kLinkCrossDeviceInternalsDescription[];

extern const char kAllowDevtoolsInSystemUIName[];
extern const char kAllowDevtoolsInSystemUIDescription[];

extern const char kAllowEapDefaultCasWithoutSubjectVerificationName[];
extern const char kAllowEapDefaultCasWithoutSubjectVerificationDescription[];

extern const char kAllowScrollSettingsName[];
extern const char kAllowScrollSettingsDescription[];

extern const char kAlmanacGameMigrationName[];
extern const char kAlmanacGameMigrationDescription[];

extern const char kAltClickAndSixPackCustomizationName[];
extern const char kAltClickAndSixPackCustomizationDescription[];
extern const char kAlwaysEnableHdcpName[];
extern const char kAlwaysEnableHdcpDescription[];
extern const char kAlwaysEnableHdcpDefault[];
extern const char kAlwaysEnableHdcpType0[];
extern const char kAlwaysEnableHdcpType1[];

extern const char kAmbientModeThrottleAnimationName[];
extern const char kAmbientModeThrottleAnimationDescription[];

extern const char kApnRevampName[];
extern const char kApnRevampDescription[];

extern const char kAppInstallServiceUriName[];
extern const char kAppInstallServiceUriDescription[];

extern const char kAppLaunchAutomationName[];
extern const char kAppLaunchAutomationDescription[];

extern const char kArcArcOnDemandExperimentName[];
extern const char kArcArcOnDemandExperimentDescription[];

extern const char kArcCustomTabsExperimentName[];
extern const char kArcCustomTabsExperimentDescription[];

extern const char kArcDocumentsProviderUnknownSizeName[];
extern const char kArcDocumentsProviderUnknownSizeDescription[];

extern const char kArcFilePickerExperimentName[];
extern const char kArcFilePickerExperimentDescription[];

extern const char kArcIdleManagerName[];
extern const char kArcIdleManagerDescription[];

extern const char kArcInstantResponseWindowOpenName[];
extern const char kArcInstantResponseWindowOpenDescription[];

extern const char kArcKeyboardShortcutHelperIntegrationName[];
extern const char kArcKeyboardShortcutHelperIntegrationDescription[];

extern const char kArcNativeBridgeToggleName[];
extern const char kArcNativeBridgeToggleDescription[];

extern const char kArcNearbyShareFuseBoxName[];
extern const char kArcNearbyShareFuseBoxDescription[];

extern const char kArcPerAppLanguageName[];
extern const char kArcPerAppLanguageDescription[];

extern const char kArcResizeCompatName[];
extern const char kArcResizeCompatDescription[];

extern const char kArcRoundedWindowCompatName[];
extern const char kArcRoundedWindowCompatDescription[];

extern const char kArcRtVcpuDualCoreName[];
extern const char kArcRtVcpuDualCoreDesc[];

extern const char kArcRtVcpuQuadCoreName[];
extern const char kArcRtVcpuQuadCoreDesc[];

extern const char kArcSwitchToKeyMintDaemonName[];
extern const char kArcSwitchToKeyMintDaemonDesc[];

extern const char kArcSwitchToKeyMintOnTName[];
extern const char kArcSwitchToKeyMintOnTDesc[];

extern const char kArcSwitchToKeyMintOnTOverrideName[];
extern const char kArcSwitchToKeyMintOnTOverrideDesc[];

extern const char kArcSyncInstallPriorityName[];
extern const char kArcSyncInstallPriorityDescription[];

extern const char kArcTouchscreenEmulationName[];
extern const char kArcTouchscreenEmulationDesc[];

extern const char kArcVmmSwapKBShortcutName[];
extern const char kArcVmmSwapKBShortcutDesc[];

extern const char kArcVmMemorySizeName[];
extern const char kArcVmMemorySizeDesc[];

extern const char kArcXdgModeName[];
extern const char kArcXdgModeDesc[];

extern const char kArcEnableAAudioMMAPName[];
extern const char kArcEnableAAudioMMAPDescription[];

extern const char kArcAAudioMMAPLowLatencyName[];
extern const char kArcAAudioMMAPLowLatencyDescription[];

extern const char kArcEnableVirtioBlkForDataName[];
extern const char kArcEnableVirtioBlkForDataDesc[];

extern const char kArcExternalStorageAccessName[];
extern const char kArcExternalStorageAccessDescription[];

extern const char kAshEnableUnifiedDesktopName[];
extern const char kAshEnableUnifiedDesktopDescription[];

extern const char kAshPickerName[];
extern const char kAshPickerDescription[];

extern const char kAshPickerFeatureKeyName[];
extern const char kAshPickerFeatureKeyDescription[];

extern const char kAsynchronousScannerDiscoveryName[];
extern const char kAsynchronousScannerDiscoveryDescription[];

extern const char kAssistantIphName[];
extern const char kAssistantIphDescription[];

extern const char kAudioA2DPAdvancedCodecsName[];
extern const char kAudioA2DPAdvancedCodecsDescription[];

extern const char kAudioAPNoiseCancellationName[];
extern const char kAudioAPNoiseCancellationDescription[];

extern const char kAudioFlexibleLoopbackName[];
extern const char kAudioFlexibleLoopbackDescription[];

extern const char kAudioHFPMicSRName[];
extern const char kAudioHFPMicSRDescription[];

extern const char kAudioHFPMicSRToggleName[];
extern const char kAudioHFPMicSRToggleDescription[];

extern const char kAudioHFPSwbName[];
extern const char kAudioHFPSwbDescription[];

extern const char kAudioHFPOffloadName[];
extern const char kAudioHFPOffloadDescription[];

extern const char kAudioOffloadCrasDSPToSOFName[];
extern const char kAudioOffloadCrasDSPToSOFDescription[];

extern const char kAudioSuppressSetRTCAudioActiveName[];
extern const char kAudioSuppressSetRTCAudioActiveDescription[];

extern const char kAutoFramingOverrideName[];
extern const char kAutoFramingOverrideDescription[];

extern const char kAutocorrectByDefaultName[];
extern const char kAutocorrectByDefaultDescription[];

extern const char kAutocorrectParamsTuningName[];
extern const char kAutocorrectParamsTuningDescription[];

extern const char kAutocorrectToggleName[];
extern const char kAutocorrectToggleDescription[];

extern const char kAutocorrectUseReplaceSurroundingTextName[];
extern const char kAutocorrectUseReplaceSurroundingTextDescription[];

extern const char kAvatarsCloudMigrationName[];
extern const char kAvatarsCloudMigrationDescription[];

extern const char kBluetoothFixA2dpPacketSizeName[];
extern const char kBluetoothFixA2dpPacketSizeDescription[];

extern const char kBluetoothFlossTelephonyName[];
extern const char kBluetoothFlossTelephonyDescription[];

extern const char kBluetoothDisconnectWarningName[];
extern const char kBluetoothDisconnectWarningDescription[];

extern const char kBluetoothQualityReportName[];
extern const char kBluetoothQualityReportDescription[];

extern const char kBluetoothCoredumpName[];
extern const char kBluetoothCoredumpDescription[];

extern const char kBluetoothFlossCoredumpName[];
extern const char kBluetoothFlossCoredumpDescription[];

extern const char kBluetoothUseFlossName[];
extern const char kBluetoothUseFlossDescription[];

extern const char kBluetoothFlossIsAvailabilityCheckNeededName[];
extern const char kBluetoothFlossIsAvailabilityCheckNeededDescription[];

extern const char kBluetoothUseLLPrivacyName[];
extern const char kBluetoothUseLLPrivacyDescription[];

extern const char kCaptureModeAudioMixingName[];
extern const char kCaptureModeAudioMixingDescription[];

extern const char kCaptureModeEducationName[];
extern const char kCaptureModeEducationDescription[];

extern const char kCaptureModeEducationBypassLimitsName[];
extern const char kCaptureModeEducationBypassLimitsDescription[];

extern const char kCaptureModeGifRecordingName[];
extern const char kCaptureModeGifRecordingDescription[];

extern const char kCrosBatterySaverAlwaysOnName[];
extern const char kCrosBatterySaverAlwaysOnDescription[];

extern const char kCrosBatterySaverName[];
extern const char kCrosBatterySaverDescription[];

extern const char kCrosShortstandName[];
extern const char kCrosShortstandDescription[];

extern const char kCrosSoulName[];
extern const char kCrosSoulDescription[];

extern const char kCrosWebAppShortcutUiUpdateName[];
extern const char kCrosWebAppShortcutUiUpdateDescription[];

extern const char kDeskButtonName[];
extern const char kDeskButtonDescription[];

extern const char kDesksTemplatesName[];
extern const char kDesksTemplatesDescription[];

extern const char kDnsOverHttpsWithIdentifiersReuseOldPolicyName[];
extern const char kDnsOverHttpsWithIdentifiersReuseOldPolicyDescription[];

extern const char kPreferConstantFrameRateName[];
extern const char kPreferConstantFrameRateDescription[];

extern const char kForceControlFaceAeName[];
extern const char kForceControlFaceAeDescription[];

extern const char kCellularBypassESimInstallationConnectivityCheckName[];
extern const char kCellularBypassESimInstallationConnectivityCheckDescription[];

extern const char kCellularCarrierLockName[];
extern const char kCellularCarrierLockDescription[];

extern const char kCellularUseSecondEuiccName[];
extern const char kCellularUseSecondEuiccDescription[];

extern const char kClipboardHistoryLongpressName[];
extern const char kClipboardHistoryLongpressDescription[];

extern const char kClipboardHistoryRefreshName[];
extern const char kClipboardHistoryRefreshDescription[];

extern const char kClipboardHistoryUrlTitlesName[];
extern const char kClipboardHistoryUrlTitlesDescription[];

extern const char kClipboardHistoryWebContentsPasteName[];
extern const char kClipboardHistoryWebContentsPasteDescription[];

extern const char kCloudGamingDeviceName[];
extern const char kCloudGamingDeviceDescription[];

extern const char kComponentUpdaterTestRequestName[];
extern const char kComponentUpdaterTestRequestDescription[];

extern const char kContextualNudgesName[];
extern const char kContextualNudgesDescription[];

extern const char kCroshSWAName[];
extern const char kCroshSWADescription[];

extern const char kCrosOnDeviceGrammarCheckName[];
extern const char kCrosOnDeviceGrammarCheckDescription[];

extern const char kEnableServiceWorkersForChromeUntrustedName[];
extern const char kEnableServiceWorkersForChromeUntrustedDescription[];

extern const char kEnterpriseReportingUIName[];
extern const char kEnterpriseReportingUIDescription[];

extern const char kPermissiveUsbPassthroughName[];
extern const char kPermissiveUsbPassthroughDescription[];

extern const char kCrostiniContainerInstallName[];
extern const char kCrostiniContainerInstallDescription[];

extern const char kCrostiniGpuSupportName[];
extern const char kCrostiniGpuSupportDescription[];

extern const char kCrostiniResetLxdDbName[];
extern const char kCrostiniResetLxdDbDescription[];

extern const char kCrostiniMultiContainerName[];
extern const char kCrostiniMultiContainerDescription[];

extern const char kCrostiniQtImeSupportName[];
extern const char kCrostiniQtImeSupportDescription[];

extern const char kCrostiniVirtualKeyboardSupportName[];
extern const char kCrostiniVirtualKeyboardSupportDescription[];

extern const char kCameraAppAutoQRDetectionName[];
extern const char kCameraAppAutoQRDetectionDescription[];

extern const char kCameraAppDigitalZoomName[];
extern const char kCameraAppDigitalZoomDescription[];

extern const char kDisableCameraFrameRotationAtSourceName[];
extern const char kDisableCameraFrameRotationAtSourceDescription[];

extern const char kForceSpectreVariant2MitigationName[];
extern const char kForceSpectreVariant2MitigationDescription[];

extern const char kDisableCancelAllTouchesName[];
extern const char kDisableCancelAllTouchesDescription[];

extern const char kDisableExplicitDmaFencesName[];
extern const char kDisableExplicitDmaFencesDescription[];

extern const char kDisplayAlignmentAssistanceName[];
extern const char kDisplayAlignmentAssistanceDescription[];

extern const char kDropdownPanel[];
extern const char kDropdownPanelDescription[];

extern const char kFasterSplitScreenSetupName[];
extern const char kFasterSplitScreenSetupDescription[];

extern const char kFastPairName[];
extern const char kFastPairDescription[];

extern const char kFastPairDebugMetadataName[];
extern const char kFastPairDebugMetadataDescription[];

extern const char kFastPairHandshakeLongTermRefactorName[];
extern const char kFastPairHandshakeLongTermRefactorDescription[];

extern const char kFastPairHIDName[];
extern const char kFastPairHIDDescription[];

extern const char kFastPairPwaCompanionName[];
extern const char kFastPairPwaCompanionDescription[];

extern const char kFastPairSoftwareScanningName[];
extern const char kFastPairSoftwareScanningDescription[];

extern const char kFastPairSoftwareScanningSupportName[];
extern const char kFastPairSoftwareScanningSupportDescription[];

extern const char kFastPairSavedDevicesName[];
extern const char kFastPairSavedDevicesDescription[];

extern const char kFastPairDevicesBluetoothSettingsName[];
extern const char kFastPairDevicesBluetoothSettingsDescription[];

extern const char kFrameSinkDesktopCapturerInCrdName[];
extern const char kFrameSinkDesktopCapturerInCrdDescription[];

extern const char kUseHDRTransferFunctionName[];
extern const char kUseHDRTransferFunctionDescription[];

extern const char kEnableExternalDisplayHdr10Name[];
extern const char kEnableExternalDisplayHdr10Description[];

extern const char kDoubleTapToZoomInTabletModeName[];
extern const char kDoubleTapToZoomInTabletModeDescription[];

extern const char kQuickSettingsPWANotificationsName[];
extern const char kQuickSettingsPWANotificationsDescription[];

extern const char kDriveFsShowCSEFilesName[];
extern const char kDriveFsShowCSEFilesDescription[];

extern const char kEnableAssistantRoutinesName[];
extern const char kEnableAssistantRoutinesDescription[];

extern const char kEnableBackgroundBlurName[];
extern const char kEnableBackgroundBlurDescription[];

extern const char kDisableDnsProxyName[];
extern const char kDisableDnsProxyDescription[];

extern const char kEnableRFC8925Name[];
extern const char kEnableRFC8925Description[];

extern const char kEnableEdidBasedDisplayIdsName[];
extern const char kEnableEdidBasedDisplayIdsDescription[];

extern const char kTiledDisplaySupportName[];
extern const char kTiledDisplaySupportDescription[];

extern const char kEnableExternalKeyboardsInDiagnosticsAppName[];
extern const char kEnableExternalKeyboardsInDiagnosticsAppDescription[];

extern const char kEnableFastInkForSoftwareCursorName[];
extern const char kEnableFastInkForSoftwareCursorDescription[];

extern const char kEnableGetDebugdLogsInParallelName[];
extern const char kEnableGetDebugdLogsInParallelDescription[];

extern const char kEnableHostnameSettingName[];
extern const char kEnableHostnameSettingDescription[];

extern const char kEnableGesturePropertiesDBusServiceName[];
extern const char kEnableGesturePropertiesDBusServiceDescription[];

extern const char kEnableGoogleAssistantDspName[];
extern const char kEnableGoogleAssistantDspDescription[];

extern const char kEnableGoogleAssistantStereoInputName[];
extern const char kEnableGoogleAssistantStereoInputDescription[];

extern const char kEnableGoogleAssistantAecName[];
extern const char kEnableGoogleAssistantAecDescription[];

extern const char kEnableInputEventLoggingName[];
extern const char kEnableInputEventLoggingDescription[];

extern const char kDiagnosticsAppJellyName[];
extern const char kDiagnosticsAppJellyDescription[];

extern const char kEnableKeyboardBacklightToggleName[];
extern const char kEnableKeyboardBacklightToggleDescription[];

extern const char kEnableLibinputToHandleTouchpadName[];
extern const char kEnableLibinputToHandleTouchpadDescription[];

extern const char kEnableNeuralPalmAdaptiveHoldName[];
extern const char kEnableNeuralPalmAdaptiveHoldDescription[];

extern const char kEnableFakeKeyboardHeuristicName[];
extern const char kEnableFakeKeyboardHeuristicDescription[];

extern const char kEnableFakeMouseHeuristicName[];
extern const char kEnableFakeMouseHeuristicDescription[];

extern const char kEnableNeuralStylusPalmRejectionName[];
extern const char kEnableNeuralStylusPalmRejectionDescription[];

extern const char kEnableEdgeDetectionName[];
extern const char kEnableEdgeDetectionDescription[];

extern const char kEnablePalmSuppressionName[];
extern const char kEnablePalmSuppressionDescription[];

extern const char kEnablePerDeskZOrderName[];
extern const char kEnablePerDeskZOrderDescription[];

extern const char kEnableRemoveStalePolicyPinnedAppsFromShelfName[];
extern const char kEnableRemoveStalePolicyPinnedAppsFromShelfDescription[];

extern const char kEnableRuntimeCountersTelemetryName[];
extern const char kEnableRuntimeCountersTelemetryDescription[];

extern const char kEnableSeamlessRefreshRateSwitchingName[];
extern const char kEnableSeamlessRefreshRateSwitchingDescription[];

extern const char kEnableTouchpadsInDiagnosticsAppName[];
extern const char kEnableTouchpadsInDiagnosticsAppDescription[];

extern const char kEnableTouchscreensInDiagnosticsAppName[];
extern const char kEnableTouchscreensInDiagnosticsAppDescription[];

extern const char kEnableWifiQosName[];
extern const char kEnableWifiQosDescription[];

extern const char kEnableSuspendToDisk[];
extern const char kEnableSuspendToDiskDescription[];

extern const char kEapGtcWifiAuthenticationName[];
extern const char kEapGtcWifiAuthenticationDescription[];

extern const char kEcheSWAName[];
extern const char kEcheSWADescription[];

extern const char kEcheLauncherName[];
extern const char kEcheLauncherDescription[];

extern const char kEcheLauncherListViewName[];
extern const char kEcheLauncherListViewDescription[];

extern const char kEcheLauncherIconsInMoreAppsButtonName[];
extern const char kEcheLauncherIconsInMoreAppsButtonDescription[];

extern const char kEcheNetworkConnectionStateName[];
extern const char kEcheNetworkConnectionStateDescription[];

extern const char kEcheSWADebugModeName[];
extern const char kEcheSWADebugModeDescription[];

extern const char kEcheSWAMeasureLatencyName[];
extern const char kEcheSWAMeasureLatencyDescription[];

extern const char kEcheSWASendStartSignalingName[];
extern const char kEcheSWASendStartSignalingDescription[];

extern const char kEcheSWADisableStunServerName[];
extern const char kEcheSWADisableStunServerDescription[];

extern const char kEcheSWACheckAndroidNetworkInfoName[];
extern const char kEcheSWACheckAndroidNetworkInfoDescription[];

extern const char kEcheSWAProcessAndroidAccessibilityTreeName[];
extern const char kEcheSWAProcessAndroidAccessibilityTreeDescription[];

extern const char kEnableNotificationImageDragName[];
extern const char kEnableNotificationImageDragDescription[];

extern const char kEnableNotifierCollisionName[];
extern const char kEnableNotifierCollisionDescription[];

extern const char kEnableSystemNudgeMigrationName[];
extern const char kEnableSystemNudgeMigrationDescription[];

extern const char kEnableOAuthIppName[];
extern const char kEnableOAuthIppDescription[];

extern const char kPanelSelfRefresh2Name[];
extern const char kPanelSelfRefresh2Description[];

extern const char kEnableVariableRefreshRateName[];
extern const char kEnableVariableRefreshRateDescription[];

extern const char kEnableVariableRefreshRateAlwaysOnName[];
extern const char kEnableVariableRefreshRateAlwaysOnDescription[];

extern const char kEnforceAshExtensionKeeplistName[];
extern const char kEnforceAshExtensionKeeplistDescription[];

extern const char kEolIncentiveName[];
extern const char kEolIncentiveDescription[];

extern const char kEolResetDismissedPrefsName[];
extern const char kEolResetDismissedPrefsDescription[];

extern const char kExoGamepadVibrationName[];
extern const char kExoGamepadVibrationDescription[];

extern const char kExoOrdinalMotionName[];
extern const char kExoOrdinalMotionDescription[];

extern const char kExoSurroundingTextOffsetName[];
extern const char kExoSurroundingTextOffsetDescription[];

extern const char kExperimentalAccessibilityDictationContextCheckingName[];
extern const char
    kExperimentalAccessibilityDictationContextCheckingDescription[];

extern const char kExperimentalAccessibilityGoogleTtsLanguagePacksName[];
extern const char kExperimentalAccessibilityGoogleTtsLanguagePacksDescription[];

extern const char kExperimentalAccessibilityGoogleTtsHighQualityVoicesName[];
extern const char
    kExperimentalAccessibilityGoogleTtsHighQualityVoicesDescription[];

extern const char kExperimentalAccessibilityManifestV3Name[];
extern const char kExperimentalAccessibilityManifestV3Description[];

extern const char kExperimentalAccessibilitySwitchAccessTextName[];
extern const char kExperimentalAccessibilitySwitchAccessTextDescription[];

extern const char kExposeOutOfProcessVideoDecodingToLacrosName[];
extern const char kExposeOutOfProcessVideoDecodingToLacrosDescription[];

extern const char kFederatedServiceName[];
extern const char kFederatedServiceDescription[];

extern const char kFileTransferEnterpriseConnectorName[];
extern const char kFileTransferEnterpriseConnectorDescription[];

extern const char kFileTransferEnterpriseConnectorUIName[];
extern const char kFileTransferEnterpriseConnectorUIDescription[];

extern const char kFilesAppExperimentalName[];
extern const char kFilesAppExperimentalDescription[];

extern const char kFilesConflictDialogName[];
extern const char kFilesConflictDialogDescription[];

extern const char kFilesExtractArchiveName[];
extern const char kFilesExtractArchiveDescription[];

extern const char kFilesNewDirectoryTreeName[];
extern const char kFilesNewDirectoryTreeDescription[];

extern const char kFilesLocalImageSearchName[];
extern const char kFilesLocalImageSearchDescription[];

extern const char kFilesSinglePartitionFormatName[];
extern const char kFilesSinglePartitionFormatDescription[];

extern const char kFilesTrashDriveName[];
extern const char kFilesTrashDriveDescription[];

extern const char kFileSystemProviderContentCacheName[];
extern const char kFileSystemProviderContentCacheDescription[];

extern const char kFirmwareUpdateJellyName[];
extern const char kFirmwareUpdateJellyDescription[];

extern const char kFirmwareUpdateUIV2Name[];
extern const char kFirmwareUpdateUIV2Description[];

extern const char kFirstPartyVietnameseInputName[];
extern const char kFirstPartyVietnameseInputDescription[];

extern const char kFocusFollowsCursorName[];
extern const char kFocusFollowsCursorDescription[];

extern const char kFocusModeName[];
extern const char kFocusModeDescription[];

extern const char kForceReSyncDriveName[];
extern const char kForceReSyncDriveDescription[];

extern const char kFrameThrottleFpsName[];
extern const char kFrameThrottleFpsDescription[];
extern const char kFrameThrottleFpsDefault[];
extern const char kFrameThrottleFps5[];
extern const char kFrameThrottleFps10[];
extern const char kFrameThrottleFps15[];
extern const char kFrameThrottleFps20[];
extern const char kFrameThrottleFps25[];
extern const char kFrameThrottleFps30[];

extern const char kFSPsInRecentsName[];
extern const char kFSPsInRecentsDescription[];

extern const char kFuseBoxDebugName[];
extern const char kFuseBoxDebugDescription[];

extern const char kGlanceablesV2Name[];
extern const char kGlanceablesV2Description[];

extern const char kGlanceablesV2CalendarViewName[];
extern const char kGlanceablesV2CalendarViewDescription[];

extern const char kHelpAppAppDetailPageName[];
extern const char kHelpAppAppDetailPageDescription[];

extern const char kHelpAppAppsListName[];
extern const char kHelpAppAppsListDescription[];

extern const char kHelpAppAutoTriggerInstallDialogName[];
extern const char kHelpAppAutoTriggerInstallDialogDescription[];

extern const char kHelpAppCrosComponentsName[];
extern const char kHelpAppCrosComponentsDescription[];

extern const char kHelpAppHomePageAppArticlesName[];
extern const char kHelpAppHomePageAppArticlesDescription[];

extern const char kHelpAppLauncherSearchName[];
extern const char kHelpAppLauncherSearchDescription[];

extern const char kDiacriticsOnPhysicalKeyboardLongpressName[];
extern const char kDiacriticsOnPhysicalKeyboardLongpressDescription[];

extern const char kDiacriticsUseReplaceSurroundingTextName[];
extern const char kDiacriticsUseReplaceSurroundingTextDescription[];

extern const char kHoldingSpacePredictabilityName[];
extern const char kHoldingSpacePredictabilityDescription[];

extern const char kHoldingSpaceRefreshName[];
extern const char kHoldingSpaceRefreshDescription[];

extern const char kHoldingSpaceSuggestionsName[];
extern const char kHoldingSpaceSuggestionsDescription[];

extern const char kHoldingSpaceWallpaperNudgeName[];
extern const char kHoldingSpaceWallpaperNudgeDescription[];

extern const char kHoldingSpaceWallpaperNudgeForceEligibilityName[];
extern const char kHoldingSpaceWallpaperNudgeForceEligibilityDescription[];

extern const char kHotspotName[];
extern const char kHotspotDescription[];

extern const char kImeAssistEmojiEnhancedName[];
extern const char kImeAssistEmojiEnhancedDescription[];

extern const char kImeAssistMultiWordName[];
extern const char kImeAssistMultiWordDescription[];

extern const char kImeAssistMultiWordExpandedName[];
extern const char kImeAssistMultiWordExpandedDescription[];

extern const char kImeFstDecoderParamsUpdateName[];
extern const char kImeFstDecoderParamsUpdateDescription[];

extern const char kImeTrayHideVoiceButtonName[];
extern const char kImeTrayHideVoiceButtonDescription[];

extern const char kImeKoreanModeSwitchDebugName[];
extern const char kImeKoreanModeSwitchDebugDescription[];

extern const char kImeKoreanOnlyModeSwitchOnRightAltName[];
extern const char kImeKoreanOnlyModeSwitchOnRightAltDescription[];

extern const char kIppFirstSetupForUsbPrintersName[];
extern const char kIppFirstSetupForUsbPrintersDescription[];

extern const char kLacrosProfileMigrationForceOffName[];
extern const char kLacrosProfileMigrationForceOffDescription[];

extern const char kLacrosProfileBackwardMigrationName[];
extern const char kLacrosProfileBackwardMigrationDescription[];

extern const char kVirtualKeyboardNewHeaderName[];
extern const char kVirtualKeyboardNewHeaderDescription[];

extern const char kHindiInscriptLayoutName[];
extern const char kHindiInscriptLayoutDescription[];

extern const char kImeSystemEmojiPickerExtensionName[];
extern const char kImeSystemEmojiPickerExtensionDescription[];

extern const char kImeSystemEmojiPickerGIFSupportName[];
extern const char kImeSystemEmojiPickerGIFSupportDescription[];

extern const char kImeSystemEmojiPickerJellySupportName[];
extern const char kImeSystemEmojiPickerJellySupportDescription[];

extern const char kImeSystemEmojiPickerClipboardName[];
extern const char kImeSystemEmojiPickerClipboardDescription[];

extern const char kImeSystemEmojiPickerSearchExtensionName[];
extern const char kImeSystemEmojiPickerSearchExtensionDescription[];

extern const char kImeSystemEmojiPickerVariantGroupingName[];
extern const char kImeSystemEmojiPickerVariantGroupingDescription[];

extern const char kImeStylusHandwritingName[];
extern const char kImeStylusHandwritingDescription[];

extern const char kImeUsEnglishModelUpdateName[];
extern const char kImeUsEnglishModelUpdateDescription[];

extern const char kJellyColorsName[];
extern const char kJellyColorsDescription[];

extern const char kCrosComponentsName[];
extern const char kCrosComponentsDescription[];

extern const char kLacrosAvailabilityIgnoreName[];
extern const char kLacrosAvailabilityIgnoreDescription[];

extern const char kLacrosOnlyName[];
extern const char kLacrosOnlyDescription[];

extern const char kLacrosStabilityName[];
extern const char kLacrosStabilityDescription[];

extern const char kLacrosSelectionName[];
extern const char kLacrosSelectionDescription[];
extern const char kLacrosSelectionRootfsDescription[];
extern const char kLacrosSelectionStatefulDescription[];

extern const char kLacrosSelectionPolicyIgnoreName[];
extern const char kLacrosSelectionPolicyIgnoreDescription[];

extern const char kLacrosWaylandLoggingName[];
extern const char kLacrosWaylandLoggingDescription[];

extern const char kLanguagePacksInSettingsName[];
extern const char kLanguagePacksInSettingsDescription[];

extern const char kLauncherItemSuggestName[];
extern const char kLauncherItemSuggestDescription[];

extern const char kLimitShelfItemsToActiveDeskName[];
extern const char kLimitShelfItemsToActiveDeskDescription[];

extern const char kListAllDisplayModesName[];
extern const char kListAllDisplayModesDescription[];

extern const char kEnableHardwareMirrorModeName[];
extern const char kEnableHardwareMirrorModeDescription[];

extern const char kLockScreenNotificationName[];
extern const char kLockScreenNotificationDescription[];

extern const char kMediaAppPdfA11yOcrName[];
extern const char kMediaAppPdfA11yOcrDescription[];

extern const char kMeteredShowToggleName[];
extern const char kMeteredShowToggleDescription[];

extern const char kMicrophoneMuteNotificationsName[];
extern const char kMicrophoneMuteNotificationsDescription[];

extern const char kMicrophoneMuteSwitchDeviceName[];
extern const char kMicrophoneMuteSwitchDeviceDescription[];

extern const char kMultiZoneRgbKeyboardName[];
extern const char kMultiZoneRgbKeyboardDescription[];

extern const char kMultilingualTypingName[];
extern const char kMultilingualTypingDescription[];

extern const char kNearbySharingSelfShareName[];
extern const char kNearbySharingSelfShareDescription[];

extern const char kNearbyPresenceName[];
extern const char kNearbyPresenceDescription[];

extern const char kOobeHidDetectionRevampName[];
extern const char kOobeHidDetectionRevampDescription[];

extern const char kOsSettingsAppBadgingToggleName[];
extern const char kOsSettingsAppBadgingToggleDescription[];

extern const char kOsSettingsRevampWayfindingName[];
extern const char kOsSettingsRevampWayfindingDescription[];

extern const char kPcieBillboardNotificationName[];
extern const char kPcieBillboardNotificationDescription[];

extern const char kPerformantSplitViewResizing[];
extern const char kPerformantSplitViewResizingDescription[];

extern const char kPhoneHubCallNotificationName[];
extern const char kPhoneHubCallNotificationDescription[];

extern const char kPhoneHubOnboardingNotifierRevampName[];
extern const char kPhoneHubOnboardingNotifierRevampDescription[];

extern const char kPolicyProvidedTrustAnchorsAllowedAtLockScreenName[];
extern const char kPolicyProvidedTrustAnchorsAllowedAtLockScreenDescription[];

extern const char kPreferDcheckName[];
extern const char kPreferDcheckDescription[];

extern const char kPrinterSettingsPrinterStatusName[];
extern const char kPrinterSettingsPrinterStatusDescription[];

extern const char kPrinterSettingsRevampName[];
extern const char kPrinterSettingsRevampDescription[];

extern const char kPrintPreviewDiscoveredPrintersName[];
extern const char kPrintPreviewDiscoveredPrintersDescription[];

extern const char kPrintingPpdChannelName[];
extern const char kPrintingPpdChannelDescription[];

extern const char kPrintManagementJellyName[];
extern const char kPrintManagementJellyDescription[];

extern const char kPrintManagementSetupAssistanceName[];
extern const char kPrintManagementSetupAssistanceDescription[];

extern const char kProductivityLauncherName[];
extern const char kProductivityLauncherDescription[];

extern const char kProductivityLauncherImageSearchName[];
extern const char kProductivityLauncherImageSearchDescription[];

extern const char kProjectorAppDebugName[];
extern const char kProjectorAppDebugDescription[];

extern const char kProjectorServerSideSpeechRecognitionName[];
extern const char kProjectorServerSideSpeechRecognitionDescription[];

extern const char kReleaseNotesNotificationAllChannelsName[];
extern const char kReleaseNotesNotificationAllChannelsDescription[];

extern const char kRenderArcNotificationsByChromeName[];
extern const char kRenderArcNotificationsByChromeDescription[];

extern const char kArcWindowPredictorName[];
extern const char kArcWindowPredictorDescription[];

extern const char kArcInputOverlayNameBeta[];
extern const char kArcInputOverlayDescriptionBeta[];

extern const char kArcInputOverlayNameAlphaV2[];
extern const char kArcInputOverlayDescriptionAlphaV2[];

extern const char kScalableIphDebugName[];
extern const char kScalableIphDebugDescription[];

extern const char kScanningAppJellyName[];
extern const char kScanningAppJellyDescription[];

extern const char kScreencastV2Name[];
extern const char kScreencastV2Description[];

extern const char kSealKeyName[];
extern const char kSealKeyDescription[];

extern const char kSeaPenName[];
extern const char kSeaPenDescription[];

extern const char kShelfAutoHideSeparationName[];
extern const char kShelfAutoHideSeparationDescription[];

extern const char kShimlessRMAComplianceCheckName[];
extern const char kShimlessRMAComplianceCheckDescription[];

extern const char kShimlessRMASkuDescriptionName[];
extern const char kShimlessRMASkuDescriptionDescription[];

extern const char kShimlessRMAOsUpdateName[];
extern const char kShimlessRMAOsUpdateDescription[];

extern const char kShortcutCustomizationJellyName[];
extern const char kShortcutCustomizationJellyDescription[];

extern const char kSchedulerConfigurationName[];
extern const char kSchedulerConfigurationDescription[];

extern const char kSchedulerConfigurationConservative[];
extern const char kSchedulerConfigurationPerformance[];

extern const char kSnapGroupsName[];
extern const char kSnapGroupsDescription[];

extern const char kMediaDynamicCgroupName[];
extern const char kMediaDynamicCgroupDescription[];

extern const char kMissiveStorageName[];
extern const char kMissiveStorageDescription[];

extern const char kShowBluetoothDebugLogToggleName[];
extern const char kShowBluetoothDebugLogToggleDescription[];

extern const char kBluetoothSessionizedMetricsName[];
extern const char kBluetoothSessionizedMetricsDescription[];

extern const char kShowTapsName[];
extern const char kShowTapsDescription[];

extern const char kShowTouchHudName[];
extern const char kShowTouchHudDescription[];

extern const char kSnoopingProtectionName[];
extern const char kSnoopingProtectionDescription[];

extern const char kContinuousOverviewScrollAnimationName[];
extern const char kContinuousOverviewScrollAnimationDescription[];

extern const char kSpectreVariant2MitigationName[];
extern const char kSpectreVariant2MitigationDescription[];

extern const char kSystemJapanesePhysicalTypingName[];
extern const char kSystemJapanesePhysicalTypingDescription[];

extern const char kSupportF11AndF12ShortcutsName[];
extern const char kSupportF11AndF12ShortcutsDescription[];

extern const char kTerminalAlternativeEmulatorName[];
extern const char kTerminalAlternativeEmulatorDescription[];

extern const char kTerminalDevName[];
extern const char kTerminalDevDescription[];

extern const char kTetherName[];
extern const char kTetherDescription[];

extern const char kTimeOfDayWallpaperForcedAutoScheduleName[];
extern const char kTimeOfDayWallpaperForcedAutoScheduleDescription[];

extern const char kTimeOfDayDlcName[];
extern const char kTimeOfDayDlcDescription[];

extern const char kTouchscreenCalibrationName[];
extern const char kTouchscreenCalibrationDescription[];

extern const char kTrafficCountersEnabledName[];
extern const char kTrafficCountersEnabledDescription[];

extern const char kUiDevToolsName[];
extern const char kUiDevToolsDescription[];

extern const char kUiSlowAnimationsName[];
extern const char kUiSlowAnimationsDescription[];

extern const char kUploadOfficeToCloudName[];
extern const char kUploadOfficeToCloudDescription[];

extern const char kUpstreamTrustedReportsFirmwareName[];
extern const char kUpstreamTrustedReportsFirmwareDescription[];

extern const char kUseFakeDeviceForMediaStreamName[];
extern const char kUseFakeDeviceForMediaStreamDescription[];

extern const char kVcDlcUiName[];
extern const char kVcDlcUiDescription[];

extern const char kVirtualKeyboardName[];
extern const char kVirtualKeyboardDescription[];

extern const char kVirtualKeyboardDisabledName[];
extern const char kVirtualKeyboardDisabledDescription[];

extern const char kVirtualKeyboardGlobalEmojiPreferencesName[];
extern const char kVirtualKeyboardGlobalEmojiPreferencesDescription[];

extern const char kVirtualKeyboardRoundCornersName[];
extern const char kVirtualKeyboardRoundCornersDescription[];

extern const char kVmMemoryManagementServiceName[];
extern const char kVmMemoryManagementServiceDescription[];

extern const char kWakeOnWifiAllowedName[];
extern const char kWakeOnWifiAllowedDescription[];

extern const char kWelcomeTourName[];
extern const char kWelcomeTourDescription[];

extern const char kWelcomeTourForceUserEligibilityName[];
extern const char kWelcomeTourForceUserEligibilityDescription[];

extern const char kWifiConnectMacAddressRandomizationName[];
extern const char kWifiConnectMacAddressRandomizationDescription[];

extern const char kWindowSplittingName[];
extern const char kWindowSplittingDescription[];

extern const char kLacrosSharedComponentsDirName[];
extern const char kLacrosSharedComponentsDirDescription[];

extern const char kLacrosLaunchAtLoginScreenName[];
extern const char kLacrosLaunchAtLoginScreenDescription[];

extern const char kLacrosForkZygotesAtLoginScreenName[];
extern const char kLacrosForkZygotesAtLoginScreenDescription[];

extern const char kLauncherGameSearchName[];
extern const char kLauncherGameSearchDescription[];

extern const char kLauncherKeywordExtractionScoring[];
extern const char kLauncherKeywordExtractionScoringDescription[];

extern const char kLauncherLocalImageSearchName[];
extern const char kLauncherLocalImageSearchDescription[];

extern const char kLauncherLocalImageSearchOcrName[];
extern const char kLauncherLocalImageSearchOcrDescription[];

extern const char kLauncherLocalImageSearchIcaName[];
extern const char kLauncherLocalImageSearchIcaDescription[];

extern const char kLauncherFuzzyMatchAcrossProvidersName[];
extern const char kLauncherFuzzyMatchAcrossProvidersDescription[];

extern const char kLauncherFuzzyMatchForOmniboxName[];
extern const char kLauncherFuzzyMatchForOmniboxDescription[];

extern const char kLauncherSearchControlName[];
extern const char kLauncherSearchControlDescription[];

extern const char kLauncherNudgeSessionResetName[];
extern const char kLauncherNudgeSessionResetDescription[];

extern const char kLauncherSystemInfoAnswerCardsName[];
extern const char kLauncherSystemInfoAnswerCardsDescription[];

extern const char kLauncherManateeForKeyboardShortcutsName[];
extern const char kLauncherManateeForKeyboardShortcutsDescription[];

extern const char kMacAddressRandomizationName[];
extern const char kMacAddressRandomizationDescription[];

extern const char kSmdsSupportName[];
extern const char kSmdsSupportDescription[];

extern const char kSysUiDownloadIntegrationV2Name[];
extern const char kSysUiDownloadIntegrationV2Description[];

extern const char kTetheringExperimentalFunctionalityName[];
extern const char kTetheringExperimentalFunctionalityDescription[];

// Prefer keeping this section sorted to adding new declarations down here.

#endif  // #if BUILDFLAG(IS_CHROMEOS_ASH)

#if BUILDFLAG(IS_CHROMEOS_LACROS)
extern const char kExperimentalWebAppStoragePartitionIsolationName[];
extern const char kExperimentalWebAppStoragePartitionIsolationDescription[];

extern const char kBlinkExtensionName[];
extern const char kBlinkExtensionDescription[];

extern const char kBlinkExtensionDiagnosticsName[];
extern const char kBlinkExtensionDiagnosticsDescription[];

extern const char kLacrosMergeIcuDataFileName[];
extern const char kLacrosMergeIcuDataFileDescription[];
#endif  // #if BUILDFLAG(IS_CHROMEOS_LACROS)

#if BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_LINUX)
extern const char kGetAllScreensMediaName[];
extern const char kGetAllScreensMediaDescription[];
#endif  // BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_LINUX)

#if BUILDFLAG(IS_CHROMEOS)
extern const char kAppToAppLinkCapturingName[];
extern const char kAppToAppLinkCapturingDescription[];

extern const char kAppToAppLinkCapturingWorkspaceAppsName[];
extern const char kAppToAppLinkCapturingWorkspaceAppsDescription[];

extern const char kCrosAppsBackgroundEventHandlingName[];
extern const char kCrosAppsBackgroundEventHandlingDescription[];

extern const char kCrosOmniboxInstallDialogName[];
extern const char kCrosOmniboxInstallDialogDescription[];

extern const char kCrosWebAppInstallDialogName[];
extern const char kCrosWebAppInstallDialogDescription[];

extern const char kRunOnOsLoginName[];
extern const char kRunOnOsLoginDescription[];

extern const char kPreventCloseName[];
extern const char kPreventCloseDescription[];

extern const char kFileSystemAccessGetCloudIdentifiersName[];
extern const char kFileSystemAccessGetCloudIdentifiersDescription[];

extern const char kCrOSDspBasedAecAllowedName[];
extern const char kCrOSDspBasedAecAllowedDescription[];

extern const char kCrOSDspBasedNsAllowedName[];
extern const char kCrOSDspBasedNsAllowedDescription[];

extern const char kCrOSDspBasedAgcAllowedName[];
extern const char kCrOSDspBasedAgcAllowedDescription[];

extern const char kCrosPrivacyHubName[];
extern const char kCrosPrivacyHubDescription[];

extern const char kCrosPrivacyHubAppPermissionsName[];
extern const char kCrosPrivacyHubAppPermissionsDescription[];

extern const char kCrosPrivacyHubV0Name[];
extern const char kCrosPrivacyHubV0Description[];

extern const char kCrOSEnforceSystemAecName[];
extern const char kCrOSEnforceSystemAecDescription[];

extern const char kCrOSEnforceSystemAecAgcName[];
extern const char kCrOSEnforceSystemAecAgcDescription[];

extern const char kCrOSEnforceSystemAecNsName[];
extern const char kCrOSEnforceSystemAecNsDescription[];

extern const char kCrOSEnforceSystemAecNsAgcName[];
extern const char kCrOSEnforceSystemAecNsAgcDescription[];

extern const char kDisableIdleSocketsCloseOnMemoryPressureName[];
extern const char kDisableIdleSocketsCloseOnMemoryPressureDescription[];

extern const char kDisableOfficeEditingComponentAppName[];
extern const char kDisableOfficeEditingComponentAppDescription[];

extern const char kEnableBorderlessPrintingName[];
extern const char kEnableBorderlessPrintingDescription[];

extern const char kLacrosColorManagementName[];
extern const char kLacrosColorManagementDescription[];

extern const char kLockedModeName[];
extern const char kLockedModeDescription[];

extern const char kOneGroupPerRendererName[];
extern const char kOneGroupPerRendererDescription[];

extern const char kPlatformKeysAesEncryptionName[];
extern const char kPlatformKeysAesEncryptionDescription[];

extern const char kPreinstalledWebAppWindowExperimentName[];
extern const char kPreinstalledWebAppWindowExperimentDescription[];

extern const char kPrintPreviewSetupAssistanceName[];
extern const char kPrintPreviewSetupAssistanceDescription[];

extern const char kLocalPrinterObservingName[];
extern const char kLocalPrinterObservingDescription[];

extern const char kDisableQuickAnswersV2TranslationName[];
extern const char kDisableQuickAnswersV2TranslationDescription[];

extern const char kQuickAnswersRichCardName[];
extern const char kQuickAnswersRichCardDescription[];

extern const char kSyncChromeOSExplicitPassphraseSharingName[];
extern const char kSyncChromeOSExplicitPassphraseSharingDescription[];

extern const char kQuickOfficeForceFileDownloadName[];
extern const char kQuickOfficeForceFileDownloadDescription[];

extern const char kWebPrintingApiName[];
extern const char kWebPrintingApiDescription[];

extern const char kIgnoreUiGainsName[];
extern const char kIgnoreUiGainsDescription[];

extern const char kShowForceRespectUiGainsToggleName[];
extern const char kShowForceRespectUiGainsToggleDescription[];

extern const char kCrOSSystemVoiceIsolationOptionName[];
extern const char kCrOSSystemVoiceIsolationOptionDescription[];

extern const char kAudioFlexibleLoopbackForSystemLoopbackName[];
extern const char kAudioFlexibleLoopbackForSystemLoopbackDescription[];
#endif  // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_CHROMEOS) && BUILDFLAG(USE_CHROMEOS_MEDIA_ACCELERATION)
extern const char kChromeOSDirectVideoDecoderName[];
extern const char kChromeOSDirectVideoDecoderDescription[];
extern const char kChromeOSHWVBREncodingName[];
extern const char kChromeOSHWVBREncodingDescription[];
#if defined(ARCH_CPU_ARM_FAMILY)
extern const char kUseGLForScalingName[];
extern const char kUseGLForScalingDescription[];
extern const char kPreferGLImageProcessorName[];
extern const char kPreferGLImageProcessorDescription[];
extern const char kPreferSoftwareMT21Name[];
extern const char kPreferSoftwareMT21Description[];
#endif  // defined(ARCH_CPU_ARM_FAMILY
#endif  // BUILDFLAG(IS_CHROMEOS) && BUILDFLAG(USE_CHROMEOS_MEDIA_ACCELERATION)

#if BUILDFLAG(USE_CHROMEOS_MEDIA_ACCELERATION) && defined(ARCH_CPU_ARM_FAMILY)
extern const char kV4L2FlatStatefulVideoDecoderName[];
extern const char kV4L2FlatStatefulVideoDecoderDescription[];
#endif

// Linux ---------------------------------------------------------------------

#if BUILDFLAG(IS_LINUX)
extern const char kOzonePlatformHintChoiceDefault[];
extern const char kOzonePlatformHintChoiceAuto[];
extern const char kOzonePlatformHintChoiceX11[];
extern const char kOzonePlatformHintChoiceWayland[];

extern const char kOzonePlatformHintName[];
extern const char kOzonePlatformHintDescription[];

extern const char kPulseaudioLoopbackForCastName[];
extern const char kPulseaudioLoopbackForCastDescription[];
extern const char kPulseaudioLoopbackForScreenShareName[];
extern const char kPulseaudioLoopbackForScreenShareDescription[];
#endif  // BUILDFLAG(IS_LINUX)

// All views-based platforms --------------------------------------------------

#if defined(TOOLKIT_VIEWS)
extern const char kSideSearchName[];
extern const char kSideSearchDescription[];

extern const char kSearchWebInSidePanelName[];
extern const char kSearchWebInSidePanelDescription[];
#endif  // defined(TOOLKIT_VIEWS)

// Random platform combinations -----------------------------------------------

#if BUILDFLAG(IS_CHROMEOS_ASH) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
extern const char kZeroCopyVideoCaptureName[];
extern const char kZeroCopyVideoCaptureDescription[];
#endif  // BUILDFLAG(IS_CHROMEOS_ASH) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_FUCHSIA)
extern const char kQuickCommandsName[];
extern const char kQuickCommandsDescription[];

#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) ||
        // defined (OS_FUCHSIA)

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_MAC)
extern const char kWebShareName[];
extern const char kWebShareDescription[];
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_MAC)

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)
extern const char kWebBluetoothConfirmPairingSupportName[];
extern const char kWebBluetoothConfirmPairingSupportDescription[];
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC)
extern const char kSkipUndecryptablePasswordsName[];
extern const char kSkipUndecryptablePasswordsDescription[];

extern const char kForcePasswordInitialSyncWhenDecryptionFailsName[];
extern const char kForcePasswordInitialSyncWhenDecryptionFailsDescription[];

extern const char kRestartToGainAccessToKeychainName[];
extern const char kRestartToGainAccessToKeychainDescription[];
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC)

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)
extern const char kAsyncDnsName[];
extern const char kAsyncDnsDescription[];
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_FUCHSIA) || BUILDFLAG(IS_CHROMEOS)
extern const char kFollowingFeedSidepanelName[];
extern const char kFollowingFeedSidepanelDescription[];
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) ||
        // BUILDFLAG(IS_FUCHSIA) || BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(ENABLE_SUPERVISED_USERS)
extern const char kEnableProtoApiForClassifyUrlName[];
extern const char kEnableProtoApiForClassifyUrlDescription[];
#endif  // BUILDFLAG(ENABLE_SUPERVISED_USERS)

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
extern const char kEnableNetworkServiceSandboxName[];
extern const char kEnableNetworkServiceSandboxDescription[];

extern const char kUseOutOfProcessVideoDecodingName[];
extern const char kUseOutOfProcessVideoDecodingDescription[];
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)

// Feature flags --------------------------------------------------------------

#if BUILDFLAG(CHROME_WIDE_ECHO_CANCELLATION)
extern const char kChromeWideEchoCancellationName[];
extern const char kChromeWideEchoCancellationDescription[];
#endif  // BUILDFLAG(CHROME_WIDE_ECHO_CANCELLATION)

#if BUILDFLAG(DCHECK_IS_CONFIGURABLE)
extern const char kDcheckIsFatalName[];
extern const char kDcheckIsFatalDescription[];
#endif  // BUILDFLAG(DCHECK_IS_CONFIGURABLE)

#if BUILDFLAG(ENABLE_NACL)
extern const char kNaclName[];
extern const char kNaclDescription[];

extern const char kVerboseLoggingInNaclName[];
extern const char kVerboseLoggingInNaclDescription[];
extern const char kVerboseLoggingInNaclChoiceDefault[];
extern const char kVerboseLoggingInNaclChoiceLow[];
extern const char kVerboseLoggingInNaclChoiceMedium[];
extern const char kVerboseLoggingInNaclChoiceHigh[];
extern const char kVerboseLoggingInNaclChoiceHighest[];
extern const char kVerboseLoggingInNaclChoiceDisabled[];
#endif  // ENABLE_NACL

#if BUILDFLAG(ENABLE_OOP_PRINTING)
extern const char kEnableOopPrintDriversName[];
extern const char kEnableOopPrintDriversDescription[];
#endif  // BUILDFLAG(ENABLE_OOP_PRINTING)

#if BUILDFLAG(ENABLE_PAINT_PREVIEW) && BUILDFLAG(IS_ANDROID)
extern const char kPaintPreviewDemoName[];
extern const char kPaintPreviewDemoDescription[];
#endif  // ENABLE_PAINT_PREVIEW && BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(ENABLE_VR)
extern const char kWebXrInternalsName[];
extern const char kWebXrInternalsDescription[];
#endif  // #if defined(ENABLE_VR)

#if BUILDFLAG(ENABLE_WEBUI_TAB_STRIP)
extern const char kWebUITabStripFlagId[];
extern const char kWebUITabStripName[];
extern const char kWebUITabStripDescription[];

extern const char kWebUITabStripContextMenuAfterTapName[];
extern const char kWebUITabStripContextMenuAfterTapDescription[];
#endif  // BUILDFLAG(ENABLE_WEBUI_TAB_STRIP)

#if defined(TOOLKIT_VIEWS) || BUILDFLAG(IS_ANDROID)

extern const char kAutofillCreditCardUploadName[];
extern const char kAutofillCreditCardUploadDescription[];

#endif  // defined(TOOLKIT_VIEWS) || BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_ANDROID)
extern const char kElasticOverscrollName[];
extern const char kElasticOverscrollDescription[];
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_ANDROID)

#if !BUILDFLAG(IS_ANDROID)
extern const char kElementCaptureName[];
extern const char kElementCaptureDescription[];
#endif  // !BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_WIN) ||                                      \
    (BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS_LACROS)) || \
    BUILDFLAG(IS_MAC) || BUILDFLAG(IS_FUCHSIA)
extern const char kUIDebugToolsName[];
extern const char kUIDebugToolsDescription[];

extern const char kSyncPollImmediatelyOnEveryStartupName[];
extern const char kSyncPollImmediatelyOnEveryStartupDescription[];
#endif

#if defined(WEBRTC_USE_PIPEWIRE)
extern const char kWebrtcPipeWireCapturerName[];
extern const char kWebrtcPipeWireCapturerDescription[];
#endif  // #if defined(WEBRTC_USE_PIPEWIRE)

#if BUILDFLAG(IS_CHROMEOS_ASH)
extern const char kChromeKioskEnableLacrosName[];
extern const char kChromeKioskEnableLacrosDescription[];

extern const char kWebKioskEnableLacrosName[];
extern const char kWebKioskEnableLacrosDescription[];

extern const char kDisableLacrosTtsSupportName[];
extern const char kDisableLacrosTtsSupportDescription[];

extern const char kPromiseIconsName[];
extern const char kPromiseIconsDescription[];

extern const char kEnableAudioFocusEnforcementName[];
extern const char kEnableAudioFocusEnforcementDescription[];
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

#if BUILDFLAG(ENABLE_COMPOSE)
extern const char kComposeId[];
extern const char kComposeName[];
extern const char kComposeDescription[];
#endif  // BUILDFLAG(ENABLE_COMPOSE)

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
extern const char kThirdPartyProfileManagementName[];
extern const char kThirdPartyProfileManagementDescription[];

extern const char kUnoDesktopName[];
extern const char kUnoDesktopDescription[];

extern const char kDesktopPWAsUserLinkCapturingName[];
extern const char kDesktopPWAsUserLinkCapturingDescription[];
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)

#if BUILDFLAG(ENABLE_HLS_DEMUXER)
extern const char kEnableBuiltinHlsName[];
extern const char kEnableBuiltinHlsDescription[];
#endif  // BUILDFLAG(ENABLE_HLS_DEMUXER)

#if !BUILDFLAG(IS_CHROMEOS_ASH)
extern const char kProfilesReorderingName[];
extern const char kProfilesReorderingDescription[];
#endif

#if BUILDFLAG(ENABLE_BOUND_SESSION_CREDENTIALS)
extern const char kEnableBoundSessionCredentialsName[];
extern const char kEnableBoundSessionCredentialsDescription[];

extern const char
    kEnableBoundSessionCredentialsSoftwareKeysForManualTestingName[];
extern const char
    kEnableBoundSessionCredentialsSoftwareKeysForManualTestingDescription[];
#endif  // BUILDFLAG(ENABLE_BOUND_SESSION_CREDENTIALS)

#if BUILDFLAG(ENABLE_FFMPEG_VIDEO_DECODERS)
extern const char kTheoraVideoCodecName[];
extern const char kTheoraVideoCodecDescription[];
#endif

extern const char kForceSigninFlowInProfilePickerName[];
extern const char kForceSigninFlowInProfilePickerDescription[];

// ============================================================================
// Don't just add flags to the end, put them in the right section in
// alphabetical order. See top instructions for more.
// ============================================================================

}  // namespace flag_descriptions

#endif  // CHROME_BROWSER_FLAG_DESCRIPTIONS_H_
