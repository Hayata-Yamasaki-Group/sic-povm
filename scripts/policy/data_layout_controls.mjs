// SPDX-License-Identifier: AGPL-3.0-or-later
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {checkData} from './data_layout.mjs';
const [root, work] = process.argv.slice(2).map(x => path.resolve(x));
fs.rmSync(work, {recursive: true, force: true});
fs.mkdirSync(work, {recursive: true});
fs.cpSync(path.join(root, 'data'), path.join(work, 'data'), {recursive: true});
checkData(work);
for (const [name, reason] of [
  ['d8/stabilizers-v2.txt', /data name/], ['d8/stabilizers_v2.txt', /internal label/],
  ['d3/stabilizers.txt', /data dimension/], ['d8/fixture.txt', /internal label/],
  ['d8/d8.txt', /data role/], ['d8/m9/c0.csv', /documented exceptions/],
  ['d8/m01/c0.csv', /data directory/], ['d8/sample/c0.csv', /data directory/],
  ['exact-fiducials/value.txt', /data name/], ['d8/m1.bad/c0.csv', /data directory name/],
]) {
  const target = path.join(work, 'data', name);
  fs.mkdirSync(path.dirname(target), {recursive: true}); fs.writeFileSync(target, 'control\n');
  assert.throws(() => checkData(work), reason, name);
  fs.unlinkSync(target);
  for (let d = path.dirname(target); d !== path.join(work, 'data') && fs.readdirSync(d).length === 0; d = path.dirname(d)) fs.rmdirSync(d);
}
fs.rmSync(work, {recursive: true});
console.log('PASS data layout negative controls (names, versions, dimensions, fixtures, roles, bundled exceptions)');

console.log('SIC_NEGATIVE_CONTROL:data-layout-controls');
