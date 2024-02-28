// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base;

import org.jni_zero.CalledByNative;

import java.util.Map;

/** This class provides JNI-related methods to the native library. */
public class JNIUtils {
    private static final String TAG = "JNIUtils";
    private static ClassLoader sJniClassLoader;

    /**
     * Returns a ClassLoader which can load Java classes from the specified split.
     *
     * @param splitName Name of the split, or empty string for the base split.
     */
    @CalledByNative
    private static ClassLoader getSplitClassLoader(String splitName) {
        if (!splitName.isEmpty()) {
            boolean isInstalled = BundleUtils.isIsolatedSplitInstalled(splitName);
            Log.i(TAG, "Init JNI Classloader for %s. isInstalled=%b", splitName, isInstalled);

            if (isInstalled) {
                return BundleUtils.getOrCreateSplitClassLoader(splitName);
            } else {
                // Split was installed by PlayCore in "compat" mode, meaning that our base module's
                // ClassLoader was patched to add the splits' dex file to it.
                // This should never happen on Android T+, where PlayCore is configured to fully
                // install splits from the get-go, but can still sometimes happen if play store
                // is very out of date.
            }
        }
        return sJniClassLoader != null ? sJniClassLoader : JNIUtils.class.getClassLoader();
    }

    /**
     * Sets the ClassLoader to be used for loading Java classes from native.
     *
     * @param classLoader the ClassLoader to use.
     */
    public static void setClassLoader(ClassLoader classLoader) {
        sJniClassLoader = classLoader;
    }

    /** Helper to convert from java maps to two arrays for JNI. */
    public static <K, V> void splitMap(Map<K, V> map, K[] outKeys, V[] outValues) {
        assert map.size() == outKeys.length;
        assert outValues.length == outKeys.length;

        int i = 0;
        for (Map.Entry<K, V> entry : map.entrySet()) {
            outKeys[i] = entry.getKey();
            outValues[i] = entry.getValue();
            i++;
        }
    }
}
