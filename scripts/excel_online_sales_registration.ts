type SalesRegistrationItem = {
  key?: string;
  productName?: string;
  quantity?: number;
  aliases?: string[];
};

type SalesRegistrationPayload = {
  isTest?: boolean;
  date?: string;
  sellerInitials?: string;
  orderNumber?: string;
  cvrNumber?: string;
  companyName?: string;
  phoneNumber?: string;
  items?: SalesRegistrationItem[];
};

function normalize(value: string | number | boolean | undefined): string {
  return String(value ?? "")
    .toLowerCase()
    .replaceAll("?", "ae")
    .replaceAll("?", "oe")
    .replaceAll("?", "aa")
    .replace(/[^a-z0-9]+/g, "");
}

function findHeaderRow(values: string[][]): number {
  const maxRows = Math.min(values.length, 25);

  for (let row = 0; row < maxRows; row++) {
    const normalized = values[row].map((value) => normalize(value));
    if (normalized.includes("dato") && normalized.includes("initialer")) {
      return row;
    }
  }

  return 0;
}

function findColumn(headers: string[], aliases: string[]): number {
  for (const alias of aliases) {
    const needle = normalize(alias);
    if (!needle) continue;

    for (let col = 0; col < headers.length; col++) {
      const header = normalize(headers[col]);
      if (!header) continue;
      if (header === needle || header.includes(needle) || needle.includes(header)) {
        return col;
      }
    }
  }

  return -1;
}

function setCell(sheet: ExcelScript.Worksheet, row: number, col: number, value: string | number | undefined) {
  if (col >= 0) {
    sheet.getCell(row, col).setValue(value ?? "");
  }
}

function main(workbook: ExcelScript.Workbook, payloadJson: string): string {
  const payload = JSON.parse(payloadJson) as SalesRegistrationPayload;

  if (payload.isTest) {
    return JSON.stringify({ success: true, message: "Mailflow test OK." });
  }

  const sheet = workbook.getWorksheets()[0];
  const usedRange = sheet.getUsedRange();
  if (!usedRange) {
    throw new Error("Masterarket er tomt.");
  }

  const textValues = usedRange.getTexts();
  const headerRowOffset = findHeaderRow(textValues);
  const headerRow = usedRange.getRowIndex() + headerRowOffset;
  const headers = textValues[headerRowOffset];
  const lastCol = headers.length;

  const dateCol = findColumn(headers, ["Dato", "Date"]);
  const initialsCol = findColumn(headers, ["Initialer", "Initials"]);
  const orderCol = findColumn(headers, ["OSE-nr", "OSE nr", "Ordre nummer", "Ordrenummer", "Ordre-ID"]);
  const cvrCol = findColumn(headers, ["Cvr nr.", "CVR-nr", "CVR nr", "CVR"]);
  const companyCol = findColumn(headers, ["Firmanavn", "Firma"]);
  const phoneCol = findColumn(headers, ["Telefon", "Tlf", "Telefonnummer"]);

  const missing = [
    ["Dato", dateCol],
    ["Initialer", initialsCol],
    ["Ordre nummer/OSE-nr", orderCol],
    ["CVR-nr.", cvrCol],
    ["Firmanavn", companyCol],
    ["Telefon", phoneCol],
  ].filter((entry) => Number(entry[1]) < 0);

  if (missing.length > 0) {
    throw new Error("Mangler kolonner i masterarket: " + missing.map((entry) => entry[0]).join(", "));
  }

  const nextRow = usedRange.getRowIndex() + usedRange.getRowCount();
  if (nextRow > headerRow + 1) {
    const previousRow = sheet.getRangeByIndexes(nextRow - 1, 0, 1, lastCol);
    const newRow = sheet.getRangeByIndexes(nextRow, 0, 1, lastCol);
    newRow.copyFrom(previousRow, ExcelScript.RangeCopyType.formats);
  }

  setCell(sheet, nextRow, dateCol, payload.date);
  setCell(sheet, nextRow, initialsCol, payload.sellerInitials);
  setCell(sheet, nextRow, orderCol, payload.orderNumber);
  setCell(sheet, nextRow, cvrCol, payload.cvrNumber);
  setCell(sheet, nextRow, companyCol, payload.companyName);
  setCell(sheet, nextRow, phoneCol, payload.phoneNumber);

  const unmatchedProducts: string[] = [];
  for (const item of payload.items ?? []) {
    const aliases = [
      item.productName ?? "",
      item.key ?? "",
      ...(item.aliases ?? []),
    ];
    const col = findColumn(headers, aliases);
    if (col < 0) {
      unmatchedProducts.push(`${item.productName ?? item.key ?? "Ukendt"} x${item.quantity ?? 1}`);
      continue;
    }

    const quantity = Number(item.quantity ?? 1);
    const existing = sheet.getCell(nextRow, col).getValue();
    const existingNumber = typeof existing === "number" ? existing : Number(existing);
    sheet.getCell(nextRow, col).setValue(Number.isFinite(existingNumber) ? existingNumber + quantity : quantity);
  }

  return JSON.stringify({
    success: true,
    row: nextRow + 1,
    unmatchedProducts,
    message: `Salgs-reg sendt og tilfoejet paa raekke ${nextRow + 1}.`,
  });
}
