// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tabmodel;

import org.chromium.base.ObserverList;
import org.chromium.base.ThreadUtils;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabCreationState;
import org.chromium.chrome.browser.tab.TabLaunchType;
import org.chromium.chrome.browser.tab.TabSelectionType;

import java.util.List;

/**
 * A TabModel implementation that handles off the record tabs.
 *
 * <p>
 * This is not thread safe and must only be operated on the UI thread.
 *
 * <p>
 * The lifetime of this object is not tied to that of the native TabModel.  This ensures the
 * native TabModel is present when at least one incognito Tab has been created and added.  When
 * no Tabs remain, the native model will be destroyed and only rebuilt when a new incognito Tab
 * is created.
 */
class IncognitoTabModelImpl implements IncognitoTabModel {
    /** Creates TabModels for use in IncognitoModel. */
    public interface IncognitoTabModelDelegate {
        /** Creates a fully working TabModel to delegate calls to. */
        TabModel createTabModel();
    }

    private final IncognitoTabModelDelegate mDelegate;
    private final ObserverList<TabModelObserver> mObservers = new ObserverList<>();
    private final ObserverList<IncognitoTabModelObserver> mIncognitoObservers =
            new ObserverList<>();
    private TabModel mDelegateModel;
    private int mCountOfAddingOrClosingTabs;
    private boolean mActive;

    /**
     * Constructor for IncognitoTabModel.
     */
    IncognitoTabModelImpl(IncognitoTabModelDelegate tabModelCreator) {
        mDelegate = tabModelCreator;
        mDelegateModel = EmptyTabModel.getInstance();
    }

    /**
     * Ensures that the real TabModel has been created.
     */
    protected void ensureTabModelImpl() {
        ThreadUtils.assertOnUiThread();
        if (!(mDelegateModel instanceof EmptyTabModel)) return;

        mDelegateModel = mDelegate.createTabModel();
        for (TabModelObserver observer : mObservers) {
            mDelegateModel.addObserver(observer);
        }
    }

    /**
     * Resets the delegate TabModel to be a stub EmptyTabModel and notifies
     * {@link IncognitoTabModelObserver}s.
     */
    protected void destroyIncognitoIfNecessary() {
        ThreadUtils.assertOnUiThread();
        if (!isEmpty() || mDelegateModel instanceof EmptyTabModel
                || mCountOfAddingOrClosingTabs != 0) {
            return;
        }

        for (IncognitoTabModelObserver observer : mIncognitoObservers) {
            observer.didBecomeEmpty();
        }

        mDelegateModel.destroy();

        mDelegateModel = EmptyTabModel.getInstance();
    }

    private boolean isEmpty() {
        return getComprehensiveModel().getCount() == 0;
    }

    // Triggers IncognitoTabModelObserver.wasFirstTabCreated function. This function should only be
    // called just after the first tab is created.
    private void notifyIncognitoObserverFirstTabCreated(boolean shouldTrigger) {
        if (!shouldTrigger) return;
        assert getCount() == 1;

        for (IncognitoTabModelObserver observer : mIncognitoObservers) {
            observer.wasFirstTabCreated();
        }
    }

    @Override
    public Profile getProfile() {
        return mDelegateModel.getProfile();
    }

    @Override
    public boolean isIncognito() {
        return true;
    }

    @Override
    public boolean closeTab(Tab tab) {
        mCountOfAddingOrClosingTabs++;
        boolean retVal = mDelegateModel.closeTab(tab);
        mCountOfAddingOrClosingTabs--;
        destroyIncognitoIfNecessary();
        return retVal;
    }

    @Override
    public boolean closeTab(Tab tab, boolean animate, boolean uponExit, boolean canUndo) {
        mCountOfAddingOrClosingTabs++;
        boolean retVal = mDelegateModel.closeTab(tab, animate, uponExit, canUndo);
        mCountOfAddingOrClosingTabs--;
        destroyIncognitoIfNecessary();
        return retVal;
    }

    @Override
    public boolean closeTab(
            Tab tab, Tab recommendedNextTab, boolean animate, boolean uponExit, boolean canUndo) {
        mCountOfAddingOrClosingTabs++;
        boolean retVal =
                mDelegateModel.closeTab(tab, recommendedNextTab, animate, uponExit, canUndo);
        mCountOfAddingOrClosingTabs--;
        destroyIncognitoIfNecessary();
        return retVal;
    }

    @Override
    public Tab getNextTabIfClosed(int id, boolean uponExit) {
        return mDelegateModel.getNextTabIfClosed(id, uponExit);
    }

