// SPDX-License-Identifier: AGPL-3.0-or-later
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import {checkData} from './data_layout.mjs';
const [root, work] = process.argv.slice(2).map(x => path.resolve(x));
fs.rmSync(work, {recursive: true, force: true});
fs.mkdirSync(work, {recursive: true});
fs.cpSync(path.join(root, 'data'), path.join(work, 'data'), {recursive: true});
fs.mkdirSync(path.join(work, 'cpp/cmake/package'), {recursive: true});
fs.copyFileSync(path.join(root, 'cpp/cmake/package/InstallPackage.cmake'), path.join(work, 'cpp/cmake/package/InstallPackage.cmake'));
checkData(work);
// The source sidecar has just been checked. Exercise layout errors without a
// covering checksum so its coverage check cannot mask the intended diagnostic.
const copiedChecksum = path.join(work, 'data/sha256sums.txt');
if (fs.existsSync(copiedChecksum)) fs.unlinkSync(copiedChecksum);
for (const [name, reason] of [
  ['d8/stabilizers-v2.txt', /data name/], ['d8/stabilizers_v2.txt', /internal label/],
  ['d3/stabilizers.txt', /data dimension/], ['d8/fixture.txt', /internal label/],
  ['d8/d8.txt', /data role/], ['d8/m9/c0.csv', /unreferenced data/],
  ['d8/m01/c0.csv', /data directory/], ['d8/sample/c0.csv', /data directory/],
  ['exact-fiducials/value.txt', /data name/], ['d8/m1.bad/c0.csv', /data directory name/],
]) {
  const target = path.join(work, 'data', name);
  fs.mkdirSync(path.dirname(target), {recursive: true}); fs.writeFileSync(target, 'control\n');
  assert.throws(() => checkData(work), reason, name);
  fs.unlinkSync(target);
  for (let d = path.dirname(target); d !== path.join(work, 'data') && fs.readdirSync(d).length === 0; d = path.dirname(d)) fs.rmdirSync(d);
}
const cataloguePath = path.join(work, 'data/catalogue.json');
const original = fs.readFileSync(cataloguePath, 'utf8');
for (const d of ['3', '2048']) {
  const c = JSON.parse(original); c.dimensions.push({d, files: [], multiplets: []});
  fs.writeFileSync(cataloguePath, JSON.stringify(c));
  assert.throws(() => checkData(work), /catalogue dimension folder|ENOENT/);
}
fs.writeFileSync(cataloguePath, original);
const checksum = path.join(work, 'data/sha256sums.txt');
const members = [];
function hashes(dir, prefix = '') {
  for (const e of fs.readdirSync(dir, {withFileTypes: true})) {
    const f = prefix + e.name;
    if (e.isDirectory()) hashes(path.join(dir, e.name), f + '/');
    else if (f !== 'sha256sums.txt') members.push(crypto.createHash('sha256').update(fs.readFileSync(path.join(dir, e.name))).digest('hex') + '  ' + f);
  }
}
hashes(path.join(work, 'data'));
const valid = members.sort().join('\n') + '\n';
fs.writeFileSync(checksum, valid); checkData(work);
for (const bad of [members.slice(1).join('\n'), valid + members[0], valid.replace('catalogue.json', '../catalogue.json'), valid.replace(/^[0-9a-f]/, 'x'), valid.replace(/^[0-9a-f]{64}/, '0'.repeat(64))]) {
  fs.writeFileSync(checksum, bad);
  assert.throws(() => checkData(work), /checksum/);
}
fs.unlinkSync(checksum);
const ray = path.join(work, 'data/d2/m1/c0.csv');
fs.appendFileSync(ray, 'changed');
assert.throws(() => checkData(work), /catalogue size/);
fs.rmSync(work, {recursive: true});
console.log('PASS data layout negative controls (names, versions, dimensions, fixtures, roles, references, hashes)');

console.log('SIC_NEGATIVE_CONTROL:data-layout-controls');
