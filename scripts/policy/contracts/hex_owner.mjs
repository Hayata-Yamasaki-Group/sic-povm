// SPDX-License-Identifier: AGPL-3.0-or-later
// Lexical encoder forms; arithmetic encoders and indirect forwarding require review.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {production,tokens} from './source.mjs';
const root=path.resolve(process.argv[2]);
function check(file,source) {
  if(file==='cpp/src/support/text/hex.cpp'||file.startsWith('cpp/checkers/'))return;
  const text=tokens(source).join(' ');
  assert(!/(?:=|\{)\s*"0123456789abcdef"|"0123456789abcdef"\s*\[/i.test(text),`hex owner: duplicate lookup encoder in ${file}`);
  assert(!/"(?:[^"\\]|\\.)*%0?2[xX]/.test(text),`hex owner: duplicate formatted byte encoder in ${file}`);
  assert(!/\b(?:hex|std : : hex)\b[\s\S]{0,160}\bsetw\s*\(\s*2\s*\)|\bsetw\s*\(\s*2\s*\)[\s\S]{0,160}\bhex\b/.test(text),`hex owner: duplicate stream byte encoder in ${file}`);
}
for(const file of production(root))check(file,fs.readFileSync(path.join(root,file),'utf8'));
const duplicate=fs.readFileSync(path.join(root,'cpp/src/support/text/hex.cpp'),'utf8');
assert.throws(()=>check('cpp/src/data/duplicate.cpp',duplicate),/hex owner:/);
for(const source of ['sprintf(out, "%02x", byte);','out << std::hex << std::setw(2) << byte;'])
  assert.throws(()=>check('cpp/src/data/duplicate.cpp',source),/hex owner:/);
check('cpp/checkers/verifier/hex.cpp',duplicate);
check('cpp/src/data/validator.cpp','return s.find_first_not_of("0123456789abcdef") == s.npos;');
check('cpp/src/data/comment.cpp','// constexpr char digits[] = "0123456789abcdef";');
console.log('PASS production hex encoder ownership');
console.log('SIC_NEGATIVE_CONTROL:hex-owner');
