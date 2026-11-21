// SPDX-License-Identifier: AGPL-3.0-or-later
// Called by the existing relocated installed-consumer owner after installation.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {root,read,files,run,equalSet,refuses} from './runtime_checks.mjs';
const [prefix,mode,libdir='lib',bindir='bin',includedir='include',apps='ON'] = process.argv.slice(2);
const inventory=JSON.parse(read(path.join(root,'docs/install_contract.json')));
assert(inventory[mode],'install: unknown configuration');
const contract=structuredClone(inventory[mode]);
if(apps==='OFF')contract.tools=contract.tools.filter(n=>!inventory.optional_apps.includes(n));
assert(contract,'install: unknown configuration');
function check(actual,expected,what) {equalSet(actual,expected,`install: ${what}`);}
const tools=fs.readdirSync(path.join(prefix,bindir)).sort();
check(tools,contract.tools,'tool inventory');
const headers=files(path.join(prefix,includedir)).map(p=>path.relative(path.join(prefix,includedir),p));
check(headers,contract.headers,'public headers');
const exports=read(path.join(prefix,libdir,'cmake/sic/sicTargets.cmake'));
const targets=[...exports.matchAll(/add_(?:library|executable)\(([^ )\n]+)/g)].map(m=>{
  assert(m[1].startsWith('sic::'),`install: unexpected exported namespace ${m[1]}`);return m[1].slice(5);
});
check(targets,contract.components,'exported component targets');
const config=read(path.join(prefix,libdir,'cmake/sic/sicConfig.cmake'));
for(const c of contract.components) assert(config.includes(`set(sic_${c}_FOUND `),`install: absent component ${c}`);
for(const name of tools) {
  const program=path.join(prefix,bindir,name);
  const help=run(program,['--help']);assert(help.startsWith('usage:'),`install: ${name} help`);
  const version=JSON.parse(run(program,['--version']));
  assert.equal(version.name,name,`install: ${name} version identity`);
  assert.equal(version.version,'1.0.0',`install: ${name} version`);
}
refuses(()=>check([...tools,'sic-undocumented'],contract.tools,'added tool'),/install:/);
refuses(()=>check(headers.slice(1),contract.headers,'removed header'),/install:/);
refuses(()=>check([...targets,'undocumented'],contract.components,'added component'),/install:/);
console.log(`PASS installed contract: ${tools.length} runnable tools, ${headers.length} headers, ${targets.length} components; negative controls rejected`);

console.log('SIC_NEGATIVE_CONTROL:install-contract');
