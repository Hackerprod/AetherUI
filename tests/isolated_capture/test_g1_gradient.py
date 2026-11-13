#!/usr/bin/env python3
"""Real HTML -> native core -> PNG. Independent analytic oracle; no browser."""
import pathlib, subprocess, sys, struct, zlib, math
binary=str(pathlib.Path(sys.argv[1]).resolve())
root=pathlib.Path(sys.argv[2]);root.mkdir(parents=True,exist_ok=True)
fixtures=pathlib.Path(__file__).parent/'fixtures'
checks=0

def load(path):
 b=path.read_bytes();i=8;packed=b'';w=h=0
 while i<len(b):
  n=struct.unpack('>I',b[i:i+4])[0];kind=b[i+4:i+8];v=b[i+8:i+8+n];i+=n+12
  if kind==b'IHDR':w,h=struct.unpack('>II',v[:8])
  if kind==b'IDAT':packed+=v
 raw=zlib.decompress(packed);stride=w*4+1
 assert all(raw[y*stride]==0 for y in range(h))
 return lambda x,y:tuple(raw[y*stride+1+x*4:y*stride+5+x*4])
def run(name,source,ok=True):
 out=root/name
 p=subprocess.run([binary,'--geometry',str(source),str(out),'40','24','1'],capture_output=True,text=True)
 log=(out/'report.log').read_text()
 assert p.returncode==(0 if ok else 3),(name,p.returncode,log,p.stderr)
 assert 'chromium=NOT_RUN' in log
 fields=dict(line.split('=',1) for line in log.splitlines() if '=' in line)
 for key in ['child_cpu_seconds','child_peak_rss_kib','process_wall_ms']:assert float(fields[key])>=0
 if not ok:
  assert not (out/'aether.png').exists()
  return
 return load(out/'aether.png')
def expect(pixel,x,y,want):
 global checks
 got=pixel(x,y);assert all(abs(a-b)<=2 for a,b in zip(got,want)),(x,y,got,want);checks+=1
def mixed(t):return (round(255*(1-t)),0,round(255*t),255)
p=run('block',fixtures/'g1-gradient-block.html')
for y in range(24):
 for x in range(40):expect(p,x,y,mixed((x+.5-3)/17) if 3<=x<20 and 2<=y<13 else (0,0,0,0))
# Explicit 100% on an oblique line guards float-vs-double preflight rounding.
source=root/'oblique.html'
source.write_text((fixtures/'g1-gradient-block.html').read_text().replace('to right','37deg').replace('red,blue','red 0%,blue 100%'))
p=run('oblique',source)
dx,dy=math.sin(math.radians(37)),-math.cos(math.radians(37))
line=17*abs(dx)+11*abs(dy)
for y in range(24):
 for x in range(40):
  t=max(0,min(1,.5+((x+.5-11.5)*dx+(y+.5-7.5)*dy)/line))
  expect(p,x,y,mixed(t) if 3<=x<20 and 2<=y<13 else (0,0,0,0))
p=run('flex',fixtures/'g1-gradient-flex.html')
for y in range(24):
 for x in range(40):expect(p,x,y,mixed((y+.5)/12) if (x<12 or 16<=x<28) and y<12 else (0,0,0,0))
# Unsupported forms MUST reject instead of the parser dropping the declaration.
for name,value in [('invalid-prelude-middle','linear-gradient(to in srgb right,red,blue)'),('implicit','linear-gradient(to right,red,blue)'),('corner','linear-gradient(to right bottom in srgb,red,blue)'),('external','linear-gradient(to right in srgb,red -10%,blue 100%)'),('external-length','linear-gradient(to right in srgb,red 0px,blue 100px)'),('oklab','linear-gradient(to right in oklab,red,blue)'),('hint','linear-gradient(in srgb,red,30%,blue)'),('repeating','repeating-linear-gradient(in srgb,red,blue)')]:
 source=root/(name+'.html');source.write_text('<html><head><style>body{margin:0}div{width:16px;height:12px;background-image:'+value+'}</style></head><body><div></div></body></html>')
 run(name,source,False)
# Strict authored profile rejects even overridden unsupported declarations.
for name,decl in [('supported-to-implicit','background-image:linear-gradient(to right in srgb,red,blue);background-image:linear-gradient(to right,red,blue)'),('implicit-to-supported','background-image:linear-gradient(to right,red,blue);background-image:linear-gradient(to right in srgb,red,blue)')]:
 source=root/(name+'.html');source.write_text('<html><head><style>body{margin:0}div{width:16px;height:12px;'+decl+'}</style></head><body><div></div></body></html>');run(name,source,False)
# Effective inheritance is covered directly by g1_gradient_native_tests.cpp;
# CLI admits inherit; validate child and parent use distinct geometry.
source=root/'inherited.html';source.write_text('<html><head><style>body{margin:0}div{width:16px;height:12px;background-image:linear-gradient(to right in srgb,red,blue)}span{display:block;width:8px;height:6px;background-image:inherit}</style></head><body><div><span></span></div></body></html>');p=run('inherited',source);expect(p,3,2,mixed(3.5/8));expect(p,3,8,mixed(3.5/16))
# HTML command -> rounded fill + isolated group opacity. Transparent red cannot
# leak red into blue interpolation; PNG is unpremultiplied RGBA.
source=root/'alpha-group.html';source.write_text('<html><head><style>body{margin:0;background-color:transparent}.group{display:block;width:20px;height:16px;opacity:.5}.box{display:block;width:16px;height:12px;border-radius:3px;background-image:linear-gradient(to right in srgb,rgba(255,0,0,0),blue)}</style></head><body><div class="group"><div class="box"></div></div></body></html>')
p=run('alpha-group',source)
for x in range(3,13):expect(p,x,6,(0,0,255,round(255*(x+.5)/16*.5)))
expect(p,0,0,(0,0,0,0))
source=root/'clip-group.html';source.write_text('<html><head><style>body{margin:0;background-color:transparent}.group{display:block;width:12px;height:9px;overflow:hidden;border-radius:2px;opacity:.5}.box{display:block;width:16px;height:12px;background-image:linear-gradient(to right in srgb,rgba(255,0,0,0),blue)}</style></head><body><div class="group"><div class="box"></div></div></body></html>')
p=run('clip-group',source)
for x in range(3,10):expect(p,x,5,(0,0,255,round(255*(x+.5)/16*.5)))
expect(p,13,5,(0,0,0,0));expect(p,5,10,(0,0,0,0))
for name,extra in [('opacity-hidden','opacity:0'),('visibility-hidden','visibility:hidden'),('display-hidden','display:none')]:
 source=root/(name+'.html');source.write_text('<html><head><style>body{margin:0}div{width:16px;height:12px;'+extra+';background-image:linear-gradient(to right in srgb,red -10%,blue)}</style></head><body><div></div></body></html>');run(name,source,False)
source=root/'tiny-tile.html';source.write_text('<html><head><style>body{margin:0}div{width:16px;height:12px;background-image:linear-gradient(to right in srgb,red,blue);background-size:0.000001px 1px;background-repeat:repeat}</style></head><body><div></div></body></html>');run('tiny-tile',source,False)
print(f'G1 HTML PNG checks={checks} block=PASS flex=PASS rejection=PASS chromium=NOT_RUN')
