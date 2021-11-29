// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.app.appmenu;

import android.content.Context;
import android.content.pm.ResolveInfo;
import android.content.res.Resources;
import android.graphics.drawable.Drawable;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.Menu;
import android.view.MenuInflater;
import android.view.MenuItem;
import android.view.SubMenu;
import android.view.View;
import android.widget.PopupMenu;

import androidx.annotation.ColorRes;
import androidx.annotation.IdRes;
import androidx.annotation.IntDef;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;
import androidx.appcompat.content.res.AppCompatResources;
import androidx.core.graphics.drawable.DrawableCompat;

import org.chromium.base.Callback;
import org.chromium.base.CallbackController;
import org.chromium.base.CommandLine;
import org.chromium.base.ContextUtils;
import org.chromium.base.metrics.RecordHistogram;
import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.base.supplier.OneshotSupplier;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.ActivityTabProvider;
import org.chromium.chrome.browser.banners.AppMenuVerbiage;
import org.chromium.chrome.browser.bookmarks.BookmarkBridge;
import org.chromium.chrome.browser.bookmarks.BookmarkFeatures;
import org.chromium.chrome.browser.bookmarks.ReadingListFeatures;
import org.chromium.chrome.browser.compositor.layouts.OverviewModeBehavior;
import org.chromium.chrome.browser.device.DeviceClassManager;
import org.chromium.chrome.browser.device.DeviceConditions;
import org.chromium.chrome.browser.download.DownloadUtils;
import org.chromium.chrome.browser.flags.CachedFeatureFlags;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.flags.ChromeSwitches;
import org.chromium.chrome.browser.image_descriptions.ImageDescriptionsController;
import org.chromium.chrome.browser.incognito.IncognitoUtils;
import org.chromium.chrome.browser.multiwindow.MultiWindowModeStateDispatcher;
import org.chromium.chrome.browser.multiwindow.MultiWindowUtils;
import org.chromium.chrome.browser.night_mode.WebContentsDarkModeController;
import org.chromium.chrome.browser.omaha.UpdateMenuItemHelper;
import org.chromium.chrome.browser.partnercustomizations.PartnerBrowserCustomizations;
import org.chromium.chrome.browser.preferences.Pref;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.share.ShareHelper;
import org.chromium.chrome.browser.share.ShareUtils;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tabmodel.TabModelSelector;
import org.chromium.chrome.browser.tasks.ReturnToChromeExperimentsUtil;
import org.chromium.chrome.browser.tasks.tab_management.PriceTrackingUtilities;
import org.chromium.chrome.browser.tasks.tab_management.TabUiFeatureUtilities;
import org.chromium.chrome.browser.toolbar.ToolbarManager;
import org.chromium.chrome.browser.translate.TranslateUtils;
import org.chromium.chrome.browser.ui.appmenu.AppMenuHandler;
import org.chromium.chrome.browser.ui.appmenu.AppMenuHandler.AppMenuItemType;
import org.chromium.chrome.browser.ui.appmenu.AppMenuItemProperties;
import org.chromium.chrome.browser.ui.appmenu.AppMenuPropertiesDelegate;
import org.chromium.chrome.browser.ui.appmenu.AppMenuUtil;
import org.chromium.chrome.browser.ui.appmenu.CustomViewBinder;
import org.chromium.chrome.features.start_surface.StartSurface;
import org.chromium.chrome.features.start_surface.StartSurfaceState;
import org.chromium.components.dom_distiller.core.DomDistillerUrlUtils;
import org.chromium.components.embedder_support.util.UrlConstants;
import org.chromium.components.embedder_support.util.UrlUtilities;
import org.chromium.components.user_prefs.UserPrefs;
import org.chromium.components.webapk.lib.client.WebApkValidator;
import org.chromium.components.webapps.AppBannerManager;
import org.chromium.components.webapps.WebappsUtils;
import org.chromium.net.ConnectionType;
import org.chromium.ui.base.DeviceFormFactor;
import org.chromium.ui.modelutil.MVCListAdapter;
import org.chromium.ui.modelutil.MVCListAdapter.ModelList;
import org.chromium.ui.modelutil.PropertyModel;
import org.chromium.url.GURL;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/**
 * Base implementation of {@link AppMenuPropertiesDelegate} that handles hiding and showing menu
 * items based on activity state.
 */
public class AppMenuPropertiesDelegateImpl implements AppMenuPropertiesDelegate {
    private static Boolean sItemBookmarkedForTesting;

    protected PropertyModel mReloadPropertyModel;

    protected final Context mContext;
    protected final boolean mIsTablet;
    protected final ActivityTabProvider mActivityTabProvider;
    protected final MultiWindowModeStateDispatcher mMultiWindowModeStateDispatcher;
    protected final TabModelSelector mTabModelSelector;
    protected final ToolbarManager mToolbarManager;
    protected final View mDecorView;
    private CallbackController mCallbackController = new CallbackController();
    private final ObservableSupplier<BookmarkBridge> mBookmarkBridgeSupplier;
    private Callback<BookmarkBridge> mBookmarkBridgeSupplierCallback;
    private boolean mUpdateMenuItemVisible;
    private ShareUtils mShareUtils;
    // Keeps track of which menu item was shown when installable app is detected.
    private int mAddAppTitleShown;
    private Map<CustomViewBinder, Integer> mCustomViewTypeOffsetMap;

    @VisibleForTesting
    @IntDef({MenuGroup.INVALID, MenuGroup.PAGE_MENU, MenuGroup.OVERVIEW_MODE_MENU,
            MenuGroup.TABLET_EMPTY_MODE_MENU})
    @interface MenuGroup {
        int INVALID = -1;
        int PAGE_MENU = 0;
        int OVERVIEW_MODE_MENU = 1;
        int TABLET_EMPTY_MODE_MENU = 2;
    }

    // Please treat this list as append only and keep it in sync with
    // AppMenuHighlightItem in enums.xml.
    @IntDef({AppMenuHighlightItem.UNKNOWN, AppMenuHighlightItem.DOWNLOADS,
            AppMenuHighlightItem.BOOKMARKS, AppMenuHighlightItem.TRANSLATE,
            AppMenuHighlightItem.ADD_TO_HOMESCREEN, AppMenuHighlightItem.DOWNLOAD_THIS_PAGE,
            AppMenuHighlightItem.BOOKMARK_THIS_PAGE, AppMenuHighlightItem.DATA_REDUCTION_FOOTER})
    @Retention(RetentionPolicy.SOURCE)
    @interface AppMenuHighlightItem {
        int UNKNOWN = 0;
        int DOWNLOADS = 1;
        int BOOKMARKS = 2;
        int TRANSLATE = 3;
        int ADD_TO_HOMESCREEN = 4;
        int DOWNLOAD_THIS_PAGE = 5;
        int BOOKMARK_THIS_PAGE = 6;
        int DATA_REDUCTION_FOOTER = 7;
        int NUM_ENTRIES = 8;
    }

