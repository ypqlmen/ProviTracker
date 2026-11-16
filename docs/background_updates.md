Provi Tracker on Windows checks at startup and every 15 minutes while open. It downloads in the background and verifies the released archive's size and SHA-256 before unpacking. The sidebar shows when an update is ready.

Installation waits until the program has been in the background for at least two minutes and there are no open dialogs, form drafts, running workers or outstanding cloud saves. The conditions are checked again when the installer is ready. The app accepts a normal close only after it approves the handoff. The helper waits for that app process to exit, never forces another instance to close, and starts the installed version minimized. If installation fails, the helper keeps its evidence and tries to reopen the previous executable.

The last production version needs to be upgraded once before it can use periodic checks. The change does not alter the cloud save format or the update feed during trial builds.
