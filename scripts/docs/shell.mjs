// SPDX-License-Identifier: AGPL-3.0-or-later
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {root, read, files, run, refuses} from './runtime_checks.mjs';
const inventory=JSON.parse(read(path.join(root,'docs/cli_contract.json')));
// Shell words retain quoted strings as single operands; command boundaries are
// preserved so options from awk, cmake, loops and pipelines cannot leak across.
export function checkOptions(block, name) {
  const joined=block.replace(/\\\n/g,' ');
  const tokens=joined.match(/"(?:\\.|[^"\\])*"|'[^']*'|[^\s;|&<>]+|[;|&<>\n]/g)||[];
  for(let i=0;i<tokens.length;++i) {
    const commandWord=tokens[i].replace(/^["']|["']$/g,'');
    if(!/^(?:[^ ]*\/)?sic-[a-z-]+$/.test(commandWord))continue;
    const tool=path.basename(commandWord); let end=i+1;
    while(end<tokens.length&&!/^[;|&<>\n]$/.test(tokens[end]))++end;
    const args=tokens.slice(i+1,end).map(s=>s.replace(/^["']|["']$/g,''));
    const command=['sic-data','sic-container'].includes(tool)?args[0]:'';
    const row=inventory.find(r=>r.tool===tool&&r.command===(command||''));
    assert(row,`shell: unknown command ${tool} ${command} in ${name}`);
    for(const option of args.filter(s=>/^--[a-z]/.test(s)))
      assert(row.options[option.split('=')[0]],`shell: unknown option ${tool} ${option} in ${name}`);
    i=end-1;
  }
}
export function shellBlocks(doc, name) {
  const blocks=[];
  for(const match of doc.matchAll(/(?:<!-- shell:(run|template)(?: id=([a-z0-9-]+))? -->\n)?```(?:sh|bash)\n([\s\S]*?)```/g)) {
    assert(match[1],`shell: unclassified block in ${name}`);
    checkOptions(match[3],name);
    blocks.push({kind:match[1],id:match[2],body:match[3],name});
  }
  return blocks;
}
export function recipes(build,work) {
  const blocks=[...files(path.join(root,'docs')).filter(p=>p.endsWith('.md')),
    path.join(root,'README.md'),path.join(root,'CONTRIBUTING.md')].flatMap(p=>shellBlocks(read(p),p));
  const bindings=JSON.parse(read(path.join(root,'scripts/docs/contracts/shell_bindings.json')));
  const selected=blocks.filter(b=>b.kind==='run'||bindings[b.id]);
  assert(selected.length>0,'shell: no runnable examples');
  for(const id of Object.keys(bindings))assert.equal(blocks.filter(b=>b.id===id).length,1,`shell: missing/duplicate binding ${id}`);
  fs.rmSync(work,{recursive:true,force:true});fs.mkdirSync(work,{recursive:true});
  const env={...process.env,PATH:[build,...['verifier','norm','crt','stabilizer'].map(n=>path.join(build,'checkers',n)),process.env.PATH].join(path.delimiter),T:'1',M:'2048',SIC_THREADS:'1',SIC_MEMORY_MB:'2048'};
  // The recipes document their own defaults; an inherited selector would change what they exercise.
  delete env.READER_MODE;
  const results=[];
  function execute(block,binding,cwd) {
    let body=block.body;
    for(const [key,value]of Object.entries(binding.substitute||{}))body=body.replaceAll(key,value);
    assert(!/\b(?:WORD|ANCHOR)_C\d+\b/.test(body),'shell: unresolved binding');
    const output=run('bash',['-euo','pipefail','-c',body],{cwd,env,timeout:45000});
    for(const {pattern,count}of binding.expect)assert.equal((output.match(new RegExp(pattern,'g'))||[]).length,count,`shell: ${block.id} expected ${pattern}`);
    results.push({id:block.id,expect:binding.expect,substitute:binding.substitute||{},output});
  }
  for(const block of selected) {
    assert(bindings[block.id],`shell: runnable block lacks expected outcomes: ${block.name}`);
    if(block.id==='shared-checks')continue;
    const dir=path.join(work,block.id);fs.mkdirSync(dir);
    execute(block,bindings[block.id],dir);
    if(block.id==='small-certificates') {
      for(const d of [4,8])fs.renameSync(path.join(dir,`d${d}`),path.join(work,`d${d}`));
    } else {
      const shared=blocks.find(b=>b.id==='shared-checks');
      execute({...shared,body:block.body.split('sic-construct')[0]+shared.body},bindings['shared-checks'],dir);
      execute({id:block.id+'-certificate',body:block.body.split('sic-construct')[0]+'sic-certify-crt "$FILE" --threads "$T"'},
        {expect:[{pattern:'RESULT: EXACT SIC[^\\n]*\\[PROVED\\]',count:1}]},dir);
    }
  }
  fs.writeFileSync(path.join(work,'recipe-results.json'),JSON.stringify(results,null,2)+'\n');
  for(const d of [4,8])assert(fs.statSync(path.join(work,`d${d}/fresh.sicx`)).size>0,'shell: missing fresh data');
}
if(process.argv[1]===new URL(import.meta.url).pathname) {
  const [build,work]=process.argv.slice(2).map(p=>path.resolve(p));
  refuses(()=>shellBlocks('```sh\ntrue\n```','control'),/shell: unclassified/);
  refuses(()=>checkOptions('sic-decode FILE OUT --policy-unknown','control'),/shell: unknown option/);
  refuses(()=>run('bash',['-euo','pipefail','-c','false'],{status:0}),/false/);
  recipes(build,work);
  console.log('PASS runnable shell blocks and template options: real d4/d8 recipes, shared checks and fresh certificates');
  console.log('SIC_NEGATIVE_CONTROL:shell-options');
}
