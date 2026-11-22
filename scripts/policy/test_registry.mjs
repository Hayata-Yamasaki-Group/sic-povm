// SPDX-License-Identifier: AGPL-3.0-or-later
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';

const [root, build, ctest = 'ctest'] = process.argv.slice(2);
const contract = JSON.parse(fs.readFileSync(path.join(build, 'test-registry-contract.json')));
const enabled = value => /^(ON|TRUE|1)$/i.test(value);
const exact = enabled(contract.flint) && enabled(contract.apps);
const full = exact && enabled(contract.regression);
const hooks = new Set(contract.hooks);
const components = new Set(['access', 'bounds', 'construct', 'core', 'crt', 'data', 'decode',
  'exact', 'inspect', 'norm', 'policy', 'search', 'sicx', 'stabilizer', 'support', 'transport', 'verifier']);
const primary = new Set(['unit', 'integration', 'e2e']);
const owner = /^sic_test_([a-z]+)_[a-z0-9_]+$/;
const gp = 'sic_test_sicx_exact_gp_d16';
const privateTreeExport = 'sic_test_support_public_tree_export';
const mixedHookControls = ['sic_test_construct_failure_categories',
  'sic_test_exact_storage_failure_categories', 'sic_test_exact_failure_categories',
  'sic_test_support_cli_failure_categories'];
