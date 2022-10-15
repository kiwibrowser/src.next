// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base;

import android.app.Activity;
import android.content.res.Resources.NotFoundException;
import android.os.Looper;
import android.os.MessageQueue;
import android.util.Log;
import android.util.Printer;
import android.view.View;
import android.view.ViewGroup;

import androidx.annotation.AnyThread;
import androidx.annotation.Nullable;
import androidx.annotation.UiThread;
import androidx.annotation.VisibleForTesting;

import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.JNINamespace;
import org.chromium.base.annotations.NativeMethods;
import org.chromium.base.task.PostTask;
import org.chromium.base.task.TaskTraits;
import org.chromium.build.annotations.MainDex;

import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Java mirror of Chrome trace event API. See base/trace_event/trace_event.h.
 *
 * To get scoped trace events, use the "try with resource" construct, for instance:
 * <pre>{@code
 * try (TraceEvent e = TraceEvent.scoped("MyTraceEvent")) {
 *   // code.
 * }
 * }</pre>
 *
 * The event name of the trace events must be a string literal or a |static final String| class
 * member. Otherwise NoDynamicStringsInTraceEventCheck error will be thrown.
 *
 * It is OK to use tracing before the native library has loaded, in a slightly restricted fashion.
 * @see EarlyTraceEvent for details.
 */
@JNINamespace("base::android")
@MainDex
public class TraceEvent implements AutoCloseable {
    private static volatile boolean sEnabled; // True when tracing into Chrome's tracing service.
    private static AtomicBoolean sNativeTracingReady = new AtomicBoolean();
    private static AtomicBoolean sUiThreadReady = new AtomicBoolean();
    private static boolean sEventNameFilteringEnabled;

    // Trace tags replicated from android.os.Trace.
    public static final long ATRACE_TAG_WEBVIEW = 1L << 4;
    public static final long ATRACE_TAG_APP = 1L << 12;

    /**
     * Watches for active ATrace sessions and accordingly enables or disables
     * tracing in Chrome/WebView.
     */
    private static class ATrace implements MessageQueue.IdleHandler {
        private static final String TAG = "ATrace";

        private Class<?> mTraceClass;
        private Method mIsTraceTagEnabledMethod;
        private Method mTraceBeginMethod;
        private Method mTraceEndMethod;
        private Method mAsyncTraceBeginMethod;
        private Method mAsyncTraceEndMethod;
        private Class<?> mSystemPropertiesClass;
        private Method mGetSystemPropertyMethod;

        private final AtomicBoolean mNativeTracingReady = new AtomicBoolean();
        private final AtomicBoolean mUiThreadReady = new AtomicBoolean();
        private final AtomicBoolean mTraceTagActive = new AtomicBoolean();
        private final long mTraceTag;
        private boolean mShouldWriteToSystemTrace;
        private boolean mIdleHandlerRegistered;

        private static class CategoryConfig {
            public String filter = "";
            public boolean shouldWriteToATrace = true;
        }

        public ATrace(long traceTag) {
            // Look up hidden ATrace APIs.
            try {
                mTraceClass = Class.forName("android.os.Trace");
                mIsTraceTagEnabledMethod = mTraceClass.getMethod("isTagEnabled", long.class);
                mTraceBeginMethod = mTraceClass.getMethod("traceBegin", long.class, String.class);
                mTraceEndMethod = mTraceClass.getMethod("traceEnd", long.class);
                mAsyncTraceBeginMethod = mTraceClass.getMethod(
                        "asyncTraceBegin", long.class, String.class, int.class);
                mAsyncTraceEndMethod =
                        mTraceClass.getMethod("asyncTraceEnd", long.class, String.class, int.class);
                mSystemPropertiesClass = Class.forName("android.os.SystemProperties");
                mGetSystemPropertyMethod = mSystemPropertiesClass.getMethod("get", String.class);
            } catch (Exception e) {
                // If we hit reflection errors, just disable atrace support.
                org.chromium.base.Log.w(TAG, "Reflection error", e);
                mIsTraceTagEnabledMethod = null;
            }
            // If there's an active atrace session, also start collecting early trace events.
            mTraceTag = traceTag;
            pollConfig();
        }

        /**
         * Reads a system property and returns its string value.
         *
         * @param name the name of the system property
         * @return the result string or null if an exception occurred
         */
        @Nullable
        private String getSystemProperty(String name) {
            try {
                return (String) mGetSystemPropertyMethod.invoke(mSystemPropertiesClass, name);
            } catch (Exception e) {
                return null;
            }
        }

