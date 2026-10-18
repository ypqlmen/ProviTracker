import assert from 'node:assert/strict';
import vm from 'node:vm';
import {officeStep} from '../chrome_extension/office_ui.js';
import {traceStep,diagnosticReport,failureReason,structuralInfo} from '../chrome_extension/diagnostics.js';
const privateInfo={url:'https://private/?token=SECRET',payload:'SECRET',error:'SECRET',worksheetName:'SECRET',orderNumber:'SECRET',name:'SECRET',phase:'parameter',fields:1,matched:false,defaultTest:true};
assert.deepEqual(structuralInfo(privateInfo),{matched:false,defaultTest:true,fields:1,phase:'parameter'});
const job={startedAt:Date.now(),type:'register',phase:'parameter',trace:[]};
for(let i=0;i<500;i++)traceStep(job,'injected',privateInfo);
assert.equal(job.trace.length,1);assert.equal(job.trace[0].repeats,500);
for(let i=0;i<500;i++)traceStep(job,'injected',{fields:i,phase:'parameter'});
assert.equal(job.trace.length,120,'diagnostics must be bounded during repeated failures');
job.phaseTimeline=[{phase:'install-open',firstMs:0,lastMs:100,visits:1,url:'SECRET'},{phase:'parameter',firstMs:100,lastMs:100000,visits:2,source:'SECRET'}];
const report=diagnosticReport(job,'0.1.10');
assert.deepEqual(report.stages,[{phase:'install-open',firstMs:0,lastMs:100,visits:1},{phase:'parameter',firstMs:100,lastMs:100000,visits:2}]);
assert(!JSON.stringify(report).includes('SECRET'));
assert(report.events.every(e=>!('signature' in e)));
assert.equal(failureReason(Error('Cannot access contents of https://private/?token=SECRET')),'permission');
assert.equal(failureReason(Error('No frame with id 123')),'frame-gone');
assert.equal(failureReason(Error('Extension context invalidated')),'context-gone');
assert.equal(failureReason(Error('SECRET')),'other');
assert.deepEqual(structuralInfo({phase:'SECRET',frame:Infinity,fields:-1,matched:'SECRET'}),{});
function dialog(text) {
  const node={children:[],textContent:text,getAttribute:()=>null,closest:()=>null};
  return vm.runInNewContext('('+officeStep.toString()+')("parameter", "{}")',{
    location:{origin:'https://fa000000043.mro1cdnstorage.public.onecdn.static.microsoft',pathname:'/sdx/fa000000043/1/en-us_web/dialog.html'},
    document:{body:{textContent:text},querySelector:()=>null,querySelectorAll:selector=>selector==='span,div,p,h1,h2,h3'?[node]:[]},
    getComputedStyle:()=>({display:'block',visibility:'visible'})
  });
}
assert.equal(dialog('').waiting,'parameter-script','an existing but unrendered dialog is not another script');
assert.equal(dialog('Loading…').waiting,'parameter-script');
assert.equal(dialog('Angiv parametre for AnotherScript').controls.titleKnown,true);
assert.match(dialog('Angiv parametre for AnotherScript').error,/andet script/);
assert.match(dialog('Enter parameters for AnotherScript').error,/andet script/);
assert.equal(dialog('Angiv parametre for ProviTrackerSalesRegistrationV4').waiting,'parameter-field');
console.log('Bounded structural failure diagnostics exclude URLs, payloads, identities and raw errors.');

const heavy={startedAt:Date.now(),type:'install',phase:'parameter',phaseTimeline:job.phaseTimeline,trace:[]};
for(let i=0;i<120;i++)heavy.trace.push({event:'injected',phase:'parameter',frame:1000,ms:100000,repeats:1,matched:false,defaultTest:true,attached:true,readOnly:false,disabled:false,runEnabled:false,scriptMatched:true,fields:1,matches:1});
const bounded=diagnosticReport(heavy,'0.1.15');assert(new TextEncoder().encode(JSON.stringify(bounded)).length<=12000);assert.deepEqual(bounded.stages,report.stages,'Complete phase history survives the detailed-event byte limit');
