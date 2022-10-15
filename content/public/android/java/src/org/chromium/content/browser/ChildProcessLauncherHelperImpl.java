// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.content.browser;

import android.content.Context;
import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;
import android.os.ParcelFileDescriptor;
import android.text.TextUtils;

import androidx.annotation.IntDef;
import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;

import org.chromium.base.ApplicationState;
import org.chromium.base.ApplicationStatus;
import org.chromium.base.Callback;
import org.chromium.base.ChildBindingState;
import org.chromium.base.CollectionUtil;
import org.chromium.base.ContextUtils;
import org.chromium.base.CpuFeatures;
import org.chromium.base.EarlyTraceEvent;
import org.chromium.base.JavaExceptionReporter;
import org.chromium.base.Log;
import org.chromium.base.ThreadUtils;
import org.chromium.base.TraceEvent;
import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.JNINamespace;
import org.chromium.base.annotations.NativeMethods;
import org.chromium.base.library_loader.LibraryLoader;
import org.chromium.base.library_loader.LibraryLoader.MultiProcessMediator;
import org.chromium.base.metrics.RecordHistogram;
import org.chromium.base.process_launcher.ChildConnectionAllocator;
import org.chromium.base.process_launcher.ChildProcessConnection;
import org.chromium.base.process_launcher.ChildProcessConstants;
import org.chromium.base.process_launcher.ChildProcessLauncher;
import org.chromium.base.process_launcher.FileDescriptorInfo;
import org.chromium.base.task.PostTask;
import org.chromium.content.app.SandboxedProcessService;
import org.chromium.content.common.ContentSwitchUtils;
import org.chromium.content_public.browser.ChildProcessImportance;
import org.chromium.content_public.browser.ContentFeatureList;
import org.chromium.content_public.browser.UiThreadTaskTraits;
import org.chromium.content_public.common.ContentFeatures;
import org.chromium.content_public.common.ContentSwitches;

import java.io.IOException;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * This is the java counterpart to ChildProcessLauncherHelper. It is owned by native side and
 * has an explicit destroy method.
 * Each public or jni methods should have explicit documentation on what threads they are called.
 */
@JNINamespace("content::internal")
public final class ChildProcessLauncherHelperImpl {
    private static final String TAG = "ChildProcLH";

    // Manifest values used to specify the service names.
    private static final String NUM_SANDBOXED_SERVICES_KEY =
            "org.chromium.content.browser.NUM_SANDBOXED_SERVICES";
    private static final String NUM_PRIVILEGED_SERVICES_KEY =
            "org.chromium.content.browser.NUM_PRIVILEGED_SERVICES";

    // When decrementing the refcount on bindings, delay the decrement by this amount of time in
    // case a new ref count is added in the mean time. This is a heuristic to avoid temporarily
    // dropping bindings when inputs to calculating importance change independently.
    private static final int REMOVE_BINDING_DELAY_MS = 500;

    // To be conservative, only delay removing binding in the initial second of the process.
    private static final int TIMEOUT_FOR_DELAY_BINDING_REMOVE_MS = 1000;

    // Delay after app is background before reducing process priority.
    private static final int REDUCE_PRIORITY_ON_BACKGROUND_DELAY_MS = 9 * 1000;

    private static final Runnable sDelayedBackgroundTask =
            ChildProcessLauncherHelperImpl::onSentToBackgroundOnLauncherThreadAfterDelay;

    // Flag to check if ServiceGroupImportance should be enabled after native is initialized.
    private static boolean sCheckedServiceGroupImportance;

    // A warmed-up connection to a sandboxed service.
    private static SpareChildConnection sSpareSandboxedConnection;

    // A warmed-up connection to a privileged service (network service).
    private static SpareChildConnection sSparePrivilegedConntection;

    // Allocator used for sandboxed services.
    private static ChildConnectionAllocator sSandboxedChildConnectionAllocator;
    private static ChildProcessRanking sSandboxedChildConnectionRanking;

    // Map from PID to ChildProcessLauncherHelper.
    private static final Map<Integer, ChildProcessLauncherHelperImpl> sLauncherByPid =
            new HashMap<>();

    // Allocator used for non-sandboxed services.
    private static ChildConnectionAllocator sPrivilegedChildConnectionAllocator;

    // Used by tests to override the default sandboxed service allocator settings.
    private static ChildConnectionAllocator.ConnectionFactory sSandboxedServiceFactoryForTesting;
    private static int sSandboxedServicesCountForTesting = -1;
    private static String sSandboxedServicesNameForTesting;
    private static boolean sSkipDelayForReducePriorityOnBackgroundForTesting;

    private static BindingManager sBindingManager;

    // Whether the main application is currently brought to the foreground.
    private static boolean sApplicationInForegroundOnUiThread;

    // Set on UI thread only, but null-checked on launcher thread as well.
    private static ApplicationStatus.ApplicationStateListener sAppStateListener;

    // TODO(boliu): These are only set for sandboxed renderer processes. Generalize them for
    // all types of processes.
    private final ChildProcessRanking mRanking;
    private final BindingManager mBindingManager;

