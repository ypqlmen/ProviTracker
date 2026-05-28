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
KVIKOC_URL = "https://kvikoc.tdc.dk/"
DEBUG_ENABLED = False
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


def kvikoc_wait(page, timeout=3000):
    try:
        page.wait_for_load_state("networkidle", timeout=timeout)
    except Exception:
        page.wait_for_timeout(min(timeout, 3000))


def kvikoc_body_text(page):
    try:
        return clean_text(page.locator("body").inner_text(timeout=3000))
    except Exception:
        return ""


def kvikoc_logged_in(page):
    text = kvikoc_body_text(page).lower()
    return "kvikoc" in text and ("vælg sælger" in text or "vaelg sælger" in text or "ordresøgning" in text)


def kvikoc_login(page, args, debug_dir):
    page.goto(KVIKOC_URL, wait_until="domcontentloaded", timeout=60000)
    kvikoc_wait(page, 10000)

    if kvikoc_logged_in(page):
        return {"success": True, "stage": "kvikoc_login_cached"}

    try:
        page.locator("#username").fill(args.username, timeout=15000)
        page.locator("#password").fill(args.password, timeout=15000)
        page.evaluate(
            """() => {
                const ok = document.querySelector('input[name="pf.ok"]');
                if (ok) ok.value = 'clicked';
                if (document.forms.length) document.forms[0].submit();
            }"""
        )
    except Exception as exc:
        save_debug_screenshot(page, debug_dir, "kvikoc_login_fields_failed.png", force=True)
        write_debug_text(debug_dir / "kvikoc_login_fields_failed.html", page.content(), force=True)
        return {
            "success": False,
            "stage": "kvikoc_login",
            "error": f"Kunne ikke udfylde KvikOC-login: {exc}",
            "debugDir": str(debug_dir),
        }

    try:
        page.wait_for_selector("#salesmanForm1\\:salesman", timeout=45000)
    except Exception:
        save_debug_screenshot(page, debug_dir, "kvikoc_after_login_failed.png", force=True)
        write_debug_text(debug_dir / "kvikoc_after_login_failed.html", page.content(), force=True)
        return {
            "success": False,
            "stage": "kvikoc_login",
            "error": "KvikOC-login lykkedes ikke, eller sælgerlisten blev ikke fundet.",
            "debugDir": str(debug_dir),
        }

    return {"success": True, "stage": "kvikoc_login"}


def kvikoc_select_seller(page, args, debug_dir):
    seller_name = clean_text(getattr(args, "seller_name", "")) or "Victor K"
    seller_code = clean_text(getattr(args, "seller_code", ""))
    if not seller_code:
        return {
            "success": False,
            "stage": "kvikoc_seller",
            "error": "Sælgerkode mangler.",
        }

    try:
        page.wait_for_selector("#salesmanForm1\\:salesman", timeout=30000)
        option_value = page.eval_on_selector(
            "#salesmanForm1\\:salesman",
            r"""(select, sellerName) => {
                const needle = (sellerName || '').toLowerCase().replace(/\./g, '').trim();
                for (const option of Array.from(select.options)) {
                    const text = (option.textContent || '').toLowerCase().replace(/\./g, '').trim();
                    if (text.includes(needle)) return option.value;
                }
                return '';
            }""",
            seller_name,
        )
        if not option_value:
            return {
                "success": False,
                "stage": "kvikoc_seller",
                "error": f"Kunne ikke finde sælgeren '{seller_name}' i KvikOC.",
            }

        selected_value = page.eval_on_selector("#salesmanForm1\\:salesman", "el => el.value")
        if selected_value != option_value:
            page.select_option("#salesmanForm1\\:salesman", value=option_value)
            kvikoc_wait(page, 5000)

        password_box = page.locator("#j_idt331\\:inputId")
        if password_box.count() > 0 and password_box.first.is_visible(timeout=2000):
            password_box.first.fill(seller_code, timeout=10000)
            page.locator("#j_idt331\\:buttontValidate").click(timeout=10000)
            kvikoc_wait(page, 6000)

        try:
            page.wait_for_selector("#accordPanel\\:0\\:searchCardForm\\:cvrNr:not([disabled]), #accordPanel\\:0\\:searchCardForm\\:telNr:not([disabled])", timeout=30000)
        except Exception:
            save_debug_screenshot(page, debug_dir, "kvikoc_seller_failed.png", force=True)
            write_debug_text(debug_dir / "kvikoc_seller_failed.html", page.content(), force=True)
            return {
                "success": False,
                "stage": "kvikoc_seller",
                "error": "KvikOC accepterede ikke sælgerlogin, eller søgefelterne blev ikke aktive.",
                "debugDir": str(debug_dir),
            }

        return {"success": True, "stage": "kvikoc_seller"}
    except Exception as exc:
        save_debug_screenshot(page, debug_dir, "kvikoc_seller_exception.png", force=True)
        write_debug_text(debug_dir / "kvikoc_seller_exception.html", page.content(), force=True)
        return {
            "success": False,
            "stage": "kvikoc_seller",
            "error": f"KvikOC sælgerlogin fejlede: {exc}",
            "debugDir": str(debug_dir),
        }


def kvikoc_extract_customer_name(page):
    try:
        return page.evaluate(
            r"""() => {
                const text = document.body ? document.body.innerText : '';
                const match = text.match(/Kundens navn:\s*([^\n\r]+)/i);
                if (!match) return '';
                return (match[1] || '').replace(/\s*Kundetype:.*/i, '').trim();
            }"""
        )
    except Exception:
        return ""


def kvikoc_loading_active(page):
    try:
        return page.evaluate(
            r"""() => Array.from(document.querySelectorAll('.ui-blockui-content'))
                .some(el => {
                    const style = window.getComputedStyle(el);
                    const opacity = Number(style.opacity || '1');
                    return style.display !== 'none'
                        && style.visibility !== 'hidden'
                        && opacity > 0.05
                        && /loading/i.test(el.innerText || el.textContent || '');
                })"""
        )
    except Exception:
        return False


