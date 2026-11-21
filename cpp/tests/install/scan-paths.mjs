// SPDX-License-Identifier: AGPL-3.0-or-later
// Exempt only complete ELF RUNPATH strings, not other occurrences of their paths.
import fs from 'node:fs';
import path from 'node:path';
const [root, ...args] = process.argv.slice(2);
const separator = args.indexOf('--dependencies');
if (!root || separator < 0 || args.some(s => !s)) throw Error('expected root, forbidden paths, --dependencies, dependency paths');
const forbidden = args.slice(0, separator);
const dependencies = args.slice(separator + 1);
let errors = 0;
function scan(file) {
  const stat = fs.lstatSync(file);
  if (stat.isDirectory()) { for (const name of fs.readdirSync(file)) scan(path.join(file, name)); return; }
  if (stat.isSymbolicLink()) {
    const target = fs.readlinkSync(file);
    if ([...forbidden, ...dependencies].some(s => target.includes(s))) { console.error(`path in symlink: ${file}`); errors++; }
    return;
  }
  if (!stat.isFile()) return;
  const bytes = fs.readFileSync(file);
  const ranges = [];
  if (bytes.subarray(0, 4).equals(Buffer.from([0x7f, 0x45, 0x4c, 0x46]))) {
    const is64 = bytes[4] === 2, little = bytes[5] === 1;
    const u16 = o => little ? bytes.readUInt16LE(o) : bytes.readUInt16BE(o);
    const u32 = o => little ? bytes.readUInt32LE(o) : bytes.readUInt32BE(o);
    const u64 = o => Number(little ? bytes.readBigUInt64LE(o) : bytes.readBigUInt64BE(o));
    const word = is64 ? u64 : u32;
    const shoff = word(is64 ? 40 : 32), shentsize = u16(is64 ? 58 : 46);
    const shnum = u16(is64 ? 60 : 48);
    const section = i => shoff + i * shentsize;
    for (let i = 0; i < shnum; i++) {
      const sh = section(i);
      if (u32(sh + 4) !== 6) continue; // SHT_DYNAMIC
      const offset = word(sh + (is64 ? 24 : 16));
      const size = word(sh + (is64 ? 32 : 20));
      const strings = section(u32(sh + (is64 ? 40 : 24)));
      const strOffset = word(strings + (is64 ? 24 : 16));
      const entrySize = is64 ? 16 : 8;
      for (let pos = offset; pos + entrySize <= offset + size; pos += entrySize) {
        if (word(pos) !== 29) continue; // DT_RUNPATH
        const start = strOffset + word(pos + entrySize / 2);
        const end = bytes.indexOf(0, start);
        if (end < start) throw Error(`invalid RUNPATH in ${file}`);
        ranges.push([start, end]);
      }
    }
  }
  for (const value of [...forbidden, ...dependencies]) {
    const needle = Buffer.from(value);
    for (let at = bytes.indexOf(needle); at !== -1; at = bytes.indexOf(needle, at + 1)) {
      if (!dependencies.includes(value) || !ranges.some(([start, end]) => at >= start && at + needle.length <= end)) {
        console.error(`forbidden path in ${file}: ${value}`); errors++;
      }
    }
  }
}
scan(root);
process.exitCode = errors ? 1 : 0;
