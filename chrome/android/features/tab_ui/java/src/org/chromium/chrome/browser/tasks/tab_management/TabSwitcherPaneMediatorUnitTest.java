// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tasks.tab_management;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import static org.chromium.chrome.browser.tasks.tab_management.TabListContainerProperties.BROWSER_CONTROLS_STATE_PROVIDER;
import static org.chromium.chrome.browser.tasks.tab_management.TabListContainerProperties.FOCUS_TAB_INDEX_FOR_ACCESSIBILITY;
import static org.chromium.chrome.browser.tasks.tab_management.TabListContainerProperties.INITIAL_SCROLL_INDEX;
import static org.chromium.chrome.browser.tasks.tab_management.TabListContainerProperties.IS_INCOGNITO;
import static org.chromium.chrome.browser.tasks.tab_management.TabListContainerProperties.MODE;
import static org.chromium.chrome.browser.tasks.tab_management.TabListContainerProperties.PANE_KEYS;

import android.view.View;
import android.view.ViewGroup;

import androidx.test.filters.SmallTest;

import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Captor;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.robolectric.shadows.ShadowLooper;

import org.chromium.base.Callback;
import org.chromium.base.supplier.LazyOneshotSupplier;
import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.base.supplier.ObservableSupplierImpl;
import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.base.test.util.Features.DisableFeatures;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.tab.MockTab;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabCreationState;
import org.chromium.chrome.browser.tab.TabLaunchType;
import org.chromium.chrome.browser.tabmodel.TabModelFilter;
import org.chromium.chrome.browser.tabmodel.TabModelObserver;
import org.chromium.chrome.browser.tasks.tab_management.TabGridDialogMediator.DialogController;
import org.chromium.chrome.browser.tasks.tab_management.TabListCoordinator.TabListMode;
import org.chromium.chrome.browser.tasks.tab_management.TabListEditorCoordinator.TabListEditorController;
import org.chromium.chrome.browser.tasks.tab_management.TabListMediator.TabActionListener;
import org.chromium.chrome.features.start_surface.StartSurfaceUserData;
import org.chromium.chrome.test.util.browser.tabmodel.MockTabModel;
import org.chromium.components.browser_ui.widget.gesture.BackPressHandler.BackPressResult;
import org.chromium.ui.modelutil.PropertyModel;

import java.util.List;

/** Unit tests for {@link TabSwitcherPaneMediator}. */
@RunWith(BaseRobolectricTestRunner.class)
public class TabSwitcherPaneMediatorUnitTest {
    private static final int UNGROUPED_TAB_ID = 1;
    private static final int GROUPED_TAB_1_ID = 2;
    private static final int GROUPED_TAB_2_ID = 3;

    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Mock private TabSwitcherResetHandler mResetHandler;
    @Mock private DialogController mTabGridDialogController;
    @Mock private Runnable mOnTabSwitcherShownRunnable;
    @Mock private Profile mProfile;
    @Mock private TabModelFilter mTabModelFilter;
    @Mock private TabListEditorController mTabListEditorController;
    @Mock private ViewGroup mContainerView;
    @Mock private View mCustomView;
    @Mock private Runnable mCustomViewBackPressRunnable;
    @Mock private Callback<Integer> mOnTabClickedCallback;

    @Captor private ArgumentCaptor<TabModelObserver> mTabModelObserverCaptor;

    private final ObservableSupplierImpl<TabModelFilter> mTabModelFilterSupplier =
            new ObservableSupplierImpl<>();
    private final ObservableSupplierImpl<Boolean> mDialogBackPressChangedSupplier =
            new ObservableSupplierImpl<>();
    private final ObservableSupplierImpl<Boolean> mIsVisibleSupplier =
            new ObservableSupplierImpl<>();
    private final ObservableSupplierImpl<Boolean> mIsAnimatingSupplier =
            new ObservableSupplierImpl<>();
    private final ObservableSupplierImpl<TabListEditorController> mTabListEditorControllerSupplier =
            new ObservableSupplierImpl<>();
    private final ObservableSupplierImpl<Boolean> mTabListEditorBackPressChangedSupplier =
            new ObservableSupplierImpl<>();

