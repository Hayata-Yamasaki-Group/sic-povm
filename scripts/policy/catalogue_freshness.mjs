// SPDX-License-Identifier: AGPL-3.0-or-later
// Regeneration happens on a private copy: never mutate the source checkout.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';

const [root, program] = process.argv.slice(2).map(x => path.resolve(x));
const work = fs.mkdtempSync(path.join(os.tmpdir(), 'sic-catalogue-freshness-'));
try {
  const data = path.join(work, 'data');
  fs.cpSync(path.join(root, 'data'), data, {recursive: true});
  const committed = fs.readFileSync(path.join(data, 'catalogue.json'));
  const result = spawnSync(program, ['--catalogue', data], {encoding: 'utf8', timeout: 10000});
  assert.ifError(result.error);
  assert.equal(result.signal, null);
  assert.equal(result.status, 0, `${result.stdout}${result.stderr}`);
  assert.match(result.stdout, /WROTE /);
  const fresh = fs.readFileSync(path.join(data, 'catalogue.json'));
  function requireFresh(expected) {
    assert(expected.equals(fresh), 'committed catalogue differs from fresh production output');
  }
  requireFresh(committed);
  // Semantically equivalent JSON with one extra whitespace byte is still stale.
  const stale = Buffer.concat([committed, Buffer.from('\n')]);
  assert.deepEqual(JSON.parse(stale), JSON.parse(committed));
  assert.throws(() => requireFresh(stale), /committed catalogue differs/);
} finally {
  fs.rmSync(work, {recursive: true, force: true});
}
console.log('PASS committed catalogue matches fresh production bytes; stale-whitespace control refused');

console.log('SIC_NEGATIVE_CONTROL:catalogue-freshness');