    protected @Nullable OverviewModeBehavior mOverviewModeBehavior;
    private @Nullable OneshotSupplier<StartSurface> mStartSurfaceSupplier;
    private @Nullable StartSurface.StateObserver mStartSurfaceStateObserver;
    private @StartSurfaceState int mStartSurfaceState;
    protected BookmarkBridge mBookmarkBridge;
    protected Runnable mAppMenuInvalidator;

    /**
     * Construct a new {@link AppMenuPropertiesDelegateImpl}.
     * @param context The activity context.
     * @param activityTabProvider The {@link ActivityTabProvider} for the containing activity.
     * @param multiWindowModeStateDispatcher The {@link MultiWindowModeStateDispatcher} for the
     *         containing activity.
     * @param tabModelSelector The {@link TabModelSelector} for the containing activity.
     * @param toolbarManager The {@link ToolbarManager} for the containing activity.
     * @param decorView The decor {@link View}, e.g. from Window#getDecorView(), for the containing
     *         activity.
     * @param overviewModeBehaviorSupplier An {@link ObservableSupplier} for the
     *         {@link OverviewModeBehavior} associated with the containing activity.
     * @param startSurfaceSupplier An {@link OneshotSupplier} for the Start surface.
     * @param bookmarkBridgeSupplier An {@link ObservableSupplier} for the {@link BookmarkBridge}
     *         associated with the containing activity.
     */
    public AppMenuPropertiesDelegateImpl(Context context, ActivityTabProvider activityTabProvider,
            MultiWindowModeStateDispatcher multiWindowModeStateDispatcher,
            TabModelSelector tabModelSelector, ToolbarManager toolbarManager, View decorView,
            @Nullable OneshotSupplier<OverviewModeBehavior> overviewModeBehaviorSupplier,
            @Nullable OneshotSupplier<StartSurface> startSurfaceSupplier,
            ObservableSupplier<BookmarkBridge> bookmarkBridgeSupplier) {
        mContext = context;
        mIsTablet = DeviceFormFactor.isNonMultiDisplayContextOnTablet(mContext);
        mActivityTabProvider = activityTabProvider;
        mMultiWindowModeStateDispatcher = multiWindowModeStateDispatcher;
        mTabModelSelector = tabModelSelector;
        mToolbarManager = toolbarManager;
        mDecorView = decorView;

        if (overviewModeBehaviorSupplier != null) {
            overviewModeBehaviorSupplier.onAvailable(mCallbackController.makeCancelable(
                    overviewModeBehavior -> { mOverviewModeBehavior = overviewModeBehavior; }));
        }

        if (startSurfaceSupplier != null) {
            mStartSurfaceSupplier = startSurfaceSupplier;
            startSurfaceSupplier.onAvailable(mCallbackController.makeCancelable((startSurface) -> {
                mStartSurfaceState = startSurface.getController().getStartSurfaceState();
                mStartSurfaceStateObserver = (newState, shouldShowToolbar) -> {
                    assert ReturnToChromeExperimentsUtil.isStartSurfaceEnabled(mContext);
                    mStartSurfaceState = newState;
                };
                startSurface.addStateChangeObserver(mStartSurfaceStateObserver);
            }));
        }
        mBookmarkBridgeSupplier = bookmarkBridgeSupplier;
        mBookmarkBridgeSupplierCallback = (bookmarkBridge) -> mBookmarkBridge = bookmarkBridge;
        mBookmarkBridgeSupplier.addObserver(mBookmarkBridgeSupplierCallback);
        mShareUtils = new ShareUtils();
    }

    @Override
    public void destroy() {
        mBookmarkBridgeSupplier.removeObserver(mBookmarkBridgeSupplierCallback);
        if (mCallbackController != null) {
            mCallbackController.destroy();
            mCallbackController = null;
        }
        if (mStartSurfaceSupplier != null) {
            if (mStartSurfaceSupplier.get() != null) {
                mStartSurfaceSupplier.get().removeStateChangeObserver(mStartSurfaceStateObserver);
            }
            mStartSurfaceSupplier = null;
            mStartSurfaceStateObserver = null;
        }
    }

    /**
     * @return The resource id for the menu to use in {@link AppMenu}.
     */
    protected int getAppMenuLayoutId() {
        return R.menu.main_menu;
    }

    @Override
    public @Nullable List<CustomViewBinder> getCustomViewBinders() {
        List<CustomViewBinder> customViewBinders = new ArrayList<>();
        customViewBinders.add(new UpdateMenuItemViewBinder());
        customViewBinders.add(new ManagedByMenuItemViewBinder());
        customViewBinders.add(new IncognitoMenuItemViewBinder());
        customViewBinders.add(new DividerLineMenuItemViewBinder());
        return customViewBinders;
    }

    /**
     * @return Whether the app menu for a web page should be shown.
     */
    protected boolean shouldShowPageMenu() {
        boolean isInTabSwitcher = isInTabSwitcher();
        if (mIsTablet) {
            boolean hasTabs = mTabModelSelector.getCurrentModel().getCount() != 0;
            return hasTabs && !isInTabSwitcher;
        } else {
            return !isInTabSwitcher;
        }
    }

    @VisibleForTesting
    @MenuGroup
    int getMenuGroup() {
        // Determine which menu to show.
        @MenuGroup
        int menuGroup = MenuGroup.INVALID;
        if (shouldShowPageMenu()) menuGroup = MenuGroup.PAGE_MENU;

        boolean isInTabSwitcher = isInTabSwitcher();
        if (mIsTablet) {
            boolean hasTabs = mTabModelSelector.getCurrentModel().getCount() != 0;
            if (hasTabs && isInTabSwitcher) {
                menuGroup = MenuGroup.OVERVIEW_MODE_MENU;
            } else if (!hasTabs) {
                menuGroup = MenuGroup.TABLET_EMPTY_MODE_MENU;
            }
        } else if (isInTabSwitcher) {
            menuGroup = MenuGroup.OVERVIEW_MODE_MENU;
        }
        assert menuGroup != MenuGroup.INVALID;
        return menuGroup;
    }

