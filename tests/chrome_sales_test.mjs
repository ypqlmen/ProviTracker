import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import {workbookIdentity} from '../chrome_extension/identity.js';
import {officeStep} from '../chrome_extension/office_ui.js';
import {validReceipt} from '../chrome_extension/receipt.js';
const url='https://5rmarketing-my.sharepoint.com/a?sourcedoc={00000000-0000-4000-8000-000000000001}';
const registration={requestId:'request-a',registrationId:'sale-a',orderNumber:'BISS'};
const receipt={success:true,status:'registered',scriptVersion:3,row:123,...registration};
assert(validReceipt(receipt,registration));
for(const [key,value] of [['requestId','stale'],['registrationId','sale-b'],['orderNumber','other'],['row',3],['row',1.5],['scriptVersion',2],['success',1]]) assert(!validReceipt({...receipt,[key]:value},registration));
const source=fs.readFileSync('chrome_extension/background.js','utf8').replace(/^import .*;$/gm,'');
function fixture(){
 const calls=[],phases=[],intervals=[],timeouts=[];let native,currentUrl=url,nextReceipt=receipt;
 const event=()=>({addListener(){}});
 const port={postMessage:m=>calls.push(m),onDisconnect:event(),onMessage:{addListener:fn=>native=fn}};
 const frames=[{frameId:0,parentFrameId:-1,url},{frameId:1,parentFrameId:0,url:'https://euc-excel.officeapps.live.com/x'},
 {frameId:2,parentFrameId:1,url:'https://fa000000043.mro1cdnstorage.public.onecdn.static.microsoft/x/sdx/fa000000043/1/en-us_web/index.html#/edit'},
 {frameId:3,parentFrameId:1,url:'https://fa000000043.mro1cdnstorage.public.onecdn.static.microsoft/x/sdx/fa000000043/1/en-us_web/dialog.html'},
 {frameId:4,parentFrameId:0,url:'https://fa000000043.mro1cdnstorage.public.onecdn.static.microsoft/x/sdx/fa000000043/1/en-us_web/index.html'}];
 vm.runInNewContext(source,{workbookIdentity,officeStep,validReceipt,URL,
 setInterval:fn=>{intervals.push(fn);return fn},clearInterval(){},setTimeout:fn=>{timeouts.push(fn);return fn},clearTimeout(){},
 chrome:{runtime:{getManifest:()=>({version:'0.1.4'}),reload:()=>calls.push('reload'),connectNative:()=>port,onMessage:event(),onStartup:event(),onInstalled:event()},
 tabs:{query:async()=>[{id:7,url}],get:async()=>({url:currentUrl,active:false}),sendMessage:async()=>{},update:()=>{throw new Error('must not activate workbook')}},
 webNavigation:{getAllFrames:async()=>frames},scripting:{executeScript:async input=>{
 if(input.files)return [{result:{sheet:'Ark1',origin:'https://euc-excel.officeapps.live.com'}}];
 assert.equal(input.func,officeStep);assert(!input.target.frameIds.includes(4),'pane outside verified Excel parent excluded');
 const phase=input.args[0],request=JSON.parse(input.args[1]);phases.push(phase);
 if(phase==='open')return [{result:{ready:true}}];if(phase==='run')return [{result:{started:true}}];
 if(phase==='parameter')return [{result:{filled:true}}];if(phase==='submit')return [{result:{submitted:true}}];
 return [{result:{receipt:request.isTest?{success:true,status:'checked',scriptVersion:3,row:0,orderNumber:'',registrationId:'',requestId:request.requestId}:nextReceipt}}];
 }},alarms:{onAlarm:event(),create(){}}}});
 return {calls,phases,timeouts,native:m=>native(m),tick:async()=>{intervals.at(-1)();for(let i=0;i<20;i++)await Promise.resolve()},navigate:()=>currentUrl=url.replace('000000000001','000000000002'),receipt:r=>nextReceipt=r};
}
let f=fixture();await f.native({type:'register',requestId:'request-a',workbookUrl:url,registration});
await f.native({type:'reload-extension',version:'0.1.5'});assert(!f.calls.includes('reload'));
for(let i=0;i<5;i++)await f.tick();
assert.deepEqual(f.phases,['open','run','parameter','submit','receipt']);assert.equal(f.calls.at(-2).registrationId,'sale-a');assert.equal(f.calls.at(-2).background,true);assert.equal(f.calls.at(-1),'reload');
f=fixture();f.receipt({...receipt,registrationId:'wrong'});await f.native({type:'register',requestId:'request-a',workbookUrl:url,registration});for(let i=0;i<5;i++)await f.tick();assert(!f.calls.some(m=>m.type==='result'));f.timeouts.at(-1)();assert.equal(f.calls.at(-1).success,false);
f=fixture();await f.native({type:'setup',requestId:'setup-a',workbookUrl:url});for(let i=0;i<5;i++)await f.tick();assert.equal(f.calls.at(-1).status,'checked');
f=fixture();await f.native({type:'register',requestId:'request-a',workbookUrl:url,registration});f.navigate();await f.tick();assert.equal(f.calls.at(-1).success,false);assert.equal(f.phases.length,0);
console.log('Chrome sale transport: setup, fresh receipt identity, background tab, changed workbook, permitted pane and deferred updates passed.');
