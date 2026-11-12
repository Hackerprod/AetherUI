#!/usr/bin/env python3
"""H14 scoped native var resolution; native PNG controls, not Chromium."""
import pathlib,sys,subprocess,json
binary=str(pathlib.Path(sys.argv[1]).resolve());root=pathlib.Path(sys.argv[2]);root.mkdir();count=0
base='body{margin:0;background-color:white}.row{display:flex;width:300px;height:60px;align-items:flex-start}.a,.b{width:40px;height:30px;background-color:red}.b{background-color:blue}'
body='<div class="row"><div class="a"></div><div class="b"></div></div>'
results=[]
def run(css,ok=True,content=body):
 global count
 count+=1;src=root/f'{count}.html';out=root/str(count);src.write_text('<html><head><style>'+base+css+'</style></head><body>'+content+'</body></html>')
 p=subprocess.run([binary,'--geometry',str(src),str(out),'320','100','1'],capture_output=True,text=True);log=(out/'report.log').read_text()
 assert p.returncode==(0 if ok else 3),(count,css,log)
 fields=dict(x.split('=',1) for x in log.splitlines() if '=' in x)
 for key in ['child_cpu_seconds','child_peak_rss_kib','process_wall_ms']:assert float(fields[key])>=0
 assert 'chromium=NOT_RUN' in log and 'authored_css_validation=NOT_RUN' in log
 results.append({'case':count,'expected':ok,'metrics':{k:fields[k] for k in ['child_cpu_seconds','child_peak_rss_kib','process_wall_ms']}})
 if not ok:assert not(out/'aether.png').exists();return
 return(out/'aether.png').read_bytes()
def same(css,explicit,content=body):assert run(css,content=content)==run(explicit,content=content),(css,explicit)
same('.row{--item-flex:1 1 0}.a,.b{flex:var(--item-flex)}','.a,.b{flex:1 1 0}')
same('.a{flex:0 1 var(--missing-basis,120px)}','.a{flex:0 1 120px}')
same('.row{--x:120px}.a{flex-basis:var(--x)}','.a{flex-basis:120px}')
same('.row{--x:1 1 0}.a{flex:var(--x)}.b{--x:0 0 100px;flex:var(--x)}','.a{flex:1 1 0}.b{flex:0 0 100px}')
same('.row{--x:100px;--y:var(--x)}.a{flex:0 0 var(--y)}','.a{flex:0 0 100px}')
same('.a{flex:var(--x,var(--y,0 0 80px))}', '.a{flex:0 0 80px}')
same('.a{--X:0 0 110px;--x:0 0 70px;flex:var(--X)}','.a{flex:0 0 110px}')
same(r'.a{--\78 :0 0 110px;flex:v\61 r(--x)}','.a{flex:0 0 110px}')
same('.a{--x:0/**/ 0/**/ 90px;flex:var(--x)}','.a{flex:0 0 90px}')
same('.a{--x:0 0 60px;flex:var(--x)!important;flex:0 0 100px}', '.a{flex:0 0 60px}')
same('.a{--x:1 1 0;flex:var(--x);flex-grow:2}.b{flex:1 1 0}', '.a{flex:2 1 0}.b{flex:1 1 0}')
same('.a{--x:1 1 0;flex-grow:2!important;flex:var(--x)}.b{flex:1 1 0}', '.a{flex:2 1 0}.b{flex:1 1 0}')
same('.a{--x:0 0 50%;flex:var(--x)}','.a{flex:0 0 50%}')
same('.a{--x:auto;flex:var(--x)}','.a{flex:auto}')
same('.a{--x:none;flex:var(--x)}','.a{flex:none}')
same('.a{flex:var(--absent,initial)}','.a{flex:initial}')
same('.a{}','.a{flex:0 0 90px}', '<div class="row"><div class="a" style="--x:0 0 90px;flex:var(--x)"></div><div class="b"></div></div>')
same('html{--x:0 0 90px}.a{flex:var(--x)}','.a{flex:0 0 90px}')
same('.row{--x:0 0 100px!important}.a{--x:0 0 80px;flex:var(--x)}','.a{flex:0 0 80px}')
same('@media (min-width:200px){.a{--x:0 0 90px;flex:var(--x)}}','.a{flex:0 0 90px}')
same('.a{--x:0 0 90px;flex:var(--x)}.b{--x:0 0 60px;flex:var(--x)}','.a{flex:0 0 90px}.b{flex:0 0 60px}')
for css in [
 '.a{--x:;flex:var(--x,0 0 90px)}', '.a{flex:0 0 90px;flex:var(--missing)}',
 '.a{--x:var(--x);flex:var(--x,0 0 90px)}', '.a{--x:var(--y);--y:var(--x);flex:var(--x)}',
 '.a{--x:red;flex:var(--x)}', '.a{--x:0 0 10em;flex:var(--x)}',
 '.a{--x:calc(10px + 2px);flex:var(--x)}', '.a{--x:0 0 80px;flex:var(--x,calc(10px + 2px))}',
 '.a{--x:url(never);flex:var(--x);flex:0 0 10px}',
 '#absent{--x:url(never)}.a{flex:var(--missing,0 0 10px)}',
 '.a{--x:10px;width:var(--x)}', '.a{--x:10px;gap:var(--x)}',
 '.a{--x:0 0 1e999px;flex:var(--x)}', '.a{--x:0 0 1e50px;flex:var(--x)}', '.a{--x:0 0 -1px;flex:var(--x)}',
 '.a{--x:0 0 10px;flex:var(--x,unknown())}',
 '@media print{.a{--x:url(never)}}.a{flex:var(--m,0 0 10px)}',
 '.a{flex:var(x,0 0 10px)}', '.a{flex:var(--x,0 0 10px}',
 '.a{--x:0 0 80px;flex:var(--x,fit-content(2px))}',
]:run(css,False)
# Native limits are fail-closed, never turned into a permitted fallback.
chain=''.join(f'--v{i}:var(--v{i+1});' for i in range(40))+'--v40:0 0 80px;flex:var(--v0,0 0 10px)'
run('.a{'+chain+'}',False)
expansion='--v0:1;'+''.join(f'--v{i}:var(--v{i-1}) var(--v{i-1});' for i in range(1,14))+'flex:var(--v13,0 0 10px)'
run('.a{'+expansion+'}',False)
(root/'summary.json').write_text(json.dumps(results,indent=2))
print('H14 CLI PASS',count)