def kvikoc_wait_until_idle(page, timeout=240000):
    try:
        page.wait_for_function(
            r"""() => !Array.from(document.querySelectorAll('.ui-blockui-content'))
                .some(el => {
                    const style = window.getComputedStyle(el);
                    const opacity = Number(style.opacity || '1');
                    return style.display !== 'none'
                        && style.visibility !== 'hidden'
                        && opacity > 0.05
                        && /loading/i.test(el.innerText || el.textContent || '');
                })""",
            timeout=timeout,
        )
        return True
    except Exception:
        return False


def kvikoc_wait_for_search_result(page):
    try:
        page.wait_for_function(
            """() => {
                const body = document.body ? document.body.innerText : '';
                const loading = Array.from(document.querySelectorAll('.ui-blockui-content'))
                    .some(el => {
                        const style = window.getComputedStyle(el);
                        const opacity = Number(style.opacity || '1');
                        return style.display !== 'none'
                            && style.visibility !== 'hidden'
                            && opacity > 0.05
                            && /loading/i.test(el.innerText || el.textContent || '');
                    });
                if (loading) return false;
                return body.includes('Kundens oplysninger')
                    || body.includes('Vis Alle Abonnementer')
                    || body.includes('Hent Kunde')
                    || body.includes('No records found')
                    || body.includes('Ingen poster');
            }""",
            timeout=240000,
        )
    except Exception:
        pass
    kvikoc_wait_until_idle(page, timeout=30000)
    kvikoc_wait(page, 1000)


def kvikoc_search_has_result(page):
    try:
        return bool(
            page.evaluate(
                r"""() => {
                    const text = document.body ? document.body.innerText : '';
                    return /Kundens oplysninger|Kundens navn:|Hent Kunde|Vis Alle Abonnementer/i.test(text)
                        || !!document.querySelector('div[id$="hentkunde2"], div[id$="hentkunde"]')
                        || Array.from(document.querySelectorAll('a'))
                            .some(a => (a.id || '').endsWith('bulkcvrcomman'));
                }"""
            )
        )
    except Exception:
        return False


def kvikoc_reload_if_search_stalled(page):
    if kvikoc_search_has_result(page):
        return
    try:
        page.reload(wait_until="domcontentloaded", timeout=60000)
        kvikoc_wait_until_idle(page, timeout=180000)
        kvikoc_wait(page, 2500)
    except Exception:
        pass


def kvikoc_submit_search(page):
    search_button = page.locator("#accordPanel\\:0\\:searchCardForm\\:searchId")
    try:
        search_button.click(timeout=10000, force=True)
    except Exception:
        pass
    try:
        search_button.evaluate("(button) => button.click()", timeout=10000)
    except Exception:
        pass


def kvikoc_search(page, args, debug_dir):
    cvr = re.sub(r"\D+", "", clean_text(getattr(args, "cvr", "")))
    phone = re.sub(r"\D+", "", clean_text(getattr(args, "phone", "")))

    if not cvr and not phone:
        return {
            "success": False,
            "stage": "kvikoc_input",
            "error": "Indtast CVR-nummer eller mobilnummer.",
        }

    try:
        cvr_field = page.locator("#accordPanel\\:0\\:searchCardForm\\:cvrNr")
        phone_field = page.locator("#accordPanel\\:0\\:searchCardForm\\:telNr")

        if cvr:
            cvr_field.fill(cvr, timeout=10000)
            if phone_field.count() > 0:
                phone_field.fill("", timeout=5000)
        else:
            phone_field.fill(phone, timeout=10000)
            if cvr_field.count() > 0:
                cvr_field.fill("", timeout=5000)

        kvikoc_submit_search(page)
        kvikoc_wait_for_search_result(page)
    except Exception as exc:
        save_debug_screenshot(page, debug_dir, "kvikoc_search_failed.png", force=True)
        write_debug_text(debug_dir / "kvikoc_search_failed.html", page.content(), force=True)
        return {
            "success": False,
            "stage": "kvikoc_search",
            "error": f"KvikOC-søgning fejlede: {exc}",
            "debugDir": str(debug_dir),
        }

    return {"success": True, "stage": "kvikoc_search", "queryType": "cvr" if cvr else "phone"}


def kvikoc_search_account_number(page, args, debug_dir, account_number):
    account_number = re.sub(r"\D+", "", clean_text(account_number))
    if not account_number:
        return {
            "success": False,
            "stage": "kvikoc_account_search",
            "error": "Kundenummer mangler.",
        }

    try:
        page.goto(KVIKOC_URL, wait_until="domcontentloaded", timeout=60000)
        login_result = kvikoc_login(page, args, debug_dir)
        if not login_result.get("success"):
            return login_result

        seller_result = kvikoc_select_seller(page, args, debug_dir)
        if not seller_result.get("success"):
            return seller_result

        account_field = page.locator("#accordPanel\\:0\\:searchCardForm\\:accNr")
        account_field.fill(account_number, timeout=10000)
        kvikoc_submit_search(page)
        kvikoc_wait_for_search_result(page)
        kvikoc_reload_if_search_stalled(page)
        return {"success": True, "stage": "kvikoc_account_search", "accountNumber": account_number}
    except Exception as exc:
        save_debug_screenshot(page, debug_dir, "kvikoc_account_search_failed.png", force=True)
        write_debug_text(debug_dir / "kvikoc_account_search_failed.html", page.content(), force=True)
        return {
            "success": False,
            "stage": "kvikoc_account_search",
            "error": f"KvikOC-kundenummeropslag fejlede: {exc}",
            "debugDir": str(debug_dir),
        }


