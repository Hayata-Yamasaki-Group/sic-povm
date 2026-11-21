// SPDX-License-Identifier: AGPL-3.0-or-later
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawn, spawnSync} from 'node:child_process';
import {startServer} from '../access/server.mjs';
const [bin, fixture, work, remoteEnabled] = process.argv.slice(2);
fs.rmSync(work, {recursive: true, force: true});
fs.mkdirSync(work, {recursive: true});
const stop = path.join(work, 'stop');
fs.writeFileSync(stop, 'stop');
function run(name, args, expected) {
  const r = spawnSync(path.join(bin, name), args, {encoding: 'utf8', timeout: 30000});
  assert.ifError(r.error);
  assert.equal(r.status, expected, `${name} ${args}: ${r.stdout}${r.stderr}`);
  return r;
}
for (const stopPath of [stop, path.join(work, 'absent')]) {
  for (const [name, args] of [
    ['sic-verify', [fixture, '--threads', '1']],
    ['sic-container', ['read-range', fixture, '0', '1']],
  ]) {
    const r = run(name, [...args, '--stop-file', stopPath], 2);
    assert.equal(r.stdout, '');
    assert.match(r.stderr, /unknown option --stop-file/);
    assert.match(r.stderr, /usage:/);
  }
}
const out = path.join(work, 'decode');
const stopped = run('sic-decode', [fixture, out, '--threads', '1', '--stop-file', stop], 1);
assert.equal(stopped.stdout, '');
assert.match(stopped.stderr, /stop|cancel/);
assert(!fs.existsSync(out));
run('sic-decode', [fixture, out, '--threads', '1', '--stop-file', path.join(work, 'absent')], 0);
assert(fs.existsSync(path.join(out, 'decode.provenance.json')));
console.log('PASS verify/container refusal (2), decoder cancellation (1) and absent-file decode (0)');

// The first HTTP request stalls before Reader can finish its metadata probe.
// Creating the stop file now must cancel that request, before Decoder binds/schedules.
if (remoteEnabled === 'ON') {
  const parts = path.join(work, 'parts');
  run('sic-container', ['split', fixture, '--out', parts, '--asset-prefix', 'd4-m1'], 0);
  const server = await startServer(parts);
  const remoteStop = path.join(work, 'remote-stop');
  const remoteOut = path.join(work, 'remote-out');
  let child;
  try {
    child = spawn(path.join(bin, 'sic-decode'), ['--remote', path.join(parts, 'multiplet.sicx.parts.json'),
      remoteOut, '--source', server.url + '/timeout', '--cache', path.join(work, 'cache'),
      '--remote-max-fraction', '1', '--threads', '1', '--range-jobs', '1',
      '--remote-timeout-ms', '10000', '--remote-retries', '0', '--stop-file', remoteStop]);
    let stdout = '', stderr = '';
    child.stdout.on('data', b => stdout += b);
    child.stderr.on('data', b => stderr += b);
    const completed = new Promise((resolve, reject) => {
      child.once('error', reject);
      child.once('close', resolve);
    });
    const limit = setTimeout(() => child.kill('SIGKILL'), 5000);
    try {
      const start = performance.now();
      while (!server.stats.timeouts && performance.now() - start < 2000)
        await new Promise(resolve => setTimeout(resolve, 10));
      assert.equal(server.stats.timeouts, 1, 'decoder reached its initial stalled remote read');
      fs.writeFileSync(remoteStop, 'stop');
      assert.equal(await completed, 1, stderr);
      assert.equal(stdout, '');
      assert.match(stderr, /requested stop/);
      assert(!fs.existsSync(remoteOut));
    } finally { clearTimeout(limit); }
  } finally {
    if (child && child.exitCode === null) child.kill('SIGKILL');
    await server.close();
  }
  console.log('PASS decoder stop file cancels the first remote metadata read');
}
