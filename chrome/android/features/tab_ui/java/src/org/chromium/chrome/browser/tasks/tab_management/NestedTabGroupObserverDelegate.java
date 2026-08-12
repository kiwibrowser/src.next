// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tasks.tab_management;

import static org.chromium.chrome.browser.tasks.tab_management.TabListModel.CardProperties.CARD_TYPE;
import static org.chromium.chrome.browser.tasks.tab_management.TabListModel.CardProperties.ModelType.TAB;

import org.chromium.base.Token;
import org.chromium.build.annotations.NullMarked;
import org.chromium.components.tab_group_sync.EitherId.EitherGroupId;
import org.chromium.components.tab_group_sync.LocalTabGroupId;
import org.chromium.components.tab_groups.TabGroupColorId;
import org.chromium.ui.modelutil.PropertyModel;

/**
 * {@link TabListMediator.TabListLayoutType#NESTED} implementation of {@link
 * TabGroupObserverDelegate}.
 */
@NullMarked
class NestedTabGroupObserverDelegate extends TabGroupObserverDelegate {
    NestedTabGroupObserverDelegate(TabListMediator mediator, TabListModel modelList) {
        super(mediator, modelList);
    }

    @Override
    public void didChangeTabGroupColor(Token tabGroupId, @TabGroupColorId int newColor) {
        super.didChangeTabGroupColor(tabGroupId, newColor);
        // Sync the color down to the child models so decorations (like the group spine) can
        // read it.
        updateColorForChildTabsInNestedLayout(tabGroupId, newColor);
    }

    /**
     * Updates the UI properties of child tabs when their group color changes.
     *
     * @param tabGroupId The ID of the tab group.
     * @param newColor The new color of the tab group.
     */
    private void updateColorForChildTabsInNestedLayout(
            Token tabGroupId, @TabGroupColorId int newColor) {
        boolean foundGroup = false;
        for (int i = 0; i < mModelList.size(); i++) {
            PropertyModel childModel = mModelList.get(i).model;
            if (childModel.get(CARD_TYPE) == TAB
                    && tabGroupId.equals(childModel.get(TabProperties.TAB_GROUP_ID))) {
                mMediator.updateTabGroupColorViewProvider(
                        EitherGroupId.createLocalId(new LocalTabGroupId(tabGroupId)),
                        childModel,
                        newColor);
                foundGroup = true;
            } else if (foundGroup) {
                break;
            }
        }
    }
}
