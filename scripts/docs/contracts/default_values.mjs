// SPDX-License-Identifier: AGPL-3.0-or-later
// Semantic comparisons contain selectors and unit conversions, never copied values.
import assert from 'node:assert/strict';
import {root,read} from '../runtime_checks.mjs';
import path from 'node:path';
export function numeric(text) {
  if(text==='one')return 1;
  if(text==='one million')return 1000000;
  const normalized=text.replaceAll(',','').replaceAll('^','**').replace(/\b(?:uint64_t|slong)\s*[({]([^)}]*)[)}]/g,'($1)').replace(/(\d)(?:ULL|ull|ul|u|U|LL|l|L)\b/g,'$1').trim();
  assert(/^[\d\s.eE+*/()<^-]+$/.test(normalized),`defaults: unsupported numeric expression ${text}`);
  // C++ shifts need unbounded arithmetic rather than JavaScript's int32 shift.
  const parts=normalized.split('<<');
  const evaluate=s=>Function('"use strict";return ('+s+')')();
  const value=parts.slice(1).reduce((a,b)=>a*2**evaluate(b),evaluate(parts[0]));
  assert(Number.isFinite(value),'defaults: nonfinite value');return value;
}
// Claim locators are independent of the generated binding text.
const bindings=[];
function bind(file,starts,doc,source,code,docScale=1,sourceScale=1) {
  bindings.push({file,starts,doc,source,code,docScale,sourceScale});
}
const data='docs/data.md',library='docs/library.md',verification='docs/verification.md';
const remote='cpp/include/sic/access/remote.hpp',container='cpp/apps/container/command_line.cpp';
bind(data,'For each multiplet','default (\\d+)\\)', 'cpp/cmake/components/Data.cmake','SIC_DATA_JOBS "(\\d+)"');
bind(data,'For each multiplet','default (\\d+)\\)\\. Disk','cpp/cmake/components/Data.cmake','SIC_DATA_EXACT_JSON_FACTOR "(\\d+)"');
bind(data,'All tools support','--threads (\\d+)',container,'kDefaultTransferThreads = ([^;]+)');
for(const [doc,code]of [['default (\\d+), range','jobs = ([^,]+)'],['--remote-retries (\\d+)','retries = ([^;]+)'],['defaults to (\\d+) per','timeout_ms = ([^,]+)'],['timeout is (\\d+) ms','connect_timeout_ms = ([^;]+)'],['default (\\d+), maximum','max_ranges = ([^;]+)']])bind(data,'At most `--range-jobs`',doc,remote,code);
bind(data,'`--prefetch-mib`','default (\\d+),',remote,'prefetch_bytes = ([^;]+)',2**20);
bind(data,'`--prefetch-mib`','bytes \\(default (\\d+)',remote,'coalesce_gap = ([^;]+)');
bind(data,'The default transfer','\\*\\*(\\d+)%',remote,'max_fraction = ([^;]+)',0.01);
bind(data,'The default transfer','--remote-max-fraction ([0-9.]+)',remote,'max_fraction = ([^;]+)');
for(const [doc,name,scale]of [['defaults of (\\d+) working','working_bits',1],['(\\d+) absolute radius','minimum_absolute_accuracy_bits',1],['maximum (\\d+) working','maximum_working_bits',1],['maximum dimension (\\d+)','maximum_dimension',1],['(\\d+) Mi total','maximum_input_mantissa_bits',2**20],['(\\d+) MiB admitted','maximum_workspace_bytes',2**20],['([0-9,]+) work score','maximum_work_score',1]])bind(library,'`BallVerificationLimits`',doc,'cpp/include/sic/bounds/overlap_bounds.hpp',name+' = ([^;]+)',scale);
bind('docs/reproduce.md','The selector’s complete','defaults to (\\d+) MiB','cpp/src/construct/model/alignment_options.hpp','memory_bytes = ([^;]+)',2**20);
for(const [doc,name]of [['`(2\\^\\d+)` bytes','kMaxIntegerBytes'],['at most (\\d+) MiB','kMaxTextChunk'],['physical chunk at most\\s+(\\d+) MiB','kMaxBlockBytes'],['`(2\\^\\d+)` entries','kMaxChunks']])bind('docs/sicx.md','Implementation resource',doc,'cpp/include/sic/sicx/format.hpp',name+' = ([^;]+)',doc.includes('MiB')?2**20:1);
bind('docs/sicx.md','Implementation resource','nesting at most (\\d+)','cpp/include/sic/support/json.hpp','kDefaultJsonDepth = ([^;]+)');
bind('docs/sicx.md','Implementation resource','at most (\\d+) digits','cpp/include/sic/support/json.hpp','kCanonicalDecimalDigits = ([^;]+)');
bind('docs/tools.md','```text\nusage: sic-certify-norm',String.raw`--threads T\s+worker threads \(default (\d+)\)`,'cpp/checkers/norm/src/cli/command_line.cpp','kDefaultThreads = ([^;]+)');
bind('docs/tools.md','```text\nusage: sic-certify-norm',String.raw`--max-bits B\s+precision cap \(default (\d+)\)`,'cpp/checkers/norm/src/cli/command_line.cpp','kDefaultMaxBits = ([^;]+)');
for(const [doc,name]of [['d <= (\\d+)','kDefaultMaxDimension'],['D <= (\\d+)','kDefaultMaxFieldDimension'],['([0-9,]+) total dense','max_dense_coordinates']])bind(verification,'Default input limits',doc,'cpp/checkers/norm/src/model/input_model.hpp',name+' = ([^;]+)');
bind(verification,'The default algebraic','tower degree (\\d+)','cpp/checkers/verifier/src/model/input_model.hpp','branch_degree_limit = (\\d+)');

