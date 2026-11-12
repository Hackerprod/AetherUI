#!/usr/bin/env python3
"""H8 actual CSS -> native PNG and report metrics; no browser equivalence claim."""
import pathlib,subprocess,tempfile,sys,hashlib
binary=str(pathlib.Path(sys.argv[1]).resolve())
temp=None
if len(sys.argv)>2:
 root=pathlib.Path(sys.argv[2]);root.mkdir()
else:
 temp=tempfile.TemporaryDirectory(prefix='h8-');root=pathlib.Path(temp.name)
count=0
body='<div class="outer"><div class="box"><div class="a"></div><div class="b"></div></div></div>'
base='body{margin:0;background-color:#ffffff}.outer{width:200px}.box{display:flex;flex-wrap:wrap;background-color:#00ff00}.a{width:40px;height:20px;flex:none;background-color:#ff0000}.b{width:60px;height:20px;flex:none;background-color:#0000ff}'
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
for keyword,width in [('min-content',60),('max-content',100)]:
 same('.box{width:'+keyword+'}',f'.box{{width:{width}px}}')
 same('.box{width:'+keyword+';padding:10px}',f'.box{{width:{width}px;padding:10px}}')
 same('.box{box-sizing:border-box;width:'+keyword+';padding:10px}',f'.box{{box-sizing:border-box;width:{width+20}px;padding:10px}}')
for available,expected in [(30,60),(80,80),(140,100)]:
 same(f'.outer{{width:{available}px}}.box{{width:fit-content}}',f'.outer{{width:{available}px}}.box{{width:{expected}px}}')
for margin,expected in [(10,60),(-10,100)]:
 same(f'.outer{{width:80px}}.box{{width:fit-content;margin:0 {margin}px}}',f'.outer{{width:80px}}.box{{width:{expected}px;margin:0 {margin}px}}')
for keyword,width in [('min-content',60),('max-content',100),('fit-content',100)]:
 same('.box{width:20px;min-width:'+keyword+'}',f'.box{{width:{width}px}}')
 same('.box{width:150px;max-width:'+keyword+'}',f'.box{{width:{width}px}}')
 same('.outer{display:flex}.box{flex:0 0 '+keyword+'}',f'.outer{{display:flex}}.box{{flex:0 0 {width}px}}')
# Positioned keyword sizing must match its explicit-size native geometry.
for position,inset in [('absolute',60),('fixed',110)]:
 for keyword,width in [('min-content',60),('max-content',100),('fit-content',80)]:
  prefix=f'.outer{{position:relative}}.box{{position:{position};left:{inset}px;right:{inset}px;'
  same(prefix+'width:'+keyword+'}',prefix+f'width:{width}px'+'}')
for sizing,expected in [('content-box',80),('border-box',120)]:
 prefix=f'.outer{{position:relative}}.box{{position:absolute;left:40px;right:40px;padding:10%;box-sizing:{sizing};'
 same(prefix+'width:fit-content}',prefix+f'width:{expected}px'+'}')
same('.box{width:20px;min-width:max-content;max-width:min-content}', '.box{width:100px}')
same(r'.box{width:m\69 n-content}', '.box{width:60px}')
for direction in ['column','column-reverse']:
 run('.outer{display:flex;flex-direction:'+direction+'}.box{flex-basis:min-content}',False)
for css in ['.box{width:min-content junk}', '.box{width:fit-content(80px)}','.box{width:calc-size(min-content,size)}',r'.box{wr\69 ting-mode:vertical-rl;width:min-content}',r'.box{d\69 rection:rtl;width:min-content}','@media print{.box{writing-mode:vertical-rl}}']:
 run(css,False)
# No substitution of the full original by this text-free geometry fixture.
original=pathlib.Path('tests/visual_compare/fixtures/flexbox_runtime_validation.html')
assert hashlib.sha256(original.read_bytes()).hexdigest()=='eab17055687df3296d8b911b0217dfb0429c306a6aa57ab49895937a6a34b837'
out=root/'original';p=subprocess.run([binary,'--geometry',str(original),str(out),'300','160','1'],capture_output=True)
assert p.returncode==3 and not (out/'aether.png').exists()
assert 'TEXT_RENDERER_UNAVAILABLE' in (out/'report.log').read_text()
print(f'PASS H8 {count} CSS/PNG equivalence, error and metric cases; original rejected unchanged')
