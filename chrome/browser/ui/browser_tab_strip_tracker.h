// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_BROWSER_TAB_STRIP_TRACKER_H_
#define CHROME_BROWSER_UI_BROWSER_TAB_STRIP_TRACKER_H_

#include "chrome/browser/ui/browser_list_observer.h"

class BrowserTabStripTrackerDelegate;
class TabStripModelObserver;

// BrowserTabStripTracker attaches a TabStripModelObserver to a subset of
// pre-existing and future Browsers. The subset of Browsers that are tracked is
// determined by an optional BrowserTabStripTrackerDelegate.
class BrowserTabStripTracker : public BrowserListObserver {
 public:
  // |tab_strip_model_observer| is a non-nullptr TabStripModelObserver
  // registered on tracked Browsers. |delegate| determines which Browsers are
  // tracked. If nullptr, all Browsers are tracked.
  BrowserTabStripTracker(TabStripModelObserver* tab_strip_model_observer,
                         BrowserTabStripTrackerDelegate* delegate);

  BrowserTabStripTracker(const BrowserTabStripTracker&) = delete;
  BrowserTabStripTracker& operator=(const BrowserTabStripTracker&) = delete;

  ~BrowserTabStripTracker() override;

  // Registers the TabStripModelObserver on existing tracked Browsers and starts
  // observing Browser creation to register the TabStripModelObserver on future
  // tracked Browsers. When the TabStripModelObserver is registered on an
  // existing of future Browser, OnTabStripModelChanged() is invoked to indicate
  // the initial state of the Browser. If a delegate needs to differentiate
  // between Browsers observed by way of Init() vs. a Browser added after the
  // fact use is_processing_initial_browsers().
  void Init();

  // Returns true if processing an existing Browser in Init().
  bool is_processing_initial_browsers() const {
    return is_processing_initial_browsers_;
  }

 private:
  // Returns true if a TabStripModelObserver should be added to |browser|.
  bool ShouldTrackBrowser(Browser* browser);

  // If ShouldTrackBrowser() returns true for |browser| then a
  // TabStripModelObserver is attached.
  void MaybeTrackBrowser(Browser* browser);

  // BrowserListObserver:
  void OnBrowserAdded(Browser* browser) override;
  void OnBrowserRemoved(Browser* browser) override;

  TabStripModelObserver* const tab_strip_model_observer_;
  BrowserTabStripTrackerDelegate* const delegate_;
  bool is_processing_initial_browsers_;
};

#endif  // CHROME_BROWSER_UI_BROWSER_TAB_STRIP_TRACKER_H_