    @Override
    public void closeMultipleTabs(List<Tab> tabs, boolean canUndo) {
        mCountOfAddingOrClosingTabs++;
        mDelegateModel.closeMultipleTabs(tabs, canUndo);
        mCountOfAddingOrClosingTabs--;
        destroyIncognitoIfNecessary();
    }

    @Override
    public void closeAllTabs() {
        mCountOfAddingOrClosingTabs++;
        mDelegateModel.closeAllTabs();
        mCountOfAddingOrClosingTabs--;
        destroyIncognitoIfNecessary();
    }

    @Override
    public void closeAllTabs(boolean uponExit) {
        mCountOfAddingOrClosingTabs++;
        mDelegateModel.closeAllTabs(uponExit);
        mCountOfAddingOrClosingTabs--;
        destroyIncognitoIfNecessary();
    }

    @Override
    public int getCount() {
        return mDelegateModel.getCount();
    }

    @Override
    public Tab getTabAt(int index) {
        return mDelegateModel.getTabAt(index);
    }

    @Override
    public int indexOf(Tab tab) {
        return mDelegateModel.indexOf(tab);
    }

    @Override
    public int index() {
        return mDelegateModel.index();
    }

    @Override
    public void setIndex(int i, @TabSelectionType int type, boolean skipLoadingTab) {
        mDelegateModel.setIndex(i, type, skipLoadingTab);
    }

    @Override
    public boolean isActiveModel() {
        return mActive;
    }

    @Override
    public void moveTab(int id, int newIndex) {
        mDelegateModel.moveTab(id, newIndex);
    }

    @Override
    public void destroy() {
        mDelegateModel.destroy();
    }

    @Override
    public boolean isClosurePending(int tabId) {
        return mDelegateModel.isClosurePending(tabId);
    }

    @Override
    public boolean supportsPendingClosures() {
        return mDelegateModel.supportsPendingClosures();
    }

    @Override
    public TabList getComprehensiveModel() {
        return mDelegateModel.getComprehensiveModel();
    }

    @Override
    public void commitAllTabClosures() {
        // Return early if no tabs are open. In particular, we don't want to destroy the incognito
        // tab model, in case we are about to add a tab to it.
        if (isEmpty()) return;
        mDelegateModel.commitAllTabClosures();
        destroyIncognitoIfNecessary();
    }

    @Override
    public void commitTabClosure(int tabId) {
        mDelegateModel.commitTabClosure(tabId);
        destroyIncognitoIfNecessary();
    }

    @Override
    public void cancelTabClosure(int tabId) {
        mDelegateModel.cancelTabClosure(tabId);
    }

    @Override
    public void notifyAllTabsClosureUndone() {
        mDelegateModel.notifyAllTabsClosureUndone();
    }

    @Override
    public void addTab(
            Tab tab, int index, @TabLaunchType int type, @TabCreationState int creationState) {
        mCountOfAddingOrClosingTabs++;
        ensureTabModelImpl();
        boolean shouldTriggerFirstTabCreated = getCount() == 0;
        mDelegateModel.addTab(tab, index, type, creationState);
        notifyIncognitoObserverFirstTabCreated(shouldTriggerFirstTabCreated);
        mCountOfAddingOrClosingTabs--;
    }

    @Override
    public void addObserver(TabModelObserver observer) {
        mObservers.addObserver(observer);
        mDelegateModel.addObserver(observer);
    }

    @Override
    public void removeObserver(TabModelObserver observer) {
        mObservers.removeObserver(observer);
        mDelegateModel.removeObserver(observer);
    }

    @Override
    public void setActive(boolean active) {
        mActive = active;
        if (active) ensureTabModelImpl();
        mDelegateModel.setActive(active);
        if (!active) destroyIncognitoIfNecessary();
    }

    @Override
    public void addIncognitoObserver(IncognitoTabModelObserver observer) {
        mIncognitoObservers.addObserver(observer);
    }

    @Override
    public void removeIncognitoObserver(IncognitoTabModelObserver observer) {
        mIncognitoObservers.removeObserver(observer);
    }

    @Override
    public void removeTab(Tab tab) {
        mCountOfAddingOrClosingTabs++;
        mDelegateModel.removeTab(tab);
        mCountOfAddingOrClosingTabs--;
        // Call destroyIncognitoIfNecessary() in case the last incognito tab in this model is
        // reparented to a different activity. See crbug.com/611806.
        destroyIncognitoIfNecessary();
    }

    @Override
    public void openMostRecentlyClosedEntry() {}
}
