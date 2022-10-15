// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_DOWNLOAD_DOWNLOAD_STATS_H_
#define CHROME_BROWSER_DOWNLOAD_DOWNLOAD_STATS_H_

#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/download/download_commands.h"
#include "chrome/browser/download/download_prompt_status.h"
#include "chrome/browser/profiles/profile.h"
#include "components/download/public/common/download_danger_type.h"
#include "components/download/public/common/download_path_reservation_tracker.h"

// Used for counting UMA stats. Similar to content's
// download_stats::DownloadCountTypes but from the chrome layer.
enum ChromeDownloadCountTypes {
  // Stale enum values left around os that values passed to UMA don't
  // change.
  CHROME_DOWNLOAD_COUNT_UNUSED_0 = 0,
  CHROME_DOWNLOAD_COUNT_UNUSED_1,
  CHROME_DOWNLOAD_COUNT_UNUSED_2,
  CHROME_DOWNLOAD_COUNT_UNUSED_3,

  // A download *would* have been initiated, but it was blocked
  // by the DownloadThrottlingResourceHandler.
  CHROME_DOWNLOAD_COUNT_BLOCKED_BY_THROTTLING,

  CHROME_DOWNLOAD_COUNT_TYPES_LAST_ENTRY
};

// Used for counting UMA stats. Similar to content's
// download_stats::DownloadInitiattionSources but from the chrome layer.
enum ChromeDownloadSource {
  // The download was initiated by navigating to a URL (e.g. by user click).
  DOWNLOAD_INITIATED_BY_NAVIGATION = 0,

  // The download was initiated by invoking a context menu within a page.
  DOWNLOAD_INITIATED_BY_CONTEXT_MENU,

  // Formerly DOWNLOAD_INITIATED_BY_WEBSTORE_INSTALLER.
  CHROME_DOWNLOAD_SOURCE_UNUSED_2,

  // Formerly DOWNLOAD_INITIATED_BY_IMAGE_BURNER.
  CHROME_DOWNLOAD_SOURCE_UNUSED_3,

  // Formerly DOWNLOAD_INITIATED_BY_PLUGIN_INSTALLER.
  CHROME_DOWNLOAD_SOURCE_UNUSED_4,

  // The download was initiated by the PDF plugin.
  DOWNLOAD_INITIATED_BY_PDF_SAVE,

  // Formerly DOWNLOAD_INITIATED_BY_EXTENSION.
  CHROME_DOWNLOAD_SOURCE_UNUSED_6,

  CHROME_DOWNLOAD_SOURCE_LAST_ENTRY
};

// How a download was opened. Note that a download could be opened multiple
// times.
enum ChromeDownloadOpenMethod {
  // The download was opened using the platform handler. There was no special
  // handling for this download.
  DOWNLOAD_OPEN_METHOD_DEFAULT_PLATFORM = 0,

  // The download was opened using the browser bypassing the system handler.
  DOWNLOAD_OPEN_METHOD_DEFAULT_BROWSER,

  // The user chose to open the download using the system handler even though
  // the preferred method was to open the download using the browser.
  DOWNLOAD_OPEN_METHOD_USER_PLATFORM,

  // The download was opened using a rename handler.
  DOWNLOAD_OPEN_METHOD_RENAME_HANDLER,

  DOWNLOAD_OPEN_METHOD_LAST_ENTRY
};

// Records path generation behavior in download target determination process.
// Used in UMA, do not remove, change or reuse existing entries.
// Update histograms.xml and enums.xml when adding entries.
enum class DownloadPathGenerationEvent {
  // Use existing virtual path provided to download target determiner.
  USE_EXISTING_VIRTUAL_PATH = 0,
  // Use the force path provided to download target determiner.
  USE_FORCE_PATH,
  // Use last prompt directory.
  USE_LAST_PROMPT_DIRECTORY,
  // Use the default download directory.
  USE_DEFAULTL_DOWNLOAD_DIRECTORY,
  // No valid target file path is provided, the download will fail soon.
  NO_VALID_PATH,

  COUNT
};

// Records reasons that will result in the download being canceled with
// DOWNLOAD_INTERRUPT_REASON_USER_CANCELED.
// Used in UMA, do not remove, change or reuse existing entries.
// Update histograms.xml and enums.xml when adding entries.
enum class DownloadCancelReason {
  // Existed download path after download target determination.
  kExistingDownloadPath = 0,
  // Canceled due to download target determiner confirmation result.
  kTargetConfirmationResult = 1,
  // Canceled due to no valid virtual path.
  kNoValidPath = 2,
  // Canceled due to no mixed content.
  kMixedContent = 3,
  // Canceled due to failed path reservacation.
  kFailedPathReservation = 4,
  // Canceled due to empty local path.
  kEmptyLocalPath = 5,
  kMaxValue = kEmptyLocalPath
};

