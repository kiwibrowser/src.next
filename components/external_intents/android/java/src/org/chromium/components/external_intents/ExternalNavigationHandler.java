// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.components.external_intents;

import android.content.ActivityNotFoundException;
import android.content.ComponentName;
import android.content.Context;
import android.content.DialogInterface;
import android.content.DialogInterface.OnCancelListener;
import android.content.DialogInterface.OnClickListener;
import android.content.Intent;
import android.content.Intent.ShortcutIconResource;
import android.content.IntentFilter;
import android.content.pm.ActivityInfo;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.content.pm.PackageManager.NameNotFoundException;
import android.content.pm.ResolveInfo;
import android.content.res.Resources;
import android.graphics.drawable.Drawable;
import android.net.Uri;
import android.os.StrictMode;
import android.os.SystemClock;
import android.provider.Browser;
import android.provider.Telephony;
import android.text.TextUtils;
import android.util.AndroidRuntimeException;
import android.util.Pair;
import android.view.WindowManager.BadTokenException;
import android.webkit.MimeTypeMap;
import android.webkit.WebView;

import androidx.annotation.IntDef;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;
import androidx.appcompat.app.AlertDialog;

import org.chromium.base.CommandLine;
import org.chromium.base.ContextUtils;
import org.chromium.base.IntentUtils;
import org.chromium.base.Log;
import org.chromium.base.PackageManagerUtils;
import org.chromium.base.PathUtils;
import org.chromium.base.metrics.RecordHistogram;
import org.chromium.base.metrics.RecordUserAction;
import org.chromium.base.supplier.Supplier;
import org.chromium.base.task.PostTask;
import org.chromium.build.BuildConfig;
import org.chromium.components.embedder_support.util.UrlConstants;
import org.chromium.components.embedder_support.util.UrlUtilities;
import org.chromium.components.embedder_support.util.UrlUtilitiesJni;
import org.chromium.components.external_intents.ExternalNavigationDelegate.IntentToAutofillAllowingAppResult;
import org.chromium.components.messages.MessageBannerProperties;
import org.chromium.components.messages.MessageDispatcher;
import org.chromium.components.messages.MessageDispatcherProvider;
import org.chromium.components.messages.MessageIdentifier;
import org.chromium.components.messages.MessageScopeType;
import org.chromium.components.messages.PrimaryActionClickBehavior;
import org.chromium.components.webapk.lib.client.ChromeWebApkHostSignature;
import org.chromium.components.webapk.lib.client.WebApkValidator;
import org.chromium.content_public.browser.LoadUrlParams;
import org.chromium.content_public.browser.UiThreadTaskTraits;
import org.chromium.content_public.browser.WebContents;
import org.chromium.content_public.common.ContentUrlConstants;
import org.chromium.content_public.common.Referrer;
import org.chromium.network.mojom.ReferrerPolicy;
import org.chromium.ui.base.MimeTypeUtils;
import org.chromium.ui.base.PageTransition;
import org.chromium.ui.base.WindowAndroid;
import org.chromium.ui.modelutil.PropertyModel;
import org.chromium.ui.permissions.PermissionCallback;
import org.chromium.url.GURL;
import org.chromium.url.Origin;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Set;

/**
 * Logic related to the URL overriding/intercepting functionality.
 * This feature supports conversion of certain navigations to Android Intents allowing
 * applications like Youtube to direct users clicking on a http(s) link to their native app.
 */
public class ExternalNavigationHandler {
    private static final String TAG = "UrlHandler";

    private static final String WTAI_URL_PREFIX = "wtai://wp/";
    private static final String WTAI_MC_URL_PREFIX = "wtai://wp/mc;";

    private static final String PLAY_PACKAGE_PARAM = "id";
    private static final String PLAY_REFERRER_PARAM = "referrer";
    private static final String PLAY_APP_PATH = "/store/apps/details";
    private static final String PLAY_HOSTNAME = "play.google.com";
    @VisibleForTesting
    public static final String PLAY_APP_PACKAGE = "com.android.vending";

    private static final String PDF_EXTENSION = "pdf";
    private static final String PDF_VIEWER = "com.google.android.apps.docs";
    private static final String PDF_MIME = "application/pdf";
    private static final String PDF_SUFFIX = ".pdf";

    /**
     * Records package names of external applications in the system that could have handled this
     * intent.
     */
    public static final String EXTRA_EXTERNAL_NAV_PACKAGES = "org.chromium.chrome.browser.eenp";

    @VisibleForTesting
    public static final String EXTRA_BROWSER_FALLBACK_URL = "browser_fallback_url";

    // An extra that may be specified on an intent:// URL that contains an encoded value for the
    // referrer field passed to the market:// URL in the case where the app is not present.
    @VisibleForTesting
    static final String EXTRA_MARKET_REFERRER = "market_referrer";

    // A mask of flags that are safe for untrusted content to use when starting an Activity.
    // This list is not exhaustive and flags not listed here are not necessarily unsafe.
    @VisibleForTesting
    static final int ALLOWED_INTENT_FLAGS = Intent.FLAG_EXCLUDE_STOPPED_PACKAGES
            | Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_SINGLE_TOP
            | Intent.FLAG_ACTIVITY_MATCH_EXTERNAL | Intent.FLAG_ACTIVITY_NEW_TASK
            | Intent.FLAG_ACTIVITY_MULTIPLE_TASK | Intent.FLAG_ACTIVITY_NEW_DOCUMENT
            | Intent.FLAG_ACTIVITY_RETAIN_IN_RECENTS | Intent.FLAG_ACTIVITY_LAUNCH_ADJACENT;

    @VisibleForTesting
    static final String INSTANT_APP_SUPERVISOR_PKG = "com.google.android.instantapps.supervisor";

    @VisibleForTesting
    static final String[] INSTANT_APP_START_ACTIONS = {"com.google.android.instantapps.START",
            "com.google.android.instantapps.nmr1.INSTALL",
            "com.google.android.instantapps.nmr1.VIEW"};

    // These values are persisted in histograms. Please do not renumber. Append only.
    @IntDef({AiaIntent.FALLBACK_USED, AiaIntent.SERP, AiaIntent.OTHER})
    @Retention(RetentionPolicy.SOURCE)
    private @interface AiaIntent {
        int FALLBACK_USED = 0;
        int SERP = 1;
        int OTHER = 2;

        int NUM_ENTRIES = 3;
    }

    /**
     * Histogram for the result of an intent scheme navigation.
     * This enum is used in UMA, do not reorder values.
     */
    @IntDef({IntentUriNavigationResult.WITH_FALLBACK_LAUNCHED_INTENT,
            IntentUriNavigationResult.WITH_FALLBACK_USED_FALLBACK,
            IntentUriNavigationResult.WITH_FALLBACK_NO_OVERRIDE,
            IntentUriNavigationResult.WITH_FALLBACK_ASYNC_RESULT,
            IntentUriNavigationResult.NO_FALLBACK_LAUNCHED_INTENT,
            IntentUriNavigationResult.NO_FALLBACK_NO_OVERRIDE,
            IntentUriNavigationResult.NO_FALLBACK_ASYNC_RESULT,
            IntentUriNavigationResult.NUM_ENTRIES})
    @Retention(RetentionPolicy.SOURCE)
    public @interface IntentUriNavigationResult {
        /* Intent with an unused fallback URL was launched. */
        int WITH_FALLBACK_LAUNCHED_INTENT = 0;
        /* Intent fallback URL was used. */
        int WITH_FALLBACK_USED_FALLBACK = 1;
        /* Intent with an unused fallback URL was blocked. */
        int WITH_FALLBACK_NO_OVERRIDE = 2;
        /* Intent with a fallback URL prompted the user. */
        int WITH_FALLBACK_ASYNC_RESULT = 3;
        /* Intent without a fallback URL was launched. */
        int NO_FALLBACK_LAUNCHED_INTENT = 4;
        /* Intent without a fallback URL was blocked. */
        int NO_FALLBACK_NO_OVERRIDE = 5;
        /* Intent without a fallback URL prompted the user. */
        int NO_FALLBACK_ASYNC_RESULT = 6;

        int NUM_ENTRIES = 7;
    }

    private static final String INTENT_URI_RESULT_NAME = "Android.Intent.IntentUriNavigationResult";

    // Helper class to return a boolean by reference.
    private static class MutableBoolean {
        private Boolean mValue;
        public void set(boolean value) {
            mValue = value;
        }
        public Boolean get() {
            return mValue;
        }
    }

    // A Supplier that only evaluates when needed then caches the value.
    protected static class LazySupplier<T> implements Supplier<T> {
        private T mValue;
        private Supplier<T> mInnerSupplier;

        public LazySupplier(Supplier<T> innerSupplier) {
            assert innerSupplier != null : "innerSupplier cannot be null";
            mInnerSupplier = innerSupplier;
        }

        @Nullable
        @Override
        public T get() {
            if (mInnerSupplier != null) {
                mValue = mInnerSupplier.get();

                // Clear the inner supplier to record that we have evaluated and to free any
                // references it may have held.
                mInnerSupplier = null;
            }
            return mValue;
        }

        @Override
        public boolean hasValue() {
            return true;
        }
    }

    private static class IntentBasedSupplier<T> extends LazySupplier<T> {
        protected final Intent mIntent;
        private Intent mIntentCopy;

        public IntentBasedSupplier(Intent intent, Supplier<T> innerSupplier) {
            super(innerSupplier);
            mIntent = intent;
        }

        protected void assertIntentMatches() {
            // If the intent filter changes the previously supplied result will no longer be valid.
            if (BuildConfig.ENABLE_ASSERTS) {
                if (mIntentCopy != null) {
                    assert intentResolutionMatches(mIntent, mIntentCopy);
                } else {
                    mIntentCopy = new Intent(mIntent);
                }
            }
        }

        @Nullable
        @Override
        public T get() {
            assertIntentMatches();
            return super.get();
        }
    }

    // Used to ensure we only call queryIntentActivities when we really need to.
    protected class QueryIntentActivitiesSupplier extends IntentBasedSupplier<List<ResolveInfo>> {
        // We need the query to include non-default intent filters, but should not return
        // them for clients that don't explicitly need to check non-default filters.
        private class QueryNonDefaultSupplier extends LazySupplier<List<ResolveInfo>> {
            public QueryNonDefaultSupplier(Intent intent) {
                super(()
                                -> PackageManagerUtils.queryIntentActivities(
                                        intent, PackageManager.GET_RESOLVED_FILTER));
            }
        }

        final QueryNonDefaultSupplier mNonDefaultSupplier;

        public QueryIntentActivitiesSupplier(Intent intent) {
            super(intent, () -> queryIntentActivities(intent));
            mNonDefaultSupplier = new QueryNonDefaultSupplier(intent);
        }

        public List<ResolveInfo> getIncludingNonDefaultResolveInfos() {
            assertIntentMatches();
            return mNonDefaultSupplier.get();
        }
    }

    protected static class ResolveActivitySupplier extends IntentBasedSupplier<ResolveInfo> {
        public ResolveActivitySupplier(Intent intent) {
            super(intent,
                    ()
                            -> PackageManagerUtils.resolveActivity(
                                    intent, PackageManager.MATCH_DEFAULT_ONLY));
        }
    }

    private final ExternalNavigationDelegate mDelegate;

    /**
     * Result types for checking if we should override URL loading.
     * NOTE: this enum is used in UMA, do not reorder values. Changes should be append only.
     * Values should be numerated from 0 and can't have gaps.
     * NOTE: NUM_ENTRIES must be added inside the IntDef{} to work around crbug.com/1300585. It
     * should be removed from the IntDef{} if an alternate solution for that bug is found.
     */
    @IntDef({OverrideUrlLoadingResultType.OVERRIDE_WITH_EXTERNAL_INTENT,
            OverrideUrlLoadingResultType.OVERRIDE_WITH_CLOBBERING_TAB,
            OverrideUrlLoadingResultType.OVERRIDE_WITH_ASYNC_ACTION,
            OverrideUrlLoadingResultType.NO_OVERRIDE, OverrideUrlLoadingResultType.NUM_ENTRIES})
    @Retention(RetentionPolicy.SOURCE)
    public @interface OverrideUrlLoadingResultType {
        /* We should override the URL loading and launch an intent. */
        int OVERRIDE_WITH_EXTERNAL_INTENT = 0;
        /* We should override the URL loading and clobber the current tab. */
        int OVERRIDE_WITH_CLOBBERING_TAB = 1;
        /* We should override the URL loading.  The desired action will be determined
         * asynchronously (e.g. by requiring user confirmation). */
        int OVERRIDE_WITH_ASYNC_ACTION = 2;
        /* We shouldn't override the URL loading. */
        int NO_OVERRIDE = 3;

        int NUM_ENTRIES = 4;
    }

    /**
     * Types of async action that can be taken for a navigation.
     */
    @IntDef({OverrideUrlLoadingAsyncActionType.UI_GATING_BROWSER_NAVIGATION,
            OverrideUrlLoadingAsyncActionType.UI_GATING_INTENT_LAUNCH,
            OverrideUrlLoadingAsyncActionType.NO_ASYNC_ACTION})
    @Retention(RetentionPolicy.SOURCE)
    public @interface OverrideUrlLoadingAsyncActionType {
        /* The user has been presented with a consent dialog gating a browser navigation. */
        int UI_GATING_BROWSER_NAVIGATION = 0;
        /* The user has been presented with a consent dialog gating an intent launch. */
        int UI_GATING_INTENT_LAUNCH = 1;
        /* No async action has been taken. */
        int NO_ASYNC_ACTION = 2;

        int NUM_ENTRIES = 3;
    }

    /**
     * Packages information about the result of a check of whether we should override URL loading.
     */
    public static class OverrideUrlLoadingResult {
        @OverrideUrlLoadingResultType
        int mResultType;

        @OverrideUrlLoadingAsyncActionType
        int mAsyncActionType;

        boolean mWasExternalFallbackUrlLaunch;

        private OverrideUrlLoadingResult(@OverrideUrlLoadingResultType int resultType) {
            this(resultType, OverrideUrlLoadingAsyncActionType.NO_ASYNC_ACTION, false);
        }

