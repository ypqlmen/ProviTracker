import argparse
import json
import os
import re
import sys
import time
from pathlib import Path

BASE_URL = "https://5r.intramanager.com/"
LOGIN_URL = BASE_URL + "reports/history/"
HISTORY_URL = BASE_URL + "reports/history/"
PUNCH_URL = BASE_URL + "reports/punch-in/"
DEBUG_ENABLED = False
OFFICE_ONLY_MESSAGE = "Man kan kun stemple ind eller ud på kontorets internet."
INTRAMANAGER_SICK_PAY_FACTOR = 1.0


def configure_playwright_browser_path():
    if os.environ.get("PLAYWRIGHT_BROWSERS_PATH"):
        return

    candidates = []

    if getattr(sys, "frozen", False):
        exe_dir = Path(sys.executable).resolve().parent
        candidates.append(exe_dir / "b")
        candidates.append(exe_dir / "pw-browsers")

        meipass = getattr(sys, "_MEIPASS", "")
        if meipass:
            candidates.append(Path(meipass).resolve().parent / "b")
            candidates.append(Path(meipass).resolve().parent / "pw-browsers")

    candidates.append(Path(__file__).resolve().parent / "b")
    candidates.append(Path(__file__).resolve().parent / "pw-browsers")

    for candidate in candidates:
        if candidate.exists():
            os.environ["PLAYWRIGHT_BROWSERS_PATH"] = str(candidate)
            return


configure_playwright_browser_path()

from playwright.sync_api import sync_playwright, TimeoutError as PlaywrightTimeoutError


def looks_office_only(text):
    haystack = (text or "").lower()
    markers = [
        "ip-adresse",
        "ip adresse",
        "kontor",
        "kontorets",
        "netværk",
        "netvaerk",
        "adgang nægtet",
        "adgang naegtet",
        "ikke tilladt",
        "not allowed",
        "permission",
        "forbidden",
    ]
    return any(marker in haystack for marker in markers)


def output(obj):
    data = json.dumps(obj, ensure_ascii=False) + "\n"
    sys.stdout.buffer.write(data.encode("utf-8", errors="replace"))
    sys.stdout.flush()


def read_stdin_json():
    # Qt sends UTF-8 bytes. Windows pipe text encoding otherwise depends on locale.
    stream = getattr(sys.stdin, "buffer", None)
    raw = stream.read().decode("utf-8") if stream is not None else sys.stdin.read()
    return json.loads(raw)


def parse_hours(text):
    match = re.search(r"(\d+)\s*t\.\s*(\d+)\s*min\.", text)

    if not match:
        return None

    hours = int(match.group(1))
    minutes = int(match.group(2))

    return round(hours + minutes / 60.0, 2)


def parse_money(text):
    value = clean_text(text)
    match = re.search(r"-?\d[\d\.\s]*(?:,\d+)?|-?\d+(?:\.\d+)?", value)

    if not match:
        return None

    normalized = match.group(0).replace(" ", "")
    if "," in normalized:
        normalized = normalized.replace(".", "").replace(",", ".")

    try:
        return round(float(normalized), 2)
    except ValueError:
        return None


def clean_text(value):
    return re.sub(r"\s+", " ", value or "").strip()


def fold_danish_letters(value):
    return (value or "").lower() \
        .replace("\u00e6", "ae") \
        .replace("\u00f8", "oe") \
        .replace("\u00e5", "aa") \
        .replace("\u00c6", "ae") \
        .replace("\u00d8", "oe") \
        .replace("\u00c5", "aa") \
        .replace("\u00c3\u00a6", "ae") \
        .replace("\u00c3\u00b8", "oe") \
        .replace("\u00c3\u00a5", "aa")


def text_lines(value):
    return [clean_text(line) for line in (value or "").splitlines() if clean_text(line)]


def normalise_date(value):
    return (value or "").strip()


def save_debug_screenshot(page, debug_dir, name, full_page=True, force=False):
    if not (DEBUG_ENABLED or force):
        return

    try:
        debug_dir.mkdir(parents=True, exist_ok=True)
        page.screenshot(
            path=str(debug_dir / name),
            full_page=full_page
        )
    except Exception:
        pass


def write_debug_text(path, text, force=False):
    if not (DEBUG_ENABLED or force):
        return

    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    except Exception:
        pass


def is_probably_logged_in(page):
    try:
        if page.locator('input[type="password"]').count() > 0:
            return False
    except Exception:
        pass

    try:
        if clean_text(page.locator("#main").inner_text(timeout=1500)):
            return True
    except Exception:
        pass

    try:
        body_text = clean_text(page.locator("body").inner_text(timeout=1500)).lower()
        if "log ud" in body_text or "velkommen" in body_text:
            return True
    except Exception:
        pass

    return False


