import argparse
import json
import os
import re
import sys
from pathlib import Path

BASE_URL = "https://5r.intramanager.com/"
LOGIN_URL = BASE_URL + "reports/history/"
HISTORY_URL = BASE_URL + "reports/history/"
PUNCH_URL = BASE_URL + "reports/punch-in/"
OFFICE_ONLY_MESSAGE = "Man kan kun stemple ind eller ud på kontorets internet."


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


def parse_hours(text):
    match = re.search(r"(\d+)\s*t\.\s*(\d+)\s*min\.", text)

    if not match:
        return None

    hours = int(match.group(1))
    minutes = int(match.group(2))

    return round(hours + minutes / 60.0, 2)


def clean_text(value):
    return re.sub(r"\s+", " ", value or "").strip()


def text_lines(value):
    return [clean_text(line) for line in (value or "").splitlines() if clean_text(line)]


def normalise_date(value):
    return (value or "").strip()


def login(page, args, debug_dir):
    page.goto(
        LOGIN_URL,
        wait_until="domcontentloaded",
        timeout=60000
    )

    page.screenshot(
        path=str(debug_dir / "01_start.png"),
        full_page=True
    )

    username_selectors = [
        'input[name="user"]',
        'input[name="username"]',
        'input[name="email"]',
        'input[type="email"]',
        'input[type="text"]',
        '#username',
        '#email',
    ]

    password_selectors = [
        'input[name="password"]',
        'input[type="password"]',
        '#password',
    ]

    user_filled = False

    for sel in username_selectors:
        try:
            loc = page.locator(sel).first

            if loc.count() > 0:
                loc.fill(args.username)
                user_filled = True
                break

        except Exception:
            pass

    pass_filled = False

    for sel in password_selectors:
        try:
            loc = page.locator(sel).first

            if loc.count() > 0:
                loc.fill(args.password)
                pass_filled = True
                break

        except Exception:
            pass

    if not user_filled or not pass_filled:
        page.screenshot(
            path=str(debug_dir / "02_login_fields_not_found.png"),
            full_page=True
        )

        (debug_dir / "login_page.html").write_text(
            page.content(),
            encoding="utf-8"
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
        'button:has-text("Login")',
        'button:has-text("Log ind")',
        'text=Login',
        'text=Log ind',
    ]

    for sel in login_button_selectors:
        try:
            loc = page.locator(sel).first

            if loc.count() > 0:
                loc.click()
                clicked = True
                break

        except Exception:
            pass

    if not clicked:
        page.keyboard.press("Enter")

    page.wait_for_timeout(3000)

    try:
        page.wait_for_load_state(
            "networkidle",
            timeout=60000
        )

    except PlaywrightTimeoutError:
        pass

    page.screenshot(
        path=str(debug_dir / "03_after_login.png"),
        full_page=True
    )

    (debug_dir / "after_login.html").write_text(
        page.content(),
        encoding="utf-8"
    )

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
        wait_until="networkidle",
        timeout=60000
    )

    page.locator("#startdate").fill(from_date)
    page.locator("#stopdate").fill(to_date)

    page.screenshot(
        path=str(debug_dir / f"{prefix}_dates_filled.png"),
        full_page=True
    )

    page.locator('button[type="submit"]').click()
    page.wait_for_timeout(5000)

    page.screenshot(
        path=str(debug_dir / f"{prefix}_results.png"),
        full_page=True
    )

    (debug_dir / f"{prefix}_results_page.html").write_text(
        page.content(),
        encoding="utf-8"
    )

    main_html = ""

    try:
        main_html = page.locator("#main").inner_html()

    except Exception:
        main_html = ""

    (debug_dir / f"{prefix}_main_results.html").write_text(
        main_html,
        encoding="utf-8"
    )


def result_says_no_rows(page):
    try:
        main_text = clean_text(page.locator("#main").inner_text())
    except Exception:
        main_text = ""

    return "intet blev fundet" in main_text.lower()


def sum_hours_from_result_rows(page):
    paid_hours = 0.0
    phone_hours = 0.0
    found_any = False

    try:
        rows = page.locator("#main tbody tr")

        for i in range(rows.count()):
            cells = rows.nth(i).locator("td").all_inner_texts()
            if len(cells) < 3:
                continue

            hour_lines = text_lines(cells[2])
            if not hour_lines:
                continue

            paid = parse_hours(hour_lines[0])
            if paid is None:
                continue

            phone = parse_hours(hour_lines[1]) if len(hour_lines) >= 2 else 0.0
            paid_hours += paid
            phone_hours += phone or 0.0
            found_any = True

    except Exception:
        return None, None

    if not found_any:
        return None, None

    return round(paid_hours, 2), round(phone_hours, 2)


