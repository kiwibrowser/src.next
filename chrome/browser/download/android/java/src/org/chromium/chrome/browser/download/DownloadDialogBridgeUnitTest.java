// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.download;

import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.verify;

import android.app.Activity;

import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TestRule;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Captor;
import org.mockito.Mock;
import org.mockito.MockitoAnnotations;
import org.robolectric.Robolectric;
import org.robolectric.annotation.Config;
import org.robolectric.shadows.ShadowLog;

import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.base.test.util.CommandLineFlags;
import org.chromium.base.test.util.JniMocker;
import org.chromium.chrome.browser.download.dialogs.DownloadLocationDialogCoordinator;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.test.util.browser.Features;
import org.chromium.components.prefs.PrefService;
import org.chromium.net.ConnectionType;
import org.chromium.ui.modaldialog.ModalDialogManager;
import org.chromium.ui.modelutil.PropertyModel;

/**
 * Unit test for {@link DownloadDialogBridge}.
 */
@RunWith(BaseRobolectricTestRunner.class)
@Config(manifest = Config.NONE)
public class DownloadDialogBridgeUnitTest {
    private static final int FAKE_NATIVE_HOLDER = 1;
    private static final long INVALID_START_TIME = -1;
    private static final long START_TIME = 1000;
    private static final long TOTAL_BYTES = 100;
    private static final @ConnectionType int CONNECTION_TYPE = ConnectionType.CONNECTION_3G;
    private static final @DownloadLocationDialogType int LOCATION_DIALOG_TYPE =
            DownloadLocationDialogType.DEFAULT;
    private static final @DownloadLocationDialogType int LOCATION_DIALOG_ERROR_TYPE =
            DownloadLocationDialogType.NAME_CONFLICT;
    private static final Boolean isIncognito = false;

    private static final String SUGGESTED_PATH = "sdcard/download.txt";
    private static final String NEW_SUGGESTED_PATH = "sdcard/new_download.txt";

    private DownloadDialogBridge mBridge;

    @Rule
    public JniMocker mJniMocker = new JniMocker();

    @Rule
    public TestRule mFeaturesProcessor = new Features.JUnitProcessor();

    @Rule
    public TestRule mCommandLineFlagsRule = CommandLineFlags.getTestRule();

    @Mock
    private DownloadDialogBridge.Natives mNativeMock;

    @Mock
    ModalDialogManager mModalDialogManager;

    Activity mActivity;

    @Mock
    DownloadLocationDialogCoordinator mLocationDialog;

    @Mock
    private PrefService mPrefService;

    @Captor
    private ArgumentCaptor<PropertyModel> mModelCaptor;

    @Before
    public void setUp() {
        MockitoAnnotations.initMocks(this);
        ShadowLog.stream = System.out;
        mJniMocker.mock(DownloadDialogBridgeJni.TEST_HOOKS, mNativeMock);
        mActivity = Robolectric.buildActivity(Activity.class).setup().get();
        mBridge = new DownloadDialogBridge(FAKE_NATIVE_HOLDER, mLocationDialog);
        mBridge.setPrefServiceForTesting(mPrefService);
    }

    @After
    public void tearDown() {
        mBridge = null;
        mActivity = null;
    }

    private void showDialog() {
        mBridge.showDialog(mActivity, mModalDialogManager, mPrefService, TOTAL_BYTES,
                CONNECTION_TYPE, LOCATION_DIALOG_TYPE, SUGGESTED_PATH, true, isIncognito);
    }

    private void locationDialogWillReturn(String newPath) {
        doAnswer(invocation -> {
            mBridge.onDownloadLocationDialogComplete(newPath);
            return null;
        })
                .when(mLocationDialog)
                .showDialog(any(), any(), eq(TOTAL_BYTES), anyInt(), eq(SUGGESTED_PATH),
                        eq(isIncognito));
    }

    @Test
    @Features.DisableFeatures({ChromeFeatureList.DOWNLOAD_LATER})
    public void testShowDialog_disableDownloadLater() {
        doAnswer(invocation -> {
            mBridge.onDownloadLocationDialogComplete(NEW_SUGGESTED_PATH);
            return null;
        })
                .when(mLocationDialog)
                .showDialog(any(), any(), eq(TOTAL_BYTES), eq(LOCATION_DIALOG_TYPE),
                        eq(SUGGESTED_PATH), eq(isIncognito));

        showDialog();
        verify(mLocationDialog)
                .showDialog(any(), any(), eq(TOTAL_BYTES), eq(LOCATION_DIALOG_TYPE),
                        eq(SUGGESTED_PATH), eq(isIncognito));
        verify(mNativeMock)
                .onComplete(anyLong(), any(), eq(NEW_SUGGESTED_PATH), eq(false),
                        eq(INVALID_START_TIME));
    }

    @Test
    public void testDestroy() {
        mBridge.destroy();
        verify(mLocationDialog).destroy();
    }

    @Test
    @Features.DisableFeatures({ChromeFeatureList.DOWNLOAD_LATER})
    public void testLocationDialogCanceled_disableDownloadLater() {
        doAnswer(invocation -> {
            mBridge.onDownloadLocationDialogCanceled();
            return null;
        })
                .when(mLocationDialog)
                .showDialog(any(), any(), eq(TOTAL_BYTES), eq(LOCATION_DIALOG_TYPE),
                        eq(SUGGESTED_PATH), eq(isIncognito));

        showDialog();
        verify(mNativeMock).onCanceled(anyLong(), any());
    }
}
