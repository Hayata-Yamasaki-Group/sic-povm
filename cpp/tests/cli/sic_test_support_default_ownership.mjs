// SPDX-License-Identifier: AGPL-3.0-or-later
// Production CLI declarations are discovered recursively, including delegated
// parsers in src/. The independent checkers' own parsers are excluded by design.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {discover, inspect} from './ownership_policy.mjs';
const [repo, bin] = process.argv.slice(2);
const files = discover(repo);
const audit = inspect(files);
const failures = [...audit.violations];
function check(condition, message) { if (!condition) failures.push(message); }
// Negative controls run in memory, never rewriting production sources.
const control = `const int kDefaultCount = 4, kMinimumCount = 1, kMaximumCount = 64;
const Parser parser{"control", {{"--count", "N", "Count (default " + default_text(kDefaultCount) + "; " + default_text(kMinimumCount) + ".." + default_text(kMaximumCount) + ").", 1, 1}}};
const auto n = args.numeric<int>("--count", kDefaultCount, kMinimumCount, kMaximumCount);
`;
const controlFile = 'cpp/apps/future_parser/nested/command_line.cpp';
const baseline = new Map([[controlFile, control]]);
assert.deepEqual(inspect(baseline).violations, []);
for (const [name, from, to, expected] of [
  ['hidden arbitrary string', 'kDefaultCount, kMinimumCount', '"hidden-value", kMinimumCount', 'fallback lacks named owner'],
  ['copied numeric default', 'kDefaultCount, kMinimumCount', '4, kMinimumCount', 'fallback lacks named owner'],
  ['copied range', 'kMinimumCount, kMaximumCount);', 'kMinimumCount, 65);', 'unnamed range bound'],
  ['wrong range owner', 'kMinimumCount, kMaximumCount);', 'kMinimumCount, kWrongMaximum);', 'help does not consume range owner'],
  ['hidden default owner', 'default_text(kDefaultCount)', 'default_text(kOtherDefault)', 'help does not consume fallback owner'],
  ['changed fallback expression', 'kDefaultCount, kMinimumCount', 'kDefaultCount+7, kMinimumCount', 'fallback expression differs'],
  ['parenthesized copied range', 'kMinimumCount, kMaximumCount);', 'kMinimumCount, (65));', 'unnamed range bound'],
  ['aliased copied range', 'const auto n =', 'const auto upper=65; const auto n =', null],
  ['computed named range', 'kMinimumCount, kMaximumCount);', 'kMinimumCount, kMaximumCount+7);', 'computed range'],
  ['changed native headroom', 'kMinimumCount, kMaximumCount);', 'kMinimumCount, LONG_MAX-123);', 'unnamed range bound'],
  ['changed model field', 'kMinimumCount, kMaximumCount);', 'kMinimumCount, options_.limits.maximum_retries);', 'dynamic bound not documented'],
  ['changed unit shift', 'kDefaultCount, kMinimumCount', 'kDefaultCount>>19, kMinimumCount', 'fallback expression differs'],
  ['owner mention without default', 'Count (default ', 'Count (owner ', 'help does not identify omitted default'],
  ['subtracted fallback', 'kDefaultCount, kMinimumCount', 'kDefaultCount-1, kMinimumCount', 'fallback expression differs'],
  ['called fallback', 'kDefaultCount, kMinimumCount', 'std::min(kDefaultCount,99), kMinimumCount', 'fallback expression differs'],
  ['conditional fallback', 'kDefaultCount, kMinimumCount', 'condition?kDefaultCount:99, kMinimumCount', 'fallback expression differs'],
  ['subtracted bounds', 'kMinimumCount, kMaximumCount);', 'kMinimumCount, kMaximumCount-kMinimumCount);', 'computed range'],
  ['conditional bounds', 'kMinimumCount, kMaximumCount);', 'kMinimumCount, condition?kMaximumCount:kMinimumCount);', 'computed range'],
  ['qualified wrong owner', 'kDefaultCount, kMinimumCount', 'unrelated::kDefaultCount, kMinimumCount', 'help does not consume fallback owner'],
  ['unrelated optional keyword', 'kDefaultCount, kMinimumCount', '0, kMinimumCount', 'undocumented absence sentinel'],
  ['member subtraction', 'kMinimumCount, kMaximumCount);', 'kMinimumCount, kMaximumCount.limit-kDefaultCount);', 'computed range'],
  ['member division', 'kMinimumCount, kMaximumCount);', 'kMinimumCount, kMaximumCount.limit/kDefaultCount);', 'computed range'],
  ['unrelated member', 'kMinimumCount, kMaximumCount);', 'kMinimumCount, unrelated.limits.maximum_jobs);', 'dynamic bound not documented'],
  ['copied help', '"Count (default "', '"Count (default 4 "', 'copied help default/range'],
]) {
  assert(control.includes(from), name);
  const mutated = new Map(baseline); mutated.set(controlFile, name==='aliased copied range' ? control.replace(from,to).replace('kMinimumCount, kMaximumCount);','kMinimumCount, upper);') : control.replace(from, to));
  assert(inspect(mutated).violations.some(x => x.includes(expected || 'unnamed range bound')), name);
}
const helperControl=control.replace('const auto n = args.numeric<int>("--count", kDefaultCount, kMinimumCount, kMaximumCount);',
  'auto bounded = [&](const char* flag, int lower, int upper) { return cli::number<int>(args.get(flag), flag, lower, upper); }; const auto n = bounded("--count", kMinimumCount, kMaximumCount);');