        private OverrideUrlLoadingResult(@OverrideUrlLoadingResultType int resultType,
                @OverrideUrlLoadingAsyncActionType int asyncActionType,
                boolean wasExternalFallbackUrlLaunch) {
            // The async action type should be set only for async actions...
            assert (asyncActionType == OverrideUrlLoadingAsyncActionType.NO_ASYNC_ACTION
                    || resultType == OverrideUrlLoadingResultType.OVERRIDE_WITH_ASYNC_ACTION);

            // ...and it *must* be set for async actions.
            assert (!(asyncActionType == OverrideUrlLoadingAsyncActionType.NO_ASYNC_ACTION
                    && resultType == OverrideUrlLoadingResultType.OVERRIDE_WITH_ASYNC_ACTION));

            assert (!wasExternalFallbackUrlLaunch
                    || resultType == OverrideUrlLoadingResultType.OVERRIDE_WITH_EXTERNAL_INTENT);

            mResultType = resultType;
            mAsyncActionType = asyncActionType;
            mWasExternalFallbackUrlLaunch = wasExternalFallbackUrlLaunch;
        }

        public @OverrideUrlLoadingResultType int getResultType() {
            return mResultType;
        }

        public @OverrideUrlLoadingAsyncActionType int getAsyncActionType() {
            return mAsyncActionType;
        }

        public boolean wasExternalFallbackUrlLaunch() {
            return mWasExternalFallbackUrlLaunch;
        }

        /**
         * Use this result when an asynchronous action needs to be carried out before deciding
         * whether to block the external navigation.
         */
        public static OverrideUrlLoadingResult forAsyncAction(
                @OverrideUrlLoadingAsyncActionType int asyncActionType) {
            return new OverrideUrlLoadingResult(
                    OverrideUrlLoadingResultType.OVERRIDE_WITH_ASYNC_ACTION, asyncActionType,
                    false);
        }

        /**
         * Use this result when we would like to block an external navigation without prompting the
         * user asking them whether would like to launch an app, or when the navigation does not
         * target an app.
         */
        public static OverrideUrlLoadingResult forNoOverride() {
            return new OverrideUrlLoadingResult(OverrideUrlLoadingResultType.NO_OVERRIDE);
        }

        /**
         * Use this result when the current external navigation should be blocked and a new
         * navigation will be started in the Tab, clobbering the previous one.
         */
        public static OverrideUrlLoadingResult forClobberingTab() {
            return new OverrideUrlLoadingResult(
                    OverrideUrlLoadingResultType.OVERRIDE_WITH_CLOBBERING_TAB);
        }

        /**
         * Use this result when an external app has been launched as a result of the navigation.
         */
        public static OverrideUrlLoadingResult forExternalIntent() {
            return new OverrideUrlLoadingResult(
                    OverrideUrlLoadingResultType.OVERRIDE_WITH_EXTERNAL_INTENT);
        }

        /**
         * Use this result when an external app has been launched as a result of using the fallback
         * URL for an intent scheme navigation.
         */
        public static OverrideUrlLoadingResult forExternalFallbackUrl() {
            return new OverrideUrlLoadingResult(
                    OverrideUrlLoadingResultType.OVERRIDE_WITH_EXTERNAL_INTENT,
                    OverrideUrlLoadingAsyncActionType.NO_ASYNC_ACTION, true);
        }
    }

    /**
     * Constructs a new instance of {@link ExternalNavigationHandler}, using the injected
     * {@link ExternalNavigationDelegate}.
     */
    public ExternalNavigationHandler(ExternalNavigationDelegate delegate) {
        mDelegate = delegate;
    }

    private static boolean debug() {
        return ExternalIntentsFeatures.EXTERNAL_NAVIGATION_DEBUG_LOGS.isEnabled();
    }

    /**
     * Determines whether the URL needs to be sent as an intent to the system,
     * and sends it, if appropriate.
     * @return Whether the URL generated an intent, caused a navigation in
     *         current tab, or wasn't handled at all.
     */
    public OverrideUrlLoadingResult shouldOverrideUrlLoading(ExternalNavigationParams params) {
        if (debug()) Log.i(TAG, "shouldOverrideUrlLoading called on " + params.getUrl().getSpec());
        Intent targetIntent;
        boolean isIntentUrl = UrlUtilities.hasIntentScheme(params.getUrl());
        // Perform generic parsing of the URI to turn it into an Intent.
        if (isIntentUrl) {
            try {
                targetIntent = Intent.parseUri(params.getUrl().getSpec(), Intent.URI_INTENT_SCHEME);
            } catch (Exception ex) {
                Log.w(TAG, "Bad URI %s", params.getUrl().getSpec(), ex);
                return OverrideUrlLoadingResult.forNoOverride();
            }
        } else if (isSupportedWtaiProtocol(params.getUrl())) {
            targetIntent = parseWtaiMcProtocol(params.getUrl());
        } else {
            targetIntent = new Intent(Intent.ACTION_VIEW);
            targetIntent.setData(Uri.parse(params.getUrl().getSpec()));
        }

        GURL browserFallbackUrl =
                new GURL(IntentUtils.safeGetStringExtra(targetIntent, EXTRA_BROWSER_FALLBACK_URL));
        if (!browserFallbackUrl.isValid() || !UrlUtilities.isHttpOrHttps(browserFallbackUrl)) {
            browserFallbackUrl = GURL.emptyGURL();
        }
        targetIntent.removeExtra(EXTRA_BROWSER_FALLBACK_URL);

        // TODO(https://crbug.com/1096099): Refactor shouldOverrideUrlLoadingInternal, splitting it
        // up to separate out the notions wanting to fire an external intent vs being able to.
        MutableBoolean canLaunchExternalFallbackResult = new MutableBoolean();

        long time = SystemClock.elapsedRealtime();
        OverrideUrlLoadingResult result = shouldOverrideUrlLoadingInternal(
                params, targetIntent, browserFallbackUrl, canLaunchExternalFallbackResult);
        assert canLaunchExternalFallbackResult.get() != null;
        RecordHistogram.recordTimesHistogram(
                "Android.StrictMode.OverrideUrlLoadingTime", SystemClock.elapsedRealtime() - time);

        if (result.getResultType() != OverrideUrlLoadingResultType.NO_OVERRIDE) {
            int pageTransitionCore = params.getPageTransition() & PageTransition.CORE_MASK;
            boolean isFormSubmit = pageTransitionCore == PageTransition.FORM_SUBMIT;
            boolean isRedirectFromFormSubmit = isFormSubmit && params.isRedirect();
            if (isRedirectFromFormSubmit) {
                RecordHistogram.recordBooleanHistogram(
                        "Android.Intent.LaunchExternalAppFormSubmitHasUserGesture",
                        params.hasUserGesture());
            }
        } else {
            result = handleFallbackUrl(params, targetIntent, browserFallbackUrl,
                    canLaunchExternalFallbackResult.get());
        }
        if (debug()) printDebugShouldOverrideUrlLoadingResultType(result);
        if (isIntentUrl) captureIntentSchemeMetrics(result, browserFallbackUrl);

        return result;
    }

    private OverrideUrlLoadingResult handleFallbackUrl(ExternalNavigationParams params,
            Intent targetIntent, GURL browserFallbackUrl, boolean canLaunchExternalFallback) {
        if (browserFallbackUrl.isEmpty()
                || (params.getRedirectHandler() != null
                        && params.getRedirectHandler().isOnNavigation()
                        // For instance, if this is a chained fallback URL, we ignore it.
                        && params.getRedirectHandler().shouldNotOverrideUrlLoading())) {
            return OverrideUrlLoadingResult.forNoOverride();
        }

        if (isIntentToInstantApp(targetIntent)) {
            RecordHistogram.recordEnumeratedHistogram("Android.InstantApps.DirectInstantAppsIntent",
                    AiaIntent.FALLBACK_USED, AiaIntent.NUM_ENTRIES);
        }

        if (canLaunchExternalFallback) {
            if (shouldBlockAllExternalAppLaunches(params, isIncomingIntentRedirect(params))) {
                throw new SecurityException("Context is not allowed to launch an external app.");
            }
            if (!params.isIncognito()) {
                // Launch WebAPK if it can handle the URL.
                try {
                    Intent intent =
                            Intent.parseUri(browserFallbackUrl.getSpec(), Intent.URI_INTENT_SCHEME);
                    sanitizeQueryIntentActivitiesIntent(intent);
                    QueryIntentActivitiesSupplier supplier =
                            new QueryIntentActivitiesSupplier(intent);
                    if (!isAlreadyInTargetWebApk(supplier, params)
                            && launchWebApkIfSoleIntentHandler(supplier, intent, params)) {
                        return OverrideUrlLoadingResult.forExternalFallbackUrl();
                    }
                } catch (Exception e) {
                    if (debug()) Log.i(TAG, "Could not parse fallback url as intent");
                }
            }

            // If the fallback URL is a link to Play Store, send the user to Play Store app
            // instead: crbug.com/638672.
            Pair<String, String> appInfo = maybeGetPlayStoreAppIdAndReferrer(browserFallbackUrl);
            if (appInfo != null) {
                String marketReferrer = TextUtils.isEmpty(appInfo.second)
                        ? ContextUtils.getApplicationContext().getPackageName()
                        : appInfo.second;
                OverrideUrlLoadingResult result = sendIntentToMarket(
                        appInfo.first, marketReferrer, params, browserFallbackUrl);
                if (result.getResultType()
                        == OverrideUrlLoadingResultType.OVERRIDE_WITH_EXTERNAL_INTENT) {
                    result = OverrideUrlLoadingResult.forExternalFallbackUrl();
                }
                return result;
            }
        }

        // For subframes, we don't support fallback url for now. If we ever do implement this, be
        // careful to prevent sandbox escapes.
        // http://crbug.com/364522.
        if (!params.isMainFrame()) {
            if (debug()) Log.i(TAG, "Don't support fallback url in subframes");
            return OverrideUrlLoadingResult.forNoOverride();
        }

        // NOTE: any further redirection from fall-back URL should not override URL loading.
        // Otherwise, it can be used in chain for fingerprinting multiple app installation
        // status in one shot. In order to prevent this scenario, we notify redirection
        // handler that redirection from the current navigation should stay in this app.
        if (params.getRedirectHandler() != null && params.getRedirectHandler().isOnNavigation()
                && !params.getRedirectHandler()
                            .getAndClearShouldNotBlockOverrideUrlLoadingOnCurrentRedirectionChain()) {
            params.getRedirectHandler().setShouldNotOverrideUrlLoadingOnCurrentRedirectChain();
        }
        if (debug()) Log.i(TAG, "clobberCurrentTab called");
        return clobberCurrentTab(browserFallbackUrl, params.getReferrerUrl(),
                params.getInitiatorOrigin(), params.isRendererInitiated());
    }

    private void printDebugShouldOverrideUrlLoadingResultType(OverrideUrlLoadingResult result) {
        String resultString;
        switch (result.getResultType()) {
            case OverrideUrlLoadingResultType.OVERRIDE_WITH_EXTERNAL_INTENT:
                resultString = "OVERRIDE_WITH_EXTERNAL_INTENT";
                break;
            case OverrideUrlLoadingResultType.OVERRIDE_WITH_CLOBBERING_TAB:
                resultString = "OVERRIDE_WITH_CLOBBERING_TAB";
                break;
            case OverrideUrlLoadingResultType.OVERRIDE_WITH_ASYNC_ACTION:
                resultString = "OVERRIDE_WITH_ASYNC_ACTION";
                break;
            case OverrideUrlLoadingResultType.NO_OVERRIDE: // Fall through.
            default:
                resultString = "NO_OVERRIDE";
                break;
        }
        Log.i(TAG, "shouldOverrideUrlLoading result: " + resultString);
    }

    private void captureIntentSchemeMetrics(
            OverrideUrlLoadingResult result, GURL browserFallbackUrl) {
        @IntentUriNavigationResult
        int value = IntentUriNavigationResult.NO_FALLBACK_NO_OVERRIDE;
        if (browserFallbackUrl.isEmpty()) {
            switch (result.getResultType()) {
                case OverrideUrlLoadingResultType.OVERRIDE_WITH_EXTERNAL_INTENT:
                    value = IntentUriNavigationResult.NO_FALLBACK_LAUNCHED_INTENT;
                    break;
                case OverrideUrlLoadingResultType.OVERRIDE_WITH_ASYNC_ACTION:
                    value = IntentUriNavigationResult.NO_FALLBACK_ASYNC_RESULT;
                    break;
                case OverrideUrlLoadingResultType.NO_OVERRIDE:
                    value = IntentUriNavigationResult.NO_FALLBACK_NO_OVERRIDE;
                    break;
                case OverrideUrlLoadingResultType.OVERRIDE_WITH_CLOBBERING_TAB:
                    // Quirk of incognito intent scheme URLs synchronously clobbering the tab with
                    // the target URL when the dialog can't be shown.
                    value = IntentUriNavigationResult.NO_FALLBACK_NO_OVERRIDE;
                    break;
                default:
                    assert false;
                    break;
            }
        } else {
            switch (result.getResultType()) {
                case OverrideUrlLoadingResultType.OVERRIDE_WITH_EXTERNAL_INTENT:
                    if (result.wasExternalFallbackUrlLaunch()) {
                        value = IntentUriNavigationResult.WITH_FALLBACK_USED_FALLBACK;
                    } else {
                        value = IntentUriNavigationResult.WITH_FALLBACK_LAUNCHED_INTENT;
                    }
                    break;
                case OverrideUrlLoadingResultType.OVERRIDE_WITH_ASYNC_ACTION:
                    value = IntentUriNavigationResult.WITH_FALLBACK_ASYNC_RESULT;
                    break;
                case OverrideUrlLoadingResultType.NO_OVERRIDE:
                    value = IntentUriNavigationResult.WITH_FALLBACK_NO_OVERRIDE;
                    break;
                case OverrideUrlLoadingResultType.OVERRIDE_WITH_CLOBBERING_TAB:
                    value = IntentUriNavigationResult.WITH_FALLBACK_USED_FALLBACK;
                    break;
                default:
                    assert false;
                    break;
            }
        }

        RecordHistogram.recordEnumeratedHistogram(
                INTENT_URI_RESULT_NAME, value, IntentUriNavigationResult.NUM_ENTRIES);
    }

    private boolean resolversSubsetOf(List<ResolveInfo> infos, List<ResolveInfo> container) {
        if (container == null) return false;
        HashSet<ComponentName> containerSet = new HashSet<>();
        for (ResolveInfo info : container) {
            containerSet.add(
                    new ComponentName(info.activityInfo.packageName, info.activityInfo.name));
        }
        for (ResolveInfo info : infos) {
            if (!containerSet.contains(
                        new ComponentName(info.activityInfo.packageName, info.activityInfo.name))) {
                return false;
            }
        }
        return true;
    }

