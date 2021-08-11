# Kiwi Browser Next

Source-code for Kiwi Next, a Kiwi Browser auto-rebased with latest Chromium.

This repository is the control center for development, testing and deployment of new releases.

If you are looking into modifying or improving Kiwi Browser, you are at the right place.

The mindset behind this project is that you should be able to modify and create a modification of Kiwi Browser directly from within Kiwi Browser (without the need of a computer).

Keep it in mind, if you need a computer to do a change, it means it's too complicated. Even rebasing should be doable from the GitHub user interface.

Servers are paid-for and sponsored by Geometry OU (the company behind Kiwi Browser), and additional computing hours for contributors are provided for free by Microsoft Azure via GitHub Actions.

## Code of conduct

Be kind.

## Building

Use workflow `"Any branch: Build apk"` to build Kiwi.

The generated APK will appear on the releases page of your project.

It will be a draft release, so only you can see it, but you can change that by editing the release.

An APK is automatically generated as well if you open a pull request to `kiwibrowser/src.next:kiwi`.

The APK that you will receive is named `Wiki Browser` (not `Kiwi Browser`).

This is intentional. Dev builds are called `Wiki Browser`, and become `Kiwi Browser` once they pass review.

## How to contribute

1) Press Fork on the top right.
2) In your fork, edit the file you want from the branch called `kiwi`.
3) In your fork, go to `Actions`, run the workflow `"Any branch: Build apk"` on the branch you want to test.
4) You will get an APK you can test on your phone.
5) If you are happy with the result, open a Pull request from your branch to branch `kiwibrowser/src.next:kiwi`.

To improve Kiwi, you don't need to download anything to your computer.
All you need is a browser, and you know which one is good to use ;)

## About workflows

This repository is managed using GitHub Actions workflows.

Workflows can be accessed in the `Actions` tab on top of the page.

In "kiwi" branch you can find the source-code, assets and tools that are used to build Kiwi Browser.

In "chromium" branch you can find a replica of the Chromium repository.

  - Workflows starting with `"Any branch:"` can be used on any branch of your choice.
  - Workflows starting with `"Kiwi:"` are meant to be used on the branch "kiwi".
  - Workflows starting with `"Chromium:"` are meant to be used on the branch "chromium".

To keep the size of the repository small, we replicate only the files that are changed in Kiwi Browser.

  - Use workflow `"Chromium: Import"` to add a new file to be replicated.
  - Use workflow `"Chromium: Update files from upstream"` to update the replica.

Except with the "Any branch" workflows, you don't need to pick the correct branch, the workflow does it for you.

## Improving Kiwi Browser

Kiwi Browser is technically a re-based fork of Chromium. This means that the code of Chromium is taken, and
modifications are applied on top. Once the modifications are live, this is Kiwi.

When we mean rebase, in the context of this project, we mean that we take a bunch of code, and we
apply modification on top.

The repository of Chromium is huge. You need 100 GB of disk space, and a lot of computing power.

If you want to make Kiwi better, stay in this repository, you need only the files from Kiwi.

It will make your life easier.

