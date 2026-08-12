// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tasks.tab_management;

import org.chromium.base.Token;
import org.chromium.build.annotations.NullMarked;
import org.chromium.components.tab_groups.TabGroupColorId;

/**
 * {@link TabListMediator.TabListLayoutType#FLAT} implementation of {@link
 * TabGroupObserverDelegate}.
 */
@NullMarked
class FlatTabGroupObserverDelegate extends TabGroupObserverDelegate {
    FlatTabGroupObserverDelegate(TabListMediator mediator, TabListModel modelList) {
        super(mediator, modelList);
    }

    @Override
    public void didChangeTabGroupColor(Token tabGroupId, @TabGroupColorId int newColor) {}
}