def session_state_path(args):
    explicit = clean_text(getattr(args, "session_state", ""))
    if explicit:
        return Path(explicit)

    base_dir = os.environ.get("LOCALAPPDATA") or os.environ.get("APPDATA") or str(Path.home())
    return Path(base_dir) / "ProvisionTrackerV2" / "ProviTracker" / "intramanager_session.json"


def create_browser_context(browser, args):
    path = session_state_path(args)
    if path.exists():
        try:
            return browser.new_context(storage_state=str(path))
        except Exception:
            pass

    return browser.new_context()


def save_session_state(context, args):
    path = session_state_path(args)
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        context.storage_state(path=str(path))
    except Exception:
        pass


def clear_session_state(page, args):
    try:
        page.context.clear_cookies()
    except Exception:
        pass

    try:
        session_state_path(args).unlink(missing_ok=True)
    except Exception:
        pass


def login_surfaces(page):
    surfaces = [page]
    try:
        surfaces.extend(page.frames)
    except Exception:
        pass
    return surfaces


def fill_first_login_field(page, selectors, value):
    fallback = None

    for surface in login_surfaces(page):
        for sel in selectors:
            try:
                loc = surface.locator(sel).first
                if loc.count() <= 0:
                    continue
                if fallback is None:
                    fallback = loc
                try:
                    if not loc.is_visible(timeout=500):
                        continue
                except Exception:
                    continue
                loc.fill(value, timeout=5000)
                return True
            except Exception:
                pass

    if fallback is not None:
        try:
            fallback.fill(value, timeout=5000)
            return True
        except Exception:
            pass

    return False


def click_login_button(page, selectors):
    for surface in login_surfaces(page):
        for sel in selectors:
            try:
                loc = surface.locator(sel).first
                if loc.count() > 0 and click_locator(loc):
                    return True
            except Exception:
                pass

    return False


def login(page, args, debug_dir):
    page.goto(
        LOGIN_URL,
        wait_until="domcontentloaded",
        timeout=60000
    )

    try:
        page.wait_for_selector("#main, form, input[type='password'], input[name*='pass' i]", timeout=10000)
    except PlaywrightTimeoutError:
        page.wait_for_timeout(500)

    if is_probably_logged_in(page):
        return {"success": True, "usedSession": True}

    save_debug_screenshot(page, debug_dir, "01_start.png")

    username_selectors = [
        'input[name="user"]',
        'input[name="username"]',
        'input[name="login"]',
        'input[name="userid"]',
        'input[name="user_name"]',
        'input[name="email"]',
        'input[name*="user" i]',
        'input[autocomplete="username"]',
        'input[type="email"]',
        'input[type="text"]',
        '#username',
        '#email',
    ]

    password_selectors = [
        'input[name="password"]',
        'input[name*="pass" i]',
        'input[autocomplete="current-password"]',
        'input[type="password"]',
        '#password',
    ]

    user_filled = fill_first_login_field(page, username_selectors, args.username)
    pass_filled = fill_first_login_field(page, password_selectors, args.password)

    if not user_filled or not pass_filled:
        clear_session_state(page, args)
        try:
            page.goto(BASE_URL, wait_until="domcontentloaded", timeout=60000)
            page.wait_for_selector("form, input[type='password'], input[name*='pass' i]", timeout=10000)
        except PlaywrightTimeoutError:
            pass

        if is_probably_logged_in(page):
            return {"success": True, "usedSession": False}

        user_filled = fill_first_login_field(page, username_selectors, args.username)
        pass_filled = fill_first_login_field(page, password_selectors, args.password)

    if not user_filled or not pass_filled:
        save_debug_screenshot(page, debug_dir, "02_login_fields_not_found.png", force=True)

        write_debug_text(
            debug_dir / "login_page.html",
            page.content(),
            force=True
        )

        return {
            "success": False,
            "stage": "login_fields",
            "error": "Kunne ikke finde loginfelterne automatisk.",
            "debugDir": str(debug_dir)
        }

    clicked = False

    login_button_selectors = [
        'button[type="submit"]',
        'input[type="submit"]',
        'button.btn',
        'button:has-text("Login")',
        'button:has-text("Log ind")',
        'text=Login',
        'text=Log ind',
    ]

    clicked = click_login_button(page, login_button_selectors)

    if not clicked:
        page.keyboard.press("Enter")

    page.wait_for_timeout(1200)

    try:
        page.wait_for_load_state(
            "networkidle",
            timeout=15000
        )

    except PlaywrightTimeoutError:
        pass

    save_debug_screenshot(page, debug_dir, "03_after_login.png")

    write_debug_text(debug_dir / "after_login.html", page.content())

    current_url = page.url

    error_text = ""

    try:
        if page.locator(".alert-danger").count() > 0:
            error_text = page.locator(".alert-danger").inner_text()

    except Exception:
        error_text = ""

    if "stemmer ikke overens" in error_text.lower():
        return {
            "success": False,
            "stage": "login",
            "error": "Forkert brugernavn eller adgangskode.",
            "url": current_url,
            "debugDir": str(debug_dir)
        }

    return {"success": True}


