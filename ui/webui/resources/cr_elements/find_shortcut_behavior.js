// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {assert, assertNotReached} from '../js/assert.m.js';
import {isMac} from '../js/cr.m.js';
import {KeyboardShortcutList} from '../js/cr/ui/keyboard_shortcut_list.m.js';
import {isTextInputElement} from '../js/util.m.js';

/**
 * @fileoverview Listens for a find keyboard shortcut (i.e. Ctrl/Cmd+f or /)
 * and keeps track of an stack of potential listeners. Only the listener at the
 * top of the stack will be notified that a find shortcut has been invoked.
 * NOTE: This file is deprecated in favor of find_shortcut_mixin.ts. Don't use
 * it in new code.
 */

export const FindShortcutManager = (() => {
  /**
   * Stack of listeners. Only the top listener will handle the shortcut.
   * @type {!Array}
   */
  const listeners = [];

  /**
   * Tracks if any modal context is open in settings. This assumes only one
   * modal can be open at a time. The modals that are being tracked include
   * cr-dialog and cr-drawer.
   * @type {boolean}
   */
  let modalContextOpen = false;

  const shortcutCtrlF = new KeyboardShortcutList(isMac ? 'meta|f' : 'ctrl|f');
  const shortcutSlash = new KeyboardShortcutList('/');

  window.addEventListener('keydown', e => {
    if (e.defaultPrevented || listeners.length === 0) {
      return;
    }

    if (!shortcutCtrlF.matchesEvent(e) &&
        (isTextInputElement(/** @type {!Element} */ (e.composedPath()[0])) ||
         !shortcutSlash.matchesEvent(e))) {
      return;
    }

    const focusIndex =
        listeners.findIndex(listener => listener.searchInputHasFocus());
    // If no listener has focus or the first (outer-most) listener has focus,
    // try the last (inner-most) listener.
    // If a listener has a search input with focus, the next listener that
    // should be called is the right before it in |listeners| such that the
    // goes from inner-most to outer-most.
    const index = focusIndex <= 0 ? listeners.length - 1 : focusIndex - 1;
    if (listeners[index].handleFindShortcut(modalContextOpen)) {
      e.preventDefault();
    }
  });

  window.addEventListener('cr-dialog-open', () => {
    modalContextOpen = true;
  });

  window.addEventListener('cr-drawer-opened', () => {
    modalContextOpen = true;
  });

  window.addEventListener('close', e => {
    if (['CR-DIALOG', 'CR-DRAWER'].includes(e.composedPath()[0].nodeName)) {
      modalContextOpen = false;
    }
  });

  return Object.freeze({listeners: listeners});
})();

/**
 * Used to determine how to handle find shortcut invocations.
 * @polymerBehavior
 */
export const FindShortcutBehavior = {
  /**
   * @type {boolean}
   * @protected
   */
  findShortcutListenOnAttach: true,

  attached() {
    if (this.findShortcutListenOnAttach) {
      this.becomeActiveFindShortcutListener();
    }
  },

  detached() {
    if (this.findShortcutListenOnAttach) {
      this.removeSelfAsFindShortcutListener();
    }
  },

  becomeActiveFindShortcutListener() {
    const listeners = FindShortcutManager.listeners;
    assert(!listeners.includes(this), 'Already listening for find shortcuts.');
    listeners.push(this);
  },

  /**
   * If handled, return true.
   * @param {boolean} modalContextOpen
   * @return {boolean}
   */
  handleFindShortcut(modalContextOpen) {
    assertNotReached();
  },

  removeSelfAsFindShortcutListener() {
    const listeners = FindShortcutManager.listeners;
    const index = listeners.indexOf(this);
    assert(listeners.includes(this), 'Find shortcut listener not found.');
    listeners.splice(index, 1);
  },

  /** @return {boolean} */
  searchInputHasFocus() {
    assertNotReached();
  },
};

/** @interface */
export class FindShortcutBehaviorInterface {
  constructor() {
    /**
     * @type {boolean}
     * @protected
     */
    this.findShortcutListenOnAttach = true;
  }

  becomeActiveFindShortcutListener() {}

  /**
   * If handled, return true.
   * @param {boolean} modalContextOpen
   * @return {boolean}
   */
  handleFindShortcut(modalContextOpen) {}

  removeSelfAsFindShortcutListener() {}

  /** @return {boolean} */
  searchInputHasFocus() {}
}
