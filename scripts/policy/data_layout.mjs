// SPDX-License-Identifier: AGPL-3.0-or-later
// Public data roles, naming and catalogue closure. No Git inventory: include every file.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';

export function checkData(root) {
  const data = path.join(root, 'data');
  const files = [];
  function walk(dir, prefix = '') {
    for (const entry of fs.readdirSync(dir, {withFileTypes: true})) {
      const name = prefix + entry.name;
      assert(!entry.isSymbolicLink(), `data symlink: ${name}`);
      assert(/^[a-z0-9_]+(?:\.[a-z0-9_]+)*$/.test(entry.name), `data name: ${name}`);
      assert(!/(?:^|_)v[0-9]+(?:_|\.|$)|internal|fixture|private/.test(entry.name), `data internal label: ${name}`);
      if (entry.isDirectory()) {
        assert(/^[a-z0-9_]+$/.test(entry.name), `data directory name: ${name}`);
        assert(prefix ? /^d[1-9][0-9]*\/$/.test(prefix) &&
          (/^m[1-9][0-9]*$/.test(entry.name) || name === 'd8/hoggar') : /^d[1-9][0-9]*$/.test(entry.name), `data directory: ${name}`);
        assert(fs.readdirSync(path.join(dir, entry.name)).length, `empty data directory: ${name}`);
        walk(path.join(dir, entry.name), name + '/');
      } else {
        assert(entry.isFile(), `data file type: ${name}`);
        const dimension = name.match(/^d([1-9][0-9]*)\//);
        if (dimension) {
          const d = BigInt(dimension[1]);
          assert(d >= 2n && (d & (d - 1n)) === 0n, `data dimension: ${name}`);
        }
        assert(name === 'catalogue.json' || name === 'sha256sums.txt' || name === 'd2/d2.sicrad' ||
          /^d[1-9][0-9]*\/(?:stabilizers\.txt|all\.npz)$/.test(name) ||
          /^d[1-9][0-9]*\/m[1-9][0-9]*\/(?:manifest\.json|multiplet\.sicx\.parts\.json|c(?:0|[1-9][0-9]*)\.(?:json|csv|npy|mat|h5))$/.test(name) ||
          /^d8\/hoggar\/(?:exact\.json|manifest\.json|c0\.(?:json|csv|npy|mat|h5))$/.test(name), `data role: ${name}`);
        files.push(name);
      }
    }
  }
  walk(data);
  const catalogue = JSON.parse(fs.readFileSync(path.join(data, 'catalogue.json')));
  assert.equal(catalogue.schema, 'sic-release-catalogue/1.0');
  assert.equal(typeof catalogue.release.repository, 'string');
  assert.equal(typeof catalogue.release.tag, 'string');
  const referenced = new Set(['catalogue.json']);
  function record(f) {
    assert(files.includes(f.path), `catalogue missing public file: ${f.path}`);
    assert(!referenced.has(f.path), `catalogue duplicate file: ${f.path}`);
    const bytes = fs.readFileSync(path.join(data, f.path));
    assert.equal(f.bytes, String(bytes.length), `catalogue size: ${f.path}`);
    assert.equal(f.sha256, crypto.createHash('sha256').update(bytes).digest('hex'), `catalogue digest: ${f.path}`);
    referenced.add(f.path);
  }
  const dimensions = new Set();
  for (const dim of catalogue.dimensions) {
    assert(/^[1-9][0-9]*$/.test(dim.d) && !dimensions.has(dim.d), 'catalogue dimension id');
    const d = BigInt(dim.d);
    assert(d >= 2n && (d & (d - 1n)) === 0n && fs.statSync(path.join(data, 'd' + dim.d)).isDirectory(), 'catalogue dimension folder');
    dimensions.add(dim.d);
    for (const f of dim.files) { assert(f.path.startsWith(`d${dim.d}/`) && f.path.split('/').length === 2); record(f); }
    const multiplets = new Set();
    for (const m of dim.multiplets) {
      assert(new RegExp(`^d${dim.d}-m[1-9][0-9]*$`).test(m.id) && !multiplets.has(m.id), 'catalogue multiplet id');
      multiplets.add(m.id);
      const dir = m.id.replace('-', '/') + '/';
      assert(Array.isArray(m.smith) && m.smith.every(x => typeof x === 'string'));
      assert(m.labels && typeof m.labels === 'object');
      assert.equal(m.sicx.parts_file, dir + 'multiplet.sicx.parts.json');
      const manifest = JSON.parse(fs.readFileSync(path.join(data, dir, 'manifest.json')));
      const parts = JSON.parse(fs.readFileSync(path.join(data, m.sicx.parts_file)));
      assert.equal(manifest.id, m.id);
      for (const key of ['sha256', 'exact_root', 'merkle_root', 'size_bytes']) assert.equal(m.sicx[key], manifest.sicx[key]);
      assert.equal(m.sicx.sha256, parts.sha256);
      const rays = new Set();
      for (const r of m.rays) {
        assert(/^c(?:0|[1-9][0-9]*)$/.test(r.label) && !rays.has(r.label), 'catalogue ray label');
        rays.add(r.label);
        assert.equal(r.id, m.id + '-' + r.label);
        assert(Array.isArray(r.exponents) && typeof r.accuracy_bits === 'string' && typeof r.sicx_ray_label === 'string');
        for (const f of r.files) { assert(f.path.startsWith(dir + r.label + '.')); record(f); }
        assert(r.files.some(f => f.path === dir + r.label + '.csv'), 'searchable release ray needs CSV');
      }
      for (const f of m.files) { assert(f.path.startsWith(dir)); record(f); }
    }
  }
  assert.deepEqual([...dimensions].sort(), fs.readdirSync(data, {withFileTypes: true}).filter(e => e.isDirectory()).map(e => e.name.slice(1)).sort(), 'catalogue dimension coverage');
  for (const e of catalogue.exceptional || []) {
    assert.equal(e.id, 'd8-hoggar-c0');
    assert.equal(e.manifest, 'd8/hoggar/manifest.json');
    assert.equal(e.wh_covariant, 'false');
    assert.equal(e.counted_in_ray_total, 'false');
    for (const f of e.files) { assert(f.path.startsWith('d8/hoggar/')); record(f); }
  }
  if (files.includes('sha256sums.txt')) {
    // This sidecar hashes the catalogue; including it in the catalogue would be circular.
    const checksums = new Set();
    for (const row of fs.readFileSync(path.join(data, 'sha256sums.txt'), 'utf8').trimEnd().split('\n')) {
      const match = row.match(/^([0-9a-f]{64})  (.+)$/);
      assert(match && files.includes(match[2]) && match[2] !== 'sha256sums.txt' && !checksums.has(match[2]), 'checksum entry');
      assert.equal(match[1], crypto.createHash('sha256').update(fs.readFileSync(path.join(data, match[2]))).digest('hex'), 'checksum digest');
      checksums.add(match[2]);
    }
    assert.equal(checksums.size, files.length - 1, 'checksum coverage');
    referenced.add('sha256sums.txt');
  }
  for (const f of files) assert(referenced.has(f), `unreferenced data: ${f}`);
  // The single install declaration covers catalogue plus its checked, closed tree.
  const install = fs.readFileSync(path.join(root, 'cpp/cmake/package/InstallPackage.cmake'), 'utf8');
  assert(install.includes('install(DIRECTORY "${SIC_REPO_ROOT}/data/" DESTINATION "${CMAKE_INSTALL_DATADIR}/sic/data")'), 'data install rule missing');
  return files.length;
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  try { console.log(`PASS data layout: ${checkData(path.resolve(process.argv[2]))} referenced release files`); }
  catch (error) { console.error(error.message); process.exitCode = 1; }
}