def kvikoc_search_subscriber_ref(page, args, debug_dir, subscriber_ref):
    subscriber_ref = clean_text(subscriber_ref).upper()
    if not subscriber_ref:
        return {
            "success": False,
            "stage": "kvikoc_subscriber_search",
            "error": "VK-/telefonnummer mangler.",
        }

    try:
        candidates = [subscriber_ref]
        numeric_ref = re.sub(r"^(VK|EM|EF)", "", subscriber_ref, flags=re.IGNORECASE)
        if numeric_ref and numeric_ref != subscriber_ref:
            candidates.append(numeric_ref)

        for candidate in candidates:
            page.goto(KVIKOC_URL, wait_until="domcontentloaded", timeout=60000)
            login_result = kvikoc_login(page, args, debug_dir)
            if not login_result.get("success"):
                return login_result

            seller_result = kvikoc_select_seller(page, args, debug_dir)
            if not seller_result.get("success"):
                return seller_result

            phone_field = page.locator("#accordPanel\\:0\\:searchCardForm\\:telNr")
            phone_field.fill(candidate, timeout=10000)
            kvikoc_submit_search(page)
            kvikoc_wait_for_search_result(page)
            kvikoc_reload_if_search_stalled(page)
            kvikoc_wait_until_idle(page, timeout=240000)
            if kvikoc_all_subscriptions_visible(page) or kvikoc_customer_account_links(page):
                return {"success": True, "stage": "kvikoc_subscriber_search", "subscriberRef": candidate}

        return {
            "success": False,
            "stage": "kvikoc_subscriber_search",
            "error": f"KvikOC fandt ingen resultater for {subscriber_ref}.",
            "debugDir": str(debug_dir),
        }
    except Exception as exc:
        save_debug_screenshot(page, debug_dir, "kvikoc_subscriber_search_failed.png", force=True)
        write_debug_text(debug_dir / "kvikoc_subscriber_search_failed.html", page.content(), force=True)
        return {
            "success": False,
            "stage": "kvikoc_subscriber_search",
            "error": f"KvikOC-VK-opslag fejlede: {exc}",
            "debugDir": str(debug_dir),
        }


def kvikoc_all_subscriptions_visible(page):
    try:
        return page.locator("div[id$='hentkunde2']").count() > 0
    except Exception:
        return False


def kvikoc_open_customer_account(page, debug_dir):
    if kvikoc_all_subscriptions_visible(page):
        return {"success": True, "stage": "kvikoc_customer_open"}

    customer_name = kvikoc_extract_customer_name(page).lower()

    try:
        rows = page.locator("tbody[id$='bulkCvrTable_data'] > tr:not(.ui-expanded-row-content)")
        row_count = rows.count()
        if row_count <= 0:
            return {"success": True, "stage": "kvikoc_customer_open"}

        selected_index = 0
        if customer_name:
            for index in range(row_count):
                try:
                    if customer_name in rows.nth(index).inner_text(timeout=1000).lower():
                        selected_index = index
                        break
                except Exception:
                    pass

        row = rows.nth(selected_index)
        toggler = row.locator(".ui-row-toggler").first
        if toggler.count() > 0:
            toggler.click(timeout=10000)
            try:
                page.wait_for_function(
                    """() => Array.from(document.querySelectorAll('a'))
                        .some(a => (a.id || '').endsWith('bulkcvrcomman'))""",
                    timeout=15000,
                )
            except Exception:
                kvikoc_wait(page, 5000)

        account_link_text = page.evaluate(
            """() => {
                const link = Array.from(document.querySelectorAll('a'))
                    .find(a => (a.id || '').endsWith('bulkcvrcomman'));
                return link ? (link.innerText || link.textContent || '').trim() : '';
            }"""
        )
        if account_link_text:
            page.get_by_text(account_link_text, exact=True).click(timeout=10000)
            try:
                page.wait_for_selector("div[id$='hentkunde2']", timeout=45000)
            except Exception:
                pass
            kvikoc_wait(page, 3000)

        return {"success": True, "stage": "kvikoc_customer_open"}
    except Exception as exc:
        save_debug_screenshot(page, debug_dir, "kvikoc_customer_open_failed.png", force=True)
        write_debug_text(debug_dir / "kvikoc_customer_open_failed.html", page.content(), force=True)
        return {
            "success": False,
            "stage": "kvikoc_customer_open",
            "error": f"Kunne ikke åbne kundens abonnementer i KvikOC: {exc}",
            "debugDir": str(debug_dir),
        }


def kvikoc_extract_subscription_rows(page):
    return page.evaluate(
        r"""() => {
            const rows = [];
            const roots = Array.from(document.querySelectorAll('div[id$="hentkunde2"], div[id$="hentkunde"]'));
            for (const root of roots) {
              for (const tr of Array.from(root.querySelectorAll('table tbody tr'))) {
                if (tr.classList.contains('ui-expanded-row-content')) continue;
                const cells = Array.from(tr.querySelectorAll(':scope > td'))
                    .map(td => (td.innerText || td.textContent || '').trim().replace(/\s+/g, ' '));
                if (cells.length >= 9 && cells[1]) {
                    rows.push({
                        phone: cells[1],
                        status: cells[2],
                        subscription: cells[3],
                        category: cells[4],
                        system: cells[5],
                        dealer: cells[6],
                        created: cells[7],
                        binding: cells[8],
                    });
                }
              }
            }
            return rows;
        }"""
    )


def kvikoc_expand_subscription_rows(page):
    for _ in range(3):
        try:
            clicked = page.evaluate(
                r"""() => {
                    const selectors = [
                        'div[id$="hentkunde2"] tbody[id$="hentkunde2_data"] > tr:not(.ui-expanded-row-content) .ui-row-toggler.ui-icon-circle-triangle-e',
                        'div[id$="hentkunde"] tbody[id$="hentkunde_data"] > tr:not(.ui-expanded-row-content) .ui-row-toggler.ui-icon-circle-triangle-e'
                    ];
                    const togglers = selectors.flatMap(selector => Array.from(document.querySelectorAll(selector)));
                    for (const toggler of togglers) {
                        toggler.scrollIntoView({block: 'center', inline: 'nearest'});
                        toggler.click();
                    }
                    return togglers.length;
                }"""
            )
        except Exception:
            clicked = 0

        if not clicked:
            break
        kvikoc_wait_until_idle(page, timeout=60000)
        kvikoc_wait(page, 1500)


