// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tasks.tab_management;

import android.content.Context;
import android.graphics.drawable.Drawable;

import androidx.appcompat.content.res.AppCompatResources;

import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tasks.tab_groups.TabGroupModelFilter;
import org.chromium.chrome.browser.tasks.tab_management.TabUiMetricsHelper.TabListEditorActionMetricGroups;
import org.chromium.chrome.tab_ui.R;

import java.util.List;

/** Ungroup action for the {@link TabListEditorMenu}. */
public class TabListEditorUngroupAction extends TabListEditorAction {
    /**
     * Create an action for ungrouping tabs.
     * @param context for loading resources.
     * @param showMode whether to show an action view.
     * @param buttonType the type of the action view.
     * @param iconPosition the position of the icon in the action view.
     */
    public static TabListEditorAction createAction(
            Context context,
            @ShowMode int showMode,
            @ButtonType int buttonType,
            @IconPosition int iconPosition) {
        Drawable drawable = AppCompatResources.getDrawable(context, R.drawable.ic_widgets);
        return new TabListEditorUngroupAction(showMode, buttonType, iconPosition, drawable);
    }

    private TabListEditorUngroupAction(
            @ShowMode int showMode,
            @ButtonType int buttonType,
            @IconPosition int iconPosition,
            Drawable drawable) {
        super(
                R.id.tab_list_editor_ungroup_menu_item,
                showMode,
                buttonType,
                iconPosition,
                R.plurals.tab_selection_editor_ungroup_tabs,
                R.plurals.accessibility_tab_selection_editor_ungroup_tabs,
                drawable);
    }

    @Override
    public void onSelectionStateChange(List<Integer> tabIds) {
        setEnabledAndItemCount(!tabIds.isEmpty(), tabIds.size());
    }

    @Override
    public boolean performAction(List<Tab> tabs) {
        assert !editorSupportsActionOnRelatedTabs()
                : "Ungrouping is not supported when actions apply to related tabs.";

        TabGroupModelFilter filter = getTabGroupModelFilter();
        for (Tab tab : tabs) {
            filter.moveTabOutOfGroup(tab.getId());
        }
        TabUiMetricsHelper.recordSelectionEditorActionMetrics(
                TabListEditorActionMetricGroups.UNGROUP);
        return true;
    }

    @Override
    public boolean shouldHideEditorAfterAction() {
        return true;
    }
}
