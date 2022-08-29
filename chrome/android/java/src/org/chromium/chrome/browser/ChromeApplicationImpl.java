// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser;

import android.app.Application;
import android.content.Intent;
import android.content.res.Configuration;
import android.os.Bundle;

import androidx.annotation.Nullable;

import org.chromium.base.ApplicationStatus;
import org.chromium.base.library_loader.LibraryLoader;
import org.chromium.chrome.browser.app.notifications.ContextualNotificationPermissionRequesterImpl;
import org.chromium.chrome.browser.background_task_scheduler.ChromeBackgroundTaskFactory;
import org.chromium.chrome.browser.base.SplitCompatApplication;
import org.chromium.chrome.browser.crash.ChromePureJavaExceptionReporter;
import org.chromium.chrome.browser.customtabs.CustomTabsConnection;
import org.chromium.chrome.browser.dependency_injection.ChromeAppComponent;
import org.chromium.chrome.browser.dependency_injection.ChromeAppModule;
import org.chromium.chrome.browser.dependency_injection.DaggerChromeAppComponent;
import org.chromium.chrome.browser.dependency_injection.ModuleFactoryOverrides;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.fonts.FontPreloader;
import org.chromium.chrome.browser.night_mode.SystemNightModeMonitor;
import org.chromium.chrome.browser.profiles.ProfileResolver;
import org.chromium.chrome.browser.vr.OnExitVrRequestListener;
import org.chromium.chrome.browser.vr.VrModuleProvider;
import org.chromium.components.browser_ui.util.GlobalDiscardableReferencePool;
import org.chromium.components.embedder_support.browser_context.PartitionResolverSupplier;
import org.chromium.components.module_installer.util.ModuleUtil;
import org.chromium.components.version_info.Channel;
import org.chromium.components.version_info.VersionConstants;
import org.chromium.url.GURL;

/**
 * Basic application functionality that should be shared among all browser applications that use
 * chrome layer.
 *
 * Note: All application logic should be added to {@link ChromeApplicationImpl}, which will be
 * called from the superclass. See {@link SplitCompatApplication} for more info.
 */
public class ChromeApplicationImpl extends SplitCompatApplication.Impl {
    /** Lock on creation of sComponent. */
    private static final Object sLock = new Object();
    @Nullable
    private static volatile ChromeAppComponent sComponent;

    public ChromeApplicationImpl() {}

    @Override
    public void onCreate() {
        super.onCreate();

        if (SplitCompatApplication.isBrowserProcess()) {
            FontPreloader.getInstance().load(getApplication());

            // Only load the native library early for bundle builds since some tests use the
            // "--disable-native-initialization" switch, and the CommandLine is not initialized at
            // this point to check.
            if (ChromeFeatureList.sEarlyLibraryLoad.isEnabled() && ProductConfig.IS_BUNDLE) {
                // Kick off library loading in a separate thread so it's ready when we need it.
                new Thread(() -> LibraryLoader.getInstance().ensureMainDexInitialized()).start();
            }

            ApplicationStatus.registerStateListenerForAllActivities(
                    ChromePowerModeVoter.getInstance());

            // Initializes the support for dynamic feature modules (browser only).
            ModuleUtil.initApplication();

            if (VersionConstants.CHANNEL == Channel.CANARY) {
                GURL.setReportDebugThrowableCallback(
                        ChromePureJavaExceptionReporter::reportJavaException);
            }

            // Set Chrome factory for mapping BackgroundTask classes to TaskIds.
            ChromeBackgroundTaskFactory.setAsDefault();
            ContextualNotificationPermissionRequesterImpl.initialize();
            PartitionResolverSupplier.setInstance(new ProfileResolver());

            AppHooks.get().getChimeDelegate().initialize();
        }
    }

    @Override
    public void onTrimMemory(int level) {
        super.onTrimMemory(level);
        if (isSevereMemorySignal(level)
                && GlobalDiscardableReferencePool.getReferencePool() != null) {
            GlobalDiscardableReferencePool.getReferencePool().drain();
        }
        CustomTabsConnection.onTrimMemory(level);
    }

    @Override
    public void startActivity(Intent intent, Bundle options) {
        if (VrModuleProvider.getDelegate().canLaunch2DIntents()
                || VrModuleProvider.getIntentDelegate().isVrIntent(intent)) {
            super.startActivity(intent, options);
            return;
        }

        VrModuleProvider.getDelegate().requestToExitVr(new OnExitVrRequestListener() {
            @Override
            public void onSucceeded() {
                if (!VrModuleProvider.getDelegate().canLaunch2DIntents()) {
                    throw new IllegalStateException("Still in VR after having exited VR.");
                }
                startActivity(intent, options);
            }

            @Override
            public void onDenied() {}
        });
    }

    @Override
    public void onConfigurationChanged(Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
        // TODO(huayinz): Add observer pattern for application configuration changes.
        if (SplitCompatApplication.isBrowserProcess()) {
            SystemNightModeMonitor.getInstance().onApplicationConfigurationChanged();
        }
    }

    /**
     * Determines whether the given memory signal is considered severe.
     * @param level The type of signal as defined in {@link android.content.ComponentCallbacks2}.
     */
    public static boolean isSevereMemorySignal(int level) {
        // The conditions are expressed using ranges to capture intermediate levels possibly added
        // to the API in the future.
        return (level >= Application.TRIM_MEMORY_RUNNING_LOW
                       && level < Application.TRIM_MEMORY_UI_HIDDEN)
                || level >= Application.TRIM_MEMORY_MODERATE;
    }

    /** Returns the application-scoped component. */
    public static ChromeAppComponent getComponent() {
        if (sComponent == null) {
            synchronized (sLock) {
                if (sComponent == null) {
                    sComponent = createComponent();
                }
            }
        }
        return sComponent;
    }

    private static ChromeAppComponent createComponent() {
        ChromeAppModule.Factory overriddenFactory =
                ModuleFactoryOverrides.getOverrideFor(ChromeAppModule.Factory.class);
        ChromeAppModule module =
                overriddenFactory == null ? new ChromeAppModule() : overriddenFactory.create();

        AppHooksModule.Factory appHooksFactory =
                ModuleFactoryOverrides.getOverrideFor(AppHooksModule.Factory.class);
        AppHooksModule appHooksModule =
                appHooksFactory == null ? new AppHooksModule() : appHooksFactory.create();

        return DaggerChromeAppComponent.builder()
                .chromeAppModule(module)
                .appHooksModule(appHooksModule)
                .build();
    }
}
