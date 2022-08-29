// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser;

import org.chromium.base.annotations.NativeMethods;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.content_public.browser.WebContents;

import javax.inject.Inject;

import dagger.Reusable;

/**
 * This factory creates WebContents objects and the associated native counterpart.
 * TODO(dtrainor): Move this to the content/ layer if BrowserContext is ever supported in Java.
 */
@Reusable
public class WebContentsFactory {
    @Inject
    public WebContentsFactory() {}

    /**
     * A factory method to build a {@link WebContents} object.
     * @param profile         The profile with which the {@link WebContents} should be built.
     * @param initiallyHidden Whether or not the {@link WebContents} should be initially hidden.
     * @return                A newly created {@link WebContents} object.
     */
    // TODO(https://crbug.com/1099138): Remove static for unit-testability.
    public static WebContents createWebContents(Profile profile, boolean initiallyHidden) {
        return WebContentsFactoryJni.get().createWebContents(profile, initiallyHidden, false);
    }

    // TODO(https://crbug.com/1033955): Remove after check discard error is fixed.
    private static WebContents createWebContents(
            Profile profile, boolean initiallyHidden, boolean initializeRenderer) {
        return WebContentsFactoryJni.get().createWebContents(
                profile, initiallyHidden, initializeRenderer);
    }

    /**
     * A factory method to build a {@link WebContents} object.
     *
     * Also creates and initializes the renderer.
     *
     * @param profile         The profile to be used by the WebContents.
     * @param initiallyHidden Whether or not the {@link WebContents} should be initially hidden.
     * @return                A newly created {@link WebContents} object.
     */
    public WebContents createWebContentsWithWarmRenderer(Profile profile, boolean initiallyHidden) {
        return createWebContents(profile, initiallyHidden, true);
    }

    @NativeMethods
    interface Natives {
        WebContents createWebContents(
                Profile profile, boolean initiallyHidden, boolean initializeRenderer);
    }
}
