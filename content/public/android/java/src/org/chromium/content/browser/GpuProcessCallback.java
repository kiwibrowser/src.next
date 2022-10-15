// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.content.browser;

import android.view.Surface;

import org.chromium.base.UnguessableToken;
import org.chromium.base.annotations.JNINamespace;
import org.chromium.base.annotations.NativeMethods;
import org.chromium.content.common.IGpuProcessCallback;
import org.chromium.content.common.SurfaceWrapper;

@JNINamespace("content")
class GpuProcessCallback extends IGpuProcessCallback.Stub {
    GpuProcessCallback() {}

    @Override
    public void forwardSurfaceForSurfaceRequest(UnguessableToken requestToken, Surface surface) {
        GpuProcessCallbackJni.get().completeScopedSurfaceRequest(requestToken, surface);
    }

    @Override
    public SurfaceWrapper getViewSurface(int surfaceId) {
        return GpuProcessCallbackJni.get().getViewSurface(surfaceId);
    }

    @NativeMethods
    interface Natives {
        void completeScopedSurfaceRequest(UnguessableToken requestToken, Surface surface);
        SurfaceWrapper getViewSurface(int surfaceId);
    }
};