def search_history(page, from_date, to_date, debug_dir, prefix):
    page.goto(
        HISTORY_URL,
        wait_until="domcontentloaded",
        timeout=30000
    )

    try:
        page.wait_for_selector("#startdate", timeout=10000)
    except PlaywrightTimeoutError:
        pass

    page.locator("#startdate").fill(from_date)
    page.locator("#stopdate").fill(to_date)

    save_debug_screenshot(page, debug_dir, f"{prefix}_dates_filled.png")

    page.locator('button[type="submit"]').click()
    try:
        page.wait_for_selector("#main table, #main tbody tr", timeout=5000)
    except PlaywrightTimeoutError:
        try:
            page.get_by_text("Intet blev fundet", exact=False).wait_for(timeout=1500)
        except PlaywrightTimeoutError:
            page.wait_for_timeout(1500)

    save_debug_screenshot(page, debug_dir, f"{prefix}_results.png")

    write_debug_text(
        debug_dir / f"{prefix}_results_page.html",
        page.content(),
    )

    main_html = ""

    try:
        main_html = page.locator("#main").inner_html()

    except Exception:
        main_html = ""

    write_debug_text(
        debug_dir / f"{prefix}_main_results.html",
        main_html,
    )


def result_says_no_rows(page):
    try:
        main_text = clean_text(page.locator("#main").inner_text())
    except Exception:
        main_text = ""

    return "intet blev fundet" in main_text.lower()


def is_absence_pay_project(project):
    lower = fold_danish_letters(clean_text(project))
    markers = [
        "syg",
        "sygdom",
        "sygeløn",
        "sygeloen",
        "fravær",
        "fravaer",
        "ferie",
        "omsorg",
        "orlov",
        "garantiløn",
        "garantiloen",
    ]
    return any(marker in lower for marker in markers)


def payroll_money_from_cells(cells):
    total = 0.0
    found_any = False

    # Timeseddelshistorik columns: Grundløn, Bonus, Provision.
    for index in (5, 6, 7):
        if len(cells) <= index:
            continue
        amount = parse_money(cells[index])
        if amount is None:
            continue
        total += amount
        found_any = True

    return round(total * INTRAMANAGER_SICK_PAY_FACTOR, 2) if found_any else 0.0


def payroll_summary_from_result_rows(page):
    paid_hours = 0.0
    phone_hours = 0.0
    absence_hours = 0.0
    absence_pay = 0.0
    cancellations = 0.0
    found_any = False

    try:
        rows = page.locator("#main tbody tr")

        for i in range(rows.count()):
            cells = rows.nth(i).locator("td").all_inner_texts()
            if not cells:
                continue
            if len(cells) < 8:
                return None
            if any(clean_text(cells[index]) not in ('', '-') and parse_money(cells[index]) is None for index in (5,6,7)):
                return None

            project = clean_text(cells[0])
            hour_lines = text_lines(cells[2])
            if not hour_lines:
                return None

            paid = parse_hours(hour_lines[0])
            if paid is None:
                return None

            phone = parse_hours(hour_lines[1]) if len(hour_lines) >= 2 else 0.0
            if any(term in fold_danish_letters(project).lower() for term in ("annullering", "cancellation")):
                cancellations += payroll_money_from_cells(cells)
                found_any = True
                continue
            if is_absence_pay_project(project):
                absence_hours += paid
                absence_pay += payroll_money_from_cells(cells)
            else:
                paid_hours += paid
                phone_hours += phone or 0.0
            found_any = True

    except Exception:
        return None

    if not found_any:
        return None

    return {
        "hours": round(paid_hours, 2),
        "phoneHours": round(phone_hours, 2),
        "sickHours": round(absence_hours, 2),
        "sickPay": round(absence_pay, 2),
        "cancellations": round(cancellations, 2),
        "payrollSchema": 2,
    }


