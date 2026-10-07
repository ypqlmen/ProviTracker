export function validReceipt(receipt, request) {
  if (!receipt || receipt.success!==true || receipt.scriptVersion!==3 || receipt.requestId!==request.requestId) return false;
  if (request.isTest===true) return receipt.status==='checked' && receipt.orderNumber==='' && receipt.registrationId==='' && receipt.row===0;
  return ['registered','already_registered'].includes(receipt.status) && Number.isSafeInteger(receipt.row) && receipt.row>=16
    && receipt.orderNumber===request.orderNumber && receipt.registrationId===request.registrationId;
}
