// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.content.browser;

import androidx.annotation.Nullable;

import org.chromium.base.annotations.JNINamespace;
import org.chromium.base.annotations.NativeMethods;
import org.chromium.blink.mojom.PaymentOptions;
import org.chromium.content_public.browser.ClientDataRequestType;

import java.nio.ByteBuffer;

/**
 * The implementation of ClientDataJson.
 */
@JNINamespace("content")
public class ClientDataJsonImpl {
    /** The implementation of {@link ClientDataJson#buildClientDataJson}. */
    @Nullable
    public static String buildClientDataJson(@ClientDataRequestType int clientDataRequestType,
            String callerOrigin, byte[] challenge, boolean isCrossOrigin,
            PaymentOptions paymentOptions, String relyingPartyId, String topOrigin) {
        return ClientDataJsonImplJni.get().buildClientDataJson(clientDataRequestType, callerOrigin,
                challenge, isCrossOrigin,
                paymentOptions == null ? null : paymentOptions.serialize(), relyingPartyId,
                topOrigin);
    }

    @NativeMethods
    public interface Natives {
        String buildClientDataJson(@ClientDataRequestType int clientDataRequestType,
                String callerOrigin, byte[] challenge, boolean isCrossOrigin,
                ByteBuffer optionsByteBuffer, String relyingPartyId, String topOrigin);
    }
}
