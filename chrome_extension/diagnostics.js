// Structural metadata only: never retain page text, URLs or sale payloads.
const flags=['active','discarded','frozen','locked','matched','defaultTest','attached','readOnly','disabled','runEnabled','scriptMatched','titleKnown','codeMatched','saved','reopened'];
const counts=['frames','office','dialogs','targets','fields','matches','frame','inflight'];
const codes=['script-updated','install','install-open','install-find','install-name','install-write','install-save','install-reopen','install-verify','installed','next','open','run','parameter','submit','receipt','probe','setup','register','waiting','parameter-script','parameter-field','parameter-run','parameters','ready','started','filled','refill','submitted','error','none','result','permission','frame-gone','context-gone','other','timeout','start','tab','navigation','injection','injected','rejected','finished','workbook-frames','office-frames','scan-error'];
export function structuralInfo(data={}) {
  const result={};
  for(const key of flags) if(typeof data[key]==='boolean') result[key]=data[key];
  for(const key of counts) if(Number.isInteger(data[key]) && data[key]>=0) result[key]=Math.min(data[key],10000);
  for(const key of ['phase','outcome','reason']) if(codes.includes(data[key])) result[key]=data[key];
  return result;
}
export function traceStep(job,event,data={}) {
  if(!codes.includes(event)) return;
  const item={event,...structuralInfo(data)};
  const last=job.trace?.at(-1),signature=JSON.stringify(item);
  if(last?.signature===signature) { last.repeats++;last.lastMs=Math.max(0,Date.now()-job.startedAt);return; }
  job.trace ??=[];
  job.trace.push({...item,ms:Math.max(0,Date.now()-job.startedAt),repeats:1,signature});
  if(job.trace.length>120) job.trace.shift();
}
export function diagnosticReport(job,version) {
  const report={format:1,extensionVersion:version,kind:['probe','setup','install','register'].includes(job.type)?job.type:'other',
    ...(job.type==='install' && /^ProviTrackerAutoV4_[0-9a-f]{10}$/.test(job.scriptName || '')?{scriptVersion:4,scriptRevision:job.scriptName.slice(-10)}:{}),
    elapsedMs:Math.max(0,Date.now()-job.startedAt),phase:structuralInfo({phase:job.phase}).phase || 'probe',
    stages:(job.phaseTimeline || []).slice(0,32).filter(step=>codes.includes(step.phase)).map(step=>({phase:step.phase,...Object.fromEntries(['firstMs','lastMs','visits'].filter(key=>Number.isInteger(step[key]) && step[key]>=0).map(key=>[key,Math.min(step[key],3600000)]))})),
    events:(job.trace || []).map(({signature,...item})=>item)};
  while(report.events.length && new TextEncoder().encode(JSON.stringify(report)).length>12000)report.events.shift();
  return report;
}
export function failureReason(error) {
  const message=String(error?.message || '');
  if(/permission|cannot access|not allowed/i.test(message)) return 'permission';
  if(/frame.*removed|no frame|frame.*not found/i.test(message)) return 'frame-gone';
  if(/context.*invalid|execution context.*destroy/i.test(message)) return 'context-gone';
  return 'other';
}
