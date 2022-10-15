// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser;

import static org.chromium.chrome.browser.base.SplitCompatApplication.CHROME_SPLIT_NAME;

import android.app.ActivityManager.TaskDescription;
import android.content.Context;
import android.content.SharedPreferences;
import android.content.res.Configuration;
import android.content.res.Resources;
import android.graphics.BitmapFactory;
import android.os.Build;
import android.os.Bundle;

import androidx.annotation.CallSuper;
import androidx.annotation.Nullable;
import androidx.annotation.RequiresApi;
import androidx.annotation.StyleRes;
import androidx.appcompat.app.AppCompatActivity;

import com.google.android.material.color.DynamicColors;

import org.chromium.base.BundleUtils;
import org.chromium.base.ContextUtils;
import org.chromium.base.metrics.RecordHistogram;
import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.base.supplier.ObservableSupplierImpl;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.base.SplitChromeApplication;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.language.GlobalAppLocaleController;
import org.chromium.chrome.browser.metrics.UmaSessionStats;
import org.chromium.chrome.browser.night_mode.GlobalNightModeStateProviderHolder;
import org.chromium.chrome.browser.night_mode.NightModeStateProvider;
import org.chromium.chrome.browser.night_mode.NightModeUtils;
import org.chromium.ui.modaldialog.ModalDialogManager;
import org.chromium.ui.modaldialog.ModalDialogManagerHolder;

import java.util.LinkedHashSet;

/**
 * A subclass of {@link AppCompatActivity} that maintains states and objects applied to all
 * activities in {@link ChromeApplication} (e.g. night mode).
 */