assert.deepEqual(inspect(new Map([[controlFile,helperControl]])).violations, []);
assert(inspect(new Map([[controlFile,helperControl.replace('bounded("--count", kMinimumCount, kMaximumCount)', 'bounded("--count", kMinimumCount, 999)')]])).violations.some(x=>x.includes('unnamed range bound')));
const variableControl=control.replace('const auto n =', 'const auto flag="--count"; const auto n =').replace('numeric<int>("--count",','numeric<int>(flag,').replace('kMinimumCount, kMaximumCount);','kMinimumCount, 65);');
assert(inspect(new Map([[controlFile,variableControl]])).violations.some(x=>x.includes('unnamed range bound')));
const detached=new Map([[controlFile,control.replace(/const Parser[^\n]+\n/,'')],['cpp/apps/unrelated/command_line.cpp',control.split('\n').slice(0,2).join('\n')]]);
assert(inspect(detached).violations.some(x=>x.includes('no parser help declaration')));
const typedHelper=helperControl.replace('int upper) {', 'int upper) -> int {');
assert(inspect(new Map([[controlFile,typedHelper]])).declarations.length > 0);
assert(inspect(new Map([[controlFile,typedHelper.replace('bounded("--count", kMinimumCount, kMaximumCount)', 'bounded("--count", kMinimumCount, 999)')]])).violations.length > 0);
const unresolved=control.replace('const auto n = args.numeric<int>("--count", kDefaultCount, kMinimumCount, kMaximumCount);', 'const auto n = cli::number<int>(args.get(flag), flag, 1, 999);');
assert(inspect(new Map([[controlFile,unresolved]])).violations.some(x=>x.includes('unresolved numeric option')));
const aliased=control.replace('const auto n =','const auto fallback=kDefaultCount+7; const auto n =').replace('numeric<int>("--count", kDefaultCount,','numeric<int>("--count", fallback,');
assert(inspect(new Map([[controlFile,aliased]])).violations.some(x=>x.includes('fallback expression differs')));
const stringControl='const char* kDefaultPath="data"; const Parser parser{"tool", {{"--path", "FILE", "Path (default " + default_text(kDefaultPath) + ").",1,1}}}; const auto value=args.get("--path", kDefaultPath);';
assert.deepEqual(inspect(new Map([[controlFile,stringControl]])).violations,[]);
for(const expr of ['kDefaultPath+".wrong"','std::string("OTHER/REPOSITORY")+kDefaultPath'])
  assert(inspect(new Map([[controlFile,stringControl.replace('get("--path", kDefaultPath)','get("--path", '+expr+')')]])).violations.some(x=>x.includes('string fallback expression')));
for(const declaration of ['sic::support::cli::Arguments args;', 'using Input = sic::support::cli::Arguments; Input args;', 'const auto args=parser.parse(argc,argv);']) {
  const source=stringControl.replace('const auto value=args.get("--path", kDefaultPath);',declaration+' const auto value=args.get(flag,kDefaultPath);');
  assert(inspect(new Map([[controlFile,source]])).violations.some(x=>x.includes('unresolved numeric option')));
}
const unrelatedGet=stringControl.replace('const auto value=args.get("--path", kDefaultPath);','Other values; const auto value=values.get(flag,kDefaultPath);');
assert.deepEqual(inspect(new Map([[controlFile,unrelatedGet]])).violations,[]);
// Outer conditional fallbacks must be audited even when get() has no fallback.
const conditionalString = stringControl.replace('args.get("--path", kDefaultPath)',
  'args.has("--path") ? args.get("--path") : kDefaultPath');