If you want to work on a full repository (e.g. to make a completely different fork of Kiwi or to experiment very
advanced changes, use http://github.com/kiwibrowser/src ) but this is going to be very difficult as you have to handle
all the Chromium version changes by hand.

## Importing new files from Chromium

Instead of importing 50 GB of files, we import only the files that we need from Chromium
or that we intend to modify.

This is done using the `"Chromium: Import file"` workflow.

```
[real chromium repository] -------> [kiwibrowser/src.next:chromium]
                 workflow "Chromium: Import file"
```

In the `chromium` branch, you have untouched, and unmodified files of Chromium.

Once you are done importing files from Chromium you need to adapt the code of Kiwi on top of the code in the Chromium `Kiwi: Rebase on top of Chromium branch`

```
                                    [kiwibrowser/src.next:kiwi]
                                               /|\
                                                |
                                                | workflow "Kiwi: Rebase on top of Chromium branch"
                                                |
[real chromium repository] -------> [kiwibrowser/src.next:chromium]
                 workflow "Chromium: Import file"
```

If you import an image (a reource in a `drawable` or a `mipmap` folder), for example `chrome/android/java/res/drawable-hdpi/btn_left.png` then the workflow will automatically import `chrome/android/java/res/drawable-*/btn_left.png` so you don't need to run the workflow for each resolution folder.

## Changing Chromium version (advanced)

To change to another Chromium version, use workflow `"Chromium: Update files from upstream"`.
The workflow is going to ask you to which version number you want to upgrade or downgrade.

Changing the Chromium version is going to work, but the build system doesn't support it yet, so you will not be able to get an APK if you change the version (for now).

## Resolving issues

If you need to add or remove commits where you have done mistake, `git rebase -i 3eb71bb7cae580107938f6394513462e67033f8a --committer-date-is-author-date`

`3eb71bb7cae580107938f6394513462e67033f8a` is the root commit from which both Kiwi and Chromium branch are born from.

## Updating your local repository

In order to keep the history clean, we are using rebase, and not merge, to fetch the latest modifications
you can use:

`git fetch`

`git pull --rebase`

In a normal situation, this is going to show `First, rewinding head to replay your work on top of it...`

## Accepting pull requests

When you accept a pull request:

 - If the source repository has very clean commits description, and you want the commits to appear as-they-are in the source repository, use `Rebase and merge`.

 - If the source repository has multiple commits for the fix or the commit names are not very explicit or messy, use `Squash and merge`.

## Navigating the source-code

Kiwi is based on Chromium, to find code, use https://source.chromium.org/chromium/chromium/src

The most commonly asked files are:

Android:
 - Main menu is in: `chrome/android/java/src/org/chromium/chrome/browser/app/appmenu`
 - Settings screen is in: `chrome/android/java/src/org/chromium/chrome/browser/settings` and `chrome/android/java/res/xml`

HTML:
 - kiwi://extensions is in: `chrome/browser/resources/extensions`
 - kiwi://chrome-search (new tab page) is in: `chrome/browser/resources/new_tab_page_instant`

Translations:
 - To add a new translation string in English: `chrome/android/java/res/values/strings.xml`
 - To translate strings in other languages, go to https://crowdin.com/project/kiwibrowser and once you have updated the translations, run workflow `"Kiwi: Download translations from Crowdin"` to download them back to the repository

## License

This repository is licensed under the same license as Chromium.
Chromium uses a very permissive and advantageous license. To keep this freedom we don't want to include code from much more restrictive licenses (for example GPL, or CC-NC).

Google has established a list of licenses that shouldn't be included in Chromium, we follow this list, and you can find the list below.

See `restricted` in https://opensource.google/docs/thirdparty/licenses/

Verify before proposing code to Kiwi Browser that the code doesn't come from such restricted projects.

## Code style

Follow the Chromium code style with one exception:

This may sound counter-intuitive, but as much as possible, do not delete code from Chromium and keep the original code of Chromium as much as possible even if it will become unused code.

For example, in the C++ code, if a segment of the code doesn't make sense on Android, wrap the whole block of code with `#if 0` and `#endif`.

In the Java code, add, if (false) in front of code segments that are unused.

If you rewrite a small function, no need to worry about this. You can overwrite it but try to stick as close as possible to the original.

The reason: We want a script to manages the rebase and upgrading the Chromium version, we do not want a human to fix it.

When Chromium refactors significant amount of code, they delete significant portions of code, or rewrite the inner-working of functions (e.g. extension APIs) and then we end up with a conflict.

If you keep all the code, and just put it in a `#if 0`, then the changes are done by Chromium team, but you don't have to worry about conflicts.

The code is not conceptually correct, but it's easier to maintain.

This also makes it easier for your fellow developers to spot where Kiwi specific modification exists.

Do not modify indentation of existing Chromium code. The code merger/rebaser cannot handle these well.
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

## Code style in the case of exceptions with a list that can grow in the future

If you write code that depends on a list of domains, or protocols, or IDs, or classes, and you think that someone else may want to add a domain to the list (for example here with `[kiwi://|chrome-search://|chrome://]`:
```
       if (tab != null && (tab.getUrl().getSpec().startsWith("kiwi://") || tab.getUrl().getSpec().startsWith("chrome-search://") || tab.getUrl().getSpec().startsWith("chrome://"))) {
          tab.getWebContents().evaluateJavaScript("(function() {" + ADAPT_TO_MOBILE_VIEWPORT + "})();", null);
       }
```

it's slightly more convenient if you write:
```
       if (tab != null && (tab.getUrl().getSpec().startsWith("kiwi://")
                       || tab.getUrl().getSpec().startsWith("chrome-search://"))
                       || tab.getUrl().getSpec().startsWith("chrome://"))) {
          tab.getWebContents().evaluateJavaScript("(function() {" + ADAPT_TO_MOBILE_VIEWPORT + "})();", null);
       }
```

This way, when adding a new domain, or a new exception in general, the developer only needs to copy-paste the line, and this is very convenient (especially for the contributors on mobile).

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

 1) Play Store builds (-playstore.apk)
These builds have been reviewed by a member of Google and by the Kiwi Browser project's lead.
Google is the company that ultimately signs and approves the builds.

 2) GitHub builds (-github.apk)
All the code in these builds has been reviewed by the Kiwi Browser project's lead.

 3) GitHub dev builds (.dev-github.apk)
All the code in these builds has been reviewed by a trusted Kiwi Browser contributor.

 4) Contributors builds (.unofficial-`<contributor>`.apk).
These builds contain code that has been added by contributors (or modders) working to do a modified version of Kiwi.
None of the code in these builds has been reviewed by anyone. Use at your own risk.

## Additional help

You can ask for extra help in our Discord server, or by [filing an issue](https://github.com/kiwibrowser/src.next/issues).

<a href="https://discord.gg/XyMppQq"> <img src="https://discordapp.com/assets/e4923594e694a21542a489471ecffa50.svg" height="50"></a>

Have fun with Kiwi!

Arnaud.