def kvikoc_active_subscription_page(page):
    try:
        return clean_text(
            page.locator("div[id$='hentkunde2_paginator_bottom'] .ui-paginator-page.ui-state-active")
            .first.inner_text(timeout=1000)
        )
    except Exception:
        return ""


def kvikoc_collect_subscriptions(page):
    rows = []
    seen_signatures = set()

    def append_current_page():
        kvikoc_expand_subscription_rows(page)
        page_rows = kvikoc_extract_subscription_rows(page)
        signature = kvikoc_active_subscription_page(page) + "|" + "|".join(
            row.get("phone", "") for row in page_rows[:4]
        )
        if signature in seen_signatures:
            return False
        seen_signatures.add(signature)
        rows.extend(page_rows)
        return True

    try:
        expected_total = page.evaluate(
            r"""() => {
                const header = Array.from(document.querySelectorAll('h3'))
                    .map(h => h.innerText || '')
                    .find(text => text.includes('Vis Alle Abonnementer')) || '';
                const match = header.match(/\((\d+)\)/);
                return match ? Number(match[1]) : 0;
            }"""
        )
    except Exception:
        expected_total = 0

    try:
        page_labels = page.evaluate(
            """() => Array.from(
                document.querySelectorAll('div[id$="hentkunde2_paginator_bottom"] .ui-paginator-page')
            ).map(el => (el.innerText || el.textContent || '').trim()).filter(Boolean)"""
        )
    except Exception:
        page_labels = []

    if not page_labels:
        return kvikoc_extract_subscription_rows(page)

    for page_label in page_labels:
        active_page = kvikoc_active_subscription_page(page)
        if active_page != page_label:
            try:
                page.locator(
                    "div[id$='hentkunde2_paginator_bottom'] .ui-paginator-page",
                    has_text=page_label,
                ).first.click(timeout=10000)
                kvikoc_wait(page, 4000)
            except Exception:
                pass

        append_current_page()

    for _ in range(50):
        unique_so_far = {
            (row.get("phone", ""), row.get("subscription", ""), row.get("category", ""))
            for row in rows
        }
        if expected_total and len(unique_so_far) >= expected_total:
            break

        next_button = page.locator("div[id$='hentkunde2_paginator_bottom'] .ui-paginator-next").first
        try:
            if next_button.count() <= 0:
                break
            if "ui-state-disabled" in (next_button.get_attribute("class") or ""):
                break
            next_button.click(timeout=10000)
            kvikoc_wait(page, 4000)
        except Exception:
            break
        if not append_current_page():
            break

    unique_so_far = {
        (row.get("phone", ""), row.get("subscription", ""), row.get("category", ""))
        for row in rows
    }
    if expected_total and len(unique_so_far) < expected_total:
        last_button = page.locator("div[id$='hentkunde2_paginator_bottom'] .ui-paginator-last").first
        try:
            if last_button.count() > 0 and "ui-state-disabled" not in (last_button.get_attribute("class") or ""):
                last_button.click(timeout=10000)
                kvikoc_wait(page, 4000)
                append_current_page()
        except Exception:
            pass

    unique = []
    seen = set()
    for row in rows:
        key = (row.get("phone", ""), row.get("subscription", ""), row.get("category", ""))
        if key in seen:
            continue
        seen.add(key)
        unique.append(row)

    return unique


def kvikoc_build_product_counts(rows):
    counts = {}
    for row in rows:
        name = kvikoc_product_name(row)
        counts[name] = counts.get(name, 0) + 1

    return [
        {"name": name, "quantity": count}
        for name, count in sorted(counts.items(), key=lambda item: (-item[1], item[0].lower()))
    ]


def kvikoc_search_result_not_found(page):
    if kvikoc_loading_active(page):
        return False

    try:
        return page.evaluate(
            r"""() => {
                const text = document.body ? document.body.innerText : '';
                const hasNoRecordsText = /No records found|Ingen poster/i.test(text);
                const hasSubscriptionPanel = !!document.querySelector('div[id$="hentkunde2"], div[id$="hentkunde"]');
                const hasCustomerInfo = /Kundens oplysninger|Kundens navn:/i.test(text);
                const accountLinks = Array.from(document.querySelectorAll('a'))
                    .filter(a => (a.id || '').endsWith('bulkcvrcomman'))
                    .filter(a => ((a.innerText || a.textContent || '').trim()));
                const customerRows = Array.from(document.querySelectorAll('tbody[id$="bulkCvrTable_data"] > tr'))
                    .filter(tr => !tr.classList.contains('ui-expanded-row-content'))
                    .filter(tr => !/No records found|Ingen poster/i.test(tr.innerText || ''));
                return !hasSubscriptionPanel
                    && !hasCustomerInfo
                    && accountLinks.length === 0
                    && (hasNoRecordsText || customerRows.length === 0);
            }"""
        )
    except Exception:
        return False


def kvikoc_bulk_cvr_row_count(page):
    try:
        return int(
            page.evaluate(
                r"""() => Array.from(
                    document.querySelectorAll('tbody[id$="bulkCvrTable_data"] > tr:not(.ui-expanded-row-content)')
                ).filter(row => !/No records found|Ingen poster/i.test(row.innerText || row.textContent || '')).length"""
            )
        )
    except Exception:
        return 0