    /**
     * @return Whether the grid tab switcher is showing.
     */
    private boolean isInTabSwitcher() {
        return mOverviewModeBehavior != null && mOverviewModeBehavior.overviewVisible()
                && !isInStartSurfaceHomepage();
    }

    /**
     * @return Whether the Start surface homepage is showing.
     */
    private boolean isInStartSurfaceHomepage() {
        return mStartSurfaceSupplier != null && mStartSurfaceSupplier.get() != null
                && mStartSurfaceState == StartSurfaceState.SHOWN_HOMEPAGE;
    }

    private void setMenuGroupVisibility(@MenuGroup int menuGroup, Menu menu) {
        menu.setGroupVisible(R.id.PAGE_MENU, menuGroup == MenuGroup.PAGE_MENU);
        menu.setGroupVisible(R.id.OVERVIEW_MODE_MENU, menuGroup == MenuGroup.OVERVIEW_MODE_MENU);
        menu.setGroupVisible(
                R.id.TABLET_EMPTY_MODE_MENU, menuGroup == MenuGroup.TABLET_EMPTY_MODE_MENU);
    }

    @Override
    public ModelList getMenuItems(
            CustomItemViewTypeProvider customItemViewTypeProvider, AppMenuHandler handler) {
        ModelList modelList = new ModelList();

        PopupMenu popup = new PopupMenu(mContext, mDecorView);
        Menu menu = popup.getMenu();
        MenuInflater inflater = popup.getMenuInflater();
        inflater.inflate(getAppMenuLayoutId(), menu);

        prepareMenu(menu, handler);

        // TODO(crbug.com/1119550): Programmatically create menu item's PropertyModel instead of
        // converting from MenuItems.
        for (int i = 0; i < menu.size(); ++i) {
            MenuItem item = menu.getItem(i);
            if (!item.isVisible()) continue;

            PropertyModel propertyModel = AppMenuUtil.menuItemToPropertyModel(item);
            propertyModel.set(AppMenuItemProperties.ICON_COLOR_RES, getMenuItemIconColorRes(item));
            propertyModel.set(AppMenuItemProperties.SUPPORT_ENTER_ANIMATION, true);
            if (item.hasSubMenu()) {
                // Only support top level menu items have SUBMENU, and a SUBMENU item cannot have a
                // SUBMENU.
                // TODO(crbug.com/1183234) : Create a new SubMenuItemProperties property key set for
                // SUBMENU items.
                ModelList subList = new ModelList();
                for (int j = 0; j < item.getSubMenu().size(); ++j) {
                    MenuItem subitem = item.getSubMenu().getItem(j);
                    if (!subitem.isVisible()) continue;

                    PropertyModel subModel = AppMenuUtil.menuItemToPropertyModel(subitem);
                    subList.add(new MVCListAdapter.ListItem(0, subModel));
                    if (subitem.getItemId() == R.id.reload_menu_id) {
                        mReloadPropertyModel = subModel;
                        Tab currentTab = mActivityTabProvider.get();
                        loadingStateChanged(currentTab == null ? false : currentTab.isLoading());
                    }
                }
                propertyModel.set(AppMenuItemProperties.SUBMENU, subList);
            }
            int menutype = AppMenuItemType.STANDARD;
            if (item.getItemId() == R.id.request_desktop_site_row_menu_id
                    || item.getItemId() == R.id.share_row_menu_id
                    || item.getItemId() == R.id.auto_dark_web_contents_row_menu_id) {
                menutype = AppMenuItemType.TITLE_BUTTON;
            } else if (item.getItemId() == R.id.icon_row_menu_id) {
                int viewCount = item.getSubMenu().size();
                if (viewCount == 3) {
                    menutype = AppMenuItemType.THREE_BUTTON_ROW;
                } else if (viewCount == 4) {
                    menutype = AppMenuItemType.FOUR_BUTTON_ROW;
                } else if (viewCount == 5) {
                    menutype = AppMenuItemType.FIVE_BUTTON_ROW;
                }
            } else {
                // Could be standard items or custom items.
                int customType = customItemViewTypeProvider.fromMenuItemId(item.getItemId());
                if (customType != CustomViewBinder.NOT_HANDLED) {
                    menutype = customType;
                }
            }
            modelList.add(new MVCListAdapter.ListItem(menutype, propertyModel));
        }

        return modelList;
    }

    @Override
    public void prepareMenu(Menu menu, AppMenuHandler handler) {
        int menuGroup = getMenuGroup();
        setMenuGroupVisibility(menuGroup, menu);

        boolean isIncognito = mTabModelSelector.getCurrentModel().isIncognito();
        Tab currentTab = mActivityTabProvider.get();

        if (menuGroup == MenuGroup.PAGE_MENU) {
            preparePageMenu(
                    menu, isInStartSurfaceHomepage() ? null : currentTab, handler, isIncognito);
        }
        prepareCommonMenuItems(menu, menuGroup, isIncognito);
    }

