import assert from 'node:assert/strict';
import vm from 'node:vm';
import {readIntramanagerPage,startIntramanager} from '../chrome_extension/intramanager.js';
const profile='Sofie Eksempel',fromDate='21-09-2026',toDate='20-10-2026';
const request={operation:'hours',expectedProfile:profile,fromDate,toDate};
function read(argument=request,{account=profile,rows,empty=false,headers=true}={}) {
  rows ??=[['Salg','dato','7 t. 30 min.\n6 t. 00 min.','1','1','771,83','50,00','0,00'],
    ['Sygdom Fuldtid - 50%','dato','4 t. 00 min.','0','0','411,64','0,00','388,80'],
    ['Annullering - Beløb','dato','0 t. 00 min.','0','0','0,00','0,00','-380,40']];
  let payrollRead=false;
  const main={textContent:empty?'Intet blev fundet':'Timeseddelshistorik',querySelectorAll:selector=>selector.startsWith('thead')?
    (headers?['Projekt','StartStop','LøntimerRingetimer','Salg','Points','Grundløn','Bonus','Provision'].map(textContent=>({textContent})):[]):
    rows.map(cells=>({querySelectorAll:()=>cells.map(innerText=>({innerText}))}))};
  const result=vm.runInNewContext('('+readIntramanagerPage.toString()+')(request)',{request:argument,
    location:{origin:'https://5r.intramanager.com',pathname:'/reports/history/'},document:{querySelector:selector=>{
      if(selector.includes('img'))return {getAttribute:()=>account};
      if(selector==='#startdate')return {value:fromDate};if(selector==='#stopdate')return {value:toDate};
      payrollRead=true;return main;
    }}});
  return {result,payrollRead};
}
let value=read().result;
assert.equal(value.hours,7.5);assert.equal(value.sickPay,800.44);assert.equal(value.cancellations,-380.4);assert.equal(value.payrollSchema,2);
assert.equal(read({operation:'profile'}).payrollRead,false,'account connection must not read payroll');
assert.equal(read({...request,expectedProfile:'Other Person'}).payrollRead,false,'account mismatch stops before reading payroll');
assert.equal(read(request,{rows:[['Salg','dato','7 t. 00 min.','0','0','?','0','0']]}).result.success,undefined);
assert.equal(read(request,{headers:false}).result.waiting,true,'unrendered headers cannot confirm zero pay');
assert.equal(read(request,{rows:[],empty:true}).result.hours,0);

async function transport({permission=true,account=profile,redacted=false,login=false}={}){
  let tab={id:8,url:login?'https://5r.intramanager.com/login/index/':redacted?undefined:'https://5r.intramanager.com/reports/history/',status:'complete',active:false};
  const created=[],navigated=[],results=[],saved={intramanagerTab:8};let closed=false;
  globalThis.chrome={permissions:{contains:async query=>{assert.deepEqual(query,{origins:['https://5r.intramanager.com/*']});return permission;}},
    storage:{session:{get:async key=>({[key]:saved[key]}),set:async data=>Object.assign(saved,data)}},
    tabs:{get:async()=>tab,create:async options=>{created.push(options);tab={id:9,url:options.url,status:'complete',active:false};return tab;},update:async(id,options)=>{assert.equal(id,tab.id);assert.deepEqual(Object.keys(options),['url']);navigated.push(options.url);tab={...tab,url:options.url};}},
    scripting:{executeScript:async input=>{assert.equal(input.func,readIntramanagerPage);return [{result:read(input.args[0],{account}).result}];}}};
  const job={...request,type:'intramanager'};
  await startIntramanager(job,(j,result)=>{results.push(result);closed=true;clearInterval(j.poll);clearTimeout(j.timeout);},()=>!closed);
  if(!closed)await job.scan();
  assert(closed);return {created,navigated,results};
}
let fixture=await transport();assert.equal(fixture.results[0].success,true);assert.equal(fixture.created.length,0);assert.equal(fixture.navigated.length,1);
fixture=await transport({permission:false});assert.equal(fixture.created.length,0);assert.match(fixture.results[0].error,/tillad adgang/);
fixture=await transport({account:'Anden Konto'});assert.equal(fixture.navigated.length,0);assert.match(fixture.results[0].error,/anden Intramanager-konto/);
fixture=await transport({redacted:true});assert.equal(fixture.created.length,1,'do not overwrite an unrelated tab with a redacted URL');assert.equal(fixture.created[0].active,false);
fixture=await transport({login:true});assert.equal(fixture.navigated.length,0);assert.match(fixture.results[0].error,/Log ind/);
delete globalThis.chrome;
console.log('Intramanager Chrome: optional access, account binding, public history form, full sick pay, signed cancellations, incomplete/loading tables and dedicated background tab passed.');
