#!/usr/bin/env python3
"""Manual H6 real CLI, pixels, metrics and fail-closed regressions. No Chromium."""
import pathlib, subprocess, tempfile, sys, struct, zlib, hashlib
binary=str(pathlib.Path(sys.argv[1]).resolve())
temp=None
if len(sys.argv)>2:
 root=pathlib.Path(sys.argv[2]);root.mkdir()
else:
 temp=tempfile.TemporaryDirectory(prefix='h6-');root=pathlib.Path(temp.name)
count=0

def run(css,body='<div class="box"></div>',width=1280,ok=True):
 global count
 count+=1;source=root/f'{count}.html';out=root/str(count)
 source.write_text('<html><head><style>body{margin:0;background-color:#ffffff}'+css+'</style></head><body>'+body+'</body></html>')
 p=subprocess.run([binary,'--geometry',str(source),str(out),str(width),'80','1'],capture_output=True,text=True)
 log=(out/'report.log').read_text()
 assert p.returncode==(0 if ok else 3),(count,css,log)
 assert 'css_eligibility=Unknown' in log and 'chromium=NOT_RUN' in log
 fields=dict(line.split('=',1) for line in log.splitlines() if '=' in line)
 for key in ['child_cpu_seconds','child_peak_rss_kib','process_wall_ms']:assert float(fields[key])>=0
 if not ok:
  assert not (out/'aether.png').exists() and 'stage_started=style' not in log
  return log
 b=(out/'aether.png').read_bytes();i=8;packed=b''
 while i<len(b):
  n=struct.unpack('>I',b[i:i+4])[0];kind=b[i+4:i+8];v=b[i+8:i+8+n];i+=n+12
  if kind==b'IDAT':packed+=v
 raw=zlib.decompress(packed);stride=width*4+1
 assert all(raw[y*stride]==0 for y in range(80))
 return lambda x,y:tuple(raw[y*stride+1+x*4:y*stride+5+x*4])


base='.box{width:20px;height:20px;background-color:#ff0000}'
for condition,active in [('(display:flex)',True),('not (display:flex)',False),('(display:initial)',True),('(display:inherit)',True),('(display:unset)',True),('(display:flex !important)',True),('(display:flex) and (display:block)',True),('(display:flex) or (display:none)',True),('((display:flex) and (display:block))',True),('(DISPLAY:FLEX)',True),(r'(d\69 splay:f\6c ex)',True),('(display:/**/flex)',True)]:
 p=run(base+'@supports '+condition+'{.box{border:6px solid #0000ff}}')
 assert p(0,0)==((0,0,255,255) if active else (255,0,0,255)),condition
 assert p(6,6)==(255,0,0,255),condition
 if active:assert p(25,5)==(0,0,255,255) and p(32,5)==(255,255,255,255)
p=run(base+r'@supp\6f rts (display:flex){.box{background-color:#0000ff}}');assert p(5,5)==(0,0,255,255)
for nesting in ['@supports (display:flex){@media screen{RULE}}','@media screen{@supports (display:flex){RULE}}']:
 p=run(base+nesting.replace('RULE','.box{background-color:#0000ff}'));assert p(5,5)==(0,0,255,255)
p=run(base+'@supports (display:flex){.box{background-color:#0000ff}}.box{background-color:#00ff00}');assert p(5,5)==(0,255,0,255)
p=run(base+'@supports (display:flex){.box{background-color:#0000ff!important}}.box{background-color:#00ff00}');assert p(5,5)==(0,0,255,255)
p=run(base+'@supports (display:flex){#x{background-color:#0000ff}}.box{background-color:#00ff00}', '<div class="box" id="x"></div>');assert p(5,5)==(0,0,255,255)
p=run(r'body{display:f\6c ex}'+base,'<div class="box"></div><div class="box"></div>');assert p(25,5)==(255,0,0,255)
for condition in [r'(display:\5c 66lex)',r'(display:\5c 69nitial)','(display:bogus)','(unknown:value)','(display:revert)','(display:revert-layer)','(display:line-break)','(width:1px)','selector(.box)','(future syntax)','not (future syntax)','(display:flex) or (future syntax)','(display:flex;display:block)','(display:flex;)','(display:flex) junk','(display:flex !important junk)','(display:flex) and (display:block) or (display:none)','(display:var(--x))']:
 run(base+'@supports '+condition+'{.box{background-color:#0000ff}}',ok=False)
for body in ['.box{background:url(secret)}','.box{width:var(--x)}','.box{width:fit-content(2px)}','@keyframes a{from{color:red}}']:
 run(base+'@supports not (display:flex){'+body+'}',ok=False)
for depth in [32,33]:
 css=base
 for i in range(depth):css=('@media screen{' if i%2 else '@supports (display:flex){')+css+'}'
 run(css,ok=depth==32)
run('@supports (display:flex){'+base,ok=False)
# No false semantic claim for the original, and fixture bytes stay unchanged.
original=pathlib.Path('tests/visual_compare/fixtures/flexbox_runtime_validation.html')
assert hashlib.sha256(original.read_bytes()).hexdigest()=='eab17055687df3296d8b911b0217dfb0429c306a6aa57ab49895937a6a34b837'
print(f'PASS H6 {count} real PNG/CLI cases, metrics and fail-closed evidence')