    // Whether the created process should be sandboxed.
    private final boolean mSandboxed;

    // Remove strong binding when app is in background.
    private final boolean mReducePriorityOnBackground;

    // The type of process as determined by the command line.
    private final String mProcessType;

    // Whether the process can use warmed up connection.
    private final boolean mCanUseWarmUpConnection;

    // Tracks reporting exception from child process to avoid reporting it more than once.
    private boolean mReportedException;

    // Enables early Java tracing in child process before native is initialized.
    private static final String TRACE_EARLY_JAVA_IN_CHILD_SWITCH =
            "--" + EarlyTraceEvent.TRACE_EARLY_JAVA_IN_CHILD_SWITCH;

    // The first known App Zygote PID. If the app zygote gets restarted, the new bundles from it
    // are not sent further for simplicity. Accessed only on LauncherThread.
    private static int sZygotePid;

    // The bundle with RELRO FD. For sending to child processes, including the ones that did not
    // announce whether they inherit from the app zygote. Declared as volatile to allow sending it
    // from different threads.
    private static volatile Bundle sZygoteBundle;

    private final ChildProcessLauncher.Delegate mLauncherDelegate =
            new ChildProcessLauncher.Delegate() {
                @Override
                public ChildProcessConnection getBoundConnection(
                        ChildConnectionAllocator connectionAllocator,
                        ChildProcessConnection.ServiceCallback serviceCallback) {
                    if (!mCanUseWarmUpConnection) return null;
                    SpareChildConnection spareConnection =
                            mSandboxed ? sSpareSandboxedConnection : sSparePrivilegedConntection;
                    if (spareConnection == null) return null;
                    return spareConnection.getConnection(connectionAllocator, serviceCallback);
                }

                @Override
                public void onBeforeConnectionAllocated(Bundle bundle) {
                    populateServiceBundle(bundle);
                }

                @Override
                public void onBeforeConnectionSetup(Bundle connectionBundle) {
                    // Populate the bundle passed to the service setup call with content specific
                    // parameters.
                    connectionBundle.putInt(
                            ContentChildProcessConstants.EXTRA_CPU_COUNT, CpuFeatures.getCount());
                    connectionBundle.putLong(
                            ContentChildProcessConstants.EXTRA_CPU_FEATURES, CpuFeatures.getMask());
                    if (sZygoteBundle != null) {
                        connectionBundle.putAll(sZygoteBundle);
                    } else {
                        LibraryLoader.getInstance().getMediator().putSharedRelrosToBundle(
                                connectionBundle);
                    }
                }

                @Override
                public void onConnectionEstablished(ChildProcessConnection connection) {
                    assert LauncherThread.runningOnLauncherThread();
                    int pid = connection.getPid();

                    if (pid > 0) {
                        sLauncherByPid.put(pid, ChildProcessLauncherHelperImpl.this);
                        if (mRanking != null) {
                            mRanking.addConnection(connection, false /* visible */,
                                    1 /* frameDepth */, false /* intersectsViewport */,
                                    ChildProcessImportance.MODERATE);
                            if (mBindingManager != null) mBindingManager.rankingChanged();
                        }
                        if (mSandboxed) {
                            ChildProcessConnectionMetrics.getInstance().addConnection(connection);
                        }
                        if (mReducePriorityOnBackground
                                && !ApplicationStatus.hasVisibleActivities()) {
                            reducePriorityOnBackgroundOnLauncherThread();
                        }
                    }

                    // Tell native launch result (whether getPid is 0).
                    if (mNativeChildProcessLauncherHelper != 0) {
                        ChildProcessLauncherHelperImplJni.get().onChildProcessStarted(
                                mNativeChildProcessLauncherHelper, connection.getPid());
                    }
                    mNativeChildProcessLauncherHelper = 0;
                }

                @Override
                public void onReceivedZygoteInfo(
                        ChildProcessConnection connection, Bundle relroBundle) {
                    assert LauncherThread.runningOnLauncherThread();
                    distributeZygoteInfo(connection, relroBundle);
                }

                @Override
                public void onConnectionLost(ChildProcessConnection connection) {
                    assert LauncherThread.runningOnLauncherThread();
                    if (connection.getPid() == 0) return;

                    ChildProcessLauncherHelperImpl result =
                            sLauncherByPid.remove(connection.getPid());
                    // Child process might die before onConnectionEstablished.
                    if (result == null) return;

                    if (mBindingManager != null) mBindingManager.removeConnection(connection);
                    if (mRanking != null) {
                        setReverseRankWhenConnectionLost(mRanking.getReverseRank(connection));
                        mRanking.removeConnection(connection);
                        if (mBindingManager != null) mBindingManager.rankingChanged();
                    }
                    if (mSandboxed) {
                        ChildProcessConnectionMetrics.getInstance().removeConnection(connection);
                    }
                }
            };

