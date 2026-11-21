// SPDX-License-Identifier: AGPL-3.0-or-later
// Reviewable source bindings catch edits on either side of prose default claims.
// Dynamic parser defaults are also compared with omitted-option model values by
// the existing compiled owner; full help equality remains in help.mjs.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {root,read,files,run,refuses,equalSet,toolPath} from './runtime_checks.mjs';
import {generateDefaults,defaultParagraphs} from './contracts/generate_contracts.mjs';
import {compareDefaultValues} from './contracts/default_values.mjs';
import {checkProseContracts} from './prose_contracts.mjs';
const inventory=JSON.parse(read(path.join(root,'docs/default_contract.json')));
const paragraphs=defaultParagraphs;
// Keep the documented example registration tied to its CMake owner.
function checkExampleDiscovery(document, cmake) {
  assert(document.includes('Standalone examples in `cpp/examples` are explicitly registered in'),
    'examples: documentation registration description changed');
  assert(cmake.includes('function(sic_register_standalone_example)') &&
    cmake.includes('sic_register_standalone_example(SOURCE read_fiducial.cpp TARGET read-fiducial'),
    'examples: standalone registration changed');
  assert(cmake.includes('add_test(NAME ${EXAMPLE_TEST} COMMAND ${EXAMPLE_TARGET} ${EXAMPLE_ARGS})'),
    'examples: standalone test registration changed');
}
const exampleDoc=read(path.join(root,'CONTRIBUTING.md'));
const exampleCmake=read(path.join(root,'cpp/cmake/checks/Docs.cmake'));
checkExampleDiscovery(exampleDoc,exampleCmake);
refuses(()=>checkExampleDiscovery(exampleDoc,exampleCmake.replace(
  'sic_register_standalone_example(SOURCE read_fiducial.cpp',
  'missing_registration(SOURCE read_fiducial.cpp')),/examples:/);
refuses(()=>checkExampleDiscovery(exampleDoc.replace('are explicitly registered in','are discovered by'),exampleCmake),/examples:/);
refuses(()=>checkExampleDiscovery(exampleDoc,exampleCmake.replace('function(sic_register_standalone_example)','function(missing_example)')),/examples:/);
refuses(()=>checkExampleDiscovery(exampleDoc,exampleCmake.replace('COMMAND ${EXAMPLE_TARGET} ${EXAMPLE_ARGS}','COMMAND missing')),/examples:/);
function checkVerifyScope(document, source) {
  assert(document.replace(/\s+/g,' ').includes('It does not establish the whole-file SHA-256.'),
    'verify: documented scope changed');
  assert(source.includes('whole-file SHA-256 not established by directory verification'),
    'verify: source scope changed');
}
const verifyDoc=read(path.join(root,'docs/verification.md'));
const verifySource=read(path.join(root,'cpp/apps/verify/command.cpp'));
checkVerifyScope(verifyDoc,verifySource);
refuses(()=>checkVerifyScope(verifyDoc.replace(/It does not(\s+)establish/,'It does$1establish'),verifySource),/verify:/);
refuses(()=>checkVerifyScope(verifyDoc,verifySource.replace('whole-file SHA-256 not established by directory verification','digest established')),/verify:/);
function check(claim,document,authority) {
  assert(['value','reference','recorded'].includes(claim.kind),'defaults: unclassified default claim');
  assert(claim.kind!=='value'||claim.authority.length>0,'defaults: missing value authority');
  assert(claim.kind==='value'||claim.reason?.length>0,'defaults: missing non-value disposition');
  assert(document.includes(claim.claim),`defaults: documented value changed in ${claim.file}`);
  for(const binding of claim.authority) {assert(binding.text.length>0,'defaults: empty authority'); assert(authority(binding.file).includes(binding.text),`defaults: source authority changed: ${binding.file}: ${binding.text}`); }
}
const docFiles=[...files(path.join(root,'docs')).filter(p=>p.endsWith('.md') && !p.endsWith('/tools.md')),
  path.join(root,'README.md'),path.join(root,'CONTRIBUTING.md')];
const actual=docFiles.flatMap(p=>paragraphs(read(p)).map(text=>path.relative(root,p)+':'+text));
equalSet(actual,inventory.map(c=>c.file+':'+c.claim),'defaults: unbound documented default claim');
assert.deepEqual(inventory,generateDefaults(),'defaults: stale generated contract');
const valueCount=compareDefaultValues();
checkProseContracts();
refuses(()=>compareDefaultValues(p=>read(path.join(root,p)),p=>read(path.join(root,p)).replace('kDefaultTransferThreads = 4','kDefaultTransferThreads = 8')),/defaults: value mismatch/);
refuses(()=>compareDefaultValues(p=>read(path.join(root,p)).replace(
  '| `SIC_BUILD_TESTS` | Build and register tests | ON |',
  '| `SIC_BUILD_TESTS` | Build and register tests | OFF |')),/defaults: configuration value mismatch/);
