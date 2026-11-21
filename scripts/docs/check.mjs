#!/usr/bin/env node
// Offline documentation checks. Paths are relative to the repository root.
import fs from 'node:fs';
import {checkPaths} from './paths.mjs';
import {refuses} from './runtime_checks.mjs';
import {readPublicExclusions} from '../policy/public_tree/exclusions.mjs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
process.chdir(root);
const mode = process.argv[2];
const read = p => fs.readFileSync(p, 'utf8');
const walk = d => fs.existsSync(d) ? fs.readdirSync(d, {withFileTypes:true}).flatMap(e => e.isDirectory() ? (/^(?:\.git|notes|results|paper|private|archive|build(?:-.*)?)$/.test(e.name) ? [] : walk(`${d}/${e.name}`)) : e.isFile() ? [`${d}/${e.name}`] : []) : [];
const {entries: excluded} = readPublicExclusions(root);
const md = walk('.').map(p => p.slice(2))
 .filter(p => p.endsWith('.md') && (p.includes('/') || !excluded.has(p)));
let count = 0;
function report(p, line, message) { console.log(`${p}:${line}: ${message}`); count++; }
const allow = new Set(read('scripts/docs/allow_list.txt').split(/\r?\n/).filter(x => x && !x.startsWith('#')));
function allowed(key) { return allow.has(key); }
if (mode === 'links') {
 for(const name of ['include/sic/absent.hpp','tests/absent.cpp','cpp/src/absent_contract.cpp','cpp/absent_contract.cpp','cpp/src/absent_contract/','sic/support/absent_contract.hpp','sic/support/absent-contract.h','sic/support/absent_contract.hxx'])
   refuses(()=>checkPaths(root,'```cpp\n#include \"'+name+'\"\n```','control'),/paths:/);
 for(const p of md) checkPaths(root,read(p),p);
 if(process.argv.includes('--negative-control')) checkPaths(root,'`data/absent_contract.json`','control');
 for (const p of md) {
  const s = read(p); const lines = s.split('\n'); let fence = false;
  const refs = new Map(); for (const line of lines) { const m = line.match(/^\s{0,3}\[([^\]]+)\]:\s*(<[^>]+>|\S+)/); if (m) refs.set(m[1].toLowerCase().replace(/\s+/g,' '), m[2]); }
  for (let i=0;i<lines.length;i++) { const line=lines[i]; if (/^\s*(```|~~~)/.test(line)) { fence=!fence; continue; } if(fence) continue;
   const prose=line.replace(/`+[^`]*`+/g,'');
   const links=[...prose.matchAll(/!?\[[^\]]*\]\((<[^>]+>|(?:[^()\s]|\([^()]*\))+)(?:\s+[^)]*)?\)/g)].map(m=>m[1]);
   for (const m of prose.matchAll(/!?\[([^\]]+)\]\[([^\]]*)\]/g)) { const key=(m[2] || m[1]).toLowerCase().replace(/\s+/g,' '); if(refs.has(key)) links.push(refs.get(key)); else report(p,i+1,`missing link reference ${key}`); }
   for (const link of links) {
    let href=link.replace(/^<|>$/g,''); if (/^(?:[a-z][a-z\d+.-]*:|\/\/)/i.test(href)) continue;
    try { href=decodeURIComponent(href); } catch { report(p,i+1,`invalid URL ${href}`); continue; }
    const [target, anchor] = href.split('#'); const dest=target ? path.resolve(path.dirname(p),target) : path.resolve(p);
    if (!fs.existsSync(dest)) { report(p,i+1,`missing link ${href}`); continue; }
    if(anchor && fs.statSync(dest).isFile() && /\.md$/i.test(dest)) {
     const seen=new Map(); const ids=new Set(); for(const l of read(dest).split('\n')) { const h=l.match(/^\s{0,3}#{1,6}\s+(.+?)\s*#*\s*$/); if(h) { let id=h[1].toLowerCase().replace(/<[^>]*>/g,'').replace(/[^\p{L}\p{N}_\- ]/gu,'').replace(/ /g,'-'); const n=seen.get(id)||0; seen.set(id,n+1); ids.add(id+(n?`-${n}`:'')); } for(const a of l.matchAll(/\bid=["']([^"']+)["']/g)) ids.add(a[1]); }
     if(!ids.has(anchor)) report(p,i+1,`missing anchor ${href}`);
    }
   }
  }
 }
} else if(mode === 'status') {
 for(const p of md) { const lines=read(p).split('\n'); for(let i=0;i<lines.length;i++) { const text=lines.slice(Math.max(0,i-1),i+2).join(' '); if ((/\b(proves|proved|proven)\b/i.test(lines[i]) && /\b(certified|conditional)\b/i.test(text)) || (/\b(certified|conditional)\b/i.test(lines[i]) && /\b(proves|proved|proven)\b/i.test(text)) || (/\bexact\b/i.test(lines[i]) && /\bconditional\b/i.test(text) && /\brays?\b/i.test(text))) { const key=`${p}:${lines[i].trim()}`; if(!allowed(key)) report(p,i+1,`review status wording: ${lines[i].trim()}`); } } }
} else if(mode === 'private') {
 const files=[...walk('docs'), ...walk('cpp').filter(p => !/^cpp\/build[^/]*\//.test(p)), ...walk('scripts'), ...walk('data').filter(p => p.endsWith('.md')), ...walk('references').filter(p => p.endsWith('.md')), 'README.md','CONTRIBUTING.md'];
 for(const p of files) { if(p === 'scripts/docs/allow_list.txt') continue; if(!/\.(?:md|cpp|cc|cxx|hpp|h|hh|hxx|cmake|txt|sh|mjs)$/.test(p) && !p.endsWith('CMakeLists.txt')) continue; const lines=read(p).split('\n'); for(let i=0;i<lines.length;i++) { if(/(?:^|[\s`"'(/=])(?:\.\.\/|\.\/|\$\{[^}]+\}\/)*(?:results|notes|paper)\//.test(lines[i])) { const key=`${p}:${lines[i].trim()}`; if(!allowed(key)) report(p,i+1,`private-tree reference: ${lines[i].trim()}`); } } }
} else { console.error('usage: check.mjs links|status|private'); process.exit(2); }
console.log(`${mode}: ${count} finding(s)`); process.exitCode=count ? 1 : 0;

console.log('SIC_NEGATIVE_CONTROL:docs-links');
