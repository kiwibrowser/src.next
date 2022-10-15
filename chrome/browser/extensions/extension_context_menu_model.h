// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTENSION_CONTEXT_MENU_MODEL_H_
#define CHROME_BROWSER_EXTENSIONS_EXTENSION_CONTEXT_MENU_MODEL_H_

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "ui/base/models/simple_menu_model.h"

class Browser;
class Profile;

namespace content {
class WebContents;
}

namespace extensions {
class ContextMenuMatcher;
class Extension;
class ExtensionAction;

// The context menu model for extension icons.
class ExtensionContextMenuModel : public ui::SimpleMenuModel,
                                  public ui::SimpleMenuModel::Delegate {
 public:
  enum MenuEntries {
    HOME_PAGE = 0,
    OPTIONS,
    TOGGLE_VISIBILITY,
    UNINSTALL,
    MANAGE_EXTENSIONS,
    INSPECT_POPUP,
    PAGE_ACCESS_CANT_ACCESS,
    PAGE_ACCESS_SUBMENU,
    PAGE_ACCESS_RUN_ON_CLICK,
    PAGE_ACCESS_RUN_ON_SITE,
    PAGE_ACCESS_RUN_ON_ALL_SITES,
    PAGE_ACCESS_LEARN_MORE,
    PAGE_ACCESS_ALL_EXTENSIONS_GRANTED,
    PAGE_ACCESS_ALL_EXTENSIONS_BLOCKED,
    PAGE_ACCESS_PERMISSIONS_PAGE,
    // NOTE: If you update this, you probably need to update the
    // ContextMenuAction enum below.
  };

  // A separate enum to indicate the action taken on the menu. We have two
  // enums (this and MenuEntries above) to avoid needing to have a single one
  // with both action-specific values (like kNoAction) and menu-specific values
  // (like PAGE_ACCESS_SUBMENU).
  // These values are persisted to logs. Entries should not be renumbered and
  // numeric values should never be reused. New values should be added before
  // kMaxValue.
  enum class ContextMenuAction {
    kNoAction = 0,
    kCustomCommand = 1,
    kHomePage = 2,
    kOptions = 3,
    kToggleVisibility = 4,
    kUninstall = 5,
    kManageExtensions = 6,
    kInspectPopup = 7,
    kPageAccessRunOnClick = 8,
    kPageAccessRunOnSite = 9,
    kPageAccessRunOnAllSites = 10,
    kPageAccessLearnMore = 11,
    kPageAccessPermissionsPage = 12,
    kMaxValue = kPageAccessPermissionsPage,
  };

  // Location where the context menu is open from.
  enum class ContextMenuSource { kToolbarAction = 0, kMenuItem = 1 };

  // The current visibility of the extension; this affects the "pin" / "unpin"
  // strings in the menu.
  // TODO(devlin): Rename this "PinState" when we finish removing the old UI
  // bits.
  enum ButtonVisibility {
    // The extension is pinned on the toolbar.
    PINNED,
    // The extension is temporarily visible on the toolbar, as for showing a
    // popup.
    TRANSITIVELY_VISIBLE,
    // The extension is not pinned (and is shown in the extensions menu).
    UNPINNED,
  };

  // Delegate to handle showing an ExtensionAction popup.
  class PopupDelegate {
   public:
    // Called when the user selects the menu item which requests that the
    // popup be shown and inspected.
    // The delegate should know which popup to display.
    virtual void InspectPopup() = 0;

   protected:
    virtual ~PopupDelegate() {}
  };

  // Creates a menu model for the given extension. If
  // prefs::kExtensionsUIDeveloperMode is enabled then a menu item
  // will be shown for "Inspect Popup" which, when selected, will cause
  // ShowPopupForDevToolsWindow() to be called on |delegate|.
  ExtensionContextMenuModel(const Extension* extension,
                            Browser* browser,
                            ButtonVisibility visibility,
                            PopupDelegate* delegate,
                            bool can_show_icon_in_toolbar,
                            ContextMenuSource source);

  ExtensionContextMenuModel(const ExtensionContextMenuModel&) = delete;
  ExtensionContextMenuModel& operator=(const ExtensionContextMenuModel&) =
      delete;

  ~ExtensionContextMenuModel() override;

  // SimpleMenuModel::Delegate:
  bool IsCommandIdChecked(int command_id) const override;
  bool IsCommandIdVisible(int command_id) const override;
  bool IsCommandIdEnabled(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;
  void OnMenuWillShow(ui::SimpleMenuModel* source) override;
  void MenuClosed(ui::SimpleMenuModel* source) override;

  ui::SimpleMenuModel* page_access_submenu_for_testing() {
    return page_access_submenu_.get();
  }

 private:
  void InitMenu(const Extension* extension, bool can_show_icon_in_toolbar);

  // Adds the page access items based on the current site setting pointed by
  // `web_contents`.
  void CreatePageAccessItems(const Extension* extension,
                             content::WebContents* web_contents);

  // Returns true if the given page access command is enabled in the menu.
  bool IsPageAccessCommandEnabled(const Extension& extension,
                                  int command_id) const;

  void HandlePageAccessCommand(int command_id,
                               const Extension* extension) const;

  // Gets the extension we are displaying the menu for. Returns NULL if the
  // extension has been uninstalled and no longer exists.
  const Extension* GetExtension() const;

  // Returns the active web contents.
  content::WebContents* GetActiveWebContents() const;

  // Appends the extension's context menu items.
  void AppendExtensionItems();

  // A copy of the extension's id.
  std::string extension_id_;

  // Whether the menu is for a component extension.
  bool is_component_;

  // The extension action of the extension we are displaying the menu for (if
  // it has one, otherwise NULL).
  raw_ptr<ExtensionAction> extension_action_;

  const raw_ptr<Browser> browser_;

  raw_ptr<Profile> profile_;

  // The delegate which handles the 'inspect popup' menu command (or NULL).
  raw_ptr<PopupDelegate> delegate_;

  // The visibility of the button at the time the menu opened.
  ButtonVisibility button_visibility_;

  // Menu matcher for context menu items specified by the extension.
  std::unique_ptr<ContextMenuMatcher> extension_items_;

  std::unique_ptr<ui::SimpleMenuModel> page_access_submenu_;

  // The action taken by the menu. Has a valid value when the menu is being
  // shown.
  absl::optional<ContextMenuAction> action_taken_;

  ContextMenuSource source_;
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_EXTENSION_CONTEXT_MENU_MODEL_H_
