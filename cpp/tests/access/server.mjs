// SPDX-License-Identifier: AGPL-3.0-or-later
// Offline HTTP Range fixture server. Faults are selected by the first URL component.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

export async function startServer(directory, {rangeDirectory, corruptAt} = {}) {
  const stats = {requests: 0, bytes: 0, active: 0, peakActive: 0, noRangeBytes: 0,
    ranges: [], disconnects: 0, retries: 0, timeouts: 0, missingRange: 0};
  const seen = new Set();
  const server = http.createServer((req, res) => {
    const [mode, asset] = new URL(req.url, 'http://localhost').pathname.slice(1).split('/');
    if (mode === 'stats') {res.end(JSON.stringify(stats)); return;}
    ++stats.requests; ++stats.active; stats.peakActive = Math.max(stats.peakActive, stats.active);
    res.on('close', () => --stats.active);
    const range = req.headers.range;
    const file = path.join(mode === 'switch' && range && rangeDirectory ? rangeDirectory : directory, asset || '');
    if (!asset || path.basename(asset) !== asset || !fs.existsSync(file)) {res.writeHead(404).end(); return;}
    const size = fs.statSync(file).size;
    if (mode === 'redirect') {res.writeHead(302, {Location: `/range/${asset}`}).end(); return;}
    if ((mode === 'no-range' || mode === 'terminal-redirect') && range) {
      res.writeHead(mode === 'terminal-redirect' ? 302 : 200, {'Content-Length': size}); res.flushHeaders();
      // Keep the body behind a timer to check that the client aborts at headers.
      const timer = setTimeout(() => {if (!res.destroyed) {stats.noRangeBytes += size; res.end(fs.readFileSync(file));}}, 100);
      res.on('close', () => clearTimeout(timer)); return;
    }
    if (mode === 'retry' && !seen.has(mode + asset)) {seen.add(mode + asset); ++stats.retries; res.writeHead(503, {'Content-Length': '4'}).end('busy'); return;}
    if (mode === 'timeout' || (mode === 'slow' && !seen.has(mode + asset))) {
      seen.add(mode + asset); ++stats.timeouts;
      const timer = setTimeout(() => {if (!res.destroyed) res.writeHead(503).end();}, 1500);
      res.on('close', () => clearTimeout(timer)); return;
    }
    let start = 0, end = size - 1;
    if (range) {
      const match = /^bytes=(\d+)-(\d+)$/.exec(range);
      if (!match) {res.writeHead(416).end(); return;}
      start = Number(match[1]); end = Number(match[2]);
      if (start > end || end >= size) {res.writeHead(416).end(); return;}
      stats.ranges.push({mode, asset, start, end});
    } else ++stats.missingRange;
    const headers = {'Content-Length': end - start + 1};
    if (range) headers['Content-Range'] = `bytes ${start + (mode === 'wrong-range' ? 1 : 0)}-${end}/${size + (mode === 'wrong-size' ? 1 : 0)}`;
    if (mode === 'missing-range') delete headers['Content-Range'];
    const send = () => {
      if (res.destroyed) return;
      res.writeHead(range ? 206 : 200, headers);
      if (mode === 'disconnect' && range && end > start && !seen.has(mode + asset)) {
        seen.add(mode + asset); ++stats.disconnects;
        const n = Math.max(1, Math.floor((end - start + 1) / 2));
        const b = fs.readFileSync(file).subarray(start, start + n);
        stats.bytes += b.length; res.write(b);
        setTimeout(() => res.destroy(), 10); return;
      }
      if (mode === 'corrupt' || (mode === 'corrupt-payload' && start <= 50 && end >= 50) ||
          (mode === 'corrupt-at' && start <= corruptAt && end >= corruptAt)) {
        const b = fs.readFileSync(file).subarray(start, end + 1);
        b[mode === 'corrupt-at' ? corruptAt - start : mode === 'corrupt-payload' ? 50 - start : 0] ^= 1; stats.bytes += b.length; res.end(b); return;
      }
      const stream = fs.createReadStream(file, {start, end, highWaterMark: 65536});
      stream.on('data', b => {stats.bytes += b.length;});
      stream.on('error', () => res.destroy());
      res.on('close', () => stream.destroy());
      stream.pipe(res);
    };
    // Small delays make the parallel bound observable without bandwidth noise.
    setTimeout(send, mode === 'parallel' ? 15 : 0);
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  return {url: `http://127.0.0.1:${server.address().port}`, stats,
    close: () => new Promise(resolve => {server.closeAllConnections(); server.close(resolve);})};
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const s = await startServer(path.resolve(process.argv[2]));
  console.log(s.url);
  const stop = async () => {await s.close(); console.error(JSON.stringify(s.stats)); process.exit(0);};
  process.on('SIGTERM', stop); process.on('SIGINT', stop);
}