        /**
         * Reads a system property and returns its value as an integer.
         *
         * @param name the name of the system property
         * @return the result integer or null if an exception occurred
         */
        private Integer getIntegerSystemProperty(String name) {
            String property = getSystemProperty(name);
            if (property == null) return null;
            try {
                return Integer.decode(property);
            } catch (NumberFormatException e) {
                return null;
            }
        }

        private boolean isTraceTagEnabled(long traceTag) {
            try {
                return (boolean) mIsTraceTagEnabledMethod.invoke(mTraceClass, traceTag);
            } catch (Exception e) {
                return false;
            }
        }

        /**
         * @return true if Chrome/WebView is part of an active ATrace session.
         */
        public boolean hasActiveSession() {
            return mTraceTagActive.get();
        }

        /**
         *  Checks whether ATrace has started or stopped tracing since the last
         *  call to this function and parses the changed config if necessary.
         *
         *  @return true if a session has started or stopped.
         */
        @UiThread
        private boolean pollConfig() {
            // ATrace's tracing configuration consists of the following system
            // properties:
            // - debug.atrace.tags.enableflags: A hex mask of the enabled system
            //                                  tracing categories (e.g, "0x10").
            // - debug.atrace.app_number:       The number of per-app config entries
            //                                  (e.g., "1").
            // - debug.atrace.app_0:            Config for app 0 (up to
            //                                  app_number-1).
            //
            // Normally the per-app config entry is just the package name, but we
            // also support setting the trace config with additional parameters,
            // e.g., assuming "com.android.chrome" as the package name:
            //
            // - Enable default categories:   "com.android.chrome"
            // - Enable specific categories:  "com.android.chrome/cat1:cat2"
            // - Disable specific categories: "com.android.chrome/*:-cat1"
            //
            // Since each app-specific config is limited to 91 characters, multiple
            // entries can be used to work around the limit.
            //
            // If either the "webview" trace tag (0x10) is enabled (for WebView)
            // or our package name is found in the list of configs, trace events
            // will be written into ATrace. However, if "-atrace" appears as a
            // category in any of the app-specific configs, events will only be
            // written into Chrome's own startup tracing buffer to avoid
            // duplicate events.
            boolean traceTagWasActive = mTraceTagActive.get();
            boolean traceTagIsActive = isTraceTagEnabled(mTraceTag);
            if (traceTagWasActive == traceTagIsActive) return false;
            mTraceTagActive.set(traceTagIsActive);

            if (!traceTagIsActive) {
                // A previously active atrace session ended.
                EarlyTraceEvent.disable();
                disableNativeATrace();
                mShouldWriteToSystemTrace = false;
                ThreadUtils.getUiThreadLooper().setMessageLogging(null);
                return true;
            }
            CategoryConfig config = getCategoryConfigFromATrace();

            // There is an active atrace session. We can output events into one
            // of the following sinks:
            //
            // - To ATrace:
            //    ...via TraceLog if native has finished loading.
            //    ...via android.os.Trace otherwise.
            // - To Chrome's own tracing service (for startup tracing):
            //    ...via TraceLog if native has finished loading.
            //    ...via EarlyTraceEvent otherwise.
            mShouldWriteToSystemTrace = false;
            if (mNativeTracingReady.get()) {
                // Native is loaded; start writing to atrace via TraceLog, or in
                // the case of a Chrome-only trace, setup a startup tracing
                // session.
                if (config.shouldWriteToATrace) {
                    enableNativeATrace(config.filter);
                } else {
                    setupATraceStartupTrace(config.filter);
                }
            } else {
                // Native isn't there yet; fall back to android.os.Trace or
                // EarlyTraceEvent. We can't use the category filter in this
                // case because Java events don't have categories.
                if (config.shouldWriteToATrace) {
                    mShouldWriteToSystemTrace = true;
                } else {
                    EarlyTraceEvent.enable();
                }
            }

            // For Chrome-only traces, also capture Looper messages. In other
            // cases, they are logged by the system.
            if (!config.shouldWriteToATrace) {
                ThreadUtils.getUiThreadLooper().setMessageLogging(LooperMonitorHolder.sInstance);
            }
            return true;
        }

        private CategoryConfig getCategoryConfigFromATrace() {
            CategoryConfig config = new CategoryConfig();
            boolean shouldWriteToATrace = true;
            Integer appCount = getIntegerSystemProperty("debug.atrace.app_number");
            // In the case of WebView, the application context may not have been
            // attached yet. Ignore per-app category settings in that case; they
            // will be applied when the native library finishes loading.
            if (appCount != null && appCount > 0 && ContextUtils.getApplicationContext() != null) {
                // Look for tracing category settings meant for this activity.
                // For Chrome this is the package name of the browser, while for
                // WebView this is the package name of the hosting application
                // (e.g., GMail).
                String packageName = ContextUtils.getApplicationContext().getPackageName();
                for (int i = 0; i < appCount; i++) {
                    String appConfig = getSystemProperty("debug.atrace.app_" + i);
                    if (appConfig == null || !appConfig.startsWith(packageName)) continue;
                    String extra = appConfig.substring(packageName.length());
                    if (!extra.startsWith("/")) continue;
                    for (String category : extra.substring(1).split(":")) {
                        if (category.equals("-atrace")) {
                            config.shouldWriteToATrace = false;
                            continue;
                        }
                        if (config.filter.length() > 0) config.filter += ",";
                        config.filter += category;
                    }
                }
            }
            return config;
        }

