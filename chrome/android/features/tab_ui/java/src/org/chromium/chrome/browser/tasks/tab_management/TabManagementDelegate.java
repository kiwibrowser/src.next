// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tasks.tab_management;

import android.app.Activity;
import android.content.Context;
import android.util.Pair;
import android.view.View.OnClickListener;
import android.view.ViewGroup;

import androidx.annotation.IntDef;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.base.supplier.OneshotSupplier;
import org.chromium.base.supplier.Supplier;
import org.chromium.chrome.browser.back_press.BackPressManager;
import org.chromium.chrome.browser.browser_controls.BrowserControlsStateProvider;
import org.chromium.chrome.browser.compositor.layouts.Layout;
import org.chromium.chrome.browser.compositor.layouts.LayoutRenderHost;
import org.chromium.chrome.browser.compositor.layouts.LayoutUpdateHost;
import org.chromium.chrome.browser.compositor.layouts.content.TabContentManager;
import org.chromium.chrome.browser.hub.Pane;
import org.chromium.chrome.browser.incognito.reauth.IncognitoReauthController;
import org.chromium.chrome.browser.layouts.LayoutStateProvider;
import org.chromium.chrome.browser.lifecycle.ActivityLifecycleDispatcher;
import org.chromium.chrome.browser.multiwindow.MultiWindowModeStateDispatcher;
import org.chromium.chrome.browser.profiles.ProfileProvider;
import org.chromium.chrome.browser.tabmodel.IncognitoStateProvider;
import org.chromium.chrome.browser.tabmodel.TabCreatorManager;
import org.chromium.chrome.browser.tabmodel.TabModelSelector;
import org.chromium.chrome.browser.ui.messages.snackbar.SnackbarManager;
import org.chromium.components.browser_ui.bottomsheet.BottomSheetController;
import org.chromium.components.browser_ui.widget.MenuOrKeyboardActionController;
import org.chromium.components.browser_ui.widget.scrim.ScrimCoordinator;
import org.chromium.ui.modaldialog.ModalDialogManager;
import org.chromium.ui.resources.dynamics.DynamicResourceLoader;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;

/** Interface to get access to components concerning tab management. */
public interface TabManagementDelegate {
    @IntDef({
        TabSwitcherType.GRID,
        TabSwitcherType.SINGLE,
        TabSwitcherType.NONE
    })
    @Retention(RetentionPolicy.SOURCE)
    public @interface TabSwitcherType {
        int GRID = 0;
        // int CAROUSEL_DEPRECATED = 1;
        int SINGLE = 2;
        int NONE = 3;
    }

    /**
     * Create the {@link TabSwitcherLayout}.
     * @param context The current Android's context.
     * @param updateHost The parent {@link LayoutUpdateHost}.
     * @param layoutStateProvider The {@link LayoutStateProvider} to provide layout state changes.
     * @param renderHost The parent {@link LayoutRenderHost}.
     * @param browserControlsStateProvider The {@link BrowserControlsStateProvider} for the top
     *         controls.
     * @param tabSwitcher The {@link TabSwitcher} the layout should own.
     * @param tabSwitcherScrimAnchor {@link ViewGroup} used by tab switcher layout to show scrim
     *         when overview is visible.
     * @param scrimCoordinator {@link ScrimCoordinator} to show/hide scrim.
     * @return The {@link TabSwitcherLayout}.
     */
    Layout createTabSwitcherLayout(
            Context context,
            LayoutUpdateHost updateHost,
            LayoutStateProvider layoutStateProvider,
            LayoutRenderHost renderHost,
            BrowserControlsStateProvider browserControlsStateProvider,
            TabSwitcher tabSwitcher,
            ViewGroup tabSwitcherScrimAnchor,
            ScrimCoordinator scrimCoordinator);

    /**
     * Create the {@link TabSwitcher} to display Tabs in grid.
     * @param activity The current android {@link Activity}.
     * @param activityLifecycleDispatcher Allows observation of the activity lifecycle.
     * @param tabModelSelector Gives access to the current set of {@TabModel}.
     * @param tabContentManager Gives access to the tab content.
     * @param browserControlsStateProvider Gives access to the state of the browser controls.
     * @param tabCreatorManager Manages creation of tabs.
     * @param menuOrKeyboardActionController allows access to menu or keyboard actions.
     * @param containerView The {@link ViewGroup} to add the switcher to.
     * @param multiWindowModeStateDispatcher Gives access to the multi window mode state.
     * @param scrimCoordinator The {@link ScrimCoordinator} to control the scrim view.
     * @param rootView The root view of the app.
     * @param dynamicResourceLoaderSupplier Supplies the current {@link DynamicResourceLoader}.
     * @param snackbarManager Manages the snackbar.
     * @param modalDialogManager Manages modal dialogs.
     * @param incognitoReauthControllerSupplier {@link OneshotSupplier<IncognitoReauthController>}
     *         to detect pending re-auth when tab switcher is shown.
     * @param backPressManager {@link BackPressManager} to handle back press gesture.
     * @param layoutStateProviderSupplier {@link OneshotSupplier<LayoutStateProvider>} to provide
     *                                    the layout state changes.
     * @return The {@link TabSwitcher}.
     */
    TabSwitcher createGridTabSwitcher(
            @NonNull Activity activity,
            @NonNull ActivityLifecycleDispatcher activityLifecycleDispatcher,
            @NonNull TabModelSelector tabModelSelector,
            @NonNull TabContentManager tabContentManager,
            @NonNull BrowserControlsStateProvider browserControlsStateProvider,
            @NonNull TabCreatorManager tabCreatorManager,
            @NonNull MenuOrKeyboardActionController menuOrKeyboardActionController,
            @NonNull ViewGroup containerView,
            @NonNull MultiWindowModeStateDispatcher multiWindowModeStateDispatcher,
            @NonNull ScrimCoordinator scrimCoordinator,
            @NonNull ViewGroup rootView,
            @NonNull Supplier<DynamicResourceLoader> dynamicResourceLoaderSupplier,
            @NonNull SnackbarManager snackbarManager,
            @NonNull ModalDialogManager modalDialogManager,
            @NonNull OneshotSupplier<IncognitoReauthController> incognitoReauthControllerSupplier,
            @Nullable BackPressManager backPressManager,
            @Nullable OneshotSupplier<LayoutStateProvider> layoutStateProviderSupplier);