def kvikoc_activate_customer_account_table(page):
    if kvikoc_bulk_cvr_row_count(page) > 0:
        return True

    for label in ("CU konto", "NABS konto"):
        try:
            clicked = page.evaluate(
                r"""label => {
                    const button = Array.from(document.querySelectorAll('button'))
                        .find(btn => (btn.innerText || btn.textContent || '').trim().toLowerCase() === label.toLowerCase());
                    if (!button) return false;
                    button.scrollIntoView({block: 'center', inline: 'nearest'});
                    button.click();
                    return true;
                }""",
                label,
            )
        except Exception:
            clicked = False

        if not clicked:
            continue

        try:
            page.wait_for_function(
                r"""() => {
                    const loading = Array.from(document.querySelectorAll('.ui-blockui-content'))
                        .some(el => {
                            const style = window.getComputedStyle(el);
                            const opacity = Number(style.opacity || '1');
                            return style.display !== 'none'
                                && style.visibility !== 'hidden'
                                && opacity > 0.05
                                && /loading/i.test(el.innerText || el.textContent || '');
                        });
                    if (loading) return false;
                    return Array.from(
                        document.querySelectorAll('tbody[id$="bulkCvrTable_data"] > tr:not(.ui-expanded-row-content)')
                    ).some(row => !/No records found|Ingen poster/i.test(row.innerText || row.textContent || ''))
                        || Array.from(document.querySelectorAll('a'))
                            .some(a => (a.id || '').endsWith('bulkcvrcomman'));
                }""",
                timeout=60000,
            )
        except Exception:
            pass

        kvikoc_wait_until_idle(page, timeout=60000)
        kvikoc_wait(page, 1500)
        if kvikoc_bulk_cvr_row_count(page) > 0:
            return True

    return kvikoc_bulk_cvr_row_count(page) > 0


def kvikoc_customer_account_links(page):
    if kvikoc_all_subscriptions_visible(page):
        return []

    kvikoc_activate_customer_account_table(page)

    row_selector = (
        "div[id$='bulkCvrTable'] > div.ui-datatable-tablewrapper > table "
        "> tbody[id$='bulkCvrTable_data'] > tr:not(.ui-expanded-row-content)"
    )

    def row_has_account(row_index):
        try:
            return page.evaluate(
                """index => {
                    const row = document.querySelector(`div[id$="bulkCvrTable"] > div.ui-datatable-tablewrapper > table > tbody[id$="bulkCvrTable_data"] > tr[data-ri="${index}"]`);
                    const expanded = row && row.nextElementSibling && row.nextElementSibling.classList.contains('ui-expanded-row-content')
                        ? row.nextElementSibling
                        : null;
                    if (!expanded) return false;
                    return Array.from(expanded.querySelectorAll('a'))
                        .some(a => (a.id || '').endsWith('bulkcvrcomman') && ((a.innerText || a.textContent || '').trim()));
                }""",
                row_index,
            )
        except Exception:
            return False

    def expand_row(row_index):
        if row_has_account(row_index):
            return
        try:
            current_link_count = page.locator("a[id$='bulkcvrcomman']").count()
            clicked = page.evaluate(
                """index => {
                    const row = document.querySelector(`div[id$="bulkCvrTable"] > div.ui-datatable-tablewrapper > table > tbody[id$="bulkCvrTable_data"] > tr[data-ri="${index}"]`);
                    const toggler = row ? row.querySelector('.ui-row-toggler') : null;
                    if (!toggler) return false;
                    row.scrollIntoView({block: 'center', inline: 'nearest'});
                    toggler.click();
                    return true;
                }""",
                row_index,
            )
            if not clicked:
                return
            try:
                page.wait_for_function(
                    """data => {
                        const row = document.querySelector(`div[id$="bulkCvrTable"] > div.ui-datatable-tablewrapper > table > tbody[id$="bulkCvrTable_data"] > tr[data-ri="${data.index}"]`);
                        const expanded = row && row.nextElementSibling && row.nextElementSibling.classList.contains('ui-expanded-row-content')
                            ? row.nextElementSibling
                            : null;
                        const rowReady = expanded && Array.from(expanded.querySelectorAll('a'))
                            .some(a => (a.id || '').endsWith('bulkcvrcomman') && ((a.innerText || a.textContent || '').trim()));
                        const linkCount = Array.from(document.querySelectorAll('a'))
                            .filter(a => (a.id || '').endsWith('bulkcvrcomman')).length;
                        return rowReady || linkCount > data.linkCount;
                    }""",
                    arg={"index": row_index, "linkCount": current_link_count},
                    timeout=12000,
                )
            except Exception:
                pass
            kvikoc_wait_until_idle(page, timeout=60000)
            if not row_has_account(row_index):
                kvikoc_wait(page, 1500)
        except Exception:
            pass

    try:
        customer_name = kvikoc_extract_customer_name(page).lower()
        rows = page.locator(row_selector)
        row_count = rows.count()
        preferred_indices = []
        if customer_name:
            for index in range(row_count):
                row = rows.nth(index)
                try:
                    if customer_name not in row.inner_text(timeout=1000).lower():
                        continue
                    preferred_indices.append(index)
                except Exception:
                    pass

        if preferred_indices:
            for index in preferred_indices:
                expand_row(index)

            links = page.evaluate(
                """() => Array.from(document.querySelectorAll('a'))
                    .filter(a => (a.id || '').endsWith('bulkcvrcomman'))
                    .map(a => (a.innerText || a.textContent || '').trim())
                    .filter(Boolean)"""
            )
            unique_links = []
            seen = set()
            for link in links:
                key = clean_text(link)
                if not key or key in seen:
                    continue
                seen.add(key)
                unique_links.append(key)
            if unique_links:
                return unique_links

        target_indices = preferred_indices or list(range(row_count))
        for index in target_indices:
            row = rows.nth(index)
            try:
                row_text = row.inner_text(timeout=1000)
                if re.search(r"No records found|Ingen poster", row_text, re.IGNORECASE):
                    continue
            except Exception:
                pass

            expand_row(index)

        for index in target_indices:
            if not row_has_account(index):
                expand_row(index)

        links = page.evaluate(
            """() => Array.from(document.querySelectorAll('a'))
                .filter(a => (a.id || '').endsWith('bulkcvrcomman'))
                .map(a => (a.innerText || a.textContent || '').trim())
                .filter(Boolean)"""
        )
    except Exception:
        links = []

    unique_links = []
    seen = set()
    for link in links:
        key = clean_text(link)
        if not key or key in seen:
            continue
        seen.add(key)
        unique_links.append(key)
    return unique_links


