#!/usr/bin/env python3
"""Compare prebuilt base-0/base-1/off/effect/both executables; no network or build."""
import csv, pathlib, statistics, subprocess, sys, math
build=pathlib.Path(sys.argv[1]); out=pathlib.Path(sys.argv[2]); out.mkdir(parents=True,exist_ok=True)
rows=[]
variants=['base-0','off','base-1','effect','both']
for cards in [18,1800]:
 for repeat in range(9):
  for variant in variants if repeat%2==0 else list(reversed(variants)):
   data=subprocess.check_output([str(build/variant),str(cards)],text=True)
   rows.extend(dict(variant=variant,repeat=repeat,**row) for row in csv.DictReader(data.splitlines()))
with (out/'cost.csv').open('w') as f:
 w=csv.DictWriter(f,fieldnames=rows[0].keys());w.writeheader();w.writerows(rows)
with (out/'cost-summary.txt').open('w') as f:
 f.write('Native diagnostic only; requested bytes, not RSS. 9 samples; nearest-rank p95. Probe=1. No reuse.\n')
 for cards in [18,1800]:
  for phase in range(3):
   f.write(f'cards={cards} phase={phase} (cold=0 nochange=1 hover=2)\n')
   for variant in variants:
    selected=[r for r in rows if r['variant']==variant and int(r['cards'])==cards and int(r['phase'])==phase]
    times=sorted(int(r['wall_ns'])/1e6 for r in selected)
    f.write(f' {variant}: median_ms={statistics.median(times):.3f} p95_ms={times[math.ceil(.95*len(times))-1]:.3f}; '+', '.join(f'{k}={statistics.median(int(r[k]) for r in selected):.0f}' for k in ['allocation_calls','requested_bytes','live_delta','peak_above_start','compute_original'])+'\n')
# OFF must have exactly the same memory counters as its R0b OFF predecessor.
keys=['allocation_calls','requested_bytes','live_delta','peak_above_start','compute_original']
for r in rows:
 if r['variant']=='off':
  b=next(x for x in rows if x['variant']=='base-0' and all(x[k]==r[k] for k in ['repeat','phase','cards']))
  assert all(r[k]==b[k] for k in keys),(r,b)
print('PASS OFF counters exactly equal R0b OFF; cost.csv and cost-summary.txt written')
