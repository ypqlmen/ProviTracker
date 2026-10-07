import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import {workbookIdentity} from '../chrome_extension/identity.js';
const source = fs.readFileSync('chrome_extension/background.js','utf8').replace(/^import .*;$/gm,'');
const version = JSON.parse(fs.readFileSync('chrome_extension/manifest.json','utf8')).version;
const next = version.split('.'); next[next.length-1]=Number(next.at(-1))+1;
const upgraded = next.join('.');
const url = 'https://5rmarketing-my.sharepoint.com/a?sourcedoc={535121b9-ed93-447b-9f89-7e8d575d03e4}';
function background() {
  const calls=[], injected=[], timers=[];
  let native, runtime;
  const event=()=>({addListener:()=>{}});
  const port={postMessage:m=>calls.push(m),onDisconnect:event(),onMessage:{addListener:fn=>native=fn}};
  vm.runInNewContext(source, {
    workbookIdentity,URL,
    setInterval:()=>1,clearInterval:()=>{},setTimeout:fn=>{timers.push(fn);return 1},clearTimeout:()=>{},
    chrome:{runtime:{getManifest:()=>({version}),reload:()=>calls.push('reload'),connectNative:()=>port,
      onMessage:{addListener:fn=>runtime=fn},onStartup:event(),onInstalled:event()},
      storage:{session:{get:async()=>({}),set:async()=>{},remove:async()=>{}}},
    tabs:{onUpdated:event(),onRemoved:event(),update:async(id,value)=>{assert.equal(Object.keys(value).join(','),'autoDiscardable');},query:async()=>[{id:7,url}],get:async()=>({id:7,url,active:false}),sendMessage:async()=>{}},
      webNavigation:{getAllFrames:async()=>[{frameId:0,parentFrameId:-1,url},{frameId:1,parentFrameId:0,url:'https://euc-excel.officeapps.live.com/x'},
        {frameId:2,parentFrameId:0,url:'https://unrelated.example/x'},{frameId:3,parentFrameId:1,url:'about:blank'},
        {frameId:4,parentFrameId:2,url:'about:blank'}]},
      scripting:{executeScript:async m=>{injected.push(m);return []}},alarms:{onAlarm:event(),create:()=>{}}}
  });
  return {calls,injected,timers,native:m=>native(m),runtime:(m,s)=>runtime(m,s,()=>{})};
}
const flush=async()=>{await Promise.resolve();await Promise.resolve()};
const idle=background();
assert.equal(idle.calls[0].extensionVersion,version,'report the version actually running');
for (const candidate of ['0.1.1',version,version+'.0','bad','0.1.65536',null]) {
  await idle.native({type:'reload-extension',version:candidate});
}
assert.equal(idle.calls.filter(x=>x==='reload').length,0);
await idle.native({type:'reload-extension',version:upgraded});
await idle.native({type:'reload-extension',version:upgraded});
assert.equal(idle.calls.filter(x=>x==='reload').length,1,'only one reload per worker');
const busy=background();
await busy.native({type:'probe',requestId:'fresh',workbookUrl:url});
assert.deepEqual(busy.injected.map(x=>x.target.frameIds[0]),[0,1,3],'include blank Excel children but exclude unrelated-origin children');
await busy.native({type:'reload-extension',version:upgraded});
assert.equal(busy.calls.filter(x=>x==='reload').length,0,'finish current work first');
busy.runtime({type:'probe-result',requestId:'fresh',sheet:'Ark1'},{tab:{id:7,url}});
await flush();
assert.equal(busy.calls.at(-2).type,'result','send the receipt before reload');
assert.equal(busy.calls.at(-1),'reload');
busy.timers.at(-1)();
assert.equal(busy.calls.filter(x=>x==='reload').length,1);
const listeners=new Set(), replies=[];
const context=vm.createContext({document:{querySelectorAll:()=>[]},Set,
  chrome:{runtime:{onMessage:{addListener:fn=>listeners.add(fn),removeListener:fn=>listeners.delete(fn)},
    sendMessage:async m=>replies.push(m)}}});
const probe=fs.readFileSync('chrome_extension/probe.js','utf8');
vm.runInContext(probe,context);
vm.runInContext(probe,context);
assert.equal(listeners.size,1,'repeated injection replaces the previous listener');
for (const fn of listeners) fn({type:'probe',requestId:'single'});
assert.equal(replies.length,1);
console.log('Chrome version, deferred reload, receipt ordering and reinjection checks passed');
