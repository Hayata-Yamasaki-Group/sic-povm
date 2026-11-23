// SPDX-License-Identifier: AGPL-3.0-or-later
// Reconcile purpose classification, authenticated manifests and flat build copies.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';
import {files,read,equalSet,refuses,run} from '../../scripts/docs/runtime_checks.mjs';
const source=path.dirname(fileURLToPath(import.meta.url));
const root=path.join(source,'fixtures'), build=process.argv[2];
function check(table,actual) {
  const rows=[...table.matchAll(/^\| `([^`]+)` \|/gm)].map(m=>m[1]);
  equalSet(actual,rows,'fixtures: purpose table membership');
  assert.equal(rows.length,new Set(rows).size,'fixtures: duplicate purpose row');
}
function consumerCensus(table,names) {
  const optional={
    sic_test_sicx_exact_gp_d16:['sicx/tests.cmake','option(SIC_SICX_GP_D16'],
    sic_test_sicx_primitives_and_faults:['sicx/tests.cmake','if(SIC_ENABLE_TEST_HOOKS)']
  };
  for(const match of table.matchAll(/sic_test_[a-z0-9_]+(?:\[_roundtrip\])?/g)) {
    const composed=match[0].endsWith('[_roundtrip]');
    const name=composed?match[0].slice(0,-'[_roundtrip]'.length):match[0];
    if(composed) {
      assert(names.includes(name)||names.includes(name+'_roundtrip'),`fixtures: stale composed consumer ${name}`);continue;
    }
    if(names.includes(name))continue;
    const binding=optional[name];
    assert(binding,`fixtures: stale consumer ${name}`);
    const sourceText=read(path.join(source,binding[0]));
    assert(sourceText.includes(binding[1])&&sourceText.includes(name),'fixtures: stale conditional consumer');
  }
}
function standaloneExamples(sourceText, actual) {
  const rows=[...sourceText.matchAll(/sic_register_standalone_example\(SOURCE (\S+) TARGET (\S+)\s+TEST (\S+)\s+ARGS "([^"]+)"\)/g)]
    .map(m=>({source:m[1],target:m[2],test:m[3],arg:m[4]}));
  equalSet(rows.map(r=>r.source),actual,'examples: standalone registration');
  assert.equal(rows.length,new Set(rows.map(r=>r.source)).size,'examples: duplicate source');
  for(const row of rows) {
    assert(/^[a-z][a-z0-9]*(?:-[a-z0-9]+)*$/.test(row.target),'examples: executable target');
    assert(/^sic_test_[a-z0-9_]+$/.test(row.test),'examples: test name');
  }
  return rows;
}
function registeredExamples(rows,tests) {
  for(const row of rows) {
    const test=tests.find(t=>t.name===row.test);
    assert(test,'examples: absent CTest');
    assert.equal(path.basename(test.command[0]),row.target,'examples: wrong executable');
    const arg=row.arg.replace('${PROJECT_SOURCE_DIR}',path.resolve(source,'..')).replace('${PROJECT_BINARY_DIR}',path.resolve(build));
    assert.equal(test.command[1],arg,'examples: wrong fixture argument');
    assert(fs.existsSync(arg),'examples: missing fixture argument');
  }
}
const examplesSource=read(path.join(source,'../cmake/checks/Docs.cmake'));
const standalone=files(path.join(source,'../examples')).map(p=>path.basename(p));
const examples=standaloneExamples(examplesSource,standalone);
refuses(()=>standaloneExamples(examplesSource,[...standalone,'unregistered.cpp']),/examples:/);
refuses(()=>standaloneExamples(examplesSource,[]),/examples:/);
if(build) {
  const tests=JSON.parse(run('ctest',['--test-dir',build,'--show-only=json-v1'])).tests;
  registeredExamples(examples,tests);
  refuses(()=>registeredExamples(examples,[]),/examples:/);
  refuses(()=>registeredExamples(examples.map(r=>({...r,target:'wrong-target'})),tests),/examples:/);
  refuses(()=>registeredExamples(examples.map(r=>({...r,arg:r.arg+'.absent'})),tests),/examples:/);
}
const actual=files(root).map(p=>path.relative(root,p));
const table=read(path.join(root,'README.md'));
check(table,actual);
for(const row of table.split('\n').filter(l=>l.startsWith('| \x60'))) {
  const cells=row.split(' | ');assert(cells.length===4 && cells.every(c=>c.trim().length),'fixtures: incomplete purpose row');
}
if(build && /SIC_ENABLE_FLINT:BOOL=ON/.test(read(path.join(build,'CMakeCache.txt')))) {
  const tests=JSON.parse(run('ctest',['--test-dir',build,'--show-only=json-v1'])).tests.map(t=>t.name);
  consumerCensus(table,tests);
  refuses(()=>consumerCensus(table+'\nsic_test_policy',tests),/fixtures: stale consumer/);
}
refuses(()=>check(table,[...actual,'unclassified.bin']),/fixtures:/);
refuses(()=>check(table,actual.slice(1)),/fixtures:/);
const copySource=read(path.join(source,'../cmake/checks/FixtureCorpus.cmake'));
const copies=[...copySource.matchAll(/tests\/fixtures\/([^\s)]+)/g)].map(m=>m[1]);
equalSet(copies,[...table.matchAll(/^<!-- build-copy: (.+) -->$/gm)].map(m=>m[1]),'fixtures: documented build-copy list');
const aliases=new Map(copies.map(p=>[path.basename(p),p]));
const digest=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
let hashes=0; const pinned=new Map();
for(const manifest of files(root).filter(p=>path.basename(p)==='SHA256SUMS')) {
  for(const line of read(manifest).trimEnd().split('\n')) {
    const m=line.match(/^([0-9a-f]{64})  (.+)$/);assert(m,`fixtures: malformed manifest ${manifest}`);
    let target=path.join(path.dirname(manifest),m[2]);
    if(!fs.existsSync(target) && path.dirname(manifest)===path.join(root,'sicx') && aliases.has(m[2])) target=path.join(root,aliases.get(m[2]));
    assert(fs.existsSync(target),`fixtures: absent hash input ${target}`);
    assert.equal(digest(target),m[1],`fixtures: checksum ${target}`);++hashes;
    const relative=path.relative(root,target);
    if(!pinned.has(relative))pinned.set(relative,new Set());
    pinned.get(relative).add(path.relative(root,manifest));
    const row=table.split('\n').find(l=>l.startsWith(`| \`${relative}\` |`));
    assert(row?.includes('SHA256SUMS'),`fixtures: pin column omits manifest ${relative}`);
  }
}
for(const line of table.split('\n').filter(l=>l.startsWith('| \x60'))) {
  const cells=line.split(' | ');const name=cells[0].slice(3,-1), pin=cells.at(-1);
  if(pin.includes('SHA256SUMS')) {
    const manifests=[...pin.matchAll(/([a-z0-9_/]+\/SHA256SUMS)/g)].map(m=>m[1]);
    assert(manifests.length>0,`fixtures: unresolved pin claim ${name}`);
    for(const manifest of manifests)assert(pinned.get(name)?.has(manifest),`fixtures: unauthenticated pin claim ${name}: ${manifest}`);
  }
}
if(build)equalSet(fs.readdirSync(path.join(build,'fixtures/sicx')),copies.map(p=>path.basename(p)),'fixtures: unexpected build copy');
if(build) for(const p of copies) assert.equal(digest(path.join(build,'fixtures/sicx',path.basename(p))),digest(path.join(root,p)),`fixtures: build copy differs ${p}`);
refuses(()=>assert.equal(digest(path.join(root,copies[0])),'0'.repeat(64),'fixtures: changed bytes'),/fixtures:/);
console.log(`PASS fixtures: ${actual.length} classified files, ${hashes} authenticated entries, ${copies.length} build copies; negative controls rejected`);

console.log('SIC_NEGATIVE_CONTROL:docs-fixture-inventory');
