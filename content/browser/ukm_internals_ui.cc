// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/ukm_internals_ui.h"

#include <stddef.h>

#include <memory>
#include <string>
#include <utility>

#include "base/bind.h"
#include "base/memory/raw_ptr.h"
#include "components/ukm/debug/ukm_debug_data_extractor.h"
#include "components/ukm/ukm_service.h"
#include "content/grit/content_resources.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "content/public/common/content_client.h"
#include "content/public/common/url_constants.h"

namespace content {
namespace {

WebUIDataSource* CreateUkmHTMLSource() {
  WebUIDataSource* source = WebUIDataSource::Create(kChromeUIUkmHost);

  source->AddResourcePath("ukm_internals.js", IDR_UKM_INTERNALS_JS);
  source->AddResourcePath("ukm_internals.css", IDR_UKM_INTERNALS_CSS);
  source->SetDefaultResource(IDR_UKM_INTERNALS_HTML);
  return source;
}

// This class receives javascript messages from the renderer.
// Note that the WebUI infrastructure runs on the UI thread, therefore all of
// this class's methods are expected to run on the UI thread.
class UkmMessageHandler : public WebUIMessageHandler {
 public:
  explicit UkmMessageHandler(const ukm::UkmService* ukm_service);

  UkmMessageHandler(const UkmMessageHandler&) = delete;
  UkmMessageHandler& operator=(const UkmMessageHandler&) = delete;

  ~UkmMessageHandler() override;

  // WebUIMessageHandler:
  void RegisterMessages() override;

 private:
  void HandleRequestUkmData(const base::Value::List& args);

  raw_ptr<const ukm::UkmService> ukm_service_;
};

UkmMessageHandler::UkmMessageHandler(const ukm::UkmService* ukm_service)
    : ukm_service_(ukm_service) {}

UkmMessageHandler::~UkmMessageHandler() {}

void UkmMessageHandler::HandleRequestUkmData(
    const base::Value::List& args_list) {
  AllowJavascript();

  // Identifies the callback, used for when resolving.
  std::string callback_id;
  if (0u < args_list.size() && args_list[0].is_string())
    callback_id = args_list[0].GetString();

  base::Value ukm_debug_data =
      ukm::debug::UkmDebugDataExtractor::GetStructuredData(ukm_service_);

  ResolveJavascriptCallback(base::Value(callback_id),
                            std::move(ukm_debug_data));
}

void UkmMessageHandler::RegisterMessages() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  // We can use base::Unretained() here, as both the callback and this class are
  // owned by UkmInternalsUI.
  web_ui()->RegisterMessageCallback(
      "requestUkmData",
      base::BindRepeating(&UkmMessageHandler::HandleRequestUkmData,
                          base::Unretained(this)));
}

}  // namespace

// Changes to this class should be in sync with its iOS equivalent
// ios/chrome/browser/ui/webui/ukm_internals_ui.mm
UkmInternalsUI::UkmInternalsUI(WebUI* web_ui) : WebUIController(web_ui) {
  ukm::UkmService* ukm_service = GetContentClient()->browser()->GetUkmService();
  web_ui->AddMessageHandler(std::make_unique<UkmMessageHandler>(ukm_service));

  // Set up the chrome://ukm/ source.
  BrowserContext* browser_context =
      web_ui->GetWebContents()->GetBrowserContext();
  WebUIDataSource::Add(browser_context, CreateUkmHTMLSource());
}

}  // namespace content
