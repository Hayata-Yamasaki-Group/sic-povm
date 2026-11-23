// SPDX-License-Identifier: AGPL-3.0-or-later
// Checker-owned specification fixture: a basis ray, not a SIC. D_(0,1)
// stabilizes it. No production or peer-checker writer/parser is used.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, mkdirSync, rmSync, writeFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join, resolve} from 'node:path';

const checker = resolve(process.argv[2]);
const work = mkdtempSync(join(tmpdir(), 'sic-stabilizer-json-'));
const zero = `0.${'0'.repeat(60)}`;
const tiny = `0.${'0'.repeat(59)}1`;
const one = `1.${'0'.repeat(60)}`;
const hash = '0'.repeat(64);
const record = {
  schema: 'sic-fiducial-json/1.0', required_features: [], d: '2', multiplet: 'm1',
  id: 'd2-m1-c0', index: '0',
  galois_element: {smith: ['2'], exponents: ['0']},
  labels: {anchor: '0', sicx_ray_label: 'c0'},
  sicx: {name: 'fixture.sicx', sha256: hash, merkle_root: hash},
  fp32: {values: [['3f800000', '00000000'], ['00000000', '00000000']],
    decimal: [['1.00000000e+00', '0.00000000e+00'], ['0.00000000e+00', '0.00000000e+00']],
    rounding: 'synthetic exact basis ray', error_bound: '0x1p-200'},
  fp64: {values: [['3ff0000000000000', '0000000000000000'],
    ['0000000000000000', '0000000000000000']],
    decimal: [['1.0000000000000000e+00', '0.0000000000000000e+00'],
      ['0.0000000000000000e+00', '0.0000000000000000e+00']],
    rounding: 'synthetic exact basis ray', error_bound: '0x1p-200'},
  hp: {accuracy_bits: '180', decimal_places: '60',
    values: [[one, tiny, zero, tiny], [zero, tiny, zero, tiny]]},
};
const file = path => ({path, bytes: '0', sha256: hash});
const ray = {id: 'd2-m1-c0', label: 'c0', sicx_ray_label: 'c0', exponents: ['0'],
  accuracy_bits: '180', files: [file('d2/m1/c0.csv'), file('d2/m1/c0.json')]};
const catalogue = {schema: 'sic-release-catalogue/1.0',
  release: {repository: 'TEST', tag: 'fixture'}, dimensions: [{d: '2', files: [],
    multiplets: [{id: 'd2-m1', labels: {}, smith: ['2'], files: [],
      sicx: {size_bytes: '0', sha256: hash, merkle_root: hash, exact_root: hash,
        parts_file: 'd2/m1/multiplet.sicx.parts.json', assets: []}, rays: [ray]}]}]};
const table = '# sic-stabilizers/1.0\n' +
  'ray d2-m1-c0 d 2 order - status - group_order 2 unitary_order 2\n' +
  'elem d2-m1-c0 0 det +1 F 1 0 0 1 chi 0 0 order 1 trace 2\n' +
  'elem d2-m1-c0 1 det +1 F 1 0 0 1 chi 0 1 order 2 trace 2\nend 1\n';
let count = 0;