const conditionalNumeric = control.replace('args.numeric<int>("--count", kDefaultCount, kMinimumCount, kMaximumCount)',
  'args.has("--count") ? cli::number<int>(args.get("--count"), "--count", kMinimumCount, kMaximumCount) : kDefaultCount');
const wrappedString = conditionalString.replace('args.has("--path")', '((args.has("--path")))');
const wrappedNumeric = conditionalNumeric.replace('args.has("--count")', '((args.has("--count")))');
for (const source of [conditionalString, conditionalNumeric, wrappedString, wrappedNumeric]) {
  const result = inspect(new Map([[controlFile,source]]));
  assert.deepEqual(result.violations, []);
  assert.equal(result.declarations.filter(d => d.conditional).length, 1);
}
for (const [name, source] of [
  ['hidden conditional string', conditionalString.replace(': kDefaultPath', ': "hidden-value"')],
  ['hidden conditional number', conditionalNumeric.replace(': kDefaultCount', ': 17')],
  ['parenthesized string condition', wrappedString.replace(': kDefaultPath', ': "hidden-value"')],
  ['parenthesized numeric condition', wrappedNumeric.replace(': kDefaultCount', ': 17')],
  ['wrong conditional string owner', conditionalString.replace(': kDefaultPath', ': kOtherPath')],
  ['wrong conditional numeric owner', conditionalNumeric.replace(': kDefaultCount', ': kOtherCount')],
  ['conditional string suffix', conditionalString.replace(': kDefaultPath', ': kDefaultPath+".wrong"')],
  ['conditional numeric expression', conditionalNumeric.replace(': kDefaultCount', ': kDefaultCount+7')],
  ['wrapped conditional string', conditionalString.replace('args.get("--path")', 'filesystem::path(args.get("--path"))').replace(': kDefaultPath', ': "hidden-value"')],
  ['nested conditional number', conditionalNumeric.replace(': kDefaultCount', ': condition ? kDefaultCount : 17')],
]) assert(inspect(new Map([[controlFile,source]])).violations.length > 0, name);
// Reversed conditional arms and initialization followed by guarded assignment
// carry the same omitted-option default and must obey the same owner checks.
const reversedString = conditionalString.replace('args.has("--path") ? args.get("--path") : kDefaultPath',
  '!args.has("--path") ? kDefaultPath : args.get("--path")');
const reversedNumeric = conditionalNumeric.replace('args.has("--count") ? ', '!args.has("--count") ? kDefaultCount : ')
  .replace(' : kDefaultCount;', ';');
const assignedString = stringControl.replace('const auto value=args.get("--path", kDefaultPath);',
  'auto value=kDefaultPath; if (args.has("--path")) value=args.get("--path");');
const assignedNumeric = control.replace('const auto n = args.numeric<int>("--count", kDefaultCount, kMinimumCount, kMaximumCount);',
  'int n=kDefaultCount; if (args.has("--count")) n=cli::number<int>(args.get("--count"), "--count", kMinimumCount, kMaximumCount);');
for (const source of [reversedString, reversedNumeric, assignedString, assignedNumeric,
  reversedString.replace('!args.has("--path")', '(!(args.has("--path")))'),
  assignedString.replace(')) value=', ')) { value=').replace('get("--path");', 'get("--path"); }')]) {
  const result=inspect(new Map([[controlFile,source]]));
  assert.deepEqual(result.violations, []);
  assert.equal(result.declarations.filter(d=>d.conditional).length, 1);
  for (const bad of ['"hidden-value"', '17', 'kWrongOwner', 'kDefaultPath+"suffix"']) {
    const mutated=source.replace(/(?<=\? |value=|n=)kDefault(?:Path|Count)/, bad);
    assert.notEqual(mutated, source);
    assert(inspect(new Map([[controlFile,mutated]])).violations.length > 0, bad+' in '+source);
  }
}
for (const source of [
  assignedString.replace('value=kDefaultPath;', 'value="unrelated";').replace('value=args.get', 'other=args.get'),
  assignedString.replace('value=kDefaultPath;', 'value="unrelated";').replace('value=args.get', 'value=other.get'),
  reversedString.replace('kDefaultPath :', '"unrelated" :').replace('args.get("--path")', 'other.get("--path")'),
]) assert.deepEqual(inspect(new Map([[controlFile,source]])).violations, []);
const conditionalPath = conditionalString.replace(': kDefaultPath',
  ': filesystem::path(args.get("--root", std::string(kDefaultRoot))) / spec / kDefaultPath');
