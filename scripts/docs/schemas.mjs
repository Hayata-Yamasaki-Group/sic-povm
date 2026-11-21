// SPDX-License-Identifier: AGPL-3.0-or-later
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {root,read,files,run,refuses,equalSet,toolPath} from './runtime_checks.mjs';
import {checkSchema,validate,corpus} from './schema_validator.mjs';
import {recipes} from './shell.mjs';
const [build,work] = process.argv.slice(2).map(p => path.resolve(p));
const schemas = new Map(files(path.join(root,'docs/schemas')).map(p => {
  const s = JSON.parse(read(p)); checkSchema(s); corpus.set(path.basename(p),s); return [s.$id,s];
}));
const seen = new Set(); let records = 0, controls = 0;
function record(id, value) {
  assert(schemas.has(id),`schema: unknown family ${id}`);
  const s = schemas.get(id); validate(s,value); seen.add(id); ++records;
  // Every family's actual positive is also a type and missing-required control.
  refuses(() => validate(s,null), /schema:/); ++controls;
  const missing = structuredClone(value);
  if ('schema' in missing) {
    delete missing.schema; refuses(() => validate(s,missing), /schema:/); ++controls;
    const version = {...value,schema:'invalid-version'};
    refuses(() => validate(s,version), /schema:/); ++controls;
  } else {
    for (const key of Object.keys(missing)) delete missing[key];
    refuses(() => validate(s,missing), /schema:/); ++controls;
  }
}
function json(file, id) {const value=JSON.parse(read(file)); record(id || value.schema,value);}
recipes(build,work);
for (const d of [4,8]) {
  const dir=path.join(work,`d${d}`), file=path.join(dir,'fresh.sicx');
  const tool=(name,args) => run(toolPath(build,name),args,{cwd:dir});
  json(path.join(dir,'finalize/decode.provenance.json'),'sic-decode-provenance/1.0');
  for (const name of fs.readdirSync(build).filter(n => /^sic-/.test(n) && fs.statSync(path.join(build,n)).isFile() && (fs.statSync(path.join(build,n)).mode & 0o111))) {
    // Only public tools are in the generated command reference.
    if (!read(path.join(root,'docs/tools.md')).includes(`<!-- cli:${name} -->`)) continue;
    record('sic-tool-version/1.0',JSON.parse(tool(name,['--version'])));
  }
  tool('sic-inspect',[file,'--text-json',path.join(dir,'text.jsonl'),'--threads','1']);
  for (const line of read(path.join(dir,'text.jsonl')).trimEnd().split('\n')) record('sicx-text-json/1.0',JSON.parse(line));
  tool('sic-inspect',[file,'--text-json',path.join(dir,'parts.json'),'--part-bytes','4096','--threads','1']);
  // Split text manifests use the requested path plus .parts.json.
  for (const p of files(dir).filter(p => p.endsWith('.parts.json'))) json(p);
  tool('sic-inspect',['--exact-json',file,path.join(dir,'exact.json'),'--multiplet','m1']); json(path.join(dir,'exact.json'));
  tool('sic-inspect',[file,'--json-rays',path.join(dir,'rays'),'--multiplet','m1']);
  tool('sic-inspect',[file,'--json-all',path.join(dir,'all.json'),'--multiplet','m1']); json(path.join(dir,'all.json'));
  const ray=path.join(dir,'rays/c0.json'); json(ray);
  tool('sic-data',['bundle',path.join(dir,'bundle.npz'),String(d),ray]);
  const zip=fs.readFileSync(path.join(dir,'bundle.npz'));
  const end=zip.lastIndexOf(Buffer.from([0x50,0x4b,0x05,0x06])); assert(end>=0,'schema: NPZ end record');
  record('sic-npz-bundle/1.0',JSON.parse(zip.subarray(end+22,end+22+zip.readUInt16LE(end+20)).toString()));
  tool('sic-data',['layout',file,path.join(dir,'release'),'--multiplet','1','--threads','1']);
  tool('sic-data',['catalogue',path.join(dir,'release')]);
  for (const p of files(path.join(dir,'release')).filter(p=>p.endsWith('.json'))) json(p);
  tool('sic-container',['split',file,'--out',path.join(dir,'transport'),'--asset-prefix',`d${d}-m1`]);
  json(path.join(dir,'transport/multiplet.sicx.parts.json'));
}
// Exceptional and numerical metadata families are committed inputs, not fabricated fresh output.
for (const p of files(path.join(root,'data/d8/hoggar')).filter(p=>p.endsWith('.json'))) json(p);
for (const p of files(path.join(root,'cpp/tests/fixtures/numeric')).filter(p=>p.endsWith('/metadata.json'))) json(p);
equalSet(seen,schemas.keys(),'schema: every committed family must be exercised');
refuses(()=>checkSchema({type:'object',unsupportedAssertion:true}),/unsupported keyword/);
console.log(`PASS schema smoke: ${schemas.size} families, ${records} positive records, ${controls} rejected mutations`);

console.log('SIC_NEGATIVE_CONTROL:schema-smoke');