// Increment one of the above counts.
void RecordDownloadCount(ChromeDownloadCountTypes type);

// Record initiation of a download from a specific source.
void RecordDownloadSource(ChromeDownloadSource source);

// Record that a download warning was shown.
void RecordDangerousDownloadWarningShown(
    download::DownloadDangerType danger_type,
    const base::FilePath& file_path,
    bool is_https,
    bool has_user_gesture);

// Record that the user opened the confirmation dialog for a dangerous download.
void RecordOpenedDangerousConfirmDialog(
    download::DownloadDangerType danger_type);

// Record that a download was opened.
void RecordDownloadOpen(ChromeDownloadOpenMethod open_method,
                        const std::string& mime_type_string);

// Record if the database is available to provide the next download id before
// starting all downloads.
void RecordDatabaseAvailability(bool is_available);

// Record download path generation event in target determination process.
void RecordDownloadPathGeneration(DownloadPathGenerationEvent event,
                                  bool is_transient);

// Record path validation result.
void RecordDownloadPathValidation(download::PathValidationResult result,
                                  bool is_transient);

// Record download cancel reason.
void RecordDownloadCancelReason(DownloadCancelReason reason);

// Records information related to dragging completed downloads from the
// shelf/bubble. Used in UMA. Do not remove, change or reuse existing entries.
// Update histograms.xml and enums.xml when adding entries.
enum class DownloadDragInfo {
  // A download starting to be dragged. It is possible the drag-and-drop will
  // not complete depending on the user's actions.
  DRAG_STARTED,
  // As a point of reference for dragged downloads, this represents when a
  // download completes on the shelf/bubble. This omits downloads that are
  // immediately removed from the shelf/bubble when they complete.
  DOWNLOAD_COMPLETE,

  COUNT
};

// Records either when a drag event is initiated by the user or, as a point of
// reference, when a download completes on the shelf/bubble.
void RecordDownloadShelfDragInfo(DownloadDragInfo drag_info);
void RecordDownloadBubbleDragInfo(DownloadDragInfo drag_info);

void RecordDownloadStartPerProfileType(Profile* profile);

#if BUILDFLAG(IS_ANDROID)
// Records whether the download dialog is shown to the user.
void RecordDownloadPromptStatus(DownloadPromptStatus status);
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_CHROMEOS)
// Records that a notification for a download was suppressed.
void RecordDownloadNotificationSuppressed();
#endif  // BUILDFLAG(IS_CHROMEOS)

enum class DownloadShelfContextMenuAction {
  // Drop down button for download shelf context menu is visible
  kDropDownShown = 0,
  // Drop down button was pressed
  kDropDownPressed = 1,
  kShowInFolderEnabled = 2,
  kShowInFolderClicked = 3,
  kOpenWhenCompleteEnabled = 4,
  kOpenWhenCompleteClicked = 5,
  kAlwaysOpenTypeEnabled = 6,
  kAlwaysOpenTypeClicked = 7,
  kPlatformOpenEnabled = 8,
  kPlatformOpenClicked = 9,
  kCancelEnabled = 10,
  kCancelClicked = 11,
  kPauseEnabled = 12,
  kPauseClicked = 13,
  kResumeEnabled = 14,
  kResumeClicked = 15,
  kDiscardEnabled = 16,
  kDiscardClicked = 17,
  kKeepEnabled = 18,
  kKeepClicked = 19,
  kLearnMoreScanningEnabled = 20,
  kLearnMoreScanningClicked = 21,
  kLearnMoreInterruptedEnabled = 22,
  kLearnMoreInterruptedClicked = 23,
  kLearnMoreMixedContentEnabled = 24,
  kLearnMoreMixedContentClicked = 25,
  kCopyToClipboardEnabled = 26,
  kCopyToClipboardClicked = 27,
  // kAnnotateEnabled = 28,
  // kAnnotateClicked = 29,
  kDeepScanEnabled = 30,
  kDeepScanClicked = 31,
  kBypassDeepScanningEnabled = 32,
  kBypassDeepScanningClicked = 33,
  // kReviewEnabled = 34,
  // kReviewClicked = 35,
  kNotReached = 36,  // Should not be possible to hit
  kMaxValue = kNotReached
};

DownloadShelfContextMenuAction DownloadCommandToShelfAction(
    DownloadCommands::Command download_command,
    bool clicked);

#endif  // CHROME_BROWSER_DOWNLOAD_DOWNLOAD_STATS_H_
