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
const release = fs.existsSync(path.join(work, 'data', 'catalogue.json'));
checkData(work, release, {requireChecksums: release});
for (const [name, reason] of [
  ['d8/stabilizers-v2.txt', /data name/], ['d8/stabilizers_v2.txt', /internal label/],
  ['d3/stabilizers.txt', /data dimension/], ['d8/fixture.txt', /internal label/],
  ['d8/d8.txt', /data role/], ['d8/m9/c0.csv', release ? /checksum coverage/ : /documented exceptions/],
  ['d8/m01/c0.csv', /data directory/], ['d8/sample/c0.csv', /data directory/],
  ['exact-fiducials/value.txt', /data name/], ['d8/m1.bad/c0.csv', /data directory name/],
]) {
  const target = path.join(work, 'data', name);
  fs.mkdirSync(path.dirname(target), {recursive: true}); fs.writeFileSync(target, 'control\n');
  assert.throws(() => checkData(work, release, {requireChecksums: release}), reason, name);
  fs.unlinkSync(target);
  for (let d = path.dirname(target); d !== path.join(work, 'data') && fs.readdirSync(d).length === 0; d = path.dirname(d)) fs.rmdirSync(d);
}
if (release) {
  // Release-tree controls: each mutation is rehashed in the catalogue and the checksum list, so that
  // the intended rule (not a digest mismatch) rejects it.
  const data = path.join(work, 'data');
  const digest = file => crypto.createHash('sha256').update(fs.readFileSync(path.join(data, file))).digest('hex');
  const save = () => new Map(['catalogue.json', 'sha256sums.txt', 'd8/m1/multiplet.sicx.parts.json']
    .map(file => [file, fs.readFileSync(path.join(data, file))]));
  const restore = saved => { for (const [file, bytes] of saved) fs.writeFileSync(path.join(data, file), bytes); };
  function rehash(changed, added = []) {
    const catalogue = JSON.parse(fs.readFileSync(path.join(data, 'catalogue.json')));
    const visit = node => {
      if (Array.isArray(node)) { node.forEach(visit); return; }
      if (!node || typeof node !== 'object') return;
      if (typeof node.path === 'string' && changed.includes(node.path)) {
        node.bytes = String(fs.statSync(path.join(data, node.path)).size);
        node.sha256 = digest(node.path);
      }
      Object.values(node).forEach(visit);
    };
    visit(catalogue);
    fs.writeFileSync(path.join(data, 'catalogue.json'), JSON.stringify(catalogue));
    const rows = fs.readFileSync(path.join(data, 'sha256sums.txt'), 'utf8').trimEnd().split('\n')
      .filter(row => !added.some(file => row.endsWith('  ./' + file)));
    const updated = rows.map(row => {
      const name = row.slice(66).replace(/^\.\//, '');
      return name === 'catalogue.json' || changed.includes(name) ? digest(name) + row.slice(64) : row;
    });
    for (const file of added) updated.push(digest(file) + '  ./' + file);
    fs.writeFileSync(path.join(data, 'sha256sums.txt'), updated.join('\n') + '\n');
  }
  let saved = save();
  fs.rmSync(path.join(data, 'sha256sums.txt'));
  assert.throws(() => checkData(work, true, {requireChecksums: true}), /checksum list missing/, 'missing checksum list');
  restore(saved);
  const partsFile = 'd8/m1/multiplet.sicx.parts.json';
  const parts = JSON.parse(fs.readFileSync(path.join(data, partsFile)));
  parts.parts[0].offset = '1';
  fs.writeFileSync(path.join(data, partsFile), JSON.stringify(parts));
  rehash([partsFile]);
  assert.throws(() => checkData(work, true, {requireChecksums: true}), /parts offset/, 'rehashed invalid part offset');
  restore(saved);
  for (const [mutate, reason] of [
    [p => { p.alignment = 'sicx-chunks/2.0'; }, /parts alignment/],
    [p => { p.parts[0].sha256 = 'A'.repeat(64); }, /parts digest syntax/],
  ]) {
    const changed = JSON.parse(fs.readFileSync(path.join(data, partsFile)));
    mutate(changed);
    fs.writeFileSync(path.join(data, partsFile), JSON.stringify(changed));
    rehash([partsFile]);
    assert.throws(() => checkData(work, true, {requireChecksums: true}), reason, String(reason));
    restore(saved);
    saved = save();
  }
  fs.mkdirSync(path.join(data, 'd8/m9'), {recursive: true});
  fs.writeFileSync(path.join(data, 'd8/m9/c0.csv'), 'control\n');
  rehash([], ['d8/m9/c0.csv']);
  assert.throws(() => checkData(work, true, {requireChecksums: true}), /unreferenced data: d8\/m9\/c0\.csv/, 'uncatalogued file with checksum entry');
  fs.rmSync(path.join(data, 'd8/m9'), {recursive: true});
  restore(saved);
  checkData(work, true, {requireChecksums: true});
}
fs.rmSync(work, {recursive: true});
console.log(`PASS data layout negative controls (names, versions, dimensions, fixtures, roles, ${release ? 'checksum list, parts manifests and catalogue closure' : 'bundled exceptions'})`);

console.log('SIC_NEGATIVE_CONTROL:data-layout-controls');
