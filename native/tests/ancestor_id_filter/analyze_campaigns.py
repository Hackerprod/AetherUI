#!/usr/bin/env python3
"""No pooling: every required v3 cell/metric/campaign gates independently."""
import argparse, json, math, pathlib, random, statistics
from run_campaigns import HERE, plan
METRICS={'pipeline':0,'style':1,'layout':2,'display_list':3,'process':4,'invariants_export':5,'destruction':6,'RSS':7,'private_match_operation':8}
median=statistics.median

def estimate(blocks,metric,campaign,cell_index,gate):
    def arms(bs):
        return ([r['metrics'][metric] for b in bs for r in b if r['arm']=='A'],[r['metrics'][metric] for b in bs for r in b if r['arm']=='B'])
    def statistic(a,b):
        ma,mb=median(a),median(b)
        if gate=='ratio':return mb/ma
        if gate=='secondary':return (mb-ma)-max(.03*ma,2.)
        if gate=='small':return (mb-ma)-max(.03*ma,.2)
        if gate=='rss':return (mb-ma)-max(.02*ma,2048.)
        return mb-ma
    a,b=arms(blocks); ma,mb=median(a),median(b)
    rng=random.Random(20261111+1000*campaign+10*cell_index+METRICS[metric]); draws=[]; k=len(blocks)
    for _ in range(10000):
        aa,bb=arms([blocks[rng.randrange(k)] for _ in range(k)])
        draws.append(statistic(aa,bb))
    draws.sort()
    paired=[]
    for block in blocks:
        indices=((0,1),(3,2)) if block[0]['arm']=='A' else ((1,0),(2,3))
        for ai,bi in indices:paired.append(block[bi]['metrics'][metric]-block[ai]['metrics'][metric])
    return dict(median_A=ma,median_B=mb,ratio=mb/ma if ma else None,absolute_change=mb-ma,
        absolute_savings=ma-mb,paired_median_difference=median(paired),range_A=[min(a),max(a)],range_B=[min(b),max(b)],
        statistic=gate,point=statistic(a,b),CI95=[draws[249],draws[9749]],seed=20261111+1000*campaign+10*cell_index+METRICS[metric])

def analyze(directory):
    # A complete-looking row set cannot override a run/identity failure.
    for name in ('STOPPED.json','execution-failure.json','identity-failure.json'):
        if (directory/name).exists():raise ValueError('campaign failure marker: '+name)
    completion=json.loads((directory/'COMPLETE.json').read_text())
    if completion.get('measured')!=1080 or completion.get('warmup')!=192:
        raise ValueError('invalid completion marker')
    for phase in ('begin','end'):
        linkage=json.loads((directory/('linkage-'+phase+'.json')).read_text())
        if linkage.get('phase')!=phase or linkage.get('PASS') is not True or linkage.get('errors'):
            raise ValueError('shared linkage not verified: '+phase)
    protocol=json.loads((HERE/'frozen_protocol.json').read_text());schedule=list(plan(protocol))
    rows=[json.loads(l) for l in (directory/'samples.jsonl').read_text().splitlines()]
    if len(rows)!=len(schedule):raise ValueError('missing samples: NO PASS, no replacement permitted')
    for row,expected in zip(rows,schedule):
        assert all(row[k]==v for k,v in expected.items()),'schedule mismatch'
        assert row['status']=='PASS','failed process'
    results=[];overall=True
    for campaign in (1,2):
        for cell in protocol['cells']:
            name=cell['name']; data=[r for r in rows if r['campaign']==campaign and r['cell']==name and not r['warmup']]
            blocks=[data[i:i+4] for i in range(0,len(data),4)]
            assert len(blocks)==cell['pairs']//2
            required={'pipeline':'ratio','style':'ratio','RSS':'rss'} if name=='P1440' else ({'pipeline':'secondary','RSS':'rss'} if name.startswith('P') else {'private_match_operation' if name.startswith('Q') else 'style':'small'})
            report={'campaign':campaign,'cell':name,'processes':len(data),'metrics':{},'PASS':True}
            metrics=list(METRICS) if name.startswith('P') else list(required)
            for metric in metrics:
                values=[r['metrics'].get(metric) for r in data]
                if any(v is None or not math.isfinite(v) for v in values):
                    report['metrics'][metric]={'status':'NOT OBSERVED'}
                    if metric in required:report['PASS']=False
                    continue
                gate=required.get(metric,'difference');result=estimate(blocks,metric,campaign,cell['index'],gate)
                if metric in required:
                    if gate=='ratio':passed=result['ratio'] <= (.95 if metric=='pipeline' else .90) and result['CI95'][1]<1
                    else:passed=result['point']<=0 and result['CI95'][1]<=0
                    result['PASS']=passed;report['PASS'] &= passed
                report['metrics'][metric]=result
            results.append(report);overall &= report['PASS']
    return {'protocol_version':3,'measured':1080,'excluded_warmups':192,'overall':'PASS' if overall else 'NO PASS','campaigns_pooled':False,'results':results}
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=pathlib.Path);args=p.parse_args()
    try:result=analyze(args.directory)
    except Exception as e:result={'overall':'NO PASS','error':repr(e)}
    output=args.directory/'analysis.json'
    if output.exists():raise SystemExit('Refusing to overwrite existing analysis')
    output.write_text(json.dumps(result,indent=2));print(result['overall'])
    raise SystemExit(0 if result['overall']=='PASS' else 1)
