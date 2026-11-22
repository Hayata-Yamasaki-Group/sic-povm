// SPDX-License-Identifier: AGPL-3.0-or-later
// Keep Newton class-team selection in its policy owner.
import { readFileSync, readdirSync } from 'node:fs';
import path from 'node:path';

const source = readFileSync(process.argv[2], 'utf8');
function violations(text) {
  return ['process_cpu_allowance', 'affinity_physical_cores']
    .filter(name => new RegExp(`\\b${name}\\s*\\(`).test(text));
}
for (const name of ['process_cpu_allowance', 'affinity_physical_cores']) {
  if (!violations(`${source}\n${name}();`).includes(name))
    throw new Error(`negative control failed for ${name}`);
}
const forbidden = violations(source);
if (forbidden.length) {
  console.error(`Newton worker policy: forbidden direct call: ${forbidden.join(', ')}`);
  process.exit(1);
}
if (!/\bnewton_class_workers\s*\(/.test(source)) {
  console.error('Newton worker policy: missing newton_class_workers call');
  process.exit(1);
}
console.log('Newton worker policy and negative controls PASS');

// Allowance calculation belongs to resources. Affinity mask capture for binding
// and the decoder's documented placement-core bound are separate operations.
const root = path.resolve(path.dirname(process.argv[2]), '../../../..');
const sourceRoots = ['src', 'apps', 'include', 'checkers', 'bench', 'examples'];
function rediscovery(text, file) {
  let scrubbed = text;
  if (file === 'construct/model/worker_policy.cpp')
    scrubbed = scrubbed.replace('requested_threads(nullptr)', 'default_workers');
  if (file === 'construct/model/options.cpp')
    scrubbed = scrubbed.replace('requested_threads(raw("SIC_THREADS"))', 'configured_workers');
  if (file === 'support/runtime/worker_team.cpp')
    scrubbed = scrubbed.replace('sched_getaffinity(0, sizeof mask, &mask)', 'true');
  if (file === 'decode/pool.cpp')
    scrubbed = scrubbed.replace('sched_getaffinity(0, sizeof owner_mask_, &owner_mask_)', 'true');
  if (file === 'construct/runtime/resources/resource_report.cpp')
    scrubbed = scrubbed.replace('budget.initial.cpus', 'reported_cpu_count');
  if (file === 'decode/placement.cpp') {
    scrubbed = scrubbed.replaceAll(/\b(?:p|place)\.cpus\b/g, 'placement_mask');
    scrubbed = scrubbed.replace('sched_getaffinity(0, sizeof set, &set)', 'false');
    scrubbed = scrubbed.replace('CPU_ISSET(c, &set)', 'true');
    scrubbed = scrubbed.replaceAll(/physical_cpu_workers\s*\([^)]*\)/g, 'placement_cores');
  }
  return /\brequested_threads\s*\(/.test(scrubbed) ||
    /(?:\.|->)\s*cpus\b/.test(scrubbed) || /\b(?:process_cpu_allowance|affinity_physical_cores|physical_cpu_workers|platform_cpu_counts|non_affinity_cpu_counts|cpu_count|hardware_concurrency|sched_getaffinity|pthread_getaffinity_np|CPU_COUNT_S|CPU_COUNT|CPU_ISSET|get_nprocs|get_nprocs_conf)\s*\(/.test(scrubbed) ||
    /\b(?:_SC_NPROCESSORS_ONLN|_SC_NPROCESSORS_CONF|SLURM_CPUS_PER_TASK)\b|cpu\.max|cpuset\.cpus|\/proc\/cpuinfo/.test(scrubbed);
}
function* sources(dir) {
  for (const entry of readdirSync(dir, {withFileTypes: true})) {
    const absolute = path.join(dir, entry.name);
    if (entry.isDirectory()) yield* sources(absolute);
    else if (/\.(?:cpp|hpp|tpp|c|h|cc|cxx)$/.test(entry.name)) yield absolute;
  }
}
for (const token of ['process_cpu_allowance()', 'affinity_physical_cores()',
                     'hardware_concurrency()', 'sched_getaffinity(0, 0, 0)', 'CPU_COUNT(&mask)',
                     'pthread_getaffinity_np(pthread_self(), 0, 0)', 'CPU_COUNT_S(n, mask)',
                     '"/proc/cpuinfo"', 'requested_threads(settings)', 'budget.initial.cpus', 'inspect().cpus', 'get_nprocs()',
                     'sysconf(_SC_NPROCESSORS_ONLN)', 'SLURM_CPUS_PER_TASK', 'requested_threads(nullptr)']) {
  if (!rediscovery(token, 'construct/stages/initialize.cpp'))
    throw new Error(`CPU ownership negative control failed: ${token}`);
}
for (const sourceRoot of sourceRoots) for (const file of sources(path.join(root, sourceRoot))) {
  const relative = path.relative(path.join(root, sourceRoot), file).split(path.sep).join('/');
  if (sourceRoot === 'src' && relative.startsWith('support/resources/')) continue;
  const text = readFileSync(file, 'utf8');
  const policyPath = sourceRoot === 'src' ? relative : `${sourceRoot}/${relative}`;
  if (rediscovery(text, policyPath)) throw new Error(`CPU allowance rediscovered outside owner: ${relative}`);
  for (const call of ['process_cpu_allowance()', 'requested_threads(nullptr)'])
    if (!rediscovery(`${text}\n${call};`, policyPath))
      throw new Error(`CPU ownership negative control failed: ${relative}: ${call}`);
}
console.log('CPU ownership across sources and negative controls PASS');

console.log('SIC_NEGATIVE_CONTROL:newton-workers');