def kvikoc_customer_account_summaries(page):
    summaries = []
    try:
        raw = page.evaluate(
            r"""() => Array.from(document.querySelectorAll('a'))
                .filter(a => (a.id || '').endsWith('bulkcvrcomman'))
                .map(a => {
                    const row = a.closest('tr');
                    const cells = row
                        ? Array.from(row.querySelectorAll(':scope > td'))
                            .map(td => (td.innerText || td.textContent || '').trim().replace(/\s+/g, ' '))
                        : [];
                    const refs = row
                        ? Array.from(row.querySelectorAll('a[id$="bulkcvrcomman2"]'))
                            .map(link => (link.innerText || link.textContent || '').trim())
                            .filter(Boolean)
                        : [];
                    const cellRefs = (cells[2] || '')
                        .split(/\s+/)
                        .map(text => text.trim())
                        .filter(text => /^(VK|EM|EF)?\d+$/i.test(text));
                    return {
                        accountNumber: (a.innerText || a.textContent || '').trim(),
                        subscriberRefs: refs.length ? refs : cellRefs,
                        status: cells[3] || '',
                        system: cells[4] || '',
                    };
                })
                .filter(item => item.accountNumber)"""
        )
    except Exception:
        raw = []

    seen = set()
    for item in raw:
        account_number = clean_text(item.get("accountNumber"))
        if not account_number or account_number in seen:
            continue
        seen.add(account_number)
        summaries.append(
            {
                "accountNumber": account_number,
                "subscriberRefs": [clean_text(ref) for ref in item.get("subscriberRefs", []) if clean_text(ref)],
                "status": clean_text(item.get("status")),
                "system": clean_text(item.get("system")),
            }
        )
    return summaries


def kvikoc_click_customer_account(page, account_number, debug_dir):
    if kvikoc_all_subscriptions_visible(page):
        return {"success": True, "stage": "kvikoc_customer_open", "customerNumber": account_number}

    try:
        clicked = page.evaluate(
            """accountNumber => {
                const wanted = String(accountNumber || '').trim();
                const accountLink = Array.from(document.querySelectorAll('a'))
                    .find(a => (a.id || '').endsWith('bulkcvrcomman')
                        && ((a.innerText || a.textContent || '').trim() === wanted));
                if (!accountLink) return false;
                const row = accountLink.closest('tr');
                const subscriberLink = row
                    ? Array.from(row.querySelectorAll('a'))
                        .find(a => (a.id || '').endsWith('bulkcvrcomman2')
                            && ((a.innerText || a.textContent || '').trim()))
                    : null;
                const link = subscriberLink || accountLink;
                link.scrollIntoView({block: 'center', inline: 'nearest'});
                link.click();
                return true;
            }""",
            account_number,
        )
        if not clicked:
            save_debug_screenshot(page, debug_dir, "kvikoc_customer_link_missing.png", force=DEBUG_ENABLED)
            write_debug_text(debug_dir / "kvikoc_customer_link_missing.html", page.content(), force=DEBUG_ENABLED)
            return {
                "success": False,
                "stage": "kvikoc_customer_open",
                "error": f"Kunne ikke åbne kundenummer {account_number} i KvikOC.",
                "debugDir": str(debug_dir),
            }

        ready = False
        try:
            page.wait_for_function(
                """() => {
                    const text = document.body ? document.body.innerText : '';
                    const loading = Array.from(document.querySelectorAll('.ui-blockui-content'))
                        .some(el => {
                            const style = window.getComputedStyle(el);
                            const opacity = Number(style.opacity || '1');
                            return style.display !== 'none'
                                && style.visibility !== 'hidden'
                                && opacity > 0.05
                                && /loading/i.test(el.innerText || el.textContent || '');
                        });
                    if (loading) return false;
                    return text.includes('Vis Alle Abonnementer')
                        || !!document.querySelector('div[id$="hentkunde2"], div[id$="hentkunde"]');
                }""",
                timeout=240000,
            )
            ready = True
        except Exception:
            pass
        kvikoc_wait_until_idle(page, timeout=60000)
        kvikoc_wait(page, 1500)
        if not ready and kvikoc_loading_active(page):
            return {
                "success": False,
                "stage": "kvikoc_customer_open",
                "error": f"KvikOC blev ved med at loade kundenummer {account_number}.",
                "debugDir": str(debug_dir),
            }
        return {"success": True, "stage": "kvikoc_customer_open", "customerNumber": account_number}
    except Exception as exc:
        save_debug_screenshot(page, debug_dir, "kvikoc_customer_open_failed.png", force=True)
        write_debug_text(debug_dir / "kvikoc_customer_open_failed.html", page.content(), force=True)
        return {
            "success": False,
            "stage": "kvikoc_customer_open",
            "error": f"Kunne ikke åbne kundens abonnementer i KvikOC: {exc}",
            "debugDir": str(debug_dir),
        }


