// SPDX-License-Identifier: AGPL-3.0-or-later
// Bounded lexical naming checks; semantic roles and dynamic forwarding remain review obligations.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';

const externalEnvironment = new Set(['HOME', 'XDG_CACHE_HOME', 'GLIBC_TUNABLES',
  'SLURM_CPUS_PER_TASK', 'SLURM_MEM_PER_NODE', 'SLURM_MEM_PER_CPU']);
const pascal = /^[A-Z][A-Za-z0-9]*$/;
const constant = /^k[A-Z][A-Za-z0-9]*$/;
function tokens(source) {
  source = source.replace(/\\\r?\n/g, '').replace(/^[ \t]*#\s*define\s+\w+(?:\([^\n]*?\))?\s+([^\n]*)/gm, '$1');
  return [...source.matchAll(/(?:u8|u|U|L)?R"([^ ()\\\t\r\n]{0,16})\([\s\S]*?\)\1"|\/\/[^\n]*|\/\*[\s\S]*?\*\/|^[ \t]*#[^\n]*|"(?:\\[\s\S]|[^"\\])*"|'(?:\\.|[^'\\])*'|[0-9][A-Za-z_0-9.']*|[A-Za-z_][A-Za-z_0-9]*|[^\s]/gm)]
    .filter(m => !/^(?:\/\/|\/\*|[ \t]*#)/.test(m[0])).map(m => m[0]);
}
export function violations(source) {
  const t = tokens(source), hits = [], scopes = [];
  // These are external function spellings used only by checker fault interposition.
  const externalMacros = new Set(['fstat','fseeko','ftello','_qqbar_roots_poly_squarefree',
    'acb_is_finite','acb_poly_find_roots']);
  for (const m of source.matchAll(/^\s*#\s*define\s+([A-Za-z_]\w*)/gm))
    if (!/^SIC_[A-Z][A-Z0-9_]*$/.test(m[1]) && !externalMacros.has(m[1])) hits.push(`macro spelling: ${m[1]}`);
  const named = new Map();
  let segment = [], parentheses = 0;
  const add = (kind, name) => hits.push(`${kind}: ${name}`);
  function declaration(s) {
    if (parentheses) return;
    if (s.includes('(')) return;
    if (s.includes('=') && !s.includes(',')) return;
    if (!s.includes('constexpr') && (!s.includes('const') || s.lastIndexOf('*') > s.lastIndexOf('const'))) return;
    if (scopes.some(s => s !== 'namespace')) return;
    const candidate = [...s].reverse().find(x => /^[A-Za-z_]\w*$/.test(x));
    if (candidate && !constant.test(candidate)) add('namespace constant', candidate);
  }
  for (let i = 0; i < t.length; ++i) {
    const v = t[i];
    if (v === '(') ++parentheses;
    if (v === ')') --parentheses;
    if (v === 'typedef') {
      const end = t.indexOf(';', i);
      const name = t[end - 1];
      if (end > i && !pascal.test(name)) add('type alias', name);
    }
    if (v === 'using'  && /^[A-Za-z_]\w*$/.test(t[i + 1] || '') && t[i + 2] === '=' && !pascal.test(t[i + 1]))
      add('type alias', t[i + 1]);
    // Resolve direct literals, adjacent literal concatenation and named literal constants.
    if (v === '=' && /^"/.test(t[i + 1] || '')) named.set(t[i - 1], t[i + 1]);
    const optionReader = ['getenv','secure_getenv','kernel_setting','env_u64','raw','one',
      'binary','unsigned_or','present','nonzero'].includes(v) || /^optional_\w+$/.test(v);
    if ((optionReader && t[i + 1] === '(') || (v === 'kernel_options_' && t[i + 1] === '[')) {
      let value = named.get(t[i + 2]) || t[i + 2];
      if (/^"[A-Za-z0-9_]*"$/.test(value || '')) {
        value = JSON.parse(value);
        let j = i + 3;
        while (/^"/.test(t[j] || '')) value += JSON.parse(t[j++]);
        if (!/^SIC_[A-Z][A-Z0-9_]*$/.test(value) && !externalEnvironment.has(value)) add('environment name', value);
      }
    }
    if (v === '=') declaration(segment);
    if (v === '{') {
      const ns = segment.includes('namespace') || (segment.includes('extern') && !segment.includes(')'));
      if (!segment.includes(')')) declaration(segment);
      scopes.push(ns ? 'namespace' : 'body'); segment = [];
    } else if (v === '}') { scopes.pop(); segment = []; }
    else if (v === ';') { declaration(segment); segment = []; }
    else segment.push(v);
  }
  return [...new Set(hits)];
}
export function cmakeViolations(source) {
  const hits = [];
  for (const m of source.matchAll(/\b(add_executable|sic_add_executable|sic_add_sicx_app|add_library)\(\s*([A-Za-z_][\w-]*)(?=[\s)])/g)) {
    const library = m[1] === 'add_library';
    if (!(library ? /^[a-z][a-z0-9_]*$/ : /^[a-z][a-z0-9]*(?:-[a-z0-9]+)*$/).test(m[2]))
      hits.push(`target spelling: ${m[2]}`);
  }
  return hits;
}
export function configuredTargets(text) {
  for (const row of text.trim().split('\n').filter(Boolean)) {
    const [kind,name] = row.split('|');
    if (kind === 'EXECUTABLE') assert(/^[a-z][a-z0-9]*(?:-[a-z0-9]+)*$/.test(name), `target spelling: ${name}`);
    else if (kind.endsWith('_LIBRARY')) assert(/^[a-z][a-z0-9_]*$/.test(name), `target spelling: ${name}`);
  }
}
export function check(root) {
  let count = 0; const errors = [];
  function visit(dir) {
    if (!fs.existsSync(dir)) return;
    for (const e of fs.readdirSync(dir, {withFileTypes:true})) {
      if (e.name.startsWith('build') || e.name.startsWith('.')) continue;
      const file = path.join(dir,e.name);
      if (e.isDirectory()) visit(file);
      else if (/\.(cpp|hpp|tpp|cmake)$/.test(e.name) || e.name === 'CMakeLists.txt') {
        ++count; const s = fs.readFileSync(file,'utf8');
        for (const error of (/\.cmake$/.test(file) || e.name === 'CMakeLists.txt' ? cmakeViolations(s) : violations(s)))
          errors.push(`${path.relative(root,file)}: ${error}`);
      }
    }
  }
  visit(path.join(root,'cpp'));
  if (errors.length) throw Error(errors.join('\n'));
  return count;
}
function controls() {
  for (const s of ['namespace x { constexpr int bad_name = 1; }', 'const int BAD = 1;',
    'namespace a::b { inline constexpr auto bad = std::array<int, 2>{1, 2}; }',
    'namespace x { void f() {} constexpr int bad = 1; }',
    'namespace x { struct C {}; const int bad = 1; }',
    'auto x = std::getenv("SICX_BAD");', 'auto x = getenv("BAD");',
    ...['secure_getenv','raw','one','binary','unsigned_or','present','nonzero',
      'optional_unsigned','optional_threads'].map(f=>`auto x = ${f}("SICX_BAD");`),
    'kernel_options_["ENTRY_ARB"] = "1";',
    'auto x = getenv("SIC_" "bad");',
    'constexpr auto kKey = "OLD_SETTING"; auto x = getenv(kKey);',
    'using lower_alias = int;', 'typedef long lower_alias;', '#define BAD_MACRO 1',
    'constexpr int kGood = 1, bad_name = 2;', 'static const char* const bad_name = nullptr;',
    '#define SIC_READ getenv("BAD")']) assert(violations(s).length, `naming control accepted: ${s}`);
  for (const s of ['namespace x { constexpr int kValue = 1; }',
    'const int& value(); const char* pointer = nullptr;',
    'void f() { const int local = 1; }',
    '// constexpr int ignored = 1;\nconst char* text = "using lower = int;";',
    'auto x = getenv("SLURM_CPUS_PER_TASK");', 'auto x = getenv("SIC_THREADS");',
    'using GoodAlias = int;']) assert.deepEqual(violations(s), [], `naming control rejected: ${s}`);
  assert(cmakeViolations('add_executable(bad_name main.cpp)').length);
  assert(cmakeViolations('add_library(bad-name STATIC main.cpp)').length);
  assert.deepEqual(cmakeViolations('add_executable(good-name main.cpp)\nadd_library(good_name STATIC main.cpp)'), []);
  assert.throws(()=>configuredTargets('EXECUTABLE|bad_name'),/target spelling:/);
  assert.throws(()=>configuredTargets('STATIC_LIBRARY|bad-name'),/target spelling:/);
  configuredTargets('EXECUTABLE|good-name\nSTATIC_LIBRARY|good_name');
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try { controls(); if (process.argv[3]) configuredTargets(fs.readFileSync(process.argv[3],'utf8')); console.log(`Naming policy passed: ${check(process.argv[2])} files`);
    console.log('SIC_NEGATIVE_CONTROL:naming');
  } catch (e) { console.error(e.message); process.exitCode = 1; }
}
