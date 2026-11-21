// SPDX-License-Identifier: AGPL-3.0-or-later
// Selected-parser metadata inventory; independent checkers keep separate contracts.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import {root,read,run,toolPath,equalSet,refuses} from './runtime_checks.mjs';
import {optionDeclarations} from './cli_contract.mjs';
export function brace(text,start) {
  let depth=0,quoted=false,escape=false;
  for(let end=start;end<text.length;++end){const c=text[end];if(escape){escape=false;continue;}
    if(quoted&&c==='\\'){escape=true;continue;}if(c==='"')quoted=!quoted;
    if(!quoted&&c==='{')++depth;if(!quoted&&c==='}'&&--depth===0)return text.slice(start,end+1);
  }
  throw Error('contracts: unterminated parser initializer');
}
export function helpValues(help,name) {
  if(['--help','--version'].includes(name))return '';
  const line=help.split('\n').find(l=>l.startsWith('  '+name+' ') || l.trim()===name);
  if(line) {
    const field=line.trim().split(/ {2,}/)[0].slice(name.length).trim();
    if(!field)return '';
    const words=field.split(/\s+/);let values=words.shift();
    while(words.length && /^[A-Z][A-Z0-9_]*(?:\.[a-z]+)?$/.test(words[0]))values+=' '+words.shift();
    return values;
  }
  const match=help.match(new RegExp(name+'(?:[ \t]+([^\\]\\n]+))?'));
  assert(match,`contracts: undocumented option ${name}`);
  return (match[1]||'').split(/\s+\|\s+|\s+--/)[0].trim();
}
export function sourceOptions(row) {
  const text=row.sources.map(p=>read(path.join(root,p))).join('\n');
  if(row.independent) {
    const names=[...text.matchAll(/==\s*"(--[a-z0-9-]+)"/g)].map(m=>m[1]);
    equalSet(names,Object.keys(row.options),'contracts: independent accepted option inventory');
    return null;
  }
  let opts=optionDeclarations(text);
  if(row.tool==='sic-data'&&!row.command)opts={};
  if(row.command && ['sic-data','sic-container'].includes(row.tool)) {
    const start=text.indexOf(`{"${row.command}",`);assert(start>=0,`contracts: missing ${row.command}`);
    // The selected initializer follows the whole-tool option declaration table.
    const body=brace(text,start);
    if(row.tool==='sic-data')opts=optionDeclarations(body);
    else if(body.includes('SourceArguments::parser'))opts=optionDeclarations(read(path.join(root,'cpp/src/access/source_arguments.cpp')));
    else if(body.includes('.select(')){
      const selection=body.slice(body.indexOf('.select('));
      const selected=[...selection.matchAll(/"(--[a-z0-9-]+)"/g)].map(m=>m[1]);
      opts=Object.fromEntries(selected.map(n=>{assert(opts[n],`contracts: unknown selected ${n}`);return [n,opts[n]];}));
    } else opts={};
  }
  return {...opts,'--help':{values:'',operands:[0,0],repeat:false},'--version':{values:'',operands:[0,0],repeat:false}};
}
export function checkRow(row,help) {
  const actual=sourceOptions(row);
  if(!row.independent)assert.deepEqual(actual,row.options,`contracts: option metadata ${row.tool} ${row.command}`);
  const names=[...help.matchAll(/(?:^|[\s\[|])(--[a-z][a-z0-9-]*)/g)].map(m=>m[1]);
  equalSet(names,Object.keys(row.options),`contracts: help options ${row.tool} ${row.command}`);
  for(const [name,option] of Object.entries(row.options)) {
    assert(Array.isArray(option.operands) && option.operands.length===2 && option.operands[0]<=option.operands[1],`contracts: operands ${name}`);
    const values=helpValues(help,name);
    assert.equal(values,option.values,`contracts: help operands ${row.tool} ${row.command} ${name}`);
    assert.equal(values ? values.split(/\s+/).length : 0,option.operands[1],`contracts: operand count ${name}`);
    if(option.default!==undefined) assert(help.includes(option.default),`contracts: rendered default ${name}`);
  }
}
function independentProbes(row,bin) {
  const fixture=path.join(root,'cpp/tests/fixtures/sicx/d4.sicx');
  const text=read(path.join(root,'cpp/tests/fixtures/sicx/d4.jsonl')).trimEnd().split('\n');
  const footer=JSON.parse(text.at(-1));
  const values={'--decode':'off','--text':path.join(root,'cpp/tests/fixtures/sicx/d4.jsonl'),
    '--prec':'256','--threshold':'1','--inject':'rep','--ray':row.tool==='sic-verify-independent'?'c0':'0',
    '--exact-root':footer.exact_root,'--expect-exact-root':footer.exact_root,
    '--file-root':footer.merkle_root,'--expect-file-root':footer.merkle_root,
    '--sha256':crypto.createHash('sha256').update(fs.readFileSync(fixture)).digest('hex'),
    '--expect-sha256':crypto.createHash('sha256').update(fs.readFileSync(fixture)).digest('hex')};
  run(bin,[],{status:2});
  run(bin,[fixture,'surplus-positional'],{status:2});
  if(row.tool==='sic-verify-independent')
    run(bin,['/nonexistent.sicx',fixture,'--decode','off'],{status:2});
  for(const [name,opt]of Object.entries(row.options)) {
    if(['--help','--version'].includes(name))continue;
    if(opt.operands[0])run(bin,[fixture,name],{status:2});
    const value=values[name] || '1';
    const operands=Array(opt.operands[1]).fill(value);
    let args=[fixture,name,...operands,name,...operands];
    if(row.tool==='sic-verify-independent')args.push('--selftest');
    else if(row.tool==='sic-check-crt-input') {
      if(!['--partial-element','--partial-chunk','--partial-metadata'].includes(name))args.push('--partial-metadata');
    } else args.push('--help');
    run(bin,args,{status:opt.repeat?0:2});
  }
}
export function checkContracts(build) {
  const inventory=JSON.parse(read(path.join(root,'docs/cli_contract.json')));
  let checked=0;
  for(const row of inventory) {
    const cache=read(path.join(build,'CMakeCache.txt'));
    if(/SIC_ENABLE_FLINT:BOOL=OFF/.test(cache) && !['sic-check','sic-overlaps','sic-identify','sic-stabilizer'].includes(row.tool))continue;
    if(/SIC_BUILD_APPS:BOOL=OFF/.test(cache) && ['sic-check','sic-overlaps','sic-identify','sic-stabilizer'].includes(row.tool))continue;
    const bin=toolPath(build,row.tool);
    const help=run(bin,[...(row.command?[row.command]:[]),'--help']);
    checkRow(row,help);++checked;
    if(row.independent)independentProbes(row,bin);
    // Bad grammar must fail before any numerical work or file publication.
    const args=row.command?[row.command]:[];
    if(row.tool==='sic-inspect')for(const [mode,arity]of Object.entries(row.mode_arities)) {
      const operand=row.options[mode].operands[1] ? [mode,'0'] : [mode];
      if(arity[0]>0)run(bin,[...Array(arity[0]-1).fill('missing'),...operand],{status:2});
      run(bin,[...Array(arity[1]+1).fill('missing'),...operand],{status:2});
    }
    run(bin,[...args,'--policy-unknown-option'],{status:2});
    if(row.arity && row.arity[0]>0 && !row.independent && row.tool!=='sic-inspect')
      run(bin,[...args],{status:2});
    if(row.arity && row.arity[1]!==null && !row.independent && row.tool!=='sic-inspect')
      run(bin,[...args,...Array(row.arity[1]+1).fill('missing')],{status:2});
    if(!row.independent) for(const [name,opt] of Object.entries(row.options)) {
      if(['--help','--version'].includes(name))continue;
      if(opt.operands[0]>0)run(bin,[...args,name],{status:2});
      // --help prevents I/O only after grammar and duplicate checks run.
      run(bin,[...args,name,...Array(opt.operands[1]).fill('1'),name,...Array(opt.operands[1]).fill('1'),'--help'],{status:opt.repeat?0:2});
    }
  }
  assert(checked>0,'contracts: no built tools');
  if(fs.existsSync(path.join(build,'sic-data'))) {
    const scratch=fs.mkdtempSync(path.join(build,'help-contract-'));
    try {
      run(process.execPath,[path.join(root,'cpp/tests/cli/sic_test_data_subcommand_arity.mjs'),build,root,path.join(scratch,'data-arity')]);
      run(process.execPath,[path.join(root,'cpp/tests/cli/sic_test_inspect_json_multiplet.mjs'),path.join(build,'sic-inspect'),path.join(root,'cpp/tests/fixtures/sicx/d4.sicx'),path.join(scratch,'inspect-multiplet')]);
    } finally {
      fs.rmSync(scratch,{recursive:true,force:true});
    }
  }
  const base=inventory.find(r=>r.tool==='sic-data'&&r.command==='check-bundle');
  const bad=structuredClone(base);bad.options['--exceptional'].operands=[2,2];
  refuses(()=>checkRow(bad,'unused'),/contracts:/);
  const help=run(toolPath(build,'sic-check'),['--help']);
  refuses(()=>checkRow(inventory.find(r=>r.tool==='sic-check'),help.replace('--help','--wrong-help')),/contracts:/);
  const missing=structuredClone(inventory[0]);delete missing.options['--help'];
  refuses(()=>checkRow(missing,'unused'),/contracts:/);
  console.log(`PASS help contracts: ${checked} selected grammars; operand and missing-option controls rejected`);
}
