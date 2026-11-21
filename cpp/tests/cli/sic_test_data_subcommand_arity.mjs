// SPDX-License-Identifier: AGPL-3.0-or-later
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
const [bin, repo, work] = process.argv.slice(2);
fs.rmSync(work, {recursive: true, force: true});
fs.mkdirSync(work, {recursive: true});
const data = path.join(bin, 'sic-data');
const inspect = path.join(bin, 'sic-inspect');
function run(tool, args, status = 0) {
  const r = spawnSync(tool, args, {encoding: 'utf8', timeout: 30000});
  assert.ifError(r.error);
  assert.equal(r.status, status, `${args}: ${r.stdout}${r.stderr}`);
  if (status === 2) {
    assert.equal(r.stdout, '');
    assert(r.stderr.includes(`usage: sic-data ${args[0]}`), r.stderr);
    assert(!r.stderr.includes('usage: sic-data COMMAND'), r.stderr);
  }
  return r;
}
const grammar = {
  formats: 'sic-data formats',
  write: 'sic-data write RAY.json OUTDIR',
  check: 'sic-data check RAY.json FILE...',
  bundle: 'sic-data bundle OUT.npz D [RAY.json...] [--exceptional RAY.json GROUP]...',
  'check-bundle': 'sic-data check-bundle FILE.npz [RAY.json...] [--exceptional RAY.json]...',
  layout: 'sic-data layout INPUT.sicx ROOT --multiplet N',
  catalogue: 'sic-data catalogue ROOT',
  list: 'sic-data list CATALOGUE.json',
};
for (const [command, usage] of Object.entries(grammar)) {
  const h = run(data, [command, '--help']);
  assert.equal(h.stderr, '');
  assert(h.stdout.startsWith(`usage: ${usage}`), h.stdout);
  run(data, [command, '--unknown-option'], 2);
}
// One real successful call for every subcommand, including both exceptional grammars.
const fixture = path.join(repo, 'cpp/tests/fixtures/sicx/d4.sicx');
const rays = path.join(work, 'rays');
run(inspect, [fixture, '--json-rays', rays, '--multiplet', 'm1']);
const ray = path.join(rays, 'c0.json');
const numeric = path.join(work, 'numeric');
run(data, ['formats']);
run(data, ['write', ray, numeric]);
assert.deepEqual(fs.readdirSync(numeric).sort(), ['c0.csv', 'c0.mat', 'c0.npy']);
run(data, ['check', ray, path.join(numeric, 'c0.npy')]);
run(data, ['check', ray, path.join(numeric, 'c0.npy'), path.join(numeric, 'c0.csv')]);
const bundle = path.join(work, 'bundle.npz');
run(data, ['bundle', bundle, '4', ray]);
run(data, ['check-bundle', bundle, ray]);
const exceptional = path.join(repo, 'data/d8/hoggar/c0.json');
const extraBundle = path.join(work, 'exceptional.npz');
run(data, ['bundle', extraBundle, '8', '--exceptional', exceptional, 'pauli-3-qubit']);
run(data, ['check-bundle', extraBundle, '--exceptional', exceptional]);
const layout = path.join(work, 'layout');
run(data, ['layout', fixture, layout, '--multiplet', '1', '--threads', '1']);
run(data, ['catalogue', layout]);
assert.match(run(data, ['list', path.join(layout, 'catalogue.json')]).stdout, /multiplet d4-m1/);
// Fixed arities reject both missing and surplus operands before file I/O.
for (const args of [
  ['formats', 'extra'], ['write'], ['write', 'missing'], ['write', 'missing', 'out', 'extra'],
  ['check'], ['check', 'missing'], ['bundle'], ['bundle', 'out'], ['bundle', 'out', '4'],
  ['bundle', 'out', '8', '--exceptional', exceptional],
  ['check-bundle'], ['check-bundle', bundle], ['check-bundle', bundle, '--exceptional'],
  ['layout'], ['layout', 'missing'], ['layout', 'missing', 'root', 'extra'],
  ['layout', 'missing', 'root'], ['catalogue'], ['catalogue', 'root', 'extra'],
  ['list'], ['list', 'missing', 'extra'],
]) run(data, args, 2);
// Variadic file operands remain valid syntax; nonexistent input is an input failure (1).
run(data, ['check', ray, path.join(work, 'missing.npy')], 1);
run(data, ['check-bundle', bundle, ray, path.join(work, 'missing.json')], 1);
run(data, ['bundle', path.join(work, 'bad.npz'), '4', ray, path.join(work, 'missing.json')], 1);
console.log('PASS all eight data grammars, help, arity, exceptional operands and exits 0/1/2');
// Container also reports only the selected subcommand's usage on help and refusal.
for (const command of ['split', 'fetch', 'repair', 'read-range', 'from-text', 'merge-parts',
  'merge-element-parts', 'reencode']) {
  const tool = path.join(bin, 'sic-container');
  const help = run(tool, [command, '--help']);
  assert(help.stdout.startsWith(`usage: sic-container ${command} `));
  const error = spawnSync(tool, [command, '--unknown-option'], {encoding: 'utf8', timeout: 10000});
  assert.ifError(error.error);
  assert.equal(error.status, 2, error.stderr);
  assert.equal(error.stdout, '');
  assert(error.stderr.includes(`usage: sic-container ${command} `), error.stderr);
}
