// SPDX-License-Identifier: AGPL-3.0-or-later
import {test} from 'node:test';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, rmSync, writeFileSync} from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {validateCommits} from './parameters.mjs';

test('A must be a valid ancestor of B', () => {
 const dir=mkdtempSync(path.join(os.tmpdir(),'sic-test-'));
 const gitExecutable=process.env.SIC_TEST_GIT || 'git';
 const git=(...args)=>{const r=spawnSync(gitExecutable,['-C',dir,...args],{encoding:'utf8',env:{...process.env,GIT_AUTHOR_NAME:'test',GIT_AUTHOR_EMAIL:'test@example.invalid',GIT_COMMITTER_NAME:'test',GIT_COMMITTER_EMAIL:'test@example.invalid'}});assert.equal(r.status,0,r.stderr);return r.stdout.trim();};
 try {
  git('init','-q'); writeFileSync(path.join(dir,'f'),'a');git('add','f');git('commit','-qm','a');const a=git('rev-parse','HEAD');
  writeFileSync(path.join(dir,'f'),'b');git('commit','-qam','b');const b=git('rev-parse','HEAD');
  assert.deepEqual(validateCommits(dir,a.slice(0,8),b,gitExecutable),{A:a,B:b});
  assert.deepEqual(validateCommits(dir,b,b,gitExecutable),{A:b,B:b});
  assert.throws(()=>validateCommits(dir,b,a,gitExecutable));
  assert.throws(()=>validateCommits(dir,'bad!hash',b,gitExecutable));
  assert.throws(()=>validateCommits(dir,'deadbeef',b,gitExecutable));
  assert.throws(()=>validateCommits(dir,'',b,gitExecutable));
 } finally {rmSync(dir,{recursive:true,force:true});}
});
