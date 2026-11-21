// SPDX-License-Identifier: AGPL-3.0-or-later
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
const [tool, fixture, work] = process.argv.slice(2);
fs.rmSync(work, {recursive: true, force: true});
fs.mkdirSync(work, {recursive: true});
for (const mode of ['--json-rays', '--json-all']) {
  const out = path.join(work, mode.slice(2));
  for (const extra of [[], ['--multiplet', ''], ['--multiplet', '1'], ['--multiplet', 'm0'],
    ['--multiplet', 'm01'], ['--multiplet', 'm1x'], ['--multiplet', 'm1234567890123456789']]) {
    const r = spawnSync(tool, [fixture, mode, out, ...extra], {encoding: 'utf8', timeout: 10000});
    assert.ifError(r.error);
    assert.equal(r.status, 2, `${mode} ${extra}: ${r.stderr}`);
    assert.equal(r.stdout, '');
    assert.match(r.stderr, /usage: sic-inspect/);
    assert(!fs.existsSync(out));
  }
  const missing = spawnSync(tool, [path.join(work, 'missing.sicx'), mode, out, '--multiplet', 'm1'],
    {encoding: 'utf8', timeout: 10000});
  assert.ifError(missing.error);
  assert.equal(missing.status, 1, missing.stderr);
  assert(!fs.existsSync(out));
  const valid = spawnSync(tool, [fixture, mode, out, '--multiplet', 'm1'],
    {encoding: 'utf8', timeout: 10000});
  assert.ifError(valid.error);
  assert.equal(valid.status, 0, valid.stderr);
  assert(fs.existsSync(out));
}
console.log('PASS both JSON modes require a valid multiplet; exits 0/1/2 and no output on refusal');
