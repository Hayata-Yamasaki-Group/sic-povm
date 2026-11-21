// SPDX-License-Identifier: AGPL-3.0-or-later
import assert from 'node:assert/strict';
// Parse balanced option initializers, ignoring quoted braces and escapes.
export function optionDeclarations(text) {
  const found = new Map();
  for (const match of text.matchAll(/\{\s*"(--[a-z0-9-]+)"\s*,\s*"([^"\n]*)"/g)) {
    let depth=0, quoted=false, escape=false, end=match.index;
    for (;end<text.length;++end) {
      const c=text[end];
      if (escape) {escape=false;continue;}
      if (quoted && c==='\\') {escape=true;continue;}
      if (c==='"') quoted=!quoted;
      if (!quoted && c==='{') ++depth;
      if (!quoted && c==='}' && --depth===0) break;
    }
    const block=text.slice(match.index,end+1);
    if(match[2].startsWith('--'))continue;
    const tail=block.match(/,\s*(\d+)\s*,\s*(\d+)(?:\s*,\s*(true|false))?\s*\}$/);
    // A two-string option->kernel-setting mapping is not an Option record.
    if (!tail && [...block.matchAll(/"(?:\\.|[^"\\])*"/g)].length < 3) continue;
    found.set(match[1], {values:match[2],operands:tail ? [Number(tail[1]),Number(tail[2])] : [0,0],repeat:tail?.[3]==='true'});
  }
  return Object.fromEntries(found);
}
export function optionReads(text) {
  const reads=new Set();
  for (const m of text.matchAll(/\.(?:has|get|all|numeric(?:<[^;()]+>)?)\s*\(\s*"(--[a-z0-9-]+)"/g)) reads.add(m[1]);
  // Resolve literal loop lists and option->kernel mapping tables separately.
  for (const m of text.matchAll(/for\s*\([^;]*?([a-z_]+)\s*:\s*\{([^}]+)\}\)\s*[^;]*?\.(?:has|get|all)\(\1\)/g))
    for (const q of m[2].matchAll(/"(--[a-z0-9-]+)"/g)) reads.add(q[1]);
  if (/for \(const auto& \[flag, key\] : valued\)/.test(text) && /\.has\(flag\)/.test(text)) {
    const table=text.match(/valued\s*=\s*\{([\s\S]*?)\};/);
    assert(table,'options: missing valued mapping');
    for (const q of table[1].matchAll(/\{"(--[a-z0-9-]+)",/g)) reads.add(q[1]);
  }
  for (const m of text.matchAll(/auto ([a-z_]+)\s*=\s*\[&\]\(const char\* flag,[\s\S]*?\n  \};/g)) {
    assert(/\.has\(flag\)/.test(m[0]),'options: unrecognized accessor lambda');
    for (const q of text.matchAll(new RegExp('\\b'+m[1]+'\\("(--[a-z0-9-]+)"','g'))) reads.add(q[1]);
  }
  return reads;
}
export function compareOptions(declared,read,exceptions=[]) {
  const findings=[...Object.keys(declared).filter(n=>!read.has(n)).map(n=>`unread ${n}`),
    ...[...read].filter(n=>!Object.hasOwn(declared,n)).map(n=>`undeclared ${n}`)].sort();
  assert.deepEqual(findings,exceptions.slice().sort(),'options: declarations and command reads differ');
}
