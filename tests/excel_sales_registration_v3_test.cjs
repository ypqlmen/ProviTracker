const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const {stripTypeScriptTypes} = require('node:module');
const ctx = {console:{log(){}}, ExcelScript:{RangeCopyType:{formats:'formats'},SheetVisibility:{veryHidden:'veryHidden'}}};
vm.createContext(ctx);
vm.runInContext(stripTypeScriptTypes(fs.readFileSync('chrome_extension/excel_online_sales_registration_v3.ts','utf8')),ctx);
const headers = ['Dato','Initialer','OSE-nr','Cvr nr.','Firmanavn','Telefon',
'Business GO 1000 GB 24+36 mdr.','Business GO 1000 GB 36 mdr. + Hardware',
'Business GO 200 GB 36 mdr.','200 GB MBB + Hardware','1000 GB MBB u. Hardware & m. Hardware',
'Business Go 200 GB 36 mdr. + Hardware','5G FWA 12 mdr. (229)','Business Go 200 GB 24 mdr.',
'Business Go 1000 GB 12 mdr.','200 GB MBB','Business Go 50 GB 24+36 mdr.',
'Business GO 200 GB 12 mdr.','Business GO 1000 GB 0 mdr.','50 GB MBB + Hardware',
'5G FWA 36 mdr. (199)','Business GO 25 GB 36 mdr.','Business GO 50 GB 36 mdr. + Hardware',
'Business GO 50 GB 12 mdr.','Business Go 200 GB 0 mdr.','20 GB MBB','50 GB MBB',
'Truetalk Firma + Agent','Busines GO 50 GB 0 mdr.','Fiber','20 GB MBB + Hardware',
'Business GO DK Only 10 GB 0+36 mdr.','Bemærkninger','Add-on'];

