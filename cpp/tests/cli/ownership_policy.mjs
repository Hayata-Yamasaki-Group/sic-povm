// SPDX-License-Identifier: AGPL-3.0-or-later
// A deliberately small C++ source-policy reader, not a general C++ parser.
// It reads balanced calls/initializers, retaining quoted strings and nested calls.
import fs from 'node:fs';
import path from 'node:path';
export function discover(root) {
  const files = new Map();
  function walk(dir) {
    for (const entry of fs.readdirSync(path.join(root, dir), {withFileTypes: true})) {
      const file = `${dir}/${entry.name}`;
      if (entry.isDirectory()) walk(file);
      else if (/\.(cpp|hpp)$/.test(file)) files.set(file, fs.readFileSync(path.join(root, file), 'utf8'));
    }
  }
  // Independent checkers live outside these production roots, intentionally.
  walk('cpp/apps'); walk('cpp/src');
  return files;
}
const tokens = source => [...source.matchAll(/\/\/[^\n]*|\/\*[\s\S]*?\*\/|"(?:\\.|[^"\\])*"|[A-Za-z_]\w*|\d+(?:\.\d+)?|::|>>|<<|[^\s]/g)]
  .map(x => x[0]).filter(x => !x.startsWith('//') && !x.startsWith('/*'));
function argumentsAt(t, start, end = ')') {
  const pairs = {'(': ')', '{': '}', '[': ']'};
  const args = [[]];
  for (let i = start; i < t.length; i++) {
    if (t[i] === end) return {args, end: i};
    if (pairs[t[i]]) {
      const sub = argumentsAt(t, i + 1, pairs[t[i]]);
      args.at(-1).push(...t.slice(i, sub.end + 1)); i = sub.end;
    } else if (t[i] === ',') args.push([]);
    else args.at(-1).push(t[i]);
  }
  throw Error('unterminated C++ declaration');
}
const text = t => t.join('');

