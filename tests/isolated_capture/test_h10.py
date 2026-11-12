#!/usr/bin/env python3
"""H10 identity transform CLI: explicit pixel controls, no browser execution."""
import pathlib,subprocess,tempfile,sys,hashlib,struct,zlib
binary=str(pathlib.Path(sys.argv[1]).resolve()); root=pathlib.Path(sys.argv[2]);root.mkdir();count=0
base='body{margin:0;background-color:white}div{display:block}.outer{padding:20px}.p{width:200px;height:100px;padding:10px;border:2px solid blue;background-color:green}.a{position:fixed;left:10%;top:20%;width:20px;height:10px;background-color:red}'
body='<div class="outer"><div class="p"><div class="middle"><div class="a"></div></div></div></div>'
def run(css,ok=True,content=body):
 global count
 count+=1;src=root/f'{count}.html';out=root/str(count);src.write_text('<html><head><style>'+base+css+'</style></head><body>'+content+'</body></html>')
 p=subprocess.run([binary,'--geometry',str(src),str(out),'300','180','1'],capture_output=True,text=True);log=(out/'report.log').read_text();assert p.returncode==(0 if ok else 3),(count,css,log)
 fields=dict(x.split('=',1) for x in log.splitlines() if '=' in x)
 for key in ['child_cpu_seconds','child_peak_rss_kib','process_wall_ms']:assert float(fields[key])>=0
 assert 'chromium=NOT_RUN' in log and 'authored_css_validation=NOT_RUN' in log
 if not ok:assert not(out/'aether.png').exists();return
 data=(out/'aether.png').read_bytes();i=8;compressed=b''
 while i<len(data):
  n=struct.unpack('>I',data[i:i+4])[0];tag=data[i+4:i+8];chunk=data[i+8:i+8+n];i+=n+12
  if tag==b'IDAT':compressed+=chunk
 raw=zlib.decompress(compressed);stride=300*4+1;assert all(raw[y*stride]==0 for y in range(180))
 def pixel(x,y):return tuple(raw[y*stride+1+4*x:y*stride+5+4*x])
 return pixel
for value in ['translateZ(0)','translateZ(0px)','translateZ(-0.00px)','TRANSLATEZ(0)',r'translat\65 Z(0p\78 )','translateZ(/*x*/0/*y*/)']:
 for pos in ['fixed','absolute']:
  px=run('.p{transform:'+value+'}.a{position:'+pos+'}')
  assert px(45,47)==(255,0,0,255),(value,pos,px(45,47));assert px(31,37)!=(255,0,0,255)
px=run('');assert px(31,37)==(255,0,0,255)
px=run('.p{transform:translateZ(0);position:relative}.a{left:auto;top:auto;right:8px;bottom:8px}')
assert px(215,125)==(255,0,0,255)
# Static identity context paints after ordinary in-flow sibling despite DOM order.
content='<div class="p"></div><div class="s"></div>'
css='.p{transform:translateZ(0);width:80px;height:40px;padding:0;border:0;background-color:red}.s{width:80px;height:40px;margin-top:-40px;background-color:blue}'
px=run(css,content=content);assert px(10,10)==(255,0,0,255)
px=run(css+'.p{transform:none}',content=content);assert px(10,10)==(0,0,255,255)
for z,expected in [('-1',(255,0,0,255)),('1',(0,0,255,255)),('0',(0,0,255,255))]:
 px=run(css+'.s{position:relative;z-index:'+z+'}',content=content);assert px(10,10)==expected
px=run(css+'.p{opacity:.5}',content=content);assert px(10,10)[:3] in [(128,0,127),(127,0,128)]
# Clipping at a transformed scrolling ancestor; zero offset CLI captures.
px=run('.p{transform:translateZ(0);overflow:hidden}.middle{height:400px}.a{left:20px;top:115px;height:20px}')
assert px(43,140)==(255,0,0,255) and px(43,146)!=(255,0,0,255)
# Root/document scrolling and escape from an intermediate scroller remain rejected.
run('.p{transform:translateZ(0);height:800px}',False)
run('.p{transform:translateZ(0)}.middle{overflow:hidden;height:30px}.a{top:50px}',False)
for bad in ['translateZ(1px)' ,'translateZ(0%)','translateZ(1e-999px)','translateZ(0em)','translateZ(calc(0px))','translateZ(0) scale(1)','translateZ(0) translateZ(0)']:
 run('.p{transform:'+bad+'}',False)
for display in ['inline','inline-block','contents']:
 run('.p{display:'+display+';transform:translateZ(0)}',False)
run('body{transform:translateZ(0)}',False)
run('.outer{display:flex}.p{transform:translateZ(0);z-index:1}',False)
run('.p{--t:translateZ(0);transform:var(--t)}',False)
run('.p{transform:translateZ(0);transform-style:preserve-3d}',False)
run('.p{transform:translateZ(0);perspective:100px}',False)
run('.p{transform:translateZ(0);rotate:10deg}',False)
run('.p{transform:translateZ(0)}.middle{overflow:hidden;height:30px}.a{position:absolute;top:50px}',False)
# Independent review regression: OOF block follows the preceding block,
# not its inline right edge; opacity/transform snapshots keep this anchor.
for transform in ['none','translateZ(0)']:
 for pos in ['absolute','fixed']:
  px=run('.p{transform:'+transform+'}.before{width:100px;height:40px}.a{position:'+pos+';left:auto;right:auto;top:auto;bottom:auto}',content='<div class="outer"><div class="p"><div class="before"></div><div class="a"></div></div></div>')
  assert px(33,73)==(255,0,0,255) and px(133,33)!=(255,0,0,255)
original=pathlib.Path('tests/visual_compare/fixtures/flexbox_runtime_validation.html');assert hashlib.sha256(original.read_bytes()).hexdigest()=='eab17055687df3296d8b911b0217dfb0429c306a6aa57ab49895937a6a34b837'
p=subprocess.run([binary,'--geometry',str(original),str(root/'original'),'300','180','1'],capture_output=True);assert p.returncode==3
print(f'H10 CLI PNG/metric/rejection cases={count}; original rejected intact')
