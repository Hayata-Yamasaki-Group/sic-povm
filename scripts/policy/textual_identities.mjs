// SPDX-License-Identifier: AGPL-3.0-or-later
// Declaration/field roles determine scope; identity values are never allowlisted.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {readPublicExclusions} from './public_tree/exclusions.mjs';

const identity = /^sicx?-[a-z0-9]+(-[a-z0-9]+)*\/1\.0$/;
export function requireTextualIdentity(value, location) {
  assert(typeof value === 'string' && identity.test(value), `format identity: ${location}: ${JSON.stringify(value)}`);
}
export function structuredIdentities(value, location, parent = '', ancestry = []) {
  if (Array.isArray(value)) return value.forEach((v, i) => structuredIdentities(v, `${location}[${i}]`, parent, ancestry));
  if (!value || typeof value !== 'object') return;
  for (const [key, child] of Object.entries(value)) {
    const schemaRole = ['schema', 'decoder', 'alignment'].includes(parent) ||
      (parent === 'rule' && ancestry.includes('generator_rescaling')) ||
      (parent === 'name' && ['entry_rule', 'decoder', 'generator_selection'].some(role => ancestry.includes(role))) ||
      (parent === 'base' && ancestry.includes('generator_selection'));
    if (key === 'enum' && schemaRole) {
      assert(Array.isArray(child) && child.length > 0, `format identity: empty enum: ${location}`);
      child.forEach(v => requireTextualIdentity(v, location + '.enum'));
    }
    if ((key === 'schema' && parent !== 'properties') || key === '$id' || (key === 'alignment' && parent !== 'properties') ||
        (key === 'decoder' && typeof child === 'string') ||
        (key === 'name' && ['decoder', 'entry_rule'].includes(parent)) ||
        (key === 'const' && schemaRole) ||
        (key === 'rule' && parent === 'generator_rescaling') ||
        (['name', 'base'].includes(key) && parent === 'generator_selection')) requireTextualIdentity(child, `${location}.${key}`);
    structuredIdentities(child, `${location}.${key}`, key, [...ancestry, key]);
  }
}
// Lex literals and actual comments separately. Only C++ joins adjacent strings;
// CMake also admits bracket/unquoted arguments, and JavaScript admits templates.
function literals(source, location) {
  const cpp = /\.(cpp|hpp|h|c|tpp)$/.test(location);
  const cmake = location.endsWith('.cmake');
  const token = cmake ? /#[^\n]*|"(?:\\.|[^"\\])*"|\[(=*)\[[\s\S]*?\]\1\]|[^\s()#"]+/g :
    /\/\*[\s\S]*?\*\/|\/\/[^\n]*|'(?:\\.|[^'\\])*'|"(?:\\.|[^"\\])*"|`(?:\\.|[^`\\])*`/g;
  const found = [], comments = [];
  for (const match of source.matchAll(token)) {
    const raw = match[0];
    if ((cmake && raw.startsWith('#')) || raw.startsWith('//') || raw.startsWith('/*')) {
      comments.push({start: match.index, text: raw});
      continue;
    }
    let value;
    if (cmake && raw.startsWith('[')) value = raw.replace(/^\[=*\[|\]=*\]$/g, '');
    else if (cmake && !raw.startsWith('"')) value = raw;
    else {
      try { value = JSON.parse(raw); }
      catch { value = raw.slice(1, -1).replace(/\\([\\'"`])/g, '$1'); }
    }
    const previous = found.at(-1);
    if (cpp && previous && /^\s*$/.test(source.slice(previous.end, match.index))) {
      previous.value += value; previous.end = match.index + raw.length;
    } else found.push({value, start: match.index, end: match.index + raw.length});
  }
  for (const literal of found) {
    const newline = source.indexOf('\n', literal.end);
    literal.rejectedInput = comments.some(c => c.start >= literal.end &&
      c.start < (newline < 0 ? source.length : newline) &&
      /^(?:\/\/|#)\s*identity-negative\s*$/.test(c.text));
  }
  return found;
}
export function sourceIdentities(source, location) {
  if (location.endsWith('.gp')) source = source.replace(/\\\\[^\n]*/g, '');
  for (const literal of literals(source, location)) {
    // Rejected-input annotations describe adversarial literal roles, never
    // exempt a file. Only test harnesses may declare this role.
    if (literal.rejectedInput) {
      const testRole = /(?:^|\/)cpp\/(?:tests\/|checkers\/.*\/tests\/)/.test(location) ||
        (/(?:^|\/)cpp\/checkers\//.test(location) && location.endsWith('.mjs') && source.includes('node:assert'));
      assert(testRole, 'format identity: rejected-input outside tests');
      continue;
    }
    assert(!/\bSIC_[A-Z0-9_]+\s+[0-9]+\b/.test(literal.value), `format identity: unsupported text header: ${location}`);
    const before = source.slice(Math.max(0, literal.start - 250), literal.start);
    // Named textual identity declarations are checked even for arbitrary bad
    // values with no slash, prefix or version. Fixed-width arrays/memcmp tags
    // have binary roles and are not textual identity declarations.
    const binarySignature = /\b(?:char|uint8_t)\s+k\w*(?:Magic|Tag)\s*\[\s*(?:4|8)\s*\]\s*=\s*$/.test(before) ||
      /std::array\s*<\s*(?:char|uint8_t)\s*,\s*(?:4|8)\s*>\s+k\w*(?:Magic|Tag)\s*=\s*$/.test(before);
    if (binarySignature) continue;
    const declaration = /\bk[A-Za-z0-9]*(?:Identity|Schema|Version|Rule|Alignment|Client|Checkpoint|Combination|Evaluator)(?:\s*\[[^\]]*\])?\s*=\s*$/.test(before);
    const gpField = location.endsWith('.gp') && /sj_get\([^\n]*,\s*"schema"\)\s*!=\s*$/.test(before);
    const selectorIdentity = /\bkStepSelection\s*=\s*$/.test(before);
    const profileIdentity = /\bstruct\s+CanonicalProfile\s*\{[^}]*\bkName\s*=\s*$/.test(before);
    const jsonField = /\["(?:schema|alignment)"\]\s*=\s*(?:[\w:]+\()?\s*$/.test(before);
    if (declaration || selectorIdentity || profileIdentity || jsonField || gpField) requireTextualIdentity(literal.value, location + ':declaration');
    // All versioned literals in production/checker/GP sources are contracts,
    // including embedded JSON and diagnostic descriptions of those contracts.
    for (const field of literal.value.matchAll(/"(?:schema|alignment)"\s*:\s*"([^"]*)"/g))
      requireTextualIdentity(field[1], location + ':embedded-field');
    for (const candidate of literal.value.matchAll(/(?<![A-Za-z0-9_-])[a-z][a-z0-9_-]*\/[0-9]+(?:\.[0-9]+)*(?![A-Za-z0-9_.-])/g))
      requireTextualIdentity(candidate[0], location + ':literal');
  }
}
export function sicxIdentities(bytes, location) {
  // SICX header/chunk framing is fixed width; this is a policy walk, not a
  // certificate reader. Production/checker tests separately authenticate it.
  assert(bytes.length >= 128, `format identity: short container: ${location}`);
  let at = 16;
  while (at < bytes.length - 112) {
    const tag = bytes.toString('ascii', at, at + 4);
    const length = Number(bytes.readBigUInt64LE(at + 8));
    assert(Number.isSafeInteger(length) && length <= bytes.length - at - 16, `format identity: chunk length: ${location}`);
    if (['EXMT', 'RAYS', 'PROV', 'PREC'].includes(tag))
      structuredIdentities(JSON.parse(bytes.toString('utf8', at + 16, at + 16 + length)), location + ':' + tag);
    at += 16 + length;
  }
  assert.equal(at, bytes.length - 112, `format identity: container framing: ${location}`);
}
function files(dir) {
  if (!fs.existsSync(dir)) return [];
  return fs.readdirSync(dir, {withFileTypes: true}).flatMap(e => {
    const file = path.join(dir, e.name);
    assert(!e.isSymbolicLink(), `format identity: symlink: ${file}`);
    return e.isDirectory() ? files(file) : [file];
  });
}
export function textHeaderIdentity(text, location) {
  const header = text.split('\n', 1)[0].replace(/^# /, '');
  requireTextualIdentity(header, location + ':header');
}
export function checkTextualIdentities(root) {
  let count = 0;
  const sources = ['cpp/include', 'cpp/src', 'cpp/apps', 'cpp/checkers', 'cpp/tests', 'cpp/cmake', 'private/scripts/release'].flatMap(dir => files(path.join(root, dir)));
  for (const file of sources) {
    if (!/\.(hpp|cpp|tpp|h|c|gp|cmake|mjs|sh)$/.test(file)) continue;
    sourceIdentities(fs.readFileSync(file, 'utf8'), file); ++count;
  }
  for (const dir of ['data', 'cpp/tests/fixtures', 'cpp/tests/data', 'docs/schemas']) for (const file of files(path.join(root, dir))) {
    if (/\.(balls|sicrad)$/.test(file) || ['stabilizers.txt', 'COMPLETED'].includes(path.basename(file)) ||
        (/^cpp\/tests\/data\/clifford\/.*\.txt$/.test(path.relative(root, file).split(path.sep).join('/'))) ||
        (file.endsWith('.txt') && /^# (?:sicx?-|SIC_)/.test(fs.readFileSync(file, 'utf8'))))
      textHeaderIdentity(fs.readFileSync(file, 'utf8'), file);
    if (file.endsWith('.sicx')) sicxIdentities(fs.readFileSync(file), file);
    if (/\.jsonl?$/.test(file)) {
      const text = fs.readFileSync(file, 'utf8');
      const values = file.endsWith('.jsonl') ? text.trimEnd().split('\n').map(s => JSON.parse(s)) : [JSON.parse(text)];
      for (const v of values) structuredIdentities(v, file);
    }
  }
  const excluded = fs.existsSync(path.join(root, 'scripts/policy/public_exclude.txt')) ?
    readPublicExclusions(root).entries : new Set();
  const rootDocs = fs.readdirSync(root).filter(f => f.endsWith('.md') && !excluded.has(f)).map(f => path.join(root, f));
  for (const file of [...files(path.join(root, 'docs')).filter(f => f.endsWith('.md')), ...rootDocs]) {
    const text = fs.readFileSync(file, 'utf8');
    assert(!/\bSIC_[A-Z0-9_]+ [0-9]+\b/.test(text), `format identity: unsupported prose header: ${file}`);
    // Concrete versioned identities in prose and examples. Placeholders and
    // paths have different grammar; the normative registry also checks kind.
    for (const m of text.matchAll(/(?<![A-Za-z0-9_/-])(?:sicx?-[a-z0-9_-]+|[a-z]+(?:-[a-z]+)+)\/[0-9]+(?:\.[0-9]+)*(?![A-Za-z0-9_.-])/g))
      requireTextualIdentity(m[0], file + ':prose');
    if (path.basename(file) === 'sicx.md') for (const row of text.split('\n').filter(s => s.startsWith('| `') && s.includes('| textual identity ('))) {
      const first = row.split('|')[1];
      for (const m of first.matchAll(/`([^`]+)`/g)) requireTextualIdentity(m[1], file + ':registry');
    }
  }
  return count;
}

// Negative controls exercise declaration roles, unfamiliar names, split strings,
// binary kinds, nested fields, GP, and JSON records inside actual byte framing.
export function textualIdentityControls() {
  for (const bad of ['unknown', 'SIC_' + 'CONTROL 1', 'sic-header/1', 'sic-header/2.0']) {
    assert.throws(() => textHeaderIdentity(bad + '\nd 2\n', 'control.balls'), /format identity:/);
    assert.throws(() => textHeaderIdentity('# ' + bad + '\n', 'stabilizers.txt'), /format identity:/);
  }
  textHeaderIdentity('sic-unfamiliar-header/1.0\nd 2\n', 'control.balls');
  assert.throws(() => sourceIdentities('auto header = "SIC_' + 'CONTROL" " 1";', 'control.cpp'), /format identity:/);
  for (const bad of ['alien/1', 'sic-control/1', 'SIC_CONTROL 1', 'sic-control/1.0.0', 'sic_control/1.0', 'unknown']) {
    assert.throws(() => sourceIdentities(`constexpr const char* kFreshIdentity = "${bad}";`, 'control.cpp'), /format identity:/);
    assert.throws(() => structuredIdentities({decoder: {name: bad}}, 'control'), /format identity:/);
    assert.throws(() => structuredIdentities({generator_rescaling: {rule: bad}}, 'control'), /format identity:/);
    assert.throws(() => structuredIdentities({entry_rule: {name: bad}}, 'control'), /format identity:/);
    assert.throws(() => structuredIdentities({alignment: bad}, 'control'), /format identity:/);
  }
  assert.throws(() => structuredIdentities({properties: {decoder: {properties: {name: {const: 'unknown'}}}}}, 'control.schema'), /format identity:/);
  assert.throws(() => structuredIdentities({properties: {entry_rule: {properties: {name: {enum: ['unknown']}}}}}, 'control.schema'), /format identity:/);
  assert.throws(() => structuredIdentities({properties: {entry_rule: {properties: {name: {const: 'unknown'}}}}}, 'control.schema'), /format identity:/);
  assert.throws(() => sourceIdentities('if(#strsplit(txt, ' + JSON.stringify('"schema":"unknown"') + ') != 2, fail("schema"));', 'control.gp'), /format identity:/);
  assert.throws(() => structuredIdentities({properties: {alignment: {const: 'unknown'}}}, 'control.schema'), /format identity:/);
  assert.throws(() => structuredIdentities({properties: {decoder: {const: 'unknown'}}}, 'control.schema'), /format identity:/);
  assert.throws(() => sourceIdentities('constexpr char kFreshIdentity[] = "unknown";', 'control.cpp'), /format identity:/);
  assert.throws(() => sourceIdentities('if(sj_get(J, "schema") != "unknown", error("schema"));', 'control.gp'), /format identity:/);
  assert.throws(() => structuredIdentities({generator_selection: {name: 'unknown', base: 'sic-control/1.0'}}, 'control'), /format identity:/);
  assert.throws(() => structuredIdentities({generator_selection: {name: 'sic-control/1.0', base: 'unknown'}}, 'control'), /format identity:/);
  assert.throws(() => sourceIdentities('auto value = "sic-control/" "1";', 'control.cpp'), /format identity:/);
  assert.throws(() => sourceIdentities('if (schema != "alien-control/1") error("schema");', 'control.gp'), /format identity:/);
  sourceIdentities('constexpr const char* kFreshIdentity = "sic-unfamiliar-contract/1.0";', 'control.cpp');
  for (const file of ['control.cmake', 'control.mjs']) {
    assert.throws(() => sourceIdentities('set(rule "sicx-2adic-greedy/1")', file), /format identity:/);
    assert.throws(() => sourceIdentities("const rule = 'sicx-2adic-greedy/1';", file), /format identity:/);
  }
  assert.throws(() => sourceIdentities('auto x = "unknown/1"; // identity-negative', 'production.cpp'), /outside tests/);
  sourceIdentities('auto x = "unknown/1"; // identity-negative', 'cpp/tests/control.cpp');
  assert.throws(() => sourceIdentities('struct CanonicalProfile { static constexpr auto kName = "unknown";', 'control.hpp'), /format identity:/);
  assert.throws(() => sourceIdentities('const label = "identity-negative", rule = "sicx-2adic-greedy/1";', 'cpp/tests/control.mjs'), /format identity:/);
  assert.throws(() => sourceIdentities('const rule = `sicx-2adic-greedy/1`;', 'control.mjs'), /format identity:/);
  assert.throws(() => sourceIdentities('set(rule sicx-2adic-greedy/1)', 'control.cmake'), /format identity:/);
  assert.throws(() => sourceIdentities('set(rule [=[sicx-2adic-greedy/1]=])', 'control.cmake'), /format identity:/);
  sourceIdentities('constexpr const char* kDefaultSelection = "all";', 'control.cpp');
  assert.throws(() => sourceIdentities('inline const std::string kStepSelection = "unknown";', 'control.hpp'), /format identity:/);
  sourceIdentities('constexpr char kMagic[8] = "other/1";', 'control.cpp');
  sourceIdentities('constexpr char kMagic[8] = "MAGIC001"; std::memcmp(p, "TAG1", 4);', 'control.cpp');
  const payload = Buffer.from(JSON.stringify({entry_rule: {name: 'alien-control/1'}}));
  const bytes = Buffer.alloc(16 + 16 + payload.length + 112);
  bytes.write('EXMT', 16); bytes.writeBigUInt64LE(BigInt(payload.length), 24); payload.copy(bytes, 32);
  assert.throws(() => sicxIdentities(bytes, 'control.sicx'), /format identity:/);
  structuredIdentities({properties: {name: {const: 'ordinary label'}, rule: {const: 'ordinary rule'}}}, 'ordinary.schema');
  structuredIdentities({name: 'ordinary label', rule: 'ordinary rule', decoder: {name: 'sic-unfamiliar-contract/1.0'}}, 'control');
}
