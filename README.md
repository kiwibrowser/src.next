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

Android:
 - Main menu is in: chrome/android/java/src/org/chromium/chrome/browser/app/appmenu
 - Settings screen is in: chrome/android/java/src/org/chromium/chrome/browser/settings and chrome/android/java/res/xml

HTML:
 - kiwi://extensions is in: chrome/browser/resources/extensions
 - kiwi://chrome-search (new tab page) is in: chrome/browser/resources/new_tab_page_instant

Translations:
 - To add a new translation string in English: chrome/android/java/res/values/strings.xml
 - To translate strings in other languages, go to https://crowdin.com/project/kiwibrowser and once you have updated the translations, run GitHub Action "Download translations from Crowdin" to download them back to the repository

## Adding a new setting

chrome/android/java/res/xml/accessibility_preferences.xml
chrome/android/java/src/org/chromium/chrome/browser/PersonalizeResults.java
chrome/android/java/src/org/chromium/chrome/browser/accessibility/settings/AccessibilitySettings.java

## Additional help

You can ask for extra help in our Discord server, or by [filing an issue](https://github.com/kiwibrowser/src.next/issues).

<a href="https://discord.gg/XyMppQq"> <img src="https://discordapp.com/assets/e4923594e694a21542a489471ecffa50.svg" height="50"></a>

Have fun with Kiwi!

Arnaud.
