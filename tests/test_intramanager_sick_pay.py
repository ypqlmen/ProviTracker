import sys
import types
import unittest


if "playwright.sync_api" not in sys.modules:
    playwright = types.ModuleType("playwright")
    sync_api = types.ModuleType("playwright.sync_api")
    sync_api.sync_playwright = lambda: None
    sync_api.TimeoutError = TimeoutError
    playwright.sync_api = sync_api
    sys.modules["playwright"] = playwright
    sys.modules["playwright.sync_api"] = sync_api


from intramanager_worker.intramanager_sync import (  # noqa: E402
    is_absence_pay_project,
    payroll_money_from_cells,
)


class IntramanagerSickPayTests(unittest.TestCase):
    def test_sick_pay_is_half_of_intramanager_pay_columns(self):
        cells = [
            "Dato",
            "Projekt",
            "Note",
            "Tidsrum",
            "Timer",
            "720,37",
            "0,00",
            "449,47",
        ]

        self.assertEqual(payroll_money_from_cells(cells), 584.92)

    def test_danish_absence_names_are_recognized(self):
        self.assertTrue(is_absence_pay_project("Sygel" + chr(0x00F8) + "n"))
        self.assertTrue(is_absence_pay_project("Frav" + chr(0x00E6) + "r"))
        self.assertTrue(is_absence_pay_project("Garantil" + chr(0x00F8) + "n"))
        self.assertFalse(is_absence_pay_project("Telenor Erhverv"))


if __name__ == "__main__":
    unittest.main()