    @IntDef({ZygoteChildState.FAILED_TO_CREATE_BUNDLE, ZygoteChildState.FIRST_USABLE_ZYGOTE,
            ZygoteChildState.ZYGOTE_RESTARTED, ZygoteChildState.ZYGOTE_SEEN})
    @Retention(RetentionPolicy.SOURCE)
    private @interface ZygoteChildState {
        // These values are persisted to logs. Entries should not be renumbered and numeric values
        // should never be reused.
        int FAILED_TO_CREATE_BUNDLE = 0;
        int FIRST_USABLE_ZYGOTE = 1;
        int ZYGOTE_RESTARTED = 2;
        int ZYGOTE_SEEN = 3;
        int COUNT = 4;
    }

    /**
     * Records a histogram telling what is known about the App Zygote that the
     * current isolated process was forked from. Must be called once per
     * isolated process (but only if it comes from the app zygote (S+) and RELRO
     * sharing with the app zygote is working.
     */
    private static void recordChildStateHistogram(@ZygoteChildState int state) {
        RecordHistogram.recordEnumeratedHistogram(
                "ChromiumAndroidLinker.ChildProcessZygoteState", state, ZygoteChildState.COUNT);
    }

    /**
     * Called for every new child connection. Receives a possibly null bundle inherited from the App
     * Zygote. Sends the bundle to existing processes that did not have usable bundles or sends
     * a previously memoized bundle to the new child.
     *
     * @param connection the connection to the new child
     * @param zygoteBundle the bundle received from the child process, null means that either the
     *                     process did not inherit from the app zygote or the app zygote did not
     *                     produce a usable RELRO region.
     */
    private static void distributeZygoteInfo(
            ChildProcessConnection connection, @Nullable Bundle zygoteBundle) {
        if (LibraryLoader.mainProcessIntendsToProvideRelroFd()) return;

        if (!connection.hasUsableZygoteInfo()) {
            Log.d(TAG, "Connection likely not created from app zygote");
            sendPreviouslySeenZygoteBundle(connection);
            return;
        }

        // If the process was created from the app zygote, but failed to generate the the zygote
        // bundle - ignore it.
        if (zygoteBundle == null) {
            recordChildStateHistogram(ZygoteChildState.FAILED_TO_CREATE_BUNDLE);
            return;
        }

        if (sZygotePid != 0) {
            Log.d(TAG, "Zygote was seen before with a usable RELRO bundle.");
            onObtainedUsableZygoteBundle(connection);
            return;
        }

        Log.d(TAG, "Encountered the first usable RELRO bundle.");
        recordChildStateHistogram(ZygoteChildState.FIRST_USABLE_ZYGOTE);
        sZygotePid = connection.getZygotePid();
        sZygoteBundle = zygoteBundle;

        // Use the RELRO FD in the current process. Some nontrivial CPU cycles are consumed because
        // it needs an mmap+memcmp(5 megs)+mmap+munmap. This happens on the process launcher thread,
        // will work correctly on any thread.
        LibraryLoader.getInstance().getMediator().takeSharedRelrosFromBundle(zygoteBundle);

        // Use the RELRO FD for all processes launched up to now. Non-blocking 'oneway' IPCs are
        // used. The CPU time costs in the child process are the same.
        sendPreviouslySeenZygoteBundleToExistingConnections(connection.getPid());
    }

    private static void onObtainedUsableZygoteBundle(ChildProcessConnection connection) {
        if (sZygotePid != connection.getZygotePid()) {
            Log.d(TAG, "Zygote restarted.");
            recordChildStateHistogram(ZygoteChildState.ZYGOTE_RESTARTED);
            return;
        }
        recordChildStateHistogram(ZygoteChildState.ZYGOTE_SEEN);
        // TODO(pasko): To avoid accumulating open file descriptors close the received RELRO FD
        // if it cannot be used.
    }

    private static void sendPreviouslySeenZygoteBundle(ChildProcessConnection connection) {
        if (sZygotePid != 0 && sZygoteBundle != null) {
            connection.consumeZygoteBundle(sZygoteBundle);
        }
    }

    private static void sendPreviouslySeenZygoteBundleToExistingConnections(int pid) {
        CollectionUtil.forEach(sLauncherByPid, entry -> {
            int otherPid = entry.getKey();
            if (pid != otherPid) {
                ChildProcessConnection otherConnection = entry.getValue().mLauncher.getConnection();
                if (otherConnection.getZygotePid() == 0) {
                    // The Zygote PID for each connection must be finalized before the launcher
                    // thread starts processing the zygote info. Zygote PID being 0 guarantees that
                    // the zygote did not produce the RELRO region.
                    otherConnection.consumeZygoteBundle(sZygoteBundle);
                }
            }
        });
    }

    private final ChildProcessLauncher mLauncher;

    private long mNativeChildProcessLauncherHelper;

    private long mStartTimeMs;

    // This is the current computed importance from all the inputs from setPriority.
    // The initial value is MODERATE since a newly created connection has visible bindings.
    private @ChildProcessImportance int mEffectiveImportance = ChildProcessImportance.MODERATE;
    private boolean mVisible;

    private boolean mDroppedStrongBingingDueToBackgrounding;

