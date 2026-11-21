// SPDX-License-Identifier: AGPL-3.0-or-later
// Real symbol-scanner negative: two synthetic object owners reference each other.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {root,run} from './runtime_checks.mjs';
const work=path.resolve(process.argv[2]);fs.rmSync(work,{recursive:true,force:true});fs.mkdirSync(work,{recursive:true});
const objects=['a','b'].map(n=>path.join(work,`CMakeFiles/sic_core.dir/${n}.cpp.o`));
for(const p of objects){fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,'');}
fs.writeFileSync(path.join(work,'compile_commands.json'),JSON.stringify(objects.map((p,i)=>({command:`c++ -o ${p} -c source`,file:path.join(root,`cpp/src/core/${i?'b':'a'}.cpp`),directory:work}))));
fs.writeFileSync(path.join(work,'policy-symbol-targets.cmake'),'set(PRODUCTION sic_core)\nset(LINKS "")\n');
const nm=path.join(work,'symbols');
const output=[`${objects[0]}: _ZN3sic1aEv T 0 1`,`${objects[0]}: _ZN3sic1bEv U`,`${objects[1]}: _ZN3sic1bEv T 0 1`,`${objects[1]}: _ZN3sic1aEv U`].join('\n');
fs.writeFileSync(nm,`#!/bin/sh\ncat <<'SYMBOLS'\n${output}\nSYMBOLS\n`,{mode:0o755});
const {spawnSync}=await import('node:child_process');
const result=spawnSync('cmake',[`-DBUILD=${work}`,`-DROOT=${root}`,`-DNM=${nm}`,'-P',path.join(root,'scripts/policy/symbols.cmake')],{encoding:'utf8',timeout:10000});
assert.ifError(result.error);assert.equal(result.status,1);assert.match(result.stderr,/File symbol cycle/);
console.log('PASS symbol policy negative control: cross-object cycle rejected');

console.log('SIC_NEGATIVE_CONTROL:symbols-controls');