        @AnyThread
        public void onNativeTracingReady() {
            mNativeTracingReady.set(true);

            // If there already was an active atrace session, we should transfer
            // it over to native. If the UI thread was already registered, post
            // a task to move the session over as soon as possible. Otherwise
            // we'll wait until the UI thread activates.
            mTraceTagActive.set(false);
            if (mUiThreadReady.get()) {
                ThreadUtils.postOnUiThread(() -> { pollConfig(); });
            }
        }

        @AnyThread
        public void onUiThreadReady() {
            mUiThreadReady.set(true);
            if (!ThreadUtils.runningOnUiThread()) {
                ThreadUtils.postOnUiThread(() -> { startPolling(); });
                return;
            }
            startPolling();
        }

        private void startPolling() {
            ThreadUtils.assertOnUiThread();
            // Since Android R there's no way for an app to be notified of
            // atrace activations. To work around this, we poll for the latest
            // state whenever the main run loop becomes idle. Since the check
            // amounts to one JNI call, the overhead of doing this is
            // negligible. See queueIdle().
            if (!mIdleHandlerRegistered) {
                Looper.myQueue().addIdleHandler(this);
                mIdleHandlerRegistered = true;
            }
            pollConfig();
        }

        @Override
        public final boolean queueIdle() {
            pollConfig();
            return true;
        }

        /**
         *  Instructs Chrome's tracing service to start tracing.
         *
         *  @param categoryFilter Set of trace categories to enable.
         */
        private void enableNativeATrace(String categoryFilter) {
            assert mNativeTracingReady.get();
            TraceEventJni.get().startATrace(categoryFilter);
        }

        /**
         *  Stop a previously started tracing session and flush remaining events
         *  to ATrace (if enabled).
         */
        private void disableNativeATrace() {
            assert mNativeTracingReady.get();
            TraceEventJni.get().stopATrace();
        }

        /**
         *  Begins a startup tracing session which will be later taken over by a
         *  system tracing session.
         *
         *  @param categoryFilter Set of trace categories to enable.
         */
        private void setupATraceStartupTrace(String categoryFilter) {
            assert mNativeTracingReady.get();
            TraceEventJni.get().setupATraceStartupTrace(categoryFilter);
        }

        public void traceBegin(String name) {
            if (!mShouldWriteToSystemTrace) return;
            try {
                mTraceBeginMethod.invoke(mTraceClass, mTraceTag, name);
            } catch (Exception e) {
                // No-op.
            }
        }

        public void traceEnd() {
            if (!mShouldWriteToSystemTrace) return;
            try {
                mTraceEndMethod.invoke(mTraceClass, mTraceTag);
            } catch (Exception e) {
                // No-op.
            }
        }

        public void asyncTraceBegin(String name, int cookie) {
            if (!mShouldWriteToSystemTrace) return;
            try {
                mAsyncTraceBeginMethod.invoke(mTraceClass, mTraceTag, name, cookie);
            } catch (Exception e) {
                // No-op.
            }
        }

        public void asyncTraceEnd(String name, int cookie) {
            if (!mShouldWriteToSystemTrace) return;
            try {
                mAsyncTraceEndMethod.invoke(mTraceClass, mTraceTag, name, cookie);
            } catch (Exception e) {
                // No-op.
            }
        }
    }

