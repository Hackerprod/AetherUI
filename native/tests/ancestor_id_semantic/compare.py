#!/usr/bin/env python3
"""Untimed semantic checks only. Run after both ordinary Release cores exist."""
import argparse, hashlib, itertools, json, os, pathlib, subprocess
HERE=pathlib.Path(__file__).resolve().parent
NATIVE=HERE.parents[1]
FIXTURE=NATIVE.parent/'bindings/dotnet/tools/AetherUI.Validator/Pages/flexbox_runtime_validation.html'
EXPECTED='eab17055687df3296d8b911b0217dfb0429c306a6aa57ab49895937a6a34b837'
def digest(p):
    h=hashlib.sha256()
    with open(p,'rb') as f:
        for b in iter(lambda:f.read(1<<20),b''):h.update(b)
    return h.hexdigest()
def compare_files(a,b):
    with open(a,'rb') as fa,open(b,'rb') as fb:
        for i,(x,y) in enumerate(itertools.zip_longest(fa,fb),1):
            if x!=y:raise AssertionError(f'{a.name}: mismatch at line {i}: {x!r} != {y!r}')
def metrics(p):
    keys={'fixture_bytes','dom_nodes','case_inventory','style_elements','css_rules','style_updates','layout_text_measure_calls','layout_text_measure_bytes','layout_boxes','paint_text_measure_calls','paint_text_measure_bytes','display_commands','assertions','failures','result','semantic_assertions_exercised','case_item'}
    return [line for line in p.read_text().splitlines() if line.split('=',1)[0] in keys]
def run(cmd,log,env=None):
    with open(log,'w') as f:
        f.write('COMMAND '+json.dumps([str(x) for x in cmd])+'\n');f.flush()
        result=subprocess.run([str(x) for x in cmd],stdout=f,stderr=subprocess.STDOUT,env=env)
    if result.returncode:raise RuntimeError(f'exit {result.returncode}: {log}')
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--baseline-lib',type=pathlib.Path,required=True);ap.add_argument('--candidate-lib',type=pathlib.Path,required=True);ap.add_argument('--output',type=pathlib.Path,required=True);ap.add_argument('--compiler',default='g++');args=ap.parse_args()
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    assert digest(FIXTURE)==EXPECTED,'fixture identity mismatch'
    assert digest(NATIVE/'include/aether/aether.h')==json.loads((HERE/'field_manifest.json').read_text())['header_sha256'],'regenerate/review changed ABI schema'
    manifest={'scope':'UNTIMED public fieldwise equivalence; not performance or 403-case conformance','fixture':EXPECTED,'sources':{p.name:digest(p) for p in HERE.iterdir() if p.is_file()},'arms':{},'viewports':{}}
    (out/'identity.json').write_text(json.dumps(manifest,indent=2))
    flags=['-std=c++20','-O2','-DNDEBUG','-ffp-contract=off','-I',NATIVE/'include']
    sensitivity=out/'sensitivity';run([args.compiler,*flags,HERE/'sensitivity.generated.cpp','-o',sensitivity],out/'sensitivity-build.log');run([sensitivity],out/'sensitivity.log')
    for arm,lib in [('A',args.baseline_lib),('B',args.candidate_lib)]:
        lib=lib.resolve();exe=out/('page-'+arm)
        run([args.compiler,*flags,HERE/'page.generated.cpp',lib,'-o',exe],out/(arm+'-build.log'))
        manifest['arms'][arm]={'library':str(lib),'sha256':digest(lib),'harness_sha256':digest(exe)}
        for width in [1440,768,390]:
            dest=out/(arm+'-'+str(width));env=dict(os.environ);env['LD_LIBRARY_PATH']=str(lib.parent)
            run([exe,FIXTURE,dest,width,900],out/(arm+'-'+str(width)+'.log'),env)
    for width in [1440,768,390]:
        a,b=[out/(arm+'-'+str(width)) for arm in ['A','B']]
        for name in ['semantic.txt','geometry.csv']:compare_files(a/name,b/name)
        assert metrics(a/'report.log')==metrics(b/'report.log'),f'count/assertion mismatch {width}'
        manifest['viewports'][str(width)]={name:digest(a/name) for name in ['semantic.txt','geometry.csv']}
    manifest['result']='PASS';(out/'result.json').write_text(json.dumps(manifest,indent=2)+'\n');print('PASS: all public style fields and active display-command fields, original geometry/counts/assertions at three viewports')
if __name__=='__main__': main()
