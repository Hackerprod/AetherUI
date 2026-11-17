#!/usr/bin/env python3
"""Run private injection harness with authentic objects, never production hooks.

Usage: python3 tests/native_surface/run_failures.py [build-directory] [--sanitize]
The default reuses the exact DSO objects except the test-enabled wrapper.
--sanitize recompiles the complete source closure with ASan+UBSan.
"""
import concurrent.futures, hashlib, json, os, pathlib, subprocess, sys
build=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else 'build').resolve()
sanitize='--sanitize' in sys.argv
manifest=json.loads((build/'manifest.json').read_text())
out=build/('failure-sanitized' if sanitize else 'failure-private'); out.mkdir(exist_ok=True)
flags=manifest['compile_flags'][:]
if sanitize:
    flags=[x for x in flags if x!='-O2']+['-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer']
wrapper='src/abi/aether_surface.cpp'
def compile(source):
    obj=out/(source.replace('/','_')+'.o')
    command=['g++',*flags,*(['-DAETHER_SURFACE_TESTING'] if source==wrapper else []),'-c','native/'+source,'-o',str(obj)]
    subprocess.run(command,check=True)
    return str(obj)
if sanitize:
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        objects=list(pool.map(compile,manifest['sources']))
else:
    objects=[str(build/'objects'/(s.replace('/','_')+'.o')) for s in manifest['sources'] if s!=wrapper]
    objects.append(compile(wrapper))
exe=out/'test_native_surface_failures'
command=['g++',*flags,'-DAETHER_SURFACE_TESTING','native/tests/test_native_surface_failures.cpp',*objects,'-o',str(exe)]
subprocess.run(command,check=True)
env=os.environ.copy()
if sanitize:
    env.setdefault('ASAN_OPTIONS','detect_leaks=1:halt_on_error=1')
    env.setdefault('UBSAN_OPTIONS','halt_on_error=1:print_stacktrace=1')
subprocess.run([str(exe)],env=env,check=True)
def sha(path): return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()
(out/'manifest.json').write_text(json.dumps({
    'sanitized_full_closure':sanitize, 'flags':flags, 'command':command,
    'ASAN_OPTIONS':env.get('ASAN_OPTIONS'), 'UBSAN_OPTIONS':env.get('UBSAN_OPTIONS'),
    'production_dso_sha256':manifest['artifact']['sha256'],
    'inputs':{p:sha(p) for p in objects+['native/tests/test_native_surface_failures.cpp','native/src/abi/aether_surface.cpp','native/src/abi/surface_test_hooks.h']},
    'executable_sha256':sha(exe), 'result':'PASS'
},indent=2)+'\n')
print('PASS private injection harness; full closure sanitized =',sanitize)