    private static ATrace sATrace;

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    static class BasicLooperMonitor implements Printer {
        @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
        static final String LOOPER_TASK_PREFIX = "Looper.dispatch: ";
        @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
        static final String FILTERED_EVENT_NAME = LOOPER_TASK_PREFIX + "EVENT_NAME_FILTERED";
        private static final int SHORTEST_LOG_PREFIX_LENGTH = "<<<<< Finished to ".length();
        private String mCurrentTarget;

        @Override
        public void println(final String line) {
            if (line.startsWith(">")) {
                beginHandling(line);
            } else {
                assert line.startsWith("<");
                endHandling(line);
            }
        }

        void beginHandling(final String line) {
            // May return an out-of-date value. this is not an issue as EarlyTraceEvent#begin()
            // will filter the event in this case.
            boolean earlyTracingActive = EarlyTraceEvent.enabled();
            if (sEnabled || earlyTracingActive) {
                // Note that we don't need to log ATrace events here because the
                // framework does that for us (M+).
                mCurrentTarget = getTraceEventName(line);
                if (sEnabled) {
                    TraceEventJni.get().beginToplevel(mCurrentTarget);
                } else {
                    EarlyTraceEvent.begin(mCurrentTarget, true /*isToplevel*/);
                }
            }
        }

        void endHandling(final String line) {
            boolean earlyTracingActive = EarlyTraceEvent.enabled();
            if ((sEnabled || earlyTracingActive) && mCurrentTarget != null) {
                if (sEnabled) {
                    TraceEventJni.get().endToplevel(mCurrentTarget);
                } else {
                    EarlyTraceEvent.end(mCurrentTarget, true /*isToplevel*/);
                }
            }
            mCurrentTarget = null;
        }

        @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
        static String getTraceEventName(String line) {
            if (sEventNameFilteringEnabled) {
                return FILTERED_EVENT_NAME;
            }
            return LOOPER_TASK_PREFIX + getTarget(line) + "(" + getTargetName(line) + ")";
        }

        /**
         * Android Looper formats |logLine| as
         *
         * ">>>>> Dispatching to (TARGET) {HASH_CODE} TARGET_NAME: WHAT"
         *
         * and
         *
         * "<<<<< Finished to (TARGET) {HASH_CODE} TARGET_NAME".
         *
         * This has been the case since at least 2009 (Donut). This function extracts the
         * TARGET part of the message.
         */
        private static String getTarget(String logLine) {
            int start = logLine.indexOf('(', SHORTEST_LOG_PREFIX_LENGTH);
            int end = start == -1 ? -1 : logLine.indexOf(')', start);
            return end != -1 ? logLine.substring(start + 1, end) : "";
        }

        // Extracts the TARGET_NAME part of the log message (see above).
        private static String getTargetName(String logLine) {
            int start = logLine.indexOf('}', SHORTEST_LOG_PREFIX_LENGTH);
            int end = start == -1 ? -1 : logLine.indexOf(':', start);
            if (end == -1) {
                end = logLine.length();
            }
            return start != -1 ? logLine.substring(start + 2, end) : "";
        }
    }

