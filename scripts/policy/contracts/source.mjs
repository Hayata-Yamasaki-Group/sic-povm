// SPDX-License-Identifier: AGPL-3.0-or-later
import fs from 'node:fs';
import path from 'node:path';
export function production(root) {
  const result=[];
  function visit(dir) {
    if(!fs.existsSync(dir))return;
    for(const entry of fs.readdirSync(dir,{withFileTypes:true})) {
      const file=path.join(dir,entry.name);
      if(entry.isDirectory())visit(file);
      else if(entry.isFile() && /\.(cpp|hpp|tpp)$/.test(file))result.push(path.relative(root,file));
    }
  }
  for(const dir of ['cpp/src','cpp/include','cpp/apps'])visit(path.join(root,dir));
  return result.sort();
}
// Keep literals intact and omit comments, including comments containing code.
export function tokens(source) {
  return [...source.matchAll(/(?:u8|u|U|L)?R"([^ ()\\\t\r\n]{0,16})\([\s\S]*?\)\1"|\/\/[^\n]*|\/\*[\s\S]*?\*\/|"(?:\\[\s\S]|[^"\\])*"|'(?:\\.|[^'\\])*'|[A-Za-z_]\w*|\d+|[^\s]/g)]
    .filter(m=>!m[0].startsWith('//')&&!m[0].startsWith('/*')).map(m=>m[0]);
}
