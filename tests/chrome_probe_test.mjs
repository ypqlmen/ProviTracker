import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import {workbookIdentity} from '../chrome_extension/identity.js';
const url = 'https://5rmarketing-my.sharepoint.com/a?sourcedoc={535121b9-ed93-447b-9f89-7e8d575d03e4}';
const tab = {getAttribute:()=> 'Regneark Ark1', getClientRects:()=>[{}]};
const sheet = {querySelectorAll:selector=>selector==='[role="tab"]'?[tab]:[]};
const parent = {querySelectorAll:selector=>selector==='iframe'?[{contentDocument:sheet}]:[]};
let handler; const replies=[];
vm.runInNewContext(fs.readFileSync('chrome_extension/probe.js','utf8'), {
  document:parent, Set, chrome:{runtime:{onMessage:{addListener:fn=>handler=fn},sendMessage:async value=>replies.push(value)}}
});
handler({type:'probe',requestId:'blank-frame'});
assert.equal(replies[0].sheet,'Ark1','must inspect Excel in same-origin about:blank child');
parent.querySelectorAll=()=>[];
handler({type:'probe',requestId:'loading'});
assert.equal(replies[1].sheet,null,'loading document must reply without claiming success');

const results=[];const timers=[];const intervals=[];let nativeHandler, runtimeHandler, currentUrl=url;const probes=[];
const event=()=>({addListener:()=>{}});
const port={postMessage:value=>results.push(value),onDisconnect:event(),onMessage:{addListener:fn=>nativeHandler=fn}};
vm.runInNewContext(fs.readFileSync('chrome_extension/background.js','utf8').replace("import {workbookIdentity} from './identity.js';",''),{
  workbookIdentity,
  setTimeout:fn=>{timers.push(fn);return fn},clearTimeout:()=>{},
  setInterval:fn=>{intervals.push(fn);return fn},clearInterval:()=>{},
  chrome:{runtime:{connectNative:()=>port,onMessage:{addListener:fn=>runtimeHandler=fn},onStartup:event(),onInstalled:event()},
    tabs:{query:async()=>[{id:7,url}],get:async()=>({id:7,url:currentUrl,active:false}),sendMessage:async(id,msg)=>probes.push(msg)},
    alarms:{onAlarm:event(),create:()=>{}}}
});
const flush=async()=>{await Promise.resolve();await Promise.resolve()};
await nativeHandler({type:'probe',requestId:'fresh',workbookUrl:url});
const sender={tab:{id:7,url}};
runtimeHandler({type:'probe-result',requestId:'stale',sheet:'Ark1'},sender,()=>{});await flush();
assert.equal(results.filter(x=>x.type==='result').length,0);
runtimeHandler({type:'probe-result',requestId:'fresh',sheet:null},sender,()=>{});
intervals.at(-1)();assert.equal(probes.length,2,'retry while Excel is still rendering');
runtimeHandler({type:'probe-result',requestId:'fresh',sheet:'Ark1'},sender,()=>{});await flush();
assert.equal(results.at(-1).success,true);assert.equal(results.at(-1).background,true);
timers.at(-1)();assert.equal(results.at(-1).success,true,'late timeout must not replace success');
await nativeHandler({type:'probe',requestId:'moved',workbookUrl:url});
currentUrl=url.replace('535121b9','535121b8');
runtimeHandler({type:'probe-result',requestId:'moved',sheet:'Ark1'},sender,()=>{});await flush();
assert.equal(results.at(-1).success,false,'navigation must invalidate success');
await nativeHandler({type:'probe',requestId:'silent',workbookUrl:url});
timers.at(-1)();assert.match(results.at(-1).error,/Udvidelsen svarede ikke/);
console.log('Chrome blank-frame, delayed-render, retry and stale/navigation checks passed');
