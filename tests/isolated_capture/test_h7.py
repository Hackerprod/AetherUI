#!/usr/bin/env python3
"""H7 native PNG checks: actual fixture CSS, time boundaries, rejection and metrics.
No Chromium execution or full-document semantic claim.
"""
import pathlib, subprocess, tempfile, sys, struct, zlib, hashlib, re
binary=str(pathlib.Path(sys.argv[1]).resolve())
temp=None
if len(sys.argv)>2:
 root=pathlib.Path(sys.argv[2]);root.mkdir()
else:
 temp=tempfile.TemporaryDirectory(prefix='h7-');root=pathlib.Path(temp.name)
count=0
RED=(255,0,0,255); BLUE=(0,0,255,255); WHITE=(255,255,255,255)
def run(css,body='<div class="box"></div>',ok=True):
 global count
 count+=1;source=root/f'{count}.html';out=root/str(count)
 source.write_text('<html><head><style>body{margin:0;background-color:#ffffff}'+css+'</style></head><body>'+body+'</body></html>')
 p=subprocess.run([binary,'--geometry',str(source),str(out),'600','180','1'],capture_output=True,text=True)
 log=(out/'report.log').read_text()
 assert p.returncode==(0 if ok else 3),(count,css,log)
 for item in ['css_eligibility=Unknown','chromium=NOT_RUN','authored_css_validation=NOT_RUN','flexbox_semantic_acceptance=false']:assert item in log
 fields=dict(line.split('=',1) for line in log.splitlines() if '=' in line)
 for key in ['child_cpu_seconds','child_peak_rss_kib','process_wall_ms']:assert float(fields[key])>=0
 if not ok:
  assert not (out/'aether.png').exists()
  return log
 b=(out/'aether.png').read_bytes();i=8;packed=b''
 while i<len(b):
  n=struct.unpack('>I',b[i:i+4])[0];kind=b[i+4:i+8];v=b[i+8:i+8+n];i+=n+12
  if kind==b'IDAT':packed+=v
 raw=zlib.decompress(packed);stride=600*4+1
 assert all(raw[y*stride]==0 for y in range(180))
 return lambda x,y:tuple(raw[y*stride+1+x*4:y*stride+5+x*4])

original=pathlib.Path('tests/visual_compare/fixtures/flexbox_runtime_validation.html')
assert hashlib.sha256(original.read_bytes()).hexdigest()=='eab17055687df3296d8b911b0217dfb0429c306a6aa57ab49895937a6a34b837'
source=original.read_text()
# Preserve the five original authored keyframe/declaration lines, including IDs.
# They are input selectors, not names interpreted specially by the implementation.
fixture_css={}
for n,name in [(374,'growMid'),(375,'basisMid'),(376,'gapMid'),(377,'orderMid'),(378,'directionStep')]:
 lines=source.splitlines()
 first=next(x for x in lines if x.startswith(f'#FLEX-{n} .flex-target'))
 anim=next(x for x in lines if x.startswith('@keyframes '+name+'{'))
 fixture_css[n]=first+'\n'+anim
common='.item{height:20px;min-width:0;background-color:#0000ff}.animated{background-color:#ff0000}'
def body(n):return f'<div id="FLEX-{n}"><div class="flex-target"><div class="item animated"></div><div class="item"></div></div></div>'
p=run(common+fixture_css[374],body(374));assert p(279,5)==RED and p(280,5)==BLUE and p(419,5)==BLUE and p(420,5)==WHITE
p=run(common+fixture_css[375],body(375));assert p(149,5)==RED and p(150,5)==BLUE and p(249,5)==BLUE and p(250,5)==WHITE
p=run(common+'.item{width:50px}'+fixture_css[376],body(376));assert p(49,5)==RED and p(50,5)==WHITE and p(79,5)==WHITE and p(80,5)==BLUE
p=run(common+'.item{width:50px}.item:last-child{order:-1}'+fixture_css[377],body(377));assert p(0,5)==BLUE and p(58,5)==RED
p=run(common+'.item{width:50px}'+fixture_css[378],body(378));assert p(0,5)==RED and p(50,5)==BLUE and p(0,25)==WHITE
# Arbitrary names and delays prove interpolation, not a five-case lookup.
base='.box{display:flex;width:400px;gap:7px}.item{width:20px;height:20px;background-color:#ff0000}.item:last-child{background-color:#0000ff}'
items='<div class="box"><div class="item"></div><div class="item"></div></div>'
for delay,expected in [('-2.5s',20),('-4.9s',29.6),('-5s',30),('-5.1s',30.4),('-7.5s',40),('1s',7),('-10s',7),('-11s',7)]:
 css=base+'@keyframes arbitrary{from{gap:10px}to{gap:50px}}.box{animation:arbitrary 10s linear '+delay+' paused}'
 p=run(css,items);x=int(20+expected+1);assert p(x,5)==BLUE,(delay,expected)