    /**
     * A class that records, traces and logs statistics about the UI thead's Looper.
     * The output of this class can be used in a number of interesting ways:
     * <p>
     * <ol><li>
     * When using chrometrace, there will be a near-continuous line of
     * measurements showing both event dispatches as well as idles;
     * </li><li>
     * Logging messages are output for events that run too long on the
     * event dispatcher, making it easy to identify problematic areas;
     * </li><li>
     * Statistics are output whenever there is an idle after a non-trivial
     * amount of activity, allowing information to be gathered about task
     * density and execution cadence on the Looper;
     * </li></ol>
     * <p>
     * The class attaches itself as an idle handler to the main Looper, and
     * monitors the execution of events and idle notifications. Task counters
     * accumulate between idle notifications and get reset when a new idle
     * notification is received.
     */
    private static final class IdleTracingLooperMonitor extends BasicLooperMonitor
            implements MessageQueue.IdleHandler {
        // Tags for dumping to logcat or TraceEvent
        private static final String TAG = "TraceEvent_LooperMonitor";
        private static final String IDLE_EVENT_NAME = "Looper.queueIdle";

        // Calculation constants
        private static final long FRAME_DURATION_MILLIS = 1000L / 60L; // 60 FPS
        // A reasonable threshold for defining a Looper event as "long running"
        private static final long MIN_INTERESTING_DURATION_MILLIS =
                FRAME_DURATION_MILLIS;
        // A reasonable threshold for a "burst" of tasks on the Looper
        private static final long MIN_INTERESTING_BURST_DURATION_MILLIS =
                MIN_INTERESTING_DURATION_MILLIS * 3;

        // Stats tracking
        private long mLastIdleStartedAt;
        private long mLastWorkStartedAt;
        private int mNumTasksSeen;
        private int mNumIdlesSeen;
        private int mNumTasksSinceLastIdle;

        // State
        private boolean mIdleMonitorAttached;

        // Called from within the begin/end methods only.
        // This method can only execute on the looper thread, because that is
        // the only thread that is permitted to call Looper.myqueue().
        private final void syncIdleMonitoring() {
            if (sEnabled && !mIdleMonitorAttached) {
                // approximate start time for computational purposes
                mLastIdleStartedAt = TimeUtils.elapsedRealtimeMillis();
                Looper.myQueue().addIdleHandler(this);
                mIdleMonitorAttached = true;
                Log.v(TAG, "attached idle handler");
            } else if (mIdleMonitorAttached && !sEnabled) {
                Looper.myQueue().removeIdleHandler(this);
                mIdleMonitorAttached = false;
                Log.v(TAG, "detached idle handler");
            }
        }

        @Override
        final void beginHandling(final String line) {
            // Close-out any prior 'idle' period before starting new task.
            if (mNumTasksSinceLastIdle == 0) {
                TraceEvent.end(IDLE_EVENT_NAME);
            }
            mLastWorkStartedAt = TimeUtils.elapsedRealtimeMillis();
            syncIdleMonitoring();
            super.beginHandling(line);
        }

        @Override
        final void endHandling(final String line) {
            final long elapsed = TimeUtils.elapsedRealtimeMillis() - mLastWorkStartedAt;
            if (elapsed > MIN_INTERESTING_DURATION_MILLIS) {
                traceAndLog(Log.WARN, "observed a task that took "
                        + elapsed + "ms: " + line);
            }
            super.endHandling(line);
            syncIdleMonitoring();
            mNumTasksSeen++;
            mNumTasksSinceLastIdle++;
        }

        private static void traceAndLog(int level, String message) {
            TraceEvent.instant("TraceEvent.LooperMonitor:IdleStats", message);
            Log.println(level, TAG, message);
        }

        @Override
        public final boolean queueIdle() {
            final long now = TimeUtils.elapsedRealtimeMillis();
            if (mLastIdleStartedAt == 0) mLastIdleStartedAt = now;
            final long elapsed = now - mLastIdleStartedAt;
            mNumIdlesSeen++;
            TraceEvent.begin(IDLE_EVENT_NAME, mNumTasksSinceLastIdle + " tasks since last idle.");
            if (elapsed > MIN_INTERESTING_BURST_DURATION_MILLIS) {
                // Dump stats
                String statsString = mNumTasksSeen + " tasks and "
                        + mNumIdlesSeen + " idles processed so far, "
                        + mNumTasksSinceLastIdle + " tasks bursted and "
                        + elapsed + "ms elapsed since last idle";
                traceAndLog(Log.DEBUG, statsString);
            }
            mLastIdleStartedAt = now;
            mNumTasksSinceLastIdle = 0;
            return true; // stay installed
        }
    }

    // Holder for monitor avoids unnecessary construction on non-debug runs
    private static final class LooperMonitorHolder {
        private static final BasicLooperMonitor sInstance =
                CommandLine.getInstance().hasSwitch(BaseSwitches.ENABLE_IDLE_TRACING)
                ? new IdleTracingLooperMonitor() : new BasicLooperMonitor();
    }

    private final String mName;

    /**
     * Constructor used to support the "try with resource" construct.
     */
    private TraceEvent(String name, String arg) {
        mName = name;
        begin(name, arg);
    }

    @Override
    public void close() {
        end(mName);
    }

    /**
     * Factory used to support the "try with resource" construct.
     *
     * Note that if tracing is not enabled, this will not result in allocating an object.
     *
     * @param name Trace event name.
     * @param arg The arguments of the event.
     * @return a TraceEvent, or null if tracing is not enabled.
     */
    public static TraceEvent scoped(String name, String arg) {
        if (!(EarlyTraceEvent.enabled() || enabled())) return null;
        return new TraceEvent(name, arg);
    }

    /**
     * Similar to {@link #scoped(String, String arg)}, but uses null for |arg|.
     */
    public static TraceEvent scoped(String name) {
        return scoped(name, null);
    }

    /**
     * Notification from native that tracing is enabled/disabled.
     */
    @CalledByNative
    public static void setEnabled(boolean enabled) {
        if (enabled) EarlyTraceEvent.disable();
        // Only disable logging if Chromium enabled it originally, so as to not disrupt logging done
        // by other applications
        if (sEnabled != enabled) {
            sEnabled = enabled;
            // Android M+ systrace logs this on its own. Only log it if not writing to Android
            // systrace.
            if (sATrace == null || !sATrace.hasActiveSession()) {
                ThreadUtils.getUiThreadLooper().setMessageLogging(
                        enabled ? LooperMonitorHolder.sInstance : null);
            }
        }
        if (sUiThreadReady.get()) {
            ViewHierarchyDumper.updateEnabledState();
        }
    }

    @CalledByNative
    public static void setEventNameFilteringEnabled(boolean enabled) {
        sEventNameFilteringEnabled = enabled;
    }

    public static boolean eventNameFilteringEnabled() {
        return sEventNameFilteringEnabled;
    }

