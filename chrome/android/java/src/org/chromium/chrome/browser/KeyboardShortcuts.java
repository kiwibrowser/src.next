// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser;

import android.content.Context;
import android.os.Build;
import android.view.KeyEvent;
import android.view.KeyboardShortcutGroup;
import android.view.KeyboardShortcutInfo;

import androidx.annotation.RequiresApi;

import org.chromium.chrome.R;
import org.chromium.chrome.browser.fullscreen.FullscreenManager;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tabmodel.TabModel;
import org.chromium.chrome.browser.tabmodel.TabModelSelector;
import org.chromium.chrome.browser.tabmodel.TabModelUtils;
import org.chromium.chrome.browser.toolbar.ToolbarManager;
import org.chromium.components.browser_ui.widget.MenuOrKeyboardActionController;
import org.chromium.content_public.browser.WebContents;
import org.chromium.device.gamepad.GamepadList;

import java.util.ArrayList;
import java.util.List;

/**
 * Implements app-level keyboard shortcuts for ChromeTabbedActivity and DocumentActivity.
 */
public class KeyboardShortcuts {

    private static final int CTRL = 1 << 31;
    private static final int ALT = 1 << 30;
    private static final int SHIFT = 1 << 29;

    private KeyboardShortcuts() {}

    private static int getMetaState(KeyEvent event) {
        return (event.isCtrlPressed() ? CTRL : 0)
                | (event.isAltPressed() ? ALT : 0)
                | (event.isShiftPressed() ? SHIFT : 0);
    }

    /**
     * This should be called from the Activity's dispatchKeyEvent() to handle keyboard shortcuts.
     *
     * Note: dispatchKeyEvent() is called before the active view or web page gets a chance to handle
     * the key event. So the keys handled here cannot be overridden by any view or web page.
     *
     * @param event The KeyEvent to handle.
     * @param uiInitialized Whether the UI has been initialized. If this is false, most keys will
     *                      not be handled.
     * @param fullscreenManager Manages fullscreen state.
     * @param menuOrKeyboardActionController Controls keyboard actions.
     * @return True if the event was handled. False if the event was ignored. Null if the event
     *         should be handled by the activity's parent class.
     */
    public static Boolean dispatchKeyEvent(KeyEvent event, boolean uiInitialized,
            FullscreenManager fullscreenManager,
            MenuOrKeyboardActionController menuOrKeyboardActionController) {
        int keyCode = event.getKeyCode();
        if (!uiInitialized) {
            if (keyCode == KeyEvent.KEYCODE_SEARCH || keyCode == KeyEvent.KEYCODE_MENU) return true;
            return null;
        }

        switch (keyCode) {
            case KeyEvent.KEYCODE_SEARCH:
                if (event.getAction() == KeyEvent.ACTION_DOWN && event.getRepeatCount() == 0) {
                    menuOrKeyboardActionController.onMenuOrKeyboardAction(
                            R.id.focus_url_bar, false);
                }
                // Always consume the SEARCH key events to prevent android from showing
                // the default app search UI, which locks up Chrome.
                return true;
            case KeyEvent.KEYCODE_MENU:
                if (event.getAction() == KeyEvent.ACTION_DOWN && event.getRepeatCount() == 0) {
                    menuOrKeyboardActionController.onMenuOrKeyboardAction(R.id.show_menu, false);
                }
                return true;
            case KeyEvent.KEYCODE_ESCAPE:
                if (event.getAction() == KeyEvent.ACTION_DOWN && event.getRepeatCount() == 0) {
                    if (fullscreenManager.getPersistentFullscreenMode()) {
                        fullscreenManager.exitPersistentFullscreenMode();
                        return true;
                    }
                }
                break;
            case KeyEvent.KEYCODE_TV:
            case KeyEvent.KEYCODE_GUIDE:
            case KeyEvent.KEYCODE_DVR:
            case KeyEvent.KEYCODE_AVR_INPUT:
            case KeyEvent.KEYCODE_AVR_POWER:
            case KeyEvent.KEYCODE_STB_INPUT:
            case KeyEvent.KEYCODE_STB_POWER:
            case KeyEvent.KEYCODE_TV_INPUT:
            case KeyEvent.KEYCODE_TV_POWER:
            case KeyEvent.KEYCODE_WINDOW:
                // Do not consume the AV device-related keys so that the system will take
                // an appropriate action, such as switching to TV mode.
                return false;
        }

        return null;
    }

