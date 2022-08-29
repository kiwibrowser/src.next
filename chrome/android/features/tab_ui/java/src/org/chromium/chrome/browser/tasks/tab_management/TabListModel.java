// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tasks.tab_management;

import static org.chromium.chrome.browser.tasks.tab_management.MessageCardViewProperties.MESSAGE_TYPE;
import static org.chromium.chrome.browser.tasks.tab_management.TabListModel.CardProperties.CARD_ALPHA;
import static org.chromium.chrome.browser.tasks.tab_management.TabListModel.CardProperties.CARD_TYPE;
import static org.chromium.chrome.browser.tasks.tab_management.TabListModel.CardProperties.ModelType.MESSAGE;
import static org.chromium.chrome.browser.tasks.tab_management.TabListModel.CardProperties.ModelType.NEW_TAB_TILE_DEPRECATED;
import static org.chromium.chrome.browser.tasks.tab_management.TabListModel.CardProperties.ModelType.OTHERS;
import static org.chromium.chrome.browser.tasks.tab_management.TabListModel.CardProperties.ModelType.TAB;
import static org.chromium.chrome.browser.tasks.tab_management.TabProperties.TAB_ID;

import android.util.Pair;

import androidx.annotation.IntDef;

import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tabmodel.TabModel;
import org.chromium.chrome.browser.tasks.pseudotab.PseudoTab;
import org.chromium.ui.modelutil.MVCListAdapter;
import org.chromium.ui.modelutil.MVCListAdapter.ModelList;
import org.chromium.ui.modelutil.PropertyListModel;
import org.chromium.ui.modelutil.PropertyModel;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.util.List;

// TODO(meiliang): Rename TabListModel to CardListModel, since this ModelList not only contains
// Tabs anymore.
/**
 * A {@link PropertyListModel} implementation to keep information about a list of
 * {@link org.chromium.chrome.browser.tab.Tab}s.
 */
class TabListModel extends ModelList {
    /**
     * Required properties for each {@link PropertyModel} managed by this {@link ModelList}.
     */
    static class CardProperties {
        /** Supported Model type within this ModelList. */
        @IntDef({TAB, MESSAGE, NEW_TAB_TILE_DEPRECATED, OTHERS})
        @Retention(RetentionPolicy.SOURCE)
        public @interface ModelType {
            int TAB = 0;
            int MESSAGE = 1;
            int NEW_TAB_TILE_DEPRECATED = 2;
            int OTHERS = 3;
        }

        /** This corresponds to {@link CardProperties.ModelType}*/
        public static final PropertyModel.ReadableIntPropertyKey CARD_TYPE =
                new PropertyModel.ReadableIntPropertyKey();

        public static final PropertyModel.WritableFloatPropertyKey CARD_ALPHA =
                new PropertyModel.WritableFloatPropertyKey();
    }

    /**
     * Convert the given tab ID to an index to match during partial updates.
     * @param tabId The tab ID to search for.
     * @return The index within the model {@link org.chromium.ui.modelutil.SimpleList}.
     */
    public int indexFromId(int tabId) {
        for (int i = 0; i < size(); i++) {
            PropertyModel model = get(i).model;
            if (model.get(CARD_TYPE) == TAB && model.get(TAB_ID) == tabId) return i;
        }
        return TabModel.INVALID_TAB_INDEX;
    }

    /**
     * Find the Nth TAB card in the {@link TabListModel}.
     * @param n N of the Nth TAB card.
     * @return The index of Nth TAB card in the {@link TabListModel}.
     */
    public int indexOfNthTabCard(int n) {
        if (n < 0) return TabModel.INVALID_TAB_INDEX;
        int tabCount = 0;
        int lastTabIndex = TabModel.INVALID_TAB_INDEX;
        for (int i = 0; i < size(); i++) {
            PropertyModel model = get(i).model;
            if (model.get(CARD_TYPE) == TAB) {
                if (tabCount++ == n) return i;
                lastTabIndex = i;
            }
        }
        // If n >= tabCount, we return the index after the last tab. This is used when adding a new
        // tab.
        return lastTabIndex + 1;
    }

