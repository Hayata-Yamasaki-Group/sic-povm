// SPDX-License-Identifier: AGPL-3.0-or-later
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';

export function validateCommits(repo, a, b='HEAD', gitExecutable='git') {
  assert(a && /^[0-9a-fA-F]{4,40}$/.test(a), 'A_COMMIT must be a commit hash');
  const git = (...args) => {
    const r = spawnSync(gitExecutable, ['-C', repo, ...args], {encoding:'utf8'});
    assert.equal(r.status, 0, `git ${args[0]} failed: ${r.stderr.trim()}`);
    return r.stdout.trim();
  };
  const A = git('rev-parse', '--verify', `${a}^{commit}`);
  const B = git('rev-parse', '--verify', `${b}^{commit}`);
  git('merge-base', '--is-ancestor', A, B);
  return {A, B};
}