    /**
     * Create the {@link TabGroupUi}.
     * @param activity The {@link Activity} that creates this surface.
     * @param parentView The parent view of this UI.
     * @param browserControlsStateProvider The {@link BrowserControlsStateProvider} of the top
     *                                     controls.
     * @param incognitoStateProvider Observable provider of incognito state.
     * @param scrimCoordinator The {@link ScrimCoordinator} to control scrim view.
     * @param omniboxFocusStateSupplier Supplier to access the focus state of the omnibox.
     * @param bottomSheetController Controls the state of the bottom sheet.
     * @param activityLifecycleDispatcher Allows observation of the activity lifecycle.
     * @param isWarmOnResumeSupplier Supplies whether the app was warm on resume.
     * @param tabModelSelector Gives access to the current set of {@TabModel}.
     * @param tabContentManager Gives access to the tab content.
     * @param rootView The root view of the app.
     * @param dynamicResourceLoaderSupplier Supplies the current {@link DynamicResourceLoader}.
     * @param tabCreatorManager Manages creation of tabs.
     * @param layoutStateProviderSupplier Supplies the {@link LayoutStateProvider}.
     * @param snackbarManager Manages the display of snackbars.
     * @return The {@link TabGroupUi}.
     */
    TabGroupUi createTabGroupUi(
            @NonNull Activity activity,
            @NonNull ViewGroup parentView,
            @NonNull BrowserControlsStateProvider browserControlsStateProvider,
            @NonNull IncognitoStateProvider incognitoStateProvider,
            @NonNull ScrimCoordinator scrimCoordinator,
            @NonNull ObservableSupplier<Boolean> omniboxFocusStateSupplier,
            @NonNull BottomSheetController bottomSheetController,
            @NonNull ActivityLifecycleDispatcher activityLifecycleDispatcher,
            @NonNull Supplier<Boolean> isWarmOnResumeSupplier,
            @NonNull TabModelSelector tabModelSelector,
            @NonNull TabContentManager tabContentManager,
            @NonNull ViewGroup rootView,
            @NonNull Supplier<DynamicResourceLoader> dynamicResourceLoaderSupplier,
            @NonNull TabCreatorManager tabCreatorManager,
            @NonNull OneshotSupplier<LayoutStateProvider> layoutStateProviderSupplier,
            @NonNull SnackbarManager snackbarManager);

    /**
     * Create a {@link TabSwitcher} and {@link Pane} for the Hub.
     *
     * @param activity The {@link Activity} that hosts the pane.
     * @param lifecycleDispatcher The lifecycle dispatcher for the activity.
     * @param profileProviderSupplier The supplier for profiles.
     * @param tabModelSelector For access to {@link TabModel}.
     * @param tabContentManager For management of thumbnails.
     * @param tabCreatorManager For creating new tabs.
     * @param browserControlsStateProvider For determining thumbnail size.
     * @param multiWindowModeStateDispatcher For managing behavior in multi-window.
     * @param rootUiScrimCoordinator The root UI coordinator's scrim coordinator. On LFF this is
     *     unused as the root UI's scrim coordinator is used for the show/hide animation.
     * @param snackbarManager The activity level snackbar manager.
     * @param modalDialogManager The modal dialog manager for the activity.
     * @param incognitoReauthControllerSupplier The incognito reauth controller supplier.
     * @param newTabButtonOnClickListener The listener for clicking the new tab button.
     * @param isIncognito Whether this is an incognito pane.
     */
    Pair<TabSwitcher, Pane> createTabSwitcherPane(
            @NonNull Activity activity,
            @NonNull ActivityLifecycleDispatcher lifecycleDispatcher,
            @NonNull OneshotSupplier<ProfileProvider> profileProviderSupplier,
            @NonNull TabModelSelector tabModelSelector,
            @NonNull TabContentManager tabContentManager,
            @NonNull TabCreatorManager tabCreatorManager,
            @NonNull BrowserControlsStateProvider browserControlsStateProvider,
            @NonNull MultiWindowModeStateDispatcher multiWindowModeStateDispatcher,
            @NonNull ScrimCoordinator rootUiScrimCoordinator,
            @NonNull SnackbarManager snackbarManager,
            @NonNull ModalDialogManager modalDialogManager,
            @Nullable OneshotSupplier<IncognitoReauthController> incognitoReauthControllerSupplier,
            @NonNull OnClickListener newTabButtonOnClickListener,
            boolean isIncognito);
}
