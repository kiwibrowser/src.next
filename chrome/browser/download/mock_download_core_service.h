// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_DOWNLOAD_MOCK_DOWNLOAD_CORE_SERVICE_H_
#define CHROME_BROWSER_DOWNLOAD_MOCK_DOWNLOAD_CORE_SERVICE_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "chrome/browser/download/chrome_download_manager_delegate.h"
#include "chrome/browser/download/download_core_service.h"
#include "extensions/buildflags/buildflags.h"
#include "testing/gmock/include/gmock/gmock.h"

class MockDownloadCoreService : public DownloadCoreService {
 public:
  MockDownloadCoreService();

  MockDownloadCoreService(const MockDownloadCoreService&) = delete;
  MockDownloadCoreService& operator=(const MockDownloadCoreService&) = delete;

  ~MockDownloadCoreService() override;

  // KeyedService:
  MOCK_METHOD(void, Shutdown, (), (override));

  MOCK_METHOD(ChromeDownloadManagerDelegate*,
              GetDownloadManagerDelegate,
              (),
              (override));
  MOCK_METHOD(DownloadUIController*, GetDownloadUIController, (), (override));
  MOCK_METHOD(DownloadHistory*, GetDownloadHistory, (), (override));
#if BUILDFLAG(ENABLE_EXTENSIONS_CORE)
  MOCK_METHOD(extensions::ExtensionDownloadsEventRouter*,
              GetExtensionEventRouter,
              (),
              (override));
#endif
  MOCK_METHOD(bool, HasCreatedDownloadManager, (), (override));
  MOCK_METHOD(int, BlockingShutdownCount, (), (const, override));
  MOCK_METHOD(void,
              CancelDownloads,
              (DownloadCoreService::CancelDownloadsTrigger),
              (override));
  MOCK_METHOD(void,
              SetDownloadManagerDelegateForTesting,
              (std::unique_ptr<ChromeDownloadManagerDelegate> delegate),
              (override));
  MOCK_METHOD(bool, IsDownloadUiEnabled, (), (override));
};

#endif  // CHROME_BROWSER_DOWNLOAD_MOCK_DOWNLOAD_CORE_SERVICE_H_
