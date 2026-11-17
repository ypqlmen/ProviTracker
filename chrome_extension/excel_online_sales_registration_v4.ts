// ProviTrackerSalesRegistrationV4. Run one order at a time per workbook.
// Keep four-space editor formatting for exact source verification after paste.
// With no parameter this only checks the workbook; it never adds a test sale.
type SalesRegistrationItem = { key: string; productName?: string; quantity: number };
type SalesRegistrationPayload = {
    registrationId?: string; requestId?: string; worksheetName?: string; isTest?: boolean; date?: string; sellerInitials?: string; orderNumber?: string;
    cvrNumber?: string; companyName?: string; phoneNumber?: string;
    note?: string; items?: SalesRegistrationItem[]; includeMail?: boolean;
};
type Cell = string | number | boolean;
type SaleMailSnapshot = { registrationId: string; worksheetId: string; row: number; html: string; text: string };
type SaleReceipt = { success: boolean; status: string; row: number; orderNumber: string; scriptVersion: number; registrationId: string; worksheetName: string; worksheetId: string; requestId?: string; mail?: SaleMailSnapshot; mailError?: string };

// Copy the two heading rows and this registration's verified archive row.
// A retry must never mail the preview of a later sale from row 3.
function saleMailSnapshot(sheet: ExcelScript.Worksheet, headerRow: number, startCol: number, archiveRow: number, columns: number[], expected: Cell[], registrationId: string): SaleMailSnapshot {
    const count = expected.length;
    if (columns.length > 64) throw new Error("Mailudsnittet har for mange kolonner.");
    const sale = sheet.getRangeByIndexes(archiveRow, startCol, 1, count);
    const values = sale.getValues()[0];
    if (!columns.every(c => String(expected[c]) === String(values[c] ?? ""))) throw new Error("Arkivrækken er ændret. Kontrollér salget før mailen sendes.");
    const header = sheet.getRangeByIndexes(headerRow, startCol, 1, count);
    const group = headerRow > 0 ? sheet.getRangeByIndexes(headerRow - 1, startCol, 1, count) : undefined;
    const rawValues = [group?.getValues()[0] || [], header.getValues()[0], expected];
    const texts = [group?.getTexts()[0] || [], header.getTexts()[0], sale.getTexts()[0]];
    const escape = (v: string): string => v.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;").replace(/'/g, "&#39;").replace(/\r?\n/g, "<br>");
    const color = (v: string, fallback: string): string => /^#?[0-9a-f]{6}$/i.test(v || "") ? "#" + v.replace(/^#/, "") : fallback;
    const bounded = (v: number, fallback: number, max: number): number => Number.isFinite(v) && v > 0 ? Math.min(v, max) : fallback;
    const rows: string[] = [];
    for (let r = 0; r < 3; r++) {
        const cells: string[] = [];
        const actualRow = r < 2 ? headerRow + r - 1 : archiveRow;
        if (actualRow < 0) {
            rows.push("<tr>" + columns.map(() => "<td></td>").join("") + "</tr>");
            continue;
        }
        const height = bounded(sheet.getRangeByIndexes(actualRow, startCol + columns[0], 1, 1).getFormat().getRowHeight(), 16, 400);
        for (const c of columns) {
            const range = sheet.getRangeByIndexes(actualRow, startCol + c, 1, 1);
            const format = range.getFormat();
            const font = format.getFont();
            const name = /^[a-z0-9 _-]{1,80}$/i.test(font.getName() || "") ? font.getName() : "Calibri";
            const fill = color(format.getFill().getColor(), "transparent");
            const foreground = color(font.getColor(), "#000000");
            const width = bounded(format.getColumnWidth(), 48, 800);
            const size = bounded(font.getSize(), 11, 40);
            const horizontal = String(format.getHorizontalAlignment()).toLowerCase();
            const align = horizontal === "center" || horizontal === "centeracrossselection" ? "center" : horizontal === "right" ? "right" : horizontal === "left" ? "left" : typeof rawValues[r][c] === "number" ? "right" : "left";
            const vertical = String(format.getVerticalAlignment()).toLowerCase();
            const valign = vertical === "top" ? "top" : vertical === "bottom" ? "bottom" : "middle";
            const borders: string[] = [];
            const sides = [ExcelScript.BorderIndex.edgeTop, ExcelScript.BorderIndex.edgeRight, ExcelScript.BorderIndex.edgeBottom, ExcelScript.BorderIndex.edgeLeft];
            const names = ["top", "right", "bottom", "left"];
            for (let b = 0; b < sides.length; b++) {
                const edge = format.getRangeBorder(sides[b]);
                const style = String(edge.getStyle()).toLowerCase();
                const line = style === "none" ? "none" : style.includes("dot") ? "dotted" : style.includes("dash") ? "dashed" : style === "double" ? "double" : "solid";
                borders.push("border-" + names[b] + ":" + (line === "none" ? "none" : "0.75pt " + line + " " + color(edge.getColor(), "#000000")));
            }
            const css = "width:" + width + "pt;height:" + height + "pt;white-space:nowrap;padding:1px;text-align:" + align + ";vertical-align:" + valign + ";background-color:" + fill + ";color:" + foreground + ";font-family:" + name + ";font-size:" + size + "pt;font-weight:" + (font.getBold() ? "700" : "400") + ";" + borders.join(";");
            cells.push('<td style="' + css + '">' + escape(texts[r][c] || "") + "</td>");
        }
        rows.push("<tr>" + cells.join("") + "</tr>");
    }
    const html = '<table style="border-collapse:collapse;border-spacing:0;font-family:Calibri;font-size:11pt"><tbody>' + rows.join("") + "</tbody></table>";
    const text = texts.map(row => columns.map(c => row[c] || "").join("\t")).join("\n");
    if (html.length > 120000 || text.length > 40000) throw new Error("Salget er for langt til mail. Kontrollér bemærkningerne.");
    return { registrationId, worksheetId: sheet.getId(), row: archiveRow + 1, html, text };
}

