#!/usr/bin/env python3
"""Capture rounded source example from an installed package at two widths.
Usage: check_rounded_source.py CLI REPOSITORY NEW_OUTPUT ORACLE
PNG decoding versus the C11 consumer proves transport, not browser parity.
Elapsed wall time is a single observation including process/CLI work, not a
benchmark. No network, builds, publication, or source fixture modifications.
"""
import hashlib,json,shutil,subprocess,time,sys
from pathlib import Path
from PIL import Image
exe,repo,out,oracle=(Path(p).resolve() for p in sys.argv[1:]); out.mkdir(); inputs=out/'rounded-inputs'; inputs.mkdir(); (inputs/'fonts').mkdir()
for name in ['DejaVuSans.ttf','DejaVuSans-Bold.ttf']:shutil.copyfile(repo/'native/tests/fixtures/fonts'/name,inputs/'fonts'/name)
source=(repo/'native/tests/fixtures/b0/source_rounded_boxes.html').read_bytes();(inputs/'rounded.html').write_bytes(source)
square=source.replace(b'border-radius:24px;',b'border-radius:0;').replace(b'border-radius:12px;',b'border-radius:0;').replace(b'border-radius:16px;',b'border-radius:0;');(inputs/'square.html').write_bytes(square)
entries=[dict(path='fonts/'+p.name,role='font',mime='font/ttf',size_bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in sorted((inputs/'fonts').glob('*.ttf'))]
(inputs/'resources.json').write_text(json.dumps(dict(schema_version='aether.resources/1',entries=entries)))
results=[]
for html,w,h in [('rounded',480,640),('rounded',320,640),('square',480,640)]:
 dest=out/f'{html}-{w}';args=[str(exe),'capture','--html',str(inputs/f'{html}.html'),'--resource-root',str(inputs),'--manifest',str(inputs/'resources.json'),'--out',str(dest),'--viewport',f'{w}x{h}','--scale','1','--backend','native-b0-source','--regular-font','fonts/DejaVuSans.ttf','--bold-font','fonts/DejaVuSans-Bold.ttf']
 t=time.perf_counter();p=subprocess.run(args,capture_output=True,text=True);elapsed=time.perf_counter()-t
 result=dict(args=args,exit=p.returncode,seconds=elapsed,stdout=p.stdout,stderr=p.stderr);results.append(result); (out/'rounded-results.json').write_text(json.dumps(results,indent=2));assert p.returncode==0,result
 rgba=out/f'{html}-{w}.rgba';subprocess.run([str(oracle),str(inputs/f'{html}.html'),str(inputs/'fonts/DejaVuSans.ttf'),str(inputs/'fonts/DejaVuSans-Bold.ttf'),str(w),str(h),str(rgba)],check=True)
 image=Image.open(dest/'aether.png'); image.load(); assert image.mode=='RGBA' and image.size==(w,h);assert image.tobytes()==rgba.read_bytes()
 print(html,w,'PNG/C11 byte equality',elapsed)
(out/'rounded-provenance.json').write_text(json.dumps([dict(path=str(p.relative_to(inputs)),sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in inputs.rglob('*') if p.is_file()],indent=2))