def fetch_hours(page, args, debug_dir):
    search_history(page, args.from_date, args.to_date, debug_dir, "hours")

    paid_hours = None
    phone_hours = None

    try:
        footer_cells = (
            page
            .locator("tfoot tr")
            .first
            .locator("td")
            .all_inner_texts()
        )

        if len(footer_cells) >= 3:

            hour_lines = footer_cells[2].splitlines()

            if len(hour_lines) >= 1:
                paid_hours = parse_hours(hour_lines[0])

            if len(hour_lines) >= 2:
                phone_hours = parse_hours(hour_lines[1])

    except Exception:
        paid_hours = None
        phone_hours = None

    if paid_hours is None:
        paid_hours, phone_hours = sum_hours_from_result_rows(page)

    if paid_hours is None and result_says_no_rows(page):
        return {
            "success": True,
            "stage": "no_results",
            "message": "Ingen timer fundet for perioden.",
            "periodFrom": args.from_date,
            "periodTo": args.to_date,
            "hours": 0.0,
            "phoneHours": 0.0,
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
    page.screenshot(
        path=str(debug_dir / f"{prefix}_page.png"),
        full_page=True
    )

    page_html = page.content()
    (debug_dir / f"{prefix}_page.html").write_text(
        page_html,
        encoding="utf-8"
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
        buttons = page.locator("button, input[type='submit'], a.btn")
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
        timeout=60000
    )

    page.wait_for_timeout(2500)

    try:
        page.wait_for_load_state("networkidle", timeout=30000)
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


def click_first_available(page, selectors):
    for sel in selectors:
        try:
            loc = page.locator(sel).first

            if loc.count() > 0:
                loc.click()
                return True

        except Exception:
            pass

    return False


def click_control_by_terms(page, terms):
    lowered_terms = [term.lower() for term in terms]

    try:
        controls = page.locator("button, input[type='submit'], input[type='button'], a")
        for i in range(controls.count()):
            control = controls.nth(i)
            values = []

            try:
                values.append(clean_text(control.inner_text()))
            except Exception:
                pass

            for attribute in ("value", "aria-label", "title"):
                try:
                    values.append(clean_text(control.get_attribute(attribute) or ""))
                except Exception:
                    pass

            haystack = " ".join(value for value in values if value).lower()
            if haystack and any(term in haystack for term in lowered_terms):
                control.click()
                return True
    except Exception:
        pass

    return False


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

    if wants_out:
        terms = ["stempel ud", "stempl ud", "check ud", "stop"]
        preferred_selectors = [
            'button:has-text("Stempel ud")',
            'button:has-text("Stempl ud")',
            'a:has-text("Stempel ud")',
            'a:has-text("Stempl ud")',
            'input[value*="Stempel ud"]',
            'input[value*="Stempl ud"]',
            'button:has-text("Stop")',
            'a:has-text("Stop")',
            'input[value*="Stop"]',
        ]
    else:
        terms = ["stempel ind", "stempl ind", "check ind", "start"]
        preferred_selectors = [
            'button:has-text("Stempel ind")',
            'button:has-text("Stempl ind")',
            'a:has-text("Stempel ind")',
            'a:has-text("Stempl ind")',
            'input[value*="Stempel ind"]',
            'input[value*="Stempl ind"]',
            'button:has-text("Start")',
            'a:has-text("Start")',
            'input[value*="Start"]',
        ]

    clicked = click_first_available(page, preferred_selectors)
    if not clicked:
        clicked = click_control_by_terms(page, terms)
    if not clicked and desired_clocked_in is None:
        clicked = click_first_available(page, [
            'button:has-text("Stempel")',
            'button:has-text("Stempl")',
            'button[type="submit"]',
            'input[type="submit"]',
        ])

    if clicked:
        page.wait_for_timeout(2500)

        try:
            page.wait_for_load_state("networkidle", timeout=30000)
        except PlaywrightTimeoutError:
            pass

    page.screenshot(
        path=str(debug_dir / "punch_after_click.png"),
        full_page=True
    )

    (debug_dir / "punch_after_click.html").write_text(
        page.content(),
        encoding="utf-8"
    )

    page_after = read_punch_state_from_punch_page(page, debug_dir, "punch_after_page")
    if not page_after.get("success", True):
        return {
            "success": False,
            "stage": "punch_toggle",
            "debugDir": str(debug_dir),
            **history_before,
            **page_after,
        }

    after = page_after if page_after.get("statusKnown") else read_punch_state_from_history(page, today, debug_dir, "punch_after_history")

    if desired_clocked_in is None:
        changed = (
            before.get("statusKnown")
            and after.get("statusKnown")
            and before.get("clockedIn") != after.get("clockedIn")
        )
    else:
        changed = after.get("statusKnown") and after.get("clockedIn") == desired_clocked_in

    if not changed:
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

    parser.add_argument("--action", choices=["hours", "punch-status", "punch-toggle"], default="hours")
    parser.add_argument("--username", required=False)
    parser.add_argument("--password", required=False)
    parser.add_argument("--stdin-json", action="store_true")

    parser.add_argument("--from-date", required=False)
    parser.add_argument("--to-date", required=False)
    parser.add_argument("--on-date", required=False)

    parser.add_argument("--debug-dir", default="debug_intramanager")
    parser.add_argument("--headed", action="store_true")
    parser.add_argument("--target-action", choices=["in", "out"], required=False)

    args = parser.parse_args()

    if args.stdin_json:
        payload = json.loads(sys.stdin.read())

        args.action = payload.get("action", args.action)
        args.username = payload.get("username", "")
        args.password = payload.get("password", "")

        args.from_date = payload.get("fromDate", args.from_date)
        args.to_date = payload.get("toDate", args.to_date)
        args.on_date = payload.get("onDate", args.on_date)
        args.target_action = payload.get("targetAction", args.target_action)

    if not args.on_date:
        from datetime import datetime
        args.on_date = datetime.now().strftime("%d-%m-%Y")

    if args.action == "hours" and (not args.from_date or not args.to_date):
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
    debug_dir.mkdir(parents=True, exist_ok=True)

    browser = None

    try:
        with sync_playwright() as p:

            browser = p.chromium.launch(
                headless=not args.headed,
                slow_mo=100 if args.headed else 0
            )

            context = browser.new_context()
            page = context.new_page()

            login_result = login(page, args, debug_dir)

            if not login_result.get("success"):
                browser.close()
                output(login_result)
                return

            if args.action == "hours":
                result = fetch_hours(page, args, debug_dir)
            elif args.action == "punch-status":
                result = fetch_punch_status(page, args, debug_dir)
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
    main()
