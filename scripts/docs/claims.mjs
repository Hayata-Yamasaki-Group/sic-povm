// SPDX-License-Identifier: AGPL-3.0-or-later
// Source census: parser declarations/reads, runtime settings and public CMake inputs.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {root,read,files,equalSet,refuses} from './runtime_checks.mjs';
import {optionDeclarations,optionReads,compareOptions} from './cli_contract.mjs';
import {brace,sourceOptions} from './contracts.mjs';
const groups={
  'sic-check':['cpp/apps/check/main.cpp'],
  'sic-overlaps':['cpp/apps/overlaps/command_line.cpp'],
  'sic-identify':['cpp/apps/identify/command_line.cpp'],
  'sic-stabilizer':['cpp/apps/stabilizer/command_line.cpp'],
  'sic-construct':['cpp/src/construct/runtime/command_line.cpp'],
  'sic-decode':['cpp/apps/decode/command_line.cpp','cpp/src/access/source_arguments.cpp'],
  'sic-verify':['cpp/apps/verify/command_line.cpp','cpp/src/access/source_arguments.cpp'],
  'sic-inspect':['cpp/apps/inspect/command_line.cpp'],
  'sic-container':['cpp/apps/container/command_line.cpp','cpp/src/access/source_arguments.cpp'],
  'sic-data':['cpp/apps/data/main.cpp','cpp/src/data/numeric.cpp'],
  'sic-check-balls':['cpp/apps/check_balls/main.cpp','cpp/src/bounds/check_balls.cpp'],
};
// Each entry is a production violation, keyed by command/rule. Stale entries fail.
const exceptions={};
let census={};
for(const [tool,paths] of Object.entries(groups)) {
  const text=paths.map(p=>read(path.join(root,p))).join('\n');
  const declared=optionDeclarations(text), used=optionReads(text);
  census[tool]={declared,read:[...used].sort(),findings:[...Object.keys(declared).filter(n=>!used.has(n)).map(n=>`unread ${n}`),...['']].filter(Boolean)};
  if(process.argv.includes('--inventory')) continue;
  compareOptions(declared,used,exceptions[tool]);
}
refuses(()=>compareOptions({'--unused':{}},new Set()),/options:/);
refuses(()=>compareOptions({},new Set(['--unknown'])),/options:/);
refuses(()=>compareOptions({},new Set(),['unread --fixed']),/options:/);
if(process.argv.includes('--inventory')) {console.log(JSON.stringify(census,null,2)); process.exit(0);}
const production=['cpp/src','cpp/include','cpp/apps','cpp/checkers'].flatMap(p=>files(path.join(root,p)))
  .filter(p=>/\.(cpp|hpp|tpp|gp)$/.test(p) && !p.includes('/tests/') && !p.includes('/self_test/'));