    // Protects fields below that are accessed on client thread as well.
    private final Object mLock = new Object();
    private int mReverseRankWhenConnectionLost;

    @CalledByNative
    private static FileDescriptorInfo makeFdInfo(
            int id, int fd, boolean autoClose, long offset, long size) {
        assert LauncherThread.runningOnLauncherThread();
        ParcelFileDescriptor pFd;
        if (autoClose) {
            // Adopt the FD, it will be closed when we close the ParcelFileDescriptor.
            pFd = ParcelFileDescriptor.adoptFd(fd);
        } else {
            try {
                pFd = ParcelFileDescriptor.fromFd(fd);
            } catch (IOException e) {
                Log.e(TAG, "Invalid FD provided for process connection, aborting connection.", e);
                return null;
            }
        }
        return new FileDescriptorInfo(id, pFd, offset, size);
    }

    @CalledByNative
    private static ChildProcessLauncherHelperImpl createAndStart(long nativePointer,
            String[] commandLine, FileDescriptorInfo[] filesToBeMapped,
            boolean canUseWarmUpConnection) {
        assert LauncherThread.runningOnLauncherThread();
        String processType =
                ContentSwitchUtils.getSwitchValue(commandLine, ContentSwitches.SWITCH_PROCESS_TYPE);

        if (TraceEvent.enabled()) {
            commandLine = Arrays.copyOf(commandLine, commandLine.length + 1);
            commandLine[commandLine.length - 1] = TRACE_EARLY_JAVA_IN_CHILD_SWITCH;
        }
        boolean sandboxed = true;
        boolean reducePriorityOnBackground = false;
        if (!ContentSwitches.SWITCH_RENDERER_PROCESS.equals(processType)) {
            if (ContentSwitches.SWITCH_GPU_PROCESS.equals(processType)) {
                sandboxed = false;
                reducePriorityOnBackground = ContentFeatureList.isEnabled(
                        ContentFeatures.REDUCE_GPU_PRIORITY_ON_BACKGROUND);
            } else {
                // We only support sandboxed utility processes now.
                assert ContentSwitches.SWITCH_UTILITY_PROCESS.equals(processType);

                if (ContentSwitches.NONE_SANDBOX_TYPE.equals(ContentSwitchUtils.getSwitchValue(
                            commandLine, ContentSwitches.SWITCH_SERVICE_SANDBOX_TYPE))) {
                    sandboxed = false;
                }
            }
        }

        IBinder binderCallback = ContentSwitches.SWITCH_GPU_PROCESS.equals(processType)
                ? new GpuProcessCallback()
                : null;

        ChildProcessLauncherHelperImpl helper = new ChildProcessLauncherHelperImpl(nativePointer,
                commandLine, filesToBeMapped, sandboxed, reducePriorityOnBackground,
                canUseWarmUpConnection, binderCallback);
        helper.start();

        if (sandboxed && !sCheckedServiceGroupImportance) {
            sCheckedServiceGroupImportance = true;
            if (sSandboxedChildConnectionRanking != null
                    && ChildProcessLauncherHelperImplJni.get().serviceGroupImportanceEnabled()) {
                sSandboxedChildConnectionRanking.enableServiceGroupImportance();
            }
        }
        return helper;
    }

    /**
     * @see {@link ChildProcessLauncherHelper#warmUp(Context)}.
     */
    public static void warmUp(final Context context, boolean sandboxed) {
        assert ThreadUtils.runningOnUiThread();
        LauncherThread.post(new Runnable() {
            @Override
            public void run() {
                warmUpOnLauncherThread(context, sandboxed);
            }
        });
    }

    private static void warmUpOnLauncherThread(Context context, boolean sandboxed) {
        SpareChildConnection spareConnection =
                sandboxed ? sSpareSandboxedConnection : sSparePrivilegedConntection;
        if (spareConnection != null && !spareConnection.isEmpty()) {
            return;
        }

        Bundle serviceBundle = populateServiceBundle(new Bundle());
        ChildConnectionAllocator allocator = getConnectionAllocator(context, sandboxed);
        if (sandboxed) {
            sSpareSandboxedConnection = new SpareChildConnection(context, allocator, serviceBundle);
        } else {
            sSparePrivilegedConntection =
                    new SpareChildConnection(context, allocator, serviceBundle);
        }
    }

    /**
     * @see {@link ChildProcessLauncherHelper#startBindingManagement(Context)}.
     */
    public static void startBindingManagement(final Context context) {
        assert ThreadUtils.runningOnUiThread();
        final int maxConnections = ContentFeatureList.getFieldTrialParamByFeatureAsInt(
                ContentFeatures.BINDING_MANAGER_CONNECTION_LIMIT, "max_connections",
                BindingManager.NO_MAX_SIZE);
        LauncherThread.post(new Runnable() {
            @Override
            public void run() {
                ChildConnectionAllocator allocator =
                        getConnectionAllocator(context, true /* sandboxed */);
                if (ChildProcessConnection.supportVariableConnections()) {
                    sBindingManager = new BindingManager(
                            context, maxConnections, sSandboxedChildConnectionRanking);
                } else {
                    sBindingManager = new BindingManager(context,
                            (maxConnections == BindingManager.NO_MAX_SIZE)
                                    ? allocator.getNumberOfServices()
                                    : Math.min(allocator.getNumberOfServices(), maxConnections),
                            sSandboxedChildConnectionRanking);
                }
                ChildProcessConnectionMetrics.getInstance().setBindingManager(sBindingManager);
            }
        });
    }

