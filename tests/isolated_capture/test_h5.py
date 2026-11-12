#!/usr/bin/env python3
"""Manual H5 real CLI, pixels, metrics and fail-closed regressions. No Chromium."""
import pathlib, subprocess, tempfile, sys, struct, zlib, hashlib
binary=str(pathlib.Path(sys.argv[1]).resolve())
temp=None
if len(sys.argv)>2:
 root=pathlib.Path(sys.argv[2]);root.mkdir()
else:
 temp=tempfile.TemporaryDirectory(prefix='h5-');root=pathlib.Path(temp.name)
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
for w in [699,700,701,1280]:
 p=run(base+'@media screen and (max-width:700px){.box{background-color:#0000ff}}',width=w)
 assert p(5,5)==((0,0,255,255) if w<=700 else (255,0,0,255))
for query,active in [('(width:1280px)',True),('(min-width:1280px)',True),('(min-width:1281px)',False),('(height:80px)',True),('(min-height:81px)',False),('(max-height:80px)',True),('(max-height:79px)',False)]:
 p=run(base+'@media '+query+'{.box{background-color:#0000ff}}');assert p(5,5)==((0,0,255,255) if active else (255,0,0,255))
for typ,color in [('all',(0,0,255,255)),('screen',(0,0,255,255)),('print',(255,0,0,255))]:
 p=run(base+'@media '+typ+'{.box{background-color:#0000ff}}');assert p(5,5)==color
p=run(base+'@media all{@media (min-width:700px){.box{background-color:#0000ff}}}');assert p(5,5)==(0,0,255,255)
p=run(base+'@media screen{.box{background-color:#0000ff!important}}.box{background-color:#00ff00}');assert p(5,5)==(0,0,255,255)
p=run(base+'@media screen{.box{background-color:#0000ff}}.box{background-color:#00ff00}');assert p(5,5)==(0,255,0,255)
p=run('body{display:flex}.box{width:20px;height:20px;background-color:#ff0000}.box:nth-child(2n){background-color:#00ff00}', '\n<div class="box"></div> <!--ignored-->\n<div class="box"></div>\t<div class="box"></div><div class="box"></div>')
for i in range(4):assert p(i*20+5,5)==((0,255,0,255) if i%2 else (255,0,0,255))
for expr,hits in [('3n',{3}),('4n',{4}),('-n+2',{1,2}),('0n+3',{3}),('0',set())]:
 p=run('body{display:flex}'+base+'.box:nth-child('+expr+'){background-color:#00ff00}', '<div class="box"></div>'*4)
 for i in range(4):assert p(i*20+5,5)==((0,255,0,255) if i+1 in hits else (255,0,0,255)),(expr,i,p(i*20+5,5))
p=run(r'body{display:flex}'+base+r'.box:\6e th-child(2n){background-color:#00ff00}', '<div class="box"></div>'*4)
assert p(25,5)==(0,255,0,255) and p(5,5)==(255,0,0,255)
for alpha,value in [('0',255),('.07',237),('.08',235),('1',0),('8%',235)]:
 p=run('.box{width:10px;height:10px;margin-left:10px;background-color:#ff0000;box-shadow:20px 0px 0px rgba(0,0,0,'+alpha+')}')
 assert all(abs(c-value)<=1 for c in p(35,5)[:3]),(alpha,p(35,5));assert p(35,5)[3]==255
# Nonzero blur emits a genuine soft exterior shadow; broad center approaches
# alpha .08 while the edge and far exterior remain lighter.
p=run('.box{width:30px;height:30px;background-color:#ff0000;box-shadow:50px 0px 4px rgba(0,0,0,.08)}')
assert all(abs(c-235)<=1 for c in p(65,15)[:3])
assert p(50,15)[0]>p(65,15)[0] and p(100,15)==(255,255,255,255)
p=run('.box{width:10px;height:10px;background-color:rgba(100%,0%,0%,50%)}');assert p(5,5)==(255,127,127,255) or p(5,5)==(255,128,128,255)
p=run('@media screen{.box{padding:calc(2px + 3px) max(10px,2%);width:20px;height:20px;background-color:#ff0000}.child{width:5px;height:5px;background-color:#00ff00}}','<div class="box"><div class="child"></div></div>',width=500)
assert p(10,5)==(0,255,0,255) and p(9,5)==(255,0,0,255)
for css in [
 '.box,???{background-color:rgba(0,0,0,.5)}', '.box:is(.box,???){background-color:rgba(0,0,0,.5)}',
 '.box:nth-child(999999999999999999999){color:red}', '.box:nth-child(2n junk){color:red}',
 '@media print and (unknown:1px){'+base+'}', '@media all, (unknown:1px){'+base+'}',
 '@media not print{'+base+'}', '@media only screen{'+base+'}', '@media (width:2em){'+base+'}',
 '@media (width:-1px){'+base+'}', '@media (width:1e999px){'+base+'}', '@media (width >= 2px){'+base+'}',
 '@media print{.box{width:fit-content(2px)}}', '@media print{.box{background:url(secret)}}',
 '@media print{@supports (display:flex){'+base+'}}', '@keyframes a{from{color:red}}',
 '.box{background-color:rgba(1,2,3,.5) junk}', '.box{background-color:rgba(1%,2,3,.5)}',
 '.box{background-color:rgba(1,2,3,1e999)}', '.box{background-color:rgba(1 2 3 / .5)}',
 '.box{box-shadow:0 0 rgba(0,0,0,.5) junk}', '.box{box-shadow:inset 0 0 rgba(0,0,0,.5)}',
 '.box{color:rgba(1,2,3)}', '.box{color:rgba(1,2,3,calc(1))}',
 '@media all{'*33+base+'}'*33, '@media screen{'+base,
]:run(css,ok=False)
# Same inventory observes ineligible content in an inactive branch.
log=run('@media print{.box{width:var(--x);background:url(secret)}}',ok=False)
assert 'name="var"' in log and 'kind=url' in log
original=pathlib.Path('tests/visual_compare/fixtures/flexbox_runtime_validation.html')
assert hashlib.sha256(original.read_bytes()).hexdigest()=='eab17055687df3296d8b911b0217dfb0429c306a6aa57ab49895937a6a34b837'
print(f'PASS H5 {count} real CLI cases, pixels, inactive analysis, metrics, original hash')
