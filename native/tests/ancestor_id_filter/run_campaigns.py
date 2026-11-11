#!/usr/bin/env python3
"""Frozen v3 execution. Plan by default. --execute requires reviewed readiness file.
Never replaces an output directory or retries a process, including infrastructure failures.
"""
import argparse, datetime, hashlib, json, math, os, pathlib, re, subprocess, sys, time
HERE=pathlib.Path(__file__).resolve().parent
FIXTURE_SHA='eab17055687df3296d8b911b0217dfb0429c306a6aa57ab49895937a6a34b837'
PROTOCOL_SHA='06c96c6c366fc734039d3f6f5cb4244b4619a8d0dc5d595cff5e8cc7937e456e'
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def plan(protocol):
    cells={c['name']:c for c in protocol['cells']}
    for campaign in (1,2):
        for name in protocol['campaign_cell_order'][str(campaign)]:
            for i,arm in enumerate('ABBA'):
                yield dict(campaign=campaign,cell=name,warmup=True,block=-1,position=i,arm=arm)
            for block in range(cells[name]['pairs']//2):
                order='ABBA' if (block+campaign)%2 else 'BAAB'
                for pos,arm in enumerate(order):
                    yield dict(campaign=campaign,cell=name,warmup=False,block=block,position=pos,arm=arm)
def semantic_signature(path):
    # Keep public widths/opacity, update counts and private results. Strip only time.
    lines=[]
    for line in path.read_text().splitlines():
        if line.startswith(('width=','opacity=','measured_count=','false_count=','ancestor_links=')):
            lines.append(line)
        elif line.startswith('iteration='):
            lines.append(re.sub(r',ms=[^,]+','',line))
    return hashlib.sha256(('\n'.join(lines)+'\n').encode()).hexdigest()

def parse_report(path):
    text=path.read_text(); result={}
    for line in text.splitlines():
        if '=' not in line:continue
        key,value=line.split('=',1)
        try:result[key]=float(value)
        except ValueError:pass
    result['RSS']=max([int(v) for v in re.findall(r'peak_rss_kib=(\d+)',text)],default=None)
    aliases={'pipeline':'native_pipeline_wall_ms','style':'style_resolve_subtree_ms','layout':'layout_ms','display_list':'display_list_build_ms','invariants_export':'invariants_export_ms','private_match_operation':'private_match_operation_ms'}
    for key,value in aliases.items():
        if value in result:result[key]=result[value]
    if 'style_mean_ms' in result:result['style']=result['style_mean_ms']
    result['destruction']=None # Original P harness does not time destruction separately.
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--config',type=pathlib.Path,required=True,help='arm executable/environment identities and readiness checks')
    p.add_argument('--output',type=pathlib.Path,required=True)
    p.add_argument('--execute',action='store_true')
    args=p.parse_args();cfg=json.loads(args.config.read_text())
    protocol=json.loads((HERE/'frozen_protocol.json').read_text())
    assert sha(HERE/'frozen_protocol.json')==PROTOCOL_SHA
    manifest=json.loads((HERE/'control_inputs_manifest.json').read_text())
    for entry in manifest:assert sha(HERE/'control-inputs'/f"{entry['name']}.json")==entry['sha256']
    fixture=pathlib.Path(cfg['fixture']).resolve();assert sha(fixture)==FIXTURE_SHA
    schedule=list(plan(protocol));assert sum(not r['warmup'] for r in schedule)==1080;assert sum(r['warmup'] for r in schedule)==192
    if not args.execute:
        print(json.dumps({'measured':1080,'warmups':192,'schedule':schedule},indent=2));return
    required=['independent_candidate_review_pass','semantic_equivalence_pass','counted_controls_pass','scratch_memory_pass','sanitizers_pass','no_local_heavy_work','parent_campaign_authorized']
    for key in required:
        if cfg.get('readiness',{}).get(key) is not True:raise RuntimeError('Readiness missing: '+key)
    required_counters={'dom_nodes','case_inventory','css_rules','style_updates','layout_boxes','display_commands','layout_text_measure_calls','layout_text_measure_bytes','paint_text_measure_calls','paint_text_measure_bytes'}
    for cell in ('P1440','P390','P768'):
        assert required_counters <= set(cfg['expected_P'][cell]['counters'])
    # Use a new directory only, preserving failed and incomplete runs permanently.
    args.output.mkdir(parents=True,exist_ok=False)
    out=args.output.resolve();(out/'identity.json').write_text(json.dumps(cfg,indent=2))
    (out/'schedule.json').write_text(json.dumps(schedule,indent=2))
    identities={'protocol':sha(HERE/'frozen_protocol.json'),'fixture':sha(fixture),'harness_files':{p.name:sha(p) for p in HERE.iterdir() if p.is_file()},'binaries':{},'started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'uname':list(os.uname())}
    for arm in ('A','B'):
        for kind in ('P','public','private'):
            executable=pathlib.Path(cfg['arms'][arm][kind]).resolve()
            expected=cfg['arms'][arm]['sha256'][kind]
            assert sha(executable)==expected
            identities['binaries'][arm+':'+kind]=expected
    (out/'manifest.json').write_text(json.dumps(identities,indent=2))
    material=out/'inputs';material.mkdir()
    for entry in manifest:
        name=entry['name'];data=json.loads((HERE/'control-inputs'/f'{name}.json').read_text())
        (material/f'{name}.html').write_text(data['html'])
        (material/f'{name}.css').write_text(data.get('author_css',data.get('selector_source','')))
    with (out/'samples.jsonl').open('x',buffering=1) as samples:
        for index,row in enumerate(schedule):
            name=row['cell'];arm=row['arm'];spec=cfg['arms'][arm];target=out/f'{index:04d}-C{row["campaign"]}-{name}-{arm}';target.mkdir()
            if name.startswith('P'):
                command=[spec['P'],str(fixture),str(target),name[1:],'900']
            elif name=='W1440':
                command=[spec['public'],name,str(fixture),'-',str(target),'0']
            else:
                data=json.loads((HERE/'control-inputs'/f'{name}.json').read_text())
                if name.startswith('S'):command=[spec['public'],name,str(material/f'{name}.html'),str(material/f'{name}.css'),str(target),str(data['subject_count'])]
                else:command=[spec['private'],str(material/f'{name}.html'),str(material/f'{name}.css'),str(target),str(data['ancestor_links'])]
            command[0]=str(pathlib.Path(command[0]).resolve())
            env=os.environ.copy();env.update(spec.get('env',{}))
            started=datetime.datetime.now(datetime.timezone.utc).isoformat();begin=time.perf_counter_ns()
            record={**row,'index':index,'command':command,'started_utc':started}
            try:
                result=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=30,env=env)
                elapsed=(time.perf_counter_ns()-begin)/1e6
                (target/'stdout.log').write_bytes(result.stdout);(target/'stderr.log').write_bytes(result.stderr)
                record.update(exit_status=result.returncode,process_elapsed_ms=elapsed,finished_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
                metrics=parse_report(target/'report.log');metrics['process']=elapsed;record['metrics']=metrics
                record['report_sha256']=sha(target/'report.log')
                if (target/'geometry.csv').exists():record['geometry_sha256']=sha(target/'geometry.csv')
                if result.returncode or metrics.get('failures')!=0:raise RuntimeError('nonzero status or semantic failure')
                if name.startswith(('S','Q')):assert metrics.get('measured_count')==16
                if name=='W1440':assert metrics.get('measured_count')==8
                if name.startswith('Q'):assert metrics.get('false_count')==16
                if not name.startswith('P'):
                    record['semantic_sha256']=semantic_signature(target/'report.log')
                    assert record['semantic_sha256']==cfg['expected_controls'][name]
                if name.startswith('P'):
                    assert all(metrics.get(k) is not None and math.isfinite(metrics[k]) and metrics[k]>0 for k in ('pipeline','style','RSS'))
                    expected=cfg['expected_P'][name]
                    assert record.get('geometry_sha256')==expected['geometry_sha256']
                    for key,value in expected['counters'].items():assert metrics.get(key)==value,(key,metrics.get(key),value)
                record['status']='PASS'
            except Exception as e:
                record['status']='NO PASS';record['error']=repr(e)
                if isinstance(e,subprocess.TimeoutExpired):
                    (target/'stdout.log').write_bytes(e.stdout or b'');(target/'stderr.log').write_bytes(e.stderr or b'')
                    record['exit_status']='TIMEOUT';record['process_elapsed_ms']=(time.perf_counter_ns()-begin)/1e6
                samples.write(json.dumps(record)+'\n');(out/'STOPPED.json').write_text(json.dumps(record,indent=2));raise
            samples.write(json.dumps(record)+'\n')
    (out/'COMPLETE.json').write_text(json.dumps({'measured':1080,'warmup':192,'status':'EXECUTION COMPLETE, statistical gates pending'},indent=2))
if __name__=='__main__':main()