    /**
     * May enable early tracing depending on the environment.
     *
     * @param traceTag If non-zero, start watching for ATrace sessions on the given tag.
     * @param readCommandLine If true, also check command line flags to see
     *                        whether tracing should be turned on.
     */
    public static void maybeEnableEarlyTracing(long traceTag, boolean readCommandLine) {
        // Enable early trace events based on command line flags. This is only
        // done for Chrome since WebView tracing isn't controlled with command
        // line flags.
        if (readCommandLine) {
            EarlyTraceEvent.maybeEnableInBrowserProcess();
        }
        if (traceTag != 0) {
            sATrace = new ATrace(traceTag);
            if (sNativeTracingReady.get()) {
                sATrace.onNativeTracingReady();
            }
            if (sUiThreadReady.get()) {
                sATrace.onUiThreadReady();
            }
        }
        if (EarlyTraceEvent.enabled() && (sATrace == null || !sATrace.hasActiveSession())) {
            ThreadUtils.getUiThreadLooper().setMessageLogging(LooperMonitorHolder.sInstance);
        }
    }

    public static void onNativeTracingReady() {
        // Register an enabled observer, such that java traces are always
        // enabled with native.
        sNativeTracingReady.set(true);
        TraceEventJni.get().registerEnabledObserver();
        if (sATrace != null) {
            sATrace.onNativeTracingReady();
        }
    }

    // Called by ThreadUtils.
    static void onUiThreadReady() {
        sUiThreadReady.set(true);
        if (sATrace != null) {
            sATrace.onUiThreadReady();
        }
        if (sEnabled) {
            ViewHierarchyDumper.updateEnabledState();
        }
    }

    /**
     * @return True if tracing is enabled, false otherwise.
     * It is safe to call trace methods without checking if TraceEvent
     * is enabled.
     */
    public static boolean enabled() {
        return sEnabled;
    }

    /**
     * Triggers the 'instant' native trace event with no arguments.
     * @param name The name of the event.
     */
    public static void instant(String name) {
        if (sEnabled) TraceEventJni.get().instant(name, null);
    }

    /**
     * Triggers the 'instant' native trace event.
     * @param name The name of the event.
     * @param arg  The arguments of the event.
     */
    public static void instant(String name, String arg) {
        if (sEnabled) TraceEventJni.get().instant(name, arg);
    }

    /**
     * Triggers a 'instant' native "AndroidIPC" event.
     * @param name The name of the IPC.
     * @param durMs The duration the IPC took in milliseconds.
     */
    public static void instantAndroidIPC(String name, long durMs) {
        if (sEnabled) TraceEventJni.get().instantAndroidIPC(name, durMs);
    }

    /**
     * Triggers a 'instant' native "AndroidToolbar" event.
     * @param blockReason the enum TopToolbarBlockCapture (-1 if not blocked).
     * @param allowReason the enum TopToolbarAllowCapture (-1 if not allowed).
     * @param snapshotDiff the enum ToolbarSnapshotDifference (-1 if no diff).
     */
    public static void instantAndroidToolbar(int blockReason, int allowReason, int snapshotDiff) {
        if (sEnabled) {
            TraceEventJni.get().instantAndroidToolbar(blockReason, allowReason, snapshotDiff);
        }
    }

    /**
     * Snapshots the view hierarchy state on the main thread and then finishes emitting a trace
     * event on the threadpool.
     */
    public static void snapshotViewHierarchy() {
        if (sEnabled && TraceEventJni.get().viewHierarchyDumpEnabled()) {
            // Emit separate begin and end so we can set the flow id at the end.
            TraceEvent.begin("instantAndroidViewHierarchy");

            // If we have no views don't bother to emit any TraceEvents for efficiency.
            ArrayList<ActivityInfo> views = snapshotViewHierarchyState();
            if (views.isEmpty()) {
                TraceEvent.end("instantAndroidViewHierarchy");
                return;
            }

            // Use the correct snapshot object as a processed scoped flow id. This connects the
            // mainthread work with the result emitted on the threadpool. We do this because
            // resolving resource names can trigger exceptions (NotFoundException) which can be
            // quite slow.
            long flow = views.hashCode();

            PostTask.postTask(TaskTraits.BEST_EFFORT, () -> {
                // Actually output the dump as a trace event on a thread pool.
                TraceEventJni.get().initViewHierarchyDump(flow, views);
            });
            TraceEvent.end("instantAndroidViewHierarchy", null, flow);
        }
    }

    /**
     * Triggers the 'start' native trace event with no arguments.
     * @param name The name of the event.
     * @param id   The id of the asynchronous event.
     */
    public static void startAsync(String name, long id) {
        EarlyTraceEvent.startAsync(name, id);
        if (sEnabled) {
            TraceEventJni.get().startAsync(name, id);
        } else if (sATrace != null) {
            sATrace.asyncTraceBegin(name, (int) id);
        }
    }

