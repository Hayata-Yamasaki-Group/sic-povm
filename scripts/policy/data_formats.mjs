// SPDX-License-Identifier: AGPL-3.0-or-later
// Check regenerated WH ray formats with the production readback command.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';

const [root, program] = process.argv.slice(2).map(x => path.resolve(x));
function check(json, file) {
  const result = spawnSync(program, ['check', json, file], {encoding: 'utf8', timeout: 10000});
  assert.ifError(result.error);
  assert.equal(result.signal, null);
  return result;
}
const rays = [];
const data = path.join(root, 'data');
for (const dim of fs.readdirSync(data, {withFileTypes: true})) {
  if (!dim.isDirectory() || !/^d[1-9][0-9]*$/.test(dim.name)) continue;
  const dir = path.join(data, dim.name);
  for (const multiplet of fs.readdirSync(dir, {withFileTypes: true})) {
    if (!multiplet.isDirectory() || !/^m[1-9][0-9]*$/.test(multiplet.name)) continue;
    const folder = path.join(dir, multiplet.name);
    for (const name of fs.readdirSync(folder)) {
      if (/^c(?:0|[1-9][0-9]*)\.json$/.test(name)) rays.push(path.join(folder, name));
    }
  }
}
assert(rays.length > 0, 'no generated WH ray JSON records');
let checked = 0;
for (const json of rays.sort()) {
  const stem = path.basename(json, '.json');
  const siblings = fs.readdirSync(path.dirname(json)).filter(name => name.startsWith(stem + '.') && name !== stem + '.json');
  assert(siblings.length > 0, `no sibling formats: ${json}`);
  for (const name of siblings.sort()) {
    assert(/\.(csv|npy|mat|h5)$/.test(name), `unknown sibling format: ${name}`);
    const result = check(json, path.join(path.dirname(json), name));
    assert.equal(result.status, 0, `${json}: ${name}\n${result.stdout}${result.stderr}`);
    assert.match(result.stdout, /NUMERIC-CHECK PASS/);
    ++checked;
  }
}

// A valid CSV with a changed coordinate must fail at the value comparison,
// rather than passing because the command or inventory was never exercised.
const work = fs.mkdtempSync(path.join(os.tmpdir(), 'sic-data-formats-'));
try {
  const json = rays[0];
  const values = JSON.parse(fs.readFileSync(json, 'utf8')).fp64.decimal;
  const rows = values.map(([re, im], i) => `${i},${i === 0 ? (re === '0.0000000000000000e+00' ? '1.0000000000000000e+00' : '0.0000000000000000e+00') : re},${im}`);
  const csv = path.join(work, 'changed.csv');
  fs.writeFileSync(csv, 'index,real,imag\n' + rows.join('\n') + '\n');
  const result = check(json, csv);
  assert.equal(result.status, 1, `${result.stdout}${result.stderr}`);
  assert.match(result.stdout, /NUMERIC-CHECK FAIL/);
  assert.match(result.stderr, /coordinate 0 differs/);
} finally {
  fs.rmSync(work, {recursive: true, force: true});
}
console.log(`PASS generated ray formats: ${rays.length} rays, ${checked} sibling files; changed-coordinate control refused`);

console.log('SIC_NEGATIVE_CONTROL:data-formats');