    private static void onSentToBackground() {
        assert ThreadUtils.runningOnUiThread();
        sApplicationInForegroundOnUiThread = false;
        int delay = sSkipDelayForReducePriorityOnBackgroundForTesting
                ? 0
                : REDUCE_PRIORITY_ON_BACKGROUND_DELAY_MS;
        LauncherThread.postDelayed(sDelayedBackgroundTask, delay);
        LauncherThread.post(() -> {
            if (sBindingManager != null) sBindingManager.onSentToBackground();
        });
    }

    private static void onSentToBackgroundOnLauncherThreadAfterDelay() {
        assert LauncherThread.runningOnLauncherThread();
        for (ChildProcessLauncherHelperImpl helper : sLauncherByPid.values()) {
            if (!helper.mReducePriorityOnBackground) continue;
            helper.reducePriorityOnBackgroundOnLauncherThread();
        }
    }

    private void reducePriorityOnBackgroundOnLauncherThread() {
        assert LauncherThread.runningOnLauncherThread();
        if (mDroppedStrongBingingDueToBackgrounding) return;
        ChildProcessConnection connection = mLauncher.getConnection();
        if (!connection.isConnected()) return;
        if (connection.isStrongBindingBound()) {
            connection.removeStrongBinding();
            mDroppedStrongBingingDueToBackgrounding = true;
        }
    }

    private void raisePriorityOnForegroundOnLauncherThread() {
        assert LauncherThread.runningOnLauncherThread();
        if (!mDroppedStrongBingingDueToBackgrounding) return;
        ChildProcessConnection connection = mLauncher.getConnection();
        if (!connection.isConnected()) return;
        connection.addStrongBinding();
        mDroppedStrongBingingDueToBackgrounding = false;
    }

    private static void onBroughtToForeground() {
        assert ThreadUtils.runningOnUiThread();
        sApplicationInForegroundOnUiThread = true;
        LauncherThread.removeCallbacks(sDelayedBackgroundTask);
        LauncherThread.post(() -> {
            for (ChildProcessLauncherHelperImpl helper : sLauncherByPid.values()) {
                if (!helper.mReducePriorityOnBackground) continue;
                helper.raisePriorityOnForegroundOnLauncherThread();
            }
            if (sBindingManager != null) sBindingManager.onBroughtToForeground();
        });
    }

    @VisibleForTesting
    public static void setSandboxServicesSettingsForTesting(
            ChildConnectionAllocator.ConnectionFactory factory, int serviceCount,
            String serviceName) {
        sSandboxedServiceFactoryForTesting = factory;
        sSandboxedServicesCountForTesting = serviceCount;
        sSandboxedServicesNameForTesting = serviceName;
    }

    @VisibleForTesting
    public static void setSkipDelayForReducePriorityOnBackgroundForTesting() {
        sSkipDelayForReducePriorityOnBackgroundForTesting = true;
    }

