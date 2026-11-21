// SPDX-License-Identifier: AGPL-3.0-or-later
// HDF5 attributes are checked by sic_test_data_hoggar_data using libhdf5 when available;
// c0.h5 is the only HDF5 file in data/. JSON policy remains dependency-free.
// Frozen JSON identities use the sic and sicx families and the release's major.minor version.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import assert from 'node:assert/strict';
import {checkTextualIdentities, textualIdentityControls} from './textual_identities.mjs';

const identity = /^sicx?-[a-z0-9]+(-[a-z0-9]+)*\/1\.0$/;
function requireIdentity(value, location) {
  assert(typeof value === 'string' && value === value.trim() && identity.test(value), `format identity: ${location}: ${JSON.stringify(value)}`);
}
function files(dir) {
  return fs.readdirSync(dir, {withFileTypes: true}).sort((a, b) => a.name.localeCompare(b.name)).flatMap(entry => {
    const file = path.join(dir, entry.name);
    assert(!entry.isSymbolicLink(), `format identity: unexpected symlink: ${file}`);
    return entry.isDirectory() ? files(file) : [file];
  });
}
function schemaValues(value, location) {
  let count = 0;
  if (Array.isArray(value)) {
    for (let i = 0; i < value.length; ++i) count += schemaValues(value[i], `${location}[${i}]`);
  } else if (value !== null && typeof value === 'object') {
    for (const [key, child] of Object.entries(value)) {
      if (key === 'schema') { requireIdentity(child, location + '.schema'); ++count; }
      else count += schemaValues(child, location + '.' + key);
    }
  }
  return count;
}
function schemaIds(value, location, id) {
  if (value === null || typeof value !== 'object') return;
  for (const [key, child] of Object.entries(value)) {
    if (key === '$id') requireIdentity(child, location + '.$id');
    if (key === 'properties' && child?.schema) {
      const field = child.schema;
      if (Object.hasOwn(field, 'const')) {
        requireIdentity(field.const, location + '.properties.schema.const');
        assert.equal(field.const, id, `format identity: schema const differs: ${location}`);
      }
      if (Object.hasOwn(field, 'enum')) {
        assert(Array.isArray(field.enum) && field.enum.length > 0, `format identity: empty schema enum: ${location}`);
        for (const entry of field.enum) {
          requireIdentity(entry, location + '.properties.schema.enum');
          assert.equal(entry, id, `format identity: schema enum differs: ${location}`);
        }
      }
    }
    schemaIds(child, location + '.' + key, id);
  }
}
// Known document locations are keyed independently of the field being checked.
function requiresSchema(relative, line) {
  return line === 0 && [
    /^data\/catalogue\.json$/,
    /^data\/d[0-9]+\/(?:m[0-9]+|hoggar)\/(?:c[0-9]+|manifest|exact|multiplet\.sicx\.parts)\.json$/,
    /^cpp\/tests\/fixtures\/numeric\/d[0-9]+\/m[0-9]+\/c[0-9]+\/metadata\.json$/,
    /^cpp\/tests\/fixtures\/sicx\/d[0-9]+(?:-c[0-9]+|_release_catalogue)?\.json$/,
    /^cpp\/tests\/fixtures\/sicx\/d[0-9]+\.jsonl$/,
  ].some(pattern => pattern.test(relative));
}
function check(root) {
  const schemas = files(path.join(root, 'docs/schemas')).filter(file => file.endsWith('.schema.json'));
  assert(schemas.length > 0, 'format identity: no schemas checked');
  for (const file of schemas) {
    const schema = JSON.parse(fs.readFileSync(file, 'utf8'));
    requireIdentity(schema.$id, file + '.$id');
    schemaIds(schema, file, schema.$id);
  }
  let count = 0;
  for (const dir of ['data', 'cpp/tests/fixtures']) {
    let checked = 0;
    for (const file of files(path.join(root, dir))) {
      if (!/\.jsonl?$/.test(file)) continue;
      const text = fs.readFileSync(file, 'utf8');
      const documents = file.endsWith('.jsonl') ? text.trimEnd().split('\n').map(line => JSON.parse(line)) : [JSON.parse(text)];
      for (let i = 0; i < documents.length; ++i) {
        if (requiresSchema(path.relative(root, file).split(path.sep).join('/'), i))
          assert(Object.hasOwn(documents[i], 'schema'), `format identity: missing schema: ${file}:${i + 1}`);
        checked += schemaValues(documents[i], `${file}:${i + 1}`);
      }
    }
    assert(checked > 0, `format identity: no schema values checked in ${dir}`);
    count += checked;
  }
  return {schemas: schemas.length, values: count};
}

