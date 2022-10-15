// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base;

/**
 *  Error when calling native methods.
 */
public class JniException extends RuntimeException {
    public JniException(String msg) {
        super(msg);
    }
}