    /**
     * This method should be called when overriding from
     * {@link android.app.Activity#onProvideKeyboardShortcuts(List, android.view.Menu, int)}
     * in an activity. It will return a list of the possible shortcuts. If
     * someone adds a shortcut they also need to add an explanation in the
     * appropriate group in this method so the user can see it when this method
     * is called.
     *
     * Preventing inlining since this uses APIs only available on Android N, and this causes dex
     * validation failures on earlier versions if inlined.
     *
     * @param context We need an activity so we can call the strings from our
     *            resource.
     * @return a list of shortcuts organized into groups.
     */
    @RequiresApi(Build.VERSION_CODES.N)
    public static List<KeyboardShortcutGroup> createShortcutGroup(Context context) {
        final int ctrlShift = KeyEvent.META_CTRL_ON | KeyEvent.META_SHIFT_ON;

        List<KeyboardShortcutGroup> shortcutGroups = new ArrayList<>();

        KeyboardShortcutGroup tabShortcutGroup = new KeyboardShortcutGroup(
                context.getString(R.string.keyboard_shortcut_tab_group_header));
        addShortcut(context, tabShortcutGroup, R.string.keyboard_shortcut_open_new_tab,
                KeyEvent.KEYCODE_N, KeyEvent.META_CTRL_ON);
        addShortcut(context, tabShortcutGroup, R.string.keyboard_shortcut_reopen_new_tab,
                KeyEvent.KEYCODE_T, ctrlShift);
        addShortcut(context, tabShortcutGroup, R.string.keyboard_shortcut_new_incognito_tab,
                KeyEvent.KEYCODE_N, ctrlShift);
        addShortcut(context, tabShortcutGroup, R.string.keyboard_shortcut_next_tab,
                KeyEvent.KEYCODE_TAB, KeyEvent.META_CTRL_ON);
        addShortcut(context, tabShortcutGroup, R.string.keyboard_shortcut_prev_tab,
                KeyEvent.KEYCODE_TAB, ctrlShift);
        addShortcut(context, tabShortcutGroup, R.string.keyboard_shortcut_close_tab,
                KeyEvent.KEYCODE_W, KeyEvent.META_CTRL_ON);
        shortcutGroups.add(tabShortcutGroup);

        KeyboardShortcutGroup chromeFeatureShortcutGroup = new KeyboardShortcutGroup(
                context.getString(R.string.keyboard_shortcut_chrome_feature_group_header));
        addShortcut(context, chromeFeatureShortcutGroup, R.string.keyboard_shortcut_open_menu,
                KeyEvent.KEYCODE_E, KeyEvent.META_ALT_ON);
        addShortcut(context, chromeFeatureShortcutGroup,
                R.string.keyboard_shortcut_bookmark_manager, KeyEvent.KEYCODE_B, ctrlShift);
        addShortcut(context, chromeFeatureShortcutGroup, R.string.keyboard_shortcut_history_manager,
                KeyEvent.KEYCODE_H, KeyEvent.META_CTRL_ON);
        addShortcut(context, chromeFeatureShortcutGroup, R.string.keyboard_shortcut_find_bar,
                KeyEvent.KEYCODE_F, KeyEvent.META_CTRL_ON);
        addShortcut(context, chromeFeatureShortcutGroup, R.string.keyboard_shortcut_address_bar,
                KeyEvent.KEYCODE_L, KeyEvent.META_CTRL_ON);
        shortcutGroups.add(chromeFeatureShortcutGroup);

        KeyboardShortcutGroup webpageShortcutGroup = new KeyboardShortcutGroup(
                context.getString(R.string.keyboard_shortcut_webpage_group_header));
        addShortcut(context, webpageShortcutGroup, R.string.keyboard_shortcut_print_page,
                KeyEvent.KEYCODE_P, KeyEvent.META_CTRL_ON);
        addShortcut(context, webpageShortcutGroup, R.string.keyboard_shortcut_reload_page,
                KeyEvent.KEYCODE_R, KeyEvent.META_CTRL_ON);
        addShortcut(context, webpageShortcutGroup, R.string.keyboard_shortcut_reload_no_cache,
                KeyEvent.KEYCODE_R, ctrlShift);
        addShortcut(context, webpageShortcutGroup, R.string.keyboard_shortcut_bookmark_page,
                KeyEvent.KEYCODE_D, KeyEvent.META_CTRL_ON);
        addShortcut(context, webpageShortcutGroup, R.string.keyboard_shortcut_zoom_in,
                KeyEvent.KEYCODE_EQUALS, KeyEvent.META_CTRL_ON);
        addShortcut(context, webpageShortcutGroup, R.string.keyboard_shortcut_zoom_out,
                KeyEvent.KEYCODE_MINUS, KeyEvent.META_CTRL_ON);
        addShortcut(context, webpageShortcutGroup, R.string.keyboard_shortcut_reset_zoom,
                KeyEvent.KEYCODE_0, KeyEvent.META_CTRL_ON);
        addShortcut(context, webpageShortcutGroup, R.string.keyboard_shortcut_help_center,
                KeyEvent.KEYCODE_SLASH, ctrlShift);
        shortcutGroups.add(webpageShortcutGroup);

        return shortcutGroups;
    }