const properties = test => Object.fromEntries(test.properties.map(p => [p.name, p.value]));
function inventory(args = []) {
  const result = spawnSync(ctest, ['--test-dir', build, '--show-only=json-v1', ...args],
    {encoding: 'utf8', timeout: 10000, maxBuffer: 16 * 1024 * 1024});
  assert.equal(result.status, 0, result.error?.message || result.stderr);
  return JSON.parse(result.stdout).tests;
}
const documents = ['CONTRIBUTING.md'];
function walk(directory) {
  for (const entry of fs.readdirSync(path.join(root, directory), {withFileTypes: true})) {
    const file = path.join(directory, entry.name);
    if (entry.isDirectory()) walk(file);
    else if (file.endsWith('.md')) documents.push(file);
  }
}
walk('docs');
const guide = fs.readFileSync(path.join(root, 'CONTRIBUTING.md'), 'utf8');
const heavyBlock = guide.match(/<!-- test-heavy -->\s*([\s\S]*?)<!-- \/test-heavy -->/);
assert(heavyBlock, 'Missing documented heavy membership');
const heavyPatterns = [...heavyBlock[1].matchAll(/^- `([^`]+)`/gm)].map(m => new RegExp(m[1]));
assert(heavyPatterns.length, 'Empty documented heavy membership');

const storageHookAvailable = configuration => configuration.system === 'Linux' &&
  /GNU|Clang/.test(configuration.compiler);
export function checkMixedHooks(tests, controls, instrumented, complete, configuration = contract) {
  const enrollment = new Set(controls);
  for (const name of mixedHookControls) {
    const test = tests.find(t => t.name === name);
    const available = name !== 'sic_test_exact_storage_failure_categories' ||
      storageHookAvailable(configuration);
    if (complete && available) assert(test, `Missing mixed hook control: ${name}`);
    if (!test) continue;
    const selected = instrumented && (name !== 'sic_test_support_cli_failure_categories' ||
      enabled(configuration.flint));
    assert.equal(enrollment.has(name), selected, `Mixed hook enrollment: ${name}`);
    assert.equal(properties(test).LABELS.includes('test_hooks'), selected,
      `Mixed hook label: ${name}`);
  }
}
export function checkTests(tests) {
  assert(tests.length, 'Empty registry');
  const names = new Set(tests.map(t => t.name));
  assert.equal(names.size, tests.length, 'Duplicate test names');
  for (const test of tests) {
    const match = test.name.match(owner);
    assert(match && components.has(match[1]), `Invalid owner/name: ${test.name}`);
    const p = properties(test), labels = p.LABELS || [];
    assert.equal(labels.filter(l => primary.has(l)).length, 1, `Primary label: ${test.name}`);
    assert(labels.includes(match[1]), `Missing owning component: ${test.name}`);
    assert.equal(labels.includes('test_hooks'), hooks.has(test.name), `Hook segregation: ${test.name}`);
    assert(!hooks.has(test.name) || contract.enabled === 1, `Hook control in OFF: ${test.name}`);
    if (hooks.has(test.name))
      assert(!p.DISABLED && p.SKIP_RETURN_CODE === undefined && p.SKIP_REGULAR_EXPRESSION === undefined,
        `Required hook control may not skip: ${test.name}`);
    assert.equal(labels.includes('heavy'), heavyPatterns.some(re => re.test(test.name)),
      `Documented heavy membership: ${test.name}`);
    const long = test.name === gp;
    assert(!long || enabled(contract.gp_d16), 'GP exemption without opt-in');
    assert(Number.isFinite(p.TIMEOUT) && p.TIMEOUT > 0 && p.TIMEOUT <= (long ? 3600 : 120),
      `Timeout: ${test.name}: ${p.TIMEOUT}`);
    if (long) assert.equal(p.TIMEOUT, 3600, 'GP explicit timeout');
  }
  assert.equal(names.has(gp), exact && enabled(contract.gp_d16), 'Opt-in GP enrollment');
  assert.equal(names.has(privateTreeExport),
    fs.existsSync(path.join(root, 'private/scripts/release/test-public-tree.mjs')),
    'Private-tree export test enrollment');
  if (full && contract.enabled === 1)
    for (const name of hooks) assert(names.has(name), `Missing hook control: ${name}`);
  for (const name of ['sic_test_support_json', 'sic_test_support_cli_parser',
    ...(enabled(contract.flint) ? ['sic_test_exact_memory_trace'] : [])]) {
    const test = tests.find(t => t.name === name);
    assert(test && properties(test).LABELS.includes('unit'), `Unit enrollment: ${name}`);
  }
  assert(!names.has('sic_test_support_memory_trace') && !names.has('sic_test_exact_cpp_format'),
    'Stale owner name');
}

// Required witnesses prevent a selector retaining just one incidental match.
// FLINT-only recipes are checked in FLINT registries, not plain builds.
const promises = new Map([
  ['-L docs', {
    family: /^(sic_test_support_(docs_|doc_example_|cli_help_)|sic_test_core_read_fiducial_csv_|sic_test_policy_cpp_format$)/,
    witnesses: ['sic_test_support_docs_links', 'sic_test_support_docs_status',
      'sic_test_support_docs_private', 'sic_test_support_docs_fixture_inventory',
      'sic_test_support_doc_example_0'],
  }],
  ['-L policy', {family: /^sic_test_policy_/, witnesses: ['sic_test_policy_layout', 'sic_test_policy_test_registry']}],
  ['-L hygiene', {family: /^(sic_test_policy_(hygiene|cpp_format)|sic_test_support_public_tree_export)$/,
    witnesses: ['sic_test_policy_hygiene', 'sic_test_policy_cpp_format']}],
  ['-L independence', {
    family: /^sic_test_(support_(independence_|norm_certificate_input_cli_)|verifier_(release_)?d|stabilizer_(text_headers|json_inputs)$|norm_certificate_)/,
    witnesses: ['sic_test_support_independence_gp'],
  }],
  ['-LE heavy', {witnesses: ['sic_test_core_verify']}],
  ['-L test_hooks', {mode: 'hooks', witnesses: ['sic_test_sicx_primitives_and_faults', ...mixedHookControls.filter(name =>
    name !== 'sic_test_exact_storage_failure_categories' || storageHookAvailable(contract))]}],
  ['-L crt', {mode: 'flint', family: /^sic_test_(crt_|support_crt_independent_sources$)/,
    witnesses: [
      ...['valid', 'reject', 'boundaries', 'partial', 'amendments', 'primitives', 'hybrid',
        'targeted', 'general', 'fuzz', 'cli'].map(group => `sic_test_crt_input_${group}`),
      ...['ftello', 'fstat', 'fmpz_set_str', 'arb_intersection', 'qqbar_roots']
        .map(fault => `sic_test_crt_input_fault_${fault}`),
      ...['primes', 'seed', 'prec', 'integral_max_dim', 'partial_metadata']
        .map(option => `sic_test_crt_option_${option}`),
      ...['d4', 'd8', 'd8_maximal', 'd16', 'd32', 'd4_bound_only', 'd16_inject_rep',
        'd16_inject_act', 'd16_inject_conj', 'd16_shift_retry', 'help', 'cli_defaults']
        .map(name => `sic_test_crt_${name}`),
      'sic_test_support_crt_independent_sources',
    ]}],
  ['-R hoggar', {witnesses: ['sic_test_data_hoggar_data', 'sic_test_data_hoggar_gp']}],
]);
function applicable(promise) {
  return promise.mode === 'exact' ? exact : promise.mode === 'flint' ? enabled(contract.flint) :
    promise.mode === 'hooks' ? full && contract.enabled === 1 : true;
}
export function checkSelector(tests, option, pattern, selected) {
  const key = `${option} ${pattern}`, promise = promises.get(key);
  assert(promise, `Undeclared selector expectation: ${key}`);
  const re = new RegExp(pattern);
  const expected = tests.filter(t => ['-R', '-E'].includes(option) ?
    re.test(t.name) === (option === '-R') :
    (properties(t).LABELS || []).some(l => re.test(l)) === (option === '-L'));
  assert(expected.length, `Empty documented selector: ${key}`);
  assert.deepEqual(selected.map(t => t.name).sort(), expected.map(t => t.name).sort(),
    `Unexpected selector set: ${key}`);
  for (const witness of promise.witnesses || [])
    assert(selected.some(t => t.name === witness), `Missing selector witness: ${key}: ${witness}`);
  // Family expectations are independent of the observed LABELS property.
  // In particular, losing one docs/help/checker label must fail even if the
  // selector still finds hundreds of unrelated or remaining topic members.
  if (promise.family) {
    const family = tests.filter(t => promise.family.test(t.name));
    assert(family.length, `Missing family: ${key}`);
    for (const test of family)
      assert(selected.some(t => t.name === test.name), `Missing selector member: ${key}: ${test.name}`);
  }
}

const tests = inventory();
checkTests(tests);
checkMixedHooks(tests, contract.hooks, contract.enabled === 1, full);
export function discoverSelectors(text) {
  const selectors = [];
  const aliases = {'--label-regex': '-L', '--tests-regex': '-R',
    '--exclude-regex': '-E', '--label-exclude': '-LE'};
  // Shell line continuations belong to the same command.
  text = text.replace(/\\\r?\n/g, ' ');
  for (const command of text.matchAll(/\bctest\b[^\n`]*?(?=`|\n|$)/g)) {
    for (const m of command[0].matchAll(/(?:^|\s)(--label-regex|--tests-regex|--exclude-regex|--label-exclude|-LE|-L|-R|-E)(?:\s+|=)(?:'([^']+)'|"([^"]+)"|([^\s`#]+))/g)) {
      selectors.push([aliases[m[1]] || m[1], m[2] ?? m[3] ?? m[4]]);
    }
  }
  return selectors;
}
const selectors = new Map();
for (const document of documents) {
  for (const [option, pattern] of discoverSelectors(fs.readFileSync(path.join(root, document), 'utf8')))
    selectors.set(`${option} ${pattern}`, [option, pattern, document]);
}
assert(selectors.size, 'No documented selectors discovered');
for (const [key, [option, pattern, document]] of selectors) {
  const promise = promises.get(key);
  assert(promise, `${document}: undocumented expected set for ${key}`);
  if (applicable(promise)) checkSelector(tests, option, pattern, inventory([option, pattern]));
}