function run(name, {changeCatalogue, changeRecord, rawCatalogue, rawRecord, changeTable, status = 2,
    match = /error:/, options = [], externalCatalogue = false} = {}) {
  const root = join(work, name);
  const data = externalCatalogue ? join(work, `${name}-external-data`) : join(root, 'data');
  mkdirSync(root, {recursive: true});
  mkdirSync(join(data, 'd2/m1'), {recursive: true});
  const cat = structuredClone(catalogue), value = structuredClone(record);
  changeCatalogue?.(cat);
  changeRecord?.(value);
  writeFileSync(join(data, 'catalogue.json'), rawCatalogue?.(JSON.stringify(cat)) ??
    JSON.stringify(cat));
  writeFileSync(join(data, 'd2/m1/c0.json'), rawRecord?.(JSON.stringify(value)) ??
    JSON.stringify(value));
  writeFileSync(join(data, 'd2/m1/c0.csv'), 'index,real,imag\n0,1,0\n1,0,0\n');
  writeFileSync(join(root, 'stabilizers.txt'), changeTable ? changeTable(table) : table);
  options = [...options, '--catalogue', join(data, 'catalogue.json')];
  const result = spawnSync(checker, [join(root, 'stabilizers.txt'), '--dim', '2', '--repo', root, ...options],
    {env: {...process.env, SIC_THREADS: '1'}, encoding: 'utf8', timeout: 10000});
  assert.ifError(result.error);
  assert.equal(result.status, status, `${name}: ${result.stdout}\n${result.stderr}`);
  assert.match(result.stdout + result.stderr, match, name);
  ++count;
}
const multiplet = cat => cat.dimensions[0].multiplets[0];
const entry = cat => multiplet(cat).rays[0];
try {
  run('empty-table', {changeTable: () => '# sic-stabilizers/1.0\nend 0\n',
    match: /does not cover the catalogue rays/});
  run('truncated-table', {changeTable: s => s.replace('end 1\n', ''),
    match: /missing or unreadable footer/});
  run('truncated-ray', {changeTable: s => s.replace(/^elem .* 1 det.*\n/m, ''),
    match: /declared element counts differ/});
  run('missing-ray', {changeCatalogue: c => {
    multiplet(c).rays.push({...ray, label: 'c1', id: 'd2-m1-c1',
      files: [file('d2/m1/c1.csv'), file('d2/m1/c1.json')]});
  }, match: /does not cover the catalogue rays/});
  run('footer-count', {changeTable: s => s.replace('end 1', 'end 2'),
    match: /invalid footer count/});
  run('basis-positive', {status: 0, match: /certified 1 non-identity elements.*PASS/});
  run('external-catalogue-repo-mismatch-positive', {externalCatalogue: true,
    status: 0, match: /certified 1 non-identity elements.*PASS/});
  run('zero-radius-positive', {status: 0, match: /bits: PASS/,
    changeRecord: r => r.hp.values.forEach(v => {v[1] = zero; v[3] = zero;})});
  run('nonbinary-midpoint-positive', {status: 0, match: /bits: PASS/,
    changeRecord: r => {r.hp.values[0][0] = `0.1${'0'.repeat(59)}`;}});
  run('metadata-utf8-positive', {status: 0, match: /bits: PASS/,
    changeCatalogue: c => {c.release.repository = '漢字🐕';}});
  run('metadata-escaped-unicode-positive', {status: 0, match: /bits: PASS/,
    rawCatalogue: s => s.replace('TEST', '\\u6f22\\ud83d\\udc15')});
  run('source-label-positive', {status: 0, match: /bits: PASS/,
    changeCatalogue: c => {entry(c).sicx_ray_label = 'older-source-label';}});
  run('wrong-coordinate', {status: 1, match: /certified 1 non-identity elements.*FAIL/,
    changeRecord: r => {r.hp.values[1][0] = `0.1${'0'.repeat(59)}`;}});
  run('nonzero-radius-is-used', {status: 1, match: /certified 1 non-identity elements.*FAIL/,
    changeRecord: r => {r.hp.accuracy_bits = '0';
      r.hp.values[1][1] = `0.${'0'.repeat(9)}1${'0'.repeat(50)}`;}});
  run('duplicate-key', {rawCatalogue: s => s.replace('{', '{"schema":"duplicate",')}); // identity-negative
  run('duplicate-decoded-key', {rawCatalogue: s =>
    s.replace('{', '{"\\u0073chema":"duplicate",')}); // identity-negative
  run('trailing-data', {rawCatalogue: s => `${s} {}`});
  run('trailing-nul', {rawCatalogue: s => Buffer.concat([Buffer.from(s), Buffer.from([0])])});
  run('truncated-json', {rawRecord: s => s.slice(0, -1)});
  run('trailing-comma', {rawCatalogue: s => s.replace('"dimensions":[{', '"dimensions":[,{')});
  run('nonstring-scalar', {changeCatalogue: c => {c.dimensions[0].d = 2;}});
  run('wrong-schema', {changeCatalogue: c => {c.schema = 'future/2';}}); // identity-negative
  run('excess-nesting', {changeCatalogue: c => {
    let nested = 'leaf'; for (let i = 0; i < 40; ++i) nested = [nested];
    c.release.extra = nested;
  }});
  run('oversize-json', {rawCatalogue: s => `${s}${' '.repeat(16 * 1024 * 1024)}`});
  run('invalid-utf8', {rawCatalogue: s => {
    const bytes = Buffer.from(s); bytes[bytes.indexOf('TEST')] = 0xff; return bytes;
  }});
  run('overlong-utf8', {rawCatalogue: s => {
    const bytes = Buffer.from(s), k = bytes.indexOf('TEST');
    bytes[k] = 0xc0; bytes[k + 1] = 0xaf; return bytes;
  }});
  run('unpaired-surrogate', {rawCatalogue: s => s.replace('TEST', '\\ud800')});
  run('unpaired-low-surrogate', {rawCatalogue: s => s.replace('TEST', '\\udfff')});
  run('duplicate-dimension', {changeCatalogue: c => c.dimensions.push(c.dimensions[0])});
  run('duplicate-multiplet', {changeCatalogue: c =>
    c.dimensions[0].multiplets.push(multiplet(c))});
  run('duplicate-ray', {changeCatalogue: c => multiplet(c).rays.push(entry(c))});
  run('duplicate-csv', {changeCatalogue: c => entry(c).files.push(file('d2/m1/c0.csv'))});
  run('escaping-path', {changeCatalogue: c => {entry(c).files[0].path = '../c0.csv';}});
  run('missing-listed-json', {changeCatalogue: c => {entry(c).files.pop();}});
  run('wrong-ray-id', {changeCatalogue: c => {entry(c).id = 'd2-m2-c0';}});
  run('noncanonical-ray-label', {changeCatalogue: c => {entry(c).label = 'c00';}});
  run('wrong-record-id', {changeRecord: r => {r.id = 'd2-m1-c1';}});
  run('wrong-record-dimension', {changeRecord: r => {r.d = '4';}});
  run('unknown-required-feature', {changeRecord: r => {r.required_features = ['future'];}});
  run('wrong-coordinate-count', {changeRecord: r => {r.hp.values.pop();}});
  run('wrong-tuple-size', {changeRecord: r => {r.hp.values[0].pop();}});
  run('wrong-decimal-width', {changeRecord: r => {r.hp.values[0][0] += '0';}});
  run('decimal-exponent-refused', {changeRecord: r => {r.hp.values[0][0] = '1e0';}});
  run('nonfinite-refused', {changeRecord: r => {r.hp.values[0][0] = 'nan';}});
  run('negative-radius', {changeRecord: r => {r.hp.values[0][1] = `-${tiny}`;}});
  run('false-radius-accuracy', {changeRecord: r => {r.hp.accuracy_bits = '1000000';}});
  run('radius-at-accuracy-bound', {changeRecord: r => {
    r.hp.accuracy_bits = '1'; r.hp.values[0][1] = `0.5${'0'.repeat(59)}`;
  }});
  run('decimal-admission-limit', {changeRecord: r => {r.hp.decimal_places = '1000001';}});
  console.log(`checker JSON controls: ${count} passed`);
} finally {
  rmSync(work, {recursive: true, force: true});
}
