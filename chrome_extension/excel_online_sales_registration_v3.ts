// ProviTrackerSalesRegistrationV3. Run one order at a time per workbook.
// With no parameter this only checks the workbook; it never adds a test sale.
type SalesRegistrationItem = { key: string; productName?: string; quantity: number };
type SalesRegistrationPayload = {
  registrationId?: string; requestId?: string; isTest?: boolean; date?: string; sellerInitials?: string; orderNumber?: string;
  cvrNumber?: string; companyName?: string; phoneNumber?: string;
  note?: string; items?: SalesRegistrationItem[];
};
type Cell = string | number | boolean;

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

function column(headers: string[], label: string): number {
  const found = headers.map((h, i) => normalize(h) === normalize(label) ? i : -1).filter(i => i >= 0);
  if (found.length !== 1) throw new Error(`Kolonnen “${label}” mangler eller findes flere gange.`);
  return found[0];
}

function required(value: string | undefined, label: string): string {
  if (typeof value !== "string" || !value.trim()) throw new Error(`${label} mangler.`);
  if (value.length > 32767) throw new Error(`${label} er for langt.`);
  return value.trim();
}

function result(status: string, row: number, orderNumber: string, registrationId: string = ""): string {
  return JSON.stringify({ success: true, status, row, orderNumber, registrationId, scriptVersion: 3 });
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
  // Require the known sheet rather than accidentally writing to a summary/other user's sheet.
  const sheet = workbook.getWorksheet("Ark1");
  if (!sheet) throw new Error("Masterarket mangler fanen Ark1.");
  const used = sheet.getUsedRange(true);
  if (!used) throw new Error("Masterarket er tomt.");
  const texts = used.getTexts();
  const values = used.getValues();
  const startRow = used.getRowIndex();
  const startCol = used.getColumnIndex();
  const matches = texts.map((row, i) => row.some(h => normalize(h) === "dato") && row.some(h => normalize(h) === "initialer") ? i : -1).filter(i => i >= 0);
  if (matches.length !== 1) throw new Error("Kunne ikke finde én entydig overskriftsrække i masterarket.");
  const headerOffset = matches[0];
  const headers = texts[headerOffset];
  const fixed = ["Dato", "Initialer", "OSE-nr", "Cvr nr.", "Firmanavn", "Telefon"].map(h => column(headers, h));
  const noteCol = column(headers, "Bemærkninger");
  const mapping = productHeaders();
  const columns: { [key: string]: number } = {};
  Object.keys(mapping).forEach(key => { columns[key] = column(headers, mapping[key]); });
  const known = [...fixed, noteCol, ...Object.keys(columns).map(key => columns[key])];
  if (headers.some((label, index) => label.trim() && !known.includes(index))) throw new Error("Masterarket har en ukendt kolonne. Kontrollér opsætningen før registrering.");
  // Row immediately below the one header is the current sale; history starts
  // after the intentionally blank area. Check both before any workbook write.
  const previewRow = startRow + headerOffset + 1;
  if (previewRow !== 2 || startCol !== 0) throw new Error("Masterarket skal have overskrifter i række 2 og salgsrækken i række 3.");
  let lastSaleMonth = -1;
  let markerTemplate = -1;
  for (let i = headerOffset + 2; i < values.length; i++) {
    if (String(values[i][fixed[2]] ?? "").trim()) lastSaleMonth = saleMonth(values[i][fixed[0]]);
    else if (String(values[i][fixed[0]] ?? "").trim() && values[i].every((v, c) => c === fixed[0] || String(v ?? "") === "")) markerTemplate = startRow + i;
  }
  if (markerTemplate < 0 || lastSaleMonth < 0) throw new Error("Masterarket mangler den forventede historik og månedsmarkering. Kontrollér opsætningen.");
  if (payload.isTest === true) return result("checked", 0, "");

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
    const col = columns[item.key];
    row[col] = Number(row[col] || 0) + item.quantity;
    if (mapping[item.key] === "Add-on") addonNotes.push(`${item.productName || item.key} x${item.quantity}`);
  }
  row[noteCol] = [payload.note || "", ...addonNotes].filter(value => Boolean(value)).join(" | ");
  if (String(row[noteCol]).length > 32767) throw new Error("Bemærkninger er for lange.");

  // A separate reserved-row ledger binds a retry to this registration, not its OSE.
  // Persist the reservation before writing the sale. An interrupted write is retried
  // in the same row; conflicting or manually moved content requires human review.
  const month = saleMonth(date);
  const registrationId = required(payload.registrationId, "Registreringsnummer");
  if (!/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i.test(registrationId)) throw new Error("Ugyldigt registreringsnummer.");
  const fingerprint = JSON.stringify(row);
  if (fingerprint.length > 32767) throw new Error("Salgsregistreringen er for lang.");
  let ledger = workbook.getWorksheet("_ProviTrackerReg");
  const journal = ledger?.getUsedRange(true)?.getValues() || [["ProviTracker registration v3"]];
  if (journal[0]?.[0] !== "ProviTracker registration v3") throw new Error("Registreringsjournalen har et ukendt format.");
  const entries = journal.map((entry, i) => String(entry[0]) === registrationId ? i : -1).filter(i => i >= 1);
  if (entries.length > 1) throw new Error("Registreringsnummeret findes flere gange i journalen. Kontrollér masterarket.");
  let ledgerRow = journal.length;
  let nextRow = startRow + values.length;
  let markerRow = -1;
  let markerSerial = 0;
  if (entries.length === 1) {
    if (!ledger) throw new Error("Registreringsjournalen mangler.");
    ledgerRow = entries[0];
    if (String(journal[ledgerRow][2]) !== fingerprint) throw new Error("Registreringen er ændret siden første forsøg. Kontrollér masterarket.");
    nextRow = Number(journal[ledgerRow][1]) - 1;
    markerRow = Number(journal[ledgerRow][4]) - 1;
    markerSerial = Number(journal[ledgerRow][5]);
    if (!Number.isSafeInteger(nextRow) || nextRow < 15 || nextRow >= 1048576) throw new Error("Registreringsjournalen indeholder en ugyldig række.");
    const existing = sheet.getRangeByIndexes(nextRow, startCol, 1, headers.length).getValues()[0];
    if (row.every((value, i) => String(value) === String(existing[i] ?? ""))) {
      ledger.getRangeByIndexes(ledgerRow, 3, 1, 1).setValues([["registered"]]);
      return result("already_registered", nextRow + 1, orderNumber, registrationId);
    }
    if (existing.some(value => String(value ?? "") !== "") || journal[ledgerRow][3] === "registered") throw new Error("Den reserverede salgsrække er ændret. Kontrollér masterarket før genforsøg.");
  } else {
    // An unresolved reservation belongs to another sale. Do not reuse its row.
    if (journal.slice(1).some(entry => entry[3] !== "registered")) throw new Error("Et tidligere salg afventer bekræftelse. Prøv det salg igen først.");
    if (month < lastSaleMonth) throw new Error("Salget tilhører en tidligere måned. Registrer det manuelt i den korrekte måneds historik.");
    if (month > lastSaleMonth) {
      markerRow = nextRow;
      nextRow += 1;
      markerSerial = Date.UTC(Math.floor(month / 12), month % 12, 1) / 86400000 + 25569;
    }
    if (nextRow >= 1048576 || ledgerRow >= 1048576) throw new Error("Masterarket er fyldt.");
    if (!ledger) {
      ledger = workbook.addWorksheet("_ProviTrackerReg");
      ledger.getRangeByIndexes(0, 0, 1, 6).setValues([["ProviTracker registration v3", "Ark1 row", "Contents", "State", "Month row", "Month date"]]);
      ledger.setVisibility(ExcelScript.SheetVisibility.veryHidden);
    }
    const reservation = ledger.getRangeByIndexes(ledgerRow, 0, 1, 6);
    reservation.setNumberFormat("@");
    reservation.setValues([[registrationId, nextRow + 1, fingerprint, "reserved", markerRow + 1, markerSerial]]);
    const reserved = reservation.getValues()[0];
    if (String(reserved[0]) !== registrationId || Number(reserved[1]) !== nextRow + 1 || reserved[2] !== fingerprint) throw new Error("Excel kunne ikke bekræfte reservationen. Kontrollér journalen.");
  }
  if (!ledger) throw new Error("Registreringsjournalen mangler.");
  if (markerRow >= 0) {
    if (markerRow !== nextRow - 1 || markerSerial !== Date.UTC(Math.floor(month / 12), month % 12, 1) / 86400000 + 25569) throw new Error("Journalens månedsmarkering er ugyldig.");
    const marker = sheet.getRangeByIndexes(markerRow, startCol, 1, headers.length);
    const markerValues = marker.getValues()[0];
    if (markerValues.some((v, i) => String(v ?? "") !== "" && !(i === fixed[0] && Number(v) === markerSerial))) throw new Error("Månedsrækken er ændret. Kontrollér masterarket.");
  } else if (markerRow !== -1 || markerSerial !== 0) throw new Error("Journalens månedsmarkering er ugyldig.");
  // Refresh the current sale first, then archive it. Never count this preview
  // as another history row, and never refresh it for an already completed retry.
  const preview = sheet.getRangeByIndexes(previewRow, startCol, 1, headers.length);
  preview.setNumberFormat("@");
  preview.setValues([row]);
  if (!row.every((v, i) => String(v) === String(preview.getValues()[0][i]))) throw new Error("Excel kunne ikke bekræfte den øverste salgsrække.");
  if (markerRow >= 0) {
    const marker = sheet.getRangeByIndexes(markerRow, startCol, 1, headers.length);
    if (markerRow !== markerTemplate) marker.copyFrom(sheet.getRangeByIndexes(markerTemplate, startCol, 1, headers.length), ExcelScript.RangeCopyType.formats);
    const blank: Cell[] = headers.map(() => "");
    blank[fixed[0]] = markerSerial;
    marker.setValues([blank]);
    marker.getCell(0, fixed[0]).setNumberFormat("dd.mm.yyyy");
  }
  const target = sheet.getRangeByIndexes(nextRow, startCol, 1, headers.length);
  if (nextRow > startRow + headerOffset + 1) {
    target.copyFrom(sheet.getRangeByIndexes(previewRow, startCol, 1, headers.length), ExcelScript.RangeCopyType.formats);
  }
  // Text formatting prevents OSE/CVR rounding and formula injection in customer fields.
  target.setNumberFormat("@");
  target.setValues([row]);
  const readBack = target.getValues()[0];
  if (!row.every((value, i) => String(value) === String(readBack[i]))) throw new Error("Excel kunne ikke bekræfte hele registreringen. Kontrollér OSE før genforsøg.");
  ledger.getRangeByIndexes(ledgerRow, 3, 1, 1).setValues([["registered"]]);
  return result("registered", nextRow + 1, orderNumber, registrationId);
}

function main(workbook: ExcelScript.Workbook, payloadJson: string = '{"isTest":true}'): string {
  const request = JSON.parse(payloadJson) as SalesRegistrationPayload;
  const response = JSON.parse(registerSale(workbook, payloadJson)) as { success: boolean; status: string; row: number; orderNumber: string; scriptVersion: number; registrationId: string; requestId?: string };
  response.requestId = request.requestId || "";
  const encoded = JSON.stringify(response);
  console.log("PROVITRACKER_RESULT:" + encoded);
  return encoded;
}