// Compare actual opposite-mode registrations, including the optional GP test.
// This is configure-only: no counterpart binaries or GP computation are built.
function counterpart() {
  // The claim-registry control can invoke this test concurrently with CTest.
  const peer = fs.mkdtempSync(path.join(build, 'registry-counterpart-'));
  try {
    const cache = fs.readFileSync(path.join(build, 'CMakeCache.txt'), 'utf8');
    const args = [...cache.matchAll(/^(CMAKE_(?:CXX_COMPILER|CXX_FLAGS[^:]*|EXE_LINKER_FLAGS[^:]*|PREFIX_PATH|BUILD_TYPE)|SIC_[A-Z_]+):(?:STRING|BOOL|FILEPATH|PATH|UNINITIALIZED)=(.*)$/gm)]
      .filter(m => !['SIC_ENABLE_TEST_HOOKS', 'SIC_SICX_GP_D16'].includes(m[1]))
      .map(m => `-D${m[1]}=${m[2]}`);
    function configure(gpEnabled) {
      const result = spawnSync('cmake', ['-S', path.join(root, 'cpp'), '-B', peer, ...args,
        `-DSIC_ENABLE_TEST_HOOKS=${contract.enabled === 1 ? 'OFF' : 'ON'}`,
        `-DSIC_SICX_GP_D16=${gpEnabled ? 'ON' : 'OFF'}`],
      {encoding: 'utf8', timeout: 60000, maxBuffer: 4 * 1024 * 1024});
      assert.equal(result.status, 0, `Counterpart configure: ${result.error?.message || result.stdout + result.stderr}`);
      const resultTests = spawnSync(ctest, ['--test-dir', peer, '--show-only=json-v1'],
        {encoding: 'utf8', timeout: 10000, maxBuffer: 16 * 1024 * 1024});
      assert.equal(resultTests.status, 0, resultTests.stderr);
      return {tests: JSON.parse(resultTests.stdout).tests,
        contract: JSON.parse(fs.readFileSync(path.join(peer, 'test-registry-contract.json')))};
    }
    const optional = configure(true);
    const sameOptions = enabled(contract.gp_d16) ? optional : configure(false);
    return {...sameOptions, optional: optional.tests};
  } finally {
    fs.rmSync(peer, {recursive: true, force: true});
  }
}
export function checkHooks(on, off, controls) {
  const offNames = new Set(off.map(t => t.name));
  for (const test of on)
    assert(offNames.has(test.name) || controls.includes(test.name),
      `ON-only test missing hook enrollment: ${test.name}`);
}
export function checkEnrollments(enrollments, tests) {
  const names = new Set(tests.map(t => t.name));
  for (const name of enrollments) assert(names.has(name), `Stale enrollment: ${name}`);
}
if (full) {
  const peer = counterpart();
  const on = contract.enabled === 1 ? {tests, contract} : peer;
  const off = contract.enabled === 1 ? peer.tests : tests;
  checkHooks(on.tests, off, on.contract.hooks);
  checkMixedHooks(peer.tests, peer.contract.hooks, peer.contract.enabled === 1, true, peer.contract);
  checkEnrollments(contract.enrollments, [...tests, ...peer.tests, ...peer.optional]);
  checkEnrollments(peer.contract.enrollments, [...tests, ...peer.tests, ...peer.optional]);
}
if (enabled(contract.flint)) {
  const product = path.join(build, 'sic-test-exact-parallel-ball-product');
  for (const workers of ['1', '2']) {
    const result = spawnSync(product, ['arithmetic', workers], {encoding: 'utf8', timeout: 10000});
    assert.equal(result.status, 1, `Serial parallel-product control: ${result.error?.message}`);
    assert.match(result.stderr, /coverage requires at least 3 native workers/);
    assert.match(result.stdout, new RegExp(`native workers=${workers}\\b`));
  }
}
// These fixtures intentionally share the test-only atomic scratch allocator.
const scratchFiles = ['support/resources.cpp', 'support/parallel.cpp', 'exact/group/closure.cpp',
  'exact/group/group_checkpoint.cpp', 'exact/storage/completed_class_cache.cpp',
  'exact/storage/completed_cache_requirement.cpp', 'exact/storage/table_checkpoint.cpp',
  'exact/analytic/class_native_share.cpp'];
