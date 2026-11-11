#!/usr/bin/env python3
"""Bounded counted S/Q verification. This is never an A/B timing population."""
import argparse,json,pathlib,subprocess
from run_campaigns import HERE
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--harnesses',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False);out=a.output.resolve();results={}
for path in sorted((HERE/'control-inputs').glob('*.json')):
 data=json.loads(path.read_text());name=data['name'];target=out/name;target.mkdir();html=target/'input.html';css=target/'input.css';html.write_text(data['html']);css.write_text(data.get('author_css',data.get('selector_source','')))
 args=[str(a.harnesses.resolve()/('private' if name.startswith('Q') else 'public'))]
 args+=([str(html),str(css),str(target),str(data['ancestor_links'])] if name.startswith('Q') else [name,str(html),str(css),str(target),str(data['subject_count'])])
 r=subprocess.run(args,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=30);(target/'stdout.log').write_bytes(r.stdout);(target/'stderr.log').write_bytes(r.stderr)
 rows=[]
 for line in r.stdout.decode().splitlines():
  if line.startswith('counted='):
   values=dict(v.split('=',1) for v in line.split(','));rows.append({k:int(v) if k!='counted' else v for k,v in values.items()})
 results[name]={'command':args,'exit_status':r.returncode,'observations':rows,'checks':[]}
 def check(condition,label):
  results[name]['checks'].append({'PASS':bool(condition),'label':label})
  if not condition:raise AssertionError(name+': '+label)
 try:
  check(r.returncode==0,'public/private semantic harness success')
  check(len(rows)==23 if name.startswith('Q') else len(rows)==22,'every prescribed operation/resolution counted')
  for c in rows:
   check(c['rhs']==c['admitted'],'one RHS per admitted')
   if name=='S00':check(c['admitted']==c['collections']==c['copies']==c['ancestor_links']==0,'no eligible scratch work')
   elif name=='S04':check(c['admitted']>0 and c['continuations']==c['ancestor_links']==c['collections']==c['copies']==0,'admitted RHS failures do no ancestor work')
   elif name in ('S01','S02','S06','S14','S15'):
    check(c['admitted']>0 and c['collections']==c['collection_links']==c['copies']==0,'exclusively positive has no collection/copies')
    if name in ('S01','S02','S06','S14','S15'):check(c['continuations']==c['admitted'] and c['ancestor_links']==c['admitted'],'positive immediate-parent one visit per query')
   elif name.startswith('Q'):
    check(c['admitted']==1 and c['continuations']==1,'one query and one original continuation')
    if c['counted']=='sanity':check(c['collections']==0 and c['ancestor_links']>0,'positive sanity has no collection')
    else:
     check(c['ancestor_links']==data['ancestor_links'],'full negative original path once')
     check(c['collections']==1 and c['collection_links']==min(128,data['ancestor_links']),'one bounded post-negative collection')
     check(c['copies']==1,'single present ID copied once')
   else:
    check(c['admitted']>0 and c['collections']>0,'eligible workload builds after negatives')
    check(c['continuations']>0 and c['continuations']<=c['admitted'],'continuations bounded by queries')
    check(c['collection_links']<=128*c['collections'] and c['copies']<=64*c['collections'],'collection logical caps')
  results[name]['PASS']=True
 except Exception as e:
  results[name]['PASS']=False;results[name]['error']=repr(e);(out/'results.json').write_text(json.dumps(results,indent=2));raise
 (out/'results.json').write_text(json.dumps(results,indent=2))
print('20 counted controls verified; elapsed times ignored.')
