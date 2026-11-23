// SPDX-License-Identifier: AGPL-3.0-or-later
// Finite documentation contracts: schema vocabulary, precision policy and the
// retained byte example. This is not an independent SICX acceptance checker.
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import {root, read, refuses, equalSet} from './runtime_checks.mjs';

const load = file => read(path.join(root,file));
const digest = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
function between(text, first, next) {
  const start=text.indexOf(first),end=text.indexOf(next,start+first.length);
  assert(start>=0&&end>start,`prose: missing section ${first}`);
  return text.slice(start,end);
}
export function checkSchemaVocabulary(documents=load) {
  const data=documents('docs/data.md'),spec=documents('docs/sicx.md');
  const schema=JSON.parse(documents('docs/schemas/sic-fiducial-json-1.schema.json'));
  const metadata=between(data,'### 2.1 Metadata','### 2.2 Numerical levels');
  const keys=[...metadata.matchAll(/^\| `([a-z0-9_]+)` \|/gm)].map(m=>m[1]);
  equalSet(keys,Object.keys(schema.properties).filter(k=>!['fp32','fp64','hp'].includes(k)),
    'prose: ray metadata schema vocabulary');
  const ray=between(spec,'## 9. Ray files:','## 9a.');
  assert(ray.includes('`sicx` (`name`,')&&schema.properties.sicx,
    'prose: normative ray source property');
  const catalogue=between(data,'**`catalogue.json`**','## 2. The ray file');
  assert(catalogue.includes('`sicx` (`size_bytes`')&&catalogue.includes('`sicx` (`parts_file`'),
    'prose: catalogue/manifest source property');
}
export function checkDecoderPrecision(documents=load,sources=load) {
  const paragraph=between(documents('docs/sicx.md'),'**Precision (informative).**','<a id="spec-37-')
    .replace(/\s+/g,' ');
  const header=sources('cpp/include/sic/decode/decoder.hpp');
  const guard=header.match(/kDefaultGuardBits = (\d+);/);
  const initial=header.match(/kDefaultInitialPrecisionCap = (\d+);/);
  const claimed=paragraph.match(/`delta` uses PREC `delta_bits` or (\d+), and `base_bits` uses PREC `base_path_bits` or the smaller of (\d+) and the computational ceiling/);
  assert(guard&&initial&&claimed,'prose: precision selectors');
  assert.equal(Number(claimed[1]),Number(guard[1]),'prose: guard bits');
  assert.equal(Number(claimed[2]),Number(initial[1]),'prose: initial bits');
  const scheduling=sources('cpp/src/decode/scheduling.cpp');
  for(const operation of [
    'o.base_bits = std::min<slong>(kDefaultInitialPrecisionCap, o.maximum_precision);',
    'o.base_bits = std::max(o.base_bits, initial);',
    'wp > options_.maximum_precision / 2 ? options_.maximum_precision : 2 * wp;',
  ])assert(scheduling.includes(operation),'prose: bounded decoder precision owner');
}
export function checkWorkedFixture(document,bytes) {
  const example=between(document,'### 12.1 `d = 4`','### 12.2 `d = 5`');
  const summary=example.match(/Fixture SHA-256: ([0-9a-f]{64}); bytes: (\d+)/);
  assert(summary,'prose: fixture summary');
  assert.equal(summary[1],digest(bytes),'prose: fixture SHA-256');
  assert.equal(Number(summary[2]),bytes.length,'prose: fixture byte count');
  const footer=bytes.subarray(-112),number=offset=>Number(footer.readBigUInt64LE(offset));
  const geometry=example.match(/LDIR offset (\d+), payload (\d+); PDIR offset (\d+), payload (\d+)\./);
  assert(geometry,'prose: directory geometry');
  assert.deepEqual(geometry.slice(1).map(Number),[number(0),number(8),number(16),number(24)],
    'prose: directory geometry');
  for(const [name,a,b]of [['Exact root',32,64],['File root',64,96]])
    assert.equal(example.match(new RegExp(name+': ([0-9a-f]{64})'))?.[1],footer.subarray(a,b).toString('hex'),
      'prose: '+name);
  const table=between(example,'| Chunk | Offset | Payload bytes | SHA-256 |','Footer bytes:');
  const rows=[...table.matchAll(/^\| ([A-Z]{4}) \| (\d+) \| (\d+) \| ([0-9a-f]{64}) \|$/gm)];
  let offset=16;
  for(const row of rows){
    const length=Number(bytes.readBigUInt64LE(offset+8));
    assert.deepEqual(row.slice(1),[bytes.toString('ascii',offset,offset+4),String(offset),String(length),
      digest(bytes.subarray(offset+16,offset+16+length))],'prose: physical chunk row');
    offset+=16+length;
  }
  assert.equal(offset,number(0),'prose: complete chunk table');
  const serialized=example.match(/Footer bytes:\n```text\n([0-9a-f\n]+)\n```/);
  assert(serialized,'prose: footer example');
  assert.equal(serialized[1].replaceAll('\n',''),footer.toString('hex'),'prose: footer bytes');
}
export function checkExampleCoverage(documents=load,sources=load) {
  assert(documents('docs/data.md').includes('example is compiled and run on a CSV fixture in CTest.'),
    'prose: example coverage scope');
  assert(sources('cpp/cmake/checks/Docs.cmake').includes(
    'sic_register_standalone_example(SOURCE read_fiducial.cpp TARGET read-fiducial\n  TEST sic_test_core_read_fiducial_csv\n  ARGS "${PROJECT_BINARY_DIR}/test-vectors/d2/fiducial-fp64.csv")'),
    'prose: example coverage registration');
}
export function checkRecipeParameters(documents=load,sources=load) {
  const section=between(documents('docs/reproduce.md'),'## Per-multiplet commands','## Dimension-two checks');
  const options=sources('cpp/src/construct/model/alignment_options.hpp');
  for (const [field, constant] of Object.entries({
    selector:'kAlignmentSelector', train:'kAlignmentTrain', candidates:'kAlignmentCandidates',
    sweeps:'kAlignmentSweeps', seed:'kAlignmentSeed',
  })) {
    assert(new RegExp('\\b'+field+' = '+constant+'\\b').test(options),
      'prose: recipe default owner changed for '+field);
  }
  const expected=['d2/m1','d4/m1','d8/m1','d8/m2','d16/m1','d32/m1','d64/m1',
    'd128/m1','d128/m2','d256/m1','d512/m1','d512/m2','d1024/m1','d1024/m2','d2048/m1'];
  const recipes=[...section.matchAll(/^### (d[0-9]+\/m[0-9]+): (standard|maximal) order\n([\s\S]*?)(?=^### |$(?![\s\S]))/gm)];
  assert.deepEqual(recipes.map(match=>match[1]),expected,'prose: recipe identity coverage changed');
  const commands=recipes.map(match=>{
    const found=[...match[3].replace(/\\\n/g,' ').matchAll(/^[ \t]*sic-construct [^\n]+/gm)];
    assert.equal(found.length,1,'prose: missing/duplicate construction recipe '+match[1]);
    const command = found[0][0].trim();
    const exponent = Math.log2(Number(/^d([0-9]+)/.exec(match[1])[1]));
    assert.equal(command, 'sic-construct '+exponent+' --out "$FILE" --rays-out "$FILE.rays.json" --order ' +
      match[2]+' --threads "$T" --memory-mib "$M"', 'prose: recipe arguments differ '+match[1]);
    return command;
  });
  return commands.length;
}
export function checkSmallCertificateBinding(documents=load) {
  const recipe=documents('docs/reproduce.md').match(/for S in ([0-9 ]+); do/);
  const binding=JSON.parse(documents('scripts/docs/contracts/shell_bindings.json'))['small-certificates'];
  assert(recipe && binding && binding.expect.length===1, 'prose: small certificate binding');
  assert.equal(binding.expect[0].pattern, 'RESULT: EXACT SIC[^\\n]*\\[PROVED\\]', 'prose: certificate verdict');
  assert.equal(binding.expect[0].count, recipe[1].trim().split(/\s+/).length, 'prose: certificate binding count');
}
export function checkProseContracts(){
  checkSchemaVocabulary();checkDecoderPrecision();checkExampleCoverage();
  checkRecipeParameters();checkSmallCertificateBinding();
  const doc=load('docs/sicx.md'),fixture=fs.readFileSync(path.join(root,'cpp/tests/fixtures/sicx/d4.sicx'));
  checkWorkedFixture(doc,fixture);
  refuses(()=>checkSchemaVocabulary(p=>load(p).replace('| `sicx` |','| `absent_field` |')),/prose:/);
  refuses(()=>checkSchemaVocabulary(p=>p.endsWith('.schema.json')?load(p).replace('"sicx": {','"absent_field": {'):load(p)),/prose:/);
  refuses(()=>checkDecoderPrecision(p=>load(p).replace('`delta_bits` or 64','`delta_bits` or 65')),/prose:/);
  refuses(()=>checkDecoderPrecision(load,p=>load(p).replace('kDefaultGuardBits = 64','kDefaultGuardBits = 65')),/prose:/);
  refuses(()=>checkDecoderPrecision(load,p=>load(p).replace('o.base_bits = std::max(o.base_bits, initial);','o.base_bits = initial;')),/prose:/);
  refuses(()=>checkWorkedFixture(doc.replace('| EXMT | 16 |','| EXMT | 17 |'),fixture),/prose:/);
  const changed=Buffer.from(fixture);changed[32]^=1;
  refuses(()=>checkWorkedFixture(doc,changed),/prose:/);
  refuses(()=>checkWorkedFixture(doc.replace('Fixture SHA-256: '+digest(fixture),'Fixture SHA-256: '+'0'.repeat(64)),fixture),/prose:/);
  refuses(()=>checkWorkedFixture(doc.replace('Footer bytes:\n```text\n','Footer bytes:\n```text\n00'),fixture),/prose:/);
  refuses(()=>checkExampleCoverage(p=>load(p).replace('on a CSV fixture','on CSV, NPY and text fixtures')),/prose:/);
  refuses(()=>checkExampleCoverage(load,p=>load(p).replace('TEST sic_test_core_read_fiducial_csv','TEST sic_test_core_example_removed')),/prose:/);
  refuses(()=>checkExampleCoverage(load,p=>load(p).replace('TARGET read-fiducial','TARGET missing-example')),/prose:/);
  refuses(()=>checkSmallCertificateBinding(p=>p.endsWith('shell_bindings.json')?load(p).replace('\"count\": 2','\"count\": 1'):load(p)),/prose: certificate binding count/);
  refuses(()=>checkRecipeParameters(p=>load(p).replace('sic-construct 11 --out','sic-construct 11 --alignment-train 65 --out')),/prose: recipe arguments differ/);
  refuses(()=>checkRecipeParameters(load,p=>load(p).replace('train = kAlignmentTrain','train = 65')),/prose: recipe default owner changed/);
  assert.equal(checkRecipeParameters(p=>load(p).replace('sic-construct 11 --out',
    ' sic-construct 11 --out')),15,'prose: indented recipe coverage');
  refuses(()=>checkRecipeParameters(p=>load(p).replace(
    'sic-construct 11 --out', ' sic-construct 10 --out')),
    /prose: recipe arguments differ/);
  refuses(()=>checkRecipeParameters(p=>load(p).replace('sic-construct 11 --out','omitted-command 11 --out')),/prose: missing\/duplicate construction recipe/);
  console.log('PASS schema vocabulary, decoder precision, fixture bytes and example scope; negative controls rejected');
}
