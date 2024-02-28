// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.content.browser;

import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.LimitExceededException;
import android.os.Process;
import android.view.MotionEvent;

import androidx.annotation.IntDef;
import androidx.privacysandbox.ads.adservices.java.measurement.MeasurementManagerFutures;
import androidx.privacysandbox.ads.adservices.measurement.DeletionRequest;
import androidx.privacysandbox.ads.adservices.measurement.WebSourceParams;
import androidx.privacysandbox.ads.adservices.measurement.WebSourceRegistrationRequest;
import androidx.privacysandbox.ads.adservices.measurement.WebTriggerParams;
import androidx.privacysandbox.ads.adservices.measurement.WebTriggerRegistrationRequest;

import com.google.common.collect.ImmutableList;
import com.google.common.util.concurrent.FutureCallback;
import com.google.common.util.concurrent.Futures;
import com.google.common.util.concurrent.ListenableFuture;

import org.jni_zero.CalledByNative;
import org.jni_zero.JNINamespace;
import org.jni_zero.NativeMethods;

import org.chromium.base.ContextUtils;
import org.chromium.base.Log;
import org.chromium.base.ResettersForTesting;
import org.chromium.base.ThreadUtils;
import org.chromium.base.metrics.RecordHistogram;
import org.chromium.base.task.PostTask;
import org.chromium.base.task.TaskTraits;
import org.chromium.url.GURL;

import java.io.IOException;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.time.Instant;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Locale;
import java.util.concurrent.TimeoutException;

/**
 * Handles passing registrations with Web Attribution Reporting API to the underlying native
 * library.
 */
@JNINamespace("content")
public class AttributionOsLevelManager {
    private static final String TAG = "AttributionManager";
    // TODO: replace with constant in android.Manifest.permission once it becomes available in U.
    private static final String PERMISSION_ACCESS_ADSERVICES_ATTRIBUTION =
            "android.permission.ACCESS_ADSERVICES_ATTRIBUTION";

    // Used for testing
    private static MeasurementManagerFutures sManagerForTesting;

    private long mNativePtr;
    private MeasurementManagerFutures mManager;

    @IntDef({
        OperationType.REGISTER_SOURCE,
        OperationType.REGISTER_WEB_SOURCE,
        OperationType.REGISTER_TRIGGER,
        OperationType.REGISTER_WEB_TRIGGER,
        OperationType.GET_MEASUREMENT_API_STATUS,
        OperationType.DELETE_REGISTRATIONS
    })
    @Retention(RetentionPolicy.SOURCE)
    public @interface OperationType {
        int REGISTER_SOURCE = 0;
        int REGISTER_WEB_SOURCE = 1;
        int REGISTER_TRIGGER = 2;
        int REGISTER_WEB_TRIGGER = 3;
        int GET_MEASUREMENT_API_STATUS = 4;
        int DELETE_REGISTRATIONS = 5;
    }

    // These values are persisted to logs. Entries should not be renumbered and
    // numeric values should never be reused.
    @IntDef({
        OperationResult.SUCCESS,
        OperationResult.ERROR_UNKNOWN,
        OperationResult.ERROR_ILLEGAL_ARGUMENT,
        OperationResult.ERROR_IO,
        OperationResult.ERROR_ILLEGAL_STATE,
        OperationResult.ERROR_SECURITY,
        OperationResult.ERROR_TIMEOUT,
        OperationResult.ERROR_LIMIT_EXCEEDED,
        OperationResult.ERROR_INTERNAL,
        OperationResult.ERROR_BACKGROUND_CALLER,
        OperationResult.ERROR_VERSION_UNSUPPORTED,
        OperationResult.ERROR_PERMISSION_UNGRANTED,
        OperationResult.COUNT
    })
    @Retention(RetentionPolicy.SOURCE)
    public @interface OperationResult {
        int SUCCESS = 0;
        int ERROR_UNKNOWN = 1;
        int ERROR_ILLEGAL_ARGUMENT = 2;
        int ERROR_IO = 3;
        int ERROR_ILLEGAL_STATE = 4;
        int ERROR_SECURITY = 5;
        int ERROR_TIMEOUT = 6;
        int ERROR_LIMIT_EXCEEDED = 7;
        int ERROR_INTERNAL = 8;
        int ERROR_BACKGROUND_CALLER = 9;
        int ERROR_VERSION_UNSUPPORTED = 10;
        int ERROR_PERMISSION_UNGRANTED = 11;
        int COUNT = 12;
    }

