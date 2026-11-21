// SPDX-License-Identifier: AGPL-3.0-or-later
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
export const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
export const read = file => fs.readFileSync(file, 'utf8');
export function files(dir) {
  return fs.readdirSync(dir, {withFileTypes:true}).sort((a,b) => a.name.localeCompare(b.name)).flatMap(e => {
    if (e.isDirectory() && /^(?:\.git|build(?:-.*)?)$/.test(e.name)) return [];
    const file = path.join(dir, e.name);
    assert(!e.isSymbolicLink(),`Unexpected symlink in checked tree: ${file}`);
    return e.isDirectory() ? files(file) : e.isFile() ? [file] : [];
  });
}
export function run(program, args, options = {}) {
  const result = spawnSync(program, args, {encoding:'utf8', timeout:20000,
    env:{...process.env, SIC_THREADS:'1', SIC_MEMORY_MB:'2048', OMP_NUM_THREADS:'1', OPENBLAS_NUM_THREADS:'1'}, ...options});
  assert.ifError(result.error);
  assert.equal(result.signal, null, `${program}: signal ${result.signal}`);
  assert.equal(result.status, options.status ?? 0, `${program} ${args.join(' ')}: ${result.stdout}${result.stderr}`);
  return result.stdout;
}
export function refuses(action, pattern) {
  assert.throws(action, pattern, 'negative control did not fail at the intended rule');
}
export function equalSet(actual, expected, role) {
  assert.deepEqual([...new Set(actual)].sort(), [...new Set(expected)].sort(), role);
}
export function toolPath(build,name) {
  for (const dir of ['', 'checkers/verifier','checkers/norm','checkers/crt','checkers/stabilizer']) {
    const file=path.join(build,dir,name); if(fs.existsSync(file)) return file;
  }
  throw Error(`Missing documented tool ${name} in ${build}`);
}