def fetch_hours(page, args, debug_dir):
    search_history(page, args.from_date, args.to_date, debug_dir, "hours")

    paid_hours = None
    phone_hours = None
    sick_hours = 0.0
    sick_pay = 0.0
    cancellations = 0.0
    row_summary = payroll_summary_from_result_rows(page)

    if row_summary is not None:
        paid_hours = row_summary.get("hours", 0.0)
        phone_hours = row_summary.get("phoneHours", 0.0)
        sick_hours = row_summary.get("sickHours", 0.0)
        sick_pay = row_summary.get("sickPay", 0.0)
        cancellations = row_summary.get("cancellations", 0.0)

    if paid_hours is None and result_says_no_rows(page):
        return {
            "success": True,
            "stage": "no_results",
            "cancellations": 0.0,
            "payrollSchema": 2,
            "message": "Ingen timer fundet for perioden.",
            "periodFrom": args.from_date,
            "periodTo": args.to_date,
            "hours": 0.0,
            "phoneHours": 0.0,
            "sickHours": 0.0,
            "sickPay": 0.0,
            "debugDir": str(debug_dir)
        }

    if paid_hours is None:
        return {
            "success": False,
            "stage": "parse_hours",
            "error": "Kunne ikke finde total løntimer i resultattabellen.",
            "periodFrom": args.from_date,
            "periodTo": args.to_date,
            "debugDir": str(debug_dir)
        }

    return {
        "success": True,
        "stage": "results_loaded",
        "message": "Timer hentet.",
        "periodFrom": args.from_date,
        "periodTo": args.to_date,
        "hours": paid_hours,
        "phoneHours": phone_hours,
        "sickHours": sick_hours,
        "sickPay": sick_pay,
        "cancellations": cancellations,
        "payrollSchema": 2,
        "debugDir": str(debug_dir)
    }


def read_punch_state_from_history(page, target_date, debug_dir, prefix):
    search_history(page, target_date, target_date, debug_dir, prefix)

    rows = []
    target_variants = {
        target_date,
        target_date.replace("-", "."),
        target_date.replace("-", "/"),
    }

    try:
        table_rows = page.locator("#main table tbody tr")

        for i in range(table_rows.count()):
            row = table_rows.nth(i)
            cells = row.locator("td").all_inner_texts()

            if len(cells) < 2:
                continue

            row_lines = text_lines("\n".join(cells))
            date_lines = [
                line for line in row_lines
                if any(variant in line for variant in target_variants)
            ]

            start_value = date_lines[0] if len(date_lines) >= 1 else ""
            stop_value = date_lines[1] if len(date_lines) >= 2 else ""

            if not stop_value and start_value:
                try:
                    start_index = row_lines.index(start_value)
                    trailing = row_lines[start_index + 1:]
                    for candidate in trailing:
                        lowered = candidate.lower()
                        if lowered in {"-", "nu", "aktiv", "i gang", "igang"}:
                            stop_value = candidate
                            break
                except ValueError:
                    pass

            if not start_value:
                continue

            rows.append({
                "start": start_value,
                "stop": stop_value,
                "project": clean_text(cells[0]) if len(cells) > 0 else ""
            })

    except Exception:
        rows = []

    if not rows:
        return {
            "statusKnown": True,
            "clockedIn": False,
            "statusText": "Stemplet ud",
            "detail": "Der er ikke fundet en åben stempling for i dag.",
            "lastStart": "",
            "lastStop": ""
        }

    latest = rows[0]
    stop_lower = latest["stop"].lower()
    clocked_in = not latest["stop"] or stop_lower in {"-", "nu", "aktiv", "i gang", "igang"}

    if clocked_in:
        status_text = "Stemplet ind"
        detail = "Aktiv stempling startet " + latest["start"]
    else:
        status_text = "Stemplet ud"
        detail = "Seneste stempling sluttede " + latest["stop"]

    return {
        "statusKnown": True,
        "clockedIn": clocked_in,
        "statusText": status_text,
        "detail": detail,
        "lastStart": latest["start"],
        "lastStop": latest["stop"]
    }