function checkScratch(text) {
  assert(text.includes('sic_test::TemporaryDirectory') && !text.includes('time_since_epoch'),
    'Timestamp scratch name or missing atomic temporary directory');
}
for (const file of scratchFiles) checkScratch(fs.readFileSync(path.join(root, 'cpp/tests', file), 'utf8'));

// Permanent controls exercise the same validator with registry mutations.
function rejects(name, action, reason) {
  assert.throws(action, reason, `Negative control accepted: ${name}`);
}
const wrongTree = tests.some(t => t.name === privateTreeExport)
  ? tests.filter(t => t.name !== privateTreeExport)
  : [...tests, {name: privateTreeExport, properties: [
    {name: 'LABELS', value: ['integration', 'support']}, {name: 'TIMEOUT', value: 120},
  ]}];
rejects('wrong private-tree export enrollment', () => checkTests(wrongTree),
  /Private-tree export test enrollment/);
const noLayer = structuredClone(tests);
noLayer[0].properties.find(p => p.name === 'LABELS').value =
  properties(noLayer[0]).LABELS.filter(l => !primary.has(l));
rejects('missing primary label', () => checkTests(noLayer), /Primary label/);
const typo = structuredClone(tests);
typo[0].name = 'sic_test_exatc_typo';
typo[0].properties.find(p => p.name === 'LABELS').value.push('exatc');
rejects('nonexistent owning component', () => checkTests(typo), /Invalid owner/);
rejects('unlisted ON-only test', () => checkHooks([{name: 'sic_test_support_unlisted'}], [], []),
  /ON-only test/);
rejects('stale enrollment', () => checkEnrollments(['sic_test_exact_removed'], tests), /Stale enrollment/);
rejects('timestamp scratch name', () => checkScratch('sic_test::TemporaryDirectory; time_since_epoch'),
  /Timestamp scratch/);