    private LazyOneshotSupplier<DialogController> mTabGridDialogControllerSupplier;
    private PropertyModel mModel;
    private MockTabModel mTabModel;
    private Tab mUngroupedTab;
    private Tab mGroupedTab1;
    private Tab mGroupedTab2;
    private TabSwitcherPaneMediator mMediator;

    @Before
    public void setUp() {
        when(mProfile.isOffTheRecord()).thenReturn(false);
        mTabModel = new MockTabModel(mProfile, null);
        mTabModel.addTab(
                new MockTab(UNGROUPED_TAB_ID, mProfile, TabLaunchType.FROM_START_SURFACE),
                /* index= */ 0,
                TabLaunchType.FROM_START_SURFACE,
                TabCreationState.LIVE_IN_FOREGROUND);
        mTabModel.addTab(GROUPED_TAB_1_ID);
        mTabModel.addTab(GROUPED_TAB_2_ID);
        mUngroupedTab = mTabModel.getTabAt(0);
        mGroupedTab1 = mTabModel.getTabAt(1);
        mGroupedTab2 = mTabModel.getTabAt(2);
        when(mTabModelFilter.getRelatedTabList(UNGROUPED_TAB_ID))
                .thenReturn(List.of(mUngroupedTab));
        when(mTabModelFilter.getRelatedTabList(GROUPED_TAB_1_ID))
                .thenReturn(List.of(mGroupedTab1, mGroupedTab2));
        when(mTabModelFilter.getTabModel()).thenReturn(mTabModel);

        when(mTabGridDialogController.getHandleBackPressChangedSupplier())
                .thenReturn(mDialogBackPressChangedSupplier);
        when(mTabListEditorController.getHandleBackPressChangedSupplier())
                .thenReturn(mTabListEditorBackPressChangedSupplier);

        when(mTabGridDialogController.isVisible()).thenReturn(false);
        when(mTabListEditorController.isVisible()).thenReturn(false);

        mModel =
                new PropertyModel.Builder(PANE_KEYS)
                        .with(BROWSER_CONTROLS_STATE_PROVIDER, null)
                        .with(MODE, TabListMode.GRID)
                        .with(IS_INCOGNITO, false)
                        .build();
        mTabGridDialogControllerSupplier = LazyOneshotSupplier.fromValue(mTabGridDialogController);
        mMediator =
                new TabSwitcherPaneMediator(
                        mResetHandler,
                        mTabModelFilterSupplier,
                        mTabGridDialogControllerSupplier,
                        mModel,
                        mContainerView,
                        mOnTabSwitcherShownRunnable,
                        mIsVisibleSupplier,
                        mIsAnimatingSupplier,
                        mOnTabClickedCallback);

        assertTrue(mTabModelFilterSupplier.hasObservers());
        assertTrue(mIsVisibleSupplier.hasObservers());
        assertTrue(mIsAnimatingSupplier.hasObservers());

        mTabModelFilterSupplier.set(mTabModelFilter);
        verify(mTabModelFilter).addObserver(mTabModelObserverCaptor.capture());

        mMediator.setTabListEditorControllerSupplier(mTabListEditorControllerSupplier);
        assertTrue(mTabListEditorControllerSupplier.hasObservers());
        mTabListEditorControllerSupplier.set(mTabListEditorController);

        mTabGridDialogControllerSupplier.get();
        ShadowLooper.runUiThreadTasks();
        assertTrue(mDialogBackPressChangedSupplier.hasObservers());

        verify(mOnTabSwitcherShownRunnable, never()).run();
        mIsVisibleSupplier.set(true);
        mIsAnimatingSupplier.set(false);
        verify(mOnTabSwitcherShownRunnable).run();
    }

    @After
    public void tearDown() {
        mMediator.destroy();

        verify(mTabModelFilter).removeObserver(mTabModelObserverCaptor.getValue());

        assertFalse(mTabModelFilterSupplier.hasObservers());
        assertFalse(mIsVisibleSupplier.hasObservers());
        assertFalse(mIsAnimatingSupplier.hasObservers());
        assertFalse(mDialogBackPressChangedSupplier.hasObservers());
        assertFalse(mTabListEditorControllerSupplier.hasObservers());
        assertFalse(mTabListEditorBackPressChangedSupplier.hasObservers());
    }