bind(data,'At most `--range-jobs`',String.raw`range size is (\d+) MiB`,remote,'range_bytes = ([^;]+)',2**20);
bind(library,'Default library producer threads',String.raw`default\s+at (\d+)`,'cpp/apps/decode/command_line.cpp',String.raw`kDefaultWorkers\{([^}]+)\}`);
bind(library,'On glibc,',String.raw`trim threshold to (\d+) MiB`,'cpp/apps/construct/main.cpp','kHeapTrimThresholdBytes = ([^;]+)',2**20);
bind(library,'On glibc,',String.raw`mmap threshold to (\d+) MiB`,'cpp/apps/construct/main.cpp','kHeapMmapThresholdBytes = ([^;]+)',2**20);
const norm='cpp/checkers/norm/src/';
for(const [doc,source,code,scale]of [
 [String.raw`(\d+) MiB per directory`,norm+'model/input_model.hpp','max_directory_bytes = ([^;]+)',2**20],
 [String.raw`(\d+) MiB per physical`,norm+'format/container.cpp',String.raw`c.length <= \(json_type\(c.type\) \? [^:]+: ([^)]+)`,2**20],
 [String.raw`(\d+) MiB per physical`,norm+'format/container.cpp',String.raw`resource\(length <= \(([^)]+)\)`,2**20],
 ['(one) MiB per integer',norm+'format/integer_codec.cpp',String.raw`n <= \(([^)]+)\)`,2**20],
 [String.raw`JSON depth (\d+)`,norm+'format/canonical_json.cpp',String.raw`depth <= (\d+)`,1],
 ['and ([0-9,]+) JSON nodes',norm+'format/canonical_json.cpp',String.raw`nodes <= \(([^)]+)\)`,1],
 [String.raw`use degree (\d+)`,norm+'model/input_model.hpp','degree = ([^,]+)',1],
 ['height `(2\\^\\d+)`',norm+'model/input_model.hpp','height_bits = ([^;]+)',1],
])bind(verification,'Default input limits',doc,source,code,scale);
const verifier='cpp/checkers/verifier/src/';
for(const [doc,source,code,scale]of [
 [String.raw`branch precision and absolute exponents (2\^\d+)`,verifier+'format/metadata.cpp',String.raw`M.branch_bits > \(([^)]+)\)`,1],
 [String.raw`branch precision and absolute exponents (2\^\d+)`,verifier+'format/ball_codec.cpp',String.raw`fmpz_cmp_si\(E, ([^)]+)\)`,1],
 [String.raw`branch strings (\d+) MiB`,verifier+'format/ball_codec.cpp',String.raw`s.size\(\) > \(([^)]+)\)`,2**20],
 [String.raw`BALL admission (\d+) GiB`,verifier+'format/ball_codec.cpp',String.raw`checked_mul\(rays, per_ray\) > \(([^)]+)\)`,2**30],
 [String.raw`weight keys (2\^\d+)`,verifier+'format/container.cpp',String.raw`count >= \(([^)]+)\)`,1],
 [String.raw`text lines (\d+) MiB`,verifier+'text/text_check.hpp',String.raw`limit = ([^;)]+)`,2**20],
])bind(verification,'The default algebraic',doc,source,code,scale);

