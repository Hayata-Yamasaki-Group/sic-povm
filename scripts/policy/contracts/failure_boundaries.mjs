// SPDX-License-Identifier: AGPL-3.0-or-later
// Direct throw census in installed headers, production library components and apps.
// This is lexical: indirect callees, aliases and runtime control flow require review.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {production,tokens} from './source.mjs';
const root=path.resolve(process.argv[2]);
const allowances=JSON.parse(fs.readFileSync(new URL('./standard_failures.json',import.meta.url),'utf8'));
const used=new Set();
function check(file,source,exceptions=allowances) {
  const text=tokens(source).join(' ');
  for(const match of text.matchAll(/\bthrow\s+(?:std\s*:\s*:\s*)?(?:runtime_error|logic_error|invalid_argument)\s*[({][^;]+;/g)) {
    const key=file+'|'+match[0];
    assert(exceptions[key],`failure boundary: uncategorized direct throw in ${file}: ${match[0]}`);
    used.add(key);
  }
}
for(const file of production(root).filter(f=>f.startsWith('cpp/include/')||f.startsWith('cpp/src/')||f.startsWith('cpp/apps/')))
  check(file,fs.readFileSync(path.join(root,file),'utf8'));
for(const key of Object.keys(allowances))assert(used.has(key),`failure boundary: unused standard exception ${key}`);
for(const file of ['cpp/include/sic/bounds/control.hpp','cpp/src/bounds/control.cpp','cpp/src/construct/control.cpp','cpp/src/exact/control.cpp','cpp/apps/control.cpp'])
  for(const type of ['runtime_error','logic_error','invalid_argument'])
  for(const expression of [`throw std::${type}("bad input");`,`throw std::${type}{"bad input"};`,`throw ${type}("bad input");`])
    assert.throws(()=>check(file,expression,{}),/failure boundary:/);
check('cpp/include/sic/bounds/control.hpp','throw sic::Error(sic::ErrorKind::invalid_argument, "bad input");',{});
assert.throws(()=>check('cpp/src/core/numeric/verify.cpp','throw std::runtime_error("different cause");'),/failure boundary:/);
console.log('PASS direct library failure categories and reviewed internal invariants');
console.log('SIC_NEGATIVE_CONTROL:failure-boundaries');
