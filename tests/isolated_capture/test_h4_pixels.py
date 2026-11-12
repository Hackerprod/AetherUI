#!/usr/bin/env python3
"""Exact controlled-fixture pixels. No Chromium equivalence claim."""
import pathlib, struct, zlib, sys
p=pathlib.Path(sys.argv[1]); b=p.read_bytes(); assert b[:8]==b'\x89PNG\r\n\x1a\n'
o=8; data=b''
while o<len(b):
 n=struct.unpack('>I',b[o:o+4])[0]; k=b[o+4:o+8]; v=b[o+8:o+8+n];o+=12+n
 if k==b'IHDR': w,h,depth,typ,_,_,_=struct.unpack('>IIBBBBB',v);assert (w,h,depth,typ)==(1280,800,8,6)
 if k==b'IDAT':data+=v
raw=zlib.decompress(data);stride=1+4*w;assert len(raw)==stride*h
assert all(raw[y*stride]==0 for y in range(h))
def px(x,y):return tuple(raw[y*stride+1+x*4:y*stride+1+x*4+4])
for x,y,color in [(50,28,(0,255,0,255)),(49,28,(255,0,0,255)),(59,37,(0,255,0,255)),(60,38,(255,0,0,255)),(149,100,(0,0,255,255)),(150,100,(255,255,255,255))]:assert px(x,y)==color,(x,y,px(x,y))
print('PASS H4 exact pixel boundaries header padding50/28 flex-basis150')