    @VisibleForTesting
    static ChildConnectionAllocator getConnectionAllocator(Context context, boolean sandboxed) {
        assert LauncherThread.runningOnLauncherThread();
        boolean bindToCaller = ChildProcessCreationParamsImpl.getBindToCallerCheck();
        boolean bindAsExternalService =
                sandboxed && ChildProcessCreationParamsImpl.getIsSandboxedServiceExternal();

        if (!sandboxed) {
            if (sPrivilegedChildConnectionAllocator == null) {
                sPrivilegedChildConnectionAllocator =
                        ChildConnectionAllocator.create(context, LauncherThread.getHandler(), null,
                                ChildProcessCreationParamsImpl.getPackageNameForPrivilegedService(),
                                ChildProcessCreationParamsImpl.getPrivilegedServicesName(),
                                NUM_PRIVILEGED_SERVICES_KEY, bindToCaller, bindAsExternalService,
                                true /* useStrongBinding */);
            }
            return sPrivilegedChildConnectionAllocator;
        }

        if (sSandboxedChildConnectionAllocator == null) {
            final String packageName =
                    ChildProcessCreationParamsImpl.getPackageNameForSandboxedService();
            Log.d(TAG,
                    "Create a new ChildConnectionAllocator with package name = %s,"
                            + " sandboxed = true",
                    packageName);
            Runnable freeSlotRunnable = () -> {
                ChildProcessConnection lowestRank =
                        sSandboxedChildConnectionRanking.getLowestRankedConnection();
                if (lowestRank != null) {
                    lowestRank.kill();
                }
            };

            ChildConnectionAllocator connectionAllocator = null;
            if (sSandboxedServicesCountForTesting != -1) {
                // Testing case where allocator settings are overriden.
                String serviceName = !TextUtils.isEmpty(sSandboxedServicesNameForTesting)
                        ? sSandboxedServicesNameForTesting
                        : SandboxedProcessService.class.getName();
                connectionAllocator =
                        ChildConnectionAllocator.createFixedForTesting(freeSlotRunnable,
                                packageName, serviceName, sSandboxedServicesCountForTesting,
                                bindToCaller, bindAsExternalService, false /* useStrongBinding */);
            } else if (ChildProcessConnection.supportVariableConnections()) {
                connectionAllocator = ChildConnectionAllocator.createVariableSize(context,
                        LauncherThread.getHandler(), freeSlotRunnable, packageName,
                        ChildProcessCreationParamsImpl.getSandboxedServicesName(), bindToCaller,
                        bindAsExternalService, false /* useStrongBinding */);
            } else {
                connectionAllocator = ChildConnectionAllocator.create(context,
                        LauncherThread.getHandler(), freeSlotRunnable, packageName,
                        ChildProcessCreationParamsImpl.getSandboxedServicesName(),
                        NUM_SANDBOXED_SERVICES_KEY, bindToCaller, bindAsExternalService,
                        false /* useStrongBinding */);
            }
            if (sSandboxedServiceFactoryForTesting != null) {
                connectionAllocator.setConnectionFactoryForTesting(
                        sSandboxedServiceFactoryForTesting);
            }
            sSandboxedChildConnectionAllocator = connectionAllocator;
            if (ChildProcessConnection.supportVariableConnections()) {
                sSandboxedChildConnectionRanking = new ChildProcessRanking();
            } else {
                sSandboxedChildConnectionRanking = new ChildProcessRanking(
                        sSandboxedChildConnectionAllocator.getNumberOfServices());
            }
        }
        return sSandboxedChildConnectionAllocator;
    }

    private ChildProcessLauncherHelperImpl(long nativePointer, String[] commandLine,
            FileDescriptorInfo[] filesToBeMapped, boolean sandboxed,
            boolean reducePriorityOnBackground, boolean canUseWarmUpConnection,
            IBinder binderCallback) {
        assert LauncherThread.runningOnLauncherThread();

        mNativeChildProcessLauncherHelper = nativePointer;
        mSandboxed = sandboxed;
        mReducePriorityOnBackground = reducePriorityOnBackground;
        mCanUseWarmUpConnection = canUseWarmUpConnection;
        ChildConnectionAllocator connectionAllocator =
                getConnectionAllocator(ContextUtils.getApplicationContext(), sandboxed);
        mLauncher = new ChildProcessLauncher(LauncherThread.getHandler(), mLauncherDelegate,
                commandLine, filesToBeMapped, connectionAllocator,
                binderCallback == null ? null : Arrays.asList(binderCallback));
        mProcessType =
                ContentSwitchUtils.getSwitchValue(commandLine, ContentSwitches.SWITCH_PROCESS_TYPE);

        if (sandboxed) {
            mRanking = sSandboxedChildConnectionRanking;
            mBindingManager = sBindingManager;
            mReverseRankWhenConnectionLost = -1;
        } else {
            mRanking = null;
            mBindingManager = null;
            // -2 means not applicable.
            mReverseRankWhenConnectionLost = -2;
        }

        if (!ApplicationStatus.isInitialized()) return;
        if (sAppStateListener != null) return;
        PostTask.postTask(UiThreadTaskTraits.BEST_EFFORT, () -> {
            if (sAppStateListener != null) return;
            sApplicationInForegroundOnUiThread = ApplicationStatus.hasVisibleActivities();
            sAppStateListener = newState -> {
                switch (newState) {
                    case ApplicationState.UNKNOWN:
                        break;
                    case ApplicationState.HAS_RUNNING_ACTIVITIES:
                    case ApplicationState.HAS_PAUSED_ACTIVITIES:
                        if (!sApplicationInForegroundOnUiThread) onBroughtToForeground();
                        break;
                    default:
                        if (sApplicationInForegroundOnUiThread) onSentToBackground();
                        break;
                }
            };
            ApplicationStatus.registerApplicationStateListener(sAppStateListener);
        });
    }

    private void start() {
        mLauncher.start(true /* doSetupConnection */, true /* queueIfNoFreeConnection */);
        mStartTimeMs = System.currentTimeMillis();
    }

    /**
     * @return The type of process as specified in the command line at
     * {@link ContentSwitches#SWITCH_PROCESS_TYPE}.
     */
    private String getProcessType() {
        return TextUtils.isEmpty(mProcessType) ? "" : mProcessType;
    }

    private void setReverseRankWhenConnectionLost(int reverseRank) {
        synchronized (mLock) {
            mReverseRankWhenConnectionLost = reverseRank;
        }
    }

    private int getReverseRankWhenConnectionLost() {
        synchronized (mLock) {
            return mReverseRankWhenConnectionLost;
        }
    }

