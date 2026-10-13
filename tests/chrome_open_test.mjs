import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import {workbookIdentity} from '../chrome_extension/identity.js';
const url='https://5rmarketing-my.sharepoint.com/personal/test/_layouts/15/doc2.aspx?sourcedoc={00000000-0000-4000-8000-000000000001}';
const source=fs.readFileSync('chrome_extension/background.js','utf8').replace(/^import .*;$/gm,'');
const version=JSON.parse(fs.readFileSync('chrome_extension/manifest.json','utf8')).version;
function fixture({existing=false,pendingTab=false,duplicate=false}={}) {
  const tabs=new Map(),addresses=new Map(),stored={},results=[],created=[],injected=[],intervals=[];let native,nextId=7;
  const event=()=>({addListener(){}});
  const port={postMessage:m=>results.push(m),onDisconnect:event(),onMessage:{addListener:fn=>native=fn}};
  // Other browser tabs need no URL access and must retain focus.
  tabs.set(1,{id:1,active:true});addresses.set(1,'https://unrelated.example/');
  const add=tab=>{tabs.set(tab.id,tab);addresses.set(tab.id,tab.url || 'about:blank');return tab};
  if(existing)add({id:7,url,active:false,autoDiscardable:true});
  if(pendingTab)add({id:7,url:'about:blank',pendingUrl:url,status:'loading',active:false});
  if(duplicate)add({id:8,url,active:false});
  vm.runInNewContext(source,{workbookIdentity,URL,
    setInterval:fn=>{intervals.push(fn);return fn},clearInterval(){},setTimeout:()=>1,clearTimeout(){},
    chrome:{runtime:{getManifest:()=>({version}),reload(){},connectNative:()=>port,onMessage:event(),onStartup:event(),onInstalled:event()},
    storage:{session:{get:async key=>({[key]:stored[key]}),set:async obj=>Object.assign(stored,obj),remove:async key=>delete stored[key]}},
    tabs:{onUpdated:event(),onRemoved:event(),query:async()=>[...tabs.values()],get:async id=>{if(!tabs.has(id))throw Error('Closed');return {...tabs.get(id)}},
      create:async options=>{assert.equal(options.url,url);assert.equal(options.active,false,'opening must preserve foreground');created.push(options);return add({id:nextId++,url:'about:blank',pendingUrl:options.url,status:'loading',active:false,autoDiscardable:true})},
      update:async(id,value)=>{assert.equal(Object.keys(value).join(','),'autoDiscardable');Object.assign(tabs.get(id),value)},sendMessage:async()=>{}},
    webNavigation:{getAllFrames:async({tabId})=>[{frameId:0,parentFrameId:-1,url:addresses.get(tabId)},...(workbookIdentity(addresses.get(tabId))? [{frameId:2,parentFrameId:0,url:'https://euc-excel.officeapps.live.com/x'}]:[])]},
    scripting:{executeScript:async m=>{injected.push(m);assert.equal(workbookIdentity(addresses.get(m.target.tabId)),workbookIdentity(url),'never inject into authentication or unrelated pages');return [{result:{origin:'https://euc-excel.officeapps.live.com',sheet:'Ark1'}}]}},
    alarms:{onAlarm:event(),create(){}}}});
  return {created,injected,results,tabs,native:(id,workbookUrl=url)=>native({type:'probe',requestId:id,workbookUrl}),
    tick:async()=>{intervals.at(-1)();for(let i=0;i<40;i++)await Promise.resolve()},
    load:(id=7)=>{Object.assign(tabs.get(id),{url,status:'complete'});delete tabs.get(id).pendingUrl;addresses.set(id,url)},
    login:(id=7)=>{delete tabs.get(id).url;delete tabs.get(id).pendingUrl;tabs.get(id).status='complete';addresses.set(id,'https://login.microsoftonline.com/tenant/oauth2/authorize')},
    close:id=>{tabs.delete(id);addresses.delete(id)}};
}
let f=fixture();await f.native('open');assert.equal(f.created.length,1);assert(!f.results.some(r=>r.type==='result'),'wait until the created tab finishes navigation');assert.equal(f.injected.length,0);
f.load();await f.tick();assert.equal(f.results.at(-1).success,true);assert.equal(f.results.at(-1).background,true);assert.equal(f.tabs.get(1).active,true);
await f.native('reuse');assert.equal(f.created.length,1);assert.equal(f.results.at(-1).requestId,'reuse');
f.close(7);await f.native('reopen');assert.equal(f.created.length,2);f.load(8);await f.tick();assert.equal(f.results.at(-1).success,true);
f=fixture({existing:true});await f.native('existing');assert.equal(f.created.length,0);assert.equal(f.results.at(-1).success,true);
f=fixture({pendingTab:true});await f.native('loading');assert.equal(f.created.length,0);assert.equal(f.injected.length,0);f.load();await f.tick();assert.equal(f.results.at(-1).success,true);
f=fixture({existing:true,duplicate:true});await f.native('duplicate');assert.equal(f.created.length,0);assert.equal(f.injected.length,0);assert.match(f.results.at(-1).error,/flere gange/);
f=fixture();await f.native('invalid','https://unrelated.example/');assert.equal(f.created.length,0);assert.equal(f.injected.length,0);
f=fixture();await f.native('before-login');f.login();await f.tick();assert.equal(f.results.at(-1).status,'chrome-login-required');assert.equal(f.results.at(-1).success,false);assert.equal(f.injected.length,0);
await f.native('still-login');assert.equal(f.created.length,1,'retry must reuse its own login tab even when tabs.get redacts the URL');assert.equal(f.results.at(-1).status,'chrome-login-required');assert.equal(f.injected.length,0);
f.load();await f.native('after-login');assert.equal(f.created.length,1);assert.equal(f.results.at(-1).success,true);assert.equal(f.results.at(-1).requestId,'after-login');
console.log('Chrome automatically opens only the configured workbook in the background, waits for navigation, reuses tabs and login redirects, and resumes after login.');
