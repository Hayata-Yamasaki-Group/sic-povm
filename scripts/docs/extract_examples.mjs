#!/usr/bin/env node
import fs from 'node:fs';
import path from 'node:path';
const [docs,out]=process.argv.slice(2);
fs.mkdirSync(out,{recursive:true});
let sources=[];
for(const name of fs.readdirSync(docs).filter(x=>x.endsWith('.md')).sort()) {
 const content=fs.readFileSync(path.join(docs,name),'utf8');
 const re=/<!--\s*example\s*-->\s*\n```(?:cpp|c\+\+)\s*\n([\s\S]*?)\n```/g;
 for(const m of content.matchAll(re)) { const file=`example_${sources.length}.cpp`; fs.writeFileSync(path.join(out,file),m[1]+'\n'); sources.push(file); }
 const marks=(content.match(/<!--\s*example\s*-->/g)||[]).length;
 if(marks !== [...content.matchAll(re)].length) { console.error(`${name}: example marker must precede a fenced C++ block`); process.exit(1); }
}
fs.writeFileSync(path.join(out,'examples.cmake'),sources.map((s,i)=>`sic_add_executable(sic-test-support-doc-example-${i} "${out}/${s}")\nadd_test(NAME sic_test_support_doc_example_${i} COMMAND sic-test-support-doc-example-${i})\nset_tests_properties(sic_test_support_doc_example_${i} PROPERTIES LABELS docs)`).join('\n')+'\n');