function ownerRefs(t) {
  const refs = [];
  for (let i = 0; i < t.length; i++) {
    if (!/^k[A-Z]\w*$/.test(t[i])) continue;
    let start=i;
    while(start>=2 && t[start-1]==='::' && /^[a-zA-Z_]\w*$/.test(t[start-2])) start-=2;
    let ref = text(t.slice(start,i+1));
    while (t[i+1] === '.' && /^\w+$/.test(t[i+2] || '') && t[i+3] !== '(') { ref += '.' + t[i+2]; i += 2; }
    refs.push(ref);
  }
  return refs;
}
export function inspect(files) {
  const violations = [], declarations = [], options = [];
  const aliases = new Map(), valueBindings = new Map(), argumentObjects = new Map();
  const helpers = new Map(), helperSpans = new Map();
  const includes = new Map([...files].map(([file, source]) => [file, [...source.matchAll(/#include "([^"]+)"/g)].map(x => x[1])]));
  const add = (file, message) => violations.push(`${file}: ${message}`);
  function resolve(t, file, expandNamed = true) {
    let result = [...t];
    for (let n=0;n<8;n++) {
      let changed=false;
      result=result.flatMap(token=>{
        const alias=aliases.get(file+':'+token);
        if (!alias || alias.includes(token) || (!expandNamed && /^k[A-Z]/.test(token))) return [token];
        changed=true; return alias;
      });
      if(!changed) break;
    }
    return result;
  }
  function collectCalls(t,file,allowUnknown) { for(let i=0;i<t.length;i++) collectAt(t,i,file,allowUnknown); }
  function collectAt(t,i,file,allowUnknown) {
    // Defaults can live in either conditional arm or in an initializer before if.
    // Keep nested calls/conditionals balanced so path and numeric expressions are
    // checked in full, just like an accessor's explicit fallback argument.
    if (t[i] === 'has' && t[i-1] === '.' && t[i+1] === '(') {
      const condition = argumentsAt(t, i+2);
      const names = resolve(condition.args[0], file).filter(x => /^"--[a-z]/.test(x));
      let question = condition.end+1;
      // Parentheses around has() preserve the same condition/default policy.
      let wrappers = 0, negations = 0, before = i-3;
      while (['!', '('].includes(t[before])) {
        if (t[before] === '(') wrappers++;
        else negations++;
        before--;
      }
      while (wrappers > 0 && t[question] === ')') { question++; wrappers--; }
      if (names.length === 1 && t[question] === '?') {
        const first = question+1;
        let colon = first, depth = 0;
        for (; colon < t.length; colon++) {
          if (['(', '{', '['].includes(t[colon])) {
            colon = argumentsAt(t, colon+1, {'(': ')', '{': '}', '[': ']'}[t[colon]]).end;
          } else if (t[colon] === '?') depth++;
          else if (t[colon] === ':' && depth-- === 0) break;
          else if ([';', ')', '}', ']'].includes(t[colon])) break;
        }
        if (t[colon] === ':') {
          let end = colon+1; depth = 0;
          for (; end < t.length; end++) {
            if (['(', '{', '['].includes(t[end])) {
              end = argumentsAt(t, end+1, {'(': ')', '{': '}', '[': ']'}[t[end]]).end;
            } else if (t[end] === '?') depth++;
            else if (t[end] === ':' && depth > 0) depth--;
            else if (depth === 0 && [';', ',', ')', '}', ']', ':'].includes(t[end])) break;
          }
          const selected = negations % 2 ? t.slice(colon+1,end) : t.slice(first, colon);
          const accesses = [];
          for (let n=0; n<selected.length; n++) {
            if (selected[n] === 'get' && selected[n-1] === '.' &&
                selected[n-2] === t[i-2] && selected[n+1] === '(') {
              accesses.push(resolve(argumentsAt(selected,n+2).args[0],file));
            }
          }
          if (accesses.some(args => text(args) === names[0])) {
            declarations.push({file, name: JSON.parse(names[0]),
              kind: selected.includes('number') || selected.includes('numeric') ? 'numeric' : 'get',
              conditional: true, conditionalObject: t[i-2], fallback: negations % 2 ? t.slice(first,colon) : t.slice(colon+1,end), bounds: []});
          }
        }
      }
      // Match an immediately preceding initializer and an assignment guarded by
      // the same argument object's has(). Other objects/targets are unrelated.
      if (names.length === 1 && t[before] === 'if' && negations % 2 === 0 &&
          t[before-1] === ';') {
        let equal = before-2;
        while (equal >= 0 && !['=', ';', '{', '}'].includes(t[equal])) equal--;
        if (t[equal] === '=') {
          let targetStart = equal-1;
          while (targetStart >= 2 && t[targetStart-1] === '.') targetStart -= 2;
          const target = t.slice(targetStart,equal);
          let start = question;
          if (t[start] === '{') start++;
          const assignment = text(t.slice(start,start+target.length));
          if (assignment === text(target) && t[start+target.length] === '=') {
            const rhsStart = start+target.length+1;
            let end = rhsStart;
            while (end < t.length && ![';', '}'].includes(t[end])) {
              if (['(', '{', '['].includes(t[end]))
                end = argumentsAt(t,end+1, {'(': ')', '{': '}', '[': ']'}[t[end]]).end;
              end++;
            }
            const selected = t.slice(rhsStart,end);
            for (let n=0; n<selected.length; n++) {
              if (selected[n] !== 'get' || selected[n-1] !== '.' ||
                  selected[n-2] !== t[i-2] || selected[n+1] !== '(') continue;
              if (text(resolve(argumentsAt(selected,n+2).args[0],file)) !== names[0]) continue;
              declarations.push({file, name: JSON.parse(names[0]),
                kind: selected.includes('number') || selected.includes('numeric') ? 'numeric' : 'get',
                conditional: true, conditionalObject: t[i-2],
                fallback: t.slice(equal+1,before-1), bounds: []});
              break;
            }
          }
        }
      }
    }
    if (!['numeric','number','get'].includes(t[i]) || !['.','::'].includes(t[i-1])) return;
    const kind=t[i]; let open=i+1;
    if(t[open]==='<') while(open<t.length && t[open]!=='(') open++;
    if(t[open]!=='(') return;
    const {args}=argumentsAt(t,open+1);
    const nameArg=resolve(kind==='number' ? (args[1] || []) : args[0],file);
    const names=nameArg.filter(x=>/^"--[a-z]/.test(x)).map(x=>JSON.parse(x).split(' ')[0]);
    if(!names.length) {
      if(allowUnknown && (kind==='numeric' || (kind==='get' && t[i-1]==='.' && argumentObjects.get(file)?.has(t[i-2]) && args[1] && text(args[1])!=='""') || (kind==='number' && !nameArg.some(x=>x.startsWith('"'))))) add(file,'unresolved numeric option name '+text(nameArg));
      return;
    }
    for(const name of names) {
      if(kind==='numeric') {
        let equal=i-1; while(equal>=0 && !['=',';','{','}'].includes(t[equal])) equal--;
        if(t[equal]==='=') {
          let start=equal-1;
          while(start>=2 && t[start-1]==='.' && /^\w+$/.test(t[start-2])) start-=2;
          valueBindings.set(file+':'+text(t.slice(start,equal)),name);
        }
      }
      declarations.push({file,name,kind,
      fallback:kind==='number' ? null : args[1], bounds:kind==='get' ? [] : args.slice(2)});
    }
  }
  for (const [file, source] of files) {
    const t = tokens(source);
    const declarationsText=source+'\n'+(files.get(file.replace(/\.cpp$/,'.hpp')) || '');
    const argumentTypes=['Arguments',...[...declarationsText.matchAll(/using\s+(\w+)\s*=\s*[\w:]*Arguments\s*;/g)].map(x=>x[1])];
    const objectNames=new Set();
    for(const type of argumentTypes) {
      const pattern=new RegExp('\\b(?:[a-zA-Z_]\\w*::)*'+type+'\\s*[&*]?\\s*(\\w+)','g');
      for(const declaration of declarationsText.matchAll(pattern)) objectNames.add(declaration[1]);
    }
    for(const parsed of source.matchAll(/(?:auto|Arguments)\s+(\w+)\s*=\s*[^;]*\.parse\s*\(/g)) objectNames.add(parsed[1]);
    argumentObjects.set(file,objectNames);
    const models=[...source.matchAll(/(?:const|constexpr)\s+([\w:]+)\s+(k[A-Z]\w*)\s*[;{]/g)];
    const header=files.get(file.replace(/\.cpp$/,'.hpp')) || '';
    for(const model of models) {
      const type=model[1].split('::').at(-1);
      for(const declaration of (source+'\n'+header).matchAll(/([\w:]+)\s+([a-zA-Z_]\w*)\s*(?:[;=])/g)) {
        if(declaration[1].split('::').at(-1)===type && declaration[2]!==model[2] && !/^k[A-Z]/.test(declaration[2]))
          aliases.set(file+':'+declaration[2],[model[2]]);
      }
    }
    const spans = [];
    for (let i = 0; i < t.length; i++) {
      if (t[i] === 'auto' && t[i+2] === '=' && t[i+3] === '[') {
        const capture = argumentsAt(t, i+4, ']');
        if (t[capture.end+1] !== '(') continue;
        const parameters = argumentsAt(t, capture.end+2);
        let bodyStart=parameters.end+1;
        while(bodyStart<t.length && !['{',';'].includes(t[bodyStart])) bodyStart++;
        if(t[bodyStart]!=='{') continue;
        const body = argumentsAt(t, bodyStart+1, '}');
        const bodyTokens = t.slice(bodyStart+1, body.end);
        if (bodyTokens.includes('number') || bodyTokens.includes('numeric')) {
          const params = parameters.args.map(p => {
            const equal = p.indexOf('=');
            const declaration = equal < 0 ? p : p.slice(0,equal);
            return {name: declaration.at(-1), fallback: equal < 0 ? null : p.slice(equal+1)};
          });
          helpers.set(file + ':' + t[i+1], {params, body: bodyTokens});
          spans.push([i, body.end]);
        }
      }
    }
    helperSpans.set(file, spans);
    for (let i = 0; i < t.length; i++) {
      if (/^[a-zA-Z_]\w*$/.test(t[i]) && t[i+1] === '=' && (/^k[A-Z]/.test(t[i+2] || '') || /^k[A-Z]/.test(t[i]) || /^(?:\"|\d)/.test(t[i+2] || ''))) {
        let end = i + 2;
        while (end < t.length && ![';', ','].includes(t[end])) {
          if (t[end] === '(' || t[end] === '{' || t[end] === '[')
            end = argumentsAt(t, end + 1, {'(': ')', '{': '}', '[': ']'}[t[end]]).end;
          end++;
        }
        const rhs = t.slice(i+2, end);
        if (!aliases.has(file + ':' + t[i])) aliases.set(file + ':' + t[i], rhs);
      }
      if (t[i] === '{' && /^"--[a-z]/.test(t[i+1] || '')) {
        const {args} = argumentsAt(t, i + 1, '}');
        if (args.length >= 5 && args[0].length === 1 && args[1][0]?.startsWith('"') && args[2].some(x => x.startsWith('"'))) {
          options.push({file, name: JSON.parse(args[0][0]), help: args[2]});
        }
      }
      const helper = helpers.get(file + ':' + t[i]);
      if (helper && t[i+1] === '(') {
        const invocation = argumentsAt(t,i+2);
        const replacements = new Map(helper.params.map((p,n) => [p.name, invocation.args[n] || p.fallback]));
        const substituted = helper.body.flatMap(token => replacements.get(token) || [token]);
        collectCalls(substituted, file, false);
      }
      if (!spans.some(([low, high]) => i >= low && i <= high)) collectAt(t,i,file,true);

    }
  }
  for (const opt of options) {
    for (const literal of opt.help.filter(x => x.startsWith('"'))) {
      const value = JSON.parse(literal);
      if (/\bdefault(?:\s+|:\s*)[-+]?\d|\b\d+\.\.\d+|\b(?:maximum|ceiling|at most)\s+\d{2,}/i.test(value))
        add(opt.file, `${opt.name}: copied help default/range ${literal}`);
    }
  }
  for (const d of declarations) {
    const local = options.filter(x => x.file === d.file && x.name === d.name);
    const ownPrefix = d.file.startsWith('cpp/src/') ? d.file.split('/').slice(0,3).join('/') + '/' : null;
    const ownHeaders = (includes.get(d.file) || []).filter(header => ownPrefix && files.has('cpp/src/'+header) && ('cpp/src/'+header).startsWith(ownPrefix));
    const candidates = local.length ? local : options.filter(x => x.name === d.name && ownHeaders.some(header => (includes.get(x.file) || []).includes(header)));
    if (!candidates.length) { add(d.file, `${d.name}: no parser help declaration`); continue; }
    const help = candidates.flatMap(x => x.help);
    const helpText = text(help);
    const ownNamespace = files.get(d.file).match(/namespace\s+([a-zA-Z_][\w:]*)\s*\{/)?.[1];
    const normalized = value => ownNamespace ? value.replaceAll(ownNamespace+'::','') : value;
    const helpOwners = new Set(ownerRefs(help).map(normalized));
    const rendered = [];
    for(let n=0;n<help.length;n++) if(help[n]==='default_text' && help[n+1]==='(')
      rendered.push(normalized(text(argumentsAt(help,n+2).args[0])));
    const expandedFallback = d.fallback ? resolve(d.fallback,d.file,false) : [];
    const fallbackText = normalized(text(expandedFallback));
    if(d.fallback && fallbackText!=='""' && fallbackText!=='0' && !/\bdefault\b|when omitted/i.test(helpText))
      add(d.file,d.name+': help does not identify omitted default');
    const directOwner = /^(?:[a-zA-Z_]\w*::)*k[A-Z]\w*(?:\.\w+)*(?:\(\))?$/;
    const policyCall = d.fallback && /^k[A-Z]\w*\.[a-zA-Z_]\w*\(/.test(fallbackText) && text(d.fallback).endsWith(')') && argumentsAt(d.fallback, d.fallback.indexOf('(')+1).end===d.fallback.length-1;
    const numericExpression = d.kind==='numeric' && !directOwner.test(fallbackText) && !policyCall && !rendered.includes(fallbackText) && fallbackText!=='0';
    if(numericExpression) add(d.file,d.name+': fallback expression differs from rendered help: '+fallbackText);
    if(d.fallback && d.kind==='get' && fallbackText!=='""') {
      const owner='(?:[a-zA-Z_]\\w*::)*k[A-Z]\\w*(?:\\.\\w+)*';
      const direct = new RegExp('^'+owner+'(?:\\.string\\(\\))?$').test(fallbackText);
      const wrapped = new RegExp('^(?:std::string|default_text)\\('+owner+'\\)$').test(fallbackText);
      const relative = new RegExp('^\\w+\\+"/"\\+'+owner+'$').test(fallbackText);
      const pathJoin = new RegExp('^\\(filesystem::path\\('+owner+'\\)/\\w+\\)\\.string\\(\\)$').test(fallbackText);
      const conditionalPath = d.conditional && new RegExp(
        '^filesystem::path\\(' + d.conditionalObject + '\\.get\\("--[a-z][\\w-]*",std::string\\(' + owner +
        '\\)\\)\\)/\\w+/' + owner + '$').test(fallbackText);
      if(!direct && !wrapped && !relative && !pathJoin && !conditionalPath) add(d.file,d.name+': string fallback expression lacks a named owner '+fallbackText);
    }
    if (d.fallback && text(d.fallback) !== '""') {
      let expanded = [...d.fallback];
      const refs = ownerRefs(expanded).map(normalized);
      for (let n = 0; n < 8; n++) {
        let changed = false;
        expanded = expanded.flatMap(token => {
          const alias = aliases.get(d.file + ':' + token);
          if (!alias || alias.includes(token)) return [token];
          changed = true; return alias;
        });
        refs.push(...ownerRefs(expanded).map(normalized));
        if (!changed) break;
      }
      // Zero sentinels for required/optional arguments are absence policies, not
      // user-visible numeric defaults. Their help must explicitly describe omission.
      if (text(d.fallback) === '0') {
        if (!/required/i.test(helpText)) add(d.file, `${d.name}: undocumented absence sentinel`);
      } else if (!refs.length) {
        add(d.file, `${d.name}: fallback lacks named owner: ${text(d.fallback)}`);
      } else if (!refs.some(ref => helpOwners.has(ref))) {
        add(d.file, `${d.name}: help does not consume fallback owner ${refs.join(',')}`);
      }
    }
    for (const bound of d.bounds) {
      const binding=valueBindings.get(d.file+':'+text(bound));
      if(binding && helpText.includes(binding)) continue;
      const expanded = resolve(bound,d.file,false);
      let spelling = text(expanded).replace(/^\((.*)\)$/,'$1');
      if(spelling==='false?0:1') spelling='1';
      if(spelling==='true?0:1') spelling='0';
      // Only the intrinsic endpoints and exact native unit/headroom expressions
      // are exempt. Prefix matching an arbitrary native-bound expression is unsafe.
      const native = /^(?:U?INT\d+|LONG|SIZE)_MAX$/;
      if (/^(0|1)$/.test(spelling) || native.test(spelling) ||
          /^(?:U?INT\d+|LONG|SIZE)_MAX(?:>>20|-1)$/.test(spelling) ||
          spelling==='UINT64_MAX/(uint64_t{1}<<20)' ||
          /^UINT64_MAX-[a-zA-Z_.]+-[a-zA-Z_.]+-[a-zA-Z_.]+$/.test(spelling) ||
          /^std::numeric_limits<[^>]+>::(?:min|max)\(\)$/.test(spelling)) continue;
      const refs = ownerRefs(expanded).map(normalized);
      if(expanded.some(x=>/^\d/.test(x)) && !rendered.includes(normalized(spelling))) add(d.file,d.name+': unnamed range bound or changed expression '+spelling);
      if(!refs.length && !/^[a-zA-Z_]\w*(?:\.[a-zA-Z_]\w*)+$/.test(spelling) && !(new RegExp('\\b'+spelling+'\\s*=\\s*[\\s\\S]{0,200}number<').test(files.get(d.file)))) add(d.file,d.name+': unresolved range owner '+spelling);
      if(!refs.length && /^[a-zA-Z_]\w*(?:\.[a-zA-Z_]\w*)+$/.test(spelling))
        add(d.file,d.name+': dynamic bound not documented '+spelling);
      if(refs.length && !refs.every(ref=>helpOwners.has(ref))) add(d.file,d.name+': help does not consume range owner '+refs.join(','));
      if(refs.length && /[+*%?\/-]/.test(spelling) && !rendered.includes(normalized(spelling))) add(d.file,d.name+': computed range must have a named owner '+spelling);
    }
  }
  return {violations, declarations, options};
}
