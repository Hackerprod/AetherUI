#!/usr/bin/env python3
"""Useful-size supported-profile HTML -> native PNG; independent full pixel scan."""
import pathlib,subprocess,sys,struct,zlib,math,hashlib
binary=str(pathlib.Path(sys.argv[1]).resolve());out=pathlib.Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True)
fixtures=pathlib.Path(__file__).parent/'fixtures'
def decode(path):
 data=path.read_bytes();assert data[:8]==b'\x89PNG\r\n\x1a\n';packed=b'';i=8
 while i<len(data):
  n=struct.unpack('>I',data[i:i+4])[0];kind=data[i+4:i+8];body=data[i+8:i+8+n];i+=n+12
  if kind==b'IHDR':w,h,depth,colour,comp,filt,inter=struct.unpack('>IIBBBBB',body);assert (depth,colour,comp,filt,inter)==(8,6,0,0,0)
  if kind==b'IDAT':packed+=body
 raw=zlib.decompress(packed);assert len(raw)==h*(1+4*w);assert all(raw[y*(1+4*w)]==0 for y in range(h))
 return w,h,raw
for name,w,h,rw,rh in [('card',400,300,200,200),('background',1280,800,1280,800),('background-oblique',1280,800,1280,800)]:
 source=fixtures/('g1-gradient-card-useful.html' if name=='card' else 'g1-gradient-background-useful.html')
 if name.endswith('oblique'):
  changed=out/'oblique.html';changed.write_text(source.read_text().replace('to right','37deg'));source=changed
 target=out/name
 p=subprocess.run([binary,'--geometry',str(source),str(target),str(w),str(h),'1'],capture_output=True,text=True)
 assert p.returncode==0,(name,p.returncode,p.stderr,(target/'report.log').read_text())
 ww,hh,raw=decode(target/'aether.png');assert (ww,hh)==(w,h)
 dx,dy=(math.sin(math.radians(37)),-math.cos(math.radians(37))) if name.endswith('oblique') else (1,0)
 maxerror=0
 for y in range(h):
  for x in range(w):
   t=.5+((x+.5-rw/2)*dx+(y+.5-rh/2)*dy)/(rw*abs(dx)+rh*abs(dy));t=min(1,max(0,t))
   want=(math.floor(255*(1-t)+.5),0,math.floor(255*t+.5),255) if x<rw and y<rh else (0,0,0,0)
   start=y*(1+4*w)+1+x*4;got=raw[start:start+4];error=max(abs(a-b) for a,b in zip(got,want));assert error<=1,(name,x,y,list(got),want);maxerror=max(maxerror,error)
 data=(target/'aether.png').read_bytes();print(name,'PASS','pixels',w*h,'max_byte_error',maxerror,'png_bytes',len(data),'sha256',hashlib.sha256(data).hexdigest())
