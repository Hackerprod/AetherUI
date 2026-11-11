#!/usr/bin/env python3
"""One untimed-oracle smoke per arm/input, outside both frozen campaigns.
Harness clocks are emitted but never used as A/B samples or a performance claim.
"""
import argparse,json,pathlib,subprocess,sys
from run_campaigns import HERE,semantic_signature,parse_report,sha,FIXTURE_SHA
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--A',type=pathlib.Path,required=True);p.add_argument('--B',type=pathlib.Path,required=True);p.add_argument('--fixture',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,required=True);a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=False);out=a.output.resolve();material=out/'inputs';material.mkdir();assert sha(a.fixture)==FIXTURE_SHA
results={}
for path in sorted((HERE/'control-inputs').glob('*.json')):
 data=json.loads(path.read_text());name=data['name'];html=material/(name+'.html');css=material/(name+'.css');html.write_text(data['html']);css.write_text(data.get('author_css',data.get('selector_source','')))
 results[name]={}
 for arm,directory in [('A',a.A),('B',a.B)]:
  target=out/(name+'-'+arm);target.mkdir()
  args=[str(directory.resolve()/('private' if name.startswith('Q') else 'public'))]
  args+=([str(html),str(css),str(target),str(data['ancestor_links'])] if name.startswith('Q') else [name,str(html),str(css),str(target),str(data['subject_count'])])
  r=subprocess.run(args,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=30);(target/'stdout.log').write_bytes(r.stdout);(target/'stderr.log').write_bytes(r.stderr)
  results[name][arm]={'exit_status':r.returncode,'command':args}
  if r.returncode: (out/'FAILED.json').write_text(json.dumps(results,indent=2));raise SystemExit('Smoke failure, no retry: '+name+' '+arm)
  results[name][arm]['semantic_sha256']=semantic_signature(target/'report.log')
 results[name]['equal']=results[name]['A']['semantic_sha256']==results[name]['B']['semantic_sha256']
 if not results[name]['equal']:(out/'FAILED.json').write_text(json.dumps(results,indent=2));raise SystemExit('Mismatch '+name)
name='W1440';results[name]={}
for arm,directory in [('A',a.A),('B',a.B)]:
 target=out/(name+'-'+arm);target.mkdir();args=[str(directory.resolve()/'public'),name,str(a.fixture.resolve()),'-',str(target),'0']
 r=subprocess.run(args,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=30);(target/'stdout.log').write_bytes(r.stdout);(target/'stderr.log').write_bytes(r.stderr)
 results[name][arm]={'exit_status':r.returncode,'command':args}
 if r.returncode:(out/'FAILED.json').write_text(json.dumps(results,indent=2));raise SystemExit('Smoke failure W '+arm)
 results[name][arm]['semantic_sha256']=semantic_signature(target/'report.log')
results[name]['equal']=results[name]['A']['semantic_sha256']==results[name]['B']['semantic_sha256']
(out/'results.json').write_text(json.dumps(results,indent=2));assert results[name]['equal']
(out/'expected_controls.json').write_text(json.dumps({k:v['A']['semantic_sha256'] for k,v in results.items()},indent=2))
print('21 controls, both arms, semantic signatures equal. No campaign samples.')
