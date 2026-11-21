// SPDX-License-Identifier: AGPL-3.0-or-later
// Locator specifications classify prose and select its source authorities.
// Generated values and parser options always come from current source/help.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {root,read,run,toolPath,files} from '../runtime_checks.mjs';
import {sourceOptions,helpValues} from '../contracts.mjs';
export function defaultParagraphs(text) {return text.split(/\n\s*\n/).filter(p=>/\bdefaults?\b/i.test(p)&&!/^```/.test(p)&&!/^\|/.test(p));}
export function generateDefaults() {
  const specs=JSON.parse(read(path.join(root,'scripts/docs/contracts/default_sources.json')));
  const docs=[...files(path.join(root,'docs')).filter(p=>p.endsWith('.md')&&!p.endsWith('/tools.md')),
    path.join(root,'README.md'),path.join(root,'CONTRIBUTING.md')];
  const paragraphs=docs.flatMap(p=>defaultParagraphs(read(p)).map(claim=>({file:path.relative(root,p),claim})));
  const result=specs.map(spec=>{
    const matches=paragraphs.filter(p=>p.file===spec.file&&
      p.claim.replace(/\s+/g,' ').startsWith(spec.starts.replace(/\s+/g,' ')));
    assert.equal(matches.length,1,`generate: default claim locator ${spec.file}: ${spec.starts}`);
    return {...matches[0],kind:spec.kind,...(spec.reason?{reason:spec.reason}:{}),authority:spec.authority.map(b=>{
      const hits=[...read(path.join(root,b.file)).matchAll(new RegExp(b.pattern,'g'))];
      assert.equal(hits.length,1,`generate: source authority ${b.file}: ${b.pattern}`);
      return {file:b.file,text:hits[0][0]};
    })};
  });
  assert.equal(result.length,paragraphs.length,'generate: unclassified default paragraph');
  return result;
}
export function generateCli(build) {
  return JSON.parse(read(path.join(root,'scripts/docs/contracts/contract_sources.json'))).map(spec=>{
    const help=run(toolPath(build,spec.tool),[...(spec.command?[spec.command]:[]),'--help']);
    let options;
    if(spec.independent) {
      const text=spec.sources.map(p=>read(path.join(root,p))).join('\n');
      const names=[...new Set([...text.matchAll(/==\s*"(--[a-z0-9-]+)"/g)].map(m=>m[1]))];
      options=Object.fromEntries(names.map(name=>{
        const values=helpValues(help,name),n=values?values.split(/\s+/).length:0;
        return [name,{operands:[n,n],repeat:!['--help','--version'].includes(name),values}];
      }));
    } else options=sourceOptions(spec);
    return {...spec,options};
  });
}
if(process.argv[1]===new URL(import.meta.url).pathname) {
  const [mode,build]=process.argv.slice(2);
  assert(['--write','--check'].includes(mode)&&build,'usage: generate_contracts.mjs --write|--check EXACT_BUILD');
  for(const [file,value]of [['cli_contract.json',generateCli(path.resolve(build))],['default_contract.json',generateDefaults()]]) {
    const output=JSON.stringify(value,null,2)+'\n',dest=path.join(root,'docs',file);
    if(mode==='--write')fs.writeFileSync(dest,output);
    else assert.equal(read(dest),output,`generate: stale ${file}; run --write and review the diff`);
  }
}
