import assert from 'node:assert/strict';
import {startOutlook,mailHash} from '../chrome_extension/outlook.js';
const storage={},ui=[],tabs=[];let allowed=true,answer={},serial=0;
globalThis.chrome={permissions:{contains:async()=>allowed},storage:{local:{get:async k=>({[k]:storage[k]}),set:async v=>Object.assign(storage,v),remove:async k=>delete storage[k]},session:{get:async k=>({[k]:storage[k]}),set:async v=>Object.assign(storage,v)}},runtime:{getURL:p=>'chrome-extension://fixture/'+p},tabs:{create:async o=>{const t={id:++serial,url:o.url,active:o.active,status:'complete'};tabs.push(t);return t},get:async id=>tabs.find(t=>t.id===id),update:async(id,values)=>Object.assign(tabs.find(t=>t.id===id),values)},webNavigation:{getAllFrames:async()=>[]},scripting:{executeScript:async o=>{const req=o.args[0];ui.push(req.operation);if(req.operation==='send'){assert.equal(storage[job.ledgerKey].state,'sending');assert.deepEqual(storage[job.ledgerKey].baseline,['fixture-previous-conversation']);}return [{result:answer[req.operation]||{}}]}}};
const mail={recipient:'receiver@example.com',registrationId:'00000000-0000-4000-8000-000000000001',subject:'salgs reg',html:'<table><tr><td>fixture</td></tr></table>',text:'fixture'};
let job,result;
async function begin(options={}){result=null;job={type:'outlook',operation:'send',requestId:'attempt-'+serial,expectedSender:'seller@example.com',mail,...options};await startOutlook(job,(j,r)=>{result=r;clearInterval(j.poll);clearTimeout(j.timeout)},()=>!result);}
storage.outlookAccount='seller@example.com';
answer={identity:{senderEmail:'seller@example.com'},'sent-baseline':{baseline:['fixture-previous-conversation']},'sent-list':{opened:'fixture-new-conversation'},'sent-message':{sent:true,messageKey:'fixture-new-conversation'},compose:{composed:true},recipient:{recipient:true},subject:{subject:true},body:{body:true},ready:{ready:true},send:{submitted:true}};
await begin();for(let i=0;i<24&&!result;i++)await job.scan();
assert.equal(result.status,'mail-sent');assert.equal(storage[job.ledgerKey].state,'sent');assert.equal(result.contentHash,await mailHash(mail,'seller@example.com'));assert(tabs.every(t=>t.active===false));
const calls=ui.length;await begin();assert.equal(result.status,'mail-already-sent');assert.equal(ui.length,calls);
await begin({mail:{...mail,recipient:'different@example.com'}});assert.equal(result.success,false);assert.equal(ui.length,calls);
const uncertainMail={...mail,registrationId:'00000000-0000-4000-8000-000000000002'};
answer['sent-message']={waiting:'sent-copy'};await begin({mail:uncertainMail});for(let i=0;i<24&&job.phase!=='sent-message';i++)await job.scan();assert.equal(job.armed,true);assert.equal(storage[job.ledgerKey].state,'sending');
// Simulated process interruption: retain durable Chrome storage, lose the job.
clearInterval(job.poll);clearTimeout(job.timeout);const before=ui.length;
await begin({mail:uncertainMail});assert.equal(result.status,'mail-uncertain');assert.equal(result.notSent,false);assert.equal(ui.length,before);
answer['sent-message']={sent:true,messageKey:'fixture-new-conversation'};await begin({mail:uncertainMail,reviewedRetry:true});for(let i=0;i<24&&!result;i++)await job.scan();assert.equal(result.status,'mail-sent');
allowed=false;await begin({mail:{...mail,registrationId:'00000000-0000-4000-8000-000000000003'}});assert.equal(result.status,'mail-permission-required');assert.equal(result.notSent,true);
console.log('Outlook: persisted pre-send guard, confirmed receipt, stable content hash, duplicate prevention, ambiguous interruption, explicit reviewed retry, permission stop and inactive dedicated tabs passed.');
