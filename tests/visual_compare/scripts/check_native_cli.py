#!/usr/bin/env python3
"""Manual integration matrix. No build, network, CI or publication actions.
Usage: check_native_cli.py EXECUTABLE FIXTURES OUTPUT_NEW_DIRECTORY
Uses only the Python standard library. PNG pixels are checked by the separate oracle.
"""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

exe, fixtures, output = [Path(p).resolve() for p in sys.argv[1:]]
output.mkdir()  # Exclusive; never overwrite evidence.
source = output / 'inputs'
shutil.copytree(fixtures / 'native', source)
regular = 'fonts/DejaVuSans.ttf'
bold = 'fonts/DejaVuSans-Bold.ttf'

def digest(data):
    return hashlib.sha256(data).hexdigest()

def run(name, expected, *, html='profile2-static-controls.html', profile='2', scale='1',
        viewport='480x320', manifest='resources.json', bold_path=bold, legacy=False):
    dest = output / name
    args = [str(exe), 'capture', '--html', str(source/html), '--resource-root', str(source),
            '--manifest', str(source/manifest), '--out', str(dest), '--viewport', viewport, '--scale', scale]
    if not legacy:
        args += ['--backend', 'native-html-ltr', '--profile', profile,
                 '--regular-font', regular, '--bold-font', bold_path]
    process = subprocess.run(args, capture_output=True, text=True)
    (output / (name+'.process.json')).write_text(json.dumps({'args': args, 'exit':process.returncode,
        'stdout':process.stdout, 'stderr':process.stderr}, indent=2))
    assert process.returncode == expected, (name, process.returncode, process.stderr)
    report = json.loads((dest/'report.json').read_text())
    marker = json.loads((dest/'manifest.json').read_text())
    assert marker['run_id'] == report['run_id'] and marker['exit_code'] == expected
    assert marker['schema_version'].endswith('/1' if legacy else '/2')
    for artifact in marker['artifacts']:
        data = (dest/artifact['name']).read_bytes()
        assert len(data) == artifact['size_bytes'] and digest(data) == artifact['sha256']
    assert (dest/'aether.png').exists() == (expected == 0)
    if expected == 0:
        assert report['requested_scope'] == 'native_profile_capture'
        stages = {s['name']:s for s in report['stages']}
        for s in ['webview2_capture', 'comparison', 'coverage']:
            assert stages[s]['status'] == 'not_run'
        assert stages['aether_capture']['status'] == 'passed'
        assert report['input']['sha256'] == digest((source/html).read_bytes())
    print(name, process.returncode, report['overall']['status'])
    return report

run('profile2', 0)
run('profile2-repeat', 0)
assert (output/'profile2/aether.png').read_bytes() == (output/'profile2-repeat/aether.png').read_bytes()
run('profile1', 0, html='profile1-transparent.html', profile='1')
run('clip', 0, viewport='120x80')
for scale in ['1.25', '1.5', '2']:
    run('scale-'+scale, 10, scale=scale)
run('viewport-limit', 8, viewport='2049x320')
run('font-selection', 6, bold_path='fonts/missing.ttf')
run('legacy', 11, legacy=True)
(source/'bad-utf8.html').write_bytes(b'\xc0\xaf')
run('utf8', 4, html='bad-utf8.html')
(source/'large.html').write_bytes(b'x'*65537)
run('html-limit', 4, html='large.html')
(source/'symlink.html').symlink_to(source/'profile2-static-controls.html')
run('symlink', 4, html='symlink.html')
original=json.loads((source/'resources.json').read_text())
for name, field, value in [('hash-mismatch','sha256','0'*64), ('size-mismatch','size_bytes',1),
                            ('resource-type','role','image'), ('font-format','mime','font/otf')]:
    m=json.loads(json.dumps(original));m['entries'][0][field]=value
    (source/(name+'.json')).write_text(json.dumps(m))
    run(name, 6 if name in ['resource-type','font-format'] else 4, manifest=name+'.json')
m=json.loads(json.dumps(original));m['entries'][0]['path']='../outside.ttf'
(source/'traversal.json').write_text(json.dumps(m));run('traversal',4,manifest='traversal.json')
(source/'extra.txt').write_bytes(b'unsupported')
m=json.loads(json.dumps(original));m['entries'].append({'path':'extra.txt','role':'other',
    'mime':'application/octet-stream','size_bytes':11,'sha256':digest(b'unsupported')})
(source/'extra.json').write_text(json.dumps(m));run('extra',6,manifest='extra.json')
for name in ['solid_text.html', 'flexbox_runtime_validation.html']:
    matches=list(fixtures.rglob(name))
    assert matches, name
    shutil.copyfile(matches[0],source/name)
    r=run('original-'+name,2 if name=='solid_text.html' else 4,html=name)
    if name=='solid_text.html':
        assert r['native_capture']['metadata']['public_status'] == -6
    else:
        assert r['overall']['primary_error'] == 'INPUT_UNREADABLE'
    assert (source/name).read_bytes()==matches[0].read_bytes()
# Existing output is rejected without overwriting the original accepted evidence.
accepted=(output/'profile2/manifest.json').read_bytes()
args=json.loads((output/'profile2.process.json').read_text())['args']
assert subprocess.run(args,capture_output=True).returncode==13
assert (output/'profile2/manifest.json').read_bytes()==accepted
print('PASS native CLI integration; capture only, not browser parity')
