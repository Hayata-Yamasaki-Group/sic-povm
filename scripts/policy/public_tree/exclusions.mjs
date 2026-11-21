// SPDX-License-Identifier: AGPL-3.0-or-later
import fs from 'node:fs';
import path from 'node:path';

export function readPublicExclusions(root) {
  const read = file => fs.readFileSync(path.join(root, file), 'utf8');
  const base = read('scripts/policy/public_exclude.txt');
  // Load local, non-distributed tool configuration when present.
  const localFile = 'private/policy/public_exclude.txt';
  const local = fs.existsSync(path.join(root, localFile)) ? read(localFile) : '';
  const rules = (base + '\n' + local).split(/\r?\n/).filter(rule => rule && !rule.startsWith('#'));
  const component = '@component ';
  return {
    entries: new Set(rules.filter(rule => !rule.startsWith(component))),
    components: rules.filter(rule => rule.startsWith(component))
      .map(rule => new RegExp(rule.slice(component.length))),
  };
}
