#!/usr/bin/env python3
"""H9 actual CSS -> native PNG and report metrics; no browser equivalence claim."""
import pathlib,subprocess,tempfile,sys,hashlib
binary=str(pathlib.Path(sys.argv[1]).resolve())
temp=None
if len(sys.argv)>2:
 root=pathlib.Path(sys.argv[2]);root.mkdir()
else:
 temp=tempfile.TemporaryDirectory(prefix='h9-');root=pathlib.Path(temp.name)
count=0
body='<div class="outer"><div class="box"><div class="a"></div><div class="b"></div><div class="a"></div><div class="b"></div></div></div>'
base='body{margin:0;background-color:#ffffff}.outer{width:280px;height:140px}.box{width:240px;display:flex;flex-wrap:wrap;align-content:flex-start;align-items:flex-start;background-color:#00ff00}.a{width:40px;height:20px;flex:none;background-color:#ff0000}.b{width:60px;height:20px;flex:none;background-color:#0000ff}'
def run(css,ok=True,content=body):
 global count
 count+=1;source=root/f'{count}.html';out=root/str(count)
 source.write_text('<html><head><style>'+base+css+'</style></head><body>'+content+'</body></html>')
 p=subprocess.run([binary,'--geometry',str(source),str(out),'300','160','1'],capture_output=True,text=True)
 log=(out/'report.log').read_text()
 assert p.returncode==(0 if ok else 3),(count,css,log)
 for text in ['css_eligibility=Unknown','chromium=NOT_RUN','authored_css_validation=NOT_RUN','flexbox_semantic_acceptance=false']:assert text in log
 fields=dict(line.split('=',1) for line in log.splitlines() if '=' in line)
 for key in ['child_cpu_seconds','child_peak_rss_kib','process_wall_ms']:assert float(fields[key])>=0
 if not ok:
  assert not (out/'aether.png').exists();return None
 return (out/'aether.png').read_bytes()
def same(actual,expected):
 a=run(actual);b=run(expected);assert a==b,(actual,expected)
for direction in ['row','row-reverse','column','column-reverse']:
 for wrap in ['wrap','nowrap']:
  for height,row in [('',10),('height:100px;',12)]:
   pre=f'.box{{flex-direction:{direction};flex-wrap:{wrap};{height}'
   same(pre+'gap:calc(10px + 2%)}',pre+f'gap:{row}px 14.8px'+'}')
for size,row in [('min-height:100px;',10),('height:50%;',11.4)]:
 same('.box{'+size+'gap:calc(10px + 2%)}','.box{'+size+f'gap:{row}px 14.8px'+'}')
same('.box{padding:10px;border:2px solid red;gap:calc(10px + 2%)}','.box{padding:10px;border:2px solid red;gap:10px 14.8px}')
same('.box{box-sizing:border-box;padding:10px;border:2px solid red;gap:calc(10px + 2%)}','.box{box-sizing:border-box;padding:10px;border:2px solid red;gap:10px 14.32px}')
for value in ['calc(10px + 2%)','CALC(10px + 2%)',r'c\61 lc(10px + 2%)','calc(10px /*comment*/ + 2%)','calc((20px + 4%) / 2)',r'calc(10p\78  + 2%)']:
 same('.box{gap:'+value+'}', '.box{gap:10px 14.8px}')
same(r'.box{g\61 p:calc(10px + 2%)}','.box{gap:10px 14.8px}')
same('.box{gap:calc(-10px + 2%)}','.box{gap:0}')
same('.box{gap:calc(3px + 2%) calc(7px + 1%)}','.box{gap:3px 9.4px}')
for value in ['min(10px,2%)','max(10px,2%)','clamp(1px,2%,10px)','calc(min(1px,2px) + 2%)','calc(10em + 2%)','calc(1px+2%)','calc(1px / 0)','calc(1e999px + 2%)','calc(10px + 2%) 3px 4px','calc(1px + 2%) -1px']:
 run('.box{gap:'+value+'}',False)
# var() stays outside this source-level CLI profile; native tests cover it.
run('.box{--g:calc(10px + 2%);gap:var(--g)}',False)
run('.box{writing-mode:vertical-rl;gap:calc(10px + 2%)}',False)
original=pathlib.Path('tests/visual_compare/fixtures/flexbox_runtime_validation.html')
assert hashlib.sha256(original.read_bytes()).hexdigest()=='eab17055687df3296d8b911b0217dfb0429c306a6aa57ab49895937a6a34b837'
out=root/'original';p=subprocess.run([binary,'--geometry',str(original),str(out),'300','160','1'],capture_output=True)
assert p.returncode==3 and not (out/'aether.png').exists()
assert 'TEXT_RENDERER_UNAVAILABLE' in (out/'report.log').read_text()
print(f'PASS H9 {count} CSS/PNG/error/metric cases; original rejected unchanged')
