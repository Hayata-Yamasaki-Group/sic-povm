// SPDX-License-Identifier: AGPL-3.0-or-later
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';

const [mode, fresh, archive, standardD8Text] = process.argv.slice(2);
const read = path => JSON.parse(readFileSync(path, 'utf8'));
const readLines = path => readFileSync(path, 'utf8').trimEnd().split('\n').map(line => JSON.parse(line));
if (mode === 'text') {
  const current = readLines(fresh), reference = readLines(archive);
  const exact = lines => lines.filter(line => ['EXMT', 'TOWR', 'BODY'].includes(line.chunk));
  assert.deepEqual(exact(current), exact(reference), 'canonical exact records');
  assert.equal(current.at(-1).exact_root, reference.at(-1).exact_root, 'canonical exact root');
} else if (mode === 'ray') {
  const current = read(fresh), reference = read(archive);
  assert.equal(current.id, reference.id);
  assert.equal(current.d, reference.d);
  assert.equal(current.schema, reference.schema);
  const vector = ray => ray.fp64.decimal.map(pair => pair.map(Number));
  const a = vector(current), b = vector(reference);
  assert.equal(a.length, b.length);
  // Compare the complex ray in either parity representative. Re-anchor by
  // the largest component of the oracle; differing gauges have one phase.
  const k = b.reduce((best, v, i) => Math.hypot(...v) > Math.hypot(...b[best]) ? i : best, 0);
  function error(orientation) {
    const coordinate = i => a[(a.length + orientation * i) % a.length];
    const p = coordinate(k), q = b[k];
    const norm = Math.hypot(...p) * Math.hypot(...q);
    if (norm === 0) return Infinity;
    const re = (p[0]*q[0] + p[1]*q[1]) / norm;
    const im = (p[0]*q[1] - p[1]*q[0]) / norm;
    return Math.max(...b.map(([x,y], i) => {
      const [u,v] = coordinate(i);
      return Math.hypot(re*u-im*v-x, im*u+re*v-y);
    }));
  }
  assert(Math.min(error(1), error(-1)) <= 4e-15, 'reference binary64 ray in its projective gauge');
} else if (mode === 'catalogue') {
  function topology(object) {
    return {schema:object.schema, release:object.release, dimensions:object.dimensions.map(d => ({
      d:d.d, multiplets:d.multiplets.map(m => ({
        id:m.id, exact_root:m.sicx.exact_root, parts_file:m.sicx.parts_file,
        rays:m.rays.map(r => [r.id,r.label]),
      })),
    }))};
  }
  const reference = topology(read(archive));
  // Both presentations describe the same catalogue entry. The native
  // standard-order presentation has its own independently pinned exact root.
  const standard = reference.dimensions.find(d => d.d === '8').multiplets.find(m => m.id === 'd8-m2');
  standard.exact_root = readLines(standardD8Text).at(-1).exact_root;
  assert.deepEqual(topology(read(fresh)), reference, 'reference catalogue identities and exact roots');
} else {
  throw Error('unknown reference oracle kind: ' + mode);
}