    /**
     * Get the number of TAB cards before the given index in TabListModel.
     * @param index The given index in TabListModel.
     * @return The number of TAB cards before the given index.
     */
    public int getTabCardCountsBefore(int index) {
        if (index < 0) return TabModel.INVALID_TAB_INDEX;
        if (index > size()) index = size();
        int tabCount = 0;
        for (int i = 0; i < index; i++) {
            if (get(i).model.get(CARD_TYPE) == TAB) tabCount++;
        }
        return tabCount;
    }

    /**
     * Get the index of the last tab before the given index in TabListModel.
     * @param index The given index in TabListModel.
     * @return The index of the tab before the given index in TabListModel.
     */
    public int getTabIndexBefore(int index) {
        for (int i = index - 1; i >= 0; i--) {
            if (get(i).model.get(CARD_TYPE) == TAB) return i;
        }
        return TabModel.INVALID_TAB_INDEX;
    }

    /**
     * Get the index of the first tab after the given index in TabListModel.
     * @param index The given index in TabListModel.
     * @return The index of the tab after the given index in TabListModel.
     */
    public int getTabIndexAfter(int index) {
        for (int i = index + 1; i < size(); i++) {
            if (get(i).model.get(CARD_TYPE) == TAB) return i;
        }
        return TabModel.INVALID_TAB_INDEX;
    }

    /**
     * Gets the new position of the Tab with {@link tabId} from a sorted list with MRU order.
     * @param tabId The id of the Tab to insert into the list.
     */
    public int getNewPositionInMruOrderList(int tabId) {
        long timestamp = PseudoTab.fromTabId(tabId).getTimestampMillis();
        int pos = 0;
        while (pos < size()) {
            PropertyModel model = get(pos).model;
            if (model.get(CARD_TYPE) != TAB
                    || (PseudoTab.fromTabId(model.get(TabProperties.TAB_ID)).getTimestampMillis()
                                    - timestamp
                            >= 0)) {
                pos++;
            } else {
                break;
            }
        }
        return pos;
    }

    /**
     * Get the index that matches a message item that has the given message type.
     * @param messageType The message type to match.
     * @return The index within the model.
     */
    public int lastIndexForMessageItemFromType(@MessageService.MessageType int messageType) {
        for (int i = size() - 1; i >= 0; i--) {
            PropertyModel model = get(i).model;
            if (model.get(CARD_TYPE) == MESSAGE && model.get(MESSAGE_TYPE) == messageType) {
                return i;
            }
        }
        return TabModel.INVALID_TAB_INDEX;
    }

    /**
     * Get the last index of a message item.
     */
    public int lastIndexForMessageItem() {
        for (int i = size() - 1; i >= 0; i--) {
            PropertyModel model = get(i).model;
            if (model.get(CARD_TYPE) == MESSAGE) {
                return i;
            }
        }
        return TabModel.INVALID_TAB_INDEX;
    }

    @Override
    public void add(int position, MVCListAdapter.ListItem item) {
        assert validateListItem(item);
        super.add(position, item);
    }

    private boolean validateListItem(MVCListAdapter.ListItem item) {
        try {
            item.model.get(CARD_TYPE);
        } catch (IllegalArgumentException e) {
            return false;
        }
        return true;
    }

    /**
     * Sync the {@link TabListModel} with updated information. Update tab id of
     * the item in {@code index} with the current selected {@code tab} of the group.
     * @param selectedTab   The current selected tab in the group.
     * @param index         The index of the item in {@link TabListModel} that needs to be updated.
     */
    void updateTabListModelIdForGroup(Tab selectedTab, int index) {
        if (get(index).model.get(CARD_TYPE) != TAB) return;
        get(index).model.set(TabProperties.TAB_ID, selectedTab.getId());
    }

