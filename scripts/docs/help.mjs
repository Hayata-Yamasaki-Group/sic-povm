#!/usr/bin/env node
// Generate the command reference from the installed tools' help output.
import fs from 'node:fs';
import {checkContracts} from './contracts.mjs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const names = ['sic-construct', 'sic-check', 'sic-overlaps',
  'sic-identify', 'sic-stabilizer', 'sic-decode', 'sic-verify',
  'sic-inspect', 'sic-container', 'sic-data', 'sic-check-balls',
  'sic-certify-stabilizer', 'sic-verify-independent', 'sic-certify-crt',
  'sic-certify-norm', 'sic-check-crt-input'];
const commands = {
  'sic-container': ['split', 'fetch', 'repair', 'read-range', 'from-text', 'merge-parts', 'merge-element-parts', 'reencode'],
  'sic-data': ['formats', 'write', 'check', 'bundle', 'check-bundle', 'list', 'layout', 'catalogue'],
};
function section(bin, name, args = []) {
  const result = spawnSync(bin, [...args, '--help'], {encoding: 'utf8', timeout: 10000});
  if (result.error || result.status !== 0 || result.stderr || !result.stdout.startsWith('usage: '))
    throw Error(`${name} ${args.join(' ')} --help failed: ${result.error || result.stderr}`);
  const title = [name, ...args].join(' ');
  return `<!-- cli:${title} -->\n## ${title}\n\n\`\`\`text\n${result.stdout}\`\`\`\n<!-- /cli:${title} -->\n`;
}
function blocks(bin, name) {
  const base = section(bin, name);
  const body = base.match(/```text\n[\s\S]*?```/)[0];
  const children = (commands[name] || []).map(c => {
    const full = section(bin, name, [c]);
    if (full.match(/```text\n[\s\S]*?```/)[0] !== body) return full;
    const title = `${name} ${c}`;
    return `<!-- cli:${title} -->\n## ${title}\n\nSee [${name}](#${name}) for the shared usage and options.\n<!-- /cli:${title} -->\n`;
  });
  return [base, ...children];
}
try {
  const [mode, ...input] = process.argv.slice(2);
  const args = input.length === 1 && (mode === '--write' || mode === '--check')
    ? names.map(name => path.join(input[0], 'bin', name)) : input;
  const docPath = path.join(root, 'docs/tools.md');
  if(mode === '--contracts' && args.length===1) {
    checkContracts(path.resolve(args[0]));
  } else if (mode === '--write' || mode === '--check') {
    if (args.length !== names.length) throw Error(`Expected ${names.length} executable paths in the documented order`);
    let doc = '# Command-line tools\n\nGenerated from the tools’ `--help` output by `scripts/docs/help.mjs`.\n\n';
    for (let i = 0; i < names.length; i++) doc += blocks(args[i], names[i]).join('\n') + '\n';
    doc = doc.trimEnd() + '\n';
    if (mode === '--write') fs.writeFileSync(docPath, doc);
    else if (fs.readFileSync(docPath, 'utf8') !== doc) throw Error('docs/tools.md differs from executable help');
  } else if (mode === '--check-tool' && args.length === 2) {
    const [name, bin] = args;
    if (!names.includes(name)) throw Error(`Unknown tool ${name}`);
    const doc = fs.readFileSync(docPath, 'utf8');
    for (const block of blocks(bin, name)) if (!doc.includes(block)) throw Error(`${name}: documented help differs from executable help`);
  } else throw Error('usage: help.mjs --write|--check PREFIX (or ordered TOOL_PATH...) | --check-tool NAME TOOL_PATH');
} catch (error) { console.error(error.message); process.exitCode = 1; }

console.log('SIC_NEGATIVE_CONTROL:help-contract');