// Supply the separate root option's own help, as real container parsing does.
const pathControl = conditionalPath + ' const Parser roots{"tool", {{"--root", "DIR", "Root (default " + default_text(kDefaultRoot) + ").",1,1}}};';
assert.deepEqual(inspect(new Map([[controlFile,pathControl]])).violations, []);
for (const fallback of ['kDefaultPath+7', 'kDefaultPath/2', '"hidden-value"'])
  assert(inspect(new Map([[controlFile,pathControl.replace('/ spec / kDefaultPath', '/ spec / '+fallback)]])).violations.length > 0, 'computed conditional path '+fallback);
for (const root of ['std::string(kDefaultRoot)+"wrong"', 'std::string(kDefaultRoot+7)'])
  assert(inspect(new Map([[controlFile,pathControl.replace('std::string(kDefaultRoot)',root)]])).violations.length > 0, 'changed conditional root '+root);
assert(inspect(new Map([[controlFile,pathControl.replace('args.get("--root"', 'other.get("--root"')]])).violations.length > 0, 'unrelated root accessor');
// An unrelated has()/get() pair and a required accessor introduce no default.
for (const source of [
  conditionalString.replace('args.get("--path")', 'other.get("--path")').replace(': kDefaultPath', ': "unrelated"'),
  stringControl.replace('args.get("--path", kDefaultPath)', 'args.get("--path")'),
]) assert.deepEqual(inspect(new Map([[controlFile,source]])).violations, []);
// Prove discovery includes a newly introduced nested app and excludes checkers.
const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'sic-cli-ownership-'));
try {
  fs.mkdirSync(path.join(temporary, 'cpp/apps/future_parser/nested'), {recursive: true});
  fs.mkdirSync(path.join(temporary, 'cpp/src'), {recursive: true});
  fs.mkdirSync(path.join(temporary, 'cpp/checkers'), {recursive: true});
  fs.writeFileSync(path.join(temporary, controlFile), control.replace('kMinimumCount, kMaximumCount);', 'kMinimumCount, 65);'));
  fs.writeFileSync(path.join(temporary, 'cpp/checkers/control.cpp'), control);
  const discovered = discover(temporary);
  assert.equal(discovered.size, 1);
  assert(inspect(discovered).violations.some(x => x.includes('unnamed range bound')));
} finally { fs.rmSync(temporary, {recursive: true, force: true}); }
// Check actual positional fallback output as well as source ownership.
const checkTool = path.join(bin, 'sic-check');
const h = spawnSync(checkTool, ['--help'], {encoding: 'utf8', timeout: 10000});
assert.ifError(h.error);
assert.equal(h.status, 0, h.stderr);
const tolerance = Number(h.stdout.match(/TOL=([^\]]+)/)[1]);
const result = spawnSync(checkTool, [path.join(repo, 'cpp/tests/fixtures/core/d4_sic_from_ghost.txt')],
  {encoding: 'utf8', timeout: 10000});
assert.ifError(result.error);
assert.equal(result.status, 0, result.stderr);
check(Number(result.stdout.match(/tol = ([^)]+)/)[1]) === tolerance, 'sic-check positional tolerance default');
const ballTool = path.join(bin, 'sic-check-balls');
if (fs.existsSync(ballTool)) {
  const help = spawnSync(ballTool, ['--help'], {encoding: 'utf8', timeout: 10000});
  assert.ifError(help.error);
  assert.equal(help.status, 0, help.stderr);
  const value = option => Number(help.stdout.split('\n').find(line => line.startsWith(`  --${option} `)).match(/\(default (\d+)(?:[;)])/)[1]);
  const checked = spawnSync(ballTool, [path.join(repo, 'cpp/tests/fixtures/numeric/d8/m2/c0/normalized-gauged.balls')],
    {encoding: 'utf8', timeout: 10000});
  assert.ifError(checked.error);
  assert.equal(checked.status, 0, checked.stderr);
  check(Number(checked.stdout.match(/working_bits=(\d+)/)[1]) === value('working-bits'), 'ball working-bits default');
  check(Number(checked.stdout.match(/required<2\^-(\d+)/)[1]) === value('bits'), 'ball accuracy-bits default');
}
assert.deepEqual(failures, [], 'default/range ownership violations');
console.log('PASS ' + audit.options.length + ' option declarations, ' + audit.declarations.length + ' parser calls; discovery and helper, alias, expression, namespace and omission negative controls');
console.log("EXCLUDED defaults in the independent checkers' own parsers");