// Exercise the complete filesystem walk, including nested values and JSON Lines.
const work = fs.mkdtempSync(path.join(os.tmpdir(), 'sic-format-identities-'));
try {
  for (const dir of ['docs/schemas', 'data/d8/hoggar', 'cpp/tests/fixtures/sicx']) fs.mkdirSync(path.join(work, dir), {recursive: true});
  const schema = path.join(work, 'docs/schemas/sic-control-1.schema.json');
  const data = path.join(work, 'data/d8/hoggar/c0.json');
  const fixture = path.join(work, 'cpp/tests/fixtures/sicx/d8.jsonl');
  const good = 'sic-control/1.0';
  const write = (id = good, value = good, line = good) => {
    fs.writeFileSync(schema, JSON.stringify({$id: id, properties: {schema: {const: id}}}));
    fs.writeFileSync(data, JSON.stringify({schema: value, nested: []}));
    fs.writeFileSync(fixture, JSON.stringify({schema: line}) + '\n');
  };
  write(); assert.deepEqual(check(work), {schemas: 1, values: 2});
  write('sicx-control/1.0', 'sicx-control/1.0', 'sicx-control/1.0');
  assert.deepEqual(check(work), {schemas: 1, values: 2});
  write(); fs.writeFileSync(data, JSON.stringify({schema: good, decoder: {name: 'sic-decode/1.0'}, algorithm: 'sic-step-selection/1.0'}));
  assert.deepEqual(check(work), {schemas: 1, values: 2});
  write(); fs.writeFileSync(data, JSON.stringify({schema: good, nested: [{schema: 'bad/1'}]}));
  assert.throws(() => check(work), /format identity:/);
  for (const bad of ['sic-control/1', 'control/1.0', 'sic--control/1.0', 'sic-control-/1.0', 'sic-/1.0', 'sic-control/1.0.0', 'sic-control/2.0', 'sic-Control/1.0', 'sic_control/1.0', 'sic-control/1.0\n', '', null, 1]) {
    write(bad); assert.throws(() => check(work), /format identity:/);
    write(good, bad); assert.throws(() => check(work), /format identity:/);
    write(good, good, bad); assert.throws(() => check(work), /format identity:/);
  }
  write(); fs.writeFileSync(schema, JSON.stringify({$id: good, $defs: {nested: {properties: {schema: {const: 'sic-control/1'}}}}}));
  assert.throws(() => check(work), /format identity:/);
  write(); fs.writeFileSync(schema, JSON.stringify({$id: good, $defs: {nested: {properties: {schema: {const: 'sic-different/1.0'}}}}}));
  assert.throws(() => check(work), /format identity: schema const differs/);
  write(); fs.writeFileSync(schema, JSON.stringify({$id: good, properties: {schema: {enum: ['sic-different/1.0']}}}));
  assert.throws(() => check(work), /format identity: schema enum differs/);
  write(); fs.writeFileSync(schema, JSON.stringify({$id: good, properties: {schema: {enum: [good]}}}));
  assert.deepEqual(check(work), {schemas: 1, values: 2});
  write(); fs.writeFileSync(data, '{}'); assert.throws(() => check(work), /format identity: missing schema/);
  write(); fs.writeFileSync(fixture, '{}\n'); assert.throws(() => check(work), /format identity: missing schema/);
  write(); fs.writeFileSync(schema, '{}'); assert.throws(() => check(work), /format identity:/);
  write(); fs.writeFileSync(schema, JSON.stringify({$id: good, $defs: {nested: {$id: 'bad/1'}}}));
  assert.throws(() => check(work), /format identity:/);
  write(); fs.writeFileSync(schema, JSON.stringify({$id: good, properties: {schema: {const: 'sic-different/1.0'}}}));
  assert.throws(() => check(work), /format identity: schema const differs/);
  // Filesystem probes mirror the public fixture and root-document review cases.
  write();
  fs.mkdirSync(path.join(work, 'cpp/tests/data/clifford'), {recursive: true});
  const table = path.join(work, 'cpp/tests/data/clifford/wrong_element_d8.txt');
  fs.writeFileSync(table, '# sic-stabilizers/1.0\n');
  checkTextualIdentities(work);
  fs.writeFileSync(table, '# SIC_' + 'STABILIZERS 1\n');
  assert.throws(() => checkTextualIdentities(work), /format identity:/);
  fs.writeFileSync(table, '# unknown\n');
  assert.throws(() => checkTextualIdentities(work), /format identity:/);
  fs.writeFileSync(table, '# sic-stabilizers/1.0\n');
  const readme = path.join(work, 'README.md');
  fs.writeFileSync(readme, 'A rule: sicx-2adic-greedy/' + '1\n');
  assert.throws(() => checkTextualIdentities(work), /format identity:/);
  fs.writeFileSync(readme, 'A rule: sicx-2adic-greedy/1.0\n');
  for (const ext of ['cmake', 'mjs']) {
    const harness = path.join(work, 'cpp/tests/control.' + ext);
    fs.writeFileSync(harness, 'set(rule "sicx-2adic-greedy/' + '1")');
    assert.throws(() => checkTextualIdentities(work), /format identity:/);
    fs.writeFileSync(harness, 'set(rule "sicx-2adic-greedy/1.0")');
  }
  checkTextualIdentities(work);
} finally {
  fs.rmSync(work, {recursive: true, force: true});
}
textualIdentityControls();
const root = path.resolve(process.argv[2]);
const result = check(root);
const sources = checkTextualIdentities(root);
console.log(`PASS format identities: ${result.schemas} schemas, ${result.values} schema values; ${sources} sources; negative controls refused`);

console.log('SIC_NEGATIVE_CONTROL:format-identities');
