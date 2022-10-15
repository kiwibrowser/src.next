// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base;

import androidx.annotation.UiThread;

import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.JNINamespace;
import org.chromium.base.annotations.NativeMethods;
import org.chromium.build.annotations.MainDex;

/**
 * This UncaughtExceptionHandler will create a breakpad minidump when there is an uncaught
 * exception.
 *
 * The exception's stack trace will be added to the minidump's data. This allows java-only crashes
 * to be reported in the same way as other native crashes.
 */
@JNINamespace("base::android")
@MainDex
public class JavaExceptionReporter implements Thread.UncaughtExceptionHandler {
    private final Thread.UncaughtExceptionHandler mParent;
    private final boolean mCrashAfterReport;
    private boolean mHandlingException;

    private JavaExceptionReporter(
            Thread.UncaughtExceptionHandler parent, boolean crashAfterReport) {
        mParent = parent;
        mCrashAfterReport = crashAfterReport;
    }

    @Override
    public void uncaughtException(Thread t, Throwable e) {
        if (!mHandlingException) {
            mHandlingException = true;
            JavaExceptionReporterJni.get().reportJavaException(mCrashAfterReport, e);
        }
        if (mParent != null) {
            mParent.uncaughtException(t, e);
        }
    }

    /**
     * Report and upload the stack trace as if it was a crash. This is very expensive and should
     * be called rarely and only on the UI thread to avoid corrupting other crash uploads. Ideally
     * only called in idle handlers.
     *
     * @param stackTrace The stack trace to report.
     */
    @UiThread
    public static void reportStackTrace(String stackTrace) {
        assert ThreadUtils.runningOnUiThread();
        JavaExceptionReporterJni.get().reportJavaStackTrace(
                PiiElider.sanitizeStacktrace(stackTrace));
    }

    /**
     * Report and upload the stack trace as if it was a crash. This is very expensive and should
     * be called rarely and only on the UI thread to avoid corrupting other crash uploads. Ideally
     * only called in idle handlers.
     *
     * @param exception The exception to report.
     */
    @UiThread
    public static void reportException(Throwable exception) {
        assert ThreadUtils.runningOnUiThread();
        JavaExceptionReporterJni.get().reportJavaException(false, exception);
    }

    @CalledByNative
    private static void installHandler(boolean crashAfterReport) {
        Thread.setDefaultUncaughtExceptionHandler(new JavaExceptionReporter(
                Thread.getDefaultUncaughtExceptionHandler(), crashAfterReport));
    }

    @NativeMethods
    interface Natives {
        void reportJavaException(boolean crashAfterReport, Throwable e);
        void reportJavaStackTrace(String stackTrace);
    }
}