    /**
     * https://crbug.com/1094442: Don't allow any external navigation on subframe navigations
     * without a user gesture (eg. initial ad frame navigation).
     */
    private boolean shouldBlockSubframeAppLaunches(ExternalNavigationParams params) {
        if (!params.isMainFrame() && !params.hasUserGesture()) {
            if (debug()) Log.i(TAG, "Subframe navigation without user gesture.");
            return true;
        }
        return false;
    }

    /**
     * http://crbug.com/441284 : Disallow firing external intent while the app is in the background.
     */
    private boolean blockExternalNavWhileBackgrounded(
            ExternalNavigationParams params, boolean incomingIntentRedirect) {
        // If the redirect is from an intent Chrome could still be transitioning to the foreground.
        // Alternatively, the user may have sent Chrome to the background by this point, but for
        // navigations started by another app that should still be safe.
        if (incomingIntentRedirect) return false;
        if (params.isApplicationMustBeInForeground() && !mDelegate.isApplicationInForeground()) {
            if (debug()) Log.i(TAG, "App is not in foreground");
            return true;
        }
        return false;
    }

    /** http://crbug.com/464669 : Disallow firing external intent from background tab. */
    private boolean blockExternalNavFromBackgroundTab(
            ExternalNavigationParams params, boolean incomingIntentRedirect) {
        // See #blockExternalNavWhileBackgrounded - isBackgroundTabNavigation is effectively
        // checking both that the tab is foreground, and the app is foreground, so we can skip it
        // for intent launches for the same reason.
        if (incomingIntentRedirect) return false;
        if (params.isBackgroundTabNavigation()
                && !params.areIntentLaunchesAllowedInBackgroundTabs()) {
            if (debug()) Log.i(TAG, "Navigation in background tab");
            return true;
        }
        return false;
    }

    /**
     * http://crbug.com/164194 . A navigation forwards or backwards should never trigger the intent
     * picker.
     */
    private boolean ignoreBackForwardNav(ExternalNavigationParams params) {
        if ((params.getPageTransition() & PageTransition.FORWARD_BACK) != 0) {
            if (debug()) Log.i(TAG, "Forward or back navigation");
            return true;
        }
        return false;
    }

    /** http://crbug.com/605302 : Allow embedders to handle all pdf file downloads. */
    private boolean isInternalPdfDownload(
            boolean isExternalProtocol, ExternalNavigationParams params) {
        if (!isExternalProtocol && isPdfDownload(params.getUrl())) {
            if (debug()) Log.i(TAG, "PDF downloads are now handled internally");
            return true;
        }
        return false;
    }

    /**
     * If accessing a file URL, ensure that the user has granted the necessary file access
     * to the app.
     */
    private boolean handleFileUrlPermissions(ExternalNavigationParams params) {
        if (!params.getUrl().getScheme().equals(UrlConstants.FILE_SCHEME)) return false;

        @MimeTypeUtils.Type
        int mimeType = MimeTypeUtils.getMimeTypeForUrl(params.getUrl());
        RecordHistogram.recordEnumeratedHistogram(
                "Android.Intent.OpenFileType", mimeType, MimeTypeUtils.NUM_MIME_TYPE_ENTRIES);
        String permissionNeeded = MimeTypeUtils.getPermissionNameForMimeType(mimeType);

        if (permissionNeeded == null) return false;

        if (!shouldRequestFileAccess(params.getUrl(), permissionNeeded)) return false;
        requestFilePermissions(params, permissionNeeded);
        if (debug()) Log.i(TAG, "Requesting filesystem access");
        return true;
    }

    /**
     * Trigger a UI affordance that will ask the user to grant file access.  After the access
     * has been granted or denied, continue loading the specified file URL.
     *
     * @param params The {@link ExternalNavigationParams} for the navigation.
     * @param permissionNeeded The name of the Android permission needed to access the file.
     */
    @VisibleForTesting
    protected void requestFilePermissions(
            ExternalNavigationParams params, String permissionNeeded) {
        PermissionCallback permissionCallback = new PermissionCallback() {
            @Override
            public void onRequestPermissionsResult(String[] permissions, int[] grantResults) {
                if (grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED
                        && mDelegate.hasValidTab()) {
                    if (params.isMainFrame()
                            && params.getAsyncActionTakenInMainFrameCallback() != null) {
                        params.getAsyncActionTakenInMainFrameCallback().onResult(
                                new ExternalNavigationParams.AsyncActionTakenParams(
                                        false, true, params));
                    }
                    clobberCurrentTab(params.getUrl(), params.getReferrerUrl(),
                            params.getInitiatorOrigin(), params.isRendererInitiated());
                } else {
                    // TODO(tedchoc): Show an indication to the user that the navigation failed
                    //                instead of silently dropping it on the floor.
                    if (params.isMainFrame()
                            && params.getAsyncActionTakenInMainFrameCallback() != null) {
                        params.getAsyncActionTakenInMainFrameCallback().onResult(
                                new ExternalNavigationParams.AsyncActionTakenParams(
                                        true, false, params));
                    }
                }
            }
        };
        if (!mDelegate.hasValidTab()) return;
        mDelegate.getWindowAndroid().requestPermissions(
                new String[] {permissionNeeded}, permissionCallback);
    }

    /**
     * Clobber the current tab and try not to pass an intent when it should be handled internally
     * so that we can deliver HTTP referrer information safely.
     *
     * @param url The new URL after clobbering the current tab.
     * @param referrerUrl The HTTP referrer URL.
     * @param initiatorOrigin The Origin the navigation intiated from.
     * @return OverrideUrlLoadingResultType (if the tab has been clobbered, or we're launching an
     *         intent.)
     */
    @VisibleForTesting
    protected OverrideUrlLoadingResult clobberCurrentTab(
            GURL url, GURL referrerUrl, Origin initiatorOrigin, boolean isRendererInitiated) {
        int transitionType = PageTransition.LINK;
        final LoadUrlParams loadUrlParams = new LoadUrlParams(url, transitionType);
        if (!referrerUrl.isEmpty()) {
            Referrer referrer = new Referrer(referrerUrl.getSpec(), ReferrerPolicy.ALWAYS);
            loadUrlParams.setReferrer(referrer);
        }
        if (RedirectHandler.isRefactoringEnabled()) {
            // This URL came from the renderer, so it should be seen as part of the current redirect
            // chain.
            loadUrlParams.setIsRendererInitiated(isRendererInitiated);
            loadUrlParams.setInitiatorOrigin(initiatorOrigin);
        }

        assert mDelegate.hasValidTab() : "clobberCurrentTab was called with an empty tab.";
        // Loading URL will start a new navigation which cancels the current one
        // that this clobbering is being done for. It leads to UAF. To avoid that,
        // we're loading URL asynchronously. See https://crbug.com/732260.
        PostTask.postTask(UiThreadTaskTraits.DEFAULT, new Runnable() {
            @Override
            public void run() {
                mDelegate.loadUrlIfPossible(loadUrlParams);
            }
        });
        return OverrideUrlLoadingResult.forClobberingTab();
    }

    private static void loadUrlWithReferrer(
            final GURL url, final GURL referrerUrl, ExternalNavigationDelegate delegate) {
        LoadUrlParams loadUrlParams = new LoadUrlParams(url, PageTransition.AUTO_TOPLEVEL);
        if (!referrerUrl.isEmpty()) {
            Referrer referrer = new Referrer(referrerUrl.getSpec(), ReferrerPolicy.ALWAYS);
            loadUrlParams.setReferrer(referrer);
        }
        delegate.loadUrlIfPossible(loadUrlParams);
    }

    private boolean isTypedRedirectToExternalProtocol(
            ExternalNavigationParams params, int pageTransitionCore, boolean isExternalProtocol) {
        boolean isTyped = (pageTransitionCore == PageTransition.TYPED)
                || ((params.getPageTransition() & PageTransition.FROM_ADDRESS_BAR) != 0);
        return isTyped && params.isRedirect() && isExternalProtocol;
    }

    private boolean redirectShouldStayInApp(ExternalNavigationParams params,
            boolean isExternalProtocol, Intent targetIntent,
            QueryIntentActivitiesSupplier resolvingInfos) {
        if (RedirectHandler.isRefactoringEnabled()) return false;

        RedirectHandler handler = params.getRedirectHandler();
        if (handler == null) return false;
        boolean shouldStayInApp = handler.shouldStayInApp(isExternalProtocol,
                mDelegate.isIntentForTrustedCallingApp(targetIntent, resolvingInfos));
        if (shouldStayInApp || handler.shouldNotOverrideUrlLoading()) {
            if (isExternalProtocol) handler.maybeLogExternalRedirectBlockedWithMissingGesture();
            if (debug()) Log.i(TAG, "RedirectHandler decision");
            return true;
        }
        return false;
    }

    // https://crbug.com/1232514: On Android S, since WebAPKs aren't verified apps they are
    // never launched as the result of a suitable Intent, the user's default browser will be
    // opened instead. As a temporary solution, have Chrome launch the WebAPK.
    //
    // Note that we also need to query for non-default handlers as WebApks being non-default
    // Web Intent handlers is the cause of the issue.
    private boolean intentMatchesNonDefaultWebApk(
            ExternalNavigationParams params, QueryIntentActivitiesSupplier resolvingInfos) {
        if (params.isFromIntent() && mDelegate.shouldLaunchWebApksOnInitialIntent()) {
            String packageName = pickWebApkIfSoleIntentHandler(params, resolvingInfos);
            if (packageName != null) {
                if (debug()) Log.i(TAG, "Matches possibly non-default WebApk");
                return true;
            }
        }
        return false;
    }

    /**
     * http://crbug.com/149218: We want to show the intent picker for ordinary links, providing
     * the link is not an incoming intent from another application, unless it's a redirect.
     */
    private boolean preferToShowIntentPicker(ExternalNavigationParams params,
            boolean isExternalProtocol, boolean incomingIntentRedirect,
            boolean intentMatchesNonDefaultWebApk) {
        if (RedirectHandler.isRefactoringEnabled()) return true;

        int pageTransitionCore = params.getPageTransition() & PageTransition.CORE_MASK;
        boolean isFormSubmit = pageTransitionCore == PageTransition.FORM_SUBMIT;

        // Allow initial Intent to leave Chrome if it matched a non-Default WebApk.
        if (intentMatchesNonDefaultWebApk) return true;

        // http://crbug.com/169549 : If you type in a URL that then redirects in server side to a
        // link that cannot be rendered by the browser, we want to show the intent picker.
        if (isTypedRedirectToExternalProtocol(params, pageTransitionCore, isExternalProtocol)) {
            return true;
        }
        // http://crbug.com/181186: We need to show the intent picker when we receive a redirect
        // following a form submit.
        boolean isRedirectFromFormSubmit = isFormSubmit && params.isRedirect();

        // TODO(https://crbug.com/1300539): Historically this was intended to prevent
        // browser-initiated navigations like bookmarks from leaving Chrome. However, this is not a
        // security boundary as it can be trivially (and unintentionally) subverted, including by a
        // simple client (meta) redirect. We should decide whether or not external navigations
        // should be blocked from browser-initiated navigations and either enforce it consistently
        // or not enforce it at all.
        if (!params.isRendererInitiated() && !incomingIntentRedirect && !isRedirectFromFormSubmit
                && mDelegate.shouldEmbedderInitiatedNavigationsStayInBrowser()) {
            if (debug()) Log.i(TAG, "Browser or Intent initiated and not a redirect");
            return false;
        }

        if (isFormSubmit && !incomingIntentRedirect && !isRedirectFromFormSubmit) {
            if (debug()) Log.i(TAG, "Direct form submission, not a redirect");
            return false;
        }

        // http://crbug/331571 : Do not override a navigation started from user typing.
        if (params.getRedirectHandler() != null
                && params.getRedirectHandler().isNavigationFromUserTyping()) {
            if (debug()) Log.i(TAG, "Navigation from user typing");
            return false;
        }
        return true;
    }

    /**
     * http://crbug.com/159153: Don't override navigation from a chrome:* url to http or https. For
     * example when clicking a link in bookmarks or most visited. When navigating from such a page,
     * there is clear intent to complete the navigation in Chrome.
     */
    private boolean isLinkFromChromeInternalPage(ExternalNavigationParams params) {
        if (params.getReferrerUrl().getScheme().equals(UrlConstants.CHROME_SCHEME)
                && UrlUtilities.isHttpOrHttps(params.getUrl())) {
            if (debug()) Log.i(TAG, "Link from an internal chrome:// page");
            return true;
        }
        return false;
    }

    private static boolean isSupportedWtaiProtocol(GURL url) {
        return url.getSpec().startsWith(WTAI_MC_URL_PREFIX);
    }

    private static Intent parseWtaiMcProtocol(GURL url) {
        assert isSupportedWtaiProtocol(url);
        // wtai://wp/mc;number
        // number=string(phone-number)
        String phoneNumber = url.getSpec().substring(WTAI_MC_URL_PREFIX.length());
        if (debug()) Log.i(TAG, "wtai:// link handled");
        RecordUserAction.record("Android.PhoneIntent");
        return new Intent(Intent.ACTION_VIEW, Uri.parse(WebView.SCHEME_TEL + phoneNumber));
    }

    private static boolean isUnhandledWtaiProtocol(ExternalNavigationParams params) {
        if (!params.getUrl().getSpec().startsWith(WTAI_URL_PREFIX)) return false;
        if (isSupportedWtaiProtocol(params.getUrl())) return false;
        if (debug()) Log.i(TAG, "Unsupported wtai:// link");
        return true;
    }

    /**
     * The "about:", "chrome:", "chrome-native:", and "devtools:" schemes
     * are internal to the browser; don't want these to be dispatched to other apps.
     */
    private boolean hasInternalScheme(GURL targetUrl, Intent targetIntent) {
        if (isInternalScheme(targetUrl.getScheme())) {
            if (debug()) Log.i(TAG, "Navigating to a chrome-internal page");
            return true;
        }
        if (UrlUtilities.hasIntentScheme(targetUrl) && targetIntent.getData() != null
                && isInternalScheme(targetIntent.getData().getScheme())) {
            if (debug()) Log.i(TAG, "Navigating to a chrome-internal page");
            return true;
        }
        return false;
    }

    private static boolean isInternalScheme(String scheme) {
        if (TextUtils.isEmpty(scheme)) return false;
        return scheme.equals(ContentUrlConstants.ABOUT_SCHEME)
                || scheme.equals(UrlConstants.CHROME_SCHEME)
                || scheme.equals(UrlConstants.CHROME_NATIVE_SCHEME)
                || scheme.equals(UrlConstants.DEVTOOLS_SCHEME);
    }