    /**
     * Prepare the menu items. Note: it is possible that currentTab is null.
     */
    private void preparePageMenu(
            Menu menu, @Nullable Tab currentTab, AppMenuHandler handler, boolean isIncognito) {
        // Multiple menu items shouldn't be enabled when the currentTab is null. Use a flag to
        // indicate whether the current Tab isn't null.
        boolean isCurrentTabNotNull = currentTab != null;

        GURL url = isCurrentTabNotNull ? currentTab.getUrl() : GURL.emptyGURL();
        final boolean isChromeScheme = url.getScheme().equals(UrlConstants.CHROME_SCHEME)
                || url.getScheme().equals(UrlConstants.CHROME_NATIVE_SCHEME);
        final boolean isFileScheme = url.getScheme().equals(UrlConstants.FILE_SCHEME);
        final boolean isContentScheme = url.getScheme().equals(UrlConstants.CONTENT_SCHEME);
        final boolean isHttpOrHttpsScheme = UrlUtilities.isHttpOrHttps(url);

        // Update the icon row items (shown in narrow form factors).
        boolean shouldShowIconRow = shouldShowIconRow();
        menu.findItem(R.id.icon_row_menu_id).setVisible(shouldShowIconRow);
        if (shouldShowIconRow) {
            SubMenu actionBar = menu.findItem(R.id.icon_row_menu_id).getSubMenu();

            // Disable the "Forward" menu item if there is no page to go to.
            MenuItem forwardMenuItem = actionBar.findItem(R.id.forward_menu_id);
            forwardMenuItem.setEnabled(isCurrentTabNotNull && currentTab.canGoForward());

            Drawable icon = AppCompatResources.getDrawable(mContext, R.drawable.btn_reload_stop);
            DrawableCompat.setTintList(icon,
                    AppCompatResources.getColorStateList(
                            mContext, R.color.default_icon_color_tint_list));
            actionBar.findItem(R.id.reload_menu_id).setIcon(icon);
            loadingStateChanged(isCurrentTabNotNull && currentTab.isLoading());

            MenuItem bookmarkMenuItem = actionBar.findItem(R.id.bookmark_this_page_id);
            updateBookmarkMenuItem(bookmarkMenuItem, currentTab);

            MenuItem offlineMenuItem = actionBar.findItem(R.id.offline_page_id);
            offlineMenuItem.setEnabled(isCurrentTabNotNull && shouldEnableDownloadPage(currentTab));

            if (!isCurrentTabNotNull) {
                actionBar.findItem(R.id.info_menu_id).setEnabled(false);
                actionBar.findItem(R.id.reload_menu_id).setEnabled(false);
            }
            assert actionBar.size() == 5;
        }

        mUpdateMenuItemVisible = shouldShowUpdateMenuItem();
        menu.findItem(R.id.update_menu_id).setVisible(mUpdateMenuItemVisible);
        if (mUpdateMenuItemVisible) {
            mAppMenuInvalidator = () -> handler.invalidateAppMenu();
            UpdateMenuItemHelper.getInstance().registerObserver(mAppMenuInvalidator);
        }

        menu.findItem(R.id.new_window_menu_id).setVisible(shouldShowNewWindow());
        menu.findItem(R.id.move_to_other_window_menu_id).setVisible(shouldShowMoveToOtherWindow());
        MenuItem menu_all_windows = menu.findItem(R.id.manage_all_windows_menu_id);
        boolean showManageAllWindows = shouldShowManageAllWindows();
        menu_all_windows.setVisible(showManageAllWindows);
        if (showManageAllWindows) {
            menu_all_windows.setTitle(
                    mContext.getString(R.string.menu_manage_all_windows, getInstanceCount()));
        }

        menu.findItem(R.id.add_to_reading_list_menu_id)
                .setVisible(isCurrentTabNotNull && isHttpOrHttpsScheme
                        && ReadingListFeatures.isAddToReadingListAppMenuItemEnabled());
        // TODO(crbug.com/1252228): Show this only on URLs already on the Reading List.
        menu.findItem(R.id.delete_from_reading_list_menu_id)
                .setVisible(isHttpOrHttpsScheme
                        && ReadingListFeatures.isDeleteFromReadingListAppMenuItemEnabled());
        // TODO(crbug.com/1252228): Show this only on URLs already on the Reading List.
        menu.findItem(R.id.edit_reading_list_menu_id)
                .setVisible(isCurrentTabNotNull && isHttpOrHttpsScheme
                        && ReadingListFeatures.isEditReadingListAppMenuItemEnabled());

        // TODO(crbug.com/1257406): Show this only if the current page is not in bookmarks.
        menu.findItem(R.id.add_bookmark_menu_id)
                .setVisible(isCurrentTabNotNull && BookmarkFeatures.isAddBookmarkMenuItemEnabled());
        // TODO(crbug.com/1257406): Show this only if the current page is in bookmarks.
        menu.findItem(R.id.edit_bookmark_menu_id)
                .setVisible(
                        isCurrentTabNotNull && BookmarkFeatures.isEditBookmarkMenuItemEnabled());

        // Don't allow either "chrome://" pages or interstitial pages to be shared, or when the
        // current tab is null.
        menu.findItem(R.id.share_row_menu_id)
                .setVisible(isCurrentTabNotNull && mShareUtils.shouldEnableShare(currentTab));

        if (isCurrentTabNotNull) {
            ShareHelper.configureDirectShareMenuItem(
                    mContext, menu.findItem(R.id.direct_share_menu_id));
        }

        menu.findItem(R.id.paint_preview_show_id)
                .setVisible(isCurrentTabNotNull
                        && shouldShowPaintPreview(isChromeScheme, currentTab, isIncognito));

        // Enable image descriptions if touch exploration is currently enabled.
        if (ImageDescriptionsController.getInstance().shouldShowImageDescriptionsMenuItem()) {
            menu.findItem(R.id.get_image_descriptions_id).setVisible(true);

            int titleId = R.string.menu_stop_image_descriptions;
            Profile profile = Profile.getLastUsedRegularProfile();
            // If image descriptions are not enabled, then we want the menu item to be "Get".
            if (!ImageDescriptionsController.getInstance().imageDescriptionsEnabled(profile)) {
                titleId = R.string.menu_get_image_descriptions;
            } else if (ImageDescriptionsController.getInstance().onlyOnWifiEnabled(profile)
                    && DeviceConditions.getCurrentNetConnectionType(mContext)
                            != ConnectionType.CONNECTION_WIFI) {
                // If image descriptions are enabled, then we want "Stop", except in the special
                // case that the user specified only on Wifi, and we are not currently on Wifi.
                titleId = R.string.menu_get_image_descriptions;
            }

            menu.findItem(R.id.get_image_descriptions_id).setTitle(titleId);
        } else {
            menu.findItem(R.id.get_image_descriptions_id).setVisible(false);
        }

        // Disable find in page on the native NTP or on Start surface.
        menu.findItem(R.id.find_in_page_id)
                .setVisible(isCurrentTabNotNull && shouldShowFindInPage(currentTab));

        // Prepare translate menu button.
        prepareTranslateMenuItem(menu, currentTab);

        prepareAddToHomescreenMenuItem(menu, currentTab,
                shouldShowHomeScreenMenuItem(
                        isChromeScheme, isFileScheme, isContentScheme, isIncognito, url));

        updateRequestDesktopSiteMenuItem(menu, currentTab, true /* can show */, isChromeScheme);

        updateAutoDarkMenuItem(menu, currentTab, isChromeScheme);

        // Only display reader mode settings menu option if the current page is in reader mode.
        menu.findItem(R.id.reader_mode_prefs_id)
                .setVisible(isCurrentTabNotNull && shouldShowReaderModePrefs(currentTab));

        // Only display the Enter VR button if VR Shell Dev environment is enabled.
        menu.findItem(R.id.enter_vr_id).setVisible(isCurrentTabNotNull && shouldShowEnterVr());

        MenuItem managedByMenuItem = menu.findItem(R.id.managed_by_menu_id);
        managedByMenuItem.setVisible(
                isCurrentTabNotNull && shouldShowManagedByMenuItem(currentTab));
        // TODO(https://crbug.com/1092175): Enable "managed by" menu item after chrome://management
        // page is added.
        managedByMenuItem.setEnabled(false);
    }