export function settingReads(s) {
  const constants=new Map([...s.matchAll(/\b([A-Za-z_]\w*)\s*(?:\[\s*\])?\s*(?:=|\{)\s*"([A-Z][A-Z0-9_]+)"/g)].map(m=>[m[1],m[2]]));
  for(const [name,value]of constants)s=s.replace(new RegExp("\\b"+name+"\\b(?=\\s*\\))","g"),JSON.stringify(value));
  return [...s.matchAll(/\b(?:getenv|kernel_setting|env_u64|raw|present|one|nonzero|unsigned_or|optional_unsigned|optional_threads|binary|enabled)\s*\(\s*"([A-Z][A-Z0-9_]+)"/g)].map(m=>m[1]);
}
refuses(()=>equalSet(settingReads('constexpr const char* kInput = "SIC_UNDOCUMENTED"; auto value = std::getenv(kInput);'),[],'settings: named constant'),/settings:/);
const contracts=JSON.parse(read(path.join(root,'docs/cli_contract.json')));
const enrolled=new Set(['cpp/src/support/text/cli.cpp','cpp/src/support/text/tool_version.cpp',...Object.values(groups).flat(),...contracts.flatMap(r=>r.sources)]);
for(const p of production) {
  const text=read(p), relative=path.relative(root,p);
  if(Object.keys(optionDeclarations(text)).length || optionReads(text).size || /==\s*"--[a-z]/.test(text))
    assert(enrolled.has(relative),`options: unenrolled parser/read owner ${relative}`);
}
for(const row of contracts.filter(r=>r.independent)) {
  const text=row.sources.map(p=>read(path.join(root,p))).join('\n');
  equalSet([...text.matchAll(/==\s*"(--[a-z0-9-]+)"/g)].map(m=>m[1]),Object.keys(row.options),'options: independent checker declarations/reads');
}
function body(text,marker) {
  const start=text.indexOf(marker);assert(start>=0,`options: missing route owner ${marker}`);
  return brace(text,text.indexOf('{',start));
}
const container=read(path.join(root,'cpp/apps/container/command_line.cpp'));
const data=read(path.join(root,'cpp/src/data/numeric.cpp'));
for(const tool of ['sic-container','sic-data']) {
  const source=read(path.join(root,tool==='sic-container'?'cpp/apps/container/command_line.cpp':'cpp/apps/data/main.cpp'));
  const anchor=/\b(?:static\s+)?const\s+std::map\s*<\s*std::string_view\s*,\s*(?:sic::support::cli::)?Parser\s*>\s+\w+\s*(?:=\s*)?\{/g;
  const tables=[...source.matchAll(anchor)];
  assert.equal(tables.length,1,`options: expected one string_view-to-Parser command table in ${tool}, found ${tables.length}`);
  const start=tables[0].index;
  const dispatch=brace(source,source.indexOf('{',start));
  equalSet([...dispatch.matchAll(/\{\s*"([a-z-]+)"\s*,/g)].map(m=>m[1]).filter(n=>!n.startsWith('--')),contracts.filter(r=>r.tool===tool&&r.command).map(r=>r.command),'options: subcommand census');
}
const routeBodies={
 'sic-container':{
  split:body(container,'Invocation::split_options()'),
  fetch:body(container,'setup(const Args&')+body(container,'Invocation::fetch_options()'),
  repair:body(container,'setup(const Args&')+body(container,'Invocation::repair_job()'),
  'read-range':body(read(path.join(root,'cpp/src/access/source_arguments.cpp')),'SourceArguments::SourceArguments('),
  reencode:body(container,'Invocation::reencode_options()')+body(container,'Invocation::reencode_policy()'),
  'from-text':'','merge-parts':'','merge-element-parts':''
 },
 'sic-data':{
  formats:'',write:body(data,'int numeric_write('),check:body(data,'int numeric_check('),
  bundle:body(data,'int npz(')+body(data,'std::vector<Member> members_of('),
  'check-bundle':body(data,'int npz_check(')+body(data,'std::vector<Member> members_of('),
  layout:body(data,'if (command == "layout")'),catalogue:body(data,'if (command == "catalogue")'),
  list:body(data,'int catalogue_multiplets(')
 }
};
for(const row of contracts.filter(r=>r.command)) {
  const declarations=sourceOptions(row);delete declarations['--help'];delete declarations['--version'];
  compareOptions(declarations,optionReads(routeBodies[row.tool][row.command]));
}
const environmentSites = paths => paths.flatMap(p =>
  settingReads(read(p)).map(name => [name, path.relative(root, p)]));
const environment=new Map(environmentSites(production));
// The producer's developer-setting loop forwards these literal names to getenv(key).
const producer=read(path.join(root,'cpp/src/construct/runtime/command_line.cpp'));
const devLoop=producer.match(/for \(const char\* key : \{([^}]+)\}\)\s*if \(const char\* value = std::getenv\(key\)\)/);
assert(devLoop,'settings: developer forwarding loop changed; update its census');
for(const m of devLoop[1].matchAll(/"([A-Z][A-Z0-9_]+)"/g)) environment.set(m[1],'cpp/src/construct/runtime/command_line.cpp');
const cmakeInputs=new Map();
const cmakeFiles=files(path.join(root,'cpp')).filter(p=>p.endsWith('.cmake') || p.endsWith('CMakeLists.txt'));
const cmakeBodies=cmakeFiles.filter(p=>!p.includes('/tests/install/')).map(p=>read(p).replace(/^.*(?:(?:option|cmake_dependent_option)\(|CACHE (?:STRING|BOOL|PATH|FILEPATH)).*$/gm,'')).join('\n');
for(const p of files(path.join(root,'cpp')).filter(p=>p.endsWith('.cmake') || p.endsWith('CMakeLists.txt'))) {
  if(p.includes('/tests/install/')) continue;
  const s=read(p);
  for(const m of s.matchAll(/\b(?:(?:option|cmake_dependent_option)\(\s*((?:SIC_|FLINT_)[A-Z0-9_]+)|set\(\s*((?:SIC_|FLINT_)[A-Z0-9_]+)\s+[^\n]*?\bCACHE\s+(?:STRING|PATH|BOOL|FILEPATH))/g)) {
    const name=m[1]||m[2]; cmakeInputs.set(name,path.relative(root,p));
    const body=s.replace(m[0],'');
    const all=cmakeBodies + production.map(f=>read(f)).filter(t=>t.includes(name)).join('\n');
    assert(new RegExp(`(?:\\$\\{${name}\\}|\\b(?:if|elseif)\\([^)]*\\b${name}\\b)`).test(all),`settings: unread CMake input ${name}`);
  }
}
refuses(()=>equalSet([...('cmake_dependent_option(SIC_NEW_INPUT "description" ON "TRUE" OFF)'.matchAll(/\b(?:option|cmake_dependent_option)\(\s*((?:SIC_|FLINT_)[A-Z0-9_]+)/g))].map(m=>m[1]),[],'settings: dependent option'),/settings:/);
const config=read(path.join(root,'docs/configuration.md'));
function documented(text,kind) {return [...text.matchAll(new RegExp('^\\| '+kind+' \\| `([A-Z][A-Z0-9_]+)` \\|','gm'))].map(m=>m[1]);}
equalSet(environment.keys(),documented(config,'environment'),'settings: environment census differs from docs');
equalSet(cmakeInputs.keys(),documented(config,'CMake'),'settings: CMake census differs from docs');
// Omitting template readers must fail the same census, rather than silently
// making a real runtime setting disappear from the documented contract.
refuses(() => equalSet(environmentSites(production.filter(file => !file.endsWith('.tpp')))
  .map(([name, owner]) => name + ':' + owner),
  environmentSites(production).map(([name, owner]) => name + ':' + owner),
  'settings: omitted template reader'), /settings: omitted template reader/);
// Prefix scans inspect the environment directly and are not literal getenv reads.
function checkEnvironmentPrefixes(document, source = relative => read(path.join(root, relative))) {
  const actual = production.flatMap(file => {
    const relative = path.relative(root, file);
    return [...source(relative).matchAll(/std::string_view\(\*entry\)\.starts_with\("([A-Z][A-Z0-9_]+)"\)/g)]
      .map(match => `${match[1]}*:${relative}`);
  });
  const listed = document.split('\n').filter(line => line.startsWith('| environment prefix |'))
    .map(line => {
      const cells = line.split('|').slice(1, -1).map(cell => cell.trim().replaceAll('`', ''));
      return `${cells[1]}:${cells[5]}`;
    });
  equalSet(actual, listed, 'settings: environment prefix census differs from docs');
}
checkEnvironmentPrefixes(config);
refuses(() => checkEnvironmentPrefixes(config.replace(/^\| environment prefix \|.*\n/gm, '')),
  /settings: environment prefix/);
refuses(() => checkEnvironmentPrefixes(config, relative => read(path.join(root, relative))
  .replace('starts_with("MALLOC_")', 'starts_with("UNDOCUMENTED_")')),
  /settings: environment prefix/);
// Names alone cannot catch a stale read-owner path in the configuration table.
function checkOwners(document,source=relative=>read(path.join(root,relative))) {
  for(const line of document.split('\n').filter(l=>/^\| (CMake|environment) \|/.test(l))) {
    const cells=line.split('|').slice(1,-1).map(c=>c.trim());
    const name=cells[1].replaceAll('`',''),owner=cells[5].replaceAll('`','');
    const text=source(owner);
    if(cells[0]==='environment') {
      const forwarded=owner==='cpp/src/construct/runtime/command_line.cpp'&&
        [...devLoop[1].matchAll(/"([A-Z][A-Z0-9_]+)"/g)].some(m=>m[1]===name)&&
        text.includes(devLoop[0]);
      assert(settingReads(text).includes(name)||forwarded,`settings: wrong read owner ${name}: ${owner}`);
    } else {
      assert(new RegExp('(?:option|cmake_dependent_option|set)\\(\\s*'+name+'\\b[^\\n]*(?:CACHE|"[^"\\n]*"\\s+(?:ON|OFF|\\$\\{))').test(text),
        `settings: wrong CMake owner ${name}: ${owner}`);
    }
  }
}
checkOwners(config);
refuses(()=>checkOwners(config.replace('| `cpp/src/construct/model/options.cpp` |',
  '| `cpp/include/sic/error.hpp` |')),/settings: wrong read owner/);
refuses(()=>checkOwners(config,relative=>read(path.join(root,relative)).replace(
  'raw("SIC_THREADS")','raw("SIC_UNDOCUMENTED_THREADS")')),/settings: wrong read owner/);
refuses(()=>checkOwners(config.replace('| `cpp/checkers/verifier/CMakeLists.txt` |',
  '| `cpp/cmake/components/Core.cmake` |')),/settings: wrong CMake owner/);
refuses(()=>checkOwners(config,relative=>read(path.join(root,relative)).replace(
  'option(SIC_BUILD_TESTS','option(SIC_UNDOCUMENTED_BUILD_TESTS')),/settings: wrong CMake owner/);
refuses(()=>equalSet([...environment.keys(),'SIC_UNDOCUMENTED'],documented(config,'environment'),'settings: new environment'),/settings:/);
refuses(()=>equalSet(cmakeInputs.keys(),[...documented(config,'CMake'),'SIC_UNUSED'],'settings: stale CMake'),/settings:/);
console.log(`PASS source claims: ${Object.keys(groups).length} parser groups, ${environment.size} environment variables, ${cmakeInputs.size} CMake inputs; negative controls rejected`);

console.log('SIC_NEGATIVE_CONTROL:source-claims');