for delay,column in [('-4.9s',False),('-5s',True),('-5.1s',True)]:
 p=run(base+'@keyframes turn{from{flex-direction:row}to{flex-direction:column}}.box{animation:turn 10s linear '+delay+' paused}',items)
 assert p(1,28)==(BLUE if column else WHITE)
# Important style overrides animation; none removes sampled effect.
k='@keyframes k{from{gap:10px}to{gap:50px}}'
for suffix in ['gap:7px!important','animation:none']:
 p=run(base+k+'.box{animation:k 10s linear -5s paused;'+suffix+'}',items);assert p(28,5)==BLUE
p=run(base+k+'@keyframes k{from{gap:30px}to{gap:50px}}.box{animation:k 10s linear -5s paused}',items);assert p(61,5)==BLUE
for wrapper in ['@media print{RULE}','@supports not (display:flex){RULE}']:
 p=run(base+k+wrapper.replace('RULE','@keyframes k{from{gap:60px}to{gap:80px}}')+'.box{animation:k 10s linear -5s paused}',items);assert p(51,5)==BLUE
# These are outside this capture profile, not necessarily invalid CSS.
for endpoints in ['from{gap:10px}','from{gap:10px!important}to{gap:50px}','from{gap:10px}50%{gap:20px}to{gap:50px}','from{width:10px}to{width:50px}','from{flex-basis:auto}to{flex-basis:50px}','from{gap:10%}to{gap:50px}','from{gap:calc(10px)}to{gap:50px}']:
 run(base+'@keyframes k{'+endpoints+'}.box{animation:k 10s linear -5s paused}',items,False)
for value in ['k 10s linear -5s running','k 10s ease -5s paused','k 10s steps(2) -5s paused','k 10s linear -5s paused infinite','k 10s linear -5s paused alternate','k 10s linear -5s paused forwards','k 0s linear paused','k 10s linear paused,k 10s linear paused','k 10s linear -5s paused garbage']:
 run(base+k+'.box{animation:'+value+'}',items,False)
# Split longhands are admitted only if the effective per-node configuration is H7.
p=run(base+k+'.box{animation-name:k;animation-duration:10s}.box{animation-delay:-5s;animation-timing-function:linear;animation-play-state:paused}',items);assert p(51,5)==BLUE
run(base+k+'.box{animation-name:k}',items,False)
run(base+'.box{animation:missing 10s linear -5s paused}',items,False)
# Ignored important keyframe declarations do not replace earlier effective endpoints.
p=run(base+'@keyframes k{from{gap:10px;gap:90px!important}to{gap:50px}}.box{animation:k 10s linear -5s paused}',items);assert p(51,5)==BLUE
# Authored CSS names are case-sensitive; escaped tokens are decoded once.
p=run(base+r'@keyframes M\69 d{from{gap:10px}to{gap:50px}}.box{animation:Mid 10s linear -5s paused}',items);assert p(51,5)==BLUE
run(base+'@keyframes Mid{from{gap:10px}to{gap:50px}}.box{animation:mid 10s linear -5s paused}',items,False)
print(f'PASS H7 {count} actual fixture CSS/PNG, timing, gate and metrics cases')