public class ChromeBaseAppCompatActivity extends AppCompatActivity
        implements NightModeStateProvider.Observer, ModalDialogManagerHolder {
    private final ObservableSupplierImpl<ModalDialogManager> mModalDialogManagerSupplier =
            new ObservableSupplierImpl<>();
    private NightModeStateProvider mNightModeStateProvider;
    private LinkedHashSet<Integer> mThemeResIds = new LinkedHashSet<>();

    @Override
    protected void attachBaseContext(Context newBase) {
        super.attachBaseContext(newBase);

        // Make sure the "chrome" split is loaded before checking if ClassLoaders are equal.
        SplitChromeApplication.finishPreload(CHROME_SPLIT_NAME);
        ClassLoader chromeModuleClassLoader = ChromeBaseAppCompatActivity.class.getClassLoader();
        Context appContext = ContextUtils.getApplicationContext();
        if (!chromeModuleClassLoader.equals(appContext.getClassLoader())) {
            // This should only happen on Android O. See crbug.com/1146745 for more info.
            throw new IllegalStateException("ClassLoader mismatch detected.\nA: "
                    + chromeModuleClassLoader + "\nB: " + appContext.getClassLoader()
                    + "\nC: " + chromeModuleClassLoader.getParent()
                    + "\nD: " + appContext.getClassLoader().getParent() + "\nE: " + appContext);
        }
        // If ClassLoader was corrected by SplitCompatAppComponentFactory, also need to correct
        // the reference in the associated Context.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            BundleUtils.checkContextClassLoader(newBase, this);
        }

        mNightModeStateProvider = createNightModeStateProvider();

        Configuration config = new Configuration();
        // Pre-Android O, fontScale gets initialized to 1 in the constructor. Set it to 0 so
        // that applyOverrideConfiguration() does not interpret it as an overridden value.
        // https://crbug.com/834191
        config.fontScale = 0;
        // NightMode and other applyOverrides must be done before onCreate in attachBaseContext.
        // https://crbug.com/1139760
        if (applyOverrides(newBase, config)) applyOverrideConfiguration(config);
    }

    @Override
    protected void onCreate(@Nullable Bundle savedInstanceState) {
        BundleUtils.restoreLoadedSplits(savedInstanceState);
        mModalDialogManagerSupplier.set(createModalDialogManager());

        initializeNightModeStateProvider();
        mNightModeStateProvider.addObserver(this);
        super.onCreate(savedInstanceState);
        applyThemeOverlays();

        // Activity level locale overrides must be done in onCreate.
        GlobalAppLocaleController.getInstance().maybeOverrideContextConfig(this);

        setDefaultTaskDescription();
    }

    @Override
    protected void onDestroy() {
        mNightModeStateProvider.removeObserver(this);
        if (mModalDialogManagerSupplier.get() != null) {
            mModalDialogManagerSupplier.get().destroy();
            mModalDialogManagerSupplier.set(null);
        }
        super.onDestroy();
    }

    @Override
    public ClassLoader getClassLoader() {
        // Replace the default ClassLoader with a custom SplitAware one so that
        // LayoutInflaters that use this ClassLoader can find view classes that
        // live inside splits. Very useful when FragmentManger tries to inflate
        // the UI automatically on restore.
        return BundleUtils.getSplitCompatClassLoader();
    }

    @Override
    protected void onSaveInstanceState(Bundle outState) {
        super.onSaveInstanceState(outState);
        BundleUtils.saveLoadedSplits(outState);
    }

    @Override
    public void setTheme(@StyleRes int resid) {
        super.setTheme(resid);
        mThemeResIds.add(resid);
    }

    @Override
    @RequiresApi(Build.VERSION_CODES.O)
    public void onMultiWindowModeChanged(boolean inMultiWindowMode, Configuration configuration) {
        super.onMultiWindowModeChanged(inMultiWindowMode, configuration);
        onMultiWindowModeChanged(inMultiWindowMode);
    }

    @Override
    public void onConfigurationChanged(Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
        NightModeUtils.updateConfigurationForNightMode(
                this, mNightModeStateProvider.isInNightMode(), newConfig, mThemeResIds);
        // newConfig will have the default system locale so reapply the app locale override if
        // needed: https://crbug.com/1248944
        GlobalAppLocaleController.getInstance().maybeOverrideContextConfig(this);
    }

    // Implementation of ModalDialogManagerHolder
    /**
     * @return The {@link ModalDialogManager} that manages the display of modal dialogs (e.g.
     *         JavaScript dialogs).
     */
    @Override
    public ModalDialogManager getModalDialogManager() {
        // TODO(jinsukkim): Remove this method in favor of getModalDialogManagerSupplier().
        return getModalDialogManagerSupplier().get();
    }

    /**
     * Returns the supplier of {@link ModalDialogManager} that manages the display of modal dialogs.
     */
    public ObservableSupplier<ModalDialogManager> getModalDialogManagerSupplier() {
        return mModalDialogManagerSupplier;
    }

    /**
     * Creates a {@link ModalDialogManager} for this class. Subclasses that need one should override
     * this method.
     */
    @Nullable
    protected ModalDialogManager createModalDialogManager() {
        return null;
    }

    /**
     * Called during {@link #attachBaseContext(Context)} to allow configuration overrides to be
     * applied. If this methods return true, the overrides will be applied using
     * {@link #applyOverrideConfiguration(Configuration)}.
     * @param baseContext The base {@link Context} attached to this class.
     * @param overrideConfig The {@link Configuration} that will be passed to
     *                       @link #applyOverrideConfiguration(Configuration)} if necessary.
     * @return True if any configuration overrides were applied, and false otherwise.
     */
    @CallSuper
    protected boolean applyOverrides(Context baseContext, Configuration overrideConfig) {
        return NightModeUtils.applyOverridesForNightMode(
                getNightModeStateProvider(), overrideConfig);
    }

    /**
     * @return The {@link NightModeStateProvider} that provides the state of night mode.
     */
    protected final NightModeStateProvider getNightModeStateProvider() {
        return mNightModeStateProvider;
    }

    /**
     * @return The {@link NightModeStateProvider} that provides the state of night mode in the scope
     *         of this class.
     */
    protected NightModeStateProvider createNightModeStateProvider() {
        return GlobalNightModeStateProviderHolder.getInstance();
    }

    /**
     * Initializes the initial night mode state. This will be called at the beginning of
     * {@link #onCreate(Bundle)} so that the correct theme can be applied for the initial night mode
     * state.
     */
    protected void initializeNightModeStateProvider() {}

    /**
     * Apply theme overlay to this activity class.
     */
    @CallSuper
    protected void applyThemeOverlays() {
        setTheme(R.style.ColorOverlay_ChromiumAndroid);
        DynamicColors.applyToActivityIfAvailable(this);

        DeferredStartupHandler.getInstance().addDeferredTask(() -> {
            // #registerSyntheticFieldTrial requires native.
            boolean isDynamicColorAvailable = DynamicColors.isDynamicColorAvailable();
            RecordHistogram.recordBooleanHistogram(
                    "Android.DynamicColors.IsAvailable", isDynamicColorAvailable);
            UmaSessionStats.registerSyntheticFieldTrial(
                    "IsDynamicColorAvailable", isDynamicColorAvailable ? "Enabled" : "Disabled");
        });

        // Try to enable browser overscroll when content overscroll is enabled for consistency. This
        // needs to be in a cached feature because activity startup happens before native is
        // initialized. Unfortunately content overscroll is read in renderer threads, and these two
        // are not synchronized. Typically the first time overscroll is enabled, the following will
        // use the old value and then content will pick up the enabled value, causing one execution
        // of inconsistency.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S
                && !ChromeFeatureList.sElasticOverscroll.isEnabled()) {
            setTheme(R.style.ThemeOverlay_DisableOverscroll);
        }

        // TODO(https://crbug.com/1345778): Remove these overlays.
        // We apply an extra theme overlay to override some of the dynamic colors. For example,
        // android:textColorHighlight is overridden by dynamic colors, preventing us from specifying
        // the alpha for the selected text highlight. In this case, the overridden colors should
        // still use dynamic colors, as in the android:textColorHighlight example where we use a
        // color state list that depends on colorPrimary.
        setTheme(R.style.ThemeOverlay_DynamicColorOverrides);
        setTheme(R.style.ThemeOverlay_DynamicButtons);
    }

    /**
     * Sets the default task description that will appear in the recents UI.
     */
    protected void setDefaultTaskDescription() {
        final Resources res = getResources();
        final TaskDescription taskDescription =
                new TaskDescription(res.getString(R.string.app_name),
                        BitmapFactory.decodeResource(res, R.mipmap.app_icon),
                        res.getColor(R.color.default_task_description_color));
        setTaskDescription(taskDescription);
    }

    // NightModeStateProvider.Observer implementation.
    @Override
    public void onNightModeStateChanged() {
        if (!isFinishing()) recreate();
    }

    /**
     * Required to make preference fragments use InMemorySharedPreferences in tests.
     */
    @Override
    public SharedPreferences getSharedPreferences(String name, int mode) {
        return ContextUtils.getApplicationContext().getSharedPreferences(name, mode);
    }
}
