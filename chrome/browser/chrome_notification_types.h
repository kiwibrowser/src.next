// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROME_NOTIFICATION_TYPES_H_
#define CHROME_BROWSER_CHROME_NOTIFICATION_TYPES_H_

#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "extensions/buildflags/buildflags.h"

#if BUILDFLAG(ENABLE_EXTENSIONS)
#include "extensions/browser/notification_types.h"
#else
#include "content/public/browser/notification_types.h"
#endif

#if BUILDFLAG(ENABLE_EXTENSIONS)
#define PREVIOUS_END extensions::NOTIFICATION_EXTENSIONS_END
#else
#define PREVIOUS_END content::NOTIFICATION_CONTENT_END
#endif

// **
// ** NOTICE
// **
// ** The notification system is deprecated, obsolete, and is slowly being
// ** removed. See https://crbug.com/268984.
// **
// ** Please don't add any new notification types, and please help migrate
// ** existing uses of the notification types below to use the Observer and
// ** Callback patterns.
// **

namespace chrome {

enum NotificationType {
  NOTIFICATION_CHROME_START = PREVIOUS_END,

  // Application-wide ----------------------------------------------------------

  // This message is sent when the application is terminating (the last
  // browser window has shutdown as part of an explicit user-initiated exit,
  // or the user closed the last browser window on Windows/Linux and there are
  // no BackgroundContents keeping the browser running). No source or details
  // are passed.
  // TODO(https://crbug.com/1174781): Remove.
  NOTIFICATION_APP_TERMINATING = NOTIFICATION_CHROME_START,

  // Authentication ----------------------------------------------------------

  // This is sent when a login prompt is shown.  The source is the
  // Source<NavigationController> for the tab in which the prompt is shown.
  // Details are a LoginNotificationDetails which provide the LoginHandler
  // that should be given authentication.
  // TODO(https://crbug.com/1174785): Remove.
  NOTIFICATION_AUTH_NEEDED,

  // This is sent when authentication credentials have been supplied (either
  // by the user or by an automation service), but before we've actually
  // received another response from the server.  The source is the
  // Source<NavigationController> for the tab in which the prompt was shown.
  // Details are an AuthSuppliedLoginNotificationDetails which provide the
  // LoginHandler that should be given authentication as well as the supplied
  // username and password.
  // TODO(https://crbug.com/1174785): Remove.
  NOTIFICATION_AUTH_SUPPLIED,

  // This is sent when an authentication request has been dismissed without
  // supplying credentials (either by the user or by an automation service).
  // The source is the Source<NavigationController> for the tab in which the
  // prompt was shown. Details are a LoginNotificationDetails which provide
  // the LoginHandler that should be cancelled.
  // TODO(https://crbug.com/1174785): Remove.
  NOTIFICATION_AUTH_CANCELLED,

  // Profiles -----------------------------------------------------------------

  // Use ProfileManagerObserver::OnProfileAdded instead of this notification.
  // Sent after a Profile has been added to ProfileManager.
  // The details are none and the source is the new profile.
  // Note: this notification is only sent for profiles owned by the
  // `ProfileManager`. In particular, off-the-record profiles don't trigger this
  // notification, but on-the-record System and Guest profiles do.
  //  TODO(https://crbug.com/1174720): Remove. See also
  //  https://crbug.com/1038437.
  NOTIFICATION_PROFILE_ADDED,

  // Misc --------------------------------------------------------------------
  // Note:-
  // Currently only Content and Chrome define and use notifications.
  // Custom notifications not belonging to Content and Chrome should start
  // from here.
  NOTIFICATION_CHROME_END,
};

}  // namespace chrome

// **
// ** NOTICE
// **
// ** The notification system is deprecated, obsolete, and is slowly being
// ** removed. See https://crbug.com/268984.
// **
// ** Please don't add any new notification types, and please help migrate
// ** existing uses of the notification types below to use the Observer and
// ** Callback patterns.
// **

#endif  // CHROME_BROWSER_CHROME_NOTIFICATION_TYPES_H_
