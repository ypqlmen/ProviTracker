// ProviTrackerSalesRegistration v2. Run one order at a time per workbook.
// With no parameter this only checks the workbook; it never adds a test sale.
type SalesRegistrationItem = { key: string; productName?: string; quantity: number };
type SalesRegistrationPayload = {
  requestId?: string; isTest?: boolean; date?: string; sellerInitials?: string; orderNumber?: string;
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

function result(status: string, row: number, orderNumber: string): string {
  return JSON.stringify({ success: true, status, row, orderNumber, scriptVersion: 2 });
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

  // Retry protection includes the top preview row and the historical list.
  // Same OSE with different content is a conflict, never an overwrite or second sale.
  const relevant = [...fixed, noteCol, ...Object.values(columns)];
  let duplicateRow = -1;
  for (let i = headerOffset + 1; i < values.length; i++) {
    if (String(values[i][fixed[2]]).trim() !== orderNumber) continue;
    const equal = relevant.every(c => String(values[i][c] ?? "").trim() === String(row[c]).trim());
    if (!equal) throw new Error(`OSE ${orderNumber} findes allerede på række ${startRow + i + 1} med andet indhold. Kontrollér registreringen manuelt.`);
    duplicateRow = startRow + i + 1;
  }
  if (duplicateRow >= 0) return result("already_registered", duplicateRow, orderNumber);

  // Append after all actual values, not a blank row in the formatted preview area.
  const nextRow = startRow + values.length;
  if (nextRow >= 1048576) throw new Error("Masterarket er fyldt.");
  const target = sheet.getRangeByIndexes(nextRow, startCol, 1, headers.length);
  if (nextRow > startRow + headerOffset + 1) {
    target.copyFrom(sheet.getRangeByIndexes(nextRow - 1, startCol, 1, headers.length), ExcelScript.RangeCopyType.formats);
  }
  // Text formatting prevents OSE/CVR rounding and formula injection in customer fields.
  target.setNumberFormat("@");
  target.setValues([row]);
  const readBack = target.getValues()[0];
  if (!row.every((value, i) => String(value) === String(readBack[i]))) throw new Error("Excel kunne ikke bekræfte hele registreringen. Kontrollér OSE før genforsøg.");
  return result("registered", nextRow + 1, orderNumber);
}

function main(workbook: ExcelScript.Workbook, payloadJson: string = '{"isTest":true}'): string {
  const request = JSON.parse(payloadJson) as SalesRegistrationPayload;
  const response = JSON.parse(registerSale(workbook, payloadJson)) as { success: boolean; status: string; row: number; orderNumber: string; scriptVersion: number; requestId?: string };
  response.requestId = request.requestId || "";
  const encoded = JSON.stringify(response);
  console.log("PROVITRACKER_RESULT:" + encoded);
  return encoded;
}
