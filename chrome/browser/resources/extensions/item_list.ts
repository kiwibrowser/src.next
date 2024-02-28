// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'chrome://resources/cr_components/managed_footnote/managed_footnote.js';
import './item.js';
import './shared_style.css.js';
import './review_panel.js';

import {I18nMixin} from 'chrome://resources/cr_elements/i18n_mixin.js';
import {loadTimeData} from 'chrome://resources/js/load_time_data.js';
import {IronA11yAnnouncer} from 'chrome://resources/polymer/v3_0/iron-a11y-announcer/iron-a11y-announcer.js';
import {PolymerElement} from 'chrome://resources/polymer/v3_0/polymer/polymer_bundled.min.js';

import type {ExtensionsItemElement, ItemDelegate} from './item.js';
import {getTemplate} from './item_list.html.js';

type Filter = (info: chrome.developerPrivate.ExtensionInfo) => boolean;

const ExtensionsItemListElementBase = I18nMixin(PolymerElement);

export class ExtensionsItemListElement extends ExtensionsItemListElementBase {
  static get is() {
    return 'extensions-item-list';
  }

  static get template() {
    return getTemplate();
  }

  static get properties() {
    return {
      apps: Array,
      extensions: Array,
      delegate: Object,

      inDevMode: {
        type: Boolean,
        value: false,
      },

      filter: {
        type: String,
      },

      computedFilter_: {
        type: String,
        computed: 'computeFilter_(filter)',
        observer: 'announceSearchResults_',
      },

      maxColumns_: {
        type: Number,
        value: 3,
      },

      shownAppsCount_: {
        type: Number,
        value: 0,
      },

      shownExtensionsCount_: {
        type: Number,
        value: 0,
      },

      showSafetyCheckReviewPanel_: {
        type: Boolean,
        value: () => loadTimeData.getBoolean('safetyCheckShowReviewPanel') ||
            loadTimeData.getBoolean('safetyHubShowReviewPanel'),
      },

      hasSafetyCheckTriggeringExtension_: {
        type: Boolean,
        computed: 'computeHasSafetyCheckTriggeringExtension_(extensions)',
      },
    };
  }

  apps: chrome.developerPrivate.ExtensionInfo[];
  extensions: chrome.developerPrivate.ExtensionInfo[];
  delegate: ItemDelegate;
  inDevMode: boolean;
  filter: string;
  private computedFilter_: string;
  private maxColumns_: number;
  private shownAppsCount_: number;
  private shownExtensionsCount_: number;
  private showSafetyCheckReviewPanel_: boolean;
  private hasSafetyCheckTriggeringExtension_: boolean;

  getDetailsButton(id: string): HTMLElement|null {
    const item =
        this.shadowRoot!.querySelector<ExtensionsItemElement>(`#${id}`);
    return item && item.getDetailsButton();
  }

  getRemoveButton(id: string): HTMLElement|null {
    const item =
        this.shadowRoot!.querySelector<ExtensionsItemElement>(`#${id}`);
    return item && item.getRemoveButton();
  }

  getErrorsButton(id: string): HTMLElement|null {
    const item =
        this.shadowRoot!.querySelector<ExtensionsItemElement>(`#${id}`);
    return item && item.getErrorsButton();
  }

  /**
   * Focus the remove button for the item matching `id`. If the remove button is
   * not visible, focus the details button instead.
   * return: If an item's button has been focused, see comment below.
   */
  focusItemButton(id: string): boolean {
    const item =
        this.shadowRoot!.querySelector<ExtensionsItemElement>(`#${id}`);
    // This function is called from a setTimeout() inside manager.ts. Rarely,
    // the list of extensions rendered in this element may not match the list of
    // extensions stored in manager.ts for a brief moment (not visible to the
    // user). As a result, `item` here may be null even though `id` points to
    // an extension inside `manager.ts`. If this happens, do not focus anything.
    // Observed in crbug.com/1482580.
    if (!item) {
      return false;
    }

    const buttonToFocus = item.getRemoveButton() || item.getDetailsButton();
    buttonToFocus!.focus();
    return true;
  }

  /**
   * Computes the filter function to be used for determining which items
   * should be shown. A |null| value indicates that everything should be
   * shown.
   * return {?Function}
   */
  private computeFilter_(): Filter|null {
    const formattedFilter = this.filter.trim().toLowerCase();
    if (!formattedFilter) {
      return null;
    }

    return i => [i.name, i.id].some(
               s => s.toLowerCase().includes(formattedFilter));
  }
  private computeShowSafetyCheckReviewPanel_(): boolean {
    return (
        loadTimeData.getBoolean('safetyCheckShowReviewPanel') ||
        loadTimeData.getBoolean('safetyHubShowReviewPanel'));
  }

  private computeHasSafetyCheckTriggeringExtension_(): boolean {
    if (!this.extensions) {
      return false;
    }
    for (const extension of this.extensions) {
      if (!!extension.safetyCheckText &&
          !!extension.safetyCheckText.panelString &&
          this.showSafetyCheckReviewPanel_) {
        return true;
      }
    }
    return false;
  }

  private shouldShowEmptyItemsMessage_() {
    if (!this.apps || !this.extensions) {
      return;
    }

    return this.apps.length === 0 && this.extensions.length === 0;
  }

  private shouldShowEmptySearchMessage_() {
    return !this.shouldShowEmptyItemsMessage_() && this.shownAppsCount_ === 0 &&
        this.shownExtensionsCount_ === 0;
  }

  private onNoExtensionsClick_(e: Event) {
    if ((e.target as HTMLElement).tagName === 'A') {
      chrome.metricsPrivate.recordUserAction('Options_GetMoreExtensions');
    }
  }

  private announceSearchResults_() {
    if (this.computedFilter_) {
      IronA11yAnnouncer.requestAvailability();
      setTimeout(() => {  // Async to allow list to update.
        const total = this.shownAppsCount_ + this.shownExtensionsCount_;
        this.dispatchEvent(new CustomEvent('iron-announce', {
          bubbles: true,
          composed: true,
          detail: {
            text: this.shouldShowEmptySearchMessage_() ?
                this.i18n('noSearchResults') :
                (total === 1 ?
                     this.i18n('searchResultsSingular', this.filter) :
                     this.i18n(
                         'searchResultsPlural', total.toString(), this.filter)),
          },
        }));
      }, 0);
    }
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'extensions-item-list': ExtensionsItemListElement;
  }
}

customElements.define(ExtensionsItemListElement.is, ExtensionsItemListElement);
