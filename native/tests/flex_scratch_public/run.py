#!/usr/bin/env python3
"""Identical public harness against exact A/B DSOs, separate timing/OOM arms."""
import argparse, subprocess, pathlib, hashlib, json, statistics, math
p=argparse.ArgumentParser();p.add_argument('--baseline',required=True);p.add_argument('--candidate',required=True);p.add_argument('--out',required=True);p.add_argument('--phase',choices=['build','manifest','timing','oom','memory','all'],default='all');a=p.parse_args()
here=pathlib.Path(__file__).resolve().parent;out=pathlib.Path(a.out).resolve();out.mkdir(parents=True,exist_ok=True);include=here.parent.parent/'include'
libs={'A':pathlib.Path(a.baseline).resolve(),'B':pathlib.Path(a.candidate).resolve()}
def run(cmd,**kw):return subprocess.run(list(map(str,cmd)),check=True,text=True,capture_output=True,**kw)
if a.phase in ['build','all']:
 for arm,lib in libs.items():
  for src,name,opt in [('runtime.cpp','runtime','-O3'),('oom.cpp','oom','-O2')]:
   cmd=['g++','-std=c++20',opt,'-DNDEBUG','-ffp-contract=off','-pthread','-I',include,here/src,lib,'-Wl,-rpath,'+str(lib.parent),'-o',out/(name+arm)]
   result=run(cmd);(out/(name+arm+'-build.txt')).write_text(' '.join(map(str,cmd))+'\n'+result.stderr)
  (out/('symbols'+arm+'.txt')).write_text(run(['nm','-C',out/('runtime'+arm)]).stdout)
 (out/'libraries.json').write_text(json.dumps({k:{'path':str(v),'sha256':hashlib.sha256(v.read_bytes()).hexdigest()} for k,v in libs.items()},indent=2))
cases=[(m,n,axis)for m in ['grow','frozen','mixed','nowrap','shrink','increasing']for n in [32,256,4096]for axis in ['row','column']]
if a.phase in ['manifest','all']:
 results=[]
 for m,n,axis in cases:
  pair=[]
  for arm in libs:
   z=run([out/('runtime'+arm),'manifest',m,n,axis],timeout=240);name=f'manifest-{arm}-{m}-{n}-{axis}.txt';(out/name).write_text(z.stdout);pair.append(z.stdout)
  assert pair[0]==pair[1],(m,n,axis,'manifest mismatch')
  stages=pair[0].splitlines();assert len(stages)==3 and stages[0]!=stages[1] and stages[1]!=stages[2],(m,n,axis,'relayout/mutation witness absent')
  results.append({'mode':m,'n':n,'axis':axis,'stages':3,'equal':True,'sha256':hashlib.sha256(pair[0].encode()).hexdigest()})
  print('manifest PASS',m,n,axis,flush=True)
 (out/'manifest-summary.json').write_text(json.dumps(results,indent=2))
if a.phase in ['oom','all']:
 for arm in libs:
  z=run([out/('oom'+arm)],timeout=120);(out/('oom-'+arm+'.log')).write_text(z.stdout+z.stderr);print(z.stdout,flush=True)
if a.phase in ['timing','all']:
 raw=[]
 for campaign in range(5):
  for idx,(m,n,axis) in enumerate(cases+[("mutated",n,axis)for n in [32,256,4096]for axis in ["row","column"]]):
   for arm in (['A','B']if(campaign+idx)%2==0 else ['B','A']):
    z=run([out/('runtime'+arm),'timing',m,n,axis],timeout=120)
    samples=[float(line.split(',')[1])for line in z.stdout.splitlines()]
    raw.append({'campaign':campaign,'arm':arm,'mode':m,'n':n,'axis':axis,'ns':samples,'memory':z.stderr.strip()})
   (out/'timing-raw.json').write_text(json.dumps(raw,indent=2));print('timing',campaign,m,n,axis,flush=True)
 summaries=[]
 for m,n,axis in cases+[("mutated",n,axis)for n in [32,256,4096]for axis in ["row","column"]]:
  records=[x for x in raw if(x['mode'],x['n'],x['axis'])==(m,n,axis)];by={arm:[s for x in records if x['arm']==arm for s in x['ns']]for arm in libs}
  ratios=[]
  for c in range(5):
   med={x['arm']:statistics.median(x['ns'])for x in records if x['campaign']==c};ratios.append(med['B']/med['A'])
  summary={'mode':m,'n':n,'axis':axis,'campaign_ratios':ratios,'ratio_p50':statistics.median(ratios),'over_1_05':sum(r>1.05 for r in ratios)}
  for arm,v in by.items():
   summary[arm]={'p50_ns':statistics.median(v),'p95_ns':sorted(v)[math.ceil(.95*len(v))-1],'min_ns':min(v),'max_ns':max(v),'cv':statistics.stdev(v)/statistics.mean(v)}
  summaries.append(summary)
 (out/'timing-summary.json').write_text(json.dumps(summaries,indent=2))

if a.phase in ['memory','all']:
 records=[]
 for mode in ['grow','frozen','increasing']:
  for axis in ['row','column']:
   for arm in libs:
    z=subprocess.run(list(map(str,[out/('runtime'+arm),'memory',mode,4096,axis])),text=True,capture_output=True,timeout=120)
    if z.returncode not in (0,77):raise RuntimeError(z.stderr)
    records.append({'mode':mode,'axis':axis,'arm':arm,'n':4096,'status':'measured' if z.returncode==0 else 'unsupported','returncode':z.returncode,'result':z.stdout.strip(),'stderr':z.stderr})
 (out/'memory.json').write_text(json.dumps(records,indent=2))
