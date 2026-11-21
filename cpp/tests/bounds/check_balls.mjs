// SPDX-License-Identifier: AGPL-3.0-or-later
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {spawnSync} from 'node:child_process';
const [tool,input,work]=process.argv.slice(2);fs.rmSync(work,{recursive:true,force:true});fs.mkdirSync(work,{recursive:true});
function run(file,okay,args=[]) {const r=spawnSync(tool,[file,...args],{encoding:'utf8',timeout:30000});assert(!r.error,String(r.error));assert.equal(r.status===0,okay,`${r.status}: ${r.stdout}${r.stderr}`);return r;}
run(input,true);const text=fs.readFileSync(input,'utf8');
for(const [name,content] of Object.entries({trailing:text+'garbage\n',blank:text+'\n',missing_end:text.slice(0,text.lastIndexOf('end')),missing_row:text.split('\n').filter((_,i)=>i!==2).join('\n'),bad_dimension:text.replace('d 8','d 8junk'),bad_ball:text.replace(/\n[^\n]+ \| /,'\nbad | ')})) {const f=path.join(work,name+'.balls');fs.writeFileSync(f,content);run(f,false);}
run(path.join(work,'missing.balls'),false);
run(input,false,['--maximum-dimension','4']);run(input,false,['--maximum-work','1']);run(input,false,['--working-bits','0']);
console.log('PASS numerical ball bound, missing/truncated/trailing data and admission refusals');
