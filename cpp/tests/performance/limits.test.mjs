// SPDX-License-Identifier: AGPL-3.0-or-later
import test from 'node:test';
import assert from 'node:assert/strict';
import {performance,attemptDisposition,overallVerdict} from './limits.mjs';
const samples=(walls,rsses)=>walls.map((wall,i)=>({wall,rss:rsses[i]}));
const a=samples([100,100,100],[1000,1000,1000]);
const evaluate=(b,aa=a,kind='producer',refs=a.slice(0,2))=>performance(aa,b,refs,kind);
test('inclusive median wall and RSS limits, ignoring outliers',()=>{
 const p=evaluate(samples([103,103,200],[1050,1050,2000]));
 assert.equal(p.status,'PASS');assert.equal(p.rss.factor,1.05);
});
test('median wall above limit fails',()=>assert.equal(evaluate(samples([103.01,103.01,100],[1000,1000,1000])).status,'FAIL'));
test('median RSS above floor fails',()=>assert.equal(evaluate(samples([100,100,100],[1051,1051,1000])).status,'FAIL'));
test('A RSS range raises effective limit',()=>{
 const aa=samples([100,100,100],[900,1000,1100]);
 const p=evaluate(samples([100,100,100],[1200,1200,1200]),aa);
 assert.equal(p.rss.factor,1.2);assert.equal(p.rss.limit,1200);assert.equal(p.status,'PASS');
 assert.equal(evaluate(samples([100,100,100],[1201,1201,1201]),aa).status,'FAIL');
});
test('short decoder requires every B strictly below ten seconds',()=>{
 const aa=samples([3,3,3],[1000,1000,1000]);
 assert.equal(evaluate(samples([9.99,3,3],[1000,1000,1000]),aa,'decoder').status,'PASS');
 assert.equal(evaluate(samples([10,3,3],[1000,1000,1000]),aa,'decoder').status,'FAIL');
});
test('decoder at ten seconds and short producer use median rule',()=>{
 assert.equal(evaluate(samples([10.31,10.31,10],[1000,1000,1000]),samples([10,10,10],[1000,1000,1000]),'decoder').status,'FAIL');
 assert.equal(evaluate(samples([4,4,4],[1000,1000,1000]),samples([3,3,3],[1000,1000,1000])).status,'FAIL');
});
test('wall range over ten percent is inconclusive even for failure',()=>{
 const aa=samples([94,100,106],[1000,1000,1000]);
 assert.equal(evaluate(samples([200,200,200],[2000,2000,2000]),aa).status,'INCONCLUSIVE');
 assert.equal(evaluate(samples([100,100,100],[1000,1000,1000]),samples([95,100,105],[1000,1000,1000])).status,'PASS');
});
test('informational results remain informational regardless of drift',()=>{
 const p=evaluate(a,a,'producer',samples([1,1],[1,1]));assert.equal(p.status,'PASS');assert.equal(p.reference.informational,true);
});
test('missing or invalid samples fail closed and dry run is explicit',()=>{
 assert.throws(()=>performance(a.slice(0,2),a.slice(0,2),a.slice(0,2),'producer'));
 assert.throws(()=>evaluate([{wall:NaN,rss:1},...a.slice(1)]));
 assert.equal(performance(a.slice(0,2),a.slice(0,2),a.slice(0,2),'producer',{dryRun:true}).dryRun,true);
});

test('inconclusive retries once and aggregate fails closed',()=>{
 const first=attemptDisposition('INCONCLUSIVE',1), second=attemptDisposition('INCONCLUSIVE',2);
 assert.deepEqual(first,{retry:true,blocking:false});assert.deepEqual(second,{retry:false,blocking:true});
 assert.deepEqual(attemptDisposition('PASS',1),{retry:false,blocking:true});
 assert.deepEqual(attemptDisposition('FAIL',1),{retry:false,blocking:true});
 const old={status:'INCONCLUSIVE',blocking:first.blocking};
 assert.equal(overallVerdict([old,{status:'PASS',blocking:true}]),'PASS');
 assert.equal(overallVerdict([old,{status:'INCONCLUSIVE',blocking:second.blocking}]),'INCONCLUSIVE');
 assert.equal(overallVerdict([old,{status:'FAIL',blocking:true}]),'FAIL');
 assert.equal(overallVerdict([{status:'FAIL',blocking:true},{status:'INCONCLUSIVE',blocking:true}]),'FAIL');
});
