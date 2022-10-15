// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_CHROMEOS_BROWSER_CONTEXT_KEYED_SERVICE_FACTORIES_H_
#define CHROME_BROWSER_EXTENSIONS_CHROMEOS_BROWSER_CONTEXT_KEYED_SERVICE_FACTORIES_H_

namespace chromeos_extensions {

// Ensures the existence of any ChromeOS-specific (Ash + Lacros)
// BrowserContextKeyedServiceFactory provided by the Chrome extensions code.
// TODO(crbug.com/1340540): Find an appropriate place for this file.
void EnsureBrowserContextKeyedServiceFactoriesBuilt();

}  // namespace chromeos_extensions

#endif  // CHROME_BROWSER_EXTENSIONS_CHROMEOS_BROWSER_CONTEXT_KEYED_SERVICE_FACTORIES_H_