    /**
     * @return The number of Chrome instances either running alive or dormant but the state
     *         is present for restoration.
     */
    private int getInstanceCount() {
        return mMultiWindowModeStateDispatcher.getInstanceCount();
    }

    private void prepareCommonMenuItems(Menu menu, @MenuGroup int menuGroup, boolean isIncognito) {
        // We have to iterate all menu items since same menu item ID may be associated with more
        // than one menu items.
        boolean isOverviewModeMenu = menuGroup == MenuGroup.OVERVIEW_MODE_MENU;
        boolean isMenuGroupTabsVisible = isOverviewModeMenu
                && TabUiFeatureUtilities.isTabGroupsAndroidEnabled(mContext)
                && !DeviceClassManager.enableAccessibilityLayout(mContext);
        boolean isMenuGroupTabsEnabled = isMenuGroupTabsVisible
                && mTabModelSelector.getTabModelFilterProvider()
                                .getCurrentTabModelFilter()
                                .getTabsWithNoOtherRelatedTabs()
                                .size()
                        > 1;
        boolean isPriceTrackingVisible = isOverviewModeMenu
                && PriceTrackingUtilities.isPriceTrackingEligible()
                && !DeviceClassManager.enableAccessibilityLayout(mContext) && !isIncognito;
        boolean isPriceTrackingEnabled = isPriceTrackingVisible;
        boolean hasItemBetweenDividers = false;

        for (int i = 0; i < menu.size(); ++i) {
            MenuItem item = menu.getItem(i);
            if (!shouldShowIconBeforeItem()) {
                // Remove icons for menu items except the reader mode prefs and the update menu
                // item.
                if (item.getItemId() != R.id.reader_mode_prefs_id
                        && item.getItemId() != R.id.update_menu_id) {
                    item.setIcon(null);
                }

                // Remove title button icons.
                if (item.getItemId() == R.id.request_desktop_site_row_menu_id
                        || item.getItemId() == R.id.share_row_menu_id
                        || item.getItemId() == R.id.auto_dark_web_contents_row_menu_id) {
                    item.getSubMenu().getItem(0).setIcon(null);
                }
            }

            if (item.getItemId() == R.id.new_incognito_tab_menu_id && item.isVisible()) {
                // Disable new incognito tab when it is blocked (e.g. by a policy).
                // findItem(...).setEnabled(...)" is not enough here, because of the inflated
                // main_menu.xml contains multiple items with the same id in different groups
                // e.g.: menu_new_incognito_tab.
                item.setEnabled(isIncognitoEnabled());
            }

            if (item.getItemId() == R.id.divider_line_id) {
                item.setEnabled(false);
            }

            int itemGroupId = item.getGroupId();
            if (!(menuGroup == MenuGroup.OVERVIEW_MODE_MENU
                                && itemGroupId == R.id.OVERVIEW_MODE_MENU
                        || menuGroup == MenuGroup.PAGE_MENU && itemGroupId == R.id.PAGE_MENU)) {
                continue;
            }

            if (item.getItemId() == R.id.recent_tabs_menu_id) {
                item.setVisible(!isIncognito);
            }
            if (item.getItemId() == R.id.menu_group_tabs) {
                item.setVisible(isMenuGroupTabsVisible);
                item.setEnabled(isMenuGroupTabsEnabled);
            }
            if (item.getItemId() == R.id.track_prices_row_menu_id) {
                item.setVisible(isPriceTrackingVisible);
                item.setEnabled(isPriceTrackingEnabled);
            }
            if (item.getItemId() == R.id.close_all_tabs_menu_id) {
                boolean hasTabs = mTabModelSelector.getTotalTabCount() > 0;
                item.setVisible(!isIncognito && isOverviewModeMenu);
                item.setEnabled(hasTabs);
            }
            if (item.getItemId() == R.id.close_all_incognito_tabs_menu_id) {
                boolean hasIncognitoTabs = mTabModelSelector.getModel(true).getCount() > 0;
                item.setVisible(isIncognito && isOverviewModeMenu);
                item.setEnabled(hasIncognitoTabs);
            }
            // This needs to be done after the visibility of the item is set.
            if (item.getItemId() == R.id.divider_line_id) {
                if (!hasItemBetweenDividers) {
                    // If there isn't any visible menu items between the two divider lines, mark
                    // this line invisible.
                    item.setVisible(false);
                } else {
                    hasItemBetweenDividers = false;
                }
            } else if (!hasItemBetweenDividers && item.isVisible()) {
                // When the item isn't a divider line and is visible, we set hasItemBetweenDividers
                // to be true.
                hasItemBetweenDividers = true;
            }
        }
    }

    /**
     * @param currentTab The currentTab for which the app menu is showing.
     * @return Whether the reader mode preferences menu item should be displayed.
     */
    @VisibleForTesting(otherwise = VisibleForTesting.PROTECTED)
    public boolean shouldShowReaderModePrefs(@NonNull Tab currentTab) {
        return DomDistillerUrlUtils.isDistilledPage(currentTab.getUrl());
    }

    /**
     * @param currentTab The currentTab for which the app menu is showing.
     * @return Whether the {@code currentTab} may be downloaded, indicating whether the download
     *         page menu item should be enabled.
     */
    @VisibleForTesting(otherwise = VisibleForTesting.PROTECTED)
    public boolean shouldEnableDownloadPage(@NonNull Tab currentTab) {
        return DownloadUtils.isAllowedToDownloadPage(currentTab);
    }

    /**
     * @param currentTab The currentTab for which the app menu is showing.
     * @return Whether bookmark page menu item should be checked, indicating that the current tab
     *         is bookmarked.
     */
    @VisibleForTesting(otherwise = VisibleForTesting.PROTECTED)
    public boolean shouldCheckBookmarkStar(@NonNull Tab currentTab) {
        return sItemBookmarkedForTesting != null
                ? sItemBookmarkedForTesting
                : mBookmarkBridge != null && mBookmarkBridge.hasBookmarkIdForTab(currentTab);
    }

    /**
     * @return Whether the update Chrome menu item should be displayed.
     */
    protected boolean shouldShowUpdateMenuItem() {
        return UpdateMenuItemHelper.getInstance().getUiState().itemState != null;
    }

