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
    def test_sick_pay_includes_all_intramanager_pay_columns(self):
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

        self.assertEqual(payroll_money_from_cells(cells), 1169.84)

    def test_danish_absence_names_are_recognized(self):
        self.assertTrue(is_absence_pay_project("Sygel" + chr(0x00F8) + "n"))
        self.assertTrue(is_absence_pay_project("Frav" + chr(0x00E6) + "r"))
        self.assertTrue(is_absence_pay_project("Garantil" + chr(0x00F8) + "n"))
        self.assertFalse(is_absence_pay_project("Telenor Erhverv"))

class HistoryTableTests(unittest.TestCase):
    def test_full_sick_pay_and_signed_cancellations(self):
        from intramanager_worker.intramanager_sync import payroll_summary_from_result_rows
        rows=[['Telenor Erhverv','dato','7 t. 30 min.\n6 t. 00 min.','1','1','771,83','50,00','0,00'],
              ['Sygdom Fuldtid - 50%','dato','4 t. 00 min.','0','0','411,64','0,00','388,80'],
              ['Annullering - Beløb','dato','0 t. 00 min.','0','0','0,00','0,00','-380,40']]
        class Row:
            def __init__(self,data):self.data=data
            def locator(self,selector):return self
            def all_inner_texts(self):return self.data
        class Table:
            def count(self):return len(rows)
            def nth(self,i):return Row(rows[i])
        class Page:
            def locator(self,selector):return Table()
        result=payroll_summary_from_result_rows(Page())
        self.assertEqual(result['hours'],7.5)
        self.assertEqual(result['phoneHours'],6)
        self.assertEqual(result['sickHours'],4)
        self.assertEqual(result['sickPay'],800.44)
        self.assertEqual(result['cancellations'],-380.40)
        self.assertEqual(result['payrollSchema'],2)


if __name__ == "__main__":
    unittest.main()