function normalize(value: Cell | undefined): string {
    return String(value ?? "").toLowerCase().replace(/\s+/g, "").replace(/[.,+&/()-]/g, "");
}

// Explicit product keys. Similar names must never choose the wrong contract/hardware column.
function productHeaders(): { [key: string]: string } {
    return {
        mobil_1000gb_24: "Business GO 1000 GB 24+36 mdr.",
        mobil_1000gb_36: "Business GO 1000 GB 24+36 mdr.",
        mobil_1000gb_36_term: "Business GO 1000 GB 36 mdr. + Hardware",
        mobil_200gb_36: "Business GO 200 GB 36 mdr.",
        mbb_200_hw_6: "200 GB MBB + Hardware",
        mbb_1000_0: "1000 GB MBB u. Hardware & m. Hardware",
        mbb_1000_hw_6: "1000 GB MBB u. Hardware & m. Hardware",
        mobil_200gb_36_term: "Business Go 200 GB 36 mdr. + Hardware",
        fwa_fri_12: "5G FWA 12 mdr. (229)",
        mobil_200gb_24: "Business Go 200 GB 24 mdr.",
        mobil_1000gb_12: "Business Go 1000 GB 12 mdr.",
        mbb_200_0: "200 GB MBB",
        mobil_50gb_24: "Business Go 50 GB 24+36 mdr.",
        mobil_50gb_36: "Business Go 50 GB 24+36 mdr.",
        mobil_200gb_12: "Business GO 200 GB 12 mdr.",
        mobil_1000gb_0: "Business GO 1000 GB 0 mdr.",
        mbb_50_hw_6: "50 GB MBB + Hardware",
        fwa_fri_36: "5G FWA 36 mdr. (199)",
        mobil_25gb_36: "Business GO 25 GB 36 mdr.",
        mobil_50gb_36_term: "Business GO 50 GB 36 mdr. + Hardware",
        mobil_50gb_12: "Business GO 50 GB 12 mdr.",
        mobil_200gb_0: "Business Go 200 GB 0 mdr.",
        mbb_20_0: "20 GB MBB", mbb_50_0: "50 GB MBB",
        til_true_talk_firma: "Truetalk Firma + Agent",
        mobil_50gb_0: "Busines GO 50 GB 0 mdr.", fiber_12: "Fiber",
        mbb_20_hw_6: "20 GB MBB + Hardware",
        mobil_10gb_0_36: "Business GO DK Only 10 GB 0+36 mdr.",
        til_go_world: "Add-on", til_true_talk_kollega: "Add-on",
        til_1000gb_data: "Add-on", til_datakort: "Add-on", til_service: "Add-on"
    };
}