for (const [command, option] of [['--label-regex', '-L'], ['--tests-regex', '-R'], ['-E', '-E']]) {
  for (const join of [' ', ' \\\n ']) {
    const found = discoverSelectors(`ctest${join}${command} nonexistent_topic`);
    assert.deepEqual(found, [[option, 'nonexistent_topic']]);
    rejects(`selector ${command}`, () => checkSelector(tests, ...found[0], []), /Undeclared selector/);
  }
}
for (const name of mixedHookControls) {
  const fixture = [{name, properties: [{name: 'LABELS', value: []}]}];
  rejects(`unenrolled mixed hook ${name}`,
    () => checkMixedHooks(fixture, [], true, false, {...contract, flint: 'ON'}), /Mixed hook enrollment/);
  rejects(`unlabelled mixed hook ${name}`,
    () => checkMixedHooks(fixture, [name], true, false, {...contract, flint: 'ON'}), /Mixed hook label/);
}
// Plain instrumented builds retain the CLI matrix without exact hook controls.
checkMixedHooks([{name: 'sic_test_support_cli_failure_categories',
  properties: [{name: 'LABELS', value: []}]}], [], true, false, {flint: 'OFF'});
// The storage fault executable is registered only for its supported linker ABI.
for (const configuration of [{flint: 'ON', system: 'Darwin', compiler: 'Clang'},
  {flint: 'ON', system: 'Linux', compiler: 'MSVC'}]) {
  const available = mixedHookControls.filter(name => name !== 'sic_test_exact_storage_failure_categories');
  checkMixedHooks(available.map(name => ({name, properties: [{name: 'LABELS', value: ['test_hooks']}]})),
    available, true, true, configuration);
}
// Exercise the actual CMake final pass, before it could hide registration mistakes.
const controls = fs.mkdtempSync(path.join(build, 'registry-controls-'));
try {
  for (const [name, registration, reason] of [
    ['owner', 'add_test(NAME sic_test_exatc_typo COMMAND true)\nset_tests_properties(sic_test_exatc_typo PROPERTIES LABELS unit)', /Unknown test component/],
    ['primary', 'add_test(NAME sic_test_core_unlabeled COMMAND true)', /Missing explicit primary layer/],
    ['timeout', 'add_test(NAME sic_test_core_verify COMMAND true)\nset_tests_properties(sic_test_core_verify PROPERTIES LABELS unit TIMEOUT 300)', /Local TIMEOUT exceeds/],
  ]) {
    const directory = path.join(controls, name);
    fs.mkdirSync(directory, {recursive: true});
    fs.writeFileSync(path.join(directory, 'CMakeLists.txt'), `cmake_minimum_required(VERSION 3.20)
project(registry_control LANGUAGES NONE)
enable_testing()
set(SIC_TEST_HOOKS_MODE 0)
include("${path.join(root, 'cpp/cmake/checks/TestLayers.cmake')}")
${registration}
sic_label_tests()
`);
    const result = spawnSync('cmake', ['-S', directory, '-B', path.join(directory, 'build')],
      {encoding: 'utf8', timeout: 10000});
    assert.notEqual(result.status, 0, `Negative CMake control accepted: ${name}`);
    assert.match(result.stdout + result.stderr, reason);
  }
} finally {
  fs.rmSync(controls, {recursive: true, force: true});
}
const duplicate = structuredClone(tests);
duplicate[0].properties.find(p => p.name === 'LABELS').value.push(
  properties(duplicate[0]).LABELS.includes('unit') ? 'integration' : 'unit');
rejects('two primary labels', () => checkTests(duplicate), /Primary label/);
const timeout = structuredClone(tests), ordinary = timeout.find(t => t.name !== gp);
ordinary.properties.find(p => p.name === 'TIMEOUT').value = 121;
rejects('ordinary timeout', () => checkTests(timeout), /Timeout/);
const empty = tests.filter(t => !(properties(t).LABELS || []).includes('docs'));
rejects('empty selector', () => checkSelector(empty, '-L', 'docs', []), /Empty documented selector/);
const missing = structuredClone(tests);
const status = missing.find(t => t.name === 'sic_test_support_docs_status');
status.properties.find(p => p.name === 'LABELS').value = properties(status).LABELS.filter(l => l !== 'docs');
rejects('missing topic member', () => checkSelector(missing, '-L', 'docs',
  missing.filter(t => properties(t).LABELS.includes('docs'))), /Missing selector (witness|member)/);
if (contract.enabled === 1 && full) {
  const skip = structuredClone(tests), control = skip.find(t => hooks.has(t.name));
  control.properties.push({name: 'SKIP_RETURN_CODE', value: 0});
  rejects('successful hook skipped', () => checkTests(skip), /Required hook control may not skip/);
}
console.log(`Test registry policy PASS: ${tests.length} tests, ${selectors.size} selectors; negative controls rejected`);

console.log('SIC_NEGATIVE_CONTROL:test-registry');