def read_punch_state_from_punch_page(page, debug_dir, prefix):
    save_debug_screenshot(page, debug_dir, f"{prefix}_page.png")

    page_html = page.content()
    write_debug_text(
        debug_dir / f"{prefix}_page.html",
        page_html,
    )

    if looks_office_only(page_html):
        return {
            "success": False,
            "statusKnown": False,
            "error": OFFICE_ONLY_MESSAGE,
        }

    try:
        body_text = clean_text(page.locator("body").inner_text())
    except Exception:
        body_text = clean_text(page_html)

    lower = body_text.lower()

    button_texts = []
    try:
        buttons = page.locator(", ".join([
            "#main button",
            "#main input[type='submit']",
            "#main input[type='button']",
            "#main a.btn",
            "#main .stamp-list-item a",
            "#main .punchin-link",
            "#main .punch-link",
            "#main a[href*='punch']",
            "#main [onclick*='punch']",
            "#main [onclick*='Punch']",
        ]))
        for i in range(buttons.count()):
            text = ""
            value = ""

            try:
                text = clean_text(buttons.nth(i).inner_text())
            except Exception:
                text = ""

            try:
                value = clean_text(buttons.nth(i).get_attribute("value") or "")
            except Exception:
                value = ""

            combined = clean_text(text or value)
            if combined:
                button_texts.append(combined)
    except Exception:
        button_texts = []

    buttons_lower = " ".join(button_texts).lower()
    known = False
    clocked_in = False

    out_terms = ["stempel ud", "stempl ud", "check ud", "stop"]
    in_terms = ["stempel ind", "stempl ind", "check ind", "start"]

    if any(term in buttons_lower for term in out_terms):
        known = True
        clocked_in = True
    elif any(term in buttons_lower for term in in_terms):
        known = True
        clocked_in = False
    elif "stemplet ind" in lower:
        known = True
        clocked_in = True
    elif "stemplet ud" in lower:
        known = True
        clocked_in = False

    if not known:
        return {
            "success": True,
            "statusKnown": False,
            "clockedIn": False,
            "statusText": "Status ukendt",
            "detail": "Intramanager-stempelsiden blev hentet, men status kunne ikke aflæses.",
            "lastStart": "",
            "lastStop": "",
        }

    status_text = "Stemplet ind" if clocked_in else "Stemplet ud"
    detail = "Status aflæst fra Intramanager-stempelsiden."

    return {
        "success": True,
        "statusKnown": True,
        "clockedIn": clocked_in,
        "statusText": status_text,
        "detail": detail,
        "lastStart": "",
        "lastStop": "",
    }


def load_punch_page_state(page, debug_dir, prefix):
    page.goto(
        PUNCH_URL,
        wait_until="domcontentloaded",
        timeout=30000
    )

    page.wait_for_timeout(700)

    try:
        page.wait_for_load_state("networkidle", timeout=8000)
    except PlaywrightTimeoutError:
        pass

    return read_punch_state_from_punch_page(page, debug_dir, prefix)


def fetch_punch_status(page, args, debug_dir):
    today = normalise_date(args.on_date)
    page_state = load_punch_page_state(page, debug_dir, "punch_status_page")

    if not page_state.get("success", True):
        return {
            "success": False,
            "stage": "punch_status",
            "debugDir": str(debug_dir),
            **page_state
        }

    if page_state.get("statusKnown"):
        history_state = read_punch_state_from_history(page, today, debug_dir, "punch_status_history")
        if history_state.get("lastStart") or history_state.get("lastStop"):
            if history_state.get("clockedIn") == page_state.get("clockedIn"):
                page_state["detail"] = history_state.get("detail", page_state.get("detail", ""))
            page_state["lastStart"] = history_state.get("lastStart", page_state.get("lastStart", ""))
            page_state["lastStop"] = history_state.get("lastStop", page_state.get("lastStop", ""))
        return {
            "success": True,
            "stage": "punch_status",
            "debugDir": str(debug_dir),
            **page_state
        }

    state = read_punch_state_from_history(page, today, debug_dir, "punch_status_history")
    return {
        "success": True,
        "stage": "punch_status",
        "debugDir": str(debug_dir),
        **state
    }


def fetch_overview(page, args, debug_dir):
    hours = fetch_hours(page, args, debug_dir)
    punch = fetch_punch_status(page, args, debug_dir)
    hours_success = bool(hours.get("success"))
    punch_success = bool(punch.get("success"))

    return {
        "success": hours_success and punch_success,
        "stage": "overview",
        "debugDir": str(debug_dir),
        "hoursSuccess": hours_success,
        "punchSuccess": punch_success,
        "hoursError": hours.get("error", ""),
        "punchError": punch.get("error", ""),
        "periodFrom": hours.get("periodFrom", args.from_date),
        "periodTo": hours.get("periodTo", args.to_date),
        "hours": hours.get("hours", 0.0),
        "phoneHours": hours.get("phoneHours", 0.0),
        "sickHours": hours.get("sickHours", 0.0),
        "sickPay": hours.get("sickPay", 0.0),
        "cancellations": hours.get("cancellations", 0.0),
        "payrollSchema": 2,
        "statusKnown": punch.get("statusKnown", False),
        "clockedIn": punch.get("clockedIn", False),
        "statusText": punch.get("statusText", ""),
        "detail": punch.get("detail", ""),
        "lastStart": punch.get("lastStart", ""),
        "lastStop": punch.get("lastStop", ""),
    }


def locator_text(locator):
    values = []

    try:
        values.append(clean_text(locator.inner_text(timeout=1000)))
    except Exception:
        pass

    for attribute in (
        "value",
        "aria-label",
        "title",
        "data-original-title",
        "href",
        "onclick",
        "class",
        "id",
    ):
        try:
            values.append(clean_text(locator.get_attribute(attribute) or ""))
        except Exception:
            pass

    return clean_text(" ".join(value for value in values if value))


