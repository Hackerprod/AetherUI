#!/usr/bin/env python3
"""Summarize raw standard-allocator timing, preserving all campaigns/outliers."""
import argparse, collections, json, math, pathlib, statistics
p=argparse.ArgumentParser();p.add_argument('directory',type=pathlib.Path);a=p.parse_args()
data=collections.defaultdict(list);campaigns=collections.defaultdict(dict)
def summary(v):
 s=sorted(v);return {'n':len(v),'p50':statistics.median(v),'p95':s[math.ceil(.95*len(s))-1], 'min':s[0],'max':s[-1],'cv':statistics.pstdev(v)/statistics.mean(v)}
for path in sorted(a.directory.glob('timing-[1-5].log')):
 for line in path.read_text().splitlines():
  if not line.startswith('raw,'):continue
  _,name,block,order,reps,x,y=line.split(',');x,y=float(x),float(y)
  data[name].append((x,y));campaigns[name].setdefault(path.stem,[]).append(y/x)
report={}
for name,pairs in data.items():
 r={'baseline_us':summary([x for x,y in pairs]),'product_us':summary([y for x,y in pairs]),'paired_ratio':summary([y/x for x,y in pairs]),'campaign_median_paired_ratios':{k:statistics.median(v) for k,v in campaigns[name].items()}}
 r['campaigns_over_1_05']=sum(v>1.05 for v in r['campaign_median_paired_ratios'].values())
 if name in ['wrap-active','wrap-frozen']:r['median_improvement_at_least_5_percent']=r['paired_ratio']['p50']<=.95
 report[name]=r
(a.directory/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
for name,r in report.items():print(name,json.dumps(r,sort_keys=True))
