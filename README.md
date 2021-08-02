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

## Adding a new setting

chrome/android/java/res/xml/accessibility_preferences.xml
chrome/android/java/src/org/chromium/chrome/browser/PersonalizeResults.java
chrome/android/java/src/org/chromium/chrome/browser/accessibility/settings/AccessibilitySettings.java

## Additional help

You can ask for extra help in our Discord server, or by [filing an issue](https://github.com/kiwibrowser/src.next/issues).

<a href="https://discord.gg/XyMppQq"> <img src="https://discordapp.com/assets/e4923594e694a21542a489471ecffa50.svg" height="50"></a>

Have fun with Kiwi!

Arnaud.