def looks_like_cancel_control(text):
    lower = (text or "").lower()
    cancel_terms = [
        "annuller",
        "fortryd",
        "cancel",
        "luk",
        "close",
        "dismiss",
    ]
    return any(term in lower for term in cancel_terms)


def click_locator(locator):
    try:
        if not locator.is_visible(timeout=500):
            return False
    except Exception:
        return False

    try:
        if not locator.is_enabled(timeout=500):
            return False
    except Exception:
        pass

    try:
        locator.scroll_into_view_if_needed(timeout=2000)
    except Exception:
        pass

    try:
        locator.click(timeout=5000)
        return True
    except Exception:
        return False


def click_first_available(page, selectors, max_matches=25):
    for sel in selectors:
        try:
            matches = page.locator(sel)
            count = min(matches.count(), max_matches)

            for i in range(count):
                if click_locator(matches.nth(i)):
                    return True

        except Exception:
            pass

    return False


def has_visible_dialog(page):
    dialog_selectors = [
        ".punch-in-dialog",
        ".bootstrap-dialog",
        ".modal-dialog",
        ".modal.show",
        "[role='dialog']",
    ]

    for sel in dialog_selectors:
        try:
            matches = page.locator(sel)
            for i in range(min(matches.count(), 10)):
                if matches.nth(i).is_visible(timeout=500):
                    return True
        except Exception:
            pass

    return False


def wait_for_page_idle(page, timeout=30000):
    try:
        page.wait_for_load_state("networkidle", timeout=timeout)
    except PlaywrightTimeoutError:
        pass
    except Exception:
        pass


def scoped_control_selector(scope):
    prefix = f"{scope} " if scope else ""
    return ", ".join([
        f"{prefix}button",
        f"{prefix}input[type='submit']",
        f"{prefix}input[type='button']",
        f"{prefix}a",
        f"{prefix}[role='button']",
        f"{prefix}[onclick]",
    ])


def click_control_by_terms(page, terms, scope="#main"):
    lowered_terms = [term.lower() for term in terms]

    try:
        controls = page.locator(scoped_control_selector(scope))
        for i in range(min(controls.count(), 80)):
            control = controls.nth(i)
            haystack = locator_text(control).lower()

            if not haystack or looks_like_cancel_control(haystack):
                continue

            if any(term in haystack for term in lowered_terms) and click_locator(control):
                return True
    except Exception:
        pass

    return False


def click_punch_dialog_confirmation(page, wants_out):
    if not has_visible_dialog(page):
        return False

    action_terms = (
        ["stempel ud", "stempl ud", "check ud", "stop", "afslut"]
        if wants_out
        else ["stempel ind", "stempl ind", "check ind", "start"]
    )
    confirmation_terms = action_terms + ["bekræft", "bekraeft", "gem", "ja", "ok", "fortsæt", "fortsaet"]

    dialog_scopes = [
        ".punch-in-dialog",
        ".bootstrap-dialog",
        ".modal-dialog",
        ".modal.show",
        "[role='dialog']",
    ]

    preferred_selectors = []
    for scope in dialog_scopes:
        for term in action_terms:
            preferred_selectors.extend([
                f'{scope} button:has-text("{term}")',
                f'{scope} a:has-text("{term}")',
                f'{scope} input[value*="{term}"]',
            ])

        preferred_selectors.extend([
            f"{scope} .bootstrap-dialog-footer-buttons button.btn-primary",
            f"{scope} .bootstrap-dialog-footer-buttons button",
            f"{scope} button.btn-primary",
            f"{scope} a.btn-primary",
            f"{scope} input[type='submit']",
        ])

    if click_first_available(page, preferred_selectors):
        page.wait_for_timeout(1500)
        wait_for_page_idle(page, timeout=15000)
        return True

    for scope in dialog_scopes:
        if click_control_by_terms(page, confirmation_terms, scope):
            page.wait_for_timeout(1500)
            wait_for_page_idle(page, timeout=15000)
            return True

    return False