const fieldLabels = [
    ["Dato", "Salgsdato"], ["Initialer", "Sælgerinitialer"], ["OSE-nr", "OSE", "OSE-nummer"],
    ["Cvr nr.", "CVR", "CVR-nummer"], ["Firmanavn", "Firma", "Virksomhedsnavn"],
    ["Telefon", "Tlf.", "Telefonnummer"]
];
const noteLabels = ["Bemærkninger", "Bemærkning", "Bemaerkninger", "Noter"];
const productAliases: { [key: string]: string[] } = {
    mobil_25gb_36: ["25 GB 36 mdr.", "Mobil 25 GB 36 mdr."],
    mobil_50gb_0: ["Business GO 50 GB 0 mdr.", "Mobil 50 GB 0 mdr."],
    mobil_50gb_24: ["Business GO 50 GB 24 mdr."],
    mobil_50gb_36: ["Business GO 50 GB 36 mdr."],
    mobil_1000gb_24: ["Business GO 1000 GB 24 mdr."],
    mobil_1000gb_36: ["Business GO 1000 GB 36 mdr."]
};

function matchingColumns(headers: string[], labels: string[]): number[] {
    const keys = labels.map(value => normalize(value));
    return headers.map((h, i) => h && keys.includes(h) ? i : -1).filter(i => i >= 0);
}

function inputColumn(headers: string[], labels: string[], required: boolean): number {
    const found = matchingColumns(headers, labels);
    if (found.length > 1) throw new Error(`Flere kolonner matcher “${labels[0]}”. Kontrollér overskrifterne.`);
    if (required && found.length === 0) throw new Error(`Kolonnen “${labels[0]}” mangler i masterarket.`);
    return found[0] ?? -1;
}

type Span = { column: number; width: number };

function contiguousColumns(columns: number[]): Span[] {
    const spans: Span[] = [];
    for (const column of columns) {
        const last = spans[spans.length - 1];
        if (last && last.column + last.width === column) last.width++;
        else spans.push({ column, width: 1 });
    }
    return spans;
}

function checkWritable(sheet: ExcelScript.Worksheet, row: number, startCol: number, spans: Span[], label: string): void {
    for (const span of spans) {
        const range = sheet.getRangeByIndexes(row, startCol + span.column, 1, span.width);
        if (range.getMergedAreas() || range.getSpecialCells(ExcelScript.SpecialCellType.formulas)) {
            throw new Error(`${label} i række ${row + 1} har en formel eller en flettet celle i et felt til salgsregistrering.`);
        }
    }
}

function writeSale(sheet: ExcelScript.Worksheet, row: number, startCol: number, spans: Span[], values: Cell[], formatRow = -1): void {
    for (const span of spans) {
        const target = sheet.getRangeByIndexes(row, startCol + span.column, 1, span.width);
        if (formatRow >= 0) {
            target.copyFrom(sheet.getRangeByIndexes(formatRow, startCol + span.column, 1, span.width), ExcelScript.RangeCopyType.formats);
        }
        // Preserve leading zeros and prevent spreadsheet evaluation of customer text.
        target.setNumberFormat("@");
        target.setValues([values.slice(span.column, span.column + span.width)]);
    }
}

function required(value: string | undefined, label: string): string {
    if (typeof value !== "string" || !value.trim()) throw new Error(`${label} mangler.`);
    if (value.length > 32767) throw new Error(`${label} er for langt.`);
    return value.trim();
}

function result(status: string, row: number, orderNumber: string, registrationId: string, worksheetName: string, worksheetId: string): string {
    return JSON.stringify({ success: true, status, row, orderNumber, registrationId, worksheetName, worksheetId, scriptVersion: 4 });
}

