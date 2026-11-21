// SPDX-License-Identifier: AGPL-3.0-or-later
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
const [tool, dump, fixtures, numeric, hoggar, work] = process.argv.slice(2);
fs.rmSync(work, {recursive:true, force:true}); fs.mkdirSync(work, {recursive:true});
function run(bin, args, success=true) {
 const r=spawnSync(bin,args,{encoding:'utf8',timeout:30000});
 assert(!r.error, String(r.error));
 if(success) assert.equal(r.status,0,`${args}: ${r.stdout}${r.stderr}`);
 else assert.notEqual(r.status,0,`${args} unexpectedly accepted`);
 return r;
}
const formats=run(tool,['formats']).stdout.trim().split(' ');
for(const c of ['d2','d4','d8','d8-maximal']) {
 const dir=path.join(work,c), rays=path.join(dir,'rays');fs.mkdirSync(dir);
 run(dump,[path.join(fixtures,c+'.sicx'),'--json-rays',rays,'--multiplet',c==='d8'?'m2':'m1']);
 const ray=path.join(rays,'c0.json'), out=path.join(dir,'numeric');
 const fp=JSON.parse(fs.readFileSync(ray)).fp64.values.map(v=>v.join(' ')).join('\n')+'\n';
 const txt=path.join(dir,'values.fp64.txt');fs.writeFileSync(txt,fp);run(tool,['check',ray,txt]);
 fs.writeFileSync(txt,fp+'0');run(tool,['check',ray,txt],false);
 run(tool,['write',ray,out,'--formats',formats.join(',')]);
 run(tool,['check',ray,...formats.map(f=>path.join(out,'c0.'+f))]);
 if(formats.includes('h5')) Atomics.wait(new Int32Array(new SharedArrayBuffer(4)),0,0,1100);
 const repeat=path.join(dir,'repeat');run(tool,['write',ray,repeat,'--formats',formats.join(',')]);
 for(const f of formats) assert.deepEqual(fs.readFileSync(path.join(out,'c0.'+f)),fs.readFileSync(path.join(repeat,'c0.'+f)),f+' deterministic bytes');
 run(tool,['bundle',path.join(dir,'a.npz'),c==='d2'?'2':c==='d4'?'4':'8',ray]);
 run(tool,['bundle',path.join(dir,'b.npz'),c==='d2'?'2':c==='d4'?'4':'8',ray]);
 assert.deepEqual(fs.readFileSync(path.join(dir,'a.npz')),fs.readFileSync(path.join(dir,'b.npz')));
 run(tool,['check-bundle',path.join(dir,'a.npz'),ray]);
 // Matrix structure is independently walked by the check reader; assert column shape as well.
 const mat=fs.readFileSync(path.join(out,'c0.mat'));assert.equal(mat.subarray(0,19).toString(),'MATLAB 5.0 MAT-file');
 let at=136;const dims=[];
 while(at<mat.length){const tag=mat.readUInt32LE(at),n=tag>>>16||mat.readUInt32LE(at+4),off=at+(tag>>>16?4:8);if((tag&65535)===5&&n===8)dims.push([mat.readUInt32LE(off),mat.readUInt32LE(off+4)]);at+=tag>>>16?8:8+Math.ceil(n/8)*8;}
 assert(dims.some(([d,cols])=>d===Number(JSON.parse(fs.readFileSync(ray)).d)&&cols===1));
 const bad=Buffer.from(fs.readFileSync(path.join(out,'c0.npy')));bad[bad.length-1]^=1;fs.writeFileSync(path.join(dir,'bad.npy'),bad);
 run(tool,['check',ray,path.join(dir,'bad.npy')],false);
 const csv=fs.readFileSync(path.join(out,'c0.csv'),'utf8').replace(/(\d)(e[+-]\d+,)/,(_,d,e)=>(d==='9'?'8':'9')+e);
 fs.writeFileSync(path.join(dir,'bad.csv'),csv);run(tool,['check',ray,path.join(dir,'bad.csv')],false);
 const broken=JSON.parse(fs.readFileSync(ray));broken.fp64.decimal[0][0]='0.0';fs.writeFileSync(path.join(dir,'bad.json'),JSON.stringify(broken));
 const absent=path.join(dir,'must-not-exist');run(tool,['write',path.join(dir,'bad.json'),absent],false);assert(!fs.existsSync(absent));
 run(tool,['write',ray,absent,'--formats','npy,unknown'],false);assert(!fs.existsSync(absent));
 run(tool,['bundle',path.join(dir,'absent.npz'),'999',ray],false);assert(!fs.existsSync(path.join(dir,'absent.npz')));
 // Existing outputs are preflighted before another selected format can be written.
 const conflict=path.join(dir,'conflict');fs.mkdirSync(conflict);fs.writeFileSync(path.join(conflict,'c0.npy'),'sentinel');
 run(tool,['write',ray,conflict],false);assert.deepEqual(fs.readdirSync(conflict),['c0.npy']);assert.equal(fs.readFileSync(path.join(conflict,'c0.npy'),'utf8'),'sentinel');
}
// Keep the committed publication CSV as an oracle independent of JSON/writers.
const ray=path.join(work,'d8/rays/c0.json');
const oracle=fs.readFileSync(path.join(numeric,'d8/m2/c0/fiducial-fp64.csv'),'utf8').trim().split('\n').slice(1);
const values=JSON.parse(fs.readFileSync(ray)).fp64.values;assert.equal(oracle.length,values.length);
for(let i=0;i<oracle.length;++i){const row=oracle[i].split(',');assert.equal(Number(row[0]),i);for(let k=0;k<2;++k){const b=Buffer.alloc(8);b.writeDoubleBE(Number(row[k+1]));assert.equal(b.toString('hex'),values[i][k],`publication CSV coordinate ${i}/${k}`);}}
const h=path.join(hoggar,'c0.json');
run(tool,['check',h,...formats.map(f=>path.join(hoggar,'c0.'+f))]);
for (const identity of ['hoggar-fiducial-json/1.0', 'sic-hoggar-fiducial-json/1']) { // identity-negative
 const old=JSON.parse(fs.readFileSync(h));old.schema=identity;
 const file=path.join(work,'old-hoggar.json');fs.writeFileSync(file,JSON.stringify(old));
 const refused=run(tool,['write',file,path.join(work,'old-hoggar-output')],false);
 assert.match(refused.stderr,/unsupported schema/);
 assert(!fs.existsSync(path.join(work,'old-hoggar-output')));
}
const ordinary=path.join(work,'d8-maximal/rays/c0.json'), bundle=path.join(work,'hoggar.npz');
run(tool,['bundle',bundle,'8',ray,ordinary,'--exceptional',h,'pauli-3-qubit']);
run(tool,['check-bundle',bundle,ordinary,ray,'--exceptional',h]);
const again=path.join(work,'hoggar-again.npz');
run(tool,['bundle',again,'8',ordinary,ray,'--exceptional',h,'pauli-3-qubit']);
assert.deepEqual(fs.readFileSync(bundle),fs.readFileSync(again));
const currentZip=fs.readFileSync(bundle);
// Locate the end record by its signature, independently of comment contents.
let end=currentZip.length-22;
while(end>=0 && currentZip.readUInt32LE(end)!==0x06054b50)--end;
assert(end>=0);
const comment=JSON.parse(currentZip.subarray(end+22).toString());
assert.equal(comment.schema,'sic-npz-bundle/1.0');
for(const identity of ['sic-npz-bundle/1','sic-npz-bundle/1.0.0']) { // identity-negative
 const oldComment=currentZip.subarray(end+22).toString().replace('sic-npz-bundle/1.0',identity);
 const header=Buffer.from(currentZip.subarray(0,end+22));header.writeUInt16LE(Buffer.byteLength(oldComment),end+20);
 const file=path.join(work,'old-comment.npz');fs.writeFileSync(file,Buffer.concat([header,Buffer.from(oldComment)]));
 const refused=run(tool,['check-bundle',file,ordinary,ray,'--exceptional',h],false);
 assert.match(refused.stderr,/unsupported NPZ comment schema/);
}
const zip=Buffer.from(fs.readFileSync(bundle));zip[100]^=1;fs.writeFileSync(path.join(work,'bad.npz'),zip);
run(tool,['check-bundle',path.join(work,'bad.npz'),ordinary,ray,'--exceptional',h],false);
run(tool,['check-bundle',bundle,ray],false);
const layout=path.join(work,'layout');
run(tool,['layout',path.join(fixtures,'d8-maximal.sicx'),layout,'--multiplet','1']);
run(tool,['layout',path.join(fixtures,'d8.sicx'),layout,'--multiplet','2']);
run(tool,['catalogue',layout]);
assert.match(run(tool,['list',path.join(layout,'catalogue.json')]).stdout,/multiplet d8-m2 /);
const absentLayout=path.join(work,'absent-layout');
run(tool,['layout',path.join(work,'missing.sicx'),absentLayout,'--multiplet','1'],false);assert(!fs.existsSync(absentLayout));
run(tool,['layout',path.join(fixtures,'d8.sicx'),absentLayout,'--multiplet','0'],false);assert(!fs.existsSync(absentLayout));
const catalogue=path.join(fixtures,'d8_release_catalogue.json');
const listing=run(tool,['list',catalogue]).stdout;assert.match(listing,/multiplet d8-m1 /);assert.match(listing,/multiplet d8-m2 /);
for (const schema of ['sic-release-catalogue/2.0', 'sic-release-catalogue/1', 'sic-release-catalogue/0', 'wrong']) { // identity-negative
 const c=JSON.parse(fs.readFileSync(catalogue));c.schema=schema;
 const bad=path.join(work,'wrong-catalogue.json');fs.writeFileSync(bad,JSON.stringify(c));run(tool,['list',bad],false);
}
const manifest=path.join(layout,'d8/m1/manifest.json'), originalManifest=fs.readFileSync(manifest);
const generatedCatalogue=path.join(layout,'catalogue.json');
for (const schema of ['sic-multiplet-manifest/2', 'sic-multiplet-manifest/1', 'wrong']) { // identity-negative
 const m=JSON.parse(originalManifest);m.schema=schema;fs.writeFileSync(manifest,JSON.stringify(m));
 fs.rmSync(generatedCatalogue,{force:true});
 const result=run(tool,['catalogue',layout],false);
 assert.match(result.stderr,/unsupported multiplet manifest schema/);
}
fs.writeFileSync(manifest,originalManifest);
const exceptional=path.join(layout,'d8/hoggar');
fs.cpSync(hoggar,exceptional,{recursive:true});
fs.rmSync(generatedCatalogue,{force:true});
run(tool,['catalogue',layout]);
const hoggarManifest=path.join(exceptional,'manifest.json');
const originalHoggarManifest=fs.readFileSync(hoggarManifest);
for (const schema of ['hoggar-manifest/1.0', 'sic-hoggar-manifest/1', 'sic-hoggar-manifest/2.0']) { // identity-negative
 const m=JSON.parse(originalHoggarManifest);m.schema=schema;
 fs.writeFileSync(hoggarManifest,JSON.stringify(m));
 fs.rmSync(generatedCatalogue,{force:true});
 const result=run(tool,['catalogue',layout],false);
 assert.match(result.stderr,/unsupported exceptional manifest schema/);
}
fs.writeFileSync(hoggarManifest,originalHoggarManifest);
const c=JSON.parse(fs.readFileSync(catalogue));c.schema='wrong';fs.writeFileSync(path.join(work,'wrong.json'),JSON.stringify(c));run(tool,['list',path.join(work,'wrong.json')],false);
console.log('PASS format readbacks, d8/m2 CSV oracle, MAT column layout, deterministic/Hoggar NPZ, corruption and no-output refusals');