    // Called on client (UI or IO) thread.
    @CalledByNative
    private void getTerminationInfoAndStop(long terminationInfoPtr) {
        ChildProcessConnection connection = mLauncher.getConnection();
        // Here we are accessing the connection from a thread other than the launcher thread, but it
        // does not change once it's been set. So it is safe to test whether it's null here and
        // access it afterwards.
        if (connection == null) return;

        // Note there is no guarantee that connection lost has happened. However ChildProcessRanking
        // is not thread safe, so this is the best we can do.
        int reverseRank = getReverseRankWhenConnectionLost();
        String exceptionString = connection.getExceptionDuringInit();
        if (exceptionString != null && !mReportedException) {
            mReportedException = true;
            PostTask.postTask(UiThreadTaskTraits.BEST_EFFORT,
                    () -> JavaExceptionReporter.reportStackTrace(exceptionString));
        }
        ChildProcessLauncherHelperImplJni.get().setTerminationInfo(terminationInfoPtr,
                connection.bindingStateCurrentOrWhenDied(), connection.isKilledByUs(),
                connection.hasCleanExit(), exceptionString != null, reverseRank);
        LauncherThread.post(() -> mLauncher.stop());
    }

    @CalledByNative
    private void setPriority(int pid, boolean visible, boolean hasMediaStream,
            boolean hasForegroundServiceWorker, long frameDepth, boolean intersectsViewport,
            boolean boostForPendingViews, @ChildProcessImportance int importance) {
        assert LauncherThread.runningOnLauncherThread();
        assert mLauncher.getPid() == pid;
        if (getByPid(pid) == null) {
            // Child already disconnected. Ignore any trailing calls.
            return;
        }

        ChildProcessConnection connection = mLauncher.getConnection();
        if (ChildProcessCreationParamsImpl.getIgnoreVisibilityForImportance()) {
            visible = false;
            boostForPendingViews = false;
        }

        boolean mediaRendererHasModerate = ContentFeatureList.isEnabled(
                ContentFeatureList.BACKGROUND_MEDIA_RENDERER_HAS_MODERATE_BINDING);

        @ChildProcessImportance
        int newEffectiveImportance;
        if ((visible && frameDepth == 0) || importance == ChildProcessImportance.IMPORTANT
                || (hasMediaStream && !mediaRendererHasModerate)) {
            newEffectiveImportance = ChildProcessImportance.IMPORTANT;
        } else if ((visible && frameDepth > 0 && intersectsViewport) || boostForPendingViews
                || importance == ChildProcessImportance.MODERATE
                || (hasMediaStream && mediaRendererHasModerate) || hasForegroundServiceWorker) {
            newEffectiveImportance = ChildProcessImportance.MODERATE;
        } else {
            newEffectiveImportance = ChildProcessImportance.NORMAL;
        }

        // Add first and remove second.
        if (visible && !mVisible) {
            if (mBindingManager != null) mBindingManager.addConnection(connection);
        }
        mVisible = visible;

        if (mEffectiveImportance != newEffectiveImportance) {
            switch (newEffectiveImportance) {
                case ChildProcessImportance.NORMAL:
                    // Nothing to add.
                    break;
                case ChildProcessImportance.MODERATE:
                    connection.addVisibleBinding();
                    break;
                case ChildProcessImportance.IMPORTANT:
                    connection.addStrongBinding();
                    break;
                default:
                    assert false;
            }
        }

        if (mRanking != null) {
            mRanking.updateConnection(
                    connection, visible, frameDepth, intersectsViewport, importance);
            if (mBindingManager != null) mBindingManager.rankingChanged();
        }

        if (mEffectiveImportance != newEffectiveImportance
                && mEffectiveImportance != ChildProcessImportance.NORMAL) {
            final int existingEffectiveImportance = mEffectiveImportance;
            Runnable removeBindingRunnable = () -> {
                switch (existingEffectiveImportance) {
                    case ChildProcessImportance.NORMAL:
                        // Nothing to remove.
                        break;
                    case ChildProcessImportance.MODERATE:
                        connection.removeVisibleBinding();
                        break;
                    case ChildProcessImportance.IMPORTANT:
                        connection.removeStrongBinding();
                        break;
                    default:
                        assert false;
                }
            };
            if (System.currentTimeMillis() - mStartTimeMs < TIMEOUT_FOR_DELAY_BINDING_REMOVE_MS) {
                LauncherThread.postDelayed(removeBindingRunnable, REMOVE_BINDING_DELAY_MS);
            } else {
                removeBindingRunnable.run();
            }
        }

        mEffectiveImportance = newEffectiveImportance;
    }

    @CalledByNative
    static void stop(int pid) {
        assert LauncherThread.runningOnLauncherThread();
        Log.d(TAG, "stopping child connection: pid=%d", pid);
        ChildProcessLauncherHelperImpl launcher = getByPid(pid);
        // launcher can be null for single process.
        if (launcher != null) {
            // Can happen for single process.
            launcher.mLauncher.stop();
        }
    }