    /**
     * Triggers the 'finish' native trace event with no arguments.
     * @param name The name of the event.
     * @param id   The id of the asynchronous event.
     */
    public static void finishAsync(String name, long id) {
        EarlyTraceEvent.finishAsync(name, id);
        if (sEnabled) {
            TraceEventJni.get().finishAsync(name, id);
        } else if (sATrace != null) {
            sATrace.asyncTraceEnd(name, (int) id);
        }
    }

    /**
     * Triggers the 'begin' native trace event with no arguments.
     * @param name The name of the event.
     */
    public static void begin(String name) {
        begin(name, null);
    }

    /**
     * Triggers the 'begin' native trace event.
     * @param name The name of the event.
     * @param arg  The arguments of the event.
     */
    public static void begin(String name, String arg) {
        EarlyTraceEvent.begin(name, false /*isToplevel*/);
        if (sEnabled) {
            TraceEventJni.get().begin(name, arg);
        } else if (sATrace != null) {
            sATrace.traceBegin(name);
        }
    }

    /**
     * Triggers the 'end' native trace event with no arguments.
     * @param name The name of the event.
     */
    public static void end(String name) {
        end(name, null);
    }

    /**
     * Triggers the 'end' native trace event.
     * @param name The name of the event.
     * @param arg  The arguments of the event.
     */
    public static void end(String name, String arg) {
        end(name, arg, 0);
    }

    /**
     * Triggers the 'end' native trace event.
     * @param name The name of the event.
     * @param arg  The arguments of the event.
     * @param flow The flow ID to associate with this event (0 is treated as invalid).
     */
    public static void end(String name, String arg, long flow) {
        EarlyTraceEvent.end(name, false /*isToplevel*/);
        if (sEnabled) {
            TraceEventJni.get().end(name, arg, flow);
        } else if (sATrace != null) {
            sATrace.traceEnd();
        }
    }

    public static ArrayList<ActivityInfo> snapshotViewHierarchyState() {
        if (!ApplicationStatus.isInitialized()) {
            return new ArrayList<ActivityInfo>();
        }

        // In local testing we generally just have one activity.
        ArrayList<ActivityInfo> views = new ArrayList<>(2);
        for (Activity a : ApplicationStatus.getRunningActivities()) {
            views.add(new ActivityInfo(a.getClass().getName()));
            ViewHierarchyDumper.dumpView(views.get(views.size() - 1),
                    /*parentId=*/0, a.getWindow().getDecorView().getRootView());
        }
        return views;
    }

    @NativeMethods
    interface Natives {
        void registerEnabledObserver();
        void startATrace(String categoryFilter);
        void stopATrace();
        void setupATraceStartupTrace(String categoryFilter);
        void instant(String name, String arg);
        void begin(String name, String arg);
        void end(String name, String arg, long flow);
        void beginToplevel(String target);
        void endToplevel(String target);
        void startAsync(String name, long id);
        void finishAsync(String name, long id);
        boolean viewHierarchyDumpEnabled();
        void initViewHierarchyDump(long id, Object list);
        long startActivityDump(String name, long dumpProtoPtr);
        void addViewDump(int id, int parentId, boolean isShown, boolean isDirty, String className,
                String resourceName, long activityProtoPtr);
        void instantAndroidIPC(String name, long durMs);
        void instantAndroidToolbar(int blockReason, int allowReason, int snapshotDiff);
    }

    /**
     * A method to be called by native code that uses the ViewHierarchyDumper class to emit a trace
     * event with views of all running activities of the app.
     */
    @CalledByNative
    public static void dumpViewHierarchy(long dumpProtoPtr, Object list) {
        if (!ApplicationStatus.isInitialized()) {
            return;
        }

        // Convert the Object back into the ArrayList of ActivityInfo, lifetime of this object is
        // maintained by the Runnable that we are running in currently.
        ArrayList<ActivityInfo> activities = (ArrayList<ActivityInfo>) list;

        for (ActivityInfo activity : activities) {
            long activityProtoPtr =
                    TraceEventJni.get().startActivityDump(activity.mActivityName, dumpProtoPtr);
            for (ViewInfo view : activity.mViews) {
                // We need to resolve the resource, take care as NotFoundException can be common and
                // java exceptions aren't he fastest thing ever.
                String resource;
                try {
                    resource = view.mRes != null ? (view.mId == 0 || view.mId == -1
                                               ? "__no_id__"
                                               : view.mRes.getResourceName(view.mId))
                                                 : "__no_resources__";
                } catch (NotFoundException e) {
                    resource = "__name_not_found__";
                }
                TraceEventJni.get().addViewDump(view.mId, view.mParentId, view.mIsShown,
                        view.mIsDirty, view.mClassName, resource, activityProtoPtr);
            }
        }
    }