def kvikoc_click_customer_account_resilient(page, account_number, debug_dir):
    if kvikoc_all_subscriptions_visible(page):
        return {"success": True, "stage": "kvikoc_customer_open", "customerNumber": account_number}

    for attempt in range(2):
        try:
            clicked = page.evaluate(
                """accountNumber => {
                    const wanted = String(accountNumber || '').trim();
                    const accountLink = Array.from(document.querySelectorAll('a'))
                        .find(a => (a.id || '').endsWith('bulkcvrcomman')
                            && ((a.innerText || a.textContent || '').trim() === wanted));
                    if (!accountLink) return false;
                    const row = accountLink.closest('tr');
                    const subscriberLink = row
                        ? Array.from(row.querySelectorAll('a'))
                            .find(a => (a.id || '').endsWith('bulkcvrcomman2')
                                && ((a.innerText || a.textContent || '').trim()))
                        : null;
                    const link = subscriberLink || accountLink;
                    link.scrollIntoView({block: 'center', inline: 'nearest'});
                    link.click();
                    return true;
                }""",
                account_number,
            )
            if not clicked:
                save_debug_screenshot(page, debug_dir, "kvikoc_customer_link_missing.png", force=DEBUG_ENABLED)
                write_debug_text(debug_dir / "kvikoc_customer_link_missing.html", page.content(), force=DEBUG_ENABLED)
                return {
                    "success": False,
                    "stage": "kvikoc_customer_open",
                    "error": f"Kunne ikke Ã¥bne kundenummer {account_number} i KvikOC.",
                    "debugDir": str(debug_dir),
                }

            ready = False
            try:
                page.wait_for_function(
                    """() => {
                        const text = document.body ? document.body.innerText : '';
                        const loading = Array.from(document.querySelectorAll('.ui-blockui-content'))
                            .some(el => {
                                const style = window.getComputedStyle(el);
                                const opacity = Number(style.opacity || '1');
                                return style.display !== 'none'
                                    && style.visibility !== 'hidden'
                                    && opacity > 0.05
                                    && /loading/i.test(el.innerText || el.textContent || '');
                            });
                        if (loading) return false;
                        return text.includes('Vis Alle Abonnementer')
                            || !!document.querySelector('div[id$="hentkunde2"], div[id$="hentkunde"]');
                    }""",
                    timeout=240000,
                )
                ready = True
            except Exception:
                pass

            kvikoc_wait_until_idle(page, timeout=60000)
            kvikoc_wait(page, 1500)
            if ready or kvikoc_all_subscriptions_visible(page):
                return {"success": True, "stage": "kvikoc_customer_open", "customerNumber": account_number}

            if not kvikoc_loading_active(page):
                return {"success": True, "stage": "kvikoc_customer_open", "customerNumber": account_number}

            if attempt == 0:
                try:
                    page.reload(wait_until="domcontentloaded", timeout=60000)
                    kvikoc_wait_until_idle(page, timeout=120000)
                    kvikoc_wait(page, 2500)
                    kvikoc_activate_customer_account_table(page)
                except Exception:
                    pass
        except Exception as exc:
            save_debug_screenshot(page, debug_dir, "kvikoc_customer_open_failed.png", force=True)
            write_debug_text(debug_dir / "kvikoc_customer_open_failed.html", page.content(), force=True)
            return {
                "success": False,
                "stage": "kvikoc_customer_open",
                "error": f"Kunne ikke Ã¥bne kundens abonnementer i KvikOC: {exc}",
                "debugDir": str(debug_dir),
            }

    return {
        "success": False,
        "stage": "kvikoc_customer_open",
        "error": f"KvikOC blev ved med at loade kundenummer {account_number}.",
        "debugDir": str(debug_dir),
    }


def kvikoc_open_customer_account(page, debug_dir, account_number=""):
    if account_number:
        return kvikoc_click_customer_account_resilient(page, account_number, debug_dir)

    if kvikoc_all_subscriptions_visible(page):
        return {"success": True, "stage": "kvikoc_customer_open"}

    links = kvikoc_customer_account_links(page)
    if links:
        return kvikoc_click_customer_account_resilient(page, links[0], debug_dir)

    return {"success": True, "stage": "kvikoc_customer_open"}


def kvikoc_dedupe_subscription_rows(rows):
    unique = []
    seen = set()
    for row in rows:
        key = (
            row.get("customerNumber", ""),
            row.get("phone", ""),
            row.get("subscription", ""),
            row.get("category", ""),
            row.get("system", ""),
        )
        if key in seen:
            continue
        seen.add(key)
        unique.append(row)
    return unique


def kvikoc_product_name(row):
    subscription = clean_text(row.get("subscription"))
    category = clean_text(row.get("category"))
    phone = clean_text(row.get("phone")).upper()

    if subscription.lower() == "fastnet":
        if re.fullmatch(r"(EM|EF)\d+", phone):
            return "Fiber/internetforbindelse"
        return "Fastnet"

    return subscription or category or "Ukendt"


def kvikoc_fallback_fixed_line_row(account_summary):
    account_number = clean_text(account_summary.get("accountNumber"))
    return {
        "phone": "",
        "status": clean_text(account_summary.get("status")) or "Aktivt",
        "subscription": "Fastnet",
        "category": "Fastnet",
        "system": clean_text(account_summary.get("system")),
        "dealer": "",
        "created": "",
        "binding": "",
        "customerNumber": account_number,
        "synthetic": True,
    }


