// SPDX-License-Identifier: AGPL-3.0-or-later
// Offline lexical policy: installed headers cannot import whole namespaces.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

export function violations(text, macroBody = false) {
  // Apply C++ line splicing before lexing and retain physical diagnostic lines.
  let logical = '', physicalLine = 1;
  const lines = [];
  for (let i = 0; i < text.length; ++i) {
    if (text[i] === '\\' && (text[i + 1] === '\n' ||
        (text[i + 1] === '\r' && text[i + 2] === '\n'))) {
      i += text[i + 1] === '\r' ? 2 : 1;
      ++physicalLine;
      continue;
    }
    logical += text[i];
    lines.push(physicalLine);
    if (text[i] === '\n') ++physicalLine;
  }
  // Consume comments and literals as single tokens, retaining line positions.
  // Raw literals must be recognized before identifiers and ordinary strings.
  const tokens = logical.matchAll(/(?:u8|u|U|L)?R"([^ ()\\\t\r\n]{0,16})\([\s\S]*?\)\1"|\/\/[^\n]*|\/\*[\s\S]*?\*\/|(?:u8|u|U|L)?"(?:\\[\s\S]|[^"\\])*"|[0-9][A-Za-z_0-9.']*|(?:u8|u|U|L)?'(?:\\[\s\S]|[^'\\])*'|[A-Za-z_][A-Za-z_0-9]*|^[ \t]*#[^\n]*|[^\s]/gm);
  const scopes = [];
  const hits = [];
  let segment = [], previous = '';
  for (const token of tokens) {
    const spelling = token[0];
    const tokenLine = lines[token.index];
    if (/^[ \t]*#/.test(spelling)) {
      const definition = spelling.match(/^[ \t]*#[ \t]*define\b([\s\S]*)/);
      if (definition && violations(definition[1], true).length) hits.push(tokenLine);
      continue;
    }
    if (spelling.startsWith('//') || spelling.startsWith('/*')) continue;
    if (/^(?:(?:u8|u|U|L)?["']|(?:u8|u|U|L)?R")/.test(spelling)) continue;
    if (previous === 'using' && spelling === 'namespace' && (macroBody || !scopes.includes('body')))
      hits.push(tokenLine);
    if (spelling === '{') {
      // Namespace and language-linkage blocks do not create local scope.
      // All other braces are bodies (class-scope directives are illegal C++).
      const namespaceBlock = segment.includes('namespace') && !segment.includes('using');
      scopes.push(namespaceBlock || (segment.includes('extern') && !segment.includes(')'))
        ? 'namespace' : 'body');
      segment = [];
    } else if (spelling === '}') {
      scopes.pop();
      segment = [];
    } else if (spelling === ';') {
      segment = [];
    } else {
      segment.push(spelling);
    }
    previous = spelling;
  }
  return hits;
}

export function check(root) {
  const base = path.join(root, 'cpp/include');
  const failures = [];
  let count = 0;
  function visit(dir) {
    for (const entry of fs.readdirSync(dir, {withFileTypes: true}).sort((a, b) => a.name.localeCompare(b.name))) {
      const file = path.join(dir, entry.name);
      if (entry.isDirectory()) visit(file);
      else if (entry.isFile()) {
        ++count;
        for (const line of violations(fs.readFileSync(file, 'utf8')))
          failures.push(`${path.relative(root, file)}:${line}: using directive in namespace scope or macro definition`);
      }
    }
  }
  visit(base);
  if (failures.length) throw new Error(failures.join('\n'));
  return count;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    console.log(`Installed header namespace policy passed: ${check(process.argv[2])} files`);
  } catch (error) {
    console.error(error.message);
    process.exitCode = 1;
  }
}
