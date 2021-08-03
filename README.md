# src.next

Source-code for Kiwi Next, a Kiwi Browser auto-rebased with latest Chromium.

In this repository is the control center for development, testing and deployment of new releases.

If you are looking into modifying or improving Kiwi Browser, you are at the right place.

The mindset behind this project is that you should be able to modify and create a modification of Kiwi Browser directly from within Kiwi Browser (without the need of a computer).

Keep it in mind, if you need a computer to do a change, it means it's too complicated. Even rebasing should be doable from the GitHub user interface.

Servers are paid-for and sponsored by Geometry OU (the company behind Kiwi Browser), and additional computing hours for contributors are provided for free by Microsoft Azure via GitHub Actions.

## Code of conduct

Be kind.

## Building

The reference build machine is using Ubuntu 20.04.2 with 64 cores.

## Navigating the source-code

Kiwi is based on Chromium, to find code, use https://source.chromium.org/chromium/chromium/src

The most important to know is:

Android:
 - Main menu is in: chrome/android/java/src/org/chromium/chrome/browser/app/appmenu
 - Settings screen is in: chrome/android/java/src/org/chromium/chrome/browser/settings and chrome/android/java/res/xml

HTML:
 - kiwi://extensions is in: chrome/browser/resources/extensions
 - kiwi://chrome-search (new tab page) is in: chrome/browser/resources/new_tab_page_instant

Translations:
 - To add a new translation string in English: chrome/android/java/res/values/strings.xml
 - To translate strings in other languages, go to https://crowdin.com/project/kiwibrowser and once you have updated the translations, run GitHub Action "Download translations from Crowdin" to download them back to the repository

## Code style

Follow the Chromium code style with one exception:

This may sound counter-intuitive, but as much as possible, do not delete code from Chromium and keep the original code of Chromium as much as possible even if it will become unused code.

For example, in the C++ code, if a segment of the code doesn't make sense on Android, wrap the whole block of code with #if 0 and #endif.

In the Java code, add, if (false) in front of code segments that are unused.

If you rewrite a small function, no need to worry about this. You can overwrite it but try to stick as close as possible to the original.

The reason: We want a script to manages the rebase and upgrading the Chromium version, we do not want a human to fix it.

When Chromium refactors significant amount of code, they delete significant portions of code, or rewrite the inner-working of functions (e.g. extension APIs) and then we end up with a conflict.

If you keep all the code, and just put it in a #if 0, then the changes are done by Chromium team, but you don't have to worry about conflicts.

The code is not conceptually correct, but it's easier to maintain.

This also makes it easier for your fellow developers to spot where Kiwi specific modification exists.

Do not modify indentation of existing Chromium code. The code merger/rebase cannot handle these well.
For example, if the original code is
```
  do_Chromium_Original_feature();
```

and you want to put this feature behind flag, it's ok to do:
```
  if (kiwi_setting_enabled)
  do_Kiwi();
  else
  do_Chromium_Original_feature();
```

Regarding imports:

If you have:
```
import org.chromium.base.CommandLine;
import org.chromium.base.IntentUtils;
import org.chromium.base.Log;
import org.chromium.base.PackageManagerUtils;
import org.chromium.base.PathUtils;
```

and need ContextUtils to be imported.
Do not do:
```
import org.chromium.base.CommandLine;
import org.chromium.base.ContextUtils;
import org.chromium.base.IntentUtils;
import org.chromium.base.Log;
import org.chromium.base.PackageManagerUtils;
import org.chromium.base.PathUtils;
```

instead append the newly added classes at the very end:
```
import org.chromium.base.CommandLine;
import org.chromium.base.IntentUtils;
import org.chromium.base.Log;
import org.chromium.base.PackageManagerUtils;
import org.chromium.base.PathUtils;

import org.chromium.base.ContextUtils;
```

This is because it's a script / robot taking care of the rebase, and if you put your new imports between other imports, this can easily confuse the script during the merge process when Chromium refactors code.

## Adding a new setting

In Kiwi it's much better to add Settings directly in the user interface, rather than add flags in kiwi://flags.
Users are often confused by flags. Prefer doing Settings.

To add a boolean setting in Kiwi is easy:

Step 1)
 - Go to the xml file for the screen (example: chrome/android/java/res/xml/accessibility_preferences.xml), add a new checkbox.

```
    <org.chromium.components.browser_ui.settings.ChromeBaseCheckBoxPreference
        android:key="enable_bottom_toolbar"
        android:summary="@string/accessibility_preferences_enable_bottom_toolbar_summary"
        android:title="@string/accessibility_preferences_enable_bottom_toolbar_title" />
```

android:key is the setting key, it's the identifier where the setting will be stored.
android:title is 3 or 4 words about the feature.
android:summary is an explanation of what the preference does (2nd line that appears in the UI).

Step 2)
 - Add a new english translation in chrome/android/java/res/values/strings.xml
```
    <string name="accessibility_preferences_enable_bottom_toolbar_title">Bottom toolbar</string>
    <string name="accessibility_preferences_enable_bottom_toolbar_summary">Move address bar to the bottom</string>
```

The format is `"<screen_where_the_setting_is_integrated>_<setting_key>_<summary/title>"`

Step 3)
 - Use the setting.

In any files where you want to use the setting, you need to import ContextUtils class:

```import org.chromium.base.ContextUtils;```

Very conveniently, this class works at any place, and any time in the Java code (and soon in the C++ code) so you don't need to worry
(note, if you are the maintainer of this class, you have to worry and make sure it works everywhere and faster, but consider the developer as your user and provide to the developer a functional and efficient interface)

To check if a setting is enabled or not:
```
if (ContextUtils.getAppSharedPreferences().getBoolean("enable_bottom_toolbar", false))
```
where "enable_bottom_toolbar" is your setting key, and "false" the default value to return when the setting is not set yet by the user.

This code, for example, corresponds to "if bottom toolbar is enabled".

## User scripts (script injection)

Kiwi Browser can execute scripts on page load, this is equivalent to user scripts. For example, you can remove AMP links, to redress (correct) some web pages, or to auto-accept cookie / GDPR windows, change the meta-theme of the current page, download a video, and so on.

```
chrome/android/java/src/org/chromium/chrome/browser/PersonalizeResults.java
```

In this file you have access to the current tab URL and can execute JavaScript in the webpage.

You can of course, use this script in conjunction 

## Code review and safety

There are 4 stages of Kiwi Browser builds review:

 - Play Store builds (-playstore.apk)
These builds have been reviewed by a member of Google and by the Kiwi Browser project's lead.
Google is the company that ultimately signs and approves the builds.

This is also the builds that we show to our partners (Microsoft, Yahoo, etc) for validation.

 - GitHub builds (-github.apk)
All the code in these builds has been reviewed by the Kiwi Browser project's lead.

 - GitHub dev builds (.dev-github.apk)
All the code in these builds has been reviewed by a trusted Kiwi Browser contributor.

 - Contributors builds (.unofficial-<contributor>.apk).
These builds contain code that has been added by contributors (or modders) working to do a modified version of Kiwi.
None of the code in these builds has been reviewed by anyone.

Use at your own risk.

## Additional help

You can ask for extra help in our Discord server, or by [filing an issue](https://github.com/kiwibrowser/src.next/issues).

<a href="https://discord.gg/XyMppQq"> <img src="https://discordapp.com/assets/e4923594e694a21542a489471ecffa50.svg" height="50"></a>

Have fun with Kiwi!

Arnaud.