    @RequiresApi(Build.VERSION_CODES.N)
    private static void addShortcut(Context context, KeyboardShortcutGroup shortcutGroup, int resId,
            int keyCode, int keyModifier) {
        shortcutGroup.addItem(new KeyboardShortcutInfo(context.getString(resId), keyCode,
                keyModifier));
    }

    /**
     * This should be called from the Activity's onKeyDown() to handle keyboard shortcuts.
     *
     * Note: onKeyDown() is called after the active view or web page has had a chance to handle
     * the key event. So the keys handled here *can* be overridden by any view or web page.
     *
     * @param event The KeyEvent to handle.
     * @param isCurrentTabVisible Whether page-related actions are valid, e.g. reload, zoom in. This
     *         should be false when in the tab switcher.
     * @param tabSwitchingEnabled Whether shortcuts that switch between tabs are enabled (e.g.
     *         Ctrl+Tab, Ctrl+3).
     * @param tabModelSelector The current tab modelSelector.
     * @param menuOrKeyboardActionController Controls keyboard actions.
     * @param toolbarManager Manages the toolbar.
     * @return Whether the key event was handled.
     */
    public static boolean onKeyDown(KeyEvent event, boolean isCurrentTabVisible,
            boolean tabSwitchingEnabled, TabModelSelector tabModelSelector,
            MenuOrKeyboardActionController menuOrKeyboardActionController,
            ToolbarManager toolbarManager) {
        int keyCode = event.getKeyCode();
        if (event.getRepeatCount() != 0 || KeyEvent.isModifierKey(keyCode)) return false;
        if (KeyEvent.isGamepadButton(keyCode)) {
            if (GamepadList.isGamepadAPIActive()) return false;
        } else if (!event.isCtrlPressed() && !event.isAltPressed()
                && keyCode != KeyEvent.KEYCODE_F3
                && keyCode != KeyEvent.KEYCODE_F5
                && keyCode != KeyEvent.KEYCODE_F10
                && keyCode != KeyEvent.KEYCODE_FORWARD) {
            return false;
        }

        TabModel currentTabModel = tabModelSelector.getCurrentModel();
        Tab currentTab = tabModelSelector.getCurrentTab();
        WebContents currentWebContents = currentTab == null ? null : currentTab.getWebContents();

        int tabCount = currentTabModel.getCount();
        int metaState = getMetaState(event);
        int keyCodeAndMeta = keyCode | metaState;

        switch (keyCodeAndMeta) {
            case CTRL | SHIFT | KeyEvent.KEYCODE_T:
                menuOrKeyboardActionController.onMenuOrKeyboardAction(
                        R.id.open_recently_closed_tab, false);
                return true;
            case CTRL | KeyEvent.KEYCODE_T:
                menuOrKeyboardActionController.onMenuOrKeyboardAction(currentTabModel.isIncognito()
                                ? R.id.new_incognito_tab_menu_id
                                : R.id.new_tab_menu_id,
                        false);
                return true;
            case CTRL | KeyEvent.KEYCODE_N:
                menuOrKeyboardActionController.onMenuOrKeyboardAction(R.id.new_tab_menu_id, false);
                return true;
            case CTRL | SHIFT | KeyEvent.KEYCODE_N:
                menuOrKeyboardActionController.onMenuOrKeyboardAction(
                        R.id.new_incognito_tab_menu_id, false);
                return true;
            // Alt+E represents a special character ´ (latin code: &#180) in Android.
            // If an EditText or ContentView has focus, Alt+E will be swallowed by
            // the default dispatchKeyEvent and cannot open the menu.
            case ALT | KeyEvent.KEYCODE_E:
            case ALT | KeyEvent.KEYCODE_F:
            case KeyEvent.KEYCODE_F10:
            case KeyEvent.KEYCODE_BUTTON_Y:
                menuOrKeyboardActionController.onMenuOrKeyboardAction(R.id.show_menu, false);
                return true;
        }

        if (isCurrentTabVisible) {
            if (tabSwitchingEnabled && (metaState == CTRL || metaState == ALT)) {
                int numCode = keyCode - KeyEvent.KEYCODE_0;
                if (numCode > 0 && numCode <= Math.min(tabCount, 8)) {
                    // Ctrl+1 to Ctrl+8: select tab by index
                    TabModelUtils.setIndex(currentTabModel, numCode - 1, false);
                    return true;
                } else if (numCode == 9 && tabCount != 0) {
                    // Ctrl+9: select last tab
                    TabModelUtils.setIndex(currentTabModel, tabCount - 1, false);
                    return true;
                }
            }

            switch (keyCodeAndMeta) {
                case CTRL | KeyEvent.KEYCODE_TAB:
                case CTRL | KeyEvent.KEYCODE_PAGE_DOWN:
                case KeyEvent.KEYCODE_BUTTON_R1:
                    if (tabSwitchingEnabled && tabCount > 1) {
                        TabModelUtils.setIndex(
                                currentTabModel, (currentTabModel.index() + 1) % tabCount, false);
                    }
                    return true;
                case CTRL | SHIFT | KeyEvent.KEYCODE_TAB:
                case CTRL | KeyEvent.KEYCODE_PAGE_UP:
                case KeyEvent.KEYCODE_BUTTON_L1:
                    if (tabSwitchingEnabled && tabCount > 1) {
                        TabModelUtils.setIndex(currentTabModel,
                                (currentTabModel.index() + tabCount - 1) % tabCount, false);
                    }
                    return true;
                case CTRL | KeyEvent.KEYCODE_W:
                case CTRL | KeyEvent.KEYCODE_F4:
                case KeyEvent.KEYCODE_BUTTON_B:
                    TabModelUtils.closeCurrentTab(currentTabModel);
                    return true;
                case CTRL | KeyEvent.KEYCODE_F:
                case CTRL | KeyEvent.KEYCODE_G:
                case CTRL | SHIFT | KeyEvent.KEYCODE_G:
                case KeyEvent.KEYCODE_F3:
                case SHIFT | KeyEvent.KEYCODE_F3:
                    menuOrKeyboardActionController.onMenuOrKeyboardAction(
                            R.id.find_in_page_id, false);
                    return true;
                case CTRL | KeyEvent.KEYCODE_L:
                case ALT | KeyEvent.KEYCODE_D:
                case KeyEvent.KEYCODE_BUTTON_X:
                    menuOrKeyboardActionController.onMenuOrKeyboardAction(
                            R.id.focus_url_bar, false);
                    return true;
                case CTRL | SHIFT | KeyEvent.KEYCODE_B:
                    menuOrKeyboardActionController.onMenuOrKeyboardAction(
                            R.id.all_bookmarks_menu_id, false);
                    return true;
                case KeyEvent.KEYCODE_BOOKMARK:
                case CTRL | KeyEvent.KEYCODE_D:
                    menuOrKeyboardActionController.onMenuOrKeyboardAction(
                            R.id.bookmark_this_page_id, false);
                    return true;
                case CTRL | KeyEvent.KEYCODE_H:
                    menuOrKeyboardActionController.onMenuOrKeyboardAction(
                            R.id.open_history_menu_id, false);
                    return true;
                case CTRL | KeyEvent.KEYCODE_P:
                    menuOrKeyboardActionController.onMenuOrKeyboardAction(R.id.print_id, false);
                    return true;
                case CTRL | KeyEvent.KEYCODE_PLUS:
                case CTRL | KeyEvent.KEYCODE_EQUALS:
                case CTRL | SHIFT | KeyEvent.KEYCODE_PLUS:
                case CTRL | SHIFT | KeyEvent.KEYCODE_EQUALS:
                case KeyEvent.KEYCODE_ZOOM_IN:
                    ZoomController.zoomIn(currentWebContents);
                    return true;
                case CTRL | KeyEvent.KEYCODE_MINUS:
                case KeyEvent.KEYCODE_ZOOM_OUT:
                    ZoomController.zoomOut(currentWebContents);
                    return true;
                case CTRL | KeyEvent.KEYCODE_0:
                    ZoomController.zoomReset(currentWebContents);
                    return true;
                case SHIFT | CTRL | KeyEvent.KEYCODE_R:
                case CTRL | KeyEvent.KEYCODE_R:
                case SHIFT | KeyEvent.KEYCODE_F5:
                case KeyEvent.KEYCODE_F5:
                    if (currentTab != null) {
                        if ((keyCodeAndMeta & SHIFT) == SHIFT) {
                            currentTab.reloadIgnoringCache();
                        } else {
                            currentTab.reload();
                        }

                        if (toolbarManager != null && currentWebContents != null
                                && currentWebContents.focusLocationBarByDefault()) {
                            toolbarManager.revertLocationBarChanges();
                        } else if (currentTab.getView() != null) {
                            currentTab.getView().requestFocus();
                        }
                    }
                    return true;
                case ALT | KeyEvent.KEYCODE_DPAD_LEFT:
                    if (currentTab != null && currentTab.canGoBack()) currentTab.goBack();
                    return true;
                case ALT | KeyEvent.KEYCODE_DPAD_RIGHT:
                case KeyEvent.KEYCODE_FORWARD:
                case KeyEvent.KEYCODE_BUTTON_START:
                    if (currentTab != null && currentTab.canGoForward()) currentTab.goForward();
                    return true;
                case CTRL | SHIFT | KeyEvent.KEYCODE_SLASH:  // i.e. Ctrl+?
                    menuOrKeyboardActionController.onMenuOrKeyboardAction(R.id.help_id, false);
                    return true;
            }
        }

        return false;
    }
}
