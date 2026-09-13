"""Excel Online UI bridge. No desktop Office, account password API or admin install.

Uses a separate browser profile for each Provi Tracker account. Never attaches to
or copies credentials from the user's ordinary browser. No mail is sent.
"""
import json
import re
import sys
import time
from pathlib import Path
from urllib.parse import urlparse

SCRIPT_NAME = "ProviTrackerSalesRegistration"


def valid_workbook_url(value):
    try:
        url = urlparse(value)
        return (url.scheme == "https" and (url.hostname or "").endswith(".sharepoint.com")
                and not url.username and not url.password and bool(url.path))
    except (TypeError, ValueError):
        return False


def parse_receipt(text, request_id, order_number, is_test=False):
    for line in text.splitlines():
        if "PROVITRACKER_RESULT:" not in line:
            continue
        raw = line.split("PROVITRACKER_RESULT:", 1)[1].strip()
        try:
            result = json.loads(raw)
        except (ValueError, TypeError):
            continue
        if not isinstance(result, dict):
            continue
        allowed = {"checked"} if is_test else {"registered", "already_registered"}
        if (result.get("success") is True and result.get("scriptVersion") == 2
                and result.get("requestId") == request_id and result.get("status") in allowed
                and result.get("orderNumber") == ("" if is_test else order_number)
                and (is_test or isinstance(result.get("row"), int) and result["row"] >= 3)):
            return result
    return None


def wait_control(page, role, name, timeout=60):
    """Resolve visible controls in frames without reading tokens or hidden state."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        for frame in page.frames:
            control = frame.get_by_role(role, name=name, exact=isinstance(name, str))
            if control.count() == 1 and control.is_visible():
                return control
        page.wait_for_timeout(300)
    raise RuntimeError("Excel viste ikke det forventede felt. Kontrollér Microsoft-login og Office Scripts.")


def script_source():
    if getattr(sys, "frozen", False):
        path = Path(sys._MEIPASS) / "sales_registration" / "excel_online_sales_registration.ts"
    else:
        path = Path(__file__).resolve().parents[1] / "scripts" / "excel_online_sales_registration.ts"
    return path.read_text(encoding="utf-8")


def open_automate(page):
    try:
        wait_control(page, "tab", re.compile(r"^(Automatiser|Automate)$"), 5).click()
    except RuntimeError:
        wait_control(page, "button", re.compile(r"^(Flere faner|More tabs)$"), 10).click()
        wait_control(page, "menuitemradio", re.compile(r"^(Automatiser|Automate)$"), 10).click()


def setup_script(page):
    # Setup only. An existing script is never silently overwritten.
    open_automate(page)
    try:
        wait_control(page, "radio", SCRIPT_NAME, 8).click()
        return
    except RuntimeError:
        pass
    wait_control(page, "button", re.compile(r"^(Nyt script|New Script|New script)$")).click()
    wait_control(page, "menuitem", re.compile(r"^(Opret i Kodeeditor|Create in Code Editor)$")).click()
    wait_control(page, "button", re.compile(r"^(Omdøb|Rename)$")).click()
    name = wait_control(page, "textbox", re.compile(r"^(Angiv scriptnavn|Enter script name)"))
    name.fill(SCRIPT_NAME)
    name.press("Enter")
    editor = wait_control(page, "textbox", "editor")
    editor.press("ControlOrMeta+a")
    # insert_text delivers one paste-like input without triggering per-character shortcuts.
    page.keyboard.insert_text(script_source())
    wait_control(page, "button", re.compile(r"^(Gem script|Save script)$")).click()


def run(payload, playwright):
    url = payload.get("workbookUrl", "")
    if not valid_workbook_url(url):
        raise ValueError("Indsæt et gyldigt https-link til dit masterark på SharePoint.")
    profile = Path(payload.get("profileDir", ""))
    if not profile.is_absolute():
        raise ValueError("Browserens profilmappe mangler.")
    registration = payload.get("registration", {})
    request_id = registration.get("requestId", "")
    if not isinstance(request_id, str) or not request_id:
        raise ValueError("Registreringen mangler kontrolnummer.")
    is_setup = payload.get("action") == "master-setup"
    if is_setup:
        registration = {"isTest": True, "requestId": request_id}
    profile.mkdir(parents=True, exist_ok=True)
    context = playwright.chromium.launch_persistent_context(str(profile), headless=not is_setup,
            locale="da-DK", viewport={"width":1440,"height":1000})
    try:
        page = context.pages[0] if context.pages else context.new_page()
        page.set_default_timeout(15000)
        page.goto(url, wait_until="domcontentloaded", timeout=60000)
        # Setup remains visible for the user to sign in; automatic runs stop on expired login.
        wait_control(page, "tab", re.compile(r"^Regneark Ark1$|^Sheet Ark1$"), 300 if is_setup else 45)
        if is_setup:
            setup_script(page)
        else:
            open_automate(page)
            wait_control(page, "radio", SCRIPT_NAME, 30).click()
        # The details view only reports "script ran" and hides the receipt.
        # Open the editor to make the structured Output log available.
        if not any(f.get_by_role("textbox", name="editor", exact=True).count() for f in page.frames):
            wait_control(page, "button", re.compile(r"^(Rediger|Edit)$"), 45).click()
        wait_control(page, "textbox", "editor", 45)
        wait_control(page, "button", re.compile(r"^(Kør|Run)$"), 45).click()
        param = wait_control(page, "textbox", re.compile(r"^(Angiv en streng|Enter a string)"), 45)
        param.fill(json.dumps(registration, ensure_ascii=False, separators=(",", ":")))
        # Parameter dialog is a separate iframe. Scope the Run button to that frame.
        for parameter_frame in page.frames:
            field = parameter_frame.get_by_role("textbox", name=re.compile(r"^(Angiv en streng|Enter a string)"))
            if field.count() == 1 and field.is_visible():
                parameter_frame.get_by_role("button", name=re.compile(r"^(Kør|Run)$")).press("Enter")
                break
        else:
            raise RuntimeError("Excels parameterdialog blev lukket før registreringen.")
        deadline = time.monotonic() + 90
        while time.monotonic() < deadline:
            for frame in page.frames:
                if not frame.name or "ProdCodeEditor.URL" not in frame.name:
                    continue
                for text in frame.get_by_role("listitem").all_text_contents():
                    receipt = parse_receipt(text, request_id, registration.get("orderNumber", ""), is_setup)
                    if receipt:
                        return receipt
                    if re.match(r"^(Fejl:|Error:)", text):
                        raise RuntimeError(text[:700])
            page.wait_for_timeout(500)
        raise RuntimeError("Excel bekræftede ikke registreringen. Salget afventer stadig; kontrollér masterarket og prøv igen.")

    finally:
        context.close()