    private static boolean supportsAttribution() {
        return Build.VERSION.SDK_INT >= Build.VERSION_CODES.R;
    }

    private static @OperationResult int convertToOperationResult(Throwable thrown) {
        if (thrown instanceof IllegalArgumentException) {
            return OperationResult.ERROR_ILLEGAL_ARGUMENT;
        } else if (thrown instanceof IOException) {
            return OperationResult.ERROR_IO;
        } else if (thrown instanceof IllegalStateException) {
            // The Android API doesn't break out this error as a separate exception so we
            // are forced to inspect the message for now.
            if (thrown.getMessage().toLowerCase(Locale.US).contains("background")) {
                return OperationResult.ERROR_BACKGROUND_CALLER;
            } else {
                return OperationResult.ERROR_ILLEGAL_STATE;
            }
        } else if (thrown instanceof SecurityException) {
            return OperationResult.ERROR_SECURITY;
        } else if (thrown instanceof TimeoutException) {
            return OperationResult.ERROR_TIMEOUT;
        } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R
                && thrown instanceof LimitExceededException) {
            return OperationResult.ERROR_LIMIT_EXCEEDED;
        } else {
            return OperationResult.ERROR_UNKNOWN;
        }
    }

    private static void recordOperationResult(
            @OperationType int type, @OperationResult int result) {
        String suffix = "";
        switch (type) {
            case OperationType.REGISTER_SOURCE:
                suffix = "RegisterSource";
                break;
            case OperationType.REGISTER_WEB_SOURCE:
                suffix = "RegisterWebSource";
                break;
            case OperationType.REGISTER_TRIGGER:
                suffix = "RegisterTrigger";
                break;
            case OperationType.REGISTER_WEB_TRIGGER:
                suffix = "RegisterWebTrigger";
                break;
            case OperationType.GET_MEASUREMENT_API_STATUS:
                suffix = "GetMeasurementApiStatus";
                break;
            case OperationType.DELETE_REGISTRATIONS:
                suffix = "DeleteRegistrations";
                break;
        }

        assert suffix.length() > 0;

        RecordHistogram.recordEnumeratedHistogram(
                "Conversions.AndroidOperationResult." + suffix, result, OperationResult.COUNT);
    }

    @CalledByNative
    private AttributionOsLevelManager(long nativePtr) {
        mNativePtr = nativePtr;
    }

    private MeasurementManagerFutures getManager() {
        if (!supportsAttribution()) {
            return null;
        }
        if (sManagerForTesting != null) {
            return sManagerForTesting;
        }
        if (mManager != null) {
            return mManager;
        }
        try {
            mManager = MeasurementManagerFutures.from(ContextUtils.getApplicationContext());
        } catch (Throwable t) {
            // An error may be thrown if android.ext.adservices is not loaded.
            Log.i(TAG, "Failed to get measurement manager", t);
        }
        return mManager;
    }

    private void onRegistrationCompleted(
            int requestId, @OperationType int type, @OperationResult int result) {
        recordOperationResult(type, result);

        if (mNativePtr != 0) {
            AttributionOsLevelManagerJni.get()
                    .onRegistrationCompleted(
                            mNativePtr, requestId, result == OperationResult.SUCCESS);
        }
    }

    private void addRegistrationFutureCallback(
            int requestId, @OperationType int type, ListenableFuture<?> future) {
        if (!supportsAttribution()) {
            return;
        }
        Futures.addCallback(
                future,
                new FutureCallback<Object>() {
                    @Override
                    public void onSuccess(Object result) {
                        onRegistrationCompleted(requestId, type, OperationResult.SUCCESS);
                    }

                    @Override
                    public void onFailure(Throwable thrown) {
                        Log.w(TAG, "Failed to register", thrown);
                        onRegistrationCompleted(requestId, type, convertToOperationResult(thrown));
                    }
                },
                ContextUtils.getApplicationContext().getMainExecutor());
    }

    /**
     * Registers a web attribution source with native, see `registerWebSourceAsync()`:
     * https://developer.android.com/reference/androidx/privacysandbox/ads/adservices/java/measurement/MeasurementManagerFutures.
     */
    @CalledByNative
    private void registerWebAttributionSource(
            int requestId,
            GURL registrationUrl,
            GURL topLevelOrigin,
            boolean isDebugKeyAllowed,
            MotionEvent event) {
        if (!supportsAttribution()) {
            onRegistrationCompleted(
                    requestId,
                    OperationType.REGISTER_WEB_SOURCE,
                    OperationResult.ERROR_VERSION_UNSUPPORTED);
            return;
        }
        MeasurementManagerFutures mm = getManager();
        if (mm == null) {
            onRegistrationCompleted(
                    requestId, OperationType.REGISTER_WEB_SOURCE, OperationResult.ERROR_INTERNAL);
            return;
        }
        ListenableFuture<?> future =
                mm.registerWebSourceAsync(
                        new WebSourceRegistrationRequest(
                                Arrays.asList(
                                        new WebSourceParams(
                                                Uri.parse(registrationUrl.getSpec()),
                                                isDebugKeyAllowed)),
                                Uri.parse(topLevelOrigin.getSpec()),
                                /* inputEvent= */ event,
                                /* appDestination= */ null,
                                /* webDestination= */ null,
                                /* verifiedDestination= */ null));
        addRegistrationFutureCallback(requestId, OperationType.REGISTER_WEB_SOURCE, future);
    }

    /**
     * Registers an attribution source with native, see `registerSourceAsync()`:
     * https://developer.android.com/reference/androidx/privacysandbox/ads/adservices/java/measurement/MeasurementManagerFutures.
     */
    @CalledByNative
    private void registerAttributionSource(int requestId, GURL registrationUrl, MotionEvent event) {
        if (!supportsAttribution()) {
            onRegistrationCompleted(
                    requestId,
                    OperationType.REGISTER_SOURCE,
                    OperationResult.ERROR_VERSION_UNSUPPORTED);
            return;
        }
        MeasurementManagerFutures mm = getManager();
        if (mm == null) {
            onRegistrationCompleted(
                    requestId, OperationType.REGISTER_SOURCE, OperationResult.ERROR_INTERNAL);
            return;
        }
        ListenableFuture<?> future =
                mm.registerSourceAsync(Uri.parse(registrationUrl.getSpec()), event);
        addRegistrationFutureCallback(requestId, OperationType.REGISTER_SOURCE, future);
    }

    /**
     * Registers a web attribution trigger with native, see `registerWebTriggerAsync()`:
     * https://developer.android.com/reference/androidx/privacysandbox/ads/adservices/java/measurement/MeasurementManagerFutures.
     */
    @CalledByNative
    private void registerWebAttributionTrigger(
            int requestId, GURL registrationUrl, GURL topLevelOrigin, boolean isDebugKeyAllowed) {
        if (!supportsAttribution()) {
            onRegistrationCompleted(
                    requestId,
                    OperationType.REGISTER_WEB_TRIGGER,
                    OperationResult.ERROR_VERSION_UNSUPPORTED);
            return;
        }

        MeasurementManagerFutures mm = getManager();
        if (mm == null) {
            onRegistrationCompleted(
                    requestId, OperationType.REGISTER_WEB_TRIGGER, OperationResult.ERROR_INTERNAL);
            return;
        }
        ListenableFuture<?> future =
                mm.registerWebTriggerAsync(
                        new WebTriggerRegistrationRequest(
                                Arrays.asList(
                                        new WebTriggerParams(
                                                Uri.parse(registrationUrl.getSpec()),
                                                isDebugKeyAllowed)),
                                Uri.parse(topLevelOrigin.getSpec())));
        addRegistrationFutureCallback(requestId, OperationType.REGISTER_WEB_TRIGGER, future);
    }

    /**
     * Registers an attribution trigger with native, see `registerTriggerAsync()`:
     * https://developer.android.com/reference/androidx/privacysandbox/ads/adservices/java/measurement/MeasurementManagerFutures.
     */
    @CalledByNative
    private void registerAttributionTrigger(int requestId, GURL registrationUrl) {
        if (!supportsAttribution()) {
            onRegistrationCompleted(
                    requestId,
                    OperationType.REGISTER_TRIGGER,
                    OperationResult.ERROR_VERSION_UNSUPPORTED);
            return;
        }

        MeasurementManagerFutures mm = getManager();
        if (mm == null) {
            onRegistrationCompleted(
                    requestId, OperationType.REGISTER_TRIGGER, OperationResult.ERROR_INTERNAL);
            return;
        }
        ListenableFuture<?> future = mm.registerTriggerAsync(Uri.parse(registrationUrl.getSpec()));
        addRegistrationFutureCallback(requestId, OperationType.REGISTER_TRIGGER, future);
    }

    private void onDataDeletionCompleted(int requestId) {
        if (mNativePtr != 0) {
            AttributionOsLevelManagerJni.get().onDataDeletionCompleted(mNativePtr, requestId);
        }
    }

    /**
     * Deletes attribution data with native, see `deleteRegistrationsAsync()`:
     * https://developer.android.com/reference/androidx/privacysandbox/ads/adservices/java/measurement/MeasurementManagerFutures.
     */
    @CalledByNative
    private void deleteRegistrations(
            int requestId,
            long startMs,
            long endMs,
            GURL[] origins,
            String[] domains,
            int deletionMode,
            int matchBehavior) {
        if (!supportsAttribution()) {
            recordOperationResult(
                    OperationType.DELETE_REGISTRATIONS, OperationResult.ERROR_VERSION_UNSUPPORTED);
            onDataDeletionCompleted(requestId);
            return;
        }
        MeasurementManagerFutures mm = getManager();
        if (mm == null) {
            recordOperationResult(
                    OperationType.DELETE_REGISTRATIONS, OperationResult.ERROR_INTERNAL);
            onDataDeletionCompleted(requestId);
            return;
        }

        // Currently Android and Chromium have different matching behaviors when both
        // `origins` and `domains` are empty.
        // Chromium: Delete -> Delete nothing; Preserve -> Delete all.
        // Android: Delete -> Delete all; Preserve -> Delete nothing.
        // Android may fix the behavior in the future. As a workaround, Chromium will
        // not call Android if it's to delete nothing (no-op), and call Android with
        // both Delete and Preserve modes if it's to delete all. These two modes will
        // be one no-op and one delete all in Android releases with and without the
        // fix. See crbug.com/1442967.

        ImmutableList<Integer> matchBehaviors = null;

        if (origins.length == 0 && domains.length == 0) {
            switch (matchBehavior) {
                case DeletionRequest.MATCH_BEHAVIOR_DELETE:
                    recordOperationResult(
                            OperationType.DELETE_REGISTRATIONS, OperationResult.SUCCESS);
                    onDataDeletionCompleted(requestId);
                    return;
                case DeletionRequest.MATCH_BEHAVIOR_PRESERVE:
                    matchBehaviors =
                            ImmutableList.of(
                                    DeletionRequest.MATCH_BEHAVIOR_DELETE,
                                    DeletionRequest.MATCH_BEHAVIOR_PRESERVE);
                    break;
                default:
                    Log.e(TAG, "Received invalid match behavior: ", matchBehavior);
                    recordOperationResult(
                            OperationType.DELETE_REGISTRATIONS, OperationResult.ERROR_UNKNOWN);
                    onDataDeletionCompleted(requestId);
                    return;
            }
        } else {
            matchBehaviors = ImmutableList.of(matchBehavior);
        }

        ArrayList<Uri> originUris = new ArrayList<Uri>(origins.length);
        for (GURL origin : origins) {
            originUris.add(Uri.parse(origin.getSpec()));
        }

        ArrayList<Uri> domainUris = new ArrayList<Uri>(domains.length);
        for (String domain : domains) {
            domainUris.add(Uri.parse(domain));
        }

        int numCalls = matchBehaviors.size();

        FutureCallback<Object> callback =
                new FutureCallback<Object>() {
                    private int mNumPendingCalls = numCalls;

                    private void onCall() {
                        if (--mNumPendingCalls == 0) {
                            onDataDeletionCompleted(requestId);
                        }
                    }

                    @Override
                    public void onSuccess(Object result) {
                        recordOperationResult(
                                OperationType.DELETE_REGISTRATIONS, OperationResult.SUCCESS);
                        onCall();
                    }

                    @Override
                    public void onFailure(Throwable thrown) {
                        Log.w(TAG, "Failed to delete measurement API data", thrown);
                        recordOperationResult(
                                OperationType.DELETE_REGISTRATIONS,
                                convertToOperationResult(thrown));
                        onCall();
                    }
                };

        for (int currMatchBehavior : matchBehaviors) {
            ListenableFuture<?> future =
                    mm.deleteRegistrationsAsync(
                            new DeletionRequest(
                                    deletionMode,
                                    currMatchBehavior,
                                    Instant.ofEpochMilli(startMs),
                                    Instant.ofEpochMilli(endMs),
                                    originUris,
                                    domainUris));

            Futures.addCallback(
                    future, callback, ContextUtils.getApplicationContext().getMainExecutor());
        }
    }

    private static void onMeasurementStateReturned(int status, @OperationResult int result) {
        recordOperationResult(OperationType.GET_MEASUREMENT_API_STATUS, result);
        AttributionOsLevelManagerJni.get().onMeasurementStateReturned(status);
    }

    /**
     * Gets Measurement API status with native, see `getMeasurementApiStatusAsync()`:
     * https://developer.android.com/reference/androidx/privacysandbox/ads/adservices/java/measurement/MeasurementManagerFutures.
     */
    @CalledByNative
    private static void getMeasurementApiStatus() {
        ThreadUtils.assertOnBackgroundThread();

        if (sManagerForTesting != null) {
            AttributionOsLevelManagerJni.get().onMeasurementStateReturned(1);
            return;
        }

        if (!supportsAttribution()) {
            onMeasurementStateReturned(/* status= */ 0, OperationResult.ERROR_VERSION_UNSUPPORTED);
            return;
        }
        if (ContextUtils.getApplicationContext()
                        .checkPermission(
                                PERMISSION_ACCESS_ADSERVICES_ATTRIBUTION,
                                Process.myPid(),
                                Process.myUid())
                != PackageManager.PERMISSION_GRANTED) {
            // Permission may not be granted when embedded as WebView.
            onMeasurementStateReturned(/* status= */ 0, OperationResult.ERROR_PERMISSION_UNGRANTED);
            return;
        }
        MeasurementManagerFutures mm = null;
        try {
            mm = MeasurementManagerFutures.from(ContextUtils.getApplicationContext());
        } catch (Throwable t) {
            // An error may be thrown if android.ext.adservices is not loaded.
            Log.i(TAG, "Failed to get measurement manager", t);
        }

        if (mm == null) {
            onMeasurementStateReturned(/* status= */ 0, OperationResult.ERROR_INTERNAL);
            return;
        }

        ListenableFuture<Integer> future = null;
        try {
            future = mm.getMeasurementApiStatusAsync();
        } catch (IllegalStateException ex) {
            // An illegal state exception may be thrown for some versions of the underlying
            // Privacy Sandbox SDK.
            Log.i(TAG, "Failed to get measurement API status", ex);
        }

        if (future == null) {
            onMeasurementStateReturned(/* status= */ 0, OperationResult.ERROR_INTERNAL);
            return;
        }

        Futures.addCallback(
                future,
                new FutureCallback<Integer>() {
                    @Override
                    public void onSuccess(Integer status) {
                        onMeasurementStateReturned(status, OperationResult.SUCCESS);
                    }

                    @Override
                    public void onFailure(Throwable thrown) {
                        Log.w(TAG, "Failed to get measurement API status", thrown);
                        onMeasurementStateReturned(
                                /* status= */ 0, convertToOperationResult(thrown));
                    }
                },
                ContextUtils.getApplicationContext().getMainExecutor());
    }

    @CalledByNative
    private void nativeDestroyed() {
        mNativePtr = 0;
    }

    public static void setManagerForTesting(MeasurementManagerFutures manager) {
        sManagerForTesting = manager;
        PostTask.postTask(
                TaskTraits.BEST_EFFORT, () -> AttributionOsLevelManager.getMeasurementApiStatus());
        ResettersForTesting.register(
                () -> {
                    sManagerForTesting = null;
                    PostTask.postTask(
                            TaskTraits.BEST_EFFORT,
                            () -> AttributionOsLevelManager.getMeasurementApiStatus());
                });
    }

    @NativeMethods
    interface Natives {
        void onDataDeletionCompleted(long nativeAttributionOsLevelManagerAndroid, int requestId);

        void onRegistrationCompleted(
                long nativeAttributionOsLevelManagerAndroid, int requestId, boolean success);

        void onMeasurementStateReturned(int state);
    }
}