// Malformed numeric text must fail with a categorized parse error.
const sourceRay=JSON.parse(fs.readFileSync(ray));
for (const d of ['8junk', '-8', '+8', ' 8', '18446744073709551616']) {
 const bad=structuredClone(sourceRay);bad.d=d;
 const input=path.join(work,'bad-dimension.json');fs.writeFileSync(input,JSON.stringify(bad));
 const result=run(tool,['check',input,path.join(work,'d8/rays/c0.csv')],false);
 assert.match(result.stderr,/\[invalid_format\]/);
}
console.log('PASS strict ray dimensions');
for (const anchor of ['1junk', '-1', '+1', ' 1', '18446744073709551615', '18446744073709551616']) {
 const bad=structuredClone(sourceRay);bad.labels.anchor=anchor;
 const input=path.join(work,'bad-anchor.json');fs.writeFileSync(input,JSON.stringify(bad));
 const out=path.join(work,'bad-anchor-output');
 const result=run(tool,['write',input,out],false);
 assert.match(result.stderr,/\[invalid_format\]/);
 assert(!fs.existsSync(out),'malformed anchor published output');
}
console.log('PASS strict file anchors and no-output refusal');

// The native dimension fits uint64, but multiplying it by 16 would wrap to 128.
const npy=fs.readFileSync(path.join(work,'d8/numeric/c0.npy'));
const headerBytes=npy.readUInt16LE(8);
const shape=(2n ** 60n + 8n).toString();
const dict=`{'descr': '<c16', 'fortran_order': False, 'shape': (${shape},), }`;
assert(dict.length<headerBytes);
const hugeNpy=path.join(work,'overflow-shape.npy');
fs.writeFileSync(hugeNpy,Buffer.concat([npy.subarray(0,10),
 Buffer.from(dict.padEnd(headerBytes-1,' ')+'\n'),npy.subarray(10+headerBytes)]));
assert.match(run(tool,['check',ray,hugeNpy],false).stderr,/\[invalid_format\]/);
console.log('PASS NPY dimension multiplication overflow refusal');

// Non-finite decimal fields must fail before comparing against the ray.
for (const value of ['nan', 'inf']) {
 for (const [column, label] of [[1, 'real'], [2, 'imaginary']]) {
  const input=path.join(work,`nonfinite-${value}-${label}.csv`);
  const rows=fs.readFileSync(path.join(work,'d8/numeric/c0.csv'),'utf8').split('\n');
  const fields=rows[1].split(',');fields[column]=value;rows[1]=fields.join(',');
  fs.writeFileSync(input,rows.join('\n'));
  const result=run(tool,['check',ray,input],false);
  assert.match(result.stderr,/\[invalid_format\]/);
  assert(result.stderr.includes(`${input}: CSV ${label}: invalid number`),result.stderr);
 }
}
console.log('PASS non-finite CSV rejection with source paths');
