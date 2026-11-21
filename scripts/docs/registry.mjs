// SPDX-License-Identifier: AGPL-3.0-or-later
// Check exit assertions and execute the named controls enrolled by each policy.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {root,read,run,refuses} from './runtime_checks.mjs';
const [build,work]=process.argv.slice(2).map(p=>path.resolve(p));
const tests=JSON.parse(run('ctest',['--test-dir',build,'--show-only=json-v1'])).tests;
const properties=t=>Object.fromEntries(t.properties.map(p=>[p.name,p.value]));
const manifest=JSON.parse(read(path.join(root,'scripts/docs/policy_controls.json')));
function checkVerdicts(ts) {
  for(const test of ts) {
    const p=properties(test);if(!p.PASS_REGULAR_EXPRESSION)continue;
    const index=test.command.findIndex(s=>s.endsWith('/scripts/docs/exit_status.mjs'));
    assert(index>=0 && /^\d+$/.test(test.command[index+1]),`registry: missing exit assertion ${test.name}`);
    assert(p.FAIL_REGULAR_EXPRESSION?.includes('SIC_UNEXPECTED_EXIT_STATUS'),`registry: missing status failure marker ${test.name}`);
    assert(!p.WILL_FAIL,`registry: exit assertion must not invert wrapper ${test.name}`);
  }
}
function checkControls(ts,m) {
  for(const t of ts.filter(t=>t.name.startsWith('sic_test_policy_')||m[t.name])) {
    assert(m[t.name],`registry: missing negative control ${t.name}`);
    assert(ts.some(x=>x.name===m[t.name].test),`registry: absent negative control test ${t.name}`);
    assert(m[t.name].reason.length>0,`registry: no negative control description ${t.name}`);
    if(m[t.name].test===t.name)assert(/^[a-z0-9-]+$/.test(m[t.name].embedded||''),`registry: missing named embedded control ${t.name}`);
  }
}
checkVerdicts(tests);checkControls(tests,manifest);
// Positional scratch arguments of enrolled Node controls (script is args[0]).
// BUILD, repository roots, binaries and other read-only inputs stay unchanged.
const nodeWorkArguments=new Map([
  ['scripts/docs/registry.mjs',2],
  ['scripts/docs/policy_controls.mjs',1],
  ['scripts/docs/shell.mjs',2],
  ['scripts/docs/schemas.mjs',2],
  ['scripts/policy/data_layout_controls.mjs',2],
  ['scripts/policy/header_namespaces_controls.mjs',1],
]);
// Execute each self-referencing test and require its named control receipt.
// A child registry validates its own controls without re-entering this traversal.
if(!process.env.SIC_CONTROL_PROBE)for(const t of tests.filter(t=>manifest[t.name])) {
  const entry=manifest[t.name];
  if(entry.test!==t.name)continue;
  const [program,...args]=t.command;
  // CTest may run the enrolled owner at the same time as this control probe.
  // Nested consumers and Node controls must not remove the owner's scratch tree.
  const scratch=fs.mkdtempSync(path.join(build,'claim-control-'));
  try {
    const childArgs=args.map(arg=>arg.startsWith('-DWORK=')?
      '-DWORK='+path.join(scratch,'work'):arg);
    const workArgument=nodeWorkArguments.get(path.relative(root,args[0]||''));
    if(workArgument!==undefined){
      assert(args[workArgument],`registry: missing scratch argument ${t.name}`);
      childArgs[workArgument]=path.join(scratch,'work');
    }
    const output=run(program,childArgs,{cwd:properties(t).WORKING_DIRECTORY||build,timeout:110000,
      env:{...process.env,SIC_CONTROL_PROBE:'1',SIC_THREADS:'1',SIC_MEMORY_MB:'2048'}});
    assert(output.split('\n').some(line=>line.replace(/^-- /,'').trim()===`SIC_NEGATIVE_CONTROL:${entry.embedded}`||line.replace(/^-- /,'').trim().startsWith(`SIC_NEGATIVE_CONTROL:${entry.embedded} (`)),`registry: embedded control did not execute ${t.name}: ${entry.embedded}`);
  } finally {
    fs.rmSync(scratch,{recursive:true,force:true});
  }
}
refuses(()=>checkVerdicts([{name:'unprotected',command:['program'],properties:[{name:'PASS_REGULAR_EXPRESSION',value:['PASS']}]}]),/registry: missing exit/);
refuses(()=>checkControls([{name:'sic_test_policy_uncovered'}],manifest),/registry: missing negative control/);
refuses(()=>checkControls([{name:'sic_test_policy_fake'}],{'sic_test_policy_fake':{test:'sic_test_policy_fake',reason:'fake'}}),/registry: missing named embedded/);
// Exercise CTest's actual precedence, rather than only testing the census.
fs.rmSync(work,{recursive:true,force:true});fs.mkdirSync(work,{recursive:true});
const wrapper=path.join(root,'scripts/docs/exit_status.mjs');
for(const status of [0,1]) {
  fs.writeFileSync(path.join(work,'CTestTestfile.cmake'),`add_test(control "${process.execPath}" "${wrapper}" 0 "${process.execPath}" -e "console.log('EXPECTED_VERDICT');process.exit(${status})")\nset_tests_properties(control PROPERTIES PASS_REGULAR_EXPRESSION "EXPECTED_VERDICT" FAIL_REGULAR_EXPRESSION "SIC_UNEXPECTED_EXIT_STATUS" TIMEOUT 5)\n`);
  const out=run('ctest',['--test-dir',work,'--output-on-failure'],{status:status?8:0});
  if(status)assert.match(out,/SIC_UNEXPECTED_EXIT_STATUS/);
}
console.log('PASS exit-status and policy-control census; real CTest wrong-status control rejected despite matching verdict');

console.log('SIC_NEGATIVE_CONTROL:claim-registry');
