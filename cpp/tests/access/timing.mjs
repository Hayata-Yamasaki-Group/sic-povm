// SPDX-License-Identifier: AGPL-3.0-or-later
// Serial, alternating local decoder wall-time gate against the supplied base ELF.
// Usage: taskset -c CPUS node timing.mjs BASE NEW INPUT_DIR WORK
import fs from 'node:fs/promises';
import path from 'node:path';
import {spawn} from 'node:child_process';
import {createHash} from 'node:crypto';
import assert from 'node:assert/strict';
const [base, candidate, inputs, work] = process.argv.slice(2);
await fs.mkdir(work, {recursive: true});
const threads = Number(process.env.SIC_TEST_ACCESS_THREADS || 8), repeats = Number(process.env.SIC_TEST_ACCESS_TIMING_REPEATS || 9);
const median = a => [...a].sort((a, b) => a - b)[Math.floor(a.length / 2)];
const records = [];
for (const d of [256, 512, 1024]) {
  const row = {dimension: d, threads, target_bits: 128, memory_mib: 8192, seed: 'not applicable', runs: []};
  const expected = {};
  for (let i = -1; i < repeats; ++i) {
    for (const variant of (i % 2 ? ['candidate', 'base'] : ['base', 'candidate'])) {
      const name = `d${d}-${i < 0 ? 'warmup' : i}-${variant}`, outdir = path.join(work, name);
      const out = [], err = [];
      const code = await new Promise((resolve, reject) => {
        const p = spawn(variant === 'base' ? base : candidate, [path.join(inputs, `d${d}.sicx`), outdir,
          '--rays', 'c0', '--threads', String(threads), '--no-pin', '--memory-mib', '8192', '--check-budget'],
          {env: {...process.env, SIC_THREADS: String(threads)}});
        p.stdout.on('data', b => out.push(b)); p.stderr.on('data', b => err.push(b));
        p.on('error', reject); p.on('close', resolve);
      });
      const stdout = Buffer.concat(out).toString(), stderr = Buffer.concat(err).toString();
      await fs.writeFile(path.join(work, name + '.stdout'), stdout);
      await fs.writeFile(path.join(work, name + '.stderr'), stderr);
      assert.equal(code, 0, stderr);
      for (const suffix of ['raw.balls', 'normalized.balls', 'fp64.txt']) {
        const bytes = await fs.readFile(path.join(outdir, 'c0.' + suffix));
        const sha = createHash('sha256').update(bytes).digest('hex');
        expected[suffix] ??= sha; assert.equal(sha, expected[suffix], 'local output changed');
      }
      const wall = Number(stdout.match(/TIME wall=([\d.]+)/)?.[1]); assert(wall > 0);
      if (i >= 0) row.runs.push({iteration: i, variant, decoder_wall: wall});
      await fs.rm(outdir, {recursive: true});
      await fs.writeFile(path.join(work, `d${d}.json`), JSON.stringify(row, null, 2) + '\n');
    }
  }
  row.base_median = median(row.runs.filter(r => r.variant === 'base').map(r => r.decoder_wall));
  row.candidate_median = median(row.runs.filter(r => r.variant === 'candidate').map(r => r.decoder_wall));
  row.change_percent = 100 * (row.candidate_median / row.base_median - 1);
  row.within_three_percent = row.candidate_median <= row.base_median * 1.03;
  row.byte_identical = true; row.output_sha256 = expected;
  records.push(row);
  await fs.writeFile(path.join(work, `d${d}.json`), JSON.stringify(row, null, 2) + '\n');
  await fs.writeFile(path.join(work, 'measurement.json'), JSON.stringify(records, null, 2) + '\n');
  console.log(`d${d} base=${row.base_median} candidate=${row.candidate_median} change=${row.change_percent.toFixed(3)}% ${row.within_three_percent ? 'PASS' : 'FAIL'}`);
}
assert(records.every(r => r.within_three_percent), 'local decoder exceeds +3% median wall-time gate');