    @Test
    @SmallTest
    public void testTabModelObserver() {
        // This observer is only used to update the back press state.
        TabModelObserver observer = mTabModelObserverCaptor.getValue();

        // Dialog visibility supplier is updated when back press internal state changes. Use this as
        // a proxy to detect that back press state updated.
        ObservableSupplier<Boolean> dialogVisibilitySupplier =
                mMediator.getIsDialogVisibleSupplier();
        assertFalse(dialogVisibilitySupplier.get());

        when(mTabListEditorController.isVisible()).thenReturn(true);
        observer.tabClosureUndone(null);
        assertTrue(dialogVisibilitySupplier.get());

        when(mTabListEditorController.isVisible()).thenReturn(false);
        observer.tabPendingClosure(null);
        assertFalse(dialogVisibilitySupplier.get());

        when(mTabListEditorController.isVisible()).thenReturn(true);
        observer.onFinishingTabClosure(null);
        assertTrue(dialogVisibilitySupplier.get());

        when(mTabListEditorController.isVisible()).thenReturn(false);
        observer.tabRemoved(null);
        assertFalse(dialogVisibilitySupplier.get());

        when(mTabListEditorController.isVisible()).thenReturn(true);
        observer.multipleTabsPendingClosure(null, false);
        assertTrue(dialogVisibilitySupplier.get());
    }

    @Test
    @SmallTest
    public void testTabModelObserverOnRestore() {
        TabModelObserver observer = mTabModelObserverCaptor.getValue();

        observer.restoreCompleted();
        verify(mResetHandler).resetWithTabList(mTabModelFilter, false);
    }

    @Test
    @SmallTest
    public void testIsDialogVisibleSupplier() {
        ObservableSupplier<Boolean> dialogVisibilitySupplier =
                mMediator.getIsDialogVisibleSupplier();
        assertFalse(dialogVisibilitySupplier.get());

        when(mTabListEditorController.isVisible()).thenReturn(true);
        mTabListEditorBackPressChangedSupplier.set(true);
        assertTrue(dialogVisibilitySupplier.get());

        when(mTabListEditorController.isVisible()).thenReturn(false);
        mTabListEditorBackPressChangedSupplier.set(false);
        assertFalse(dialogVisibilitySupplier.get());

        when(mTabGridDialogController.isVisible()).thenReturn(true);
        mDialogBackPressChangedSupplier.set(true);
        assertTrue(dialogVisibilitySupplier.get());

        when(mTabGridDialogController.isVisible()).thenReturn(false);
        mDialogBackPressChangedSupplier.set(false);
        assertFalse(dialogVisibilitySupplier.get());
    }

    @Test
    @SmallTest
    public void testRequestAccessibilityFocusOnCurrentTab() {
        int index = 5;
        when(mTabModelFilter.index()).thenReturn(index);
        mMediator.requestAccessibilityFocusOnCurrentTab();

        assertEquals(index, mModel.get(FOCUS_TAB_INDEX_FOR_ACCESSIBILITY).intValue());
    }

    @Test
    @SmallTest
    @DisableFeatures(ChromeFeatureList.BACK_GESTURE_REFACTOR)
    public void testBackPress() {
        assertFalse(mMediator.getHandleBackPressChangedSupplier().get());
        assertEquals(BackPressResult.FAILURE, mMediator.handleBackPress());

        when(mTabListEditorController.isVisible()).thenReturn(true);
        when(mTabListEditorController.handleBackPressed()).thenReturn(true);
        mTabListEditorBackPressChangedSupplier.set(true);
        assertTrue(mMediator.getHandleBackPressChangedSupplier().get());
        assertEquals(BackPressResult.SUCCESS, mMediator.handleBackPress());
        verify(mTabListEditorController, times(2)).handleBackPressed();
        when(mTabListEditorController.isVisible()).thenReturn(false);
        when(mTabListEditorController.handleBackPressed()).thenReturn(false);
        mTabListEditorBackPressChangedSupplier.set(false);
        assertFalse(mMediator.getHandleBackPressChangedSupplier().get());

        mIsAnimatingSupplier.set(true);
        assertTrue(mMediator.getHandleBackPressChangedSupplier().get());
        assertEquals(BackPressResult.SUCCESS, mMediator.handleBackPress());
        mIsAnimatingSupplier.set(false);
        assertFalse(mMediator.getHandleBackPressChangedSupplier().get());

        when(mTabGridDialogController.isVisible()).thenReturn(true);
        when(mTabGridDialogController.handleBackPressed()).thenReturn(true);
        mDialogBackPressChangedSupplier.set(true);
        assertTrue(mMediator.getHandleBackPressChangedSupplier().get());
        assertEquals(BackPressResult.SUCCESS, mMediator.handleBackPress());
        verify(mTabGridDialogController, times(2)).handleBackPressed();
        when(mTabGridDialogController.isVisible()).thenReturn(false);
        when(mTabGridDialogController.handleBackPressed()).thenReturn(false);
        mDialogBackPressChangedSupplier.set(false);
        assertFalse(mMediator.getHandleBackPressChangedSupplier().get());

        // Remove this section when removing the BACK_GESTURE_REFACTOR_ANDROID annotation. This is
        // here to assert in a legacy edgecase where back could be called without checking if it is
        // supported.
        mIsVisibleSupplier.set(false);
        assertFalse(mMediator.getHandleBackPressChangedSupplier().get());
        assertEquals(BackPressResult.FAILURE, mMediator.handleBackPress());
        mIsVisibleSupplier.set(true);
        assertFalse(mMediator.getHandleBackPressChangedSupplier().get());
    }