def kvikoc_lookup(page, args, debug_dir):
    search_result = kvikoc_search(page, args, debug_dir)
    if not search_result.get("success"):
        return search_result

    if kvikoc_search_result_not_found(page):
        return {
            "success": True,
            "stage": "kvikoc_done",
            "notFound": True,
            "customerName": "",
            "products": [],
            "subscriptions": [],
            "totalSubscriptions": 0,
            "debugDir": str(debug_dir),
        }

    customer_accounts = []
    account_summaries = []
    for search_attempt in range(3):
        customer_accounts = kvikoc_customer_account_links(page)
        account_summaries = kvikoc_customer_account_summaries(page)
        if customer_accounts or account_summaries or kvikoc_all_subscriptions_visible(page):
            break
        if search_attempt >= 2:
            break
        try:
            page.goto(KVIKOC_URL, wait_until="domcontentloaded", timeout=60000)
            login_result = kvikoc_login(page, args, debug_dir)
            if not login_result.get("success"):
                return login_result
            seller_result = kvikoc_select_seller(page, args, debug_dir)
            if not seller_result.get("success"):
                return seller_result
            search_result = kvikoc_search(page, args, debug_dir)
            if not search_result.get("success"):
                return search_result
        except Exception:
            break
    rows = []
    customer_name = kvikoc_extract_customer_name(page)

    if account_summaries:
        opened_detail_account = False
        for summary in account_summaries:
            account_number = summary.get("accountNumber", "")
            subscriber_refs = summary.get("subscriberRefs", [])
            if not subscriber_refs:
                rows.append(kvikoc_fallback_fixed_line_row(summary))
                continue

            if opened_detail_account:
                continue

            detail_rows = []
            subscriber_search_result = kvikoc_search_subscriber_ref(page, args, debug_dir, subscriber_refs[0])
            if subscriber_search_result.get("success"):
                detail_rows = kvikoc_collect_subscriptions(page)

            if not detail_rows:
                open_result = kvikoc_open_customer_account(page, debug_dir, account_number)
                if not open_result.get("success"):
                    save_debug_screenshot(page, debug_dir, f"kvikoc_direct_open_failed_{account_number}.png", force=DEBUG_ENABLED)
                    write_debug_text(debug_dir / f"kvikoc_direct_open_failed_{account_number}.html", page.content(), force=DEBUG_ENABLED)
                    account_search_result = kvikoc_search_account_number(page, args, debug_dir, account_number)
                    if not account_search_result.get("success"):
                        return account_search_result

                    open_result = kvikoc_open_customer_account(page, debug_dir, account_number)
                    if not open_result.get("success") and not kvikoc_all_subscriptions_visible(page):
                        return open_result

                detail_rows = kvikoc_collect_subscriptions(page)

            if not detail_rows:
                save_debug_screenshot(page, debug_dir, f"kvikoc_direct_open_empty_{account_number}.png", force=DEBUG_ENABLED)
                write_debug_text(debug_dir / f"kvikoc_direct_open_empty_{account_number}.html", page.content(), force=DEBUG_ENABLED)
                account_search_result = kvikoc_search_account_number(page, args, debug_dir, account_number)
                if not account_search_result.get("success"):
                    return account_search_result
                if not kvikoc_all_subscriptions_visible(page):
                    open_result = kvikoc_open_customer_account(page, debug_dir, account_number)
                    if not open_result.get("success") and not kvikoc_all_subscriptions_visible(page):
                        return open_result
                detail_rows = kvikoc_collect_subscriptions(page)

            for row in detail_rows:
                row["customerNumber"] = account_number
                rows.append(row)

            opened_detail_account = True
            if not customer_name:
                customer_name = kvikoc_extract_customer_name(page)
    elif customer_accounts:
        account_number = customer_accounts[0]
        open_result = kvikoc_open_customer_account(page, debug_dir, account_number)
        if not open_result.get("success"):
            return open_result

        for row in kvikoc_collect_subscriptions(page):
            row["customerNumber"] = account_number
            rows.append(row)

        if not customer_name:
            customer_name = kvikoc_extract_customer_name(page)
    else:
        open_result = kvikoc_open_customer_account(page, debug_dir)
        if not open_result.get("success"):
            return open_result

        rows = kvikoc_collect_subscriptions(page)
        customer_name = kvikoc_extract_customer_name(page)

    rows = kvikoc_dedupe_subscription_rows(rows)
    products = kvikoc_build_product_counts(rows)

    if not rows and not customer_accounts and not clean_text(customer_name):
        return {
            "success": True,
            "stage": "kvikoc_done",
            "notFound": True,
            "customerName": "",
            "products": [],
            "subscriptions": [],
            "totalSubscriptions": 0,
            "debugDir": str(debug_dir),
        }

    if not rows:
        save_debug_screenshot(page, debug_dir, "kvikoc_no_subscriptions.png", force=DEBUG_ENABLED)
        write_debug_text(debug_dir / "kvikoc_no_subscriptions.html", page.content(), force=DEBUG_ENABLED)

    return {
        "success": True,
        "stage": "kvikoc_done",
        "notFound": False,
        "customerName": customer_name,
        "customerAccounts": customer_accounts,
        "products": products,
        "subscriptions": rows,
        "totalSubscriptions": len(rows),
        "debugDir": str(debug_dir),
    }


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

    parser.add_argument("--action", choices=["hours", "punch-status", "punch-toggle", "overview", "kvikoc-lookup"], default="hours")
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
    parser.add_argument("--seller-name", required=False)
    parser.add_argument("--seller-code", required=False)
    parser.add_argument("--cvr", required=False)
    parser.add_argument("--phone", required=False)

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
        args.session_state = payload.get("sessionState", args.session_state)
        args.debug = bool(payload.get("debug", args.debug))
        args.debug_dir = payload.get("debugDir", args.debug_dir)
        args.seller_name = payload.get("sellerName", args.seller_name)
        args.seller_code = payload.get("sellerCode", args.seller_code)
        args.cvr = payload.get("cvr", args.cvr)
        args.phone = payload.get("phone", args.phone)

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

    if args.action == "kvikoc-lookup" and not clean_text(args.cvr or "") and not clean_text(args.phone or ""):
        output({
            "success": False,
            "stage": "input",
            "error": "CVR-nummer eller mobilnummer mangler."
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

            if args.action == "kvikoc-lookup":
                login_result = kvikoc_login(page, args, debug_dir)
            else:
                login_result = login(page, args, debug_dir)

            if not login_result.get("success"):
                browser.close()
                output(login_result)
                return

            save_session_state(context, args)

            if args.action == "kvikoc-lookup":
                seller_result = kvikoc_select_seller(page, args, debug_dir)
                if not seller_result.get("success"):
                    browser.close()
                    output(seller_result)
                    return
                result = kvikoc_lookup(page, args, debug_dir)
            elif args.action == "hours":
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
    main()