bind(verification,'Default input limits',String.raw`(\d+) MiB per directory/JSON`,norm+'format/container.cpp',String.raw`json_type\(c.type\) \? ([^:]+):`,2**20);
for(const code of [String.raw`m.branch_bits <= \(([^)]+)\)`,String.raw`fmpz_bits\(part\[0\].v\) <= \(([^)]+)\)`])
  bind(verification,'Default input limits',String.raw`limited to\n`+'`'+String.raw`(2\^\d+)`+'`',norm+'format/metadata.cpp',code);
bind(verification,'The default algebraic',String.raw`support (2\^\d+)`,verifier+'certificate/branches.cpp',String.raw`ep->ncoord > \(([^)]+)\)`);
bind(verification,'The default algebraic',String.raw`height ([0-9,]+)`,verifier+'certificate/branches.cpp',String.raw`ep->max_bits > (\d+)`);

for(const part of [1,3])for(const [comparison,sign]of [['>=','-'],['<=','']])
  bind(verification,'Default input limits',String.raw`limited to\n`+'`'+String.raw`(2\^\d+)`+'`',norm+'format/metadata.cpp',
    String.raw`fmpz_get_si\(part\[`+part+String.raw`\].v\) `+comparison+' '+sign+String.raw`\(([^)]+)\)`);
for(const variable of ['E','e'])for(const sign of ['','-'])
  bind(verification,'The default algebraic',String.raw`branch precision and absolute exponents (2\^\d+)`,verifier+'format/ball_codec.cpp',
    String.raw`fmpz_cmp_si\(`+variable+', '+(sign?String.raw`-\(([^)]+)\)`:String.raw`([^)]+)`)+String.raw`\)`);
