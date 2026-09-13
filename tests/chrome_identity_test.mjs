import assert from 'node:assert/strict';
import {workbookIdentity} from '../chrome_extension/identity.js';
const url = 'https://5rmarketing-my.sharepoint.com/a?sourcedoc=%7B535121B9-ED93-447B-9F89-7E8D575D03E4%7D';
assert.equal(workbookIdentity(url), workbookIdentity(url.replace('/a?', '/other?').replace('sourcedoc=', 'SourceDoc=') + '&action=edit'));
for (const bad of [url.replace('https:', 'http:'), url.replace('.com/', '.com.evil.test/'), url.replace('https://', 'https://user@'), 'not a URL']) assert.equal(workbookIdentity(bad), null);
assert.notEqual(workbookIdentity(url), workbookIdentity(url.replace('535121B9', '535121B8')));
console.log('Chrome workbook identity checks passed');