function saleMonth(value: Cell): number {
    if (typeof value === "number") {
        if (!Number.isFinite(value) || value < 25569 || value > 2958465) throw new Error("Ugyldig dato i historikken.");
        const date = new Date(Math.round((value - 25569) * 86400000));
        return date.getUTCFullYear() * 12 + date.getUTCMonth();
    }
    const match = String(value).trim().match(/^(\d{1,2})[./-](\d{1,2})[./-](\d{2}|\d{4})$/);
    if (!match) throw new Error("Historikken indeholder en ukendt salgsdato. Kontrollér masterarket.");
    const year = Number(match[3]) + (match[3].length === 2 ? 2000 : 0);
    const month = Number(match[2]);
    const day = Number(match[1]);
    const parsed = new Date(Date.UTC(year, month - 1, day));
    if (parsed.getUTCFullYear() !== year || parsed.getUTCMonth() !== month - 1 || parsed.getUTCDate() !== day) throw new Error("Ugyldig dato i historikken.");
    return year * 12 + month - 1;
}

function registerSale(workbook: ExcelScript.Workbook, payloadJson: string = '{"isTest":true}'): string {
    const payload = JSON.parse(payloadJson) as SalesRegistrationPayload;
    if (!payload || typeof payload !== "object" || Array.isArray(payload)) throw new Error("Ugyldig salgsregistrering.");
    // Pin the selected sheet before checking layout or writing any cells.
    const active = workbook.getActiveWorksheet();
    const requestedName = payload.worksheetName || (payload.isTest === true ? active.getName() : "");
    if (!requestedName || requestedName === "_ProviTrackerReg") throw new Error("Registreringen mangler den valgte arkfane.");
    const sheet = workbook.getWorksheet(requestedName);
    if (!sheet || sheet.getId() !== active.getId()) throw new Error("Den valgte arkfane er skiftet. Vælg salgets oprindelige arkfane, og prøv igen.");
    const worksheetName = sheet.getName(), worksheetId = sheet.getId();
    const used = sheet.getUsedRange(true);
    if (!used) throw new Error("Masterarket er tomt.");
    const values = used.getValues();
    const startRow = used.getRowIndex(), startCol = used.getColumnIndex();
    const candidates = values.map((row, i) => {
        const labels = row.map(value => normalize(value));
        return fieldLabels.every(choices => matchingColumns(labels, choices).length > 0) ? i : -1;
    }).filter(i => i >= 0);
    if (candidates.length !== 1) throw new Error("Kunne ikke finde én entydig overskriftsrække med salgsoplysninger i masterarket.");
    const headerOffset = candidates[0], headerRow = startRow + headerOffset;
    const headers = values[headerOffset].map(value => normalize(value));
    const fixed = fieldLabels.map(labels => inputColumn(headers, labels, true));
    const noteCol = inputColumn(headers, noteLabels, false);
    const [dateCol, , oseCol] = fixed;
    const mapping = productHeaders();
    const columns: { [key: string]: number[] } = {};
    for (const key of Object.keys(mapping)) {
        columns[key] = matchingColumns(headers, [mapping[key], ...(productAliases[key] || [])]);
    }
    const productColumns = Object.keys(columns).filter(key => columns[key].length === 1).map(key => columns[key][0]);
    if (productColumns.length === 0) throw new Error("Masterarket har ingen entydige, kendte produktkolonner.");
    const managed = Array.from(new Set([...fixed, ...(noteCol >= 0 ? [noteCol] : []), ...productColumns])).sort((a, b) => a - b);
    const spans = contiguousColumns(managed);
    const empty = (value: Cell | undefined): boolean => String(value ?? "").trim() === "";
    // Require a separate preview row followed by a gap or a dated month marker.
    // Then identify history by OSE and date, not by a fixed row number or point class.
    const previewRow = headerRow + 1;
    const firstAfterPreview = values[headerOffset + 2] || [];
    const isMarker = (v: Cell[]): boolean => !empty(v[dateCol]) && managed.every(c => c === dateCol || empty(v[c]));
    if (!isMarker(firstAfterPreview) && managed.some(c => !empty(firstAfterPreview[c]))) {
        throw new Error("Masterarket mangler en tydelig adskillelse mellem den øverste salgsrække og historikken.");
    }
    let ledger = workbook.getWorksheet("_ProviTrackerReg");
    const journal = ledger?.getUsedRange(true)?.getValues() || [["ProviTracker registration v3"]];
    if (journal[0]?.[0] !== "ProviTracker registration v3") throw new Error("Registreringsjournalen har et ukendt format.");
    const entries = journal.map((entry, i) => String(entry[0]) === payload.registrationId ? i : -1).filter(i => i >= 1);
    if (entries.length > 1) throw new Error("Registreringsnummeret findes flere gange i journalen. Kontrollér masterarket.");
    const entry = entries.length ? journal[entries[0]] : null;
    const originalId = entry && String(entry[6] || workbook.getWorksheet("Ark1")?.getId() || "");
    if (entry && originalId !== worksheetId) throw new Error("Dette salg tilhører en anden arkfane. Vælg den oprindelige arkfane før genforsøg.");
    const reservedRow = entry?.[3] === "reserved" ? Number(entry[1]) - 1 : -1;
    const reservedMarker = entry?.[3] === "reserved" ? Number(entry[4]) - 1 : -1;
    let lastSaleMonth = -1;
    let markerTemplate = -1;
    let lastHistoryRow = -1;
    for (let i = headerOffset + 2; i < values.length; i++) {
        const at = startRow + i, v = values[i];
        if (at === reservedRow || at === reservedMarker) continue;
        if (!empty(v[oseCol])) {
            lastSaleMonth = Math.max(lastSaleMonth, saleMonth(v[dateCol]));
            lastHistoryRow = at;
        } else if (isMarker(v)) {
            // A date-only row is a month marker only if the date can be read.
            // Other date-only content still remains untouched.
            try {
                lastSaleMonth = Math.max(lastSaleMonth, saleMonth(v[dateCol]));
                markerTemplate = at;
                lastHistoryRow = at;
            } catch { continue; }
        }
    }
    if (markerTemplate < 0 || lastHistoryRow < 0) throw new Error("Masterarket mangler en læsbar månedsmarkering i historikken.");
    if (payload.isTest === true) {
        checkWritable(sheet, previewRow, startCol, spans, "Den øverste salgsrække");
        if (lastHistoryRow + 1 >= 1048576) throw new Error("Masterarket er fyldt.");
        checkWritable(sheet, lastHistoryRow + 1, startCol, spans, "Den næste historikrække");
        if (managed.some(c => !empty(values[lastHistoryRow - startRow + 1]?.[c]))) {
            throw new Error("Den næste historikrække indeholder allerede data i et salgsfelt.");
        }
        return result("checked", 0, "", "", worksheetName, worksheetId);
    }

    const orderNumber = required(payload.orderNumber, "OSE-nummer");
    const date = required(payload.date, "Dato");
    if (!/^\d{2}\.\d{2}\.(\d{2}|\d{4})$/.test(date)) throw new Error("Dato skal være dd.MM.yy eller dd.MM.yyyy.");
    const row: Cell[] = headers.map(() => "");
    const metadata = [date, required(payload.sellerInitials, "Initialer"), orderNumber,
        required(payload.cvrNumber, "CVR-nummer"), required(payload.companyName, "Firmanavn"), required(payload.phoneNumber, "Telefon")];
    metadata.forEach((value, i) => { row[fixed[i]] = value; });
    if (!Array.isArray(payload.items) || payload.items.length === 0) throw new Error("Ordren mangler produkter.");
    const addonNotes: string[] = [];
    for (const item of payload.items) {
        if (!item || !Object.prototype.hasOwnProperty.call(columns, item.key)) throw new Error("Produktet findes ikke i masterarket: " + (item?.key ?? "ukendt"));
        if (!Number.isSafeInteger(item.quantity) || item.quantity <= 0 || item.quantity > 100000) throw new Error("Ugyldigt antal for " + item.key);
        const matches = columns[item.key];
        if (matches.length !== 1) {
            const issue = matches.length === 0 ? "mangler en kolonne" : "har flere mulige kolonner";
            throw new Error(`Produktet “${mapping[item.key]}” ${issue} i masterarket. Salget er ikke overført.`);
        }
        const col = matches[0];
        row[col] = Number(row[col] || 0) + item.quantity;
        if (mapping[item.key] === "Add-on") addonNotes.push(`${item.productName || item.key} x${item.quantity}`);
    }
    const note = [payload.note || "", ...addonNotes].filter(value => Boolean(value)).join(" | ");
    if (note.length > 32767) throw new Error("Bemærkninger er for lange.");
    if (note && noteCol < 0) throw new Error("Masterarket mangler en bemærkningskolonne til denne ordre.");
    if (noteCol >= 0) row[noteCol] = note;

    // Bind retries to the absolute cells and product routing, not merely to an OSE.
    const month = saleMonth(date);
    const registrationId = required(payload.registrationId, "Registreringsnummer");
    if (!/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i.test(registrationId)) throw new Error("Ugyldigt registreringsnummer.");
    const complete = (status: string, archiveRow: number): string => {
        const receipt = JSON.parse(result(status, archiveRow + 1, orderNumber, registrationId, worksheetName, worksheetId)) as SaleReceipt;
        if (payload.includeMail === true) {
            try { receipt.mail = saleMailSnapshot(sheet, headerRow, startCol, archiveRow, managed, row, registrationId); }
            catch { receipt.mailError = "Salget er registreret, men mailudsnittet kunne ikke hentes. Kontrollér masterarket og prøv mailen igen."; }
        }
        return JSON.stringify(receipt);
    };
    const fingerprint = JSON.stringify({
        headerRow, fields: fixed.map(c => startCol + c),
        products: Object.keys(columns).filter(key => columns[key].length === 1).map(key => [key, startCol + columns[key][0]]),
        cells: managed.map(c => [startCol + c, row[c]])
    });
    if (fingerprint.length > 32767) throw new Error("Salgsregistreringen er for lang.");
    // Keep earlier journals readable for the original layout, including V3.
    const legacyLayout = previewRow === 2 && startCol === 0 && noteCol >= 0
        && fieldLabels.every((labels, i) => headers[fixed[i]] === normalize(labels[0]))
        && headers[noteCol] === normalize(noteLabels[0])
        && Object.keys(mapping).every(key => columns[key].length === 1 && headers[columns[key][0]] === normalize(mapping[key]))
        && headers.every((v, c) => !v || managed.includes(c));
    const same = (a: Cell | undefined, b: Cell | undefined): boolean => String(a ?? "") === String(b ?? "");
    const rangeAt = (r: number): ExcelScript.Range => sheet.getRangeByIndexes(r, startCol, 1, headers.length);
    let nextRow = lastHistoryRow + 1, markerRow = -1, markerSerial = 0;
    let ledgerRow = journal.length;
    if (entry) {
        if (!ledger) throw new Error("Registreringsjournalen mangler.");
        ledgerRow = entries[0];
        if (String(entry[2]) !== fingerprint && !(legacyLayout && String(entry[2]) === JSON.stringify(row))) {
            throw new Error("Registreringen eller arkets kolonner er ændret siden første forsøg. Kontrollér masterarket.");
        }
        nextRow = Number(entry[1]) - 1;
        markerRow = Number(entry[4]) - 1;
        markerSerial = Number(entry[5]);
        if (!Number.isSafeInteger(nextRow) || nextRow <= previewRow + 1 || nextRow >= 1048576) throw new Error("Registreringsjournalen indeholder en ugyldig række.");
        checkWritable(sheet, nextRow, startCol, spans, "Den reserverede salgsrække");
        const existing = rangeAt(nextRow).getValues()[0];
        if (managed.every(c => same(row[c], existing[c]))) {
            ledger.getRangeByIndexes(ledgerRow, 3, 1, 1).setValues([["registered"]]);
            return complete("already_registered", nextRow);
        }
        // After an interrupted write, resume matching cells in the same row.
        if (entry[3] !== "reserved" || managed.some(c => !same(existing[c], "") && !same(existing[c], row[c]))) {
            throw new Error("Den reserverede salgsrække er ændret. Kontrollér masterarket før genforsøg.");
        }
    } else {
        if (journal.slice(1).some(entry => entry[3] !== "registered")) throw new Error("Et tidligere salg afventer bekræftelse. Prøv det salg igen først.");
        if (month < lastSaleMonth) throw new Error("Salget tilhører en tidligere måned. Registrer det manuelt i den korrekte måneds historik.");
        if (month > lastSaleMonth) {
            markerRow = nextRow++;
            markerSerial = Date.UTC(Math.floor(month / 12), month % 12, 1) / 86400000 + 25569;
        }
    }
    if (nextRow >= 1048576 || ledgerRow >= 1048576) throw new Error("Masterarket er fyldt.");

    // Validate every destination before writing either the preview or the journal.
    checkWritable(sheet, previewRow, startCol, spans, "Den øverste salgsrække");
    checkWritable(sheet, nextRow, startCol, spans, "Historikrække");
    const targetValues = rangeAt(nextRow).getValues()[0];
    if (!entry && managed.some(c => !same(targetValues[c], ""))) {
        throw new Error("Den næste historikrække indeholder allerede data i et salgsfelt.");
    }
    if (markerRow >= 0) {
        if (markerRow !== nextRow - 1 || markerSerial !== Date.UTC(Math.floor(month / 12), month % 12, 1) / 86400000 + 25569) throw new Error("Journalens månedsmarkering er ugyldig.");
        checkWritable(sheet, markerRow, startCol, spans, "Månedsrækken");
        const markerValues = rangeAt(markerRow).getValues()[0];
        if (managed.some(c => !same(markerValues[c], "") && !(entry && c === dateCol && Number(markerValues[c]) === markerSerial))) {
            throw new Error("Månedsrækken indeholder allerede data i et salgsfelt.");
        }
    } else if (markerRow !== -1 || markerSerial !== 0) throw new Error("Journalens månedsmarkering er ugyldig.");
    if (!entry) {
        if (!ledger) {
            ledger = workbook.addWorksheet("_ProviTrackerReg");
            ledger.getRangeByIndexes(0, 0, 1, 8).setValues([["ProviTracker registration v3", "Sale row", "Contents", "State", "Month row", "Month date", "Worksheet ID", "Worksheet name"]]);
            ledger.setVisibility(ExcelScript.SheetVisibility.veryHidden);
        }
        const reservation = ledger.getRangeByIndexes(ledgerRow, 0, 1, 8);
        reservation.setNumberFormat("@");
        reservation.setValues([[registrationId, nextRow + 1, fingerprint, "reserved", markerRow + 1, markerSerial, worksheetId, worksheetName]]);
        const reserved = reservation.getValues()[0];
        if (String(reserved[0]) !== registrationId || Number(reserved[1]) !== nextRow + 1 || reserved[2] !== fingerprint || reserved[6] !== worksheetId) throw new Error("Excel kunne ikke bekræfte reservationen. Kontrollér journalen.");
    }
    if (!ledger) throw new Error("Registreringsjournalen mangler.");

    writeSale(sheet, previewRow, startCol, spans, row);
    const previewValues = rangeAt(previewRow).getValues()[0];
    if (!managed.every(c => same(row[c], previewValues[c]))) throw new Error("Excel kunne ikke bekræfte den øverste salgsrække.");
    if (markerRow >= 0) {
        for (const span of spans) {
            const target = sheet.getRangeByIndexes(markerRow, startCol + span.column, 1, span.width);
            target.copyFrom(sheet.getRangeByIndexes(markerTemplate, startCol + span.column, 1, span.width), ExcelScript.RangeCopyType.formats);
        }
        const markerDate = sheet.getRangeByIndexes(markerRow, startCol + dateCol, 1, 1);
        markerDate.setNumberFormat("dd.mm.yyyy");
        markerDate.setValues([[markerSerial]]);
        if (Number(markerDate.getValues()[0][0]) !== markerSerial) throw new Error("Excel kunne ikke bekræfte månedsmarkeringen.");
    }
    writeSale(sheet, nextRow, startCol, spans, row, previewRow);
    const readBack = rangeAt(nextRow).getValues()[0];

    if (!managed.every(c => same(row[c], readBack[c]))) throw new Error("Excel kunne ikke bekræfte hele registreringen. Kontrollér OSE før genforsøg.");
    ledger.getRangeByIndexes(ledgerRow, 3, 1, 1).setValues([["registered"]]);
    return complete("registered", nextRow);
}

function main(workbook: ExcelScript.Workbook, payloadJson: string = '{"isTest":true}'): string {
    const request = JSON.parse(payloadJson) as SalesRegistrationPayload;
    const response = JSON.parse(registerSale(workbook, payloadJson)) as SaleReceipt;
    response.requestId = request.requestId || "";
    const encoded = JSON.stringify(response);
    console.log("PROVITRACKER_RESULT:" + encoded);
    return encoded;
}