def click_punch_control(page, wants_out, allow_generic=False):
    if wants_out:
        terms = ["stempel ud", "stempl ud", "check ud", "stop", "afslut"]
        preferred_selectors = [
            '#main button:has-text("Stempel ud")',
            '#main button:has-text("Stempl ud")',
            '#main a:has-text("Stempel ud")',
            '#main a:has-text("Stempl ud")',
            '#main input[value*="Stempel ud"]',
            '#main input[value*="Stempl ud"]',
            '#main button:has-text("Stop")',
            '#main a:has-text("Stop")',
            '#main input[value*="Stop"]',
            '#main .punchin-link',
            '#main .punch-link',
            '#main a[href*="punch"]',
            '#main [onclick*="punch"]',
            '#main [onclick*="Punch"]',
        ]
    else:
        terms = ["stempel ind", "stempl ind", "check ind", "start"]
        preferred_selectors = [
            '#main button:has-text("Stempel ind")',
            '#main button:has-text("Stempl ind")',
            '#main a:has-text("Stempel ind")',
            '#main a:has-text("Stempl ind")',
            '#main input[value*="Stempel ind"]',
            '#main input[value*="Stempl ind"]',
            '#main button:has-text("Start")',
            '#main a:has-text("Start")',
            '#main input[value*="Start"]',
            "#main .stamp-list-item a",
            "#main .punchin-link",
            "#main .punch-link",
            '#main a[href*="punch"]',
            '#main [onclick*="punch"]',
            '#main [onclick*="Punch"]',
        ]

    if click_first_available(page, preferred_selectors):
        return True

    if click_control_by_terms(page, terms, "#main"):
        return True

    if allow_generic:
        return click_first_available(page, [
            '#main button:has-text("Stempel")',
            '#main button:has-text("Stempl")',
            '#main a:has-text("Stempel")',
            '#main a:has-text("Stempl")',
            "#main button[type='submit']",
            "#main input[type='submit']",
        ])

    return False


def punch_state_matches(state, before, desired_clocked_in):
    if not state.get("statusKnown"):
        return False

    if desired_clocked_in is None:
        return (
            before.get("statusKnown")
            and state.get("clockedIn") != before.get("clockedIn")
        )

    return state.get("clockedIn") == desired_clocked_in


def toggle_punch(page, args, debug_dir):
    today = normalise_date(args.on_date)
    history_before = read_punch_state_from_history(page, today, debug_dir, "punch_before_history")
    page_before = load_punch_page_state(page, debug_dir, "punch_before_page")

    if not page_before.get("success", True):
        return {
            "success": False,
            "stage": "punch_toggle",
            "debugDir": str(debug_dir),
            **history_before,
            **page_before,
        }

    before = page_before if page_before.get("statusKnown") else history_before
    target_action = clean_text(getattr(args, "target_action", "")).lower()
    desired_clocked_in = None
    if target_action in {"in", "ind"}:
        desired_clocked_in = True
    elif target_action in {"out", "ud"}:
        desired_clocked_in = False

    wants_out = desired_clocked_in is False or (desired_clocked_in is None and before.get("clockedIn"))

    if (
        desired_clocked_in is not None
        and before.get("statusKnown")
        and before.get("clockedIn") == desired_clocked_in
    ):
        return {
            "success": True,
            "stage": "punch_toggle",
            "message": "Allerede stemplet ind" if desired_clocked_in else "Allerede stemplet ud",
            "debugDir": str(debug_dir),
            **before
        }

    clicked = click_punch_control(page, wants_out, desired_clocked_in is None)

    if clicked:
        page.wait_for_timeout(1500)
        click_punch_dialog_confirmation(page, wants_out)
        page.wait_for_timeout(1800)
        wait_for_page_idle(page, timeout=12000)

        if has_visible_dialog(page):
            click_punch_dialog_confirmation(page, wants_out)
            page.wait_for_timeout(1200)
            wait_for_page_idle(page, timeout=12000)

    save_debug_screenshot(page, debug_dir, "punch_after_click.png")

    write_debug_text(
        debug_dir / "punch_after_click.html",
        page.content(),
    )

    page_after = load_punch_page_state(page, debug_dir, "punch_after_page")
    if not page_after.get("success", True):
        return {
            "success": False,
            "stage": "punch_toggle",
            "debugDir": str(debug_dir),
            **history_before,
            **page_after,
        }

    history_after = read_punch_state_from_history(page, today, debug_dir, "punch_after_history")
    candidates = [page_after, history_after]
    changed = any(punch_state_matches(candidate, before, desired_clocked_in) for candidate in candidates)
    after = next(
        (candidate for candidate in candidates if punch_state_matches(candidate, before, desired_clocked_in)),
        page_after if page_after.get("statusKnown") else history_after
    )

    if not changed:
        if desired_clocked_in is None:
            expected = "ind/ud"
        else:
            expected = "ind" if desired_clocked_in else "ud"
        return {
            "success": False,
            "stage": "punch_toggle",
            "error": OFFICE_ONLY_MESSAGE if not clicked else f"Intramanager svarede, men du blev ikke stemplet {expected}.",
            "debugDir": str(debug_dir),
            **after
        }

    return {
        "success": True,
        "stage": "punch_toggle",
        "message": "Stemplet ind" if after.get("clockedIn") else "Stemplet ud",
        "debugDir": str(debug_dir),
        **after
    }


