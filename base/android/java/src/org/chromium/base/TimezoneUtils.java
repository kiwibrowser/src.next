// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base;

import android.os.StrictMode;

import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.JNINamespace;
import org.chromium.build.annotations.MainDex;

import java.util.TimeZone;

@JNINamespace("base::android")
@MainDex
class TimezoneUtils {
    /**
     * Guards this class from being instantiated.
     */

    private TimezoneUtils() {}

    /**
     * @return the Olson timezone ID of the current system time zone.
     */
    @CalledByNative
    private static String getDefaultTimeZoneId() {
        // On Android N or earlier, getting the default timezone requires the disk
        // access when a device set up is skipped.
        StrictMode.ThreadPolicy oldPolicy = StrictMode.allowThreadDiskReads();
        String timezoneID = TimeZone.getDefault().getID();
        StrictMode.setThreadPolicy(oldPolicy);
        return timezoneID;
    }
}