    /**
     * The "content:" scheme is disabled in Clank. Do not try to start an external activity, or
     * load the URL in-browser.
     */
    private boolean hasContentScheme(GURL targetUrl, Intent targetIntent) {
        boolean hasContentScheme = false;
        if (UrlUtilities.hasIntentScheme(targetUrl) && targetIntent.getData() != null) {
            hasContentScheme =
                    UrlConstants.CONTENT_SCHEME.equals(targetIntent.getData().getScheme());
        } else {
            hasContentScheme = UrlConstants.CONTENT_SCHEME.equals(targetUrl.getScheme());
        }
        if (debug() && hasContentScheme) Log.i(TAG, "Navigation to content: URL");
        return hasContentScheme;
    }

    /**
     * Intent URIs leads to creating intents that chrome would use for firing external navigations
     * via Android. Android throws an exception [1] when an application exposes a file:// Uri to
     * another app.
     *
     * This method checks if the |targetIntent| contains the file:// scheme in its data.
     *
     * [1]: https://developer.android.com/reference/android/os/FileUriExposedException
     */
    private boolean hasFileSchemeInIntentURI(GURL targetUrl, Intent targetIntent) {
        // We are only concerned with targetIntent that was generated due to intent:// schemes only.
        if (!UrlUtilities.hasIntentScheme(targetUrl)) return false;

        Uri data = targetIntent.getData();

        if (data == null || data.getScheme() == null) return false;

        if (data.getScheme().equalsIgnoreCase(UrlConstants.FILE_SCHEME)) {
            if (debug()) Log.i(TAG, "Intent navigation to file: URI");
            return true;
        }
        return false;
    }

    /**
     * Special case - It makes no sense to use an external application for a YouTube
     * pairing code URL, since these match the current tab with a device (Chromecast
     * or similar) it is supposed to be controlling. Using a different application
     * that isn't expecting this (in particular YouTube) doesn't work.
     */
    @VisibleForTesting
    protected boolean isYoutubePairingCode(GURL url) {
        if (url.domainIs("youtube.com")
                && !TextUtils.isEmpty(UrlUtilities.getValueForKeyInQuery(url, "pairingCode"))) {
            if (debug()) Log.i(TAG, "YouTube URL with a pairing code");
            return true;
        }
        return false;
    }

    private boolean externalIntentRequestsDisabledForUrl(ExternalNavigationParams params) {
        // TODO(changwan): check if we need to handle URL even when external intent is off.
        if (CommandLine.getInstance().hasSwitch(
                    ExternalIntentsSwitches.DISABLE_EXTERNAL_INTENT_REQUESTS)) {
            Log.w(TAG, "External intent handling is disabled by a command-line flag.");
            return true;
        }

        if (mDelegate.shouldDisableExternalIntentRequestsForUrl(params.getUrl())) {
            if (debug()) Log.i(TAG, "Delegate disables external intent requests for URL.");
            return true;
        }
        return false;
    }

    /**
     * See RedirectHandler#NAVIGATION_CHAIN_TIMEOUT_MILLIS for details. We don't want an unattended
     * page to redirect to an app.
     */
    private boolean isNavigationChainExpired(ExternalNavigationParams params, Intent targetIntent,
            QueryIntentActivitiesSupplier resolvingInfos) {
        if (RedirectHandler.isRefactoringEnabled()) return false;
        if (mDelegate.isIntentForTrustedCallingApp(targetIntent, resolvingInfos)) return false;
        if (params.getRedirectHandler() != null
                && params.getRedirectHandler().isNavigationChainExpired()) {
            if (debug()) {
                Log.i(TAG,
                        "Navigation chain expired "
                                + "(a page waited more than %d seconds to redirect).",
                        RedirectHandler.NAVIGATION_CHAIN_TIMEOUT_MILLIS);
            }
            return true;
        }
        return false;
    }

    /**
     * @return whether something along the navigation chain prevents the current navigation from
     * leaving Chrome.
     */
    private boolean navigationChainBlocksExternalNavigation(ExternalNavigationParams params,
            Intent targetIntent, QueryIntentActivitiesSupplier resolvingInfos,
            boolean isExternalProtocol) {
        if (!RedirectHandler.isRefactoringEnabled()) return false;

        RedirectHandler handler = params.getRedirectHandler();
        if (handler == null) return false;
        RedirectHandler.InitialNavigationState initialState = handler.getInitialNavigationState();

        // If a navigation chain has used the history API to go back/forward external navigation is
        // probably not expected or desirable.
        if (handler.navigationChainUsedBackOrForward()) {
            if (debug()) Log.i(TAG, "Navigation chain used back or forward.");
            return true;
        }

        // Used to prevent things like chaining fallback URLs.
        if (handler.shouldNotOverrideUrlLoading()) {
            if (debug()) Log.i(TAG, "Navigation chain has blocked app launching.");
            return true;
        }

        // Tab Restores should definitely not launch apps, and refreshes launching apps would
        // probably not be expected or desirable.
        if (initialState.isFromReload) {
            if (debug()) Log.i(TAG, "Navigation chain is from a tab restore or refresh.");
            return true;
        }

        // TODO(https://crbug.com/1346731): We only need to check isFromTyping because WebLayer's
        // implementation of disabling intent processing is broken and doesn't actually disable
        // intent processing, but to align with current weblayer behavior the first navigation has
        // to be blocked even if the weblayer delegate tells us not to block embedder initiated
        // navigations. See
        // https://source.chromium.org/chromium/chromium/src/+/main:weblayer/browser/navigation_controller_impl.cc;drc=88d7b2e74349cbf8b3e15b61cc0663d65f9d1873;l=220
        if (!initialState.isRendererInitiated && !initialState.isFromIntent
                && (mDelegate.shouldEmbedderInitiatedNavigationsStayInBrowser()
                        || initialState.isFromTyping)) {
            if (debug()) Log.i(TAG, "Browser initiated navigation chain.");
            return true;
        }

        // If the intent targets the calling app, we can bypass the gesture requirements and any
        // signals from the initial intent that suggested the intent wanted to stay in Chrome.
        if (mDelegate.isIntentForTrustedCallingApp(targetIntent, resolvingInfos)) return false;

        // See RedirectHandler#NAVIGATION_CHAIN_TIMEOUT_MILLIS for details. We don't want an
        // unattended page to redirect to an app.
        if (handler.isNavigationChainExpired()) {
            if (debug()) {
                Log.i(TAG,
                        "Navigation chain expired "
                                + "(a page waited more than %d seconds to redirect).",
                        RedirectHandler.NAVIGATION_CHAIN_TIMEOUT_MILLIS);
            }
            return true;
        }

        // If an intent targeted Chrome explicitly, we assume the app wanted to launch Chrome and
        // not another app.
        if (handler.intentPrefersToStayInChrome() && !isExternalProtocol) {
            if (debug()) Log.i(TAG, "Launching intent explicitly targeted the browser.");
            return true;
        }

        // Ensure the navigation was started with a user gesture so that inactive pages can't launch
        // apps unexpectedly, unless we trust the calling app for a CCT/TWA.
        // TODO(https://crbug.com/839751): Remove gesture exception for form submits.
        if (initialState.isRendererInitiated && !initialState.hasUserGesture
                && !initialState.isFromFormSubmit) {
            if (isExternalProtocol) handler.maybeLogExternalRedirectBlockedWithMissingGesture();
            if (debug()) Log.i(TAG, "Navigation chain started without a gesture.");
            return true;
        }
        return false;
    }

    /**
     * If a site is submitting a form, it most likely wants to submit that data to a server rather
     * than launch an app.
     */
    private boolean isDirectFormSubmit(
            ExternalNavigationParams params, boolean isExternalProtocol) {
        // If a form is submitting to an external protocol, don't block it.
        if (isExternalProtocol) return false;

        // Redirects off of form submits need to be able to launch apps.
        if (params.isRedirect()) return false;

        int pageTransitionCore = params.getPageTransition() & PageTransition.CORE_MASK;
        boolean isFormSubmit = pageTransitionCore == PageTransition.FORM_SUBMIT;
        if (isFormSubmit) {
            if (debug()) Log.i(TAG, "Direct form submission, not a redirect");
            return true;
        }
        return false;
    }

    /*
     * The initial navigation from an Intent should always stay in the browser as the sending app,
     * or the user must have chosen the browser to do the navigation.
     */
    private boolean isDirectIntentNavigation(ExternalNavigationParams params,
            boolean intentMatchesNonDefaultWebApk, boolean incomingIntentRedirect) {
        if (!RedirectHandler.isRefactoringEnabled()) return false;

        // S+ workaround for WebAPKs not being able to handle Intents.
        if (intentMatchesNonDefaultWebApk) return false;

        if (!params.isFromIntent()) return false;

        // Redirects off of intents are still allowed to launch apps (eg. URL shorteners).
        if (incomingIntentRedirect) return false;

        if (debug()) Log.i(TAG, "Initial intent navigation.");
        return true;
    }

    /**
     * If the intent can't be resolved, we should fall back to the browserFallbackUrl, or try to
     * find the app on the market if no fallback is provided.
     */
    private OverrideUrlLoadingResult handleUnresolvableIntent(ExternalNavigationParams params,
            Intent targetIntent, GURL browserFallbackUrl, boolean requiresPromptForExternalIntent) {
        if (requiresPromptForExternalIntent) return OverrideUrlLoadingResult.forNoOverride();
        // Fallback URL will be handled by the caller of shouldOverrideUrlLoadingInternal.
        if (!browserFallbackUrl.isEmpty()) return OverrideUrlLoadingResult.forNoOverride();
        if (targetIntent.getPackage() != null) {
            return handleWithMarketIntent(params, targetIntent);
        }

        if (debug()) Log.i(TAG, "Could not find an external activity to use");
        return OverrideUrlLoadingResult.forNoOverride();
    }

    private OverrideUrlLoadingResult handleWithMarketIntent(
            ExternalNavigationParams params, Intent intent) {
        String marketReferrer = IntentUtils.safeGetStringExtra(intent, EXTRA_MARKET_REFERRER);
        if (TextUtils.isEmpty(marketReferrer)) {
            marketReferrer = ContextUtils.getApplicationContext().getPackageName();
        }
        return sendIntentToMarket(intent.getPackage(), marketReferrer, params, GURL.emptyGURL());
    }

    private boolean maybeSetSmsPackage(Intent targetIntent) {
        final Uri uri = targetIntent.getData();
        if (targetIntent.getPackage() == null && uri != null
                && UrlConstants.SMS_SCHEME.equals(uri.getScheme())) {
            List<ResolveInfo> resolvingInfos = queryIntentActivities(targetIntent);
            targetIntent.setPackage(getDefaultSmsPackageName(resolvingInfos));
            return true;
        }
        return false;
    }

    private void maybeRecordPhoneIntentMetrics(Intent targetIntent) {
        final Uri uri = targetIntent.getData();
        if (uri != null && UrlConstants.TEL_SCHEME.equals(uri.getScheme())
                || (Intent.ACTION_DIAL.equals(targetIntent.getAction()))
                || (Intent.ACTION_CALL.equals(targetIntent.getAction()))) {
            RecordUserAction.record("Android.PhoneIntent");
        }
    }

    /**
     * In incognito mode, links that can be handled within the browser should just do so,
     * without asking the user.
     */
    private boolean shouldStayInIncognito(
            ExternalNavigationParams params, boolean isExternalProtocol) {
        if (params.isIncognito() && !isExternalProtocol) {
            if (debug()) Log.i(TAG, "Stay incognito");
            return true;
        }
        return false;
    }

    private boolean fallBackToHandlingWithInstantApp(ExternalNavigationParams params,
            boolean incomingIntentRedirect, QueryIntentActivitiesSupplier resolveInfos,
            boolean requiresPromptForExternalIntent) {
        if (requiresPromptForExternalIntent) return false;
        if (params.isIncognito()) return false;
        if (mDelegate.maybeLaunchInstantApp(params.getUrl(), params.getReferrerUrl(),
                    incomingIntentRedirect, isSerpReferrer(), resolveInfos)) {
            if (debug()) Log.i(TAG, "Launching instant App.");
            return true;
        }
        return false;
    }

    /**
     * This is the catch-all path for any intent that the app can handle that doesn't have a
     * specialized external app handling it.
     */
    private OverrideUrlLoadingResult fallBackToHandlingInApp() {
        if (debug()) Log.i(TAG, "No specialized handler for URL");
        return OverrideUrlLoadingResult.forNoOverride();
    }

    /**
     * Returns true if the intent is an insecure intent targeting browsers or browser-like apps
     * (excluding the embedding app).
     */
    private boolean isInsecureIntentToOtherBrowser(Intent targetIntent,
            QueryIntentActivitiesSupplier resolveInfos, boolean isIntentWithSupportedProtocol,
            ResolveActivitySupplier resolveActivity, boolean intentHasExtras) {
        // If an intent has Extras or a data URI it may be used to launch arbitrary URIs in insecure
        // browsers.
        if (!intentHasExtras
                && (targetIntent.getData() == null || targetIntent.getData().equals(Uri.EMPTY))) {
            return false;
        }

        if (targetIntent.getPackage() != null
                && targetIntent.getPackage().equals(
                        ContextUtils.getApplicationContext().getPackageName())) {
            return false;
        }

        String selfPackageName = mDelegate.getContext().getPackageName();
        boolean matchesOtherPackage = false;
        for (ResolveInfo resolveInfo : resolveInfos.get()) {
            ActivityInfo info = resolveInfo.activityInfo;
            if (info == null || !selfPackageName.equals(info.packageName)) {
                matchesOtherPackage = true;
                break;
            }
        }
        if (!matchesOtherPackage) return false;

        // Querying for browser packages will catch Intents that use custom URL schemes like
        // googlechrome:// or are otherwise not considered by Android to be Web intents but can
        // still load arbitrary URLs in a browser.
        Set<String> browserPackages = getInstalledBrowserPackages();

        boolean matchesBrowser = false;
        for (ResolveInfo resolveInfo : resolveInfos.get()) {
            ActivityInfo info = resolveInfo.activityInfo;
            if (info != null && browserPackages.contains(info.packageName)) {
                matchesBrowser = true;
                break;
            }
        }
        if (!matchesBrowser) return false;
        if (resolveActivity.get().activityInfo == null) return false;

        // If the intent resolves to a non-browser even through a browser is included in
        // queryIntentActivities, it's not really targeting a browser.
        return browserPackages.contains(resolveActivity.get().activityInfo.packageName);
    }

