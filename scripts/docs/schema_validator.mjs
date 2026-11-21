// SPDX-License-Identifier: AGPL-3.0-or-later
// Bounded, offline draft-2020-12 subset used by the committed schema corpus.
// Unknown keywords, remote references and unsupported schema forms fail closed.
import assert from 'node:assert/strict';
const annotation = new Set(['$schema','$id','$comment','title','description','default','examples']);
const implemented = new Set(['$defs','$ref','type','const','enum','required','properties',
  'additionalProperties','items','prefixItems','minItems','maxItems','minLength','maxLength',
  'minProperties','minimum','maximum','exclusiveMinimum','exclusiveMaximum','pattern','oneOf','anyOf','allOf']);
export function checkSchema(schema, top = schema) {
  if (typeof schema === 'boolean') return;
  assert(schema && typeof schema === 'object' && !Array.isArray(schema), 'schema: expected object');
  for (const [key, value] of Object.entries(schema)) {
    assert(annotation.has(key) || implemented.has(key), `schema: unsupported keyword ${key}`);
    if (key === '$ref') assert(/^#\//.test(value) || /^[a-z0-9-]+\.schema\.json$/.test(value), 'schema: only local corpus references are supported');
    if (['$defs','properties'].includes(key)) Object.values(value).forEach(s => checkSchema(s, top));
    if (['items','additionalProperties'].includes(key)) checkSchema(value, top);
    if (['prefixItems','oneOf','anyOf','allOf'].includes(key)) value.forEach(s => checkSchema(s, top));
  }
}
export const corpus = new Map();
export function validate(schema, value, top = schema, at = '$', depth = 0) {
  assert(depth < 128, 'schema: recursion limit');
  if (schema === true) return;
  assert(schema !== false, `schema: ${at}: forbidden value`);
  const next = (s,v,p = at) => validate(s,v,top,p,depth+1);
  if (schema.$ref) {
    if (!schema.$ref.startsWith('#/')) {
      const external=corpus.get(schema.$ref); assert(external,`schema: unresolved ${schema.$ref}`);
      validate(external,value,external,at,depth+1);
    } else {
    const ref = schema.$ref.slice(2).split('/').reduce((o,k) => o?.[k.replace(/~1/g,'/').replace(/~0/g,'~')],top);
    assert(ref !== undefined, `schema: unresolved ${schema.$ref}`); next(ref,value);
    }
  }
  const same = (a,b) => JSON.stringify(a) === JSON.stringify(b);
  if ('const' in schema) assert(same(value,schema.const), `schema: ${at}: const`);
  if (schema.enum) assert(schema.enum.some(v => same(v,value)), `schema: ${at}: enum`);
  const kind = t => t === 'null' ? value === null : t === 'array' ? Array.isArray(value) :
    t === 'object' ? value !== null && typeof value === 'object' && !Array.isArray(value) :
    t === 'integer' ? Number.isSafeInteger(value) : t === 'number' ? typeof value === 'number' && Number.isFinite(value) : typeof value === t;
  if (schema.type) assert((Array.isArray(schema.type) ? schema.type : [schema.type]).some(kind), `schema: ${at}: type`);
  for (const op of ['oneOf','anyOf','allOf']) if (schema[op]) {
    const matches = schema[op].filter(s => {try {next(s,value); return true;} catch {return false;}}).length;
    assert(op === 'oneOf' ? matches === 1 : op === 'anyOf' ? matches > 0 : matches === schema[op].length, `schema: ${at}: ${op}`);
  }
  if (typeof value === 'number') for (const [key, good] of [
    ['minimum', n => value >= n], ['maximum', n => value <= n],
    ['exclusiveMinimum', n => value > n], ['exclusiveMaximum', n => value < n]])
    if (key in schema) assert(good(schema[key]), `schema: ${at}: ${key}`);
  if (typeof value === 'string') {
    const length = [...value].length;
    if ('minLength' in schema) assert(length >= schema.minLength, `schema: ${at}: minLength`);
    if ('maxLength' in schema) assert(length <= schema.maxLength, `schema: ${at}: maxLength`);
    if (schema.pattern) assert(new RegExp(schema.pattern,'u').test(value), `schema: ${at}: pattern`);
  }
  if (Array.isArray(value)) {
    if ('minItems' in schema) assert(value.length >= schema.minItems, `schema: ${at}: minItems`);
    if ('maxItems' in schema) assert(value.length <= schema.maxItems, `schema: ${at}: maxItems`);
    const prefix = schema.prefixItems || [];
    value.forEach((v,i) => {if (i < prefix.length) next(prefix[i],v,`${at}/${i}`); else if ('items' in schema) next(schema.items,v,`${at}/${i}`);});
  } else if (value && typeof value === 'object') {
    if ('minProperties' in schema) assert(Object.keys(value).length >= schema.minProperties, `schema: ${at}: minProperties`);
    for (const name of schema.required || []) assert(Object.hasOwn(value,name), `schema: ${at}: required ${name}`);
    for (const [name,v] of Object.entries(value)) {
      if (Object.hasOwn(schema.properties || {},name)) next(schema.properties[name],v,`${at}/${name}`);
      else if ('additionalProperties' in schema) next(schema.additionalProperties,v,`${at}/${name}`);
    }
  }
}