function compareWorkerGrid(documents,sources) {
  const doc=documents('docs/reproduce.md').replace(/\s+/g,' ');
  const prose=doc.match(/grid size \((\d+) worker through d(\d+), then d\/(\d+) through d(\d+), (\d+) at d(\d+), and at most (\d+) thereafter/);
  const source=sources('cpp/src/construct/model/worker_policy.cpp');
  const code=source.match(/work_limit = exponent < (\d+) \? std::min\((\d+), (\d+) << \(exponent > (\d+) \? exponent - (\d+) : (\d+)\)\) : (\d+);/);
  assert(prose&&code,'defaults: worker grid selectors changed');
  const p=prose.slice(1).map(Number),c=code.slice(1).map(Number);
  for(let exponent=1;exponent<=12;++exponent) {
    const dimension=2**exponent;
    const claimed=dimension<=p[1]?p[0]:dimension<=p[3]?dimension/p[2]:dimension<=p[5]?p[4]:p[6];
    const actual=exponent<c[0]?Math.min(c[1],c[2]*2**(exponent>c[3]?exponent-c[4]:c[5])):c[6];
    assert.equal(actual,claimed,`defaults: worker grid mismatch at exponent ${exponent}`);
  }
}
function compareConfiguration(documents,sources) {
  const rows=documents('docs/configuration.md').split('\n').filter(l=>/^\| CMake \|/.test(l)).map(l=>l.split('|').slice(1,-1).map(x=>x.trim()));
  for(const [,quoted,,documented,,owner]of rows) {
    const name=quoted.replaceAll('`',''),file=owner.replaceAll('`',''),source=sources(file);
    const option=source.match(new RegExp('(?:option|cmake_dependent_option)\\(\\s*'+name+'\\s+"[^"\\n]*"\\s+([^ )\\n]+)'));
    const setting=source.match(new RegExp('set\\(\\s*'+name+'\\s+"([^"\\n]*)"\\s+CACHE'));
    assert(option||setting,`defaults: missing configuration input ${name}`);
    const actual=option?option[1]:setting[1];
    let expected=documented;
    if(documented.startsWith('empty;')||['system search','manifest release','configured source identity'].includes(documented))expected='';
    if(documented==='source data directory')expected='${PROJECT_SOURCE_DIR}/../data';
    if(documented==='ON when libcurl is found')expected='${CURL_FOUND}';
    assert.equal(actual,expected,`defaults: configuration value mismatch ${name}`);
  }
  // Numeric producer settings whose fallback is directly declared in an accessor.
  const table=documents('docs/configuration.md'),source=sources('cpp/src/construct/model/options.cpp');
  for(const name of ['SIC_GENERATOR_BLOCK','SIC_FIRST_PASS_BITS']) {
    const row=table.split('\n').find(l=>l.startsWith('| environment | `'+name+'` |'));
    const doc=row.split('|')[4].trim().match(/^([0-9.]+)/);
    const code=source.match(new RegExp('unsigned_or\\("'+name+'", ([0-9.]+)\\)'));
    assert(doc&&code,`defaults: configuration fallback selector ${name}`);
    assert.equal(numeric(doc[1]),numeric(code[1]),`defaults: environment fallback mismatch ${name}`);
  }
  const seedRow=table.split('\n').find(l=>l.startsWith('| environment | `SIC_SEED_A` |'));
  const seedDoc=seedRow?.split('|')[4].trim().match(/^([0-9.]+)$/);
  const seedCode=source.match(/const char\* value = raw\("SIC_SEED_A"\);\s*return value \? sic::support::parse_number<double>\(value, "SIC_SEED_A",\s*sic::ErrorKind::invalid_argument\)\s*: ([0-9.]+);/);
  assert(seedDoc&&seedCode, 'defaults: seed fallback selector');
  assert.equal(numeric(seedDoc[1]),numeric(seedCode[1]), 'defaults: seed fallback mismatch');
  const ignored=source.match(/bool ignored_hook\(std::string_view key\) \{([\s\S]*?)\n\}/);
  assert(ignored, 'defaults: hook scope selector');
  const ignoredNames=[...ignored[1].matchAll(/key == "([A-Z0-9_]+)"/g)].map(m=>m[1]);
  for(const name of ignoredNames) {
    const row=table.split('\n').find(l=>l.startsWith('| environment | `'+name+'` |'));
    assert(row && row.split('|')[5].trim()==='hook build only', 'defaults: hook scope '+name);
  }
  const hookRows=table.split('\n').filter(l=>l.startsWith('| environment |') && l.split('|')[5].trim()==='hook build only').map(l=>l.split('|')[2].trim().replaceAll('`',''));
  // Inspect and decoder hooks have their own compile-time guards outside ProducerOptions.
  assert(/#ifdef SIC_ENABLE_TEST_HOOKS\s+if \(std::getenv\("SIC_TEST_INSPECT_EXACT_RENAME_FAIL"\)\)[\s\S]*?#endif/.test(sources('cpp/apps/inspect/command.cpp')), 'defaults: inspect hook guard');
  const decoderHooks=['cpp/apps/decode/command.cpp', 'cpp/src/decode/scheduling.cpp']
    .flatMap(file=>[...sources(file).matchAll(/#ifdef SIC_ENABLE_TEST_HOOKS([^#]+)#endif/g)])
    .flatMap(block=>[...block[1].matchAll(/env_u64\("([A-Z0-9_]+)"\)/g)].map(match=>match[1]));
  assert.deepEqual(hookRows.sort(), [...ignoredNames, 'SIC_TEST_INSPECT_EXACT_RENAME_FAIL', ...decoderHooks].sort(),
    'defaults: hook scope enrollment');
  // Optional storage booleans are interpreted by the stage consuming them.
  // Bind the unset fallback as well as the exact nonzero reader, on both sides.
  const storage = [
    ['SIC_PHASED_STORAGE','phase','cpp/src/construct/stages/initialize.cpp','phase_planned = producer_options.phase.value_or(false);','disabled'],
    ['SIC_COMPACT_WORK','compact_work','cpp/src/construct/stages/initialize.cpp','compact_work = producer_options.compact_work.value_or(phase_planned);','follows phased storage; disabled'],
    ['SIC_STREAM_ACTIONS','stream_actions','cpp/src/construct/stages/initialize.cpp','stream_actions = producer_options.stream_actions.value_or(phase_planned);','follows phased storage; disabled'],
    ['SIC_STREAM_REPS','stream_reps','cpp/src/construct/stages/initialize.cpp','stream_reps = producer_options.stream_reps.value_or(phase_planned);','follows phased storage; disabled'],
    ['SIC_STREAM_STEPS','stream_steps','cpp/src/construct/stages/initialize.cpp','stream_steps = producer_options.stream_steps.value_or(stream_actions);','follows action streaming; disabled'],
    ['SIC_STREAM_TABLES','stream_tables','cpp/src/construct/stages/step_data.cpp','phased_streamed_table = producer_options.stream_tables.value_or(true);','enabled on the phased path'],
    ['SIC_ROOT_BACKING','root_backing','cpp/src/construct/stages/table/newton_table.cpp','const bool root_backing = stream && producer_options.root_backing.value_or(true);','enabled when tables stream'],
    ['SIC_GROUP_RESIDENT_ENTRIES','group_resident_entries','cpp/src/construct/stages/entries/representative_channels.cpp','(check_groups || producer_options.group_resident_entries.value_or(field.degree >= 1048576));','direct path: field degree >= 1048576, or entry-group cross-checks'],
  ];
  for (const [name, member, owner, fallback, expected] of storage) {
    const row = table.split('\n').find(line => line.startsWith('| environment | `'+name+'` |'));
    assert(row, 'defaults: missing storage row '+name);
    assert.equal(row.split('|')[4].trim(), expected, 'defaults: storage fallback mismatch '+name);
    assert(source.includes(member+'(nonzero("'+name+'"))'), 'defaults: storage reader changed '+name);
    assert(sources(owner).includes(fallback), 'defaults: storage consumer changed '+name);
  }
  return rows.length + storage.length;
}
export function compareDefaultValues(documents=p=>read(path.join(root,p)),sources=documents) {
  compareWorkerGrid(documents,sources);
  const configurationCount=compareConfiguration(documents,sources);
  for(const b of bindings) {
    const claim=documents(b.file).split(/\n\s*\n/).find(p=>p.startsWith(b.starts));
    assert(claim,`defaults: missing value claim ${b.starts}`);
    const doc=claim.replace(/\s+/g,' ').match(new RegExp(b.doc.replace(/\\n/g,'\\s+'))),
      code=sources(b.source).match(new RegExp('\\b'+b.code));
    assert(doc&&code,`defaults: missing value selector ${b.starts}: ${b.doc} / ${b.code}`);
    assert.equal(numeric(doc[1])*b.docScale,numeric(code[1])*b.sourceScale,`defaults: value mismatch ${b.file}: ${b.starts}: ${b.code}`);
  }
  return bindings.length+configurationCount;
}
