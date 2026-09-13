# Chrome prototype 0.1.1

This is a read-only transport prototype, not the completed sale-registration replacement.
The existing order-to-Excel worker remains unchanged. The new Chrome check never marks
an order registered and never submits customer data.

## Implemented

- Settings has Install Chrome extension and Check Chrome (prototype).
- Installation prepares bundled files in %LOCALAPPDATA%/ProviTrackerChromeBridge/extension,
  registers the native host under HKCU and shows an in-app guide with Copy folder path
  and Open Chrome buttons. Users enable Developer mode and Load unpacked themselves.
  Repeated preparation refreshes the same directory; users reload the extension afterwards.
- Check Chrome registers a native messaging host under HKCU (no elevation) using the
  already packaged worker, then requests a read-only check of the specified workbook.
- Manifest V3 extension only has access to the company's SharePoint hostname and the observed euc-excel.officeapps.live.com Excel frame. It reads
  the workbook identity from the tab URL and checks for the Ark1 sheet tab, without
  reading cookies, cell values, passwords or page tokens.
- Exactly one matching, non-discarded workbook tab is required. Tabs are never activated.
- Correlation IDs, fixed local file locations, bounded native frames and an exact allowed
  extension origin prevent stale responses and arbitrary paths from being accepted.

## Developer check

Use a Windows trial build containing this source. For developer testing only, load the
chrome_extension directory unpacked in Chrome. This is not the employee installation flow.
Open the workbook, then select Check Chrome in Provi Tracker. Click the extension icon
if Chrome has not reconnected after first-time host registration. Repeat while a different
tab is foreground. A successful connection check is NOT proof of background Excel writes.

## Installation without a store account

The selected distribution is local unpacked installation. No store account, payment,
store ID or Web Store listing is required. Chrome's enterprise policy must permit
Developer mode and unpacked extensions. The app does not alter those policies or
Chrome's Developer mode setting. Users perform the final Chrome steps themselves.

The stable bundled public key keeps the unpacked extension ID and native host allowlist
aligned. The native host is registered when preparing the extension, before Chrome loads it.
The Windows workflow tests preparation using the actually installed executable, HKCU host
registration and repeated preparation to the same location. This does not simulate Chrome
installation or prove authenticated background Excel execution.

## Still required

Implement and verify script invocation in the authenticated background tab, exact workbook
and frame targeting, fresh Excel receipts, queued sale delivery, expiry/account changes,
retry/conflict handling, and recovery from Chrome sleep/restarts. Only then switch the
order queue away from its existing separate-browser worker. Verify the guided unpacked installation with workplace Chrome policies before distribution.

References:
- https://developer.chrome.com/docs/extensions/how-to/distribute/install-extensions
- https://developer.chrome.com/docs/extensions/develop/concepts/native-messaging
- https://developer.chrome.com/docs/webstore/register

## Connection fix 0.1.1

Excel's observed WacFrame_Excel_0 has src=about:blank, but its loaded document is
https://euc-excel.officeapps.live.com/x/_layouts/xlviewerinternal.aspx. Explicitly match
that Excel origin as well as SharePoint, enable match_about_blank and
inspect same-origin child documents recursively, since rendering may occur after script
injection. Retry probes for up to 22 seconds; distinguish no extension reply from a
responding document without Ark1. Revalidate current tab identity before success.
Regression tests cover blank-frame detection, delayed rendering, stale replies and navigation.

Settings buttons now occupy separate full-width rows; status text spans the card.
A local Qt/Fusion rendering at 480px reproduced the previous clipped button labels and
verified the revised layout. This is a local rendering, not a screenshot of the user's Windows session.
