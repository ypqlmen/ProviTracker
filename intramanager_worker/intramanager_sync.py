import argparse
import json
import re
import sys
from pathlib import Path
from playwright.sync_api import sync_playwright, TimeoutError as PlaywrightTimeoutError

BASE_URL = "https://5r.intramanager.com/"
LOGIN_URL = BASE_URL + "reports/history/"
HISTORY_URL = BASE_URL + "reports/history/"
PUNCH_URL = BASE_URL + "reports/punch-in/"
OFFICE_ONLY_MESSAGE = "Man kan kun stemple ind eller ud på kontorets internet."


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

    try:
        table_rows = page.locator("#main table tbody tr")

        for i in range(table_rows.count()):
            row = table_rows.nth(i)
            cells = row.locator("td").all_inner_texts()

            if len(cells) < 2:
                continue

            time_parts = text_lines(cells[1])
            start_value = time_parts[0] if len(time_parts) >= 1 else ""
            stop_value = time_parts[1] if len(time_parts) >= 2 else ""

            if not start_value.startswith(target_date):
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


def fetch_punch_status(page, args, debug_dir):
    today = normalise_date(args.on_date)
    state = read_punch_state_from_history(page, today, debug_dir, "punch_status")
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


def toggle_punch(page, args, debug_dir):
    today = normalise_date(args.on_date)
    before = read_punch_state_from_history(page, today, debug_dir, "punch_before")

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

    page_html = page.content()
    if looks_office_only(page_html):
        (debug_dir / "punch_office_only.html").write_text(
            page_html,
            encoding="utf-8"
        )
        return {
            "success": False,
            "stage": "punch_toggle",
            "error": OFFICE_ONLY_MESSAGE,
            "debugDir": str(debug_dir),
            **before
        }

    clicked = click_first_available(page, [
        'button:has-text("Stempel")',
        'button:has-text("Stempl")',
        'button:has-text("Start")',
        'button:has-text("Stop")',
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

    after = read_punch_state_from_history(page, today, debug_dir, "punch_after")

    changed = (
        before.get("statusKnown")
        and after.get("statusKnown")
        and before.get("clockedIn") != after.get("clockedIn")
    )

    if not changed:
        return {
            "success": False,
            "stage": "punch_toggle",
            "error": OFFICE_ONLY_MESSAGE if not clicked else "Intramanager svarede, men stempelstatus ændrede sig ikke.",
            "debugDir": str(debug_dir),
            **after
        }

    return {
        "success": True,
        "stage": "punch_toggle",
        "message": after.get("statusText", "Stempelstatus opdateret."),
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

    args = parser.parse_args()

    if args.stdin_json:
        payload = json.loads(sys.stdin.read())

        args.action = payload.get("action", args.action)
        args.username = payload.get("username", "")
        args.password = payload.get("password", "")

        args.from_date = payload.get("fromDate", args.from_date)
        args.to_date = payload.get("toDate", args.to_date)
        args.on_date = payload.get("onDate", args.on_date)

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
