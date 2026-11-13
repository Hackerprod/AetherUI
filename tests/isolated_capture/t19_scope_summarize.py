"""Exact semantic checks plus median/min/max; not a performance threshold gate."""
import csv, pathlib, statistics, sys
root = pathlib.Path(sys.argv[1])
sets = {kind:list(csv.DictReader((root/(kind+'.csv')).open())) for kind in ('baseline','candidate')}
assert len(sets['baseline']) == len(sets['candidate'])
for a,b in zip(sets['baseline'],sets['candidate']):
    for key in ('case','sample','phase','output_hash','diagnostics_hash'):
        assert a[key] == b[key], (key,a,b)
    if '_f0_' in b['case']:
        assert all(int(b[k]) == 0 for k in ('queries','scopes','terminals'))
    assert int(b['queries']) <= int(b['nodes'])
    assert int(b['terminals']) <= int(a['terminals'])
    if '_f0_' not in b['case'] and b['case'].endswith('_invalid1'):
        assert int(b['scopes']) == int(b['queries']), b
        assert int(b['uncertain_exits']) == int(b['queries']), b
with (root/'summary.csv').open('w') as f:
    writer=csv.writer(f)
    writer.writerow(('implementation','case','phase','wall_median_ms','wall_min_ms','wall_max_ms','cpu_median_ms','cpu_min_ms','cpu_max_ms','process_peak_rss_max_kib','queries','scopes','terminals','uncertain_exits'))
    for kind,rows in sets.items():
        groups={}
        for row in rows: groups.setdefault((row['case'],row['phase']),[]).append(row)
        for (case,phase),group in groups.items():
            wall=[float(r['wall_ms']) for r in group];cpu=[float(r['cpu_ms']) for r in group]
            writer.writerow((kind,case,phase,statistics.median(wall),min(wall),max(wall),statistics.median(cpu),min(cpu),max(cpu),max(int(r['process_peak_rss_kib']) for r in group),*(group[0][k] for k in ('queries','scopes','terminals','uncertain_exits'))))
print('PASS: exact result/diagnostic hashes, lazy zero visits without deferred, <= one query per node, local invalid early exit.')
print('Wall: steady_clock; CPU: std::clock process CPU. RSS: Linux getrusage(RUSAGE_SELF).ru_maxrss KiB process high-water mark, NOT per-operation allocation or delta. Fixture construction excluded from wall/CPU but included in RSS lifetime.')
print('Seven independent fixtures per case/phase; output summary reports median/min/max. Baseline and candidate share every object except same-source scope-walk translation unit. Baseline uses full eager walk; candidate lazy memoized early exit. No-change prepare/commit in this runtime still recomputes nodes; counters report actual work. No render authorization changes.')