refuses(()=>compareDefaultValues(p=>read(path.join(root,p)),p=>read(path.join(root,p)).replace(
  /(option\(SIC_BUILD_TESTS[^\n]+ )ON\)/,'$1OFF)')),/defaults: configuration value mismatch/);
refuses(()=>compareDefaultValues(p=>read(path.join(root,p)).replace('| Seed precision growth multiplier | 2.0 |','| Seed precision growth multiplier | 3.0 |')),/defaults: seed fallback mismatch/);
refuses(()=>compareDefaultValues(p=>read(path.join(root,p)),p=>read(path.join(root,p)).replace(': 2.0;',': 3.0;')),/defaults: seed fallback mismatch/);
refuses(()=>compareDefaultValues(p=>read(path.join(root,p)).replace(/(`SIC_CLASS_THREADS`[^\n]*\| )hook build only/, '$1producer library')),/defaults: hook scope/);
refuses(()=>compareDefaultValues(p=>read(path.join(root,p)),p=>read(path.join(root,p)).replace('key == "SIC_CLASS_THREADS" || ','')),/defaults: hook scope/);
refuses(()=>compareDefaultValues(p=>read(path.join(root,p)),p=>read(path.join(root,p)).replace('#ifdef SIC_ENABLE_TEST_HOOKS\n    if (std::getenv("SIC_TEST_INSPECT_EXACT_RENAME_FAIL"))','if (std::getenv("SIC_TEST_INSPECT_EXACT_RENAME_FAIL"))')),/defaults: inspect hook guard/);
// Refuse drift in both a documented unset rule and its consuming fallback.
refuses(()=>compareDefaultValues(p=>read(path.join(root,p)).replace(
  '| Use phased storage | disabled |','| Use phased storage | enabled |')),/defaults: storage fallback/);
refuses(()=>compareDefaultValues(p=>read(path.join(root,p)),p=>read(path.join(root,p)).replace(
  'phase_planned = producer_options.phase.value_or(false);',
  'phase_planned = producer_options.phase.value_or(true);')),/defaults: storage consumer/);
// The generated help is the complete authority for the norm defaults.
refuses(()=>compareDefaultValues(p=>read(path.join(root,p)).replace(
  '--max-bits B            precision cap (default 134217728)',
  '--max-bits B            precision cap (default 134217729)')),/defaults: value mismatch/);
refuses(()=>compareDefaultValues(p=>read(path.join(root,p)),p=>read(path.join(root,p)).replace(
  'constexpr unsigned kDefaultThreads = 1;',
  'constexpr unsigned kDefaultThreads = 2;')),/defaults: value mismatch/);
for(const claim of inventory)check(claim,read(path.join(root,claim.file)),p=>read(path.join(root,p)));
const sample=inventory.find(c=>c.kind==='value');
refuses(()=>check(sample,'changed documented default',p=>read(path.join(root,p))),/defaults:/);
refuses(()=>check(sample,read(path.join(root,sample.file)),()=>''),/defaults:/);
const build=path.resolve(process.argv[2]);
const contracts=JSON.parse(read(path.join(root,'docs/cli_contract.json')));
for(const tool of [...new Set(contracts.map(r=>r.tool))]) {
  const locations=['','checkers/verifier','checkers/norm','checkers/crt','checkers/stabilizer'];
  const bin=locations.map(d=>path.join(build,d,tool)).find(p=>fs.existsSync(p));
  if(bin)run(process.execPath,[path.join(root,'scripts/docs/help.mjs'),'--check-tool',tool,bin]);
}
run(process.execPath,[path.join(root,'cpp/tests/cli/sic_test_support_default_ownership.mjs'),root,build]);
for(const [role,tool,parser,script]of [
  ['crt','sic-certify-crt','checkers/crt/sic-test-crt-command-line','cli/defaults.cmake'],
  ['norm','sic-certify-norm','checkers/norm/sic-test-norm-command-line','defaults_cli.cmake'],
  ['stabilizer','sic-certify-stabilizer','checkers/stabilizer/sic-test-stabilizer-command-line','defaults_cli.cmake']
])if(fs.existsSync(path.join(build,parser)))run('cmake',[
  '-DCHECKER='+toolPath(build,tool),'-DPARSER='+path.join(build,parser),'-P',path.join(root,'cpp/checkers',role,'tests',script)]);
const defaultWork=fs.mkdtempSync(path.join(build,'documented-defaults-'));
try {
  run(path.join(build,'sic-test-support-help-defaults'),[root,path.join(defaultWork,'work')]);
} finally {
  fs.rmSync(defaultWork,{recursive:true,force:true});
}
console.log(`PASS default claims: ${inventory.length} bound paragraphs and compiled omitted-option checks; doc/code mutations rejected`);

console.log(`SIC_NEGATIVE_CONTROL:default-values (${valueCount} semantic comparisons)`);