def main():
    parser = argparse.ArgumentParser()

    parser.add_argument("--action", choices=["hours", "punch-status", "punch-toggle", "overview"], default="hours")
    parser.add_argument("--username", required=False)
    parser.add_argument("--password", required=False)
    parser.add_argument("--stdin-json", action="store_true")

    parser.add_argument("--from-date", required=False)
    parser.add_argument("--to-date", required=False)
    parser.add_argument("--on-date", required=False)

    parser.add_argument("--debug-dir", default="debug_intramanager")
    parser.add_argument("--session-state", required=False)
    parser.add_argument("--debug", action="store_true")
    parser.add_argument("--headed", action="store_true")
    parser.add_argument("--target-action", choices=["in", "out"], required=False)

    args = parser.parse_args()

    if args.stdin_json:
        payload = read_stdin_json()

        args.action = payload.get("action", args.action)
        if args.action == "chrome-outlook":
            from chrome_bridge import request_outlook
            try:
                output(request_outlook(payload))
            except Exception as exc:
                output({"success":False,"notSent":False,"status":"mail-uncertain","error":str(exc)[:500]})
            return
        if args.action == "chrome-intramanager":
            from chrome_bridge import request_intramanager
            try:
                output(request_intramanager(payload))
            except Exception as exc:
                output({"success":False,"error":str(exc)[:500]})
            return
        if args.action == "chrome-update":
            from chrome_bridge import sync_installed_extension
            try:
                output(sync_installed_extension())
            except Exception as exc:
                output({"success": False, "error": str(exc)[:700]})
            return
        if args.action == "chrome-install":
            from chrome_bridge import install_extension
            try:
                output(install_extension())
            except Exception as exc:
                output({"success": False, "error": str(exc)[:700]})
            return
        if args.action in {"chrome-setup", "chrome-autosetup", "chrome-register"}:
            from chrome_bridge import run_registration
            try:
                output(run_registration(payload))
            except Exception as exc:
                output({"success": False, "error": str(exc)[:700]})
            return
        if args.action == "chrome-probe":
            from chrome_bridge import probe
            try:
                output(probe(payload))
            except Exception as exc:
                output({"success": False, "error": str(exc)[:700]})
            return
        if args.action in {"master-setup", "master-register"}:
            from master_registration import run
            try:
                with sync_playwright() as p:
                    output(run(payload, p))
            except Exception as exc:
                output({"success": False, "stage": "master-registration", "error": str(exc)[:700]})
            return

        args.username = payload.get("username", "")
        args.password = payload.get("password", "")

        args.from_date = payload.get("fromDate", args.from_date)
        args.to_date = payload.get("toDate", args.to_date)
        args.on_date = payload.get("onDate", args.on_date)
        args.target_action = payload.get("targetAction", args.target_action)
        args.session_state = payload.get("sessionState", args.session_state)
        args.debug = bool(payload.get("debug", args.debug))
        args.debug_dir = payload.get("debugDir", args.debug_dir)

    if args.action not in {"hours", "punch-status", "punch-toggle", "overview"}:
        parser.error("Unsupported action")

    global DEBUG_ENABLED
    DEBUG_ENABLED = bool(args.debug)

    if not args.on_date:
        from datetime import datetime
        args.on_date = datetime.now().strftime("%d-%m-%Y")

    if args.action in {"hours", "overview"} and (not args.from_date or not args.to_date):
        output({
            "success": False,
            "stage": "input",
            "error": "Fra-dato og til-dato mangler."
        })
        return

    if not args.username or not args.password:
        output({
            "success": False,
            "stage": "input",
            "error": "Brugernavn eller adgangskode mangler."
        })
        return

    debug_dir = Path(args.debug_dir)
    if DEBUG_ENABLED:
        debug_dir.mkdir(parents=True, exist_ok=True)

    browser = None

    try:
        with sync_playwright() as p:

            browser = p.chromium.launch(
                headless=not args.headed,
                slow_mo=100 if args.headed else 0
            )

            context = create_browser_context(browser, args)
            page = context.new_page()

            login_result = login(page, args, debug_dir)

            if not login_result.get("success"):
                browser.close()
                output(login_result)
                return

            save_session_state(context, args)

            if args.action == "hours":
                result = fetch_hours(page, args, debug_dir)
            elif args.action == "punch-status":
                result = fetch_punch_status(page, args, debug_dir)
            elif args.action == "overview":
                result = fetch_overview(page, args, debug_dir)
            else:
                result = toggle_punch(page, args, debug_dir)

            browser.close()
            output(result)

    except Exception as e:

        try:
            if browser is not None:
                browser.close()

        except Exception:
            pass

        output({
            "success": False,
            "stage": "exception",
            "error": str(e),
            "debugDir": str(debug_dir)
        })


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1].startswith("chrome-extension://"):
        from chrome_bridge import native_main
        sys.exit(native_main(sys.argv[1]))
    main()