    private static Set<String> getInstalledBrowserPackages() {
        List<ResolveInfo> browsers = PackageManagerUtils.queryAllWebBrowsersInfo();

        Set<String> packageNames = new HashSet<>();
        for (ResolveInfo browser : browsers) {
            if (browser.activityInfo == null) continue;
            packageNames.add(browser.activityInfo.packageName);
        }
        return packageNames;
    }

    /**
     * Current URL has at least one specialized handler available. For navigations
     * within the same host, keep the navigation inside the browser unless the set of
     * available apps to handle the new navigation is different. http://crbug.com/463138
     */
    private boolean shouldStayWithinHost(ExternalNavigationParams params,
            List<ResolveInfo> resolvingInfos, boolean isExternalProtocol) {
        if (isExternalProtocol || !params.isRendererInitiated()) return false;

        GURL previousUrl = getLastCommittedUrl();
        if (previousUrl == null) previousUrl = params.getReferrerUrl();
        if (previousUrl.isEmpty()) return false;

        GURL currentUrl = params.getUrl();

        if (!TextUtils.equals(currentUrl.getHost(), previousUrl.getHost())) {
            return false;
        }

        Intent previousIntent = new Intent(Intent.ACTION_VIEW);
        previousIntent.setData(Uri.parse(previousUrl.getSpec()));

        if (resolversSubsetOf(resolvingInfos, queryIntentActivities(previousIntent))) {
            if (debug()) Log.i(TAG, "Same host, no new resolvers");
            return true;
        }
        return false;
    }

    /**
     * For security reasons, we disable all intent:// URLs to Instant Apps that are not coming from
     * SERP.
     */
    private boolean preventDirectInstantAppsIntent(Intent intent) {
        if (!isIntentToInstantApp(intent)) return false;
        if (isSerpReferrer() && mDelegate.handlesInstantAppLaunchingInternally()) return false;
        if (debug()) Log.i(TAG, "Intent URL to an Instant App");
        RecordHistogram.recordEnumeratedHistogram("Android.InstantApps.DirectInstantAppsIntent",
                AiaIntent.OTHER, AiaIntent.NUM_ENTRIES);
        return true;
    }

    /**
     * Prepare the intent to be sent. This function does not change the filtering for the intent,
     * so the list if resolveInfos for the intent will be the same before and after this function.
     */
    private void prepareExternalIntent(Intent targetIntent, ExternalNavigationParams params,
            List<ResolveInfo> resolvingInfos, boolean shouldProxyForInstantApps) {
        // Set the Browser application ID to us in case the user chooses this app
        // as the app.  This will make sure the link is opened in the same tab
        // instead of making a new one in the case of Chrome.
        targetIntent.putExtra(Browser.EXTRA_APPLICATION_ID,
                ContextUtils.getApplicationContext().getPackageName());
        if (params.isOpenInNewTab()) targetIntent.putExtra(Browser.EXTRA_CREATE_NEW_TAB, true);
        targetIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        // Ensure intents re-target potential caller activity when we run in CCT mode.
        targetIntent.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP);
        mDelegate.maybeSetWindowId(targetIntent);
        targetIntent.putExtra(EXTRA_EXTERNAL_NAV_PACKAGES, getSpecializedHandlers(resolvingInfos));

        if (!params.getReferrerUrl().isEmpty()) {
            mDelegate.maybeSetPendingReferrer(targetIntent, params.getReferrerUrl());
        }

        if (params.isIncognito()) mDelegate.maybeSetPendingIncognitoUrl(targetIntent);

        mDelegate.maybeAdjustInstantAppExtras(targetIntent, shouldProxyForInstantApps);

        if (shouldProxyForInstantApps) {
            RecordHistogram.recordEnumeratedHistogram("Android.InstantApps.DirectInstantAppsIntent",
                    AiaIntent.SERP, AiaIntent.NUM_ENTRIES);
        }