    // Called on client (UI or IO) thread.
    @CalledByNative
    private @ChildBindingState int getEffectiveChildBindingState() {
        ChildProcessConnection connection = mLauncher.getConnection();
        // Here we are accessing the connection from a thread other than the launcher thread, but it
        // does not change once it's been set. So it is safe to test whether it's null here and
        // access it afterwards.
        if (connection == null) return ChildBindingState.UNBOUND;

        return connection.bindingStateCurrent();
    }

    /**
     * Dumps the stack of the child process with |pid|  without crashing it.
     * @param pid Process id of the child process.
     */
    @CalledByNative
    private void dumpProcessStack(int pid) {
        assert LauncherThread.runningOnLauncherThread();
        ChildProcessLauncherHelperImpl launcher = getByPid(pid);
        if (launcher != null) {
            ChildProcessConnection connection = launcher.mLauncher.getConnection();
            connection.dumpProcessStack();
        }
    }

    private static Bundle populateServiceBundle(Bundle bundle) {
        ChildProcessCreationParamsImpl.addIntentExtras(bundle);
        bundle.putBoolean(ChildProcessConstants.EXTRA_BIND_TO_CALLER,
                ChildProcessCreationParamsImpl.getBindToCallerCheck());
        MultiProcessMediator m = LibraryLoader.getInstance().getMediator();
        m.ensureInitializedInMainProcess();
        m.putLoadAddressToBundle(bundle);
        return bundle;
    }

    private static ChildProcessLauncherHelperImpl getByPid(int pid) {
        return sLauncherByPid.get(pid);
    }

    /**
     * Groups all currently tracked processes by type and returns a map of type -> list of PIDs.
     *
     * @param callback The callback to notify with the process information.  {@code callback} will
     *                 run on the same thread this method is called on.  That thread must support a
     *                 {@link android.os.Looper}.
     */
    public static void getProcessIdsByType(Callback < Map < String, List<Integer>>> callback) {
        final Handler responseHandler = new Handler();
        LauncherThread.post(() -> {
            Map<String, List<Integer>> map = new HashMap<>();
            CollectionUtil.forEach(sLauncherByPid, entry -> {
                String type = entry.getValue().getProcessType();
                List<Integer> pids = map.get(type);
                if (pids == null) {
                    pids = new ArrayList<>();
                    map.put(type, pids);
                }
                pids.add(entry.getKey());
            });

            responseHandler.post(callback.bind(map));
        });
    }

    // Testing only related methods.

    @VisibleForTesting
    int getPidForTesting() {
        assert LauncherThread.runningOnLauncherThread();
        return mLauncher.getPid();
    }

    @VisibleForTesting
    public static Map<Integer, ChildProcessLauncherHelperImpl> getAllProcessesForTesting() {
        return sLauncherByPid;
    }

    @VisibleForTesting
    public static ChildProcessLauncherHelperImpl createAndStartForTesting(String[] commandLine,
            FileDescriptorInfo[] filesToBeMapped, boolean sandboxed,
            boolean reducePriorityOnBackground, boolean canUseWarmUpConnection,
            IBinder binderCallback, boolean doSetupConnection) {
        ChildProcessLauncherHelperImpl launcherHelper =
                new ChildProcessLauncherHelperImpl(0L, commandLine, filesToBeMapped, sandboxed,
                        reducePriorityOnBackground, canUseWarmUpConnection, binderCallback);
        launcherHelper.mLauncher.start(doSetupConnection, true /* queueIfNoFreeConnection */);
        return launcherHelper;
    }

    /** @return the count of services set-up and working. */
    @VisibleForTesting
    static int getConnectedServicesCountForTesting() {
        int count = sPrivilegedChildConnectionAllocator == null
                ? 0
                : sPrivilegedChildConnectionAllocator.allocatedConnectionsCountForTesting();
        return count + getConnectedSandboxedServicesCountForTesting();
    }

    @VisibleForTesting
    public static int getConnectedSandboxedServicesCountForTesting() {
        return sSandboxedChildConnectionAllocator == null
                ? 0
                : sSandboxedChildConnectionAllocator.allocatedConnectionsCountForTesting();
    }

    @VisibleForTesting
    public ChildProcessConnection getChildProcessConnection() {
        return mLauncher.getConnection();
    }

    @VisibleForTesting
    public ChildConnectionAllocator getChildConnectionAllocatorForTesting() {
        return mLauncher.getConnectionAllocator();
    }

    @VisibleForTesting
    public static ChildProcessConnection getWarmUpConnectionForTesting(boolean sandboxed) {
        SpareChildConnection connection =
                sandboxed ? sSpareSandboxedConnection : sSparePrivilegedConntection;
        return connection == null ? null : connection.getConnection();
    }

    @NativeMethods
    interface Natives {
        // Can be called on a number of threads, including launcher, and binder.
        void onChildProcessStarted(long nativeChildProcessLauncherHelper, int pid);

        void setTerminationInfo(long termiantionInfoPtr, @ChildBindingState int bindingState,
                boolean killedByUs, boolean cleanExit, boolean exceptionDuringInit,
                int reverseRank);

        boolean serviceGroupImportanceEnabled();
    }
}
