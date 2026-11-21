// SPDX-License-Identifier: AGPL-3.0-or-later
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';

const work = process.argv[2];
fs.mkdirSync(path.join(work, 'cpp/include/sic'), {recursive: true});
const fixture = path.join(work, 'cpp/include/sic/control.hpp');
const script = fileURLToPath(new URL('./header_namespaces.mjs', import.meta.url));
function run(source, reject) {
  fs.writeFileSync(fixture, source);
  const result = spawnSync(process.execPath, [script, work], {encoding: 'utf8', timeout: 5000});
  if (result.error || result.status !== (reject ? 1 : 0) ||
      (reject && !result.stderr.includes('sic/control.hpp:'))) {
    throw new Error(`Header namespace control failed: ${source}\n${result.stdout}${result.stderr}`);
  }
}
for (const source of [
  'using namespace external;',
  '#define IMPORT using namespace std;\nIMPORT',
  '#define IMPORT() { using /* gap */ namespace std; }',
  '#define IMPORT using na' + '\\' + '\n' + 'mespace std;',
  'inline constexpr auto raw = R"tag(\n# text )tag";\nusing namespace external;\ninline constexpr auto after = "foo";',
  '/*\n# */\nusing namespace external;\n/* end */',
  'namespace sic { using namespace external; }',
  'namespace sic::nested { namespace { using\n/* gap */ namespace ::external; } }',
  '#ifndef X_HPP\n#define X_HPP\n#if defined(__cplusplus)\nextern "C++" {\nusing namespace external;\n}\n#endif\n#endif',
  "inline constexpr unsigned a = 1'000;\nusing namespace external;\ninline constexpr unsigned b = 2'000;",
  'using na' + '\\' + '\n' + 'mespace external;',
  '#pragma once\nextern "C++" { using namespace external; }',
  'namespace sic { inline void f() {} using namespace external; }',
  'namespace sic { struct C {}; using namespace external; }',
  'namespace sic { using external::name; }\nusing namespace other;',
]) run(source, true);
run(`// using namespace ignored;
#define TEXT "using namespace ignored;"
/* using namespace ignored; */
namespace sic::nested {
using external::name;
namespace alias = external;
inline void f() { using namespace local; }
inline auto g() { return [] { using namespace local; }; }
inline constexpr auto text = "using namespace ignored; {";
inline constexpr auto raw = R"tag(using namespace ignored; { })tag";
}
`, false);
console.log('Installed header namespace positive and negative controls passed');

console.log('SIC_NEGATIVE_CONTROL:header-namespaces-controls');