        mDelegate.maybeSetRequestMetadata(targetIntent, params.hasUserGesture(),
                params.isRendererInitiated(), params.getInitiatorOrigin());
    }

    private OverrideUrlLoadingResult handleExternalIncognitoIntent(Intent targetIntent,
            ExternalNavigationParams params, GURL browserFallbackUrl,
            boolean shouldProxyForInstantApps) {
        // This intent may leave this app. Warn the user that incognito does not carry over
        // to external apps.
        if (startIncognitoIntent(
                    params, targetIntent, browserFallbackUrl, shouldProxyForInstantApps)) {
            if (debug()) Log.i(TAG, "Incognito navigation out");
            return OverrideUrlLoadingResult.forAsyncAction(
                    OverrideUrlLoadingAsyncActionType.UI_GATING_INTENT_LAUNCH);
        }
        if (debug()) Log.i(TAG, "Failed to show incognito alert dialog.");
        return OverrideUrlLoadingResult.forNoOverride();
    }

    /**
     * Display a dialog warning the user that they may be leaving this app by starting this
     * intent. Give the user the opportunity to cancel the action. And if it is canceled, a
     * navigation will happen in this app. Catches BadTokenExceptions caused by showing the dialog
     * on certain devices. (crbug.com/782602)
     * @param intent The intent for external application that will be sent.
     * @param referrerUrl The referrer for the current navigation.
     * @param fallbackUrl The URL to load if the user doesn't proceed with external intent.
     * @param proxy Whether we need to proxy the intent through AuthenticatedProxyActivity (this is
     *              used by Instant Apps intents.
     * @return True if the function returned error free, false if it threw an exception.
     */
    private boolean startIncognitoIntent(
            ExternalNavigationParams params, Intent intent, GURL fallbackUrl, boolean proxy) {
        Context context = mDelegate.getContext();
        if (!canLaunchIncognitoIntent(intent, context)) return false;

        if (mDelegate.hasCustomLeavingIncognitoDialog()) {
            mDelegate.presentLeavingIncognitoModalDialog(shouldLaunch -> {
                onUserDecidedWhetherToLaunchIncognitoIntent(
                        shouldLaunch.booleanValue(), params, intent, fallbackUrl, proxy);
            });

            return true;
        }

        try {
            AlertDialog dialog =
                    showLeavingIncognitoAlert(context, params, intent, fallbackUrl, proxy);
            return dialog != null;
        } catch (BadTokenException e) {
            return false;
        }
    }

    @VisibleForTesting
    protected boolean canLaunchIncognitoIntent(Intent intent, Context context) {
        if (!mDelegate.hasValidTab()) return false;
        if (ContextUtils.activityFromContext(context) == null) return false;
        return true;
    }

    /**
     * Shows and returns an AlertDialog asking if the user would like to leave incognito.
     */
    @VisibleForTesting
    protected AlertDialog showLeavingIncognitoAlert(final Context context,
            final ExternalNavigationParams params, final Intent intent, final GURL fallbackUrl,
            final boolean proxy) {
        return new AlertDialog.Builder(context, R.style.ThemeOverlay_BrowserUI_AlertDialog)
                .setTitle(R.string.external_app_leave_incognito_warning_title)
                .setMessage(R.string.external_app_leave_incognito_warning)
                .setPositiveButton(R.string.external_app_leave_incognito_leave,
                        new OnClickListener() {
                            @Override
                            public void onClick(DialogInterface dialog, int which) {
                                onUserDecidedWhetherToLaunchIncognitoIntent(
                                        /*shouldLaunch=*/true, params, intent, fallbackUrl, proxy);
                            }
                        })
                .setNegativeButton(R.string.external_app_leave_incognito_stay,
                        new OnClickListener() {
                            @Override
                            public void onClick(DialogInterface dialog, int which) {
                                onUserDecidedWhetherToLaunchIncognitoIntent(
                                        /*shouldLaunch=*/false, params, intent, fallbackUrl, proxy);
                            }
                        })
                .setOnCancelListener(new OnCancelListener() {
                    @Override
                    public void onCancel(DialogInterface dialog) {
                        onUserDecidedWhetherToLaunchIncognitoIntent(
                                /*shouldLaunch=*/false, params, intent, fallbackUrl, proxy);
                    }
                })
                .show();
    }

    private void onUserDecidedWhetherToLaunchIncognitoIntent(final boolean shouldLaunch,
            final ExternalNavigationParams params, final Intent intent, final GURL fallbackUrl,
            final boolean proxy) {
        if (shouldLaunch) {
            try {
                startActivity(intent, proxy);
                if (params.isMainFrame()
                        && params.getAsyncActionTakenInMainFrameCallback() != null) {
                    params.getAsyncActionTakenInMainFrameCallback().onResult(
                            new ExternalNavigationParams.AsyncActionTakenParams(
                                    mDelegate.canCloseTabOnIncognitoIntentLaunch(), false, params));
                }
                return;
            } catch (ActivityNotFoundException e) {
                // The activity that we thought was going to handle the intent
                // no longer exists, so catch the exception and fall through to handling the
                // fallback URL.
            }
        }

        OverrideUrlLoadingResult result = handleFallbackUrl(params, intent, fallbackUrl, false);
        if (params.isMainFrame() && params.getAsyncActionTakenInMainFrameCallback() != null) {
            if (result.getResultType() == OverrideUrlLoadingResultType.NO_OVERRIDE) {
                // There was no fallback URL and we can't handle the URL the intent was targeting.
                // In this case we'll return to the last committed URL.
                params.getAsyncActionTakenInMainFrameCallback().onResult(
                        new ExternalNavigationParams.AsyncActionTakenParams(false, false, params));
            } else {
                assert result.getResultType()
                        == OverrideUrlLoadingResultType.OVERRIDE_WITH_CLOBBERING_TAB;
                params.getAsyncActionTakenInMainFrameCallback().onResult(
                        new ExternalNavigationParams.AsyncActionTakenParams(false, true, params));
            }
        }
    }

    /**
     * If another app, or the user, chose to launch this app for an intent, we should keep that
     * navigation within this app through redirects until it resolves to a new app or external
     * protocol given this app was intentionally chosen. Custom tabs always explicitly target the
     * browser and this issue is handled elsewhere through
     * {@link RedirectHandler#intentPrefersToStayInChrome()}.
     *
     * Usually this covers cases like https://www.youtube.com/ redirecting to
     * https://m.youtube.com/. Note that this isn't covered by {@link #shouldStayWithinHost()} as
     * for intent navigation there is no previously committed URL.
     */
    private boolean shouldKeepIntentRedirectInApp(ExternalNavigationParams params,
            boolean incomingIntentRedirect, List<ResolveInfo> resolvingInfos,
            boolean isExternalProtocol) {
        if (params.getRedirectHandler() != null && incomingIntentRedirect && !isExternalProtocol
                && !params.getRedirectHandler().isFromCustomTabIntent()
                && !params.getRedirectHandler().hasNewResolver(
                        resolvingInfos, (Intent intent) -> queryIntentActivities(intent))) {
            if (debug()) Log.i(TAG, "Intent navigation with no new handlers.");
            return true;
        }
        return false;
    }

    /**
     * @param packageName The package to check.
     * @return Whether the package is a valid WebAPK package.
     */
    @VisibleForTesting
    protected boolean isValidWebApk(String packageName) {
        // Ensure that WebApkValidator is initialized (note: this method is a no-op after the first
        // time that it is invoked).
        WebApkValidator.init(
                ChromeWebApkHostSignature.EXPECTED_SIGNATURE, ChromeWebApkHostSignature.PUBLIC_KEY);
        return WebApkValidator.isValidWebApk(ContextUtils.getApplicationContext(), packageName);
    }

    /**
     * Returns whether the activity belongs to a WebAPK and the URL is within the scope of the
     * WebAPK. The WebAPK's main activity is a bouncer that redirects to the WebAPK Activity in
     * Chrome. In order to avoid bouncing indefinitely, we should not override the navigation if we
     * are currently showing the WebAPK (params#nativeClientPackageName()) that we will redirect to.
     */
    private boolean isAlreadyInTargetWebApk(
            QueryIntentActivitiesSupplier resolvingInfos, ExternalNavigationParams params) {
        String currentName = params.nativeClientPackageName();
        if (currentName == null) return false;
        for (ResolveInfo resolveInfo : getResolveInfosForWebApks(params, resolvingInfos)) {
            ActivityInfo info = resolveInfo.activityInfo;
            if (info != null && currentName.equals(info.packageName)) {
                if (debug()) Log.i(TAG, "Already in WebAPK");
                return true;
            }
        }
        return false;
    }

    // This will handle external navigations only for intent meant for Autofill Assistant.
    private boolean handleWithAutofillAssistant(
            ExternalNavigationParams params, Intent targetIntent, GURL browserFallbackUrl) {
        if (!mDelegate.isIntentToAutofillAssistant(targetIntent)) {
            return false;
        }

        // Launching external intents is always forbidden in incognito. Handle the intent with
        // Autofill Assistant instead. Note that Autofill Assistant won't start in incognito either,
        // this will only result in navigating to the browserFallbackUrl.
        if (!params.isIncognito()) {
            @IntentToAutofillAllowingAppResult
            int intentAllowingAppResult = mDelegate.isIntentToAutofillAssistantAllowingApp(params,
                    targetIntent,
                    (intent)
                            -> getSpecializedHandlersWithFilter(queryIntentActivities(intent),
                                       /* filterPackageName= */ null,
                                       /* handlesInstantAppLaunchingInternally= */ false)
                                       .size()
                            == 1);
            switch (intentAllowingAppResult) {
                case IntentToAutofillAllowingAppResult.DEFER_TO_APP_NOW:
                    if (debug()) {
                        Log.i(TAG, "Autofill Assistant passed in favour of App.");
                    }
                    return false;
                case IntentToAutofillAllowingAppResult.DEFER_TO_APP_LATER:
                    if (params.getRedirectHandler() != null
                            && isAutofillAssistantGoogleReferrer(params)) {
                        if (debug()) {
                            Log.i(TAG, "Autofill Assistant passed in favour of App later.");
                        }
                        params.getRedirectHandler()
                                .setShouldNotBlockUrlLoadingOverrideOnCurrentRedirectionChain();
                        return true;
                    }
                    break;
                case IntentToAutofillAllowingAppResult.NONE:
                    break;
            }
        }

        if (mDelegate.handleWithAutofillAssistant(params, targetIntent, browserFallbackUrl,
                    isAutofillAssistantGoogleReferrer(params))) {
            if (debug()) Log.i(TAG, "Handled with Autofill Assistant.");
        } else {
            if (debug()) Log.i(TAG, "Not handled with Autofill Assistant.");
        }
        return true;
    }

    // Check if we're navigating under conditions that should never launch an external app,
    // regardless of which URL we're navigating to.
    private boolean shouldBlockAllExternalAppLaunches(
            ExternalNavigationParams params, boolean incomingIntentRedirect) {
        return shouldBlockSubframeAppLaunches(params)
                || blockExternalNavWhileBackgrounded(params, incomingIntentRedirect)
                || blockExternalNavFromBackgroundTab(params, incomingIntentRedirect)
                || ignoreBackForwardNav(params);
    }

    private void recordIntentSelectorMetrics(GURL targetUrl, Intent targetIntent) {
        if (UrlUtilities.hasIntentScheme(targetUrl)) {
            RecordHistogram.recordBooleanHistogram(
                    "Android.Intent.IntentUriWithSelector", targetIntent.getSelector() != null);
        }
    }

    private OverrideUrlLoadingResult shouldOverrideUrlLoadingInternal(
            ExternalNavigationParams params, Intent targetIntent, GURL browserFallbackUrl,
            MutableBoolean canLaunchExternalFallbackResult) {
        recordIntentSelectorMetrics(params.getUrl(), targetIntent);
        sanitizeQueryIntentActivitiesIntent(targetIntent);

        // Don't allow external fallback URLs by default.
        canLaunchExternalFallbackResult.set(false);

        if (!maybeSetSmsPackage(targetIntent)) maybeRecordPhoneIntentMetrics(targetIntent);

        // http://crbug.com/170925: We need to show the intent picker when we receive an intent from
        // another app that 30x redirects to a YouTube/Google Maps/Play Store/Google+ URL etc.
        boolean incomingIntentRedirect = isIncomingIntentRedirect(params);
        boolean isExternalProtocol = !UrlUtilities.isAcceptedScheme(params.getUrl());

        GURL intentDataUrl = new GURL(targetIntent.getDataString());
        // intent: URLs are considered an external protocol, but may still contain a Data URI that
        // this app does support, and may still end up launching this app.
        boolean isIntentWithSupportedProtocol = UrlUtilities.hasIntentScheme(params.getUrl())
                && UrlUtilities.isAcceptedScheme(intentDataUrl);

        if (shouldBlockAllExternalAppLaunches(params, incomingIntentRedirect)) {
            return OverrideUrlLoadingResult.forNoOverride();
        }

        if (handleWithAutofillAssistant(params, targetIntent, browserFallbackUrl)) {
            return OverrideUrlLoadingResult.forNoOverride();
        }

        // This check should happen for reloads, navigations, etc..., which is why
        // it occurs before the subsequent blocks.
        if (handleFileUrlPermissions(params)) {
            return OverrideUrlLoadingResult.forAsyncAction(
                    OverrideUrlLoadingAsyncActionType.UI_GATING_BROWSER_NAVIGATION);
        }

        // This should come after file intents, but before any returns of
        // OVERRIDE_WITH_EXTERNAL_INTENT.
        if (externalIntentRequestsDisabledForUrl(params)) {
            return OverrideUrlLoadingResult.forNoOverride();
        }

        if (isLinkFromChromeInternalPage(params)) return OverrideUrlLoadingResult.forNoOverride();

        if (RedirectHandler.isRefactoringEnabled()) {
            if (isDirectFormSubmit(params, isExternalProtocol)) {
                return OverrideUrlLoadingResult.forNoOverride();
            }
        }

        if (hasInternalScheme(params.getUrl(), targetIntent)
                || hasContentScheme(params.getUrl(), targetIntent)
                || hasFileSchemeInIntentURI(params.getUrl(), targetIntent)) {
            return OverrideUrlLoadingResult.forNoOverride();
        }

        if (isYoutubePairingCode(params.getUrl())) return OverrideUrlLoadingResult.forNoOverride();

        if (shouldStayInIncognito(params, isExternalProtocol)) {
            return OverrideUrlLoadingResult.forNoOverride();
        }

        if (isInternalPdfDownload(isExternalProtocol, params)) {
            return OverrideUrlLoadingResult.forNoOverride();
        }

        if (isUnhandledWtaiProtocol(params)) return OverrideUrlLoadingResult.forNoOverride();

        if (preventDirectInstantAppsIntent(targetIntent)) {
            return OverrideUrlLoadingResult.forNoOverride();
        }

        QueryIntentActivitiesSupplier resolvingInfos =
                new QueryIntentActivitiesSupplier(targetIntent);

        boolean intentMatchesNonDefaultWebApk =
                intentMatchesNonDefaultWebApk(params, resolvingInfos);
        if (isDirectIntentNavigation(
                    params, intentMatchesNonDefaultWebApk, incomingIntentRedirect)) {
            return OverrideUrlLoadingResult.forNoOverride();
        }

        boolean requiresPromptForExternalIntent =
                isNavigationChainExpired(params, targetIntent, resolvingInfos)
                || redirectShouldStayInApp(params, isExternalProtocol, targetIntent, resolvingInfos)
                || navigationChainBlocksExternalNavigation(
                        params, targetIntent, resolvingInfos, isExternalProtocol)
                || !preferToShowIntentPicker(params, isExternalProtocol, incomingIntentRedirect,
                        intentMatchesNonDefaultWebApk);

        // Short-circuit expensive quertyIntentActivities calls below since we won't prompt anyways
        // for protocols the browser can handle.
        if (requiresPromptForExternalIntent && !isExternalProtocol) {
            return OverrideUrlLoadingResult.forNoOverride();
        }

        // From this point on, we have determined it is safe to launch an External App from a
        // fallback URL (unless we have to prompt).
        if (!requiresPromptForExternalIntent) canLaunchExternalFallbackResult.set(true);

        if (resolvingInfos.get().isEmpty()) {
            return handleUnresolvableIntent(
                    params, targetIntent, browserFallbackUrl, requiresPromptForExternalIntent);
        }

        if (resolvesToNonExportedActivity(resolvingInfos.get())) {
            return OverrideUrlLoadingResult.forNoOverride();
        }

        boolean hasSpecializedHandler = countSpecializedHandlers(resolvingInfos.get()) > 0;
        if (!isExternalProtocol && !hasSpecializedHandler && !intentMatchesNonDefaultWebApk) {
            if (fallBackToHandlingWithInstantApp(params, incomingIntentRedirect, resolvingInfos,
                        requiresPromptForExternalIntent)) {
                return OverrideUrlLoadingResult.forExternalIntent();
            }
            return fallBackToHandlingInApp();
        }

        if (shouldStayWithinHost(params, resolvingInfos.get(), isExternalProtocol)) {
            return OverrideUrlLoadingResult.forNoOverride();
        }

        if (shouldKeepIntentRedirectInApp(
                    params, incomingIntentRedirect, resolvingInfos.get(), isExternalProtocol)) {
            return OverrideUrlLoadingResult.forNoOverride();
        }

        if (isAlreadyInTargetWebApk(resolvingInfos, params)) {
            return OverrideUrlLoadingResult.forNoOverride();
        }

        boolean intentHasExtras =
                targetIntent.getExtras() != null && !targetIntent.getExtras().isEmpty();
        boolean shouldProxyForInstantApps = mDelegate.handlesInstantAppLaunchingInternally()
                && isIntentToInstantApp(targetIntent) && isSerpReferrer();
        prepareExternalIntent(
                targetIntent, params, resolvingInfos.get(), shouldProxyForInstantApps);

        if (params.isIncognito()) {
            return handleIncognitoIntent(params, targetIntent, intentDataUrl, resolvingInfos.get(),
                    browserFallbackUrl, shouldProxyForInstantApps);
        }

        if (launchWebApkIfSoleIntentHandler(resolvingInfos, targetIntent, params)) {
            return OverrideUrlLoadingResult.forExternalIntent();
        }

        ResolveActivitySupplier resolveActivity = new ResolveActivitySupplier(targetIntent);
        boolean requiresIntentChooser = false;
        if (!mDelegate.maybeSetTargetPackage(targetIntent, resolvingInfos)) {
            requiresIntentChooser = isInsecureIntentToOtherBrowser(targetIntent, resolvingInfos,
                    isIntentWithSupportedProtocol, resolveActivity, intentHasExtras);
        }

        if (shouldAvoidShowingDisambiguationPrompt(
                    isExternalProtocol, targetIntent, resolvingInfos, resolveActivity)) {
            return OverrideUrlLoadingResult.forNoOverride();
        }

        if (requiresPromptForExternalIntent) {
            return maybeAskToLaunchApp(isExternalProtocol, targetIntent, resolvingInfos,
                    resolveActivity, browserFallbackUrl, params);
        }

        return startActivity(targetIntent, shouldProxyForInstantApps, requiresIntentChooser,
                resolvingInfos, resolveActivity, browserFallbackUrl, intentDataUrl,
                params.getReferrerUrl(), params.getInitiatorOrigin(), params.isRendererInitiated());
    }

    // https://crbug.com/1249964
    private boolean resolvesToNonExportedActivity(List<ResolveInfo> infos) {
        for (ResolveInfo info : infos) {
            if (info.activityInfo != null && !info.activityInfo.exported) {
                Log.w(TAG, "Web Intent resolves to non-exported Activity.");
                return true;
            }
        }

        return false;
    }

    private boolean shouldAvoidShowingDisambiguationPrompt(boolean isExternalProtocol,
            Intent intent, QueryIntentActivitiesSupplier resolvingInfosSupplier,
            ResolveActivitySupplier resolveActivitySupplier) {
        // For navigations Chrome can't handle, it's fine to show the disambiguation dialog
        // regardless of the embedder's preference.
        if (isExternalProtocol) return false;

        // Don't bother performing the package manager checks if the delegate is fine with the
        // disambiguation prompt.
        if (!mDelegate.shouldAvoidDisambiguationDialog(intent)) return false;

        ResolveInfo resolveActivity = resolveActivitySupplier.get();

        if (resolveActivity == null) return true;

        List<ResolveInfo> possibleHandlingActivities = resolvingInfosSupplier.get();

        // If resolveActivity is contained in possibleHandlingActivities, that means the Intent
        // would launch a specialized Activity. If not, that means the Intent will launch the
        // Android disambiguation prompt.
        boolean result = !resolversSubsetOf(
                Collections.singletonList(resolveActivity), possibleHandlingActivities);
        if (debug() && result) Log.i(TAG, "Avoiding disambiguation dialog.");
        return result;
    }

    private OverrideUrlLoadingResult handleIncognitoIntent(ExternalNavigationParams params,
            Intent targetIntent, GURL intentDataUrl, List<ResolveInfo> resolvingInfos,
            GURL browserFallbackUrl, boolean shouldProxyForInstantApps) {
        boolean intentTargetedToApp = mDelegate.willAppHandleIntent(targetIntent);

        GURL fallbackUrl = browserFallbackUrl;
        // If we can handle the intent, then fall back to handling the target URL instead of
        // the fallbackUrl if the user decides not to leave incognito.
        if (resolveInfoContainsSelf(resolvingInfos)) {
            GURL targetUrl =
                    UrlUtilities.hasIntentScheme(params.getUrl()) ? intentDataUrl : params.getUrl();
            // Make sure the browser can handle this URL, in case the Intent targeted a
            // non-browser component for this app.
            if (UrlUtilities.isAcceptedScheme(targetUrl)) fallbackUrl = targetUrl;
        }

        // The user is about to potentially leave the app, so we should ask whether they want to
        // leave incognito or not.
        if (!intentTargetedToApp) {
            return handleExternalIncognitoIntent(
                    targetIntent, params, fallbackUrl, shouldProxyForInstantApps);
        }

        // The intent is staying in the app, so we can simply navigate to the intent's URL,
        // while staying in incognito.
        return handleFallbackUrl(params, targetIntent, fallbackUrl, false);
    }

    /**
     * Sanitize intent to be passed to {@link queryIntentActivities()}
     * ensuring that web pages cannot bypass browser security.
     */
    public static void sanitizeQueryIntentActivitiesIntent(Intent intent) {
        intent.setFlags(intent.getFlags() & ALLOWED_INTENT_FLAGS);
        intent.addCategory(Intent.CATEGORY_BROWSABLE);
        intent.setComponent(null);

        // Intent Selectors allow intents to bypass the intent filter and potentially send apps URIs
        // they were not expecting to handle. https://crbug.com/1254422
        intent.setSelector(null);
    }

    /**
     * @return OVERRIDE_WITH_EXTERNAL_INTENT when we successfully started market activity,
     *         NO_OVERRIDE otherwise.
     */
    private OverrideUrlLoadingResult sendIntentToMarket(String packageName, String marketReferrer,
            ExternalNavigationParams params, GURL fallbackUrl) {
        Uri marketUri =
                new Uri.Builder()
                        .scheme("market")
                        .authority("details")
                        .appendQueryParameter(PLAY_PACKAGE_PARAM, packageName)
                        .appendQueryParameter(PLAY_REFERRER_PARAM, Uri.decode(marketReferrer))
                        .build();
        Intent intent = new Intent(Intent.ACTION_VIEW, marketUri);
        intent.addCategory(Intent.CATEGORY_BROWSABLE);
        intent.setPackage(PLAY_APP_PACKAGE);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        if (!params.getReferrerUrl().isEmpty()) {
            intent.putExtra(Intent.EXTRA_REFERRER, Uri.parse(params.getReferrerUrl().getSpec()));
        }

        if (!deviceCanHandleIntent(intent)) {
            // Exit early if the Play Store isn't available. (https://crbug.com/820709)
            if (debug()) Log.i(TAG, "Play Store not installed.");
            return OverrideUrlLoadingResult.forNoOverride();
        }

        if (params.isIncognito()) {
            if (!startIncognitoIntent(params, intent, fallbackUrl, false)) {
                if (debug()) Log.i(TAG, "Failed to show incognito alert dialog.");
                return OverrideUrlLoadingResult.forNoOverride();
            }
            if (debug()) Log.i(TAG, "Incognito intent to Play Store.");
            return OverrideUrlLoadingResult.forAsyncAction(
                    OverrideUrlLoadingAsyncActionType.UI_GATING_INTENT_LAUNCH);
        } else {
            startActivity(intent, false);
            if (debug()) Log.i(TAG, "Intent to Play Store.");
            return OverrideUrlLoadingResult.forExternalIntent();
        }
    }

    /**
     * If the given URL is to Google Play, extracts the package name and referrer tracking code
     * from the {@param url} and returns as a Pair in that order. Otherwise returns null.
     */
    private Pair<String, String> maybeGetPlayStoreAppIdAndReferrer(GURL url) {
        if (PLAY_HOSTNAME.equals(url.getHost()) && url.getPath().startsWith(PLAY_APP_PATH)) {
            String playPackage = UrlUtilities.getValueForKeyInQuery(url, PLAY_PACKAGE_PARAM);
            if (TextUtils.isEmpty(playPackage)) return null;
            return new Pair<String, String>(
                    playPackage, UrlUtilities.getValueForKeyInQuery(url, PLAY_REFERRER_PARAM));
        }
        return null;
    }

    /**
     * @return Whether the |url| could be handled by an external application on the system.
     */
    @VisibleForTesting
    boolean canExternalAppHandleUrl(GURL url) {
        if (url.getSpec().startsWith(WTAI_MC_URL_PREFIX)) return true;
        Intent intent;
        try {
            intent = Intent.parseUri(url.getSpec(), Intent.URI_INTENT_SCHEME);
        } catch (Exception ex) {
            // Ignore the error.
            Log.w(TAG, "Bad URI %s", url, ex);
            return false;
        }
        if (intent.getPackage() != null) return true;

        List<ResolveInfo> resolvingInfos = queryIntentActivities(intent);
        return resolvingInfos != null && !resolvingInfos.isEmpty();
    }

    /**
     * Dispatch SMS intents to the default SMS application if applicable.
     * Most SMS apps refuse to send SMS if not set as default SMS application.
     *
     * @param resolvingComponentNames The list of ComponentName that resolves the current intent.
     */
    private String getDefaultSmsPackageName(List<ResolveInfo> resolvingComponentNames) {
        String defaultSmsPackageName = getDefaultSmsPackageNameFromSystem();
        if (defaultSmsPackageName == null) return null;
        // Makes sure that the default SMS app actually resolves the intent.
        for (ResolveInfo resolveInfo : resolvingComponentNames) {
            if (defaultSmsPackageName.equals(resolveInfo.activityInfo.packageName)) {
                return defaultSmsPackageName;
            }
        }
        return null;
    }

    /**
     * Launches WebAPK if the WebAPK is the sole non-browser handler for the given intent.
     * @return Whether a WebAPK was launched.
     */
    private boolean launchWebApkIfSoleIntentHandler(QueryIntentActivitiesSupplier resolvingInfos,
            Intent targetIntent, ExternalNavigationParams params) {
        String packageName = pickWebApkIfSoleIntentHandler(params, resolvingInfos);
        if (packageName == null) return false;

        Intent webApkIntent = new Intent(targetIntent);
        webApkIntent.setPackage(packageName);
        try {
            startActivity(webApkIntent, false);
            if (debug()) Log.i(TAG, "Launched WebAPK");
            return true;
        } catch (ActivityNotFoundException e) {
            // The WebApk must have been uninstalled/disabled since we queried for Activities to
            // handle this intent.
            if (debug()) Log.i(TAG, "WebAPK launch failed");
            return false;
        }
    }

    // https://crbug.com/1232514. See #intentMatchesNonDefaultWebApk.
    private List<ResolveInfo> getResolveInfosForWebApks(
            ExternalNavigationParams params, QueryIntentActivitiesSupplier resolvingInfos) {
        if (params.isFromIntent() && mDelegate.shouldLaunchWebApksOnInitialIntent()) {
            return resolvingInfos.getIncludingNonDefaultResolveInfos();
        }
        return resolvingInfos.get();
    }

    @Nullable
    private String pickWebApkIfSoleIntentHandler(
            ExternalNavigationParams params, QueryIntentActivitiesSupplier resolvingInfos) {
        ArrayList<String> packages =
                getSpecializedHandlers(getResolveInfosForWebApks(params, resolvingInfos));
        if (packages.size() != 1 || !isValidWebApk(packages.get(0))) return null;
        return packages.get(0);
    }

    /**
     * Returns whether or not there's an activity available to handle the intent.
     */
    private boolean deviceCanHandleIntent(Intent intent) {
        List<ResolveInfo> resolveInfos = queryIntentActivities(intent);
        return resolveInfos != null && !resolveInfos.isEmpty();
    }

    /**
     * See {@link PackageManagerUtils#queryIntentActivities(Intent, int)}
     */
    @NonNull
    private List<ResolveInfo> queryIntentActivities(Intent intent) {
        return PackageManagerUtils.queryIntentActivities(
                intent, PackageManager.GET_RESOLVED_FILTER | PackageManager.MATCH_DEFAULT_ONLY);
    }

    private static boolean intentResolutionMatches(Intent intent, Intent other) {
        return intent.filterEquals(other)
                && (intent.getSelector() == other.getSelector()
                        || intent.getSelector().filterEquals(other.getSelector()));
    }

    /**
     * @return Whether the URL is a file download.
     */
    @VisibleForTesting
    boolean isPdfDownload(GURL url) {
        String fileExtension = MimeTypeMap.getFileExtensionFromUrl(url.getSpec());
        if (TextUtils.isEmpty(fileExtension)) return false;

        return PDF_EXTENSION.equals(fileExtension);
    }

    private static boolean isPdfIntent(Intent intent) {
        if (intent == null || intent.getData() == null) return false;
        String filename = intent.getData().getLastPathSegment();
        return (filename != null && filename.endsWith(PDF_SUFFIX))
                || PDF_MIME.equals(intent.getType());
    }

    /**
     * Records the dispatching of an external intent.
     */
    private static void recordExternalNavigationDispatched(Intent intent) {
        ArrayList<String> specializedHandlers =
                intent.getStringArrayListExtra(EXTRA_EXTERNAL_NAV_PACKAGES);
        if (specializedHandlers != null && specializedHandlers.size() > 0) {
            RecordUserAction.record("MobileExternalNavigationDispatched");
        }
    }

    /**
     * If the intent is for a pdf, resolves intent handlers to find the platform pdf viewer if
     * it is available and force is for the provided |intent| so that the user doesn't need to
     * choose it from Intent picker.
     *
     * @param intent Intent to open.
     */
    private static void forcePdfViewerAsIntentHandlerIfNeeded(Intent intent) {
        if (intent == null || !isPdfIntent(intent)) return;
        resolveIntent(intent, true /* allowSelfOpen (ignored) */);
    }

    /**
     * Retrieve the best activity for the given intent. If a default activity is provided,
     * choose the default one. Otherwise, return the Intent picker if there are more than one
     * capable activities. If the intent is pdf type, return the platform pdf viewer if
     * it is available so user don't need to choose it from Intent picker.
     *
     * @param intent Intent to open.
     * @param allowSelfOpen Whether chrome itself is allowed to open the intent.
     * @return true if the intent can be resolved, or false otherwise.
     */
    public static boolean resolveIntent(Intent intent, boolean allowSelfOpen) {
        Context context = ContextUtils.getApplicationContext();
        ResolveInfo info =
                PackageManagerUtils.resolveActivity(intent, PackageManager.MATCH_DEFAULT_ONLY);
        if (info == null) return false;

        final String packageName = context.getPackageName();
        if (info.match != 0) {
            // There is a default activity for this intent, use that.
            return allowSelfOpen || !packageName.equals(info.activityInfo.packageName);
        }
        List<ResolveInfo> handlers = PackageManagerUtils.queryIntentActivities(
                intent, PackageManager.MATCH_DEFAULT_ONLY);
        if (handlers == null || handlers.isEmpty()) return false;
        boolean canSelfOpen = false;
        boolean hasPdfViewer = false;
        for (ResolveInfo resolveInfo : handlers) {
            String pName = resolveInfo.activityInfo.packageName;
            if (packageName.equals(pName)) {
                canSelfOpen = true;
            } else if (PDF_VIEWER.equals(pName)) {
                if (isPdfIntent(intent)) {
                    intent.setClassName(pName, resolveInfo.activityInfo.name);
                    Uri referrer = new Uri.Builder()
                                           .scheme(IntentUtils.ANDROID_APP_REFERRER_SCHEME)
                                           .authority(packageName)
                                           .build();
                    intent.putExtra(Intent.EXTRA_REFERRER, referrer);
                    hasPdfViewer = true;
                    break;
                }
            }
        }
        return !canSelfOpen || allowSelfOpen || hasPdfViewer;
    }

    /**
     * Start an activity for the intent. Used for intents that must be handled externally.
     * @param intent The intent we want to send.
     * @param proxy Whether we need to proxy the intent through AuthenticatedProxyActivity (this is
     *              used by Instant Apps intents).
     */
    private void startActivity(Intent intent, boolean proxy) {
        startActivity(intent, proxy, false, null, null, null, null, null, null, false);
    }

    /**
     * Start an activity for the intent. Used for intents that may be handled internally or
     * externally.
     * @param intent The intent we want to send.
     * @param proxy Whether we need to proxy the intent through AuthenticatedProxyActivity (this is
     *              used by Instant Apps intents).
     * @param requiresIntentChooser Whether, for security reasons, the Intent Chooser is required to
     *                              be shown.
     *
     * Below parameters are only used if |requiresIntentChooser| is true.
     *
     * @param resolvingInfos The queryIntentActivities |intent| matches against.
     * @param resolveActivity The resolving Activity |intent| matches against.
     * @param browserFallbackUrl The fallback URL if the user chooses not to leave this app.
     * @param intentDataUrl The URL |intent| is targeting.
     * @param referrerUrl The referrer for the navigation.
     * @param initiatorOrigin The Origin that initiated the navigation, if any.
     * @param isRendererInitiated True if the navigation was renderer initiated.
     * @returns The OverrideUrlLoadingResult for starting (or not starting) the Activity.
     */
    protected OverrideUrlLoadingResult startActivity(Intent intent, boolean proxy,
            boolean requiresIntentChooser, QueryIntentActivitiesSupplier resolvingInfos,
            ResolveActivitySupplier resolveActivity, GURL browserFallbackUrl, GURL intentDataUrl,
            GURL referrerUrl, Origin initiatorOrigin, boolean isRendererInitiated) {
        // Only touches disk on Kitkat. See http://crbug.com/617725 for more context.
        StrictMode.ThreadPolicy oldPolicy = StrictMode.allowThreadDiskWrites();
        try {
            forcePdfViewerAsIntentHandlerIfNeeded(intent);
            if (proxy) {
                mDelegate.dispatchAuthenticatedIntent(intent);
                recordExternalNavigationDispatched(intent);
                return OverrideUrlLoadingResult.forExternalIntent();
            } else {
                Context context = ContextUtils.activityFromContext(mDelegate.getContext());
                if (context == null) {
                    context = ContextUtils.getApplicationContext();
                    intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                }
                if (requiresIntentChooser) {
                    return startActivityWithChooser(intent, resolvingInfos, resolveActivity,
                            browserFallbackUrl, intentDataUrl, referrerUrl, context,
                            initiatorOrigin, isRendererInitiated);
                }
                return doStartActivity(intent, context);
            }
        } catch (SecurityException e) {
            // https://crbug.com/808494: Handle the URL internally if dispatching to another
            // application fails with a SecurityException. This happens due to malformed
            // manifests in another app.
        } catch (ActivityNotFoundException e) {
            // The targeted app must have been uninstalled/disabled since we queried for Activities
            // to handle this intent.
            if (debug()) Log.i(TAG, "Activity not found.");
        } catch (AndroidRuntimeException e) {
            // https://crbug.com/1226177: Most likely cause of this exception is Android failing
            // to start the app that we previously detected could handle the Intent.
            Log.e(TAG, "Could not start Activity for intent " + intent.toString(), e);
        } catch (RuntimeException e) {
            IntentUtils.logTransactionTooLargeOrRethrow(e, intent);
        } finally {
            StrictMode.setThreadPolicy(oldPolicy);
        }
        return OverrideUrlLoadingResult.forNoOverride();
    }

    private OverrideUrlLoadingResult doStartActivity(Intent intent, Context context) {
        if (debug()) Log.i(TAG, "startActivity");
        context.startActivity(intent);
        recordExternalNavigationDispatched(intent);
        return OverrideUrlLoadingResult.forExternalIntent();
    }

    // looking up resources from other apps requires the use of getIdentifier()
    @SuppressWarnings({"UseCompatLoadingForDrawables", "DiscouragedApi"})
    private OverrideUrlLoadingResult startActivityWithChooser(final Intent intent,
            QueryIntentActivitiesSupplier resolvingInfos, ResolveActivitySupplier resolveActivity,
            GURL browserFallbackUrl, GURL intentDataUrl, GURL referrerUrl, Context context,
            Origin initiatorOrigin, boolean isRendererInitiated) {
        ResolveInfo intentResolveInfo = resolveActivity.get();
        // If this is null, then the intent was only previously matching
        // non-default filters, so just drop it.
        if (intentResolveInfo == null) return OverrideUrlLoadingResult.forNoOverride();

        // If the |resolvingInfos| from queryIntentActivities don't contain the result of
        // resolveActivity, it means the intent is resolving to the ResolverActivity, so the user
        // will already get the option to choose the target app (as there will be multiple options)
        // and we don't need to do anything. Otherwise we have to make a fake option in the chooser
        // dialog that loads the URL in the embedding app.
        if (!resolversSubsetOf(Arrays.asList(intentResolveInfo), resolvingInfos.get())) {
            return doStartActivity(intent, context);
        }

        Intent pickerIntent = new Intent(Intent.ACTION_PICK_ACTIVITY);
        pickerIntent.putExtra(Intent.EXTRA_INTENT, intent);

        // Add the fake entry for the embedding app. This behavior is not well documented but works
        // consistently across Android since L (and at least up to S).
        PackageManager pm = context.getPackageManager();
        ArrayList<ShortcutIconResource> icons = new ArrayList<>();
        ArrayList<String> labels = new ArrayList<>();
        String packageName = context.getPackageName();
        String label = "";
        ShortcutIconResource resource = new ShortcutIconResource();
        try {
            ApplicationInfo applicationInfo =
                    pm.getApplicationInfo(packageName, PackageManager.GET_META_DATA);
            label = (String) pm.getApplicationLabel(applicationInfo);
            Resources resources = pm.getResourcesForApplication(applicationInfo);
            resource.packageName = packageName;
            resource.resourceName = resources.getResourceName(applicationInfo.icon);
            // This will throw a Resources.NotFoundException if the package uses resource
            // name collapsing/stripping. The ActivityPicker fails to handle this exception, we have
            // have to check for it here to avoid crashes.
            resources.getDrawable(resources.getIdentifier(resource.resourceName, null, null), null);
        } catch (NameNotFoundException | Resources.NotFoundException e) {
            Log.w(TAG, "No icon resource found for package: " + packageName);
            // Most likely the app doesn't have an icon and is just a test
            // app. Android will just use a blank icon.
            resource.packageName = "";
            resource.resourceName = "";
        }
        labels.add(label);
        icons.add(resource);
        pickerIntent.putExtra(Intent.EXTRA_SHORTCUT_NAME, labels);
        pickerIntent.putExtra(Intent.EXTRA_SHORTCUT_ICON_RESOURCE, icons);

        // Call startActivityForResult on the PICK_ACTIVITY intent, which will set the component of
        // the data result to the component of the chosen app.
        mDelegate.getWindowAndroid().showCancelableIntent(
                pickerIntent, new WindowAndroid.IntentCallback() {
                    @Override
                    public void onIntentCompleted(int resultCode, Intent data) {
                        // If |data| is null, the user backed out of the intent chooser.
                        if (data == null) return;

                        // Quirk of how we use the ActivityChooser - if the embedding app is
                        // chosen we get an intent back with ACTION_CREATE_SHORTCUT.
                        if (data.getAction().equals(Intent.ACTION_CREATE_SHORTCUT)) {
                            // It's pretty arbitrary whether to prefer the data URL or the fallback
                            // URL here. We could consider preferring the fallback URL, as the URL
                            // was probably intending to leave Chrome, but loading the URL the site
                            // was trying to load in a browser seems like the better choice and
                            // matches what would have happened had the regular chooser dialog shown
                            // up and the user selected this app.
                            if (UrlUtilities.isAcceptedScheme(intentDataUrl)) {
                                clobberCurrentTab(intentDataUrl, referrerUrl, initiatorOrigin,
                                        isRendererInitiated);
                            } else if (!browserFallbackUrl.isEmpty()) {
                                clobberCurrentTab(browserFallbackUrl, referrerUrl, initiatorOrigin,
                                        isRendererInitiated);
                            }
                            return;
                        }

                        // Set the package for the original intent to the chosen app and start
                        // it. Note that a selector cannot be set at the same time as a package.
                        intent.setSelector(null);
                        intent.setPackage(data.getComponent().getPackageName());
                        startActivity(intent, false);
                    }
                }, null);
        return OverrideUrlLoadingResult.forAsyncAction(
                OverrideUrlLoadingAsyncActionType.UI_GATING_INTENT_LAUNCH);
    }

    protected OverrideUrlLoadingResult maybeAskToLaunchApp(boolean isExternalProtocol,
            Intent targetIntent, QueryIntentActivitiesSupplier resolvingInfos,
            ResolveActivitySupplier resolveActivity, GURL browserFallbackUrl,
            ExternalNavigationParams params) {
        // For URLs the browser supports, we shouldn't have reached here.
        assert isExternalProtocol;

        // Use the fallback URL if we have it, otherwise we give sites a fingerprinting mechanism
        // where they can repeatedly attempt to launch apps without a user gesture until they find
        // one the user has installed.
        if (!browserFallbackUrl.isEmpty()) return OverrideUrlLoadingResult.forNoOverride();

        ResolveInfo intentResolveInfo = resolveActivity.get();

        // No app can resolve the intent, don't prompt.
        if (intentResolveInfo == null || intentResolveInfo.activityInfo == null) {
            if (debug()) Log.i(TAG, "Message doesn't resolve to any app.");
            return OverrideUrlLoadingResult.forNoOverride();
        }

        // If the |resolvingInfos| from queryIntentActivities don't contain the result of
        // resolveActivity, it means there's no default handler for the intent and it's resolving to
        // the ResolverActivity. This means we can't know which app will be launched and can't
        // convey that to the user. We also don't want to just allow the chooser dialog to be shown
        // when the external navigation was otherwise blocked. In this case, we should just continue
        // to block the navigation, and sites hoping to prompt the user when navigation fails should
        // make sure to correctly target their app.
        if (!resolversSubsetOf(Arrays.asList(intentResolveInfo), resolvingInfos.get())) {
            if (debug()) Log.i(TAG, "Message resolves to multiple apps.");
            return OverrideUrlLoadingResult.forNoOverride();
        }

        MessageDispatcher messageDispatcher =
                MessageDispatcherProvider.from(mDelegate.getWindowAndroid());
        WebContents webContents = mDelegate.getWebContents();
        if (messageDispatcher == null || webContents == null) {
            if (debug()) Log.i(TAG, "No WebContents to show Message for.");
            return OverrideUrlLoadingResult.forNoOverride();
        }

        String packageName = intentResolveInfo.activityInfo.packageName;
        PackageManager pm = mDelegate.getContext().getPackageManager();
        ApplicationInfo applicationInfo = null;
        try {
            applicationInfo = pm.getApplicationInfo(packageName, 0);
        } catch (NameNotFoundException e) {
            return OverrideUrlLoadingResult.forNoOverride();
        }

        Drawable icon = pm.getApplicationLogo(applicationInfo);
        if (icon == null) icon = pm.getApplicationIcon(applicationInfo);
        CharSequence label = pm.getApplicationLabel(applicationInfo);

        Resources res = mDelegate.getContext().getResources();
        String title = res.getString(R.string.external_navigation_continue_to_title, label);
        String description =
                res.getString(R.string.external_navigation_continue_to_description, label);
        String action = res.getString(R.string.external_navigation_continue_to_action);

        PropertyModel message =
                new PropertyModel.Builder(MessageBannerProperties.ALL_KEYS)
                        .with(MessageBannerProperties.MESSAGE_IDENTIFIER,
                                MessageIdentifier.EXTERNAL_NAVIGATION)
                        .with(MessageBannerProperties.TITLE, title)
                        .with(MessageBannerProperties.DESCRIPTION, description)
                        .with(MessageBannerProperties.ICON, icon)
                        .with(MessageBannerProperties.PRIMARY_BUTTON_TEXT, action)
                        .with(MessageBannerProperties.ICON_TINT_COLOR,
                                MessageBannerProperties.TINT_NONE)
                        .with(MessageBannerProperties.ON_PRIMARY_ACTION,
                                () -> {
                                    startActivity(targetIntent, false);
                                    if (params.getAsyncActionTakenInMainFrameCallback() != null) {
                                        params.getAsyncActionTakenInMainFrameCallback().onResult(
                                                new ExternalNavigationParams.AsyncActionTakenParams(
                                                        true, false, params));
                                    }
                                    return PrimaryActionClickBehavior.DISMISS_IMMEDIATELY;
                                })
                        .build();
        messageDispatcher.enqueueMessage(message, webContents, MessageScopeType.NAVIGATION, false);
        return OverrideUrlLoadingResult.forAsyncAction(
                OverrideUrlLoadingAsyncActionType.UI_GATING_INTENT_LAUNCH);
    }

    /**
     * Returns the number of specialized intent handlers in {@params infos}. Specialized intent
     * handlers are intent handlers which handle only a few URLs (e.g. google maps or youtube).
     */
    private int countSpecializedHandlers(List<ResolveInfo> infos) {
        return getSpecializedHandlersWithFilter(
                infos, null, mDelegate.handlesInstantAppLaunchingInternally())
                .size();
    }

    /**
     * Returns the subset of {@params infos} that are specialized intent handlers.
     */
    private ArrayList<String> getSpecializedHandlers(List<ResolveInfo> infos) {
        return getSpecializedHandlersWithFilter(
                infos, null, mDelegate.handlesInstantAppLaunchingInternally());
    }

    private static boolean matchResolveInfoExceptWildCardHost(
            ResolveInfo info, String filterPackageName) {
        IntentFilter intentFilter = info.filter;
        if (intentFilter == null) {
            // Error on the side of classifying ResolveInfo as generic.
            return false;
        }
        if (intentFilter.countDataAuthorities() == 0 && intentFilter.countDataPaths() == 0) {
            // Don't count generic handlers.
            return false;
        }
        boolean isWildCardHost = false;
        Iterator<IntentFilter.AuthorityEntry> it = intentFilter.authoritiesIterator();
        while (it != null && it.hasNext()) {
            IntentFilter.AuthorityEntry entry = it.next();
            if ("*".equals(entry.getHost())) {
                isWildCardHost = true;
                break;
            }
        }
        if (isWildCardHost) {
            return false;
        }
        if (!TextUtils.isEmpty(filterPackageName)
                && (info.activityInfo == null
                        || !info.activityInfo.packageName.equals(filterPackageName))) {
            return false;
        }
        return true;
    }

    /**
     * Check whether the given package is a specialized handler for given ResolveInfos.
     *
     * @param packageName Package name to check against. If null, checks if any package is a
     *         specialized handler.
     * @param infos The list of ResolveInfos to check.
     * @return Whether the given package (or any package if null) is a specialized handler in the
     *         given ResolveInfos.
     */
    public static boolean isPackageSpecializedHandler(String packageName, List<ResolveInfo> infos) {
        return !getSpecializedHandlersWithFilter(infos, packageName, false).isEmpty();
    }

    public static ArrayList<String> getSpecializedHandlersWithFilter(List<ResolveInfo> infos,
            String filterPackageName, boolean handlesInstantAppLaunchingInternally) {
        ArrayList<String> result = new ArrayList<>();
        if (infos == null) {
            return result;
        }

        for (ResolveInfo info : infos) {
            if (!matchResolveInfoExceptWildCardHost(info, filterPackageName)) {
                continue;
            }

            if (info.activityInfo != null) {
                if (handlesInstantAppLaunchingInternally
                        && IntentUtils.isInstantAppResolveInfo(info)) {
                    // Don't add the Instant Apps launcher as a specialized handler if the embedder
                    // handles launching of Instant Apps itself.
                    continue;
                }

                result.add(info.activityInfo.packageName);
            } else {
                result.add("");
            }
        }
        return result;
    }

    protected boolean resolveInfoContainsSelf(List<ResolveInfo> resolveInfos) {
        String packageName = mDelegate.getContext().getPackageName();
        for (ResolveInfo resolveInfo : resolveInfos) {
            ActivityInfo info = resolveInfo.activityInfo;
            if (info != null && packageName.equals(info.packageName)) {
                return true;
            }
        }
        return false;
    }

    /**
     * @return Default SMS application's package name at the system level. Null if there isn't any.
     */
    @VisibleForTesting
    protected String getDefaultSmsPackageNameFromSystem() {
        return Telephony.Sms.getDefaultSmsPackage(ContextUtils.getApplicationContext());
    }

    /**
     * @return The last committed URL from the WebContents.
     */
    @VisibleForTesting
    protected GURL getLastCommittedUrl() {
        if (mDelegate.getWebContents() == null) return null;
        return mDelegate.getWebContents().getLastCommittedUrl();
    }

    /**
     * @param url The requested url.
     * @param permissionNeeded The name of the Android permission needed to access the file.
     * @return Whether we should block the navigation and request file access before proceeding.
     */
    @VisibleForTesting
    protected boolean shouldRequestFileAccess(GURL url, String permissionNeeded) {
        // If the tab is null, then do not attempt to prompt for access.
        if (!mDelegate.hasValidTab()) return false;
        assert url.getScheme().equals(UrlConstants.FILE_SCHEME);
        // If the url points inside of Chromium's data directory, no permissions are necessary.
        // This is required to prevent permission prompt when uses wants to access offline pages.
        if (url.getPath().startsWith(PathUtils.getDataDirectory())) return false;

        return !mDelegate.getWindowAndroid().hasPermission(permissionNeeded)
                && mDelegate.getWindowAndroid().canRequestPermission(permissionNeeded);
    }

    /**
     * @return whether this navigation is from the search results page.
     */
    @VisibleForTesting
    protected boolean isSerpReferrer() {
        GURL referrerUrl = getLastCommittedUrl();
        if (referrerUrl == null || referrerUrl.isEmpty()) return false;

        return UrlUtilitiesJni.get().isGoogleSearchUrl(referrerUrl.getSpec());
    }

    private boolean isInitiatorOriginGoogleReferrer(ExternalNavigationParams params) {
        Origin initiatorOrigin = params.getInitiatorOrigin();
        String url = String.format("%s://%s:%s", initiatorOrigin.getScheme(),
                initiatorOrigin.getHost(), initiatorOrigin.getPort());
        return UrlUtilitiesJni.get().isGoogleSubDomainUrl(url);
    }

    @Deprecated
    private boolean isLastCommittedUrlGoogleReferrer() {
        GURL referrerUrl = getLastCommittedUrl();
        if (referrerUrl == null || referrerUrl.isEmpty()) return false;

        return UrlUtilitiesJni.get().isGoogleSubDomainUrl(referrerUrl.getSpec());
    }

    /**
     * @return whether this navigation is from a Google domain.
     */
    @VisibleForTesting
    protected boolean isAutofillAssistantGoogleReferrer(ExternalNavigationParams params) {
        if (!ExternalIntentsFeatures.AUTOFILL_ASSISTANT_GOOGLE_INITIATOR_ORIGIN_CHECK.isEnabled()) {
            return isLastCommittedUrlGoogleReferrer();
        }

        // We check that there were no previous redirects and that the navigation originated on
        // a valid google.<TLD> (top level domain).
        RedirectHandler redirectHandler = params.getRedirectHandler();
        if (redirectHandler == null || !redirectHandler.isOnFirstLoadInNavigationChain()) {
            return false;
        }

        return isInitiatorOriginGoogleReferrer(params);
    }

    /**
     * @return whether this navigation is a redirect from an intent.
     */
    private static boolean isIncomingIntentRedirect(ExternalNavigationParams params) {
        boolean isOnEffectiveIntentRedirect = params.getRedirectHandler() == null
                ? false
                : params.getRedirectHandler().isOnNoninitialLoadForIntentNavigationChain();
        return (params.isFromIntent() && params.isRedirect()) || isOnEffectiveIntentRedirect;
    }

    /**
     * Checks whether {@param intent} is for an Instant App. Considers both package and actions that
     * would resolve to Supervisor.
     * @return Whether the given intent is going to open an Instant App.
     */
    private static boolean isIntentToInstantApp(Intent intent) {
        if (INSTANT_APP_SUPERVISOR_PKG.equals(intent.getPackage())) return true;

        String intentAction = intent.getAction();
        for (String action : INSTANT_APP_START_ACTIONS) {
            if (action.equals(intentAction)) {
                return true;
            }
        }
        return false;
    }
}
