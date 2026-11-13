#!/usr/bin/env python3
"""Run prebuilt r0a-cost-{0,1,2}/r0b-cost-{0,1,2} alternately. No build/install."""
import csv, subprocess, sys, pathlib, statistics
build=pathlib.Path(sys.argv[1]); destination=pathlib.Path(sys.argv[2]); destination.mkdir(parents=True,exist_ok=True)
rows=[]
for n in [18,1800]:
 for mode in range(3):
  for repeat in range(5):
   for variant in (['r0a','r0b'] if repeat%2==0 else ['r0b','r0a']):
    text=subprocess.check_output([str(build/f'{variant}-cost-{mode}'),str(n)],text=True)
    for row in csv.DictReader(text.splitlines()): rows.append(dict(variant=variant,repeat=repeat,**row))
with (destination/'cost.csv').open('w') as f:
 w=csv.DictWriter(f,fieldnames=rows[0].keys());w.writeheader();w.writerows(rows)
with (destination/'cost-summary.txt').open('w') as f:
 for n in [18,1800]:
  for mode in range(3):
   for phase in range(3):
    f.write(f'cards={n} mode={mode} phase={phase} (0=cold 1=nochange 2=transition)\n')
    for variant in ['r0a','r0b']:
     selected=[r for r in rows if r['variant']==variant and int(r['cards'])==n and int(r['mode'])==mode and int(r['phase'])==phase]
     times=[int(r['wall_ns'])/1e6 for r in selected]
     f.write(f' {variant} ms median={statistics.median(times):.3f} min={min(times):.3f} max={max(times):.3f}; '+', '.join(f'{k}={statistics.median([int(r[k]) for r in selected]):.0f}' for k in ['allocation_calls','requested_bytes','live_delta','peak_above_start','compute_original'])+'\n')
