#!/usr/bin/env python3
"""Direct production-flag shared build for hosts without CMake. Run at repo root."""
import concurrent.futures, hashlib, json, pathlib, re, subprocess, sys
root=pathlib.Path.cwd(); out=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else 'build').resolve(); out.mkdir(parents=True,exist_ok=True)
base=subprocess.check_output(['git','show','d0064fa:native/CMakeLists.txt'],text=True)
sources=re.search(r'add_library\(aether_core SHARED(.*?)\n\)',base,re.S).group(1).split()
extra=['src/abi/aether_surface.cpp','src/rendering/native_frame.cpp','src/rendering/preparation/paint_snapshot.cpp','src/rendering/preparation/aether_command_source.cpp','src/rendering/preparation/native_command_source.cpp','src/rendering/software/software_renderer.cpp']
sources+=extra
current=re.search(r'add_library\(aether_core SHARED(.*?)\n\)',pathlib.Path('native/CMakeLists.txt').read_text(),re.S).group(1).split()
if len(current)!=len(sources) or set(current)!=set(sources):
    raise SystemExit('aether_core source closure differs from reviewed A2; stop for review')
flags=['-std=c++20','-O2','-g','-fPIC','-fvisibility=hidden','-fvisibility-inlines-hidden','-DAETHER_EXPORTS','-Wall','-Wextra','-pedantic','-ffp-contract=off','-pthread','-Inative/include','-Inative/src/rendering/preparation/include']
objects=out/'objects'; objects.mkdir(exist_ok=True)
def build(s):
    obj=objects/(s.replace('/','_')+'.o'); cmd=['g++',*flags,'-c','native/'+s,'-o',str(obj)]
    with (objects/(obj.name+'.log')).open('w') as log:
        subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True)
    return str(obj)
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    objs=list(pool.map(build,sources))
link=['g++','-shared','-pthread','-Wl,--no-undefined',*objs,'-o',str(out/'libaether_core.so')]
subprocess.run(link,check=True)
def sha(p): return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
manifest={'base':subprocess.check_output(['git','rev-parse','d0064fa'],text=True).strip(),'compiler':subprocess.check_output(['g++','--version'],text=True),'compile_flags':flags,'link_command':link,'sources':{s:sha('native/'+s) for s in sources},'headers':{str(p):sha(p) for p in sorted(pathlib.Path('native').rglob('*.h'))+sorted(pathlib.Path('native').rglob('*.hpp'))},'artifact':{'path':str(out/'libaether_core.so'),'sha256':sha(out/'libaether_core.so')},'cmake':'NOT_RUN unavailable','windows':'NOT_RUN Linux host'}
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
symbols=subprocess.check_output(['nm','-D','--defined-only',str(out/'libaether_core.so')],text=True)
(out/'symbols.txt').write_text(symbols)
for symbol in ('create','destroy','resize','render','copy_frame','frame_info_size'):
    assert ' aether_surface_'+symbol+'\n' in symbols
assert 'surface_test' not in symbols and 'FrameStoreTestAccess' not in symbols
print('PASS shared DSO build',manifest['artifact'])
