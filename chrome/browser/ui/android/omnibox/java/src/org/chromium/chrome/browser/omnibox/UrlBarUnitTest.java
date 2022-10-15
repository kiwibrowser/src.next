// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.omnibox;

import static org.mockito.Mockito.verify;

import android.app.Activity;
import android.text.SpannableStringBuilder;
import android.view.ViewStructure;

import androidx.test.filters.SmallTest;

import org.junit.Assert;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import org.mockito.MockitoAnnotations;
import org.robolectric.Robolectric;

import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.base.test.util.Feature;
import org.chromium.chrome.browser.omnibox.UrlBar.UrlBarDelegate;

/**
 * Unit tests for the URL bar UI component.
 */
@RunWith(BaseRobolectricTestRunner.class)
public class UrlBarUnitTest {
    private UrlBar mUrlBar;
    @Mock
    private UrlBarDelegate mUrlBarDelegate;
    @Mock
    private ViewStructure mViewStructure;

    @Before
    public void setUp() {
        MockitoAnnotations.initMocks(this);

        Activity activity = Robolectric.buildActivity(Activity.class).setup().get();

        mUrlBar = new UrlBarApi26(activity, null);
        mUrlBar.setDelegate(mUrlBarDelegate);
    }

    @Test
    @SmallTest
    @Feature("Omnibox")
    public void testAutofillStructureReceivesFullURL() {
        mUrlBar.setTextForAutofillServices("https://www.google.com");
        mUrlBar.setText("www.google.com");
        mUrlBar.onProvideAutofillStructure(mViewStructure, 0);

        ArgumentCaptor<SpannableStringBuilder> haveUrl =
                ArgumentCaptor.forClass(SpannableStringBuilder.class);
        verify(mViewStructure).setText(haveUrl.capture());
        Assert.assertEquals("https://www.google.com", haveUrl.getValue().toString());
    }
}