    /**
     * This class contains the minimum information to represent a view that the {@link
     * #ViewHierarchyDumper} needs, so that in {@link #snapshotViewHierarchy} we can output a trace
     * event off the main thread.
     */
    public static class ViewInfo {
        public ViewInfo(int id, int parentId, boolean isShown, boolean isDirty, String className,
                android.content.res.Resources res) {
            mId = id;
            mParentId = parentId;
            mIsShown = isShown;
            mIsDirty = isDirty;
            mClassName = className;
            mRes = res;
        }

        private int mId;
        private int mParentId;
        private boolean mIsShown;
        private boolean mIsDirty;
        private String mClassName;
        // One can use mRes to resolve mId to a resource name.
        private android.content.res.Resources mRes;
    }

    /**
     * This class contains the minimum information to represent an Activity that the {@link
     * #ViewHierarchyDumper} needs, so that in {@link #snapshotViewHierarchy} we can output a trace
     * event off the main thread.
     */
    public static class ActivityInfo {
        public ActivityInfo(String activityName) {
            mActivityName = activityName;
            // Local testing found about 115ish views in the ChromeTabbedActivity.
            mViews = new ArrayList<ViewInfo>(125);
        }

        public String mActivityName;
        public ArrayList<ViewInfo> mViews;
    }

    /**
     * A class that periodically dumps the view hierarchy of all running activities of the app to
     * the trace. Enabled/disabled via the disabled-by-default-android_view_hierarchy trace
     * category.
     *
     * The class registers itself as an idle handler, so that it can run when there are no other
     * tasks in the queue (but not more often than once a second). When the queue is idle,
     * it calls the initViewHierarchyDump() native function which in turn calls the
     * TraceEvent.dumpViewHierarchy() with a pointer to the proto buffer to fill in. The
     * TraceEvent.dumpViewHierarchy() traverses all activities and dumps view hierarchy for every
     * activity. Altogether, the call sequence is as follows:
     *   ViewHierarchyDumper.queueIdle()
     *    -> JNI#initViewHierarchyDump()
     *        -> TraceEvent.dumpViewHierarchy()
     *            -> JNI#startActivityDump()
     *            -> ViewHierarchyDumper.dumpView()
     *                -> JNI#addViewDump()
     */
    private static final class ViewHierarchyDumper implements MessageQueue.IdleHandler {
        private static final String EVENT_NAME = "TraceEvent.ViewHierarchyDumper";
        private static final long MIN_VIEW_DUMP_INTERVAL_MILLIS = 1000L;
        private static boolean sEnabled;
        private static ViewHierarchyDumper sInstance;
        private long mLastDumpTs;

        @Override
        public final boolean queueIdle() {
            final long now = TimeUtils.elapsedRealtimeMillis();
            if (mLastDumpTs == 0 || (now - mLastDumpTs) > MIN_VIEW_DUMP_INTERVAL_MILLIS) {
                mLastDumpTs = now;
                snapshotViewHierarchy();
            }

            // Returning true to keep IdleHandler alive.
            return true;
        }

        public static void updateEnabledState() {
            if (!ThreadUtils.runningOnUiThread()) {
                ThreadUtils.postOnUiThread(() -> { updateEnabledState(); });
                return;
            }

            if (TraceEventJni.get().viewHierarchyDumpEnabled()) {
                if (sInstance == null) {
                    sInstance = new ViewHierarchyDumper();
                }
                enable();
            } else {
                if (sInstance != null) {
                    disable();
                }
            }
        }

        private static void dumpView(ActivityInfo collection, int parentId, View v) {
            ThreadUtils.assertOnUiThread();
            int id = v.getId();
            collection.mViews.add(new ViewInfo(id, parentId, v.isShown(), v.isDirty(),
                    v.getClass().getSimpleName(), v.getResources()));

            if (v instanceof ViewGroup) {
                ViewGroup vg = (ViewGroup) v;
                for (int i = 0; i < vg.getChildCount(); i++) {
                    dumpView(collection, id, vg.getChildAt(i));
                }
            }
        }

        private static void enable() {
            ThreadUtils.assertOnUiThread();
            if (!sEnabled) {
                Looper.myQueue().addIdleHandler(sInstance);
                sEnabled = true;
            }
        }

        private static void disable() {
            ThreadUtils.assertOnUiThread();
            if (sEnabled) {
                Looper.myQueue().removeIdleHandler(sInstance);
                sEnabled = false;
            }
        }
    }
}
