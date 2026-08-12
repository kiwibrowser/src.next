// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_COMMAND_UPDATER_DELEGATE_H_
#define CHROME_BROWSER_COMMAND_UPDATER_DELEGATE_H_

#include "base/time/time.h"
#include "ui/base/window_open_disposition.h"

// Implement this interface so that your object can execute commands when
// needed.
class CommandUpdaterDelegate {
 public:
  // Performs the action associated with the command with the specified ID and
  // using the given disposition and timestamp.
  virtual void HandleCommandWithDisposition(
      int id,
      WindowOpenDisposition disposition,
      base::TimeTicks time_stamp = base::TimeTicks::Now()) = 0;

 protected:
  virtual ~CommandUpdaterDelegate() = default;
};

#endif  // CHROME_BROWSER_COMMAND_UPDATER_DELEGATE_H_
