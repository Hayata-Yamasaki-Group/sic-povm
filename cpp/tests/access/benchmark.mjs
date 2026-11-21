// SPDX-License-Identifier: AGPL-3.0-or-later
// Manual final-format subset/streaming benchmark, served only by the Node fixture.
// Usage: node benchmark.mjs FILE BINARY_DIR WORK [BASE_DECODER]
import fs from 'node:fs/promises';
import {createReadStream} from 'node:fs';
import path from 'node:path';
import {spawn} from 'node:child_process';
import {createHash} from 'node:crypto';
import assert from 'node:assert/strict';
import {startServer} from './server.mjs';
const [file, binaryDir, work, baseline] = process.argv.slice(2);
await fs.mkdir(work, {recursive: true});
const scratch = path.join(work, 'scratch'); await fs.mkdir(scratch);
const threads = Number(process.env.X4_THREADS || 8);
async function run(name, executable, args) {
  const out = [], err = [], start = performance.now();
  const code = await new Promise((resolve, reject) => {
    const p = spawn(executable, args, {env: {...process.env, SIC_THREADS: String(threads)}});
    p.stdout.on('data', b => out.push(b)); p.stderr.on('data', b => err.push(b));
    p.on('close', resolve); p.on('error', reject);
  });
  const stdout = Buffer.concat(out).toString(), stderr = Buffer.concat(err).toString();
  await fs.writeFile(path.join(work, name + '.stdout'), stdout);
  await fs.writeFile(path.join(work, name + '.stderr'), stderr);
  assert.equal(code, 0, `${name} failed: ${stderr}`);
  return {elapsed: (performance.now() - start) / 1000,
    decoder_wall: Number(stdout.match(/TIME wall=([\d.]+)/)?.[1]),
    peak_rss_kib: Number(stderr.match(/Maximum resident set size \(kbytes\): (\d+)/)?.[1]),
    io: Object.fromEntries([...stdout.matchAll(/(\w+)=(\d+)/g)].map(x => [x[1], Number(x[2])]))};
}
async function hash(file) {const h = createHash('sha256'); for await (const b of createReadStream(file)) h.update(b); return h.digest('hex');}
const fd = await fs.open(file), head = Buffer.alloc(32); await fd.read(head, 0, 32, 0);
const text = Buffer.alloc(Number(head.readBigUInt64LE(24))); await fd.read(text, 0, text.length, 32); await fd.close();
const d = Number(JSON.parse(text).d), inputBytes = (await fs.stat(file)).size;
const inputSha256 = await hash(file);
const parts = path.join(scratch, 'parts');
if (process.env.X4_PARTS_MANIFEST) {
  // Reuse a split, fully verified immutable snapshot on repeat runs.
  const manifest = JSON.parse(await fs.readFile(process.env.X4_PARTS_MANIFEST, 'utf8'));
  assert.equal(manifest.sha256, inputSha256); assert.equal(Number(manifest.size_bytes), inputBytes);
  assert.equal(manifest.parts.length, 1, 'repeat harness expects one part');
  await fs.mkdir(parts); await fs.link(file, path.join(parts, manifest.parts[0].asset));
  await fs.copyFile(process.env.X4_PARTS_MANIFEST, path.join(parts, 'multiplet.sicx.parts.json'));
} else {
  await run('split', path.join(binaryDir, 'sic-container'), ['split', file, '--out', parts, '--asset-prefix', `d${d}-m1`]);
}
await fs.copyFile(path.join(parts, 'multiplet.sicx.parts.json'), path.join(work, 'multiplet.sicx.parts.json'));
const server = await startServer(parts);
const summary = {input: file, input_bytes: inputBytes, input_sha256: inputSha256, dimension: d,
  reused_parts_manifest: process.env.X4_PARTS_MANIFEST || null,
  target_bits: 128, threads, memory_mib: 8192, seed: 'not applicable (deterministic)',
  coordinates: [0, d / 2], rays: ['c0', 'c1'], runs: {}, raw_sha256: {}, local_timing: []};
