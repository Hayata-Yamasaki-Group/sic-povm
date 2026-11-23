// SPDX-License-Identifier: AGPL-3.0-or-later
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {checkData} from './data_layout.mjs';

const [source, program] = process.argv.slice(2).map(x => path.resolve(x));
const work = fs.mkdtempSync(path.join(os.tmpdir(), 'sic-catalogue-freshness-'));
try {
  const data = path.join(work, 'data');
  fs.cpSync(path.join(source, 'data'), data, {recursive: true});
  const catalogue = path.join(data, 'catalogue.json');
  const expected = fs.readFileSync(catalogue);
  assert(checkData(work, true) > 1);
  const result = spawnSync(program, ['--catalogue', data], {encoding: 'utf8', timeout: 10000});
  assert.ifError(result.error);
  assert.equal(result.status, 0, result.stdout + result.stderr);
  assert.match(result.stdout, /WROTE /);
  const fresh = fs.readFileSync(catalogue);
  function requireFresh(bytes) {
    assert(bytes.equals(fresh), 'catalogue bytes differ on regeneration');
  }
  requireFresh(expected);
  assert.throws(() => requireFresh(Buffer.concat([expected, Buffer.from('\n')])), /bytes differ/);
  for (const d of ['3', '2048']) {
    const changed = JSON.parse(expected);
    changed.dimensions.push({d, files: [], multiplets: []});
    fs.writeFileSync(catalogue, JSON.stringify(changed));
    assert.throws(() => checkData(work, true), /catalogue dimension folder|ENOENT/);
  }
  fs.writeFileSync(catalogue, expected);

  function checksumRows(dir, prefix = '') {
    return fs.readdirSync(dir, {withFileTypes: true}).flatMap(entry => {
      const filename = path.join(dir, entry.name);
      const name = prefix + entry.name;
      if (entry.isDirectory()) return checksumRows(filename, name + '/');
      const digest = crypto.createHash('sha256').update(fs.readFileSync(filename)).digest('hex');
      return [digest + '  ' + name];
    });
  }
  const rows = checksumRows(data).sort();
  const sidecar = path.join(data, 'sha256sums.txt');
  const valid = rows.join('\n') + '\n';
  fs.writeFileSync(sidecar, valid);
  assert(checkData(work, true) > 1);
  for (const bad of [
    rows.slice(1).join('\n'), valid + rows[0], valid.replace('catalogue.json', '../catalogue.json'),
    valid.replace(/^[0-9a-f]/, 'x'), valid.replace(/^[0-9a-f]{64}/, '0'.repeat(64)),
  ]) {
    fs.writeFileSync(sidecar, bad);
    assert.throws(() => checkData(work, true), /checksum/);
  }
  fs.unlinkSync(sidecar);
  const ray = path.join(data, 'd2/m1/c0.csv');
  fs.appendFileSync(ray, 'changed');
  assert.throws(() => checkData(work, true), /catalogue size/);
} finally {
  fs.rmSync(work, {recursive: true, force: true});
}
console.log('PASS generated catalogue bytes and stale-catalogue controls');
console.log('SIC_NEGATIVE_CONTROL:catalogue-freshness');