    /**
     * This method gets indexes in the {@link TabListModel} of the two tabs that are merged into one
     * group. When moving a Tab to a group, we always put it at the end of the group. For example:
     * move tab1 to tab2 to form a group, tab1 is after tab2 in the TabModel (tab2, tab1); Then
     * move another Tab tab3 to (tab2, tab1) group, tab3 is after tab1, (tab2, tab1, tab3). Thus,
     * the last Tab in the related Tabs is the movedTab. We use this to find the srcIndex; and query
     * all of its related Tabs to find the desIndex, i.e., the index of the current group / Tab to
     * move to.
     *
     * @param tabModel   The tabModel that owns the tabs.
     * @param tabs       The list that contains tabs of the newly merged group.
     * @return A Pair with its first member as the index of the tab that is selected to merge and
     * the second member as the index of the tab that is being merged into.
     */
    Pair<Integer, Integer> getIndexesForMergeToGroup(TabModel tabModel, List<Tab> tabs) {
        int desIndex = TabModel.INVALID_TAB_INDEX;
        int srcIndex = TabModel.INVALID_TAB_INDEX;
        int lastTabModelIndex = tabModel.indexOf(tabs.get(tabs.size() - 1));
        for (int i = lastTabModelIndex; i >= 0; i--) {
            Tab curTab = tabModel.getTabAt(i);
            if (!tabs.contains(curTab)) break;
            int index = indexFromId(curTab.getId());
            if (index != TabModel.INVALID_TAB_INDEX && srcIndex == TabModel.INVALID_TAB_INDEX) {
                srcIndex = index;
            } else if (index != TabModel.INVALID_TAB_INDEX
                    && desIndex == TabModel.INVALID_TAB_INDEX) {
                desIndex = index;
            }
        }
        return new Pair<>(desIndex, srcIndex);
    }

    /**
     * This method updates the information in {@link TabListModel} of the selected tab when a merge
     * related operation happens.
     * @param index         The index of the item in {@link TabListModel} that needs to be updated.
     * @param isSelected    Whether the tab is selected or not in a merge related operation. If
     *         selected, update the corresponding item in {@link TabListModel} to the selected
     *         state. If not, restore it to original state.
     */
    void updateSelectedTabForMergeToGroup(int index, boolean isSelected) {
        if (index < 0 || index >= size()) return;

        assert get(index).model.get(CARD_TYPE) == TAB;

        int status = isSelected ? ClosableTabGridView.AnimationStatus.SELECTED_CARD_ZOOM_IN
                                : ClosableTabGridView.AnimationStatus.SELECTED_CARD_ZOOM_OUT;
        if (get(index).model.get(TabProperties.CARD_ANIMATION_STATUS) == status) return;

        get(index).model.set(TabProperties.CARD_ANIMATION_STATUS, status);
        get(index).model.set(CARD_ALPHA, isSelected ? 0.8f : 1f);
    }

    /**
     * This method updates the information in {@link TabListModel} of the hovered tab when a merge
     * related operation happens.
     * @param index         The index of the item in {@link TabListModel} that needs to be updated.
     * @param isHovered     Whether the tab is hovered or not in a merge related operation. If
     *         hovered, update the corresponding item in {@link TabListModel} to the hovered state.
     *         If not, restore it to original state.
     */
    void updateHoveredTabForMergeToGroup(int index, boolean isHovered) {
        if (index < 0 || index >= size()) return;

        assert get(index).model.get(CARD_TYPE) == TAB;

        int status = isHovered ? ClosableTabGridView.AnimationStatus.HOVERED_CARD_ZOOM_IN
                               : ClosableTabGridView.AnimationStatus.HOVERED_CARD_ZOOM_OUT;
        if (get(index).model.get(TabProperties.CARD_ANIMATION_STATUS) == status) return;

        get(index).model.set(TabProperties.CARD_ANIMATION_STATUS, status);
    }
}
