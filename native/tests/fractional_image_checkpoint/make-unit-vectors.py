from pathlib import Path
from fractions import Fraction as F
import json,random,math
O=Path(__file__).resolve().parent;rng=random.Random(20261116)
def half(v):return (2*v.numerator+v.denominator)//(2*v.denominator)
def sample(t):
 s,d,b,op,w,h,full,x,y,p=t
 u=s[0]+(F(2*x+1,2)-d[0])*(s[2]-s[0])/(d[2]-d[0])-F(1,2)
 v=s[1]+(F(2*y+1,2)-d[1])*(s[3]-s[1])/(d[3]-d[1])-F(1,2)
 l,top,right,bottom=(0,0,w-1,h-1) if full else (s[0],s[1],s[2]-1,s[3]-1)
 u=max(l,min(right,u));v=max(top,min(bottom,v));x0=int(u);y0=int(v);x1=min(x0+1,int(right));y1=min(y0+1,int(bottom))
 fx=u-x0;fy=v-y0;area=max(F(0),min(b[2],x+1)-max(b[0],x))*max(F(0),min(b[3],y+1)-max(b[1],y))
 result=[]
 for c in range(4):
  a=p[(y0*w+x0)*4+c]*(1-fx)+p[(y0*w+x1)*4+c]*fx
  z=p[(y1*w+x0)*4+c]*(1-fx)+p[(y1*w+x1)*4+c]*fx
  result.append(half((a*(1-fy)+z*fy)*area*op))
 return result
tex=list((O.parent/'s1-post-G2-functional-C-v3-20261116-01/resource-A.bgra').read_bytes())
cases=[]
def add(s,d,b,op=F(1),w=4,h=3,full=True,x=0,y=0,p=tex):
 t=([F(z) for z in s],[F(z) for z in d],[F(z) for z in b],F(op),w,h,full,x,y,p)
 cases.append((t,sample(t)))
# Known ties from independent F geometry, neighbors and fractional central boundaries.
for x in range(16,26):
 for y in [4,5,6,12,13]:add([0,0,4,3],[16,4,26,14],[16,4,26,14],x=x,y=y)
for w,h in [(1,1),(4,3)]:
 p=tex[:4] if w==1 else tex
 for op in [0,F(1,65536),F(1,2),F(65535,65536),1]:
  add([0,0,w,h],[-1,-1,2,2],[F(1,256),F(17,256),F(255,256),F(239,256)],op,w,h,p=p)
# Max destination coordinate ranges / clamp, negative legal coordinates.
for x,y in [(-32767,-32767),(0,0),(32766,32766)]:
 add([0,0,4,3],[F(-8388607,256)]*2+[F(8388607,256)]*2,[x,y,x+1,y+1],x=x,y=y)
for j in range(300):
 x=rng.randrange(-10,20);y=rng.randrange(-10,20)
 dx=F(rng.randrange(-2560,2560),256);dy=F(rng.randrange(-2560,2560),256)
 dw=F(rng.randrange(1,6144),256);dh=F(rng.randrange(1,6144),256)
 b=[F(x)+F(rng.randrange(128),256),F(y)+F(rng.randrange(128),256),F(x)+F(rng.randrange(128,257),256),F(y)+F(rng.randrange(128,257),256)]
 # b need not be a subset of d to test sampling+clamp, but remains a legal bounded coverage rectangle.
 full=j%2==0;s=[F(1,4),F(1,4),F(15,4),F(11,4)] if full else [1,1,3,3]
 p=[]
 for pixel in range(12):
  a=rng.randrange(256);p.extend([rng.randrange(a+1),rng.randrange(a+1),rng.randrange(a+1),a])
 add(s,[dx,dy,dx+dw,dy+dh],b,F(rng.randrange(65537),65536),full=full,x=x,y=y,p=p)
def cf(x):return float(x).hex()
lines=['// Generated independently with Fraction, frozen before build.','static const Test tests[]={']
js=[]
for t,e in cases:
 s,d,b,op,w,h,full,x,y,p=t
 lines.append('{'+','.join(['{'+','.join(map(cf,z))+'}' for z in [s,d,b]])+','+cf(op)+f',{w},{h},'+str(full).lower()+f',{x},{y},'+'{'+','.join(map(str,p))+'},{'+','.join(map(str,e))+'}},')
 js.append({'source':list(map(str,s)),'destination':list(map(str,d)),'clipped':list(map(str,b)),'opacity':str(op),'width':w,'height':h,'full':full,'x':x,'y':y,'pixels':p,'expected':e})
lines.append('};');(O/'unit-vectors.inc').write_text('\n'.join(lines)+'\n');(O/'unit-vectors.json').write_text(json.dumps(js,indent=2)+'\n')
