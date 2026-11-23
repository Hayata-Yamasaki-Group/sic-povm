// SPDX-License-Identifier: AGPL-3.0-or-later
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';

const exception = /^(?:d2\/d2\.sicrad|d8\/hoggar(?:\/(?:exact\.json|manifest\.json|c0\.(?:csv|h5|json|mat|npy)))?)$/;
const bundledPath = /(?:^|[\s/])data(?:\/([^\s"';|()]+))?(?=$|[\s/])/g;
const implicitDefault = /\b(?:kDefault(?:DataRoot|Catalogue|StabilizerTable)|SIC_DATA_ROOT_FROM_BINDIR)\b/;
const implicitFile = /(?:\.(?:get|value_or)\s*\([^;\n]*,\s*|\b(?:catalogue|stabilizer_path)\s*=\s*)["'](?:catalogue\.json|stabilizers\.txt)["']/;
const implicitLookup = /\bif\s*\([^;\n]*\.empty\(\)[^;\n]*\)\s*\{?\s*\w+\s*=[^;]*["'](?:catalogue\.json|stabilizers\.txt)["']/;
const reading = /\b(?:basic_)?(?:i|f)fstream\s*(?:[A-Za-z_]\w*\s*)?[({]\s*["'](?:\.\/)?(?:catalogue\.json|stabilizers\.txt)["']/;
const fallback = /(?:\.value_or\s*\([^;]*?|(?:\?|(?<!:):(?!:))[^;\n]*?)["'](?:\.\/)?(?:catalogue\.json|stabilizers\.txt)["']/;
const cmakeSourceData = /\$\{(?:CMAKE_(?:CURRENT_)?SOURCE_DIR|CMAKE_CURRENT_LIST_DIR|PROJECT_SOURCE_DIR|SIC_REPO_ROOT)\}\/(?:\.\.\/)*data(?=[/\s"')]|$)/g;
const shellInput = /(?:^\s*|[;&|]\s*|\$\(\s*|\b(?:if|then|do)\s+)(?:cat|head|tail|jq|awk|sed|grep|cut|sha256sum)\s+[^\n;|]*?(?<!\S)["']?(?:\.\/)?(?:catalogue\.json|stabilizers\.txt)["']?(?=\s|[)>&;|]|$)|(?<![<])<\s*(?![<&])["']?(?:\.\/)?(?:catalogue\.json|stabilizers\.txt)["']?(?=\s|[);]|$)/m;
const shellSourceData = /(?:^|[\s="'<])((?:(?:\.\/)?data|\$(?:\{(?:REPO|SIC_REPO_ROOT|SOURCE_DIR)\}|REPO|SIC_REPO_ROOT|SOURCE_DIR)\/data)\/[^\s"'<>;|()]+)/gm;

function rejectBundle(file, source) {
  for (const match of source.matchAll(bundledPath)) {
    const prefix = source.slice(0, match.index).replace(/\/$/, '');
    const member = match[1]?.replace(/\/$/, '') ?? '';
    // Build output roots are independent of the source tree.
    if (/(?:^|\/)(?:build|\x24\{CMAKE_BINARY_DIR\})$/.test(prefix)) continue;
    assert(exception.test(member), `explicit data inputs: ${file}: ${match[0].trim()}`);
  }
}

function checkSource(file, text) {
  const code = text.split('\n').filter(line => !/^\s*(?:#\s*include\b|\/\/|\\\\|#(?!\s*define\b))/.test(line)).join('\n');
  assert(!implicitDefault.test(code) && !implicitFile.test(code) && !implicitLookup.test(code) &&
    !reading.test(code) && !fallback.test(code),
    `explicit data inputs: ${file}: implicit default`);
  for (const literal of code.matchAll(/"(?:\\.|[^"\n\\])*"|'[^'\n]*'/g)) {
    const value = literal[0].slice(1, -1);
    if (value === 'data' && file === 'cpp/src/exact/storage/table_checkpoint.cpp') continue;
    if (!/\s/.test(value)) rejectBundle(file, value);
  }
}

function checkCMake(file, text) {
  for (const match of text.matchAll(cmakeSourceData)) {
    const operand = text.slice(match.index).split(/[\s"')]/, 1)[0];
    rejectBundle(file, operand);
  }
}

function checkShell(file, text) {
  const commands = text.replace(/\\\n/g, ' ').replace(/^\s*#.*$/gm, '');
  assert(!shellInput.test(commands), `explicit data inputs: ${file}: implicit shell input`);
  for (const match of commands.matchAll(shellSourceData)) rejectBundle(file, match[1]);
}

function checkDocs(file, text) {
  function checkCommands(commands) {
    checkShell(file, commands);
    checkCMake(file, commands);
    assert(!/(?:--data-root|\bcd|-DSIC_DATA_ROOT)[=\s]+["']?(?:\.\/)?data(?=["'\s]|$)/m.test(commands),
      `explicit data inputs: ${file}: implicit source data root`);
    for (const token of commands.match(/[^\s"'`=<>;|()]+/g) ?? [])
      if (token.includes('/')) rejectBundle(file, token);
  }
  for (const block of text.matchAll(/^\s*```([^\n]*)\n([\s\S]*?)^\s*```\s*$/gm)) {
    if (/^(?:text|json|cpp)?$/.test(block[1].trim()) && !/^\s*(?:sic-[\w-]+ |cmake |(?:cd |.*(?:open\(|load\(|read\()).*\bdata\/)/m.test(block[2])) continue;
    // Token boundaries include quotes and shell assignments. The path suffix is checked
    // before normalizing relative and interpolated prefixes.
    checkCommands(block[2]);
  }
  for (const indented of text.matchAll(/^ {4}(?:sic-[\w-]+ |cmake |cd ).+$/gm)) checkCommands(indented[0]);
}

function walk(dir, allowMissing = false) {
  let entries;
  try {
    entries = fs.readdirSync(dir, {withFileTypes: true});
  } catch (error) {
    if (allowMissing && error.code === 'ENOENT') return [];
    throw error;
  }
  return entries.flatMap(e => {
    const name = path.join(dir, e.name);
    if (e.isDirectory()) return e.name === 'tests' || e.name === 'self_test' ? [] : walk(name);
    return e.isFile() ? [name] : [];
  });
}
const root = path.resolve(process.argv[2]);
process.chdir(root);
for (const dir of ['cpp/apps', 'cpp/src', 'cpp/include', 'cpp/checkers', 'cpp/cmake/components', 'scripts/data']) {
  for (const file of walk(dir).filter(p => /\.(?:cpp|hpp|h|cc|gp|mjs|cmake|sh)$|\/CMakeLists\.txt$/.test(p))) {
    if (file.endsWith('/json_inputs.mjs')) continue; // Checker-owned synthetic test.
    // CMake source paths refer to source code, independent of whether a component is named "data".
    const source = fs.readFileSync(file, 'utf8');
    checkSource(file, source.replace(/(?:cpp\/|\x24\{PROJECT_SOURCE_DIR\}\/)?(?:src|apps|tests|include(?:\/sic)?)\/data\//g, 'components/').replace('scripts/data/fetch_exact.cmake', 'scripts/fetch_exact.cmake'));
    if (/\.(?:cmake)$|(?:^|\/)CMakeLists\.txt$/.test(file)) checkCMake(file, source);
    if (file.endsWith('.sh')) checkShell(file, source);
  }
}
checkCMake('cpp/CMakeLists.txt', fs.readFileSync('cpp/CMakeLists.txt', 'utf8'));
const exported = fs.statSync('cpp/SOURCE_COMMIT', {throwIfNoEntry: false})?.isFile();
for (const file of walk('private/scripts/release', exported).filter(file => file.endsWith('.sh')))
  checkShell(file, fs.readFileSync(file, 'utf8'));
for (const file of ['README.md', 'CONTRIBUTING.md', ...walk('docs').filter(p => p.endsWith('.md'))])
  checkDocs(file, fs.readFileSync(file, 'utf8'));

for (const [check, file, content] of [
  [checkSource, 'cpp/apps/example.cpp', 'return read("data/catalogue.json");'],
  [checkSource, 'cpp/checkers/example.cpp', 'std::ifstream in(root / "data" / "d8/stabilizers.txt");'],
  [checkSource, 'cpp/checkers/example.cpp', 'std::ifstream in("catalogue.json");'],
  [checkSource, 'cpp/checkers/example.cpp', 'return std::ifstream("./stabilizers.txt");'],
  [checkSource, 'cpp/src/example.cpp', 'return arg.value_or("catalogue.json");'],
  [checkSource, 'cpp/src/example.cpp', 'return arg.value_or(\n  "catalogue.json");'],
  [checkSource, 'cpp/src/example.cpp', 'return arg ? *arg : std::filesystem::path("catalogue.json");'],
  [checkSource, 'cpp/src/example.cpp', 'auto table = a.get("--table", "stabilizers.txt");'],
  [checkSource, 'cpp/apps/example.cpp', 'if (catalogue.empty()) catalogue = root / "catalogue.json";'],
  [checkSource, 'cpp/cmake/components/Example.cmake', 'set(SIC_DATA_ROOT "${PROJECT_SOURCE_DIR}/../data")'],
  [checkCMake, 'cpp/cmake/components/Example.cmake', 'set(SIC_DATA_ROOT ${PROJECT_SOURCE_DIR}/../data)'],
  [checkShell, 'scripts/data/default.sh', 'cat data/catalogue.json'],
  [checkShell, 'scripts/data/default.sh', '  cat catalogue.json'],
  [checkShell, 'scripts/data/default.sh', 'read -r contents < catalogue.json'],
  [checkShell, 'scripts/data/default.sh', 'table=$(cat "$REPO/data/d8/stabilizers.txt")'],
  [checkDocs, 'docs/example.md', '```sh\nsic-container fetch d8/m1 --data-root data\n```'],
  [checkDocs, 'docs/example.md', '```python\nread("./data/d4/m1/c0.csv")\n```'],
  [checkDocs, 'docs/example.md', '    cmake -S cpp -B build -DSIC_DATA_ROOT=data'],
]) assert.throws(() => check(file, content), /explicit data inputs:/);
checkSource('cpp/apps/example.cpp', 'read("data/d8/hoggar/exact.json"); read("data/d2/d2.sicrad");');
checkShell('scripts/data/example.sh', 'jq . "$release/catalogue.json"');
checkDocs('docs/example.md', '```sh\nsic-identify "$ray" --catalogue "$RELEASE_TREE/catalogue.json"\n```');
console.log('PASS explicit data inputs in production and documentation');
console.log('SIC_NEGATIVE_CONTROL:explicit-data-inputs');
