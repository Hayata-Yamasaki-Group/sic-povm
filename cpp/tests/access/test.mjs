// SPDX-License-Identifier: AGPL-3.0-or-later
import fs from 'node:fs/promises';
import path from 'node:path';
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createHash} from 'node:crypto';
import {startServer} from './server.mjs';

const [which, bin, fixtures, work, enabled] = process.argv.slice(2);
const httpEnabled = enabled === 'ON';
await fs.rm(work, {recursive: true, force: true}); await fs.mkdir(work, {recursive: true});
let checks = 0;
function check(v, message) {assert(v, message); ++checks;}
function equal(a, b, message) {assert.deepEqual(a, b, message); ++checks;}
async function run(exe, args, {ok = true, env = {}, signal} = {}) {
  return await new Promise((resolve, reject) => {
    const p = spawn(path.join(bin, exe), args, {env: {...process.env, SIC_THREADS: '2', ...env}, signal});
    const out = [], err = [];
    p.stdout.on('data', b => out.push(b)); p.stderr.on('data', b => err.push(b));
    p.on('error', reject);
    p.on('close', code => {
      const stdout = Buffer.concat(out), stderr = Buffer.concat(err).toString();
      if ((code === 0) !== ok) reject(Error(`${exe} ${args.join(' ')} exit ${code}\n${stdout}\n${stderr}`));
      else resolve({stdout, stderr, code});
    });
  });
}
const hash = b => createHash('sha256').update(b).digest();
// Test mirrors exercise explicit, nonuniform chunk-aligned ranges. The production
// splitter is called first with the release ceiling; its canonical manifest is
// then repartitioned only inside this local test harness.
const canonical = x => Array.isArray(x) ? x.map(canonical) : x && typeof x === 'object' ? Object.fromEntries(Object.keys(x).sort().map(k => [k, canonical(x[k])])) : x;
async function split(file, name = 'parts') {
  const dir = path.join(work, name);
  const input = await fs.readFile(file);
  const d = JSON.parse(input.subarray(32, 32 + Number(input.readBigUInt64LE(24))).toString()).d;
  const prefix = 'd' + d + '-m1';
  await run('sic-container', ['split', file, '--out', dir, '--asset-prefix', prefix]);
  const manifest = path.join(dir, 'multiplet.sicx.parts.json');
  const m = JSON.parse(await fs.readFile(manifest, 'utf8')), bytes = await fs.readFile(file);
  const f = bytes.subarray(bytes.length - 112), po = Number(f.readBigUInt64LE(16));
  const n = Number(bytes.readBigUInt64LE(po + 16));
  let suffix = Number(f.readBigUInt64LE(0)); const ends = [];
  for (let i = 0; i < n; ++i) {
    const e = po + 24 + i * 112, type = bytes.toString('ascii', e, e + 4);
    if (type === 'RAYS') {suffix = Number(bytes.readBigUInt64LE(e + 8)); break;}
    ends.push(Number(bytes.readBigUInt64LE(e + 8) + 16n + bytes.readBigUInt64LE(e + 16)));
  }
  for (const part of m.parts) await fs.rm(path.join(dir, part.asset));
  const cuts = [...new Set([...ends.filter(x => x <= suffix), suffix, bytes.length])];
  let offset = 0; m.parts = [];
  for (const end of cuts) {
    if (end <= offset) continue;
    const index = m.parts.length, asset = prefix + '.sicx.part-' + String(index).padStart(4, '0'), body = bytes.subarray(offset, end);
    await fs.writeFile(path.join(dir, asset), body);
    m.parts.push({asset, index: String(index), offset: String(offset), sha256: hash(body).toString('hex'), size_bytes: String(body.length)}); offset = end;
  }
  await fs.writeFile(manifest, JSON.stringify(canonical(m)) + '\n');
  return {dir, manifest};
}
async function entries(dir) {
  const out = [];
  for (const x of await fs.readdir(dir, {withFileTypes: true})) {
    const f = path.join(dir, x.name);
    if (x.isDirectory()) out.push(...await entries(f));
    else out.push(f);
  }
  return out;
}
function remote(manifest, source, cache = path.join(work, 'cache')) {
  return ['--remote', manifest, '--source', source, '--cache', cache, '--remote-max-fraction', '1'];
}
const file = path.join(fixtures, 'd4.sicx');
const data = await fs.readFile(file);
const parts = await split(file);
const server = httpEnabled ? await startServer(parts.dir) : null;
try {
  if (which === 'access') {
    // Shared parsing refuses malformed/duplicate options before opening a source.
    const beforeInvalid = server?.stats.requests;
    for (const [exe, prefix] of [['sic-container', ['read-range']], ['sic-decode', []], ['sic-verify', []]]) {
      for (const bad of [
        ['--range-jobs', '0'], ['--range-jobs', '33'], ['--range-jobs', '1x'],
        ['--range-bytes', '8388609'], ['--remote-retries', '11'],
        ['--coalesce-gap', '8388609'], ['--prefetch-mib', '257'], ['--prefetch-mib', '-1'],
        ['--remote-timeout-ms', '-1'], ['--remote-max-fraction', 'nan'],
        ['--remote-max-fraction', '0'], ['--cache', '--quick'],
        ['--fetch-full', '--fetch-full'], ['--remote', parts.manifest],
      ]) {
        const result = await run(exe, [...prefix, '--remote', parts.manifest, ...bad], {ok: false});
        equal(result.code, 2, 'access usage error exits 2');
      }
      const help = await run(exe, [...prefix, '--help']);
      check(help.stdout.includes('--remote-max-fraction') && help.stdout.includes('--expect-root'), 'complete access help');
    }
    equal(server?.stats.requests, beforeInvalid, 'invalid CLI never opens network source');
    for (const source of [parts.dir, `file://${parts.dir}`, ...(server ? [server.url + '/range', server.url + '/redirect'] : [])]) {
      const cache = path.join(work, `cache-${checks}`);
      const args = ['read-range', ...remote(parts.manifest, source, cache), '4070', '220'];
      const a = await run('sic-container', args);
      equal(a.stdout, data.subarray(4070, 4290), 'cross-part bytes');
      const before = server?.stats.requests;
      const b = await run('sic-container', args);
      equal(b.stdout, a.stdout, 'persistent cache bytes');
      equal(server?.stats.requests, before, 'second process makes no network requests');
      const record = (await entries(cache)).find(x => x.endsWith('.range') && !x.includes('/bootstrap/') && (() => {const [b, n] = path.basename(x).split(/[-.]/).map(Number); return b <= 4070 && b + n > 4070;})());
      check(record, 'range cache entry exists');
      const bytes = await fs.readFile(record); bytes[bytes.length - 1] ^= 1; await fs.writeFile(record, bytes);
      const c = await run('sic-container', args);
      equal(c.stdout, a.stdout, 'corrupt cache record recovered');
      if (source.startsWith('http')) check(server.stats.requests > before, 'corrupt entry refetched');
    }
    // Fresh concurrent processes publish bootstrap and identity records safely.
    const concurrentCache = path.join(work, 'concurrent');
    const concurrentArgs = ['read-range', ...remote(parts.manifest, server ? server.url + '/range' : parts.dir, concurrentCache), '100', '180'];
    const concurrent = await Promise.all(Array.from({length: 8}, () => run('sic-container', concurrentArgs)));
    for (const result of concurrent) equal(result.stdout, data.subarray(100, 280), 'concurrent cache initialization');
    const networkBefore = server?.stats.requests;
    await Promise.all(Array.from({length: 8}, () => run('sic-container', concurrentArgs)));
    equal(server?.stats.requests, networkBefore, 'concurrent cached reopen makes no requests');
    // Cache remains fully usable with the HTTP server absent.
    if (server) {
      const cache = path.join(work, 'offline');
      const args = ['read-range', ...remote(parts.manifest, server.url + '/range', cache), '0', '80'];
      const a = await run('sic-container', args);
      const b = await run('sic-container', ['read-range', ...remote(parts.manifest, 'http://127.0.0.1:1/unreachable', cache), '0', '80']);
      equal(b.stdout, a.stdout, 'offline cache use');
    }
    const threshold = await run('sic-verify', ['--remote', parts.manifest, '--source', parts.dir, '--cache', path.join(work, 'threshold'), '--remote-max-fraction', '0.01', '--quick'], {ok: false});
    check(threshold.stderr.includes('sic-container fetch'), 'threshold recommends fetch');
    const full = await run('sic-verify', ['--remote', parts.manifest, '--source', parts.dir, '--cache', path.join(work, 'full'), '--remote-max-fraction', '0.01', '--fetch-full', '--threads', '2']);
    check(full.stdout.includes('PASS full'), 'explicit full fallback');
    const offlineFull = await run('sic-verify', ['--remote', parts.manifest, '--source', server ? 'http://127.0.0.1:1/offline' : parts.dir, '--cache', path.join(work, 'full'), '--quick']);
    check(offlineFull.stdout.includes('PASS quick'), 'verified full fallback cache works offline');
    // Default threshold refuses a whole-file verification at plan admission.
    const fullPlan = await run('sic-verify', ['--remote', parts.manifest, '--source', parts.dir, '--cache', path.join(work, 'default-threshold'), '--threads', '2'], {ok: false});
    check(fullPlan.stderr.includes('read plan exceeds'), 'default 50% threshold');
    const cap = await run('sic-container', ['read-range', ...remote(parts.manifest, parts.dir, path.join(work, 'cap')), '--cache-max-ranges', '64', '--range-bytes', '1', '0', String(data.length)], {ok: false});
    check(cap.stderr.includes('cache-max-ranges') && cap.stderr.includes('sic-container fetch'), 'metadata cap refuses safely');
    const invalid = await run('sic-container', ['read-range', ...remote(parts.manifest, parts.dir), String(data.length), '1'], {ok: false});
    check(invalid.stderr.includes('beyond'), 'out of bounds');
    if (!server) {
      const off = await run('sic-verify', ['--remote', parts.manifest, '--source', 'https://example.invalid', '--cache', path.join(work, 'disabled'), '--quick'], {ok: false});
      check(off.stderr.includes('disabled at build time'), 'HTTP-disabled diagnostic');
    }
  } else if (which === 'faults') {
    if (!server) {console.log('HTTP disabled: faults skipped'); process.exitCode = 0;}
    else {
      for (const mode of ['wrong-range', 'wrong-size', 'missing-range', 'corrupt']) {
        const result = await run('sic-verify', [...remote(parts.manifest, server.url + '/' + mode, path.join(work, mode)), '--quick'], {ok: false});
        check(/Content-Range|SHA-256|digest|magic|geometry|footer|manifest|unsupported header/.test(result.stderr), `${mode} rejected: ${result.stderr}`);
      }
      const payloadCache = path.join(work, 'payload-corruption');
      const payload = await run('sic-verify', [...remote(parts.manifest, server.url + '/corrupt-payload', payloadCache), '--threads', '1'], {ok: false});
      check(/SHA-256|digest/.test(payload.stderr), 'remote chunk payload corruption detected');
      const payloadFixed = await run('sic-verify', [...remote(parts.manifest, server.url + '/range', payloadCache), '--threads', '1']);
      check(payloadFixed.stdout.includes('PASS full'), 'bad payload range invalidated and refetched');
      const count = server.stats.requests;
      const noRange = await run('sic-verify', [...remote(parts.manifest, server.url + '/no-range', path.join(work, 'no-range')), '--quick'], {ok: false});
      check(noRange.stderr.includes('sic-container fetch'), '200 recommends fetch');
      equal(server.stats.requests - count, 1, '200 is not retried');
      equal(server.stats.noRangeBytes, 0, '200 body was aborted');
      const redirectCount = server.stats.requests;
      const terminal = await run('sic-verify', [...remote(parts.manifest, server.url + '/terminal-redirect', path.join(work, 'terminal-redirect')), '--quick'], {ok: false});
      check(terminal.stderr.includes('[io]') && terminal.stderr.includes('no Location'), 'terminal redirect is refused at headers');
      equal(server.stats.requests - redirectCount, 1, 'terminal redirect is not retried');
      equal(server.stats.noRangeBytes, 0, 'terminal redirect body aborted');
      const full = await run('sic-verify', [...remote(parts.manifest, server.url + '/no-range', path.join(work, 'no-range-full')), '--fetch-full', '--threads', '2']);
      check(full.stdout.includes('PASS full'), '200 opt-in full fallback verified');
      for (const mode of ['disconnect', 'retry', 'slow']) {
        const result = await run('sic-container', ['read-range', ...remote(parts.manifest, server.url + '/' + mode, path.join(work, mode)), '--remote-timeout-ms', '200', '0', String(data.length)]);
        equal(result.stdout, data, `${mode} recovery`);
      }
      check(server.stats.disconnects > 0, 'disconnect injected');
      const interruptedRanges = server.stats.ranges.filter(r => r.mode === 'disconnect');
      check(interruptedRanges.some((r, i) => interruptedRanges.slice(0, i).some(p => p.asset === r.asset && p.end === r.end && p.start < r.start)), 'retry resumes at the next byte instead of restarting');
      check(server.stats.retries > 0, '503 injected');
      check(server.stats.timeouts > 0, 'timeout injected');
      const timeout = await run('sic-verify', [...remote(parts.manifest, server.url + '/timeout', path.join(work, 'timeout')), '--remote-timeout-ms', '50', '--remote-retries', '1', '--quick'], {ok: false});
      check(timeout.stderr.includes('[io]') && timeout.stderr.includes('after retries'), 'bounded timeout retries');
      for (const [mode, kind] of [['range/missing-asset', 'io'], ['corrupt', 'integrity'], ['timeout', 'io']]) {
        const args = remote(parts.manifest, server.url + '/' + mode, path.join(work, 'full-' + mode.replaceAll('/', '-')));
        args[args.indexOf('--remote-max-fraction') + 1] = '0.000001';
        const result = await run('sic-verify', [...args, '--fetch-full', '--remote-timeout-ms', mode === 'timeout' ? '50' : '3000', '--remote-retries', '0', '--quick'], {ok: false});
        check(result.stderr.includes('[' + kind + ']') && result.stderr.includes('remote full download'), 'full download cause category: ' + mode);
      }
      const parallel = await run('sic-container', ['read-range', ...remote(parts.manifest, server.url + '/parallel', path.join(work, 'parallel')), '--range-jobs', '2', '--range-bytes', '64', '0', String(data.length)]);
      equal(parallel.stdout, data, 'parallel bytes');
      check(/peak_active=2/.test(parallel.stderr), 'parallel requests observed at bound 2');
      check(server.stats.ranges.every(x => x.start <= x.end && x.end < data.length), 'part-relative bounded ranges');
      // Interrupt a child after it has committed extents, then resume from them.
      const cache = path.join(work, 'interrupted');
      const args = ['read-range', ...remote(parts.manifest, server.url + '/parallel', cache), '--range-bytes', '64', '0', String(data.length)];
      const child = spawn(path.join(bin, 'sic-container'), args);
      child.stdout.resume(); child.stderr.resume();
      const closed = new Promise(resolve => child.on('close', resolve));
      for (let n = 0; n < 100; ++n) {
        let count = 0; try {count = (await entries(cache)).filter(x => x.endsWith('.range')).length;} catch {}
        if (count >= 5) break;
        await new Promise(resolve => setTimeout(resolve, 10));
      }
      child.kill('SIGKILL'); await closed;
      const persisted = (await entries(cache)).filter(x => x.endsWith('.range'));
      check(persisted.length >= 5, 'committed ranges survive interruption');
      const beforeResume = server.stats.bytes;
      const resumed = await run('sic-container', args);
      equal(resumed.stdout, data, 'interrupted process resumes');
      check(server.stats.bytes - beforeResume < data.length, 'resume reuses committed ranges');
    }
  } else if (which === 'versions') {
    const footer = data.subarray(data.length - 112);
    const lo = Number(footer.readBigUInt64LE(0)), po = Number(footer.readBigUInt64LE(16));
    const ldir = data.subarray(lo, po), all = Number(data.readBigUInt64LE(po + 16));
    const entries = Array.from({length: all}, (_, i) => Buffer.from(data.subarray(po + 24 + i * 112, po + 24 + (i + 1) * 112)));
    const finalAt = entries.findIndex(e => e.toString('ascii', 0, 4) === 'RAYS'); check(finalAt >= 0, 'finalized source');
    const end = Number(entries[finalAt].readBigUInt64LE(8));
    function merkle(es) {
      if (!es.length) return hash(Buffer.alloc(0));
      if (es.length === 1) return hash(Buffer.concat([Buffer.from([0]), es[0]]));
      let k = 1; while (k * 2 < es.length) k *= 2;
      return hash(Buffer.concat([Buffer.from([1]), merkle(es.slice(0, k)), merkle(es.slice(k))]));
    }
    const ldirItem = Buffer.concat([Buffer.from('SICX1-LDIR\0'), hash(ldir)]);
    const exactEntries = entries.slice(0, finalAt), pdir = Buffer.alloc(24 + exactEntries.length * 112);
    pdir.write('PDIR'); pdir.writeUInt32LE(1, 4); pdir.writeBigUInt64LE(BigInt(pdir.length - 16), 8); pdir.writeBigUInt64LE(BigInt(exactEntries.length), 16);
    exactEntries.forEach((e, i) => e.copy(pdir, 24 + 112 * i));
    const f = Buffer.from(footer); f.writeBigUInt64LE(BigInt(end), 0); f.writeBigUInt64LE(BigInt(end + ldir.length), 16); f.writeBigUInt64LE(BigInt(pdir.length - 16), 24);
    merkle([...exactEntries, ldirItem]).copy(f, 64); f.writeBigUInt64LE(BigInt(end + ldir.length + pdir.length + 112), 96);
    const before = Buffer.concat([data.subarray(0, end), ldir, pdir, f]);
    const beforeFile = path.join(work, 'before.sicx'); await fs.writeFile(beforeFile, before);
    const p0 = await split(beforeFile, 'before');
    const after = Buffer.from(data);
    for (let i = 0; i < all; ++i) {
      const entry = entries[i];
      if (entry.toString('ascii', 0, 4) !== 'RAYS') continue;
      const start = Number(entry.readBigUInt64LE(8)) + 16, len = Number(entry.readBigUInt64LE(16));
      const text = after.subarray(start, start + len).toString(), match = /"source_commit":"([a-f0-9]{40}|[a-f0-9]{64})"/.exec(text); check(match, 'decoder source commit found');
      const pos = start + match.index + '"source_commit":"'.length; after[pos] = after[pos] === 48 ? 49 : 48;
      hash(after.subarray(start, start + len)).copy(entry, 80); entry.copy(after, po + 24 + i * 112);
    }
    merkle([...entries, ldirItem]).copy(after, after.length - 48);
    const afterFile = path.join(work, 'after.sicx'); await fs.writeFile(afterFile, after);
    const p2 = await split(afterFile, 'after');
    if (httpEnabled) {
      const wholeOriginal = await split(file, 'whole-original', 8192);
      const wholeAlternate = await split(afterFile, 'whole-alternate', 8192);
      const switching = await startServer(wholeOriginal.dir, {rangeDirectory: wholeAlternate.dir});
      try {
        const changed = await run('sic-verify', ['--remote', wholeOriginal.manifest, '--source', switching.url + '/switch', '--cache', path.join(work, 'switching'), '--remote-max-fraction', '0.9', '--fetch-full', '--threads', '2'], {ok: false});
        check(/roots|snapshot|manifest|Content-Range/.test(changed.stderr), 'snapshot change refused: ' + changed.stderr);
      } finally {await switching.close();}
    }
    const cache = path.join(work, 'shared-cache');
    for (const [p, bytes] of [[p0, before], [parts, data], [p2, after], [p0, before], [parts, data], [p2, after]]) {
      const r = await run('sic-container', ['read-range', ...remote(p.manifest, p.dir, cache), '0', String(bytes.length)]);
      equal(r.stdout, bytes, 'finalization versions stay separate');
    }
    if (httpEnabled) {
      const versionServers = await Promise.all([p0, parts, p2].map(p => startServer(p.dir)));
      try {
        const httpCache = path.join(work, 'shared-cache-http');
        for (let pass = 0; pass < 2; ++pass) {
          for (const [i, [p, bytes]] of [[p0, before], [parts, data], [p2, after]].entries()) {
            const beforeCount = versionServers[i].stats.requests;
            const r = await run('sic-container', ['read-range', ...remote(p.manifest, versionServers[i].url + '/range', httpCache), '0', String(bytes.length)]);
            equal(r.stdout, bytes, 'HTTP finalization versions stay separate');
            if (pass) equal(versionServers[i].stats.requests, beforeCount, 'each HTTP version reused offline');
          }
        }
        for (const [i, p] of [p0, parts, p2].entries()) {
          for (const mode of ['wrong-range', 'corrupt-payload']) {
            const faultCache = path.join(work, `version-${i}-${mode}`);
            const result = await run('sic-verify', [...remote(p.manifest, versionServers[i].url + '/' + mode, faultCache), '--quick'], {ok: false});
            check(/Content-Range|SHA-256|digest/.test(result.stderr), `version ${i} ${mode} refused`);
            const repaired = await run('sic-verify', [...remote(p.manifest, versionServers[i].url + '/range', faultCache), '--quick']);
            check(repaired.stdout.includes('PASS quick'), 'pre/post finalization fault cache recovered');
          }
          const disconnected = await run('sic-verify', [...remote(p.manifest, versionServers[i].url + '/disconnect', path.join(work, `version-${i}-disconnect`)), '--quick']);
          check(disconnected.stdout.includes('PASS quick') && versionServers[i].stats.disconnects > 0, 'pre/post finalization disconnect recovered');
        }
      } finally {await Promise.all(versionServers.map(s => s.close()));}
    }
    const exactNamespaces = await fs.readdir(path.join(cache, 'ranges'));
    equal(exactNamespaces.length, 1, 'versions share one exact-root namespace');
    const namespaces = await fs.readdir(path.join(cache, 'ranges', exactNamespaces[0]));
    equal(namespaces.length, 3, 'file roots separate finalization snapshots');
    for (const root of [...exactNamespaces, ...namespaces]) check(/^[a-f0-9]{64}$/.test(root), 'cache root identity is a digest');
    const pin = await run('sic-verify', [...remote(parts.manifest, parts.dir, path.join(work, 'pin')), '--expect-exact-root', '0'.repeat(64), '--quick'], {ok: false});
    check(pin.stderr.includes('[integrity]') && pin.stderr.includes('root pin'), 'wrong trusted root rejected');
    // Same-size internally valid substitution is detected when roots are pinned.
    const substitute = await run('sic-verify', [...remote(parts.manifest, p2.dir, path.join(work, 'substitute')), '--expect-root', footer.subarray(64, 96).toString('hex'), '--quick'], {ok: false});
    check(/root pin|SHA-256/.test(substitute.stderr), 'valid alternate finalization rejected against manifest or pinned root');
    const badHash = JSON.parse(await fs.readFile(parts.manifest, 'utf8'));
    badHash.sha256 = '0'.repeat(64);
    const badHashPath = path.join(work, 'wrong-whole-hash.json'); await fs.writeFile(badHashPath, JSON.stringify(badHash));
    const wholeMismatch = await run('sic-container', ['read-range', ...remote(badHashPath, parts.dir, path.join(work, 'whole-hash')), '0', String(data.length)], {ok: false});
    check(wholeMismatch.stderr.includes('whole-file SHA-256'), 'complete sparse coverage verifies whole manifest hash');
    const wrongManifest = JSON.parse(await fs.readFile(parts.manifest, 'utf8'));
    wrongManifest.parts.at(-1).size_bytes = String(Number(wrongManifest.parts.at(-1).size_bytes) + 1);
    wrongManifest.size_bytes = String(Number(wrongManifest.size_bytes) + 1);
    const bad = path.join(work, 'wrong-size.json'); await fs.writeFile(bad, JSON.stringify(wrongManifest));
    const mismatch = await run('sic-verify', [...remote(bad, parts.dir, path.join(work, 'wrong-size-cache')), '--quick'], {ok: false});
    check(/size|end/.test(mismatch.stderr), 'manifest size mismatch rejected');
  } else if (which === 'schedule') {
    const fixture = path.join(fixtures, 'd16.sicx'), bytes = await fs.readFile(fixture);
    const dir = path.join(work, 'one-part');
    await run('sic-container', ['split', fixture, '--out', dir, '--asset-prefix', 'd16-m1']);
    const manifest = path.join(dir, 'multiplet.sicx.parts.json');
    const footer = bytes.subarray(bytes.length - 112), po = Number(footer.readBigUInt64LE(16));
    let corruptAt;
    for (let i = 0; i < Number(bytes.readBigUInt64LE(po + 16)); ++i) {
      const at = po + 24 + 112 * i;
      if (bytes.toString('ascii', at, at + 4) === 'BODY') {corruptAt = Number(bytes.readBigUInt64LE(at + 8)) + 20; break;}
    }
    const s = httpEnabled ? await startServer(dir, {corruptAt}) : null;
    const config = ['--threads', '2', '--no-pin', '--memory-mib', '256', '--check-budget', '--tower', 'stream'];
    const net = (mode, cache) => [...remote(manifest, s ? s.url + '/' + mode : dir, path.join(work, cache)),
      '--cache-max-ranges', '1024', '--range-bytes', '1024', '--range-jobs', '2'];
    const metrics = result => Object.fromEntries([...result.stdout.toString().matchAll(/(\w+)=(\d+)/g)].map(x => [x[1], Number(x[2])]));
    try {
      const library = await run('sic-test-decode-remote-decode-library', [fixture, manifest, s ? s.url + '/range' : dir, path.join(work, 'library-cache')]);
      check(library.stdout.includes('PASS library mode=0') && library.stdout.includes('PASS library mode=1'), 'shared library constructors and preflight scope');
      if (s) {
        const cancelled = await run('sic-test-decode-remote-decode-library', [fixture, manifest, s.url + '/timeout', path.join(work, 'cancel-cache'), '--cancel']);
        check(cancelled.stdout.includes('PASS cancellation') && s.stats.timeouts > 0, 'in-flight prefetch cancellation is bounded');
        const stopFile = path.join(work, 'stop'), before = s.stats.timeouts, start = performance.now();
        const pending = run('sic-decode', [...net('timeout', 'stop-cache'), path.join(work, 'stop-out'), '--stop-file', stopFile], {ok: false});
        while (s.stats.timeouts === before && performance.now() - start < 2000)
          await new Promise(resolve => setTimeout(resolve, 10));
        await fs.writeFile(stopFile, 'stop\n');
        const stopped = await pending;
        check(stopped.stderr.includes('requested stop') && performance.now() - start < 3000, 'stop file cancels in-flight HTTP');
      }
      const full = path.join(work, 'full');
      await run('sic-decode', [fixture, full, ...config]);
      const fullRows = (await fs.readFile(path.join(full, 'c0.raw.balls'), 'utf8')).trim().split('\n').slice(2, -1);
      for (const [spec, selected] of [['0,8', [0, 8]], ['1-3,7', [1, 2, 3, 7]], ['8,0,8', [0, 8]]]) {
        const output = path.join(work, 'subset-' + checks), local = output + '-local';
        const args = ['--rays', 'c0', '--coordinates', spec, ...config];
        await run('sic-decode', [fixture, local, ...args]);
        const result = await run('sic-decode', [...net('range', 'subset-cache'), output, ...args]);
        const raw = await fs.readFile(path.join(output, 'c0.raw-selected.balls'));
        equal(raw, await fs.readFile(path.join(local, 'c0.raw-selected.balls')), 'local/remote subset bytes');
        equal(raw.toString().trim().split('\n').slice(2, -1), selected.map(m => fullRows[m]), 'subset equals full coordinate values');
        const provenance = JSON.parse(await fs.readFile(path.join(output, 'decode.provenance.json')));
        equal(provenance.coordinate_indices, selected, 'canonical selected-coordinate order');
        const m = metrics(result);
        check(m.peak_rss_mib <= 256, 'aggregate RSS bound');
        check(m.buffer_bound === 2 * 2 * (1024 + 56) + 2 * 8388608, 'bounded transport staging');
        check(m.transferred <= bytes.length + 4096, 'subset transfer bound including coalescing');
      }
      const common = ['--rays', 'c0', ...config];
      const noPrefetch = await run('sic-decode', [...net('range', 'no-prefetch'), path.join(work, 'no-prefetch-out'), '--prefetch-mib', '0', ...common]);
      const prefetched = await run('sic-decode', [...net('range', 'prefetch'), path.join(work, 'prefetch-out'), '--prefetch-mib', '1', ...common]);
      const a = metrics(noPrefetch), b = metrics(prefetched);
      if (s) check(b.requests < a.requests, 'coalescing reduces HTTP requests');
      check(b.prefetched_bytes > 0 && b.coalesced_ranges > 0, 'lookahead and coalescing exercised');
      check(b.transferred <= bytes.length + 4096 && b.peak_rss_mib <= 256, 'full streaming transfer/RSS bound');
      check(prefetched.stdout.includes('global exact canonicality checked'), 'full exact checks completed');
      for (const name of ['c0.raw.balls', 'c0.normalized.balls', 'c0.fp64.txt'])
        equal(await fs.readFile(path.join(work, 'prefetch-out', name)), await fs.readFile(path.join(full, name)), 'streamed full-ray bytes');
      const before = s?.stats.requests;
      await run('sic-decode', [...net('range', 'prefetch'), path.join(work, 'offline-out'), ...common]);
      equal(s?.stats.requests, before, 'warm prefetch cache has zero network requests');
      const cacheFile = (await entries(path.join(work, 'prefetch'))).find(x => x.endsWith('.range'));
      const damaged = await fs.readFile(cacheFile); damaged[damaged.length - 1] ^= 1; await fs.writeFile(cacheFile, damaged);
      await run('sic-decode', [...net('range', 'prefetch'), path.join(work, 'cache-repaired'), ...common]);
      if (s) {
        check(s.stats.requests > before, 'corrupt cache fragment refetched');
        for (const mode of ['wrong-range', 'corrupt-at']) {
          const result = await run('sic-decode', [...net(mode, mode), path.join(work, mode + '-out'), ...common], {ok: false});
          check(/Content-Range|digest|SHA-256/.test(result.stderr), 'prefetch fault rejected');
          await run('sic-decode', [...net('range', mode), path.join(work, mode + '-repaired'), ...common]);
        }
        await run('sic-decode', [...net('disconnect', 'disconnect'), path.join(work, 'disconnect-out'), ...common]);
        check(s.stats.disconnects > 0 && s.stats.peakActive <= 2, 'disconnect recovery and bounded concurrency');
      }
      const threshold = [...net('range', 'threshold'), '--remote-max-fraction', '0.5'];
      // Use fresh arguments because the strict parser rejects duplicate options.
      threshold.splice(threshold.indexOf('--remote-max-fraction'), 2);
      const refused = await run('sic-decode', [...threshold, path.join(work, 'refused'), ...common], {ok: false});
      check(refused.stderr.includes('sic-container fetch'), 'full-ray threshold recommends fetch');
      const fallback = await run('sic-decode', [...threshold, '--fetch-full', path.join(work, 'fallback'), ...common]);
      check(fallback.stdout.includes('full_download=1'), 'explicit full fallback works');
      const tooSmall = await run('sic-decode', [...net('range', 'too-small'), path.join(work, 'too-small-out'), '--memory-mib', '1'], {ok: false});
      check(tooSmall.stderr.includes('memory budget'), 'source headroom admitted before decoding');
      for (const spec of ['', '0,', '0,,1', '3-1', '16', '-1', '1-2-3']) {
        const invalid = await run('sic-decode', [fixture, path.join(work, 'invalid'), '--coordinates', spec], {ok: false});
        equal(invalid.code, 2, 'invalid coordinate selection rejected');
      }
      const help = await run('sic-decode', ['--help']);
      check(help.stdout.includes('--prefetch-mib') && help.stdout.includes('--coalesce-gap') && help.stdout.includes('A,...'), 'documented help');
    } finally {if (s) await s.close();}
  } else if (which === 'decode') {
    const cases = ['d2', 'd4', 'd8', 'd8-maximal', 'd16', 'd32', 'd64'];
    for (const name of cases) {
      let fixture = path.join(fixtures, name + '.sicx');
      let extra = [];
      if (name === 'd64') {
        fixture = path.join(work, 'd64.sicx');
        await run('sic-construct', ['6', '--out', fixture, '--order', 'standard', '--threads', '2', '--memory-mib', '4000']);
        extra = ['--ray', 'c0', '0,0,0,0,0', '0'];
      }
      const p = await split(fixture, name, 16384);
      const s = httpEnabled ? await startServer(p.dir) : null;
      try {
        const local = path.join(work, name + '-local'), net = path.join(work, name + '-remote');
        const args = ['--threads', '2', '--no-pin', ...extra];
        await run('sic-decode', [fixture, local, ...args]);
        const cache = path.join(work, name + '-cache');
        const result = await run('sic-decode', [...remote(p.manifest, s ? s.url + '/redirect' : p.dir, cache), net, ...args]);
        check(/PASS|DECODED/.test(result.stdout), `${name} decode passes`);
        for (const entry of await fs.readdir(local)) {
          if (entry === 'decode.provenance.json') continue; // records the input filename
          equal(await fs.readFile(path.join(net, entry)), await fs.readFile(path.join(local, entry)), `${name}/${entry} identical`);
        }
        const count = s?.stats.requests;
        await run('sic-decode', [...remote(p.manifest, s ? s.url + '/redirect' : p.dir, cache), net + '-again', ...args]);
        equal(s?.stats.requests, count, `${name} cached decode sends no requests`);
        if (s) equal(s.stats.missingRange, 0, 'redirect preserves Range');
      } finally {if (s) await s.close();}
    }
  } else throw Error(`unknown case ${which}`);
  console.log(`PASS ${which}: ${checks} checks; HTTP=${httpEnabled}; server=${JSON.stringify(server ? {...server.stats, ranges: server.stats.ranges.length} : {})}`);
} finally {if (server) await server.close();}
