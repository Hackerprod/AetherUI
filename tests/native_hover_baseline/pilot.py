#!/usr/bin/env python3
"""All execution including canonical checks and calibration shares one <=90 s deadline."""
import csv, json, os, pathlib, platform, statistics, subprocess, sys, time
build=pathlib.Path(sys.argv[1]).resolve(); out=pathlib.Path(sys.argv[2]);out.mkdir(exist_ok=False)
start=time.monotonic();deadline=start+85
meta={'kind':'diagnostic-native-only','hard_limit_seconds':90,'execution_limit_seconds':85,'cpu_quota':'unknown','memory_quota':'unknown','host_comparison':False,'seed':'deterministic index order; no PRNG','build':str(build),'warmup':'two native resolutions per fixture before transition, nochange follows transition','completed_blocks':0,'coverage':'partial'}
meta['logical_cpu_count']=os.cpu_count();meta['affinity_cpu_count']=len(os.sched_getaffinity(0)) if hasattr(os,'sched_getaffinity') else 'unknown';meta['frequency_governor']='unknown';meta['shared_host_isolation']='unknown';meta['platform']=platform.platform();meta['compiler']=subprocess.check_output(['c++','--version'],text=True).splitlines()[0];meta['commit']=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip();meta['working_tree_dirty']=bool(subprocess.check_output(['git','status','--porcelain'],text=True).strip())
raw=open(out/'raw.csv','w'); raw.write('record,mode,cards,scenario,phase,repeats,updates,wall_s,user_s,system_s,process_peak_rss_kib,state_calls,state_changes,compute_original,compute_pseudo,candidates,matcher_calls,inline_compiles,preparation_ns,matching_unavailable_ns,compute_inclusive_ns,freeze_ns,backing_ns,updates_finalize_ns,publication_ns,replay_ns,cleanup_success_ns,commit_total_ns,resolve_api_ns,consume_free_ns,frozen_records,revision_entries,retired_entries,update_copies,state_api_ns,block\n');raw.flush()
def run(mode,args):
 remaining=deadline-time.monotonic()
 if remaining<=0:raise TimeoutError('global pilot deadline')
 try:return subprocess.run([str(build/f'hover-{mode}'),*map(str,args)],capture_output=True,text=True,timeout=remaining,check=True).stdout
 except subprocess.TimeoutExpired as e:raise TimeoutError('global pilot deadline') from e
rows=[]
try:
 for sc in range(7):
  expected=None
  for mode in range(3):
   value=run(mode,['--canonical',18,sc]);(out/f'canonical-{sc}-{mode}.txt').write_text(value)
   if expected is None:expected=value
   elif value!=expected:raise RuntimeError(f'canonical mismatch {sc}/{mode}')
 meta['canonical']='PASS full serialized corpus outputs agree across three configs; unsupported payloads fail closed'
 # Calibration is not a result block; one fixed 1800-card case, no per-mode tuning.
 t=time.monotonic();run(0,['--measure',1800,0,2]);cal=time.monotonic()-t
 steps=2;meta['calibration_seconds']=cal;meta['frozen_transitions']=steps
 for block in range(5):
  modes=[0,1,2] if block%2==0 else [2,1,0]
  cells=[(n,s) for n in (18,180,1800) for s in range(7)]
  if block%2:cells.reverse()
  for n,sc in cells:
   for mode in modes:
    result=run(mode,['--measure',n,sc,steps])
    for line in result.splitlines():
     if line.startswith('row,'):
      raw.write(f'{line},{block}\n');raw.flush();row=next(csv.reader([line]));rows.append((block,row))
  meta['completed_blocks']+=1
 meta['coverage']='complete 5 blocks x 21 cells x 3 modes'
except TimeoutError as e:meta['stop_reason']=str(e)
except Exception as e:meta['error']=str(e)
finally:
 raw.close();meta['elapsed_seconds']=time.monotonic()-start
 # Pair only identically covered cells. Report measured overhead, never corrected times.
 grouped={}
 for block,row in rows:grouped.setdefault((row[2],row[3],row[4]),{}).setdefault(row[1],{})[block]=float(row[7])/int(row[5])
 summary=[]
 for cell,m in grouped.items():
  if '0' not in m:continue
  for mode,values in m.items():
   paired=sorted(set(values)&set(m['0']))
   if not paired:continue
   samples=[values[b] for b in paired];diffs=[values[b]-m['0'][b] for b in paired];ratios=[values[b]/m['0'][b]-1 for b in paired if m['0'][b]]
   summary.append({'cards':int(cell[0]),'scenario':int(cell[1]),'phase':int(cell[2]),'mode':int(mode),'paired_blocks':paired,'wall_per_resolve_median_s':statistics.median(samples),'wall_min_s':min(samples),'wall_max_s':max(samples),'vs_off_paired_absolute_median_s':statistics.median(diffs),'vs_off_paired_relative_median':statistics.median(ratios) if ratios else None})
 (out/'summary.json').write_text(json.dumps({'run':meta,'measurements':summary},indent=2))
 print(json.dumps(meta,indent=2))
 if 'error' in meta:sys.exit(2)
