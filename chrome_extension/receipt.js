export function validReceipt(receipt, request) {
  if (!receipt || receipt.success!==true || receipt.scriptVersion!==4 || receipt.requestId!==request.requestId) return false;
  if (typeof receipt.worksheetName!=='string' || !receipt.worksheetName.trim() || typeof receipt.worksheetId!=='string' || !receipt.worksheetId
      || (request.worksheetName && receipt.worksheetName!==request.worksheetName)) return false;
  if (request.isTest===true) return receipt.status==='checked' && receipt.orderNumber==='' && receipt.registrationId==='' && receipt.row===0;
  return ['registered','already_registered'].includes(receipt.status) && Number.isSafeInteger(receipt.row) && receipt.row>=16
    && receipt.orderNumber===request.orderNumber && receipt.registrationId===request.registrationId;
}
