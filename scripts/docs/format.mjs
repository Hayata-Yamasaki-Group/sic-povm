#!/usr/bin/env node
// Include order is deliberately preserved: the formatting pass changes whitespace only.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
const [formatter, root] = process.argv.slice(2);
const walk = d => fs.readdirSync(d, {withFileTypes: true}).flatMap(e => {
  const p = path.join(d, e.name);
  return e.isDirectory() ? (/^build(?:[-_].*)?$/.test(e.name) ? [] : walk(p)) :
    e.isFile() && /\.(?:cc|cpp|cxx|c\+\+|h|hh|hpp|hxx|ipp|inl|tpp)(?:\.in)?$/.test(e.name) ? [p] : [];
});
// Prove that the real formatter rejects a syntactically valid spacing drift.
const control=spawnSync(formatter,['--style=file:'+path.join(root,'.clang-format'),
  '--sort-includes=false','--dry-run','--Werror','--assume-filename=control.cpp'],
  {input:'int main(){return 0;}\n',encoding:'utf8',timeout:10000});
assert.ifError(control.error);
assert.notEqual(control.status,0,'format: unformatted negative control accepted');
assert.match(control.stderr,/code should be clang-formatted/);
const files = walk(path.join(root, 'cpp'));
const r = spawnSync(formatter, [`--style=file:${path.join(root, '.clang-format')}`,
  '--sort-includes=false', '--dry-run', '--Werror', ...files],
  {encoding: 'utf8', timeout: 110000, maxBuffer: 8 * 1024 * 1024});
if (r.error || r.status !== 0) console.error(r.error || r.stderr);
console.log(`C++ format: ${files.length} files, status ${r.status}`);
process.exitCode = r.error || r.status !== 0 ? 1 : 0;

if(!process.exitCode)console.log('SIC_NEGATIVE_CONTROL:cpp-format');
