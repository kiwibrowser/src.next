// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tasks.tab_management;

import org.chromium.build.annotations.NullMarked;

/**
 * {@link TabListMediator.TabListLayoutType#GROUPED} implementation of {@link
 * TabGroupObserverDelegate}.
 */
@NullMarked
class GroupedTabGroupObserverDelegate extends TabGroupObserverDelegate {
    GroupedTabGroupObserverDelegate(TabListMediator mediator, TabListModel modelList) {
        super(mediator, modelList);
    }
}
