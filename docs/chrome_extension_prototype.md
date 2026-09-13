# Chrome prototype 0.1

This is a read-only transport prototype, not the completed sale-registration replacement.
The existing order-to-Excel worker remains unchanged. The new Chrome check never marks
an order registered and never submits customer data.

## Implemented

- Settings has Install Chrome extension and Check Chrome (prototype).
- Installation opens Chrome Web Store in Chrome when a real published store ID is configured.
  Without one, it explains that the extension is not yet published; no invented URL is opened.
- Check Chrome registers a native messaging host under HKCU (no elevation) using the
  already packaged worker, then requests a read-only check of the specified workbook.
- Manifest V3 extension only has access to the company's SharePoint hostname. It reads
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

## Before employee installation

The owner needs a Chrome Web Store developer account. Account terms and any registration
payment must be completed by the owner. Upload a reviewed package, obtain the actual ID
and public key, replace the development manifest key, and rebuild the host and application
with -DPROVI_CHROME_STORE_ID=<actual published ID>. Verify the ID derived from the manifest
key equals the store ID. Test install, connection, uninstall and reinstall without elevation.
No store listing has been created or submitted yet.

## Still required

Implement and verify script invocation in the authenticated background tab, exact workbook
and frame targeting, fresh Excel receipts, queued sale delivery, expiry/account changes,
retry/conflict handling, and recovery from Chrome sleep/restarts. Only then switch the
order queue away from its existing separate-browser worker. Prepare store screenshots,
listing and privacy policy for the final feature before publication.

References:
- https://developer.chrome.com/docs/extensions/how-to/distribute/install-extensions
- https://developer.chrome.com/docs/extensions/develop/concepts/native-messaging
- https://developer.chrome.com/docs/webstore/register
