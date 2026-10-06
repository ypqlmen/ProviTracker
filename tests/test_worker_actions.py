import io
import sys
import types
import unittest
from unittest.mock import patch

if "playwright.sync_api" not in sys.modules:
    playwright = types.ModuleType("playwright")
    sync_api = types.ModuleType("playwright.sync_api")
    sync_api.sync_playwright = lambda: None
    sync_api.TimeoutError = TimeoutError
    playwright.sync_api = sync_api
    sys.modules["playwright"] = playwright
    sys.modules["playwright.sync_api"] = sync_api

from intramanager_worker import intramanager_sync as worker


class WorkerActionTests(unittest.TestCase):
    def test_removed_action_rejected_before_browser_launch_for_cli_and_json(self):
        for argv, stdin in [
            (["worker", "--action", "kvikoc-lookup"], ""),
            (["worker", "--stdin-json"], '{"action":"kvikoc-lookup","username":"fixture","password":"fixture"}'),
            (["worker", "--stdin-json"], '{"action":"unknown","username":"fixture","password":"fixture"}'),
        ]:
            with self.subTest(argv=argv, stdin=stdin):
                with patch.object(sys, "argv", argv), patch.object(sys, "stdin", io.StringIO(stdin)), patch.object(sys, "stderr", io.StringIO()), patch.object(worker, "sync_playwright") as browser:
                    with self.assertRaises(SystemExit) as error:
                        worker.main()
                    self.assertEqual(error.exception.code, 2)
                    browser.assert_not_called()


if __name__ == "__main__":
    unittest.main()