    /**
     * @return Whether the "Move to other window" menu item should be displayed.
     */
    protected boolean shouldShowMoveToOtherWindow() {
        if (!instanceSwitcherEnabled() && shouldShowNewWindow()) return false;
        boolean hasMoreThanOneTab = mTabModelSelector.getTotalTabCount() > 1;
        boolean showAlsoForSingleTab = !isPartnerHomepageEnabled();
        if (!hasMoreThanOneTab && !showAlsoForSingleTab) return false;
        if (instanceSwitcherEnabled()) {
            // Moving tabs should be possible to any other instance.
            return getInstanceCount() > 1;
        } else {
            return mMultiWindowModeStateDispatcher.isOpenInOtherWindowSupported();
        }
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    public boolean instanceSwitcherEnabled() {
        return MultiWindowUtils.instanceSwitcherEnabled()
                && MultiWindowUtils.isMultiInstanceApi31Enabled();
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    public boolean isTabletSizeScreen() {
        return mIsTablet;
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    public boolean isPartnerHomepageEnabled() {
        return PartnerBrowserCustomizations.getInstance().isHomepageProviderAvailableAndEnabled();
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    public boolean isNewWindowMenuFeatureEnabled() {
        return CachedFeatureFlags.isEnabled(ChromeFeatureList.NEW_WINDOW_APP_MENU);
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    public boolean isAutoDarkWebContentsEnabled() {
        boolean isFlagEnabled = ChromeFeatureList.isEnabled(
                ChromeFeatureList.DARKEN_WEBSITES_CHECKBOX_IN_THEMES_SETTING);

        return isFlagEnabled
                && UserPrefs.get(mTabModelSelector.getCurrentModel().getProfile())
                           .getBoolean(Pref.WEB_KIT_FORCE_DARK_MODE_ENABLED);
    }

    /**
     * @return Whether the "New window" menu item should be displayed.
     */
    protected boolean shouldShowNewWindow() {
        if (!isNewWindowMenuFeatureEnabled()) return false;
        if (instanceSwitcherEnabled()) {
            // Hide the menu if we already have the maximum number of windows.
            if (getInstanceCount() >= MultiWindowUtils.getMaxInstances()) return false;

            // On phones, show the menu only when in split-screen, with a single instance
            // running on the foreground.
            return isTabletSizeScreen()
                    || (!mMultiWindowModeStateDispatcher.isChromeRunningInAdjacentWindow()
                            && (mMultiWindowModeStateDispatcher.isInMultiWindowMode()
                                    || mMultiWindowModeStateDispatcher.isInMultiDisplayMode()));
        } else {
            if (mMultiWindowModeStateDispatcher.isMultiInstanceRunning()) return false;
            return (mMultiWindowModeStateDispatcher.canEnterMultiWindowMode()
                           && isTabletSizeScreen())
                    || mMultiWindowModeStateDispatcher.isInMultiWindowMode()
                    || mMultiWindowModeStateDispatcher.isInMultiDisplayMode();
        }
    }

    private boolean shouldShowManageAllWindows() {
        return MultiWindowUtils.shouldShowManageWindowsMenu();
    }

    /**
     * @param isChromeScheme Whether URL for the current tab starts with the chrome:// scheme.
     * @param currentTab The currentTab for which the app menu is showing.
     * @param isIncognito Whether the currentTab is incognito.
     * @return Whether the paint preview menu item should be displayed.
     */
    @VisibleForTesting(otherwise = VisibleForTesting.PROTECTED)
    public boolean shouldShowPaintPreview(
            boolean isChromeScheme, @NonNull Tab currentTab, boolean isIncognito) {
        return CachedFeatureFlags.isEnabled(ChromeFeatureList.PAINT_PREVIEW_DEMO) && !isChromeScheme
                && !isIncognito;
    }

    /**
     * @param currentTab The currentTab for which the app menu is showing.
     * @return Whether the find in page menu item should be displayed.
     */
    protected boolean shouldShowFindInPage(@NonNull Tab currentTab) {
        return !currentTab.isNativePage() && currentTab.getWebContents() != null;
    }

    /**
     * @return Whether the enter VR menu item should be displayed.
     */
    protected boolean shouldShowEnterVr() {
        return CommandLine.getInstance().hasSwitch(ChromeSwitches.ENABLE_VR_SHELL_DEV);
    }

    /**
     * This method should only be called once per context menu shown.
     * @param currentTab The currentTab for which the app menu is showing.
     * @param logging Whether logging should be performed in this check.
     * @return Whether the translate menu item should be displayed.
     */
    @VisibleForTesting(otherwise = VisibleForTesting.PROTECTED)
    public boolean shouldShowTranslateMenuItem(@NonNull Tab currentTab) {
        return TranslateUtils.canTranslateCurrentTab(currentTab, true);
    }

    /**
     * @param isChromeScheme Whether URL for the current tab starts with the chrome:// scheme.
     * @param isFileScheme Whether URL for the current tab starts with the file:// scheme.
     * @param isContentScheme Whether URL for the current tab starts with the file:// scheme.
     * @param isIncognito Whether the current tab is incognito.
     * @param url The URL for the current tab.
     * @return Whether the homescreen menu item should be displayed.
     */
    protected boolean shouldShowHomeScreenMenuItem(boolean isChromeScheme, boolean isFileScheme,
            boolean isContentScheme, boolean isIncognito, @NonNull GURL url) {
        // Hide 'Add to homescreen' for the following:
        // * chrome:// pages - Android doesn't know how to direct those URLs.
        // * incognito pages - To avoid problems where users create shortcuts in incognito
        //                      mode and then open the webapp in regular mode.
        // * file:// - After API 24, file: URIs are not supported in VIEW intents and thus
        //             can not be added to the homescreen.
        // * content:// - Accessing external content URIs requires the calling app to grant
        //                access to the resource via FLAG_GRANT_READ_URI_PERMISSION, and that
        //                is not persisted when adding to the homescreen.
        // * If creating shortcuts it not supported by the current home screen.
        return WebappsUtils.isAddToHomeIntentSupported() && !isChromeScheme && !isFileScheme
                && !isContentScheme && !isIncognito && !url.isEmpty();
    }

    /**
     * @param currentTab Current tab being displayed.
     * @return Whether the "Managed by your organization" menu item should be displayed.
     */
    protected boolean shouldShowManagedByMenuItem(Tab currentTab) {
        return false;
    }

    /**
     * Sets the visibility and labels of the "Add to Home screen" and "Open WebAPK" menu items.
     */
    protected void prepareAddToHomescreenMenuItem(
            Menu menu, Tab currentTab, boolean shouldShowHomeScreenMenuItem) {
        MenuItem homescreenItem = menu.findItem(R.id.add_to_homescreen_id);
        MenuItem openWebApkItem = menu.findItem(R.id.open_webapk_id);
        mAddAppTitleShown = AppMenuVerbiage.APP_MENU_OPTION_UNKNOWN;
        if (currentTab != null && shouldShowHomeScreenMenuItem) {
            Context context = ContextUtils.getApplicationContext();
            long addToHomeScreenStart = SystemClock.elapsedRealtime();
            ResolveInfo resolveInfo = WebApkValidator.queryFirstWebApkResolveInfo(
                    context, currentTab.getUrl().getSpec());
            RecordHistogram.recordTimesHistogram("Android.PrepareMenu.OpenWebApkVisibilityCheck",
                    SystemClock.elapsedRealtime() - addToHomeScreenStart);

            boolean openWebApkItemVisible =
                    resolveInfo != null && resolveInfo.activityInfo.packageName != null;

            if (openWebApkItemVisible) {
                String appName = resolveInfo.loadLabel(context.getPackageManager()).toString();
                openWebApkItem.setTitle(context.getString(R.string.menu_open_webapk, appName));

                homescreenItem.setVisible(false);
                openWebApkItem.setVisible(true);
            } else {
                AppBannerManager.InstallStringPair installStrings =
                        getAddToHomeScreenTitle(currentTab);
                homescreenItem.setTitle(installStrings.titleTextId);
                homescreenItem.setVisible(true);
                openWebApkItem.setVisible(false);

                if (installStrings.titleTextId == AppBannerManager.NON_PWA_PAIR.titleTextId) {
                    mAddAppTitleShown = AppMenuVerbiage.APP_MENU_OPTION_ADD_TO_HOMESCREEN;
                } else if (installStrings.titleTextId == AppBannerManager.PWA_PAIR.titleTextId) {
                    mAddAppTitleShown = AppMenuVerbiage.APP_MENU_OPTION_INSTALL;
                }
            }
        } else {
            homescreenItem.setVisible(false);
            openWebApkItem.setVisible(false);
        }
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PROTECTED)
    public AppBannerManager.InstallStringPair getAddToHomeScreenTitle(@NonNull Tab currentTab) {
        return AppBannerManager.getHomescreenLanguageOption(currentTab.getWebContents());
    }

    @Override
    public Bundle getBundleForMenuItem(int itemId) {
        Bundle bundle = new Bundle();
        if (itemId == R.id.add_to_homescreen_id) {
            bundle.putInt(AppBannerManager.MENU_TITLE_KEY, mAddAppTitleShown);
        }
        return bundle;
    }

    /**
     * Sets the visibility of the "Translate" menu item.
     */
    protected void prepareTranslateMenuItem(Menu menu, @Nullable Tab currentTab) {
        boolean isTranslateVisible = currentTab != null && shouldShowTranslateMenuItem(currentTab);
        menu.findItem(R.id.translate_id).setVisible(isTranslateVisible);
    }

    @Override
    public void loadingStateChanged(boolean isLoading) {
        if (mReloadPropertyModel != null) {
            Resources resources = mContext.getResources();
            mReloadPropertyModel.get(AppMenuItemProperties.ICON)
                    .setLevel(isLoading
                                    ? resources.getInteger(R.integer.reload_button_level_stop)
                                    : resources.getInteger(R.integer.reload_button_level_reload));
            mReloadPropertyModel.set(AppMenuItemProperties.TITLE,
                    resources.getString(isLoading ? R.string.accessibility_btn_stop_loading
                                                  : R.string.accessibility_btn_refresh));
            mReloadPropertyModel.set(AppMenuItemProperties.TITLE_CONDENSED,
                    resources.getString(
                            isLoading ? R.string.menu_stop_refresh : R.string.menu_refresh));
        }
    }

    @Override
    public void onMenuDismissed() {
        mReloadPropertyModel = null;
        if (mUpdateMenuItemVisible) {
            UpdateMenuItemHelper.getInstance().onMenuDismissed();
            UpdateMenuItemHelper.getInstance().unregisterObserver(mAppMenuInvalidator);
            mUpdateMenuItemVisible = false;
            mAppMenuInvalidator = null;
        }
    }

    @VisibleForTesting
    boolean shouldShowIconRow() {
        boolean shouldShowIconRow = !mIsTablet
                || mDecorView.getWidth()
                        < DeviceFormFactor.getNonMultiDisplayMinimumTabletWidthPx(mContext);

        final boolean isMenuButtonOnTop = mToolbarManager != null;
        shouldShowIconRow &= isMenuButtonOnTop;
        return shouldShowIconRow;
    }

    @Override
    public int getFooterResourceId() {
        return 0;
    }

    @Override
    public int getHeaderResourceId() {
        return 0;
    }

    @Override
    public int getGroupDividerId() {
        return R.id.divider_line_id;
    }

    @Override
    public boolean shouldShowFooter(int maxMenuHeight) {
        return true;
    }

    @Override
    public boolean shouldShowHeader(int maxMenuHeight) {
        return true;
    }

    @Override
    public void onFooterViewInflated(AppMenuHandler appMenuHandler, View view) {}

    @Override
    public void onHeaderViewInflated(AppMenuHandler appMenuHandler, View view) {}

    @Override
    public boolean shouldShowIconBeforeItem() {
        return false;
    }

    @Override
    public void recordHighlightedMenuItemShown(@Nullable @IdRes Integer menuItemId) {
        RecordHistogram.recordEnumeratedHistogram("Mobile.AppMenu.HighlightMenuItem.Shown",
                getUmaEnumForMenuItem(menuItemId), AppMenuHighlightItem.NUM_ENTRIES);
    }

    @Override
    public void recordHighlightedMenuItemClicked(@Nullable @IdRes Integer menuItemId) {
        RecordHistogram.recordEnumeratedHistogram("Mobile.AppMenu.HighlightMenuItem.Clicked",
                getUmaEnumForMenuItem(menuItemId), AppMenuHighlightItem.NUM_ENTRIES);
    }

    private int getUmaEnumForMenuItem(@Nullable @IdRes Integer menuItemId) {
        if (menuItemId == null) return AppMenuHighlightItem.UNKNOWN;

        if (menuItemId == R.id.downloads_menu_id) {
            return AppMenuHighlightItem.DOWNLOADS;
        } else if (menuItemId == R.id.all_bookmarks_menu_id) {
            return AppMenuHighlightItem.BOOKMARKS;
        } else if (menuItemId == R.id.translate_id) {
            return AppMenuHighlightItem.TRANSLATE;
        } else if (menuItemId == R.id.add_to_homescreen_id) {
            return AppMenuHighlightItem.ADD_TO_HOMESCREEN;
        } else if (menuItemId == R.id.offline_page_id) {
            return AppMenuHighlightItem.DOWNLOAD_THIS_PAGE;
        } else if (menuItemId == R.id.bookmark_this_page_id) {
            return AppMenuHighlightItem.BOOKMARK_THIS_PAGE;
        } else if (menuItemId == R.id.app_menu_footer) {
            return AppMenuHighlightItem.DATA_REDUCTION_FOOTER;
        }
        return AppMenuHighlightItem.UNKNOWN;
    }

    /**
     * Updates the bookmark item's visibility.
     *
     * @param bookmarkMenuItem {@link MenuItem} for adding/editing the bookmark.
     * @param currentTab        Current tab being displayed.
     */
    protected void updateBookmarkMenuItem(MenuItem bookmarkMenuItem, @Nullable Tab currentTab) {
        // If this method is called before the {@link #mBookmarkBridgeSupplierCallback} has been
        // called, try to retrieve the bridge directly from the supplier.
        if (mBookmarkBridge == null && mBookmarkBridgeSupplier != null) {
            mBookmarkBridge = mBookmarkBridgeSupplier.get();
        }

        if (mBookmarkBridge == null || currentTab == null) {
            // If the BookmarkBridge still isn't available, assume the bookmark menu item is not
            // editable.
            bookmarkMenuItem.setEnabled(false);
        } else {
            bookmarkMenuItem.setEnabled(mBookmarkBridge.isEditBookmarksEnabled());
        }

        if (currentTab != null && shouldCheckBookmarkStar(currentTab)) {
            bookmarkMenuItem.setIcon(R.drawable.btn_star_filled);
            bookmarkMenuItem.setChecked(true);
            bookmarkMenuItem.setTitleCondensed(mContext.getString(R.string.edit_bookmark));
        } else {
            bookmarkMenuItem.setIcon(R.drawable.btn_star);
            bookmarkMenuItem.setChecked(false);
            bookmarkMenuItem.setTitleCondensed(mContext.getString(R.string.menu_bookmark));
        }
    }

    /**
     * Updates the request desktop site item's state.
     *
     * @param menu {@link Menu} for request desktop site.
     * @param currentTab Current tab being displayed.
     * @param canShowRequestDesktopSite If the request desktop site menu item should show or not.
     * @param isChromeScheme Whether URL for the current tab starts with the chrome:// scheme.
     */
    protected void updateRequestDesktopSiteMenuItem(Menu menu, @Nullable Tab currentTab,
            boolean canShowRequestDesktopSite, boolean isChromeScheme) {
        MenuItem requestMenuRow = menu.findItem(R.id.request_desktop_site_row_menu_id);
        MenuItem requestMenuLabel = menu.findItem(R.id.request_desktop_site_id);
        MenuItem requestMenuCheck = menu.findItem(R.id.request_desktop_site_check_id);

        // Hide request desktop site on all chrome:// pages except for the NTP.
        boolean itemVisible = currentTab != null && canShowRequestDesktopSite
                && (!isChromeScheme || currentTab.isNativePage())
                && !shouldShowReaderModePrefs(currentTab) && currentTab.getWebContents() != null;
        requestMenuRow.setVisible(itemVisible);
        if (!itemVisible) return;

        boolean isRequestDesktopSite =
                currentTab.getWebContents().getNavigationController().getUseDesktopUserAgent();
        if (CachedFeatureFlags.isEnabled(ChromeFeatureList.APP_MENU_MOBILE_SITE_OPTION)) {
            requestMenuLabel.setTitle(isRequestDesktopSite
                            ? R.string.menu_item_request_mobile_site
                            : R.string.menu_item_request_desktop_site);
            requestMenuLabel.setIcon(isRequestDesktopSite ? R.drawable.smartphone_black_24dp
                                                          : R.drawable.ic_desktop_windows);
            requestMenuCheck.setVisible(false);
        } else {
            requestMenuLabel.setTitle(R.string.menu_request_desktop_site);
            requestMenuCheck.setVisible(true);
            // Mark the checkbox if RDS is activated on this page.
            requestMenuCheck.setChecked(isRequestDesktopSite);

            // This title doesn't seem to be displayed by Android, but it is used to set up
            // accessibility text in {@link AppMenuAdapter#setupMenuButton}.
            requestMenuLabel.setTitleCondensed(isRequestDesktopSite
                            ? mContext.getString(R.string.menu_request_desktop_site_on)
                            : mContext.getString(R.string.menu_request_desktop_site_off));
        }
    }

    /**
     * Updates the auto dark menu item's state.
     *
     * @param menu {@link Menu} for auto dark.
     * @param currentTab Current tab being displayed.
     * @param isChromeScheme Whether URL for the current tab starts with the chrome:// scheme.
     */
    protected void updateAutoDarkMenuItem(
            Menu menu, @Nullable Tab currentTab, boolean isChromeScheme) {
        MenuItem autoDarkMenuRow = menu.findItem(R.id.auto_dark_web_contents_row_menu_id);
        MenuItem autoDarkMenuCheck = menu.findItem(R.id.auto_dark_web_contents_check_id);

        // Hide app menu item if on non-NTP chrome:// page or auto dark not enabled.
        boolean isAutoDarkEnabled = isAutoDarkWebContentsEnabled();
        boolean itemVisible = currentTab != null && !isChromeScheme && isAutoDarkEnabled;
        autoDarkMenuRow.setVisible(itemVisible);
        if (!itemVisible) return;

        // Set text based on if site is blocked or not.
        boolean isEnabled = WebContentsDarkModeController.isEnabledForUrl(
                mTabModelSelector.getCurrentModel().getProfile(), currentTab.getUrl());
        autoDarkMenuCheck.setChecked(isEnabled);
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
    public boolean isIncognitoEnabled() {
        return IncognitoUtils.isIncognitoModeEnabled();
    }

    @VisibleForTesting
    static void setPageBookmarkedForTesting(Boolean bookmarked) {
        sItemBookmarkedForTesting = bookmarked;
    }

    /**
     * @return Whether the menu item's icon need to be tinted to blue.
     */
    protected @ColorRes int getMenuItemIconColorRes(MenuItem menuItem) {
        int itemId = menuItem.getItemId();
        if (itemId == R.id.edit_bookmark_menu_id || itemId == R.id.add_bookmark_menu_id) {
            return R.color.default_icon_color_blue;
        }

        return R.color.default_icon_color_secondary_tint_list;
    }
}
