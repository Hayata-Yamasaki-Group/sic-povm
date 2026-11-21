// SPDX-License-Identifier: AGPL-3.0-or-later
import {spawnSync} from 'node:child_process';
const [expected,program,...args] = process.argv.slice(2);
const result=spawnSync(program,args,{stdio:'inherit'});
if (result.error || result.signal || result.status !== Number(expected)) {
  console.error(`SIC_UNEXPECTED_EXIT_STATUS expected=${expected} actual=${result.status} signal=${result.signal} error=${result.error || ''}`);
  process.exitCode=1;
}