    @Test
    @SmallTest
    public void testOpenTabGridDialog() {
        assertNull(mMediator.openTabGridDialog(mUngroupedTab));

        TabActionListener listener = mMediator.openTabGridDialog(mGroupedTab1);
        assertNotNull(listener);
        listener.run(mGroupedTab1.getId());

        verify(mTabGridDialogController).resetWithListOfTabs(List.of(mGroupedTab1, mGroupedTab2));
    }

    @Test
    @SmallTest
    public void testOnTabSelecting() {
        assertFalse(StartSurfaceUserData.getKeepTab(mUngroupedTab));

        mMediator.onTabSelecting(mUngroupedTab.getId(), /* fromActionButton= */ true);
        assertTrue(StartSurfaceUserData.getKeepTab(mUngroupedTab));
        verify(mOnTabClickedCallback).onResult(UNGROUPED_TAB_ID);
    }

    @Test
    @SmallTest
    public void testScrollToTab() {
        int index = 5;
        when(mTabModelFilter.index()).thenReturn(index);

        mMediator.setInitialScrollIndexOffset();
        assertEquals(index, mModel.get(INITIAL_SCROLL_INDEX).intValue());

        index = 3;
        mMediator.scrollToTab(index);
        assertEquals(index, mModel.get(INITIAL_SCROLL_INDEX).intValue());
    }

    @Test
    @SmallTest
    public void testCustomViewWithClearTabList() {
        when(mTabListEditorController.isVisible()).thenReturn(true);

        mMediator.addCustomView(
                mCustomView, mCustomViewBackPressRunnable, /* clearTabList= */ true);
        verify(mResetHandler).resetWithTabList(null, false);
        verify(mContainerView).addView(mCustomView);
        verify(mTabListEditorController).hide();
        when(mTabListEditorController.isVisible()).thenReturn(false);

        assertTrue(mMediator.getHandleBackPressChangedSupplier().get());
        assertEquals(BackPressResult.SUCCESS, mMediator.handleBackPress());
        verify(mCustomViewBackPressRunnable).run();

        mMediator.removeCustomView(mCustomView);
        verify(mContainerView).removeView(mCustomView);
        assertFalse(mMediator.getHandleBackPressChangedSupplier().get());
    }

    @Test
    @SmallTest
    public void testCustomViewWithoutClearTabList() {
        when(mTabGridDialogController.isVisible()).thenReturn(true);

        mMediator.addCustomView(
                mCustomView, mCustomViewBackPressRunnable, /* clearTabList= */ false);
        verify(mResetHandler, never()).resetWithTabList(null, false);
        verify(mContainerView).addView(mCustomView);
        verify(mTabGridDialogController).hideDialog(false);
        when(mTabGridDialogController.isVisible()).thenReturn(false);

        assertTrue(mMediator.getHandleBackPressChangedSupplier().get());
        assertEquals(BackPressResult.SUCCESS, mMediator.handleBackPress());
        verify(mCustomViewBackPressRunnable).run();

        mMediator.removeCustomView(mCustomView);
        verify(mContainerView).removeView(mCustomView);
        assertFalse(mMediator.getHandleBackPressChangedSupplier().get());
    }
}