const persist = () => fs.writeFile(path.join(work, 'measurement.json'), JSON.stringify(summary, null, 2) + '\n');
try {
  const args = ['--rays', 'c0,c1', '--coordinates', `0,${d / 2}`, '--target-bits', '128', '--threads', String(threads), '--no-pin', '--memory-mib', '8192', '--check-budget'];
  const remote = ['--remote', path.join(parts, 'multiplet.sicx.parts.json'), '--source', server.url + '/range', '--cache', path.join(scratch, 'cache')];
  for (const [name, source, extra] of [['local', [file], []], ['cold', remote, []], ['warm', remote, []],
      ['no-prefetch', [...remote.slice(0, -1), path.join(scratch, 'no-prefetch-cache')], ['--prefetch-mib', '0']]]) {
    const before = {bytes: server.stats.bytes, requests: server.stats.requests};
    const record = await run(name, '/usr/bin/time', ['-v', path.join(binaryDir, 'sic-decode'), ...source, path.join(scratch, name), ...args, ...extra]);
    record.transferred = server.stats.bytes - before.bytes; record.requests = server.stats.requests - before.requests;
    record.fraction = record.transferred / inputBytes;
    summary.runs[name] = record;
    for (const ray of summary.rays) {
      const raw = `${ray}.raw-selected.balls`, actual = await fs.readFile(path.join(scratch, name, raw));
      if (name === 'local') summary.raw_sha256[ray] = createHash('sha256').update(actual).digest('hex');
      else assert.deepEqual(actual, await fs.readFile(path.join(scratch, 'local', raw)));
    }
    assert(record.peak_rss_kib * 1024 <= 8192 * 1048576, 'RSS exceeds budget');
    if (name === 'cold') assert(record.transferred < inputBytes * 0.05, 'selected dependency transfer exceeds 5%');
    if (name === 'warm') assert.equal(record.transferred, 0, 'warm cache refetched data');
    await persist();
  }
  // A complete ray streams all representatives with a bounded BODY window.
  // Paired local runs compare the actual decoder-reported wall time to the base.
  const fullArgs = ['--rays', 'c0', '--threads', String(threads), '--no-pin', '--memory-mib', '8192', '--check-budget'];
  const beforeFull = {bytes: server.stats.bytes, requests: server.stats.requests};
  summary.runs.full_remote = await run('full-remote', '/usr/bin/time', ['-v', path.join(binaryDir, 'sic-decode'),
    ...remote.slice(0, -1), path.join(scratch, 'full-cache'), path.join(scratch, 'full-remote'),
    ...fullArgs, '--remote-max-fraction', '1', '--tower', 'stream']);
  summary.runs.full_remote.transferred = server.stats.bytes - beforeFull.bytes;
  summary.runs.full_remote.requests = server.stats.requests - beforeFull.requests;
  summary.runs.full_remote.fraction = summary.runs.full_remote.transferred / inputBytes;
  const fullRows = (await fs.readFile(path.join(scratch, 'full-remote', 'c0.raw.balls'), 'utf8')).trim().split('\n').slice(2, -1);
  const subsetRows = (await fs.readFile(path.join(scratch, 'local', 'c0.raw-selected.balls'), 'utf8')).trim().split('\n').slice(2, -1);
  assert.deepEqual(subsetRows, summary.coordinates.map(m => fullRows[m]), 'large subset differs from full ray');
  summary.subset_equals_full = true;
  assert(summary.runs.full_remote.transferred <= inputBytes + 4096, 'full remote duplicate transfer');
  assert(summary.runs.full_remote.peak_rss_kib * 1024 <= 8192 * 1048576, 'full remote RSS');
  await persist();
  const repeats = baseline ? Number(process.env.X4_TIMING_REPEATS || 5) : 1;
  for (let i = 0; i < repeats; ++i) {
    for (const name of (i % 2 ? ['after', 'before'] : ['before', 'after'])) {
      if (name === 'before' && !baseline) continue;
      const out = path.join(scratch, `${name}-${i}`);
      const m = await run(`${name}-${i}`, '/usr/bin/time', ['-v', name === 'before' ? baseline : path.join(binaryDir, 'sic-decode'), file, out, ...fullArgs]);
      summary.local_timing.push({iteration: i, variant: name, ...m});
      for (const suffix of ['raw.balls', 'normalized.balls', 'fp64.txt']) {
        const output = await fs.readFile(path.join(out, 'c0.' + suffix));
        const key = 'full_' + suffix;
        if (!summary[key]) summary[key] = createHash('sha256').update(output).digest('hex');
        else assert.equal(createHash('sha256').update(output).digest('hex'), summary[key], 'local output changed');
        assert.deepEqual(output, await fs.readFile(path.join(scratch, 'full-remote', 'c0.' + suffix)), 'full remote output changed');
      }
      await fs.rm(out, {recursive: true}); await persist();
    }
  }
  summary.byte_identical = true; summary.peak_server_concurrency = server.stats.peakActive;
  summary.certification = 'Certified Arb raw subset outputs match local bytes; full local ray agrees with stored BALL and certified FP64. No field/group/SIC algebraic certificate rerun.';
  await persist(); console.log(JSON.stringify(summary, null, 2));
} finally {await server.close(); await fs.rm(scratch, {recursive: true, force: true});}
