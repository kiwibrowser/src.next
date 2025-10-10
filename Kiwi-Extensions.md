# How Kiwi Browser Handles Chrome Extensions: A Code-Based Explanation

Kiwi Browser is built on the Chromium open-source project, giving it full compatibility with the Chrome extension ecosystem. While it uses the same core framework as Chromium to install and run extensions, Kiwi introduces a layer of customization to provide a unique, branded experience. This document explains this two-part system, based on an analysis of the Kiwi source code.

## Part 1: The Chromium Foundation

At its core, Kiwi relies on the standard, battle-tested mechanisms from Chromium to handle extensions.

-   **Installation (`sandboxed_unpacker.cc`)**: When you install an extension from the Chrome Web Store, it arrives as a `.crx` file. The logic in `extensions/browser/sandboxed_unpacker.cc` is responsible for securely unpacking this file. This process includes verifying the extension's digital signature, unzipping its contents, and sanitizing its components to prevent security issues.

-   **Resource Loading (`extension_protocols.cc`)**: Once installed, an extension's files (like its pop-up HTML, background scripts, and icons) are served from your local disk. The code in `extensions/browser/extension_protocols.cc` implements the `chrome-extension://` protocol handler, which is responsible for intercepting these requests and loading the correct files from the extension's directory.

## Part 2: Kiwi's Customizations

Kiwi's primary modifications are designed to rebrand the user-facing elements of the browser, including the URLs associated with extensions. This is achieved through a consistent pattern of URL scheme replacement and internal registration.

-   **URL Rebranding in the Omnibox**:
    To present a branded experience, Kiwi replaces the standard `chrome://` and `chrome-extension://` schemes with `kiwi://` and `kiwi-extension://` in the user interface. This logic is visible in `chrome/browser/ui/android/omnibox/java/src/org/chromium/chrome/browser/omnibox/suggestions/AutocompleteMediator.java`. When a URL is about to be displayed in the omnibox, the code swaps the schemes. Conversely, when the user navigates to a `kiwi-` prefixed URL, it is translated back to the standard `chrome-` scheme for the underlying engine to process.

-   **Internal Scheme Registration**:
    For the custom `kiwi://` schemes to function correctly, they must be recognized by the browser's core utilities.
    -   In `components/embedder_support/android/java/src/org/chromium/components/embedder_support/util/UrlUtilities.java`, the `kiwi` and `kiwi-search` schemes are added to the `INTERNAL_SCHEMES` set. This ensures that these URLs are treated as internal browser pages, similar to `chrome://settings`.
    -   At the native C++ level, `chrome/browser/autocomplete/chrome_autocomplete_scheme_classifier.cc` is modified to classify `kiwi` as a URL. This is a critical change that tells the omnibox to treat `kiwi://` addresses as navigable URLs rather than search queries.

### Summary

In essence, Kiwi Browser provides the full functionality of Chrome extensions by using the unmodified Chromium core for installation and resource handling. On top of this foundation, it adds a branding layer by systematically replacing standard URL schemes with Kiwi-specific ones, ensuring these custom schemes are recognized throughout the Java and native codebases. This approach allows Kiwi to offer a distinct user experience while maintaining full compatibility with the rich ecosystem of Chrome extensions.