function fixture(customHeaders=headers) {
  const events=[], sheets={}; let interrupt=null;
  const rows=Array.from({length:122},()=>Array(customHeaders.length).fill(''));
  rows[0][6]='HA';rows[1]=[...customHeaders];rows[2][0]='06.10.2026';rows[2][2]='existing';
  rows[116][0]=46032;rows[121][0]='06.10.2026';rows[121][2]='existing';
  const make=(name,data)=>{
    const formats=new Map();
    const range=(r,c,h,w)=>({
      getRowIndex:()=>r,getColumnIndex:()=>c,
      getTexts:()=>range(r,c,h,w).getValues().map(row=>row.map(String)),
      getValues:()=>Array.from({length:h},(_,i)=>Array.from({length:w},(_,j)=>data[r+i]?.[c+j]??'')),
      getCell:(i,j)=>range(r+i,c+j,1,1),
      setNumberFormat:f=>{formats.set(`${r},${c}`,f);},
      copyFrom:(from,kind)=>{assert.equal(kind,'formats');events.push({name,r,copy:from.getRowIndex()});},
      setValues:v=>{ events.push({name,r,values:JSON.parse(JSON.stringify(v))});
        if(interrupt && interrupt(name,r,v,'before')) throw new Error('interrupted');
        v.forEach((row,i)=>{data[r+i]??=[];row.forEach((x,j)=>data[r+i][c+j]=x)});
        if(interrupt && interrupt(name,r,v,'after')) throw new Error('interrupted');
      }
    });
    return {data,formats,getRangeByIndexes:range,setVisibility:v=>events.push({name,visibility:v}),
      getUsedRange:()=> data.length?range(0,0,data.length,Math.max(...data.map(row=>row.length))):undefined};
  };
  sheets.Ark1=make('Ark1',rows);
  return {rows,events,sheets,interrupt:fn=>interrupt=fn,
    workbook:{getWorksheet:n=>sheets[n],addWorksheet:n=>sheets[n]=make(n,[])}};
}
const uid=n=>`00000000-0000-4000-8000-${String(n).padStart(12,'0')}`;
const payload={registrationId:uid(1),requestId:uid(2),date:'07.10.2026',sellerInitials:'TEST',orderNumber:'BISS',cvrNumber:'00123456',companyName:'=FAKE',phoneNumber:'00112233',note:'ÆØÅ',items:[{key:'mobil_25gb_36',quantity:8},{key:'til_datakort',productName:'Datakort',quantity:2}]};
const run=(f,p=payload)=>JSON.parse(ctx.main(f.workbook,JSON.stringify(p)));
let f=fixture();run(f,{isTest:true,requestId:uid(3)});assert.equal(f.events.length,0);assert.equal(Object.keys(f.sheets).length,1);
let old=JSON.stringify(f.rows.slice(3));const receipt=run(f);assert.equal(receipt.row,123);assert.equal(receipt.registrationId,payload.registrationId);assert.equal(receipt.requestId,payload.requestId);
assert.deepEqual(f.rows[2],f.rows[122]);assert.equal(JSON.stringify(f.rows.slice(3,122)),old);assert.equal(f.rows[122][21],8);assert.equal(f.rows[122][33],2);assert.match(f.rows[122][32],/Datakort x2/);
assert.deepEqual(f.events.filter(e=>e.name==='Ark1'&&e.values).map(e=>e.r),[2,122]);
assert.equal(run(f).status,'already_registered');assert.equal(f.rows.length,123);
run(f,{...payload,registrationId:uid(4)});assert.equal(f.rows.length,124,'same OSE may be a separate real sale');
const preview=JSON.stringify(f.rows[2]);run(f);assert.equal(JSON.stringify(f.rows[2]),preview,'retry completed old sale must not reset latest preview');
assert.throws(()=>run(f,{...payload,phoneNumber:'different'}),/ændret/);
f=fixture();run(f,{...payload,date:'02.11.2026'});assert.equal(f.rows[122][0],Date.UTC(2026,10,1)/86400000+25569);assert(f.rows[122].slice(1).every(v=>v===''));assert.deepEqual(f.rows[2],f.rows[123]);
assert.equal(f.sheets.Ark1.formats.get('122,0'),'dd.mm.yyyy');assert(f.events.some(e=>e.r===122&&e.copy===116));assert(f.events.some(e=>e.r===123&&e.copy===2));
run(f,{...payload,date:'02.11.2026'});assert.equal(f.rows.length,124);
run(f,{...payload,registrationId:uid(5),date:'03.11.2026'});assert.equal(f.rows.length,125,'one month marker only');
run(f,{...payload,registrationId:uid(6),date:'04.01.2027'});assert.equal(f.rows[125][0],Date.UTC(2027,0,1)/86400000+25569);assert.equal(f.rows.length,127);
for(const p of [{...payload,date:'01.09.2026'},{...payload,date:'31.11.2026'},{...payload,registrationId:'BISS'}, {...payload,items:[{key:'unknown',quantity:1}]}]) {f=fixture();assert.throws(()=>run(f,p));assert.equal(f.events.length,0);}
f=fixture([...headers,'Custom formula']);assert.throws(()=>run(f,{isTest:true}),/ukendt kolonne/);assert.equal(f.events.length,0);
// Reservation and completed archive survive interruptions, without a second row.
for(const [row,when] of [[2,'before'],[122,'after'],[123,'before'],[123,'after']]) {
 f=fixture();let once=true;f.interrupt((name,r,v,phase)=>name==='Ark1'&&r===row&&phase===when&&once?(once=false,true):false);
 assert.throws(()=>run(f,{...payload,date:'02.11.2026'}),/interrupted/);f.interrupt(null);
 run(f,{...payload,date:'02.11.2026'});assert.equal(f.rows.length,124);assert.equal(f.rows.filter(r=>r[2]==='BISS').length,2,'preview + one history row');
}
f=fixture();f.interrupt((name,r,v,phase)=>name==='Ark1'&&r===122&&phase==='before');assert.throws(()=>run(f),/interrupted/);f.interrupt(null);f.rows[122]=['manual'];const before=JSON.stringify(f.rows[2]);assert.throws(()=>run(f),/ændret/);assert.equal(JSON.stringify(f.rows[2]),before);
const repo=fs.readFileSync('repository.h','utf8');const catalog=repo.slice(repo.indexOf('void seedProducts()'),repo.indexOf('bool migrateProductCatalog()'));
for(const match of catalog.matchAll(/\{"([^"]+)",/g)) {f=fixture();assert.equal(run(f,{...payload,items:[{key:match[1],quantity:1}]}).status,'registered',match[1]);}
console.log('V3: preview-first, archive, month/year rollover, styles, same-OSE sales, recovery, stale/conflict and catalog checks passed.');
