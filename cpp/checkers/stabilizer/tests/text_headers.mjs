// SPDX-License-Identifier: AGPL-3.0-or-later
// Exercise public readers with obsolete headers, using checker-owned fixtures.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';

const [checker, construct, bounds, stabilizer, root] = process.argv.slice(2).map(p => path.resolve(p));
const work = fs.mkdtempSync(path.join(os.tmpdir(), 'sic-text-headers-'));
function obsolete(text, name) {
  return text.replace(/^(# )?[^\n]+/, (_, comment = '') => comment + [name, '1'].join(' '));
}
function refused(program, args, message) {
  const result = spawnSync(program, args, {encoding: 'utf8', timeout: 30000});
  assert.ifError(result.error);
  assert.equal(result.signal, null);
  assert.notEqual(result.status, 0, `${program} accepted an obsolete header`);
  assert.match(result.stdout + result.stderr, message);
}
try {
  const balls = fs.readFileSync(path.join(root, 'cpp/tests/fixtures/numeric/d2/m1/c0/normalized-gauged.balls'), 'utf8');
  const radicals = fs.readFileSync(path.join(root, 'data/d2/d2.sicrad'), 'utf8');
  fs.copyFileSync(path.join(root, 'cpp/tests/fixtures/numeric/d2/m1/c0/fiducial-fp64.csv'), path.join(work, 'ray.csv'));
  fs.writeFileSync(path.join(work, 'catalogue.tsv'), '2\td2\t-\tindependently-exact\tray.csv\n');
  fs.writeFileSync(path.join(work, 'normalized-gauged.balls'), obsolete(balls, 'SIC_VECTOR_BALLS'));
  const common = ['--repo', work, '--catalogue', path.join(work, 'catalogue.tsv')];
  const tablePath = path.join(work, 'stabilizers.txt');
  const built = spawnSync(stabilizer, ['--dim', '2', ...common, '--out', tablePath], {encoding: 'utf8', timeout: 10000});
  assert.ifError(built.error);
  assert.equal(built.status, 0, built.stdout + built.stderr);
  const table = fs.readFileSync(tablePath, 'utf8');
  refused(bounds, [path.join(work, 'normalized-gauged.balls')], /not a sic-vector-balls\/1\.0 file/);
  fs.writeFileSync(tablePath, obsolete(table, 'SIC_STABILIZERS'));
  refused(checker, [tablePath, ...common], /bad header/);
  fs.writeFileSync(tablePath, table);
  fs.writeFileSync(path.join(work, 'normalized-gauged.balls'), balls);
  fs.writeFileSync(path.join(work, 'old.sicrad'), obsolete(radicals, 'SIC_EXACT_D2_RADICALS'));
  refused(construct, ['1', '--threads', '1', '--radicals', path.join(work, 'old.sicrad'), '--out', path.join(work, 'refused.sicx')], /not a d2 radical file/);
  assert(!fs.existsSync(path.join(work, 'refused.sicx')));
  refused(checker, [path.join(work, 'stabilizers.txt'), ...common, '--d2-exact', path.join(work, 'old.sicrad')], /unexpected layout/);
} finally {
  fs.rmSync(work, {recursive: true, force: true});
}
console.log('PASS obsolete-header reader refusals; constructor published no output');
