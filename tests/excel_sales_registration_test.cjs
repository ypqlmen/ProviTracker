const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const {stripTypeScriptTypes} = require('node:module');
const source = fs.readFileSync(require('node:path').join(__dirname, '../scripts/excel_online_sales_registration.ts'), 'utf8');
const ctx = {ExcelScript: {RangeCopyType: {formats: 'formats'}}};
vm.createContext(ctx);
vm.runInContext(stripTypeScriptTypes(source), ctx);
// Non-customer fixture matching row 2 of the supplied MASTERARK.xlsx (A:AH).
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
  const rows = Array.from({length:115}, () => Array(customHeaders.length).fill(''));
  rows[0][6] = 'HA'; rows[1] = [...customHeaders];
  rows[2][2] = 'old-order'; rows[114][2] = 'old-order';
  let writes = 0;
  const range = (r,c,h,w) => ({
    copyFrom() { writes++; }, setNumberFormat() { writes++; },
    getValues() { return Array.from({length:h}, (_,i) => Array.from({length:w}, (_,j) => rows[r+i]?.[c+j] ?? '')); },
    setValues(v) { writes++; v.forEach((row,i) => { rows[r+i] ??= []; row.forEach((x,j) => rows[r+i][c+j] = x); }); }
  });
  const sheet = {
    getUsedRange(valuesOnly) {
      assert.equal(valuesOnly,true);
      return {getTexts:() => rows.map(r=>r.map(String)), getValues:()=>rows.map(r=>[...r]), getRowIndex:()=>0,getColumnIndex:()=>0};
    }, getRangeByIndexes:range
  };
  return {rows, writes:()=>writes, workbook:{getWorksheet:name=>name==='Ark1'?sheet:undefined}};
}
const payload = {date:'11.09.2026',sellerInitials:'TEST',orderNumber:'00123456789012345678',cvrNumber:'00123456',companyName:'=HYPERLINK("bad")',phoneNumber:'00112233',note:'ÆØÅ',items:[{key:'mobil_200gb_36_term',quantity:2},{key:'mobil_200gb_36',quantity:1}]};
const run = (f,p) => JSON.parse(ctx.main(f.workbook,JSON.stringify(p)));
let f=fixture();
assert.equal(run(f,{isTest:true}).status,'checked'); assert.equal(f.writes(),0);
assert.equal(run(f,payload).row,116);
assert.equal(f.rows[115][11],2); assert.equal(f.rows[115][8],1); assert.equal(f.rows[115][13],'');
assert.equal(f.rows[115][2],payload.orderNumber); assert.equal(f.rows[115][4],payload.companyName);
const writes=f.writes(); assert.equal(run(f,payload).status,'already_registered'); assert.equal(f.writes(),writes);
assert.throws(()=>run(f,{...payload,phoneNumber:'12345678'}),/andet indhold/); assert.equal(f.writes(),writes);
for(const items of [[{key:'unknown',quantity:1}],[{key:'mobil_200gb_36',quantity:0}],[{key:'mobil_200gb_36',quantity:1.5}],[],[{key:'__proto__',quantity:1}]]) {
  f=fixture(); assert.throws(()=>run(f,{...payload,items})); assert.equal(f.writes(),0);
}
f=fixture(headers.filter(h=>h!=='Telefon')); assert.throws(()=>run(f,{isTest:true}),/Telefon/); assert.equal(f.writes(),0);
f=fixture([...headers,'Telefon']); assert.throws(()=>run(f,payload),/Telefon/); assert.equal(f.writes(),0);
f=fixture(); run(f,{...payload,items:[{key:'mobil_1000gb_24',quantity:1},{key:'mobil_1000gb_36',quantity:2},{key:'til_1000gb_data',productName:'Tillæg 1000GB data',quantity:1}]});
assert.equal(f.rows[115][6],3); assert.equal(f.rows[115][33],1); assert.match(f.rows[115][32],/Tillæg 1000GB data x1/);
const repo=fs.readFileSync(require('node:path').join(__dirname,'../repository.h'),'utf8');
const catalog=repo.slice(repo.indexOf('void seedProducts()'),repo.indexOf('bool migrateProductCatalog()'));
for(const match of catalog.matchAll(/\{"([^"]+)",/g)) { f=fixture(); assert.equal(run(f,{...payload,items:[{key:match[1],quantity:1}]}).status,'registered',match[1]); }
console.log('Excel registration: validation, exact columns, full catalog, add-ons, append and retry tests passed.');
