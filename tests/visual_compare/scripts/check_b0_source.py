#!/usr/bin/env python3
"""Installed CLI + independent C11 ABI transport integration matrix.
Usage: check_b0_source.py EXECUTABLE REPO_ROOT NEW_OUTPUT_DIR ORACLE_EXECUTABLE
No builds, network, publication, or changes to the original source fixtures.
Pillow independently decodes PNG. Pixel equality proves transport, not semantics.
"""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
from PIL import Image

exe, repo, output, oracle = (Path(p).resolve() for p in sys.argv[1:])
output.mkdir()
source=output/'inputs';source.mkdir();(source/'fonts').mkdir()
regular='fonts/DejaVuSans.ttf';bold='fonts/DejaVuSans-Bold.ttf'
fixtures=['b0_source_stylesheet_controls.html','b1_semantic_page.html','b2_inline_wrap_page.html']
def digest(data): return hashlib.sha256(data).hexdigest()
provenance=[]
for name in fixtures+[regular,bold]:
    original=repo/'native/tests/fixtures'/('b0/'+name if name in fixtures else name)
    shutil.copyfile(original,source/name)
    assert (source/name).read_bytes()==original.read_bytes()
    provenance.append({'source':str(original.relative_to(repo)), 'staged':name,'sha256':digest(original.read_bytes())})
(output/'source-provenance.json').write_text(json.dumps(provenance,indent=2))
entries=[{'path':name,'role':'font','mime':'font/ttf','size_bytes':(source/name).stat().st_size,'sha256':digest((source/name).read_bytes())} for name in [regular,bold]]
def manifest(name,items): (source/name).write_text(json.dumps({'schema_version':'aether.resources/1','entries':items}))
manifest('resources.json',entries)

def run(name, expected, html=fixtures[1], viewport='480x640', scale='1', inventory='resources.json', bold_path=bold):
    dest=output/name
    args=[str(exe),'capture','--html',str(source/html),'--resource-root',str(source),'--manifest',str(source/inventory),'--out',str(dest),'--viewport',viewport,'--scale',scale,'--backend','native-b0-source','--regular-font',regular,'--bold-font',bold_path]
    p=subprocess.run(args,capture_output=True,text=True)
    (output/(name+'.process.json')).write_text(json.dumps({'args':args,'exit':p.returncode,'stdout':p.stdout,'stderr':p.stderr},indent=2))
    assert p.returncode==expected,(name,p.returncode,p.stderr,expected)
    report=json.loads((dest/'report.json').read_text());marker=json.loads((dest/'manifest.json').read_text())
    assert report['schema_version']=='aether.visual-compare.report/3'
    assert marker['schema_version']=='aether.visual-compare.artifacts/3'
    assert report['requested_scope']==marker['requested_scope']=='native_b0_source_capture'
    assert report['run_id']==marker['run_id'] and marker['exit_code']==expected
    for a in marker['artifacts']:
        data=(dest/a['name']).read_bytes();assert len(data)==a['size_bytes'] and digest(data)==a['sha256']
    assert (dest/'aether.png').exists()==(expected==0)
    assert report['comparison']['status']=='not_run'
    if expected==0:
        assert report['input']['sha256']==digest((source/html).read_bytes())
        w,h=map(int,viewport.split('x'))
        rgba=output/(name+'.rgba')
        subprocess.run([str(oracle),str(source/html),str(source/regular),str(source/bold),str(w),str(h),str(rgba)],check=True)
        image=Image.open(dest/'aether.png');image.load()
        assert image.mode=='RGBA' and image.size==(w,h)
        assert image.tobytes()==rgba.read_bytes(),'independent C11 ABI transport mismatch'
        assert len(image.getcolors(w*h) or [])>10,'empty or unexpectedly flat capture'
    print(name,p.returncode,report['overall']['status'])
    return report

for i,fixture in enumerate(fixtures):
    run('b'+str(i)+'-full',0,fixture)
    run('b'+str(i)+'-clip',0,fixture,'240x160')
run('b2-width240',0,fixtures[2],'240x640')
run('b2-repeat',0,fixtures[2])
assert (output/'b2-full/aether.png').read_bytes()==(output/'b2-repeat/aether.png').read_bytes()
for scale in ['1.25','1.5','2']: run('scale-'+scale,10,scale=scale)
run('viewport-limit',8,viewport='2049x640')
run('missing-font-selection',6,bold_path='fonts/absent.ttf')
(source/'large.html').write_bytes(b'x'*65537);run('html-limit',4,html='large.html')
(source/'bad-utf8.html').write_bytes(b'\xc0\xaf');run('bad-utf8',4,html='bad-utf8.html')
for name,body in [('image','<img src="private-secret.png">'),('shadow','<div style="box-shadow:1px 1px red">PRIVATE_SECRET</div>'),('gradient','<div style="background-image:linear-gradient(red,blue)">PRIVATE_SECRET</div>')]:
    (source/(name+'.html')).write_text('<html><body>'+body+'</body></html>')
    run(name,10,html=name+'.html')
    assert 'PRIVATE_SECRET' not in (output/name/'report.json').read_text()
broken=[dict(e) for e in entries];broken[0]['sha256']='0'*64
manifest('mismatch.json',broken);run('hash-mismatch',4,inventory='mismatch.json')
(source/'extra.ttf').write_bytes((source/regular).read_bytes())
extra=dict(entries[0]);extra['path']='extra.ttf';manifest('extra.json',entries+[extra]);run('unconsumed',6,inventory='extra.json')
(source/'corrupt.ttf').write_bytes(b'not a font');corrupt=dict(entries[1]);corrupt.update(path='corrupt.ttf',size_bytes=10,sha256=digest(b'not a font'))
manifest('corrupt.json',[entries[0],corrupt]);run('corrupt-font',10,inventory='corrupt.json',bold_path='corrupt.ttf')
accepted=(output/'b1-full/manifest.json').read_bytes();args=json.loads((output/'b1-full.process.json').read_text())['args']
assert subprocess.run(args,capture_output=True).returncode==13
assert (output/'b1-full/manifest.json').read_bytes()==accepted
print('PASS original B0/B1/B2 capture, independent C11 pixel transport, resource and rejection matrix; NOT browser parity')
