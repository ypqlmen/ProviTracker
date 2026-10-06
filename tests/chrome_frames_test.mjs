import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import {workbookIdentity} from '../chrome_extension/identity.js';
const url = 'https://5rmarketing-my.sharepoint.com/a?sourcedoc={535121b9-ed93-447b-9f89-7e8d575d03e4}';
const excel = 'https://euc-excel.officeapps.live.com';
const version = JSON.parse(fs.readFileSync('chrome_extension/manifest.json','utf8')).version;
const source = fs.readFileSync('chrome_extension/background.js','utf8').replace(/^import .*;$/gm,'');
const probe = fs.readFileSync('chrome_extension/probe.js','utf8');
const event=()=>({addListener:()=>{}});
function background() {
  let native, frames=[{frameId:0,parentFrameId:-1,url}], blocked=false, ready=false,currentUrl=url;
  const results=[],timers=[],intervals=[],injected=[];
  const port={postMessage:m=>results.push(m),onDisconnect:event(),onMessage:{addListener:fn=>native=fn}};
  vm.runInNewContext(source, {workbookIdentity,URL,setTimeout:fn=>{timers.push(fn);return fn},clearTimeout:()=>{},
    setInterval:fn=>{intervals.push(fn);return fn},clearInterval:()=>{},
    chrome:{runtime:{getManifest:()=>({version}),reload:()=>{},connectNative:()=>port,onMessage:event(),onStartup:event(),onInstalled:event()},
      tabs:{query:async()=>[{id:7,url}],get:async()=>({id:7,url:currentUrl,active:false}),sendMessage:async()=>{}},
      webNavigation:{getAllFrames:async()=>frames},scripting:{executeScript:async ({target})=>{
        const id=target.frameIds[0];injected.push(id);
        if(id===1 && blocked) throw Error('Frame access denied');
        return [{frameId:id,result:{origin:id===0?'https://5rmarketing-my.sharepoint.com':excel,sheet:id!==0 && ready?'Ark1':null}}];
      }},alarms:{onAlarm:event(),create:()=>{}}}});
  return {results,timers,intervals,injected,native:m=>native(m),
    excel:()=>frames.push({frameId:1,parentFrameId:0,url:excel+'/x'}),ready:()=>ready=true,block:()=>blocked=true,
    navigate:()=>currentUrl=url.replace('535121b9','535121b8')};
}
const flush=async()=>{for(let i=0;i<20;i++)await Promise.resolve()};
const delayed=background();
await delayed.native({type:'probe',requestId:'direct',workbookUrl:url});
assert.equal(delayed.results.filter(r=>r.type==='result').length,0,'SharePoint response must not confirm Excel');
delayed.excel();delayed.ready();delayed.intervals.at(-1)();await flush();
assert.equal(delayed.results.at(-1).success,true,'read the newly created cross-origin frame directly');
assert.equal(delayed.results.at(-1).background,true);
assert.ok(delayed.injected.includes(1));
const denied=background();denied.excel();denied.block();
await denied.native({type:'probe',requestId:'denied',workbookUrl:url});denied.timers.at(-1)();
assert.match(denied.results.at(-1).error,/ikke adgang til Excel-rammen/);
const outerOnly=background();
await outerOnly.native({type:'probe',requestId:'outer',workbookUrl:url});outerOnly.timers.at(-1)();
assert.match(outerOnly.results.at(-1).error,/SharePoint/,'do not blame a missing Ark1 when only SharePoint was read');
const moved=background();moved.excel();moved.ready();moved.navigate();
await moved.native({type:'probe',requestId:'moved',workbookUrl:url});
assert.equal(moved.results.at(-1).success,false,'revalidate the current workbook after a direct frame result');
function inspect({title,label='Regneark Ark1',painted=true}) {
  const control={querySelector:()=>title===undefined?null:{getAttribute:()=>title},getAttribute:()=>label,
    getClientRects:()=>painted?[{}]:[]};
  return vm.runInNewContext(probe,{Set,document:{location:{origin:excel},querySelectorAll:selector=>selector==='[role="tab"]'?[control]:[]},
    chrome:{runtime:{onMessage:event(),sendMessage:async()=>{}}}});
}
assert.equal(inspect({title:'Ark1',painted:false}).sheet,'Ark1','actual Excel sheet-title works without background paint');
assert.equal(inspect({title:'Other',label:'Regneark Ark1'}).sheet,null,'real sheet-title takes precedence over a stale label');
assert.equal(inspect({label:'Regneark\u00a0Ark1'}).sheet,'Ark1');
assert.equal(inspect({painted:false}).sheet,null,'a hidden generic tab does not prove workbook readiness');
assert.equal(inspect({label:'Ark10'}).sheet,null,'do not accept partial sheet-name matches');
console.log('Direct cross-origin frame, late-frame, access denial, navigation and actual Excel sheet-title checks passed');
