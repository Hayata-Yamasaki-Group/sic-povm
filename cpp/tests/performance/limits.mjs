// SPDX-License-Identifier: AGPL-3.0-or-later
import assert from 'node:assert/strict';

const median = values => {
 const v=[...values].sort((a,b)=>a-b), i=Math.floor(v.length/2);
 return v.length%2?v[i]:(v[i-1]+v[i])/2;
};
const stats = (samples,key) => {
 const v=samples.map(x=>x[key]);
 assert(v.every(x=>Number.isFinite(x)&&x>0),'invalid measurement');
 const med=median(v), range=Math.max(...v)-Math.min(...v);
 return {median:med,range,relativeRange:range/med};
};
// dryRun evaluates a two-sample record.
export function performance(a,b,refs,kind,{dryRun=false}={}){
 assert(['producer','decoder'].includes(kind));
 assert.equal(a.length,dryRun?2:3);assert.equal(b.length,dryRun?2:3);
 assert.equal(refs.length,2);
 const aw=stats(a,'wall'), bw=stats(b,'wall'), ar=stats(a,'rss'), br=stats(b,'rss');
 const reference={wall:stats(refs,'wall'),rss:stats(refs,'rss'),informational:true};
 const short=kind==='decoder'&&aw.median<10;
 const wallLimit=short?10:aw.median*1.03;
 const wallPass=short?b.every(x=>x.wall<10):bw.median<=wallLimit;
 const rssFactor=1+Math.max(0.05,ar.relativeRange),rssLimit=ar.median*rssFactor;
 const rssPass=br.median<=rssLimit;
 const status=aw.relativeRange>0.10?'INCONCLUSIVE':wallPass&&rssPass?'PASS':'FAIL';
 return {dryRun,kind,a:{wall:aw,rss:ar},b:{wall:bw,rss:br},reference,
  wall:{rule:short?'each B < 10 s':'median B <= 1.03 * median A',limit:wallLimit,pass:wallPass},
  rss:{factor:rssFactor,limit:rssLimit,pass:rssPass},status};
}

export function attemptDisposition(status,attempt){
 assert(['PASS','FAIL','INCONCLUSIVE'].includes(status));
 assert([1,2].includes(attempt));
 const retry=status==='INCONCLUSIVE'&&attempt===1;
 return {retry,blocking:!retry};
}
export function overallVerdict(checks){
 return checks.some(g=>g.blocking&&g.status==='FAIL')?'FAIL':
  checks.some(g=>g.blocking&&g.status==='INCONCLUSIVE')?'INCONCLUSIVE':'PASS';
}